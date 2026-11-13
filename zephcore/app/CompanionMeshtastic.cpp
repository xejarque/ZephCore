/*
 * SPDX-License-Identifier: MIT
 * Zephtacore: the companion's Meshtastic bridge.
 *
 *   - A channel added with "z.mt.channel.add <name>" is a Meshtastic channel
 *     with that name and key, shown to the app as the hashtag channel
 *     "#mt-<name>". Text the app sends on it leaves as a Meshtastic frame;
 *     text heard on it reaches the app as a channel message.
 *   - Meshtastic nodes heard on those channels (NodeInfo, Position) become
 *     contacts named "<long name> (MT)", keyed by an Ed25519 key derived from the
 *     node number, so they appear in the contact list and on the map.
 *   - A direct message to such a contact goes out as a Meshtastic PKI message
 *     (X25519 with the key from the node's NodeInfo, AES-256-CCM); a PKI
 *     message addressed to us reaches the app as a message of that contact.
 *     The keys are kept in the "mtkeys" file.
 * Nothing here enters the MeshCore routing path.
 */

#include "CompanionMesh.h"

#if IS_ENABLED(CONFIG_ZEPHTACORE_MESHTASTIC)

#include <helpers/meshtastic/MtConfig.h>
#include <mesh/Utils.h>
#include <string.h>
#include <stdio.h>
#include <psa/crypto.h>
#include <zephyr/random/random.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zephcore_companion, CONFIG_ZEPHCORE_MAIN_LOG_LEVEL);

static const char kMtPrefix[] = "MT:";
static constexpr size_t kMtPrefixLen = sizeof(kMtPrefix) - 1;
/* Contacts are named "<name> (MT)"; "MT:<name>" is the older form, still read. */
static const char kMtSuffix[] = " (MT)";
static constexpr size_t kMtSuffixLen = sizeof(kMtSuffix) - 1;

/* name plus the suffix, the name cut at a character boundary to fit cap. */
static void mtContactName(char *out, size_t cap, const char *name)
{
	size_t keep = strlen(name);

	if (keep > cap - 1 - kMtSuffixLen) {
		keep = cap - 1 - kMtSuffixLen;
		while (keep > 0 && ((uint8_t)name[keep] & 0xC0) == 0x80) {
			keep--;
		}
	}
	snprintf(out, cap, "%.*s%s", (int)keep, name, kMtSuffix);
}

/* The Meshtastic name behind a contact name, false if it is not one. */
static bool mtContactBase(const char *contact, char *out, size_t cap)
{
	size_t len = strlen(contact);

	if (len > kMtSuffixLen && strcmp(contact + len - kMtSuffixLen, kMtSuffix) == 0) {
		snprintf(out, cap, "%.*s", (int)(len - kMtSuffixLen), contact);
		return true;
	}
	if (strncmp(contact, kMtPrefix, kMtPrefixLen) == 0) {
		snprintf(out, cap, "%s", contact + kMtPrefixLen);
		return true;
	}
	return false;
}

/* SHA-256 for the PKI code, through PSA like the rest of the crypto. */
static void mtSha256(const uint8_t *msg, size_t len, uint8_t out[32])
{
	mesh::Utils::sha256(out, 32, msg, (int)len);
}

static uint32_t mtRandom32()
{
	uint32_t r = 0;

	sys_csrand_get(&r, sizeof(r));
	return r;
}

static const mt::PkiCrypto kMtPki = {MtRelayPort::aesBlock256, mtSha256, mtRandom32};

/* Meshtastic's default key has PSK index 1; index n adds n-1 to its last byte. */
struct MtDefaultChannel {
	const char *name;
	uint8_t psk_index;
};

static const MtDefaultChannel kMtDefaults[] = {
	{MT_DEFAULT_CHANNEL, 1}, {"Test", 2}, {"Bots", 2}, {"Catalunya", 1}, {"Barcelona", 1},
};
static const char *const kMcHashtagDefaults[] = {"#test", "#bot"};

void CompanionMesh::mtAddDefaultChannels()
{
	for (const auto &c : kMtDefaults) {
		const uint8_t variant = c.psk_index;

		mt::addChannel(prefs.mt, c.name, &variant, 1); /* one already there stays */
	}
	mtReconcileChannels();
	/* A MeshCore hashtag channel's secret is the first 16 bytes of SHA256 of its name. */
	for (const char *name : kMcHashtagDefaults) {
		uint8_t digest[32];

		if (mtSlotByName(name) >= 0) {
			continue;
		}

		mesh::Utils::sha256(digest, sizeof(digest), (const uint8_t *)name, (int)strlen(name));
		addChannel(name, digest, 16);
	}
}

