/*
 * SPDX-License-Identifier: MIT
 * RepeaterRoom - Virtual Room Server running inside RepeaterMesh.
 *
 * Uses a secondary LocalIdentity (_room_id) with distinct first pubkey byte
 * (ADV_TYPE_ROOM) while RepeaterMesh handles all mesh forwarding on self_id.
 */

#include "RepeaterMesh.h"

#include <Serial.h>
#include <adapters/gps/ZephyrGPSManager.h>
#include <adapters/radio/LoRaRadio.h>
#include <adapters/sensors/ZephyrSensorManager.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/VirtualRoom.h>
#include <mesh/Utils.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zephcore_vroom, CONFIG_ZEPHCORE_MAIN_LOG_LEVEL);

static_assert(vroom::ROLE_MASK == PERM_ACL_ROLE_MASK, "vroom role mask mismatch");
static_assert(vroom::ROLE_GUEST == PERM_ACL_GUEST, "vroom guest role mismatch");
static_assert(vroom::ROLE_READ_ONLY == PERM_ACL_READ_ONLY, "vroom ro role mismatch");
static_assert(vroom::ROLE_READ_WRITE == PERM_ACL_READ_WRITE, "vroom rw role mismatch");
static_assert(vroom::ROLE_ADMIN == PERM_ACL_ADMIN, "vroom admin role mismatch");

#define FIRMWARE_VER_LEVEL          2

#define REQ_TYPE_GET_STATUS         0x01
#define REQ_TYPE_KEEP_ALIVE         0x02
#define REQ_TYPE_GET_TELEMETRY_DATA 0x03
#define REQ_TYPE_GET_ACCESS_LIST    0x05
#define REQ_TYPE_GET_OWNER_INFO     0x07

#define RESP_SERVER_LOGIN_OK        0

#define CLI_REPLY_DELAY_MILLIS      600
#define LAZY_CONTACTS_WRITE_DELAY   5000
#define SERVER_RESPONSE_DELAY       300
#define TXT_ACK_DELAY               200

#define PUSH_NOTIFY_DELAY_MILLIS    1000
#define SYNC_PUSH_INTERVAL          1200
#define PUSH_ACK_TIMEOUT_FLOOD      12000
#define PUSH_TIMEOUT_BASE           4000
#define PUSH_ACK_TIMEOUT_FACTOR     2000
#define POST_SYNC_DELAY_SECS        2
#define ROOM_ADVERT_STAGGER_MS      30000

struct VirtualRoomStats {
	uint16_t batt_milli_volts;
	uint16_t curr_tx_queue_len;
	int16_t noise_floor;
	int16_t last_rssi;
	uint32_t n_packets_recv;
	uint32_t n_packets_sent;
	uint32_t total_air_time_secs;
	uint32_t total_up_time_secs;
	uint32_t n_sent_flood, n_sent_direct;
	uint32_t n_recv_flood, n_recv_direct;
	uint16_t err_events;
	int16_t last_snr;
	uint16_t n_direct_dups, n_flood_dups;
	uint16_t n_posted, n_post_push;
};

static inline unsigned long roomFutureMillis(uint32_t delta_ms)
{
	return k_uptime_get() + delta_ms;
}

static inline bool roomMillisPassed(unsigned long target)
{
	return (int64_t)k_uptime_get() >= (int64_t)target;
}

static bool roomSaveFilter(ClientInfo *client)
{
	uint8_t role = client->permissions & PERM_ACL_ROLE_MASK;
	return role == PERM_ACL_ADMIN || role == PERM_ACL_READ_WRITE || role == PERM_ACL_READ_ONLY;
}

void RepeaterMesh::roomBegin()
{
	_room_id_loaded = false;
	_room_next_post_idx = 0;
	_room_next_client_idx = 0;
	_room_next_push = 0;
	_room_dirty_acl_expiry = 0;
	_room_next_local_advert = 0;
	_room_next_flood_advert = 0;
	_room_num_posted = 0;
	_room_num_post_pushes = 0;
	for (int i = 0; i < MAX_UNSYNCED_POSTS; i++) {
		_room_posts[i].clear();
	}

	if (!_prefs.vroom.enabled || !_store) {
		return;
	}
	if (!roomEnsureIdentity()) {
		LOG_ERR("Virtual room: failed to initialize secondary identity");
		return;
	}
	roomLoadAcl();
	LOG_INF("Virtual room active (%s)", vroom::accessName(_prefs.vroom.access));
}

bool RepeaterMesh::roomEnsureIdentity()
{
	if (_room_id_loaded && vroom::isValidSecondaryHash(self_id.pub_key, _room_id.pub_key)) {
		return true;
	}
	if (_store && _store->loadRoomIdentity(_room_id) &&
	    vroom::isValidSecondaryHash(self_id.pub_key, _room_id.pub_key)) {
		_room_id_loaded = true;
		return true;
	}
	for (int attempt = 0; attempt < 64; attempt++) {
		mesh::LocalIdentity candidate(getRNG());
		if (vroom::isValidSecondaryHash(self_id.pub_key, candidate.pub_key)) {
			_room_id = candidate;
			_room_id_loaded = true;
			if (_store && !_store->saveRoomIdentity(_room_id)) {
				LOG_WRN("Virtual room: failed to persist room.id");
			}
			return true;
		}
	}
	return false;
}