void CompanionMesh::mtSetup()
{
	MtRelayPort::initSelf(_mt, self_id, prefs.node_name, *getRNG());
	_mtport.apply(prefs.mt);

	uint8_t identity[PRV_KEY_SIZE + PUB_KEY_SIZE];

	if (self_id.writeTo(identity, PRV_KEY_SIZE) == PRV_KEY_SIZE) {
		_mt.setPki(&kMtPki, identity);
	}
	memset(identity, 0, sizeof(identity));
	mtSyncChannels();
	/* Channels are loaded once the app side is up; with none yet, try again. */
	_mt_ready = getNumChannels() > 0;
	_mtport.arm((uint32_t)_ms->getMillis(), prefs.mt);
}

void CompanionMesh::mtApplyListen()
{
	_radio->setMtListen(prefs.mt.enable != 0);
}

/* z.mt.* changed through the CLI. */
void CompanionMesh::mtPrefsChanged()
{
	mtApplyListen();
	if (_mt_ready) {
		MtRelayPort::renameSelf(_mt, prefs.node_name);
		_mtport.apply(prefs.mt);
		_mtport.arm((uint32_t)_ms->getMillis(), prefs.mt);
	}
}

struct CompanionMesh::MtNeighbours : public neighbours::TableList {
	MtNeighbours(const neighbours::Table &t, uint32_t now, CompanionMesh &mesh)
		: TableList(t, now), _mesh(mesh) {}

protected:
	void label(const neighbours::Entry &e, neighbours::Row &r) const override
	{
		uint8_t key[PUB_KEY_SIZE];
		ContactInfo *c = _mesh.mtKeyFor(e.id, key) ? _mesh.lookupContactByPubKey(key, PUB_KEY_SIZE) : nullptr;
		char base[32];

		if (c && mtContactBase(c->name, base, sizeof(base)) && base[0] != '!') {
			neighbours::setRowName(r, base);
		} else if (e.name[0]) {
			snprintf(r.name, sizeof(r.name), "%s", e.name);
		} else {
			neighbours::nodeLabel(r.name, sizeof(r.name), e.id);
		}
	}

private:
	CompanionMesh &_mesh;
};

void CompanionMesh::mtFormatNeighbours(char *reply, size_t cap, size_t page, bool recent)
{
	neighbours::format(reply, cap, "Meshtastic neighbors",
			   MtNeighbours(_mtport.neighbours(), (uint32_t)(k_uptime_get() / 1000), *this), page, recent);
}

void CompanionMesh::mtFillDashboard(struct ui_dash &d)
{
	neighbours::Row rows[UI_DASH_NODES] = {};
	const size_t total = neighbours::top(
		MtNeighbours(_mtport.neighbours(), (uint32_t)(k_uptime_get() / 1000), *this), rows, UI_DASH_NODES);
	const size_t n = total < UI_DASH_NODES ? total : UI_DASH_NODES;

	d.mt_n_total = (uint8_t)total;

	for (size_t i = 0; i < n; i++) {
		snprintf(d.mt_n[i].name, sizeof(d.mt_n[i].name), "%.*s", (int)sizeof(d.mt_n[i].name) - 1, rows[i].name);
		d.mt_n[i].snr_x4 = rows[i].snr_x4;
		d.mt_n[i].age_s = rows[i].age_s;
	}
	d.mt_on = prefs.mt.enable != 0;
	d.mt_heard = _mtport.stats().heard;
	d.mt_relayed = _mtport.stats().relayed;
	d.mt_dropped = _mtport.stats().dropped;
	snprintf(d.mt_cfg, sizeof(d.mt_cfg), "%s relay %s", mt::roleName(prefs.mt.role),
		 mt::relayName(prefs.mt.relay));
}

void CompanionMesh::mtFormatStats(char *reply, size_t cap) const
{
	_mtport.formatStats(reply, cap);
}

const mt::ChannelEntry *CompanionMesh::mtEntryOfSlot(int idx)
{
	ChannelDetails ch;
	uint8_t secret[16];

	if (!getChannel(idx, ch) || ch.name[0] == '\0') {
		return nullptr;
	}
	const mt::ChannelEntry *e = mt::findChannel(prefs.mt, ch.name);

	if (!e) {
		return nullptr;
	}
	mt::slotSecret(ch.name, mtSha256, secret);
	return memcmp(ch.channel.secret, secret, sizeof(secret)) == 0 ? e : nullptr;
}

int CompanionMesh::mtSlotByName(const char *slot)
{
	ChannelDetails ch;

	for (int i = 0; i < getNumChannels(); i++) {
		if (getChannel(i, ch) && strcmp(ch.name, slot) == 0) {
			return i;
		}
	}
	return -1;
}

static bool mtSlotFree(const ChannelDetails &ch)
{
	static const uint8_t zeroes[sizeof(ch.channel.secret)] = {0};

	return ch.name[0] == '\0' && memcmp(ch.channel.secret, zeroes, sizeof(zeroes)) == 0;
}

bool CompanionMesh::mtPlaceChannel(const char *slot, const uint8_t secret[16])
{
	ChannelDetails ch;

	for (int i = 0; i < getNumChannels(); i++) {
		if (getChannel(i, ch) && mtSlotFree(ch)) {
			memset(&ch, 0, sizeof(ch));
			StrHelper::strncpy(ch.name, slot, sizeof(ch.name));
			memcpy(ch.channel.secret, secret, 16);
			return setChannel(i, ch);
		}
	}
	return addChannel(slot, secret, 16) != nullptr;
}

bool CompanionMesh::mtChannelAdded(const char *slot)
{
	const int idx = mtSlotByName(slot);
	uint8_t secret[16];

	if (idx >= 0) {
		return mtEntryOfSlot(idx) != nullptr; /* a slot with another key is the app's own */
	}
	mt::slotSecret(slot, mtSha256, secret);
	if (!mtPlaceChannel(slot, secret)) {
		return false;
	}
	markChannelsDirty();
	return true;
}

void CompanionMesh::mtReconcileChannels()
{
	for (auto &e : prefs.mt.channels) {
		char slot[mt::kSlotNameMax];

		if (e.name[0] == '\0') {
			continue;
		}
		mt::slotName(e.name, slot);
		if (!mtChannelAdded(slot)) {
			LOG_WRN("mt: no slot for channel '%s', dropped", e.name);
			e = mt::ChannelEntry{};
		}
	}
}

bool CompanionMesh::mtSlotRewritten(int idx, const ChannelDetails &next)
{
	ChannelDetails cur;
	mt::ChannelEntry *held = nullptr;

	if (getChannel(idx, cur) && mtEntryOfSlot(idx)) {
		held = mt::findChannel(prefs.mt, cur.name);
	}
	switch (mt::slotWrite(prefs.mt, held, next.name, next.channel.secret, mtSha256)) {
	case mt::SLOT_WRITE_REFUSED:
		return false;
	case mt::SLOT_WRITE_RENAMED:
		if (_store) {
			_store->savePrefs(prefs);
		}
		return true;
	default:
		return true;
	}
}

void CompanionMesh::mtDropOrphanChannels()
{
	auto held = [](const char *slot, void *ctx) {
		CompanionMesh &m = *static_cast<CompanionMesh *>(ctx);
		const int idx = m.mtSlotByName(slot);

		return idx >= 0 && m.mtEntryOfSlot(idx) != nullptr;
	};

	if (mt::dropOrphans(prefs.mt, held, this) && _store) {
		_store->savePrefs(prefs);
	}
}

void CompanionMesh::mtChannelRemoved(const char *slot)
{
	const int idx = mtSlotByName(slot);
	ChannelDetails ch;
	uint8_t secret[16];

	if (idx < 0 || !getChannel(idx, ch)) {
		return;
	}
	mt::slotSecret(slot, mtSha256, secret);
	if (memcmp(ch.channel.secret, secret, sizeof(secret)) != 0) {
		return; /* the app's own channel by that name */
	}
	memset(&ch, 0, sizeof(ch));
	setChannel(idx, ch);
	markChannelsDirty();
}

void CompanionMesh::mtSyncChannels()
{
	_mt.clearChannels();

	for (int i = 0; i < getNumChannels(); i++) {
		const mt::ChannelEntry *e = mtEntryOfSlot(i);

		if (e && !_mt.setChannel((uint8_t)i, e->name, e->psk, e->psk_len)) {
			LOG_WRN("mt: channel '%s' not usable (table full)", e->name);
		}
	}
	LOG_INF("mt: %d Meshtastic channel(s)", _mt.channelCount());
}

int CompanionMesh::mtFirstChannel() const
{
	for (int i = 0; i < getNumChannels(); i++) {
		if (_mt.hasChannel((uint8_t)i)) {
			return i;
		}
	}
	return -1;
}

/* The contact key of a Meshtastic node: the public key of a keypair seeded
 * from SHA256("zt-mtnode" || node). A real Ed25519 point (strict clients reject
 * anything else) whose private half nobody keeps. Cached: a derivation is a
 * scalar multiplication. */