void RepeaterMesh::roomLoadAcl()
{
	_room_acl.load(_store->getFS(), _room_id);
	for (int i = 0; i < _room_acl.getNumClients(); i++) {
		_room_acl.getClientByIdx(i)->last_timestamp = _replay_floor;
	}
	_room_next_client_idx = 0;
}

void RepeaterMesh::roomSaveAcl()
{
	if (_store) {
		_room_acl.save(_store->getFS(), roomSaveFilter);
	}
	_room_dirty_acl_expiry = 0;
}

mesh::Packet *RepeaterMesh::roomCreateSelfAdvert()
{
	char room_name[32];
	vroom::formatRoomName(_prefs.node_name, _prefs.vroom.access, room_name, sizeof(room_name));

	uint8_t app_data[MAX_ADVERT_DATA_SIZE];
	uint8_t app_data_len;
	if (_prefs.advert_loc_policy == ADVERT_LOC_NONE) {
		AdvertDataBuilder builder(ADV_TYPE_ROOM, room_name);
		app_data_len = builder.encodeTo(app_data);
	} else if (_prefs.advert_loc_policy == ADVERT_LOC_SHARE) {
		AdvertDataBuilder builder(ADV_TYPE_ROOM, room_name, sensors.node_lat, sensors.node_lon);
		app_data_len = builder.encodeTo(app_data);
	} else {
		AdvertDataBuilder builder(ADV_TYPE_ROOM, room_name, _prefs.node_lat, _prefs.node_lon);
		app_data_len = builder.encodeTo(app_data);
	}
	return createAdvert(_room_id, app_data, app_data_len);
}

void RepeaterMesh::roomSendSelfAdvert(int delay_millis, bool flood)
{
	if (!_prefs.vroom.enabled || !_room_id_loaded) {
		return;
	}
	mesh::Packet *pkt = roomCreateSelfAdvert();
	if (pkt) {
		if (flood) {
			sendFloodScoped(default_scope, pkt, delay_millis, _prefs.path_hash_mode + 1);
		} else {
			sendZeroHop(pkt, delay_millis);
		}
	}
}

void RepeaterMesh::roomScheduleStaggeredAdvert(bool flood)
{
	if (!_prefs.vroom.enabled || !_room_id_loaded) {
		return;
	}
	if (flood) {
		_room_next_flood_advert = roomFutureMillis(ROOM_ADVERT_STAGGER_MS);
	} else {
		_room_next_local_advert = roomFutureMillis(ROOM_ADVERT_STAGGER_MS);
	}
}