bool CompanionMesh::mtKeyFor(uint32_t node, uint8_t key[PUB_KEY_SIZE])
{
	for (auto &e : _mt_keys) {
		if (node != 0 && e.node == node) {
			memcpy(key, e.key, PUB_KEY_SIZE);
			return true;
		}
	}

	static const char salt[] = "zt-mtnode";
	uint8_t material[4];
	uint8_t seed[SEED_SIZE];
	mesh::LocalIdentity id;

	for (int i = 0; i < 4; i++) {
		material[i] = (uint8_t)(node >> (24 - 8 * i));
	}
	mesh::Utils::sha256(seed, SEED_SIZE, (const uint8_t *)salt, sizeof(salt) - 1, material,
			    sizeof(material));
	id.fromSeed(seed);

	MtKey &slot = _mt_keys[_mt_key_next];

	_mt_key_next = (uint8_t)((_mt_key_next + 1) % MT_KEY_CACHE);
	slot.node = node;
	memcpy(slot.key, id.pub_key, PUB_KEY_SIZE);
	memcpy(key, slot.key, PUB_KEY_SIZE);
	return true;
}

/* The node number behind a contact. A Meshtastic contact keeps it in
 * sync_since (unused for chat contacts); the key derived from it must match,
 * so a contact the app edited or imported is never taken for one. */
bool CompanionMesh::mtNodeOf(const ContactInfo &contact, uint32_t &node)
{
	uint8_t key[PUB_KEY_SIZE];

	if (contact.sync_since == 0 || !mtKeyFor(contact.sync_since, key) ||
	    memcmp(key, contact.id.pub_key, PUB_KEY_SIZE) != 0) {
		return false;
	}
	node = contact.sync_since;
	return true;
}

void CompanionMesh::mtUpsertContact(uint32_t node, const char *name, bool has_pos, int32_t lat_i,
				    int32_t lon_i)
{
	uint8_t key[PUB_KEY_SIZE];

	if (!mtKeyFor(node, key)) {
		return;
	}
	uint32_t now = (uint32_t)getRTCClock()->getCurrentTime();
	ContactInfo *c = lookupContactByPubKey(key, PUB_KEY_SIZE);
	bool is_new = (c == nullptr);

	if (is_new) {
		ContactInfo fresh;

		memcpy(fresh.id.pub_key, key, PUB_KEY_SIZE);
		fresh.type = ADV_TYPE_CHAT;
		fresh.flags = 0;
		fresh.out_path_len = OUT_PATH_UNKNOWN;
		fresh.shared_secret_valid = false;
		memset(fresh.out_path, 0, sizeof(fresh.out_path));
		snprintf(fresh.name, sizeof(fresh.name), "!%08x%s", (unsigned)node, kMtSuffix);
		fresh.gps_lat = 0;
		fresh.gps_lon = 0;
		fresh.sync_since = node;
		fresh.last_advert_timestamp = now;
		fresh.lastmod = now;
		if (!shouldAutoAddContactType(fresh.type)) {
			if (name && name[0]) {
				mtContactName(fresh.name, sizeof(fresh.name), name);
			}
			if (has_pos) {
				fresh.gps_lat = lat_i / 10;
				fresh.gps_lon = lon_i / 10;
			}
			onDiscoveredContact(fresh, true, 0, nullptr);
			return;
		}
		if (!addContact(fresh)) {
			LOG_WRN("mt: contacts full, node %08x not added", (unsigned)node);
			return;
		}
		c = lookupContactByPubKey(key, PUB_KEY_SIZE);
		if (!c) {
			return;
		}
		markContactsDirty();
	} else {
		snapshotAdvertPrev(c);
	}
	if (name && name[0]) {
		mtContactName(c->name, sizeof(c->name), name);
	}
	if (has_pos) {
		c->gps_lat = lat_i / 10;  /* 1e-7 degrees to 1e-6 */
		c->gps_lon = lon_i / 10;
	}
	c->last_advert_timestamp = now;
	c->lastmod = now;
	onDiscoveredContact(*c, is_new, 0, nullptr);
	_advert_prev.known = false;
}

/* ── RX ───────────────────────────────────────────────────────────────── */

bool CompanionMesh::onRecvForeign(const uint8_t raw[], int len, float snr, float rssi)
{
	/* With no Meshtastic channel nothing is opened here: only a MeshCore-shaped frame goes on. */
	if (getNumChannels() == 0) {
		return MtRelayPort::unopened(raw, len, 0) == mt::RX_IGNORED;
	}
	if (!_mt_ready) {
		mtSetup();
	}
	if (_mt.channelCount() == 0) {
		return MtRelayPort::unopened(raw, len, _mt.self()) == mt::RX_IGNORED;
	}

	MtRelayPort::Policy policy = MtRelayPort::policyFor(prefs.mt);

	policy.yields = false; /* a companion never cancels a repeat on an echo */
	mt::RxResult r = _mtport.onFrame(raw, len, snr, _mt_sink, policy);

	if (r == mt::RX_NOT_MESHTASTIC) {
		return false;
	}
	mt::Header h;

	if (r == mt::RX_DUPLICATE && mt::parseHeader(raw, (size_t)len, h) && h.from == _mt.self()) {
		mtEchoHeard(h.id, h.relay, snr, rssi);
	}
	return true;
}

void CompanionMesh::MtSink::onText(uint8_t channel_id, uint32_t from, uint32_t to, uint32_t id,
				   uint32_t reply_id, const char *text, size_t len, int8_t snr_x4,
				   uint8_t hops, uint8_t relay)
{
	(void)to;

	ChannelDetails ch;

	if (!m.getChannel(channel_id, ch)) {
		return;
	}
	/* "<sender>: <text>", the form MeshCore channel messages have. */
	char sender[32];
	uint8_t key[PUB_KEY_SIZE];
	ContactInfo *c = nullptr;

	if (m.mtKeyFor(from, key)) {
		c = m.lookupContactByPubKey(key, PUB_KEY_SIZE);
	}
	if (!c || !mtContactBase(c->name, sender, sizeof(sender))) {
		snprintf(sender, sizeof(sender), "!%08x", (unsigned)from);
	}

	/* A reply opens with "@[name] ", naming the author of the text it answers. */
	const char *replied = reply_id ? m.mtRecentName(reply_id) : nullptr;
	char line[MAX_TEXT_LEN + 1];
	int n = snprintf(line, sizeof(line), "%s: ", sender);

	if (replied && n > 0 && (size_t)n < sizeof(line)) {
		n += snprintf(line + n, sizeof(line) - n, "@[%s] ", replied);
	}
	m.mtNoteText(id, sender);

	if (n < 0 || (size_t)n >= sizeof(line)) {
		return;
	}
	size_t room = sizeof(line) - 1 - (size_t)n;

	if (len > room) {
		len = room;
	}
	memcpy(line + n, text, len);
	line[n + len] = '\0';

	/* The app reads the hop count from the packet's path length; without a
	 * packet it shows the "unknown" value, 63. */
	mesh::Packet *pkt = m.obtainNewPacket();

	if (pkt) {
		pkt->header = ROUTE_TYPE_FLOOD | (PAYLOAD_TYPE_GRP_TXT << PH_TYPE_SHIFT);
		pkt->setPathHashSizeAndCount(1, hops);
		pkt->_snr = snr_x4;
	}
	m.deliverChannelMessage(channel_id, pkt, m.getRTCClock()->getCurrentTimeUnique(), line);
	if (pkt) {
		m.releasePacket(pkt);
	}
	char bot_slot[mt::kSlotNameMax];

	mt::slotName(PING_MT_CHANNEL, bot_slot);
	const mt::ChannelEntry *bots = m.mtEntryOfSlot(channel_id);

	if (m.prefs.ping.mt.enable && strcmp(ch.name, bot_slot) == 0 && bots && bots->psk_len == 1 &&
	    bots->psk[0] == ping::kMtPskIndex) {
		/* relay is 0 when the sender set no relay_node; a relay that is not the sender says it was relayed. */
		const bool relayed = hops == 0 && relay != 0 && (uint8_t)from != relay;
		const ping::Heard heard = {hops, snr_x4, (int16_t)m._radio->getLastRSSI(),
					   {&relay, 1, (uint8_t)(hops || relayed ? 1 : 0)}, relayed};

		m.pingReply(ping::PROTO_MT, channel_id, ch, heard, line);
	}
}

int CompanionMesh::mtNameOfRelay(uint8_t relay, char *out, size_t cap)
{
	ContactInfo c;
	char base[32];
	uint32_t node;
	int found = 0;

	for (int i = 0; i < getNumContacts(); i++) {
		/* The cheap tests first: mtNodeOf derives a key when it is not cached. */
		if (getContactByIdx(i, c) && c.sync_since != 0 && (uint8_t)c.sync_since == relay &&
		    mtContactBase(c.name, base, sizeof(base)) && mtNodeOf(c, node)) {
			if (found++ == 0) {
				snprintf(out, cap, "%s", base);
			}
		}
	}
	return found > 1 ? 2 : found;
}

void CompanionMesh::MtSink::onNodeInfo(uint32_t from, const mt::User &user)
{
	const char *name = user.long_name[0] ? user.long_name : user.short_name;

	m._mtport.neighbours().setName(from, user.short_name);
	m._mtport.noteRole(from, user.role);
	m.mtUpsertContact(from, name, false, 0, 0);
	if (user.has_public_key) {
		m.mtStoreKey(from, user.public_key);
	}
}