void RepeaterMesh::roomOnAccessChanged()
{
	if (!_room_id_loaded) {
		return;
	}
	/* Private removes every non-admin client; read-only turns read-write into guest. */
	bool dirty = false;
	for (int i = _room_acl.getNumClients() - 1; i >= 0; i--) {
		ClientInfo *c = _room_acl.getClientByIdx(i);
		if (c->isAdmin()) {
			continue;
		}
		if (_prefs.vroom.access == vroom::ACCESS_PRIVATE) {
			_room_acl.applyPermissions(_room_id, c->id.pub_key, PUB_KEY_SIZE, PERM_ACL_GUEST);
			dirty = true;
		} else if (_prefs.vroom.access == vroom::ACCESS_READ_ONLY &&
			   (c->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_READ_WRITE) {
			c->permissions = PERM_ACL_GUEST;
			dirty = true;
		}
	}
	if (_room_next_client_idx >= _room_acl.getNumClients()) {
		_room_next_client_idx = 0;
	}
	if (dirty) {
		roomSaveAcl();
	}
	if (_prefs.vroom.enabled) {
		roomSendSelfAdvert(2000, true);
	}
}

void RepeaterMesh::roomAddPost(const mesh::Identity &author, const char *postData)
{
	if (!postData || postData[0] == '\0') {
		return;
	}
	_room_posts[_room_next_post_idx].author = author;
	strncpy(_room_posts[_room_next_post_idx].text, postData, MAX_POST_TEXT_LEN);
	_room_posts[_room_next_post_idx].text[MAX_POST_TEXT_LEN] = '\0';
	_room_posts[_room_next_post_idx].post_timestamp = getRTCClock()->getCurrentTimeUnique();
	_room_next_post_idx = (_room_next_post_idx + 1) % MAX_UNSYNCED_POSTS;

	_room_next_push = roomFutureMillis(PUSH_NOTIFY_DELAY_MILLIS);
	_room_num_posted++;
}

void RepeaterMesh::roomPushPostToClient(ClientInfo *client, RoomPostInfo &post)
{
	int len = 0;
	memcpy(&reply_data[len], &post.post_timestamp, 4);
	len += 4;

	uint8_t attempt;
	getRNG()->random(&attempt, 1);
	reply_data[len++] = (TXT_TYPE_SIGNED_PLAIN << 2) | (attempt & 3);

	memcpy(&reply_data[len], post.author.pub_key, 4);
	len += 4;

	int text_len = strlen(post.text);
	memcpy(&reply_data[len], post.text, text_len);
	len += text_len;

	mesh::Utils::sha256((uint8_t *)&client->extra.room.pending_ack, 4, reply_data, len,
			    client->id.pub_key, PUB_KEY_SIZE);
	client->extra.room.push_post_timestamp = post.post_timestamp;

	mesh::Packet *reply = createDatagramFrom(_room_id, PAYLOAD_TYPE_TXT_MSG, client->id,
						 client->shared_secret, reply_data, len);
	if (reply) {
		if (client->out_path_len == OUT_PATH_UNKNOWN) {
			sendFloodScoped(default_scope, reply, 0, _prefs.path_hash_mode + 1);
			client->extra.room.ack_timeout = roomFutureMillis(PUSH_ACK_TIMEOUT_FLOOD);
		} else {
			sendDirect(reply, client->out_path, client->out_path_len);
			uint8_t path_hash_count = client->out_path_len & 63;
			client->extra.room.ack_timeout = roomFutureMillis(
				PUSH_TIMEOUT_BASE + PUSH_ACK_TIMEOUT_FACTOR * (path_hash_count + 1));
		}
		_room_num_post_pushes++;
	} else {
		client->extra.room.pending_ack = 0;
	}
}

uint8_t RepeaterMesh::roomGetUnsyncedCount(ClientInfo *client) const
{
	uint8_t count = 0;
	for (int k = 0; k < MAX_UNSYNCED_POSTS; k++) {
		if (_room_posts[k].post_timestamp > client->extra.room.sync_since &&
		    !_room_posts[k].author.matches(client->id)) {
			count++;
		}
	}
	return count;
}

bool RepeaterMesh::roomProcessAck(const uint8_t *data)
{
	for (int i = 0; i < _room_acl.getNumClients(); i++) {
		ClientInfo *client = _room_acl.getClientByIdx(i);
		if (client->extra.room.pending_ack &&
		    memcmp(data, &client->extra.room.pending_ack, 4) == 0) {
			client->extra.room.pending_ack = 0;
			client->extra.room.push_failures = 0;
			client->extra.room.sync_since = client->extra.room.push_post_timestamp;
			return true;
		}
	}
	return false;
}

void RepeaterMesh::onAckRecv(mesh::Packet *packet, uint32_t ack_crc)
{
	if (!_prefs.vroom.enabled || !_room_id_loaded) {
		return;
	}
	if (roomProcessAck((const uint8_t *)&ack_crc)) {
		packet->markDoNotRetransmit();
	}
}

int RepeaterMesh::roomHandleRequest(ClientInfo *sender, uint32_t sender_timestamp, uint8_t *payload, size_t payload_len)
{
	if (payload_len == 0) {
		return 0;
	}
	memcpy(reply_data, &sender_timestamp, 4);

	if (payload[0] == REQ_TYPE_GET_STATUS) {
		auto &radio_driver = *static_cast<mesh::LoRaRadio *>(_radio);
		VirtualRoomStats stats;
		stats.batt_milli_volts = _board.getBattMilliVolts();
		stats.curr_tx_queue_len = _mgr->getOutboundTotal();
		stats.noise_floor = (int16_t)_radio->getNoiseFloor();
		stats.last_rssi = (int16_t)radio_driver.getLastRSSI();
		stats.n_packets_recv = radio_driver.getPacketsRecv();
		stats.n_packets_sent = radio_driver.getPacketsSent();
		stats.total_air_time_secs = getTotalAirTime() / 1000;
		stats.total_up_time_secs = uptime_millis / 1000;
		stats.n_sent_flood = getNumSentFlood();
		stats.n_sent_direct = getNumSentDirect();
		stats.n_recv_flood = getNumRecvFlood();
		stats.n_recv_direct = getNumRecvDirect();
		stats.err_events = _err_flags;
		stats.last_snr = (int16_t)(radio_driver.getLastSNR() * 4);
		stats.n_direct_dups = MIN(((SimpleMeshTables *)getTables())->getNumDirectDups(), 0xFFFFu);
		stats.n_flood_dups = MIN(((SimpleMeshTables *)getTables())->getNumFloodDups(), 0xFFFFu);
		stats.n_posted = _room_num_posted;
		stats.n_post_push = _room_num_post_pushes;
		memcpy(&reply_data[4], &stats, sizeof(stats));
		return 4 + sizeof(stats);
	}

	if (payload[0] == REQ_TYPE_GET_TELEMETRY_DATA && payload_len >= 2) {
		uint8_t perm_mask = ~(payload[1]);
		telemetry.reset();
		telemetry.addVoltage(TELEM_CHANNEL_SELF, (float)_board.getBattMilliVolts() / 1000.0f);
		if ((sender->permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_GUEST) {
			perm_mask = 0x00;
		}
		sensors.querySensors(perm_mask, telemetry);
		float temperature = _board.getMCUTemperature();
		if (!isnan(temperature)) {
			telemetry.addTemperature(TELEM_CHANNEL_SELF, temperature);
		}
		if ((perm_mask & TELEM_PERM_LOCATION) && gps_is_enabled()) {
			gps_request_fresh_fix();
		}
		uint8_t tlen = telemetry.getSize();
		memcpy(&reply_data[4], telemetry.getBuffer(), tlen);
		return 4 + tlen;
	}

	if (payload[0] == REQ_TYPE_GET_ACCESS_LIST && sender->isAdmin() && payload_len >= 3) {
		if (payload[1] == 0 && payload[2] == 0) {
			uint8_t ofs = 4;
			for (int i = 0; i < _room_acl.getNumClients() && (size_t)(ofs + 7) <= sizeof(reply_data) - 4; i++) {
				ClientInfo *c = _room_acl.getClientByIdx(i);
				if (c->permissions == 0) {
					continue;
				}
				memcpy(&reply_data[ofs], c->id.pub_key, 6);
				ofs += 6;
				reply_data[ofs++] = c->permissions;
			}
			return ofs;
		}
	}

	if (payload[0] == REQ_TYPE_GET_OWNER_INFO) {
		char room_name[32];
		vroom::formatRoomName(_prefs.node_name, _prefs.vroom.access, room_name, sizeof(room_name));
		snprintf((char *)&reply_data[4], sizeof(reply_data) - 4, "%s\n%s\n%s",
			 FIRMWARE_VERSION, room_name, _prefs.owner_info);
		return 4 + strlen((char *)&reply_data[4]);
	}

	return 0;
}

void RepeaterMesh::onSecondaryAnonDataRecv(mesh::Packet *packet, const uint8_t *secret,
					   const mesh::Identity &sender, uint8_t *data, size_t len)
{
	if (!_prefs.vroom.enabled || !_room_id_loaded) {
		return;
	}
	if (packet->getPayloadType() != PAYLOAD_TYPE_ANON_REQ || len < 8) {
		return;
	}

	uint32_t sender_timestamp, sender_sync_since;
	memcpy(&sender_timestamp, data, 4);
	memcpy(&sender_sync_since, &data[4], 4);
	data[len] = 0;

	ClientInfo *client = nullptr;
	if (data[8] == 0) {
		client = _room_acl.getClient(sender.pub_key, PUB_KEY_SIZE);
	}

	if (client == nullptr) {
		if (_room_login_fail_limiter.blocked(getRTCClock()->getCurrentTime())) {
			LOG_WRN("Room login rate-limited");
			return;
		}
		uint8_t received[sizeof(_prefs.password)] = {0};
		uint8_t admin_pw[sizeof(_prefs.password)] = {0};
		uint8_t room_pw[sizeof(_prefs.vroom.password)] = {0};
		size_t r_len = strnlen((const char *)&data[8], sizeof(received) - 1);
		memcpy(received, &data[8], r_len);
		memcpy(admin_pw, _prefs.password, strnlen(_prefs.password, sizeof(admin_pw) - 1));
		memcpy(room_pw, _prefs.vroom.password, strnlen(_prefs.vroom.password, sizeof(room_pw) - 1));

		bool admin_match = mesh::Utils::constantTimeEqual(received, admin_pw, sizeof(received));
		bool room_pw_match = mesh::Utils::constantTimeEqual(received, room_pw, sizeof(received));
		if (_prefs.password[0] == 0) {
			admin_match = false;
		}
		if (_prefs.vroom.password[0] == 0) {
			room_pw_match = false;
		}

		uint8_t perms = PERM_ACL_GUEST;
		if (!vroom::resolveLoginPerms(_prefs.vroom, admin_match, room_pw_match, perms)) {
			if (!_room_login_fail_limiter.allow(getRTCClock()->getCurrentTime())) {
				LOG_WRN("Room login rate-limited");
			} else {
				LOG_WRN("Incorrect virtual room password");
			}
			return;
		}

		client = _room_acl.putClient(sender, 0);
		if (client->last_timestamp < _replay_floor) {
			client->last_timestamp = _replay_floor;
		}
		if (sender_timestamp <= client->last_timestamp) {
			LOG_WRN("Possible virtual room login replay attack");
			return;
		}

		uint8_t prev_perms = client->permissions;
		uint32_t prev_sync_since = client->extra.room.sync_since;
		bool secret_changed = memcmp(client->shared_secret, secret, PUB_KEY_SIZE) != 0;

		client->last_timestamp = sender_timestamp;
		client->extra.room.sync_since = sender_sync_since;
		client->extra.room.pending_ack = 0;
		client->extra.room.push_failures = 0;
		client->last_activity = getRTCClock()->getCurrentTime();
		if (!client->isAdmin()) {
			client->permissions &= ~0x03;
			client->permissions |= perms;
		}
		memcpy(client->shared_secret, secret, PUB_KEY_SIZE);

		const bool persist = client->isAdmin() || (room_pw_match && perms == PERM_ACL_READ_WRITE);
		if (persist && (client->permissions != prev_perms ||
				client->extra.room.sync_since != prev_sync_since ||
				secret_changed)) {
			if (client->isAdmin()) {
				roomSaveAcl();
			} else if (!_room_dirty_acl_expiry) {
				_room_dirty_acl_expiry = roomFutureMillis(LAZY_CONTACTS_WRITE_DELAY);
			}
		}
	} else {
		/* Known client, blank password: the replay rule of a password login applies. */
		if (sender_timestamp <= client->last_timestamp) {
			LOG_WRN("Possible virtual room login replay attack");
			return;
		}
		client->last_timestamp = sender_timestamp;
		if (sender_sync_since > 0) {
			client->extra.room.sync_since = sender_sync_since;
		}
		if (!client->isAdmin()) {
			client->permissions = vroom::blankReloginPerms(_prefs.vroom, client->permissions);
		}
		client->extra.room.pending_ack = 0;
		client->extra.room.push_failures = 0;
		client->last_activity = getRTCClock()->getCurrentTime();
	}

	if (packet->isRouteFlood()) {
		client->out_path_len = OUT_PATH_UNKNOWN;
	}

	uint32_t now = getRTCClock()->getCurrentTimeUnique();
	memcpy(reply_data, &now, 4);
	reply_data[4] = RESP_SERVER_LOGIN_OK;
	reply_data[5] = 0;
	reply_data[6] = (client->isAdmin() ? 1 : (client->permissions == PERM_ACL_GUEST ? 2 : 0));
	reply_data[7] = client->permissions;
	getRNG()->random(&reply_data[8], 4);
	reply_data[12] = FIRMWARE_VER_LEVEL;

	_room_next_push = roomFutureMillis(PUSH_NOTIFY_DELAY_MILLIS);

	if (packet->isRouteFlood()) {
		mesh::Packet *path = createPathReturnFrom(_room_id, sender, client->shared_secret,
							  packet->path, packet->path_len,
							  PAYLOAD_TYPE_RESPONSE, reply_data, 13);
		if (path) {
			sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
		}
	} else {
		mesh::Packet *reply = createDatagramFrom(_room_id, PAYLOAD_TYPE_RESPONSE, sender,
							 client->shared_secret, reply_data, 13);
		if (reply) {
			if (client->out_path_len != OUT_PATH_UNKNOWN) {
				sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
			} else {
				sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
			}
		}
	}
}

int RepeaterMesh::searchSecondaryPeersByHash(const uint8_t *hash)
{
	int n = 0;
	for (int i = 0; i < _room_acl.getNumClients(); i++) {
		ClientInfo *c = _room_acl.getClientByIdx(i);
		if (c->id.isHashMatch(hash)) {
			_room_matching_peers[n++] = i;
		}
	}
	return n;
}

void RepeaterMesh::getSecondaryPeerSharedSecret(uint8_t *dest_secret, int peer_idx)
{
	int i = _room_matching_peers[peer_idx];
	if (i >= 0 && i < _room_acl.getNumClients()) {
		memcpy(dest_secret, _room_acl.getClientByIdx(i)->shared_secret, PUB_KEY_SIZE);
	}
}

void RepeaterMesh::onSecondaryPeerDataRecv(mesh::Packet *packet, uint8_t type, int sender_idx,
					   const uint8_t *secret, uint8_t *data, size_t len)
{
	if (!_prefs.vroom.enabled || !_room_id_loaded) {
		return;
	}
	int i = _room_matching_peers[sender_idx];
	if (i < 0 || i >= _room_acl.getNumClients()) {
		return;
	}
	ClientInfo *client = _room_acl.getClientByIdx(i);

	if (type == PAYLOAD_TYPE_TXT_MSG && len > 5) {
		uint32_t sender_timestamp;
		memcpy(&sender_timestamp, data, 4);
		uint8_t flags = (data[4] >> 2);

		if (!(flags == TXT_TYPE_PLAIN || flags == TXT_TYPE_CLI_DATA ||
		      flags == TXT_TYPE_CLI_COMMAND)) {
			return;
		}
		if (sender_timestamp < client->last_timestamp) {
			LOG_DBG("Virtual room: possible replay attack");
			return;
		}

		bool is_retry = (sender_timestamp == client->last_timestamp);
		client->last_timestamp = sender_timestamp;
		client->last_activity = getRTCClock()->getCurrentTime();
		client->extra.room.push_failures = 0;

		data[len] = 0;

		uint32_t ack_hash;
		mesh::Utils::sha256((uint8_t *)&ack_hash, 4, data, 5 + strlen((char *)&data[5]),
				    client->id.pub_key, PUB_KEY_SIZE);

		uint8_t temp[5 + CLI_REMOTE_REPLY_SIZE];
		bool send_ack = false;

		if (flags == TXT_TYPE_CLI_DATA || flags == TXT_TYPE_CLI_COMMAND) {
			if (client->isAdmin() && !is_retry) {
				handleCommand(sender_timestamp, (char *)&data[5], (char *)&temp[5]);
				temp[4] = (TXT_TYPE_CLI_DATA << 2);
			} else {
				temp[5] = 0;
			}
		} else {
			if (vroom::canPost(client->permissions)) {
				if (!is_retry) {
					roomAddPost(client->id, (const char *)&data[5]);
				}
				temp[5] = 0;
				send_ack = true;
			} else {
				/* ACKed so the app stops retrying; the text says why nothing was posted. */
				if (is_retry) {
					temp[5] = 0;
				} else {
					StrHelper::strncpy((char *)&temp[5], vroom::READ_ONLY_REPLY, CLI_REMOTE_REPLY_SIZE);
					temp[4] = (TXT_TYPE_PLAIN << 2);
				}
				send_ack = true;
			}
		}

		uint32_t delay_millis = 0;
		if (send_ack) {
			if (client->out_path_len == OUT_PATH_UNKNOWN) {
				mesh::Packet *ack = createAck(ack_hash);
				if (ack) {
					sendFloodReply(ack, TXT_ACK_DELAY, packet->getPathHashSize());
				}
				delay_millis = TXT_ACK_DELAY + CLI_REPLY_DELAY_MILLIS;
			} else {
				uint32_t d = TXT_ACK_DELAY;
				if (getExtraAckTransmitCount() > 0) {
					mesh::Packet *a1 = createMultiAck(ack_hash, 1);
					if (a1) {
						sendDirect(a1, client->out_path, client->out_path_len, d);
					}
					d += 300;
				}
				mesh::Packet *a2 = createAck(ack_hash);
				if (a2) {
					sendDirect(a2, client->out_path, client->out_path_len, d);
				}
				delay_millis = d + CLI_REPLY_DELAY_MILLIS;
			}
		}

		int text_len = strlen((char *)&temp[5]);
		if (text_len > 0) {
			uint32_t now = getRTCClock()->getCurrentTimeUnique();
			if (now == sender_timestamp) {
				now++;
			}
			memcpy(temp, &now, 4);
			mesh::Packet *reply = createDatagramFrom(_room_id, PAYLOAD_TYPE_TXT_MSG,
								 client->id, secret, temp, 5 + text_len);
			if (reply) {
				if (client->out_path_len == OUT_PATH_UNKNOWN) {
					sendFloodReply(reply, delay_millis + SERVER_RESPONSE_DELAY,
						       packet->getPathHashSize());
				} else {
					sendDirect(reply, client->out_path, client->out_path_len,
						   delay_millis + SERVER_RESPONSE_DELAY);
				}
			}
		}
	} else if (type == PAYLOAD_TYPE_REQ && len >= 5) {
		uint32_t sender_timestamp;
		memcpy(&sender_timestamp, data, 4);
		if (sender_timestamp < client->last_timestamp) {
			LOG_DBG("Virtual room: possible REQ replay attack");
			return;
		}
		client->last_timestamp = sender_timestamp;
		client->last_activity = getRTCClock()->getCurrentTime();
		client->extra.room.push_failures = 0;

		if (data[4] == REQ_TYPE_KEEP_ALIVE && packet->isRouteDirect()) {
			uint32_t forceSince = 0;
			if (len >= 9) {
				memcpy(&forceSince, &data[5], 4);
			} else {
				memcpy(&data[5], &forceSince, 4);
			}
			if (forceSince > 0) {
				client->extra.room.sync_since = forceSince;
			}
			client->extra.room.pending_ack = 0;

			if (client->out_path_len != OUT_PATH_UNKNOWN) {
				uint32_t ack_hash;
				mesh::Utils::sha256((uint8_t *)&ack_hash, 4, data, 9,
						    client->id.pub_key, PUB_KEY_SIZE);
				mesh::Packet *reply = createAck(ack_hash);
				if (reply) {
					reply->payload[reply->payload_len++] = roomGetUnsyncedCount(client);
					sendDirect(reply, client->out_path, client->out_path_len,
						   SERVER_RESPONSE_DELAY);
				}
			}
		} else {
			int reply_len = roomHandleRequest(client, sender_timestamp, &data[4], len - 4);
			if (reply_len > 0) {
				if (packet->isRouteFlood()) {
					mesh::Packet *path = createPathReturnFrom(
						_room_id, client->id, secret, packet->path,
						packet->path_len, PAYLOAD_TYPE_RESPONSE,
						reply_data, reply_len);
					if (path) {
						sendFloodReply(path, SERVER_RESPONSE_DELAY,
							       packet->getPathHashSize());
					}
				} else {
					mesh::Packet *reply = createDatagramFrom(
						_room_id, PAYLOAD_TYPE_RESPONSE, client->id,
						secret, reply_data, reply_len);
					if (reply) {
						if (client->out_path_len != OUT_PATH_UNKNOWN) {
							sendDirect(reply, client->out_path,
								   client->out_path_len,
								   SERVER_RESPONSE_DELAY);
						} else {
							sendFloodReply(reply, SERVER_RESPONSE_DELAY,
								       packet->getPathHashSize());
						}
					}
				}
			}
		}
	}
}

bool RepeaterMesh::onSecondaryPeerPathRecv(mesh::Packet *packet, int sender_idx, const uint8_t *secret,
					   uint8_t *path, uint8_t path_len, uint8_t extra_type,
					   uint8_t *extra, uint8_t extra_len)
{
	(void)packet;
	(void)secret;
	int i = _room_matching_peers[sender_idx];
	if (i >= 0 && i < _room_acl.getNumClients()) {
		ClientInfo *client = _room_acl.getClientByIdx(i);
		client->out_path_len = mesh::Packet::copyPath(client->out_path, path, path_len);
		client->last_activity = getRTCClock()->getCurrentTime();
	}
	if (extra_type == PAYLOAD_TYPE_ACK && extra_len >= 4) {
		roomProcessAck(extra);
	}
	return false;
}

void RepeaterMesh::roomLoop()
{
	if (!_prefs.vroom.enabled || !_room_id_loaded) {
		return;
	}

	if (roomMillisPassed(_room_next_push) && _room_acl.getNumClients() > 0) {
		for (int i = 0; i < _room_acl.getNumClients(); i++) {
			ClientInfo *c = _room_acl.getClientByIdx(i);
			if (c->extra.room.pending_ack && roomMillisPassed(c->extra.room.ack_timeout)) {
				c->extra.room.push_failures++;
				c->extra.room.pending_ack = 0;
			}
		}

		ClientInfo *client = _room_acl.getClientByIdx(_room_next_client_idx);
		bool did_push = false;
		if (client->extra.room.pending_ack == 0 &&
		    client->last_activity != 0 && client->extra.room.push_failures < 3) {
			uint32_t now = getRTCClock()->getCurrentTime();
			for (int k = 0, idx = _room_next_post_idx; k < MAX_UNSYNCED_POSTS; k++) {
				RoomPostInfo *p = &_room_posts[idx];
				if (p->post_timestamp != 0 &&
				    now >= p->post_timestamp + POST_SYNC_DELAY_SECS &&
				    p->post_timestamp > client->extra.room.sync_since &&
				    !p->author.matches(client->id)) {
					roomPushPostToClient(client, *p);
					did_push = true;
					break;
				}
				idx = (idx + 1) % MAX_UNSYNCED_POSTS;
			}
		}
		_room_next_client_idx = (_room_next_client_idx + 1) % _room_acl.getNumClients();
		_room_next_push = did_push ? roomFutureMillis(SYNC_PUSH_INTERVAL)
					   : roomFutureMillis(SYNC_PUSH_INTERVAL / 8);
	}

	if (_room_next_flood_advert && roomMillisPassed(_room_next_flood_advert)) {
		_room_next_flood_advert = 0;
		roomSendSelfAdvert(0, true);
	} else if (_room_next_local_advert && roomMillisPassed(_room_next_local_advert)) {
		_room_next_local_advert = 0;
		roomSendSelfAdvert(0, false);
	}

	if (_room_dirty_acl_expiry && roomMillisPassed(_room_dirty_acl_expiry)) {
		roomSaveAcl();
	}
}

uint32_t RepeaterMesh::roomMsUntilNextMaintenance()
{
	if (!_prefs.vroom.enabled || !_room_id_loaded) {
		return mesh::MAINTENANCE_IDLE;
	}
	uint32_t now = (uint32_t)k_uptime_get();
	uint32_t rtc_now = getRTCClock()->getCurrentTime();
	uint32_t next = mesh::MAINTENANCE_IDLE;

	if (_room_next_flood_advert) {
		next = mesh::maintenanceSooner(next, mesh::maintenanceUntil(now, _room_next_flood_advert));
	}
	if (_room_next_local_advert) {
		next = mesh::maintenanceSooner(next, mesh::maintenanceUntil(now, _room_next_local_advert));
	}
	if (_room_dirty_acl_expiry) {
		next = mesh::maintenanceSooner(next, mesh::maintenanceUntil(now, _room_dirty_acl_expiry));
	}

	uint32_t work = mesh::MAINTENANCE_IDLE;
	for (int i = 0; i < _room_acl.getNumClients() && work > 0; i++) {
		ClientInfo *c = _room_acl.getClientByIdx(i);
		if (c->extra.room.pending_ack) {
			work = mesh::maintenanceSooner(
				work, mesh::maintenanceUntil(now, c->extra.room.ack_timeout));
			continue;
		}
		if (c->last_activity == 0 || c->extra.room.push_failures >= 3) {
			continue;
		}
		for (int k = 0; k < MAX_UNSYNCED_POSTS; k++) {
			RoomPostInfo &p = _room_posts[k];
			if (p.post_timestamp == 0 || p.post_timestamp <= c->extra.room.sync_since ||
			    p.author.matches(c->id)) {
				continue;
			}
			uint32_t eligible_at = p.post_timestamp + POST_SYNC_DELAY_SECS;
			if (rtc_now >= eligible_at) {
				work = 0;
				break;
			}
			work = mesh::maintenanceSooner(work, (eligible_at - rtc_now) * 1000);
		}
	}
	if (work != mesh::MAINTENANCE_IDLE) {
		uint32_t gate = mesh::maintenanceUntil(now, _room_next_push);
		work = (work > gate) ? work : gate;
		next = mesh::maintenanceSooner(next, work);
	}
	return next;
}

void RepeaterMesh::roomTimeSyncShift(int64_t delta)
{
	for (int k = 0; k < MAX_UNSYNCED_POSTS; k++) {
		_room_posts[k].post_timestamp = shiftWallStamp(_room_posts[k].post_timestamp, delta);
	}
	for (int i = 0; i < _room_acl.getNumClients(); i++) {
		ClientInfo *c = _room_acl.getClientByIdx(i);
		c->extra.room.sync_since = shiftWallStamp(c->extra.room.sync_since, delta);
		c->extra.room.push_post_timestamp = shiftWallStamp(c->extra.room.push_post_timestamp, delta);
		if (c->last_activity == 0) {
			continue;
		}
		int64_t shifted = (int64_t)c->last_activity + delta;
		c->last_activity = (shifted > 0) ? (uint32_t)shifted : 1;
	}
	_room_login_fail_limiter.reset();
}

bool RepeaterMesh::roomHandleCommand(uint32_t sender_timestamp, const char *command, char *reply)
{
	const zcmd::Parsed p = zcmd::parse(command);
	if (!p.ok || p.proto != 0 || strncmp(p.key, "room.", 5) != 0) {
		return false;
	}
	const size_t cap = _cli.replyCap(sender_timestamp);
	const char *sub = p.key + 5;
	char name[24];
	const char *val = zcmd::splitValue(sub, name, sizeof(name));

	if (p.get) {
		if (strcmp(name, "on") == 0 && *val == '\0') {
			snprintf(reply, cap, "> %s", _prefs.vroom.enabled ? "on" : "off");
		} else if (strcmp(name, "access") == 0 && *val == '\0') {
			snprintf(reply, cap, "> %s", vroom::accessName(_prefs.vroom.access));
		} else if (strcmp(name, "password") == 0 && *val == '\0') {
			snprintf(reply, cap, "> %s",
				 _prefs.vroom.password[0] ? _prefs.vroom.password : "(none)");
		} else if (strcmp(name, "acl") == 0 && *val == '\0') {
			size_t used = 0;
			int shown = 0;
			for (int i = 0; i < _room_acl.getNumClients(); i++) {
				ClientInfo *c = _room_acl.getClientByIdx(i);
				if (c->permissions == 0) {
					continue;
				}
				char hex[13];
				mesh::Utils::toHex(hex, c->id.pub_key, 6);
				const char *sep = shown > 0 ? "\n" : "";
				int n = snprintf(reply + used, cap - used, "%s%02X %s", sep,
						 c->permissions, hex);
				if (n < 0 || used + (size_t)n >= cap) {
					break;
				}
				used += (size_t)n;
				shown++;
			}
			if (shown == 0) {
				snprintf(reply, cap, "> (empty)");
			}
		} else {
			snprintf(reply, cap, "Error: unknown command %s", command);
		}
		return true;
	}

	if (strcmp(name, "on") == 0) {
		uint8_t en = 0;
		if (!zcmd::parseOnOff(val, en)) {
			snprintf(reply, cap, "Error: expected on|off|1|0");
			return true;
		}
		if (en) {
			const bool was_running = _prefs.vroom.enabled && _room_id_loaded;
			if (!roomEnsureIdentity()) {
				snprintf(reply, cap, "Error: failed to create room identity");
				return true;
			}
			if (!was_running && _store) {
				roomLoadAcl();
			}
			_prefs.vroom.enabled = 1;
			if (!savePrefs()) {
				snprintf(reply, cap, "Error: failed to save prefs");
				return true;
			}
			roomSendSelfAdvert(2000, true);
			snprintf(reply, cap, "OK - room ON (%s)", vroom::accessName(_prefs.vroom.access));
		} else {
			if (_room_dirty_acl_expiry) {
				roomSaveAcl();
			}
			_prefs.vroom.enabled = 0;
			_room_next_local_advert = 0;
			_room_next_flood_advert = 0;
			if (!savePrefs()) {
				snprintf(reply, cap, "Error: failed to save prefs");
				return true;
			}
			snprintf(reply, cap, "OK - room OFF");
		}
		return true;
	}

	if (strcmp(name, "access") == 0) {
		uint8_t mode = 0;
		if (!vroom::parseAccess(val, mode)) {
			snprintf(reply, cap, "Error: expected open|readonly|private");
			return true;
		}
		_prefs.vroom.access = mode;
		if (!savePrefs()) {
			snprintf(reply, cap, "Error: failed to save prefs");
			return true;
		}
		roomOnAccessChanged();
		snprintf(reply, cap, "OK - room access=%s", vroom::accessName(_prefs.vroom.access));
		return true;
	}

	if (strcmp(name, "password") == 0) {
		if (*val == '\0') {
			snprintf(reply, cap, "Error: expected password or clear");
			return true;
		}
		if (strcmp(val, "clear") == 0) {
			_prefs.vroom.password[0] = '\0';
		} else if (strlen(val) >= sizeof(_prefs.vroom.password)) {
			snprintf(reply, cap, "Error: password max %u chars",
				 (unsigned)(sizeof(_prefs.vroom.password) - 1));
			return true;
		} else {
			StrHelper::strncpy(_prefs.vroom.password, val, sizeof(_prefs.vroom.password));
		}
		if (!savePrefs()) {
			snprintf(reply, cap, "Error: failed to save prefs");
			return true;
		}
		snprintf(reply, cap, _prefs.vroom.password[0] ? "OK - room password set"
							     : "OK - room password cleared");
		return true;
	}

	if (strcmp(name, "post") == 0) {
		while (*val == ' ') {
			val++;
		}
		if (!_prefs.vroom.enabled || !_room_id_loaded) {
			snprintf(reply, cap, "Error: room is off");
		} else if (*val == '\0') {
			snprintf(reply, cap, "Error: empty message");
		} else {
			roomAddPost(_room_id, val);
			snprintf(reply, cap, "OK");
		}
		return true;
	}

	if (strcmp(name, "advert") == 0) {
		if (!_prefs.vroom.enabled || !_room_id_loaded) {
			snprintf(reply, cap, "Error: room is off");
		} else if (*val == '\0') {
			roomSendSelfAdvert(500, false);
			snprintf(reply, cap, "OK - room advert sent");
		} else if (strcmp(val, "flood") == 0) {
			roomSendSelfAdvert(500, true);
			snprintf(reply, cap, "OK - room flood advert sent");
		} else {
			snprintf(reply, cap, "Error: expected flood or empty");
		}
		return true;
	}

	snprintf(reply, cap, "Error: unknown command %s", command);
	return true;
}