void CompanionMesh::MtSink::onAck(uint32_t from, uint32_t request_id, uint32_t error_reason)
{
	if (error_reason == mt::ROUTING_PKI_UNKNOWN_PUBKEY) {
		/* The node does not hold our key: send it ours. */
		m.mtSendNodeInfoLimited(from, mt::kNoChannel, false);
		return;
	}
	if (error_reason != 0) {
		LOG_INF("mt: node %08x refused packet %08x (%u)", (unsigned)from,
			(unsigned)request_id, (unsigned)error_reason);
		return;
	}
	/* An ACK counts only from the node the message went to. */
	for (const auto &e : m._ack_table) {
		ContactInfo ci;
		uint32_t node;

		if (e.active && e.expected_ack == request_id && e.contact_idx >= 0 &&
		    m.getContactByIdx((uint32_t)e.contact_idx, ci) && m.mtNodeOf(ci, node) &&
		    node == from) {
			m.processAck((const uint8_t *)&request_id);
			return;
		}
	}
}

void CompanionMesh::MtSink::onNodeInfoRequest(uint8_t channel_id, uint32_t from)
{
	m.mtSendNodeInfoLimited(from, channel_id, false);
}

void CompanionMesh::MtSink::onUnknownKey(uint32_t from)
{
	/* Our NodeInfo carries our key; asking for theirs gets us theirs. */
	m.mtSendNodeInfoLimited(from, mt::kNoChannel, true);
}

bool CompanionMesh::MtSink::peerKey(uint32_t node, uint8_t key[32])
{
	return m._store && m._store->getMtKey(node, key);
}

void CompanionMesh::MtSink::onDirectText(uint32_t from, uint32_t id, uint32_t reply_id,
					 bool want_ack, const char *text, size_t len,
					 int8_t snr_x4, uint8_t hops)
{
	(void)reply_id;

	uint8_t key[PUB_KEY_SIZE];

	if (!m.mtKeyFor(from, key)) {
		return;
	}
	if (!m.lookupContactByPubKey(key, PUB_KEY_SIZE)) {
		m.mtUpsertContact(from, nullptr, false, 0, 0);
	}
	ContactInfo *c = m.lookupContactByPubKey(key, PUB_KEY_SIZE);

	if (!c) {
		return;
	}
	char line[MAX_TEXT_LEN + 1];

	if (len > sizeof(line) - 1) {
		len = sizeof(line) - 1;
	}
	memcpy(line, text, len);
	line[len] = '\0';

	mesh::Packet *pkt = m.obtainNewPacket();

	if (pkt) {
		pkt->header = ROUTE_TYPE_FLOOD | (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
		pkt->setPathHashSizeAndCount(1, hops);
		pkt->_snr = snr_x4;
	}
	m.onMessageRecv(*c, pkt, m.getRTCClock()->getCurrentTimeUnique(), line);
	if (pkt) {
		m.releasePacket(pkt);
	}
	if (want_ack) {
		m.mtSendAck(from, id);
	}
}

void CompanionMesh::MtSink::onPosition(uint32_t from, const mt::Position &pos)
{
	m.mtUpsertContact(from, nullptr, true, pos.lat_i, pos.lon_i);
}

/* ── TX ───────────────────────────────────────────────────────────────── */

/* The text may open with "@[name] ", the MeshCore form of a reply: the id of
 * the newest text by that name goes out as Meshtastic's reply_id and the tag
 * is dropped, as Meshtastic clients show the quoted text themselves. */
uint32_t CompanionMesh::mtReplyTarget(const char *text, size_t len, const char **body,
				      size_t *body_len) const
{
	*body = text;
	*body_len = len;
	if (len < 5 || text[0] != '@' || text[1] != '[') {
		return 0;
	}
	size_t close = 2;

	while (close < len && text[close] != ']') {
		close++;
	}
	size_t name_len = close - 2;

	if (close + 2 >= len || text[close + 1] != ' ' || name_len == 0 ||
	    name_len >= sizeof(_mt_recent[0].name)) {
		return 0;
	}
	for (int i = 1; i <= MT_RECENT_KEEP; i++) {
		const MtRecent &r = _mt_recent[(_mt_recent_next + MT_RECENT_KEEP - i) % MT_RECENT_KEEP];

		if (r.id != 0 && strlen(r.name) == name_len && memcmp(r.name, text + 2, name_len) == 0) {
			*body = text + close + 2;
			*body_len = len - close - 2;
			return r.id;
		}
	}
	return 0;
}

void CompanionMesh::mtNoteText(uint32_t id, const char *name)
{
	MtRecent &r = _mt_recent[_mt_recent_next];

	_mt_recent_next = (uint8_t)((_mt_recent_next + 1) % MT_RECENT_KEEP);
	r.id = id;
	snprintf(r.name, sizeof(r.name), "%s", name);
}

const char *CompanionMesh::mtRecentName(uint32_t id) const
{
	for (const MtRecent &r : _mt_recent) {
		if (r.id == id && r.id != 0) {
			return r.name;
		}
	}
	return nullptr;
}

bool CompanionMesh::mtSendChannelText(int ch_idx, uint32_t timestamp, const char *text, size_t len,
				      bool &sent)
{
	ChannelDetails ch;

	if (!getChannel(ch_idx, ch) || !mtEntryOfSlot(ch_idx)) {
		return false;
	}
	sent = false;
	if (!_mt_ready) {
		mtSetup();
	}
	uint8_t frame[mt::kMaxTxFrame];
	const char *body;
	size_t body_len;
	uint32_t reply = mtReplyTarget(text, len, &body, &body_len);
	size_t n = _mt.buildText((uint8_t)ch_idx, mt::kBroadcast, body, body_len, frame,
				 sizeof(frame), reply);

	mt::Header h;

	if (n > 0 && mt::parseHeader(frame, n, h)) {
		MtSent &s = _mt_sent[_mt_sent_next];

		_mt_sent_next = (uint8_t)((_mt_sent_next + 1) % MT_SENT_KEEP);
		s.id = h.id;
		s.timestamp = timestamp;
		s.channel = (uint8_t)ch_idx;
		s.n_relays = 0;
		mtNoteText(h.id, prefs.node_name);
		s.len = (uint8_t)(len < sizeof(s.text) ? len : sizeof(s.text));
		memcpy(s.text, text, s.len);
		sent = _mtport.send(frame, n);
	}
	return true;
}

/* A node repeated our frame: hand the app the MeshCore group message it would
 * have heard had MeshCore carried it, which is what it counts as a repeat. */
void CompanionMesh::mtEchoHeard(uint32_t id, uint8_t relay, float snr, float rssi)
{
	for (MtSent &s : _mt_sent) {
		ChannelDetails ch;

		if (s.id != id || s.len == 0 || !getChannel(s.channel, ch)) {
			continue;
		}
		/* One echo per repeater: the relay byte is its path, so each repeater
		 * is a distinct packet to the app, and one heard twice counts once. */
		if (relay == (uint8_t)_mt.self()) {
			return;
		}
		bool known = false;

		for (int i = 0; i < s.n_relays; i++) {
			known = known || s.relays[i] == relay;
		}
		if (known || s.n_relays >= MT_RELAYS_KEEP) {
			return;
		}
		s.relays[s.n_relays++] = relay;
		uint8_t body[5 + MAX_TEXT_LEN + 32];

		memcpy(body, &s.timestamp, 4);
		body[4] = 0;  /* TXT_TYPE_PLAIN */
		int n = snprintf((char *)&body[5], sizeof(body) - 5, "%s: ", prefs.node_name);
		size_t room = sizeof(body) - 5 - (size_t)n;
		size_t t = s.len < room ? s.len : room;

		memcpy(&body[5 + n], s.text, t);
		mesh::Packet *pkt = createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, ch.channel, body,
							5 + (size_t)n + t);

		if (pkt) {
			uint8_t raw[MAX_TRANS_UNIT + 1];

			pkt->header |= ROUTE_TYPE_FLOOD;
			pkt->setPathHashSizeAndCount(1, 1);
			pkt->path[0] = relay;

			logRxRaw(snr, rssi, raw, pkt->writeTo(raw));
			releasePacket(pkt);
		}
		return;
	}
}

bool CompanionMesh::mtSendDirectText(const ContactInfo &contact, const char *text, size_t len,
				     bool &sent, uint32_t &ack)
{
	uint32_t node;
	uint8_t key[32];

	if (!mtNodeOf(contact, node)) {
		return false;
	}
	sent = false;
	ack = 0;
	if (!_mt_ready) {
		mtSetup();
	}
	/* Without the node's public key there is nothing a Meshtastic node would accept. */
	if (!_store || !_store->getMtKey(node, key)) {
		return true;
	}
	uint8_t frame[mt::kMaxTxFrame];
	size_t n = _mt.buildDirectText(node, key, text, len, frame, sizeof(frame), 0, true);
	mt::Header h;

	if (n > 0 && mt::parseHeader(frame, n, h)) {
		/* The packet id is what the node's ACK answers: the app's ack code. */
		sent = _mtport.send(frame, n);
		if (sent) {
			addPendingAck(h.id, mtContactIndex(contact));
			ack = h.id;
		}
	}
	return true;
}

int CompanionMesh::mtContactIndex(const ContactInfo &contact)
{
	ContactInfo ci;

	for (int i = 0; i < getNumContacts(); i++) {
		if (getContactByIdx((uint32_t)i, ci) && ci.id.matches(contact.id)) {
			return i;
		}
	}
	return -1;
}

/* A contact keeps its key while some contact holds its node number. */
bool CompanionMesh::mtKeyInUse(uint32_t node)
{
	ContactInfo c;

	for (int i = 0; i < getNumContacts(); i++) {
		if (getContactByIdx((uint32_t)i, c) && c.sync_since == node) {
			return true;
		}
	}
	return false;
}

void CompanionMesh::mtStoreKey(uint32_t node, const uint8_t key[32])
{
	if (!_store) {
		return;
	}
	auto in_use = [](uint32_t n, void *self) { return static_cast<CompanionMesh *>(self)->mtKeyInUse(n); };

	for (uint32_t n : _mt_key_stored) {
		if (n == node) {
			return;
		}
	}
	const ZephyrDataStore::MtKeyResult r =
		_store->putMtKey(node, key, MAX_CONTACTS, in_use, this, (uint32_t)getNumContacts());

	if (r == ZephyrDataStore::MTKEY_STORED || r == ZephyrDataStore::MTKEY_SAME) {
		_mt_key_stored[_mt_key_stored_next] = node;
		_mt_key_stored_next = (uint8_t)((_mt_key_stored_next + 1) % MT_KEY_CACHE);
	}
	switch (r) {
	case ZephyrDataStore::MTKEY_STORED:
		LOG_INF("mt: key of %08x stored", (unsigned)node);
		break;
	case ZephyrDataStore::MTKEY_CONFLICT:
		LOG_WRN("mt: node %08x sent another key, keeping the first", (unsigned)node);
		break;
	case ZephyrDataStore::MTKEY_FAILED:
		LOG_WRN("mt: key of %08x not stored", (unsigned)node);
		break;
	default:
		break;
	}
}

void CompanionMesh::mtSendAck(uint32_t to, uint32_t request_id)
{
	uint8_t key[32], frame[mt::kMaxTxFrame];

	if (!_store || !_store->getMtKey(to, key)) {
		return;
	}
	size_t n = _mt.buildAck(to, request_id, key, frame, sizeof(frame));

	if (n > 0) {
		_mtport.send(frame, n);
	}
}

void CompanionMesh::mtSendNodeInfoTo(uint32_t node, uint8_t channel_id, bool want_response)
{
	int ch = channel_id == mt::kNoChannel || !_mt.hasChannel(channel_id) ? mtFirstChannel()
									   : (int)channel_id;
	uint8_t frame[mt::kMaxTxFrame];
	size_t n = ch < 0 ? 0 : _mt.buildNodeInfoTo((uint8_t)ch, node, want_response, frame,
						    sizeof(frame));

	if (n > 0) {
		_mtport.send(frame, n);
	}
}

static constexpr uint32_t kMtNodeInfoIntervalMs = 3600UL * 1000UL;

void CompanionMesh::mtSendNodeInfoLimited(uint32_t node, uint8_t channel_id, bool want_response)
{
	if (_mt_info_limit.allow(node, (uint32_t)_ms->getMillis(), kMtNodeInfoIntervalMs)) {
		mtSendNodeInfoTo(node, channel_id, want_response);
	}
}

/* Marking a contact favourite asks its node for what is missing: name or key. */
bool CompanionMesh::mtFavouriteAsks(const ContactInfo &existing, uint8_t new_flags, uint32_t node)
{
	char base[32];
	uint8_t key[32];
	bool has_name = mtContactBase(existing.name, base, sizeof(base)) && base[0] != '!';
	bool has_key = _store && _store->getMtKey(node, key);

	return mt::favouriteAsksNodeInfo(existing.isFav(), (new_flags & 0x01) != 0, has_name, has_key);
}

/* An app-removed contact takes its key with it, so a node that changed key can be learned again. */
void CompanionMesh::mtForgetContact(const ContactInfo &contact)
{
	uint32_t node;

	if (_store && mtNodeOf(contact, node)) {
		_store->removeMtKey(node);
		for (uint32_t &n : _mt_key_stored) {
			if (n == node) {
				n = 0;
			}
		}
	}
}

/* Our NodeInfo on every Meshtastic channel is too chatty: the first one only,
 * as Meshtastic's own default sends it on the primary channel. */
void CompanionMesh::mtNodeInfoTick()
{
	if (getNumChannels() == 0) {
		return;
	}
	if (!_mt_ready) {
		mtSetup();
	}
	int ch = mtFirstChannel();

	if (ch < 0) {
		return;
	}
	MtRelayPort::Source none;

	_mtport.tick((uint32_t)_ms->getMillis(), prefs.mt, (uint8_t)ch, none);
}

#endif /* CONFIG_ZEPHTACORE_MESHTASTIC */
