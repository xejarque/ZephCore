#include "test.h"
#include "dispatcher_fixture.h"
#include "VirtualRoom.h"
#include "ZCommand.h"
#include <mesh/Mesh.h>
#include <helpers/AdvertDataHelpers.h>
#include <cstring>
#include <string>

namespace {

struct Rng : mesh::RNG {
	uint8_t seq = 1;
	void random(uint8_t *dest, size_t len) override
	{
		for (size_t i = 0; i < len; i++) {
			dest[i] = seq++;
		}
	}
};

struct Rtc : mesh::RTCClock {
	uint32_t now = 1700000000;
	uint32_t getCurrentTime() override { return now; }
	void setCurrentTime(uint32_t t) override { now = t; }
};

struct Tables : mesh::MeshTables {
	bool wasSeen(const mesh::Packet *) override { return false; }
	void markSeen(const mesh::Packet *) override {}
	void clear(const mesh::Packet *) override {}
};

struct DualIdentityMesh : mesh::Mesh {
	mesh::LocalIdentity sec_id;
	mesh::LocalIdentity client_id;
	uint8_t client_primary_secret[PUB_KEY_SIZE] = {};
	uint8_t client_sec_secret[PUB_KEY_SIZE] = {};
	bool allow_fwd = true;

	int primary_anon_calls = 0;
	int sec_anon_calls = 0;
	int primary_peer_calls = 0;
	int sec_peer_calls = 0;
	int primary_path_calls = 0;
	int sec_path_calls = 0;
	int advert_calls = 0;
	bool sec_path_reciprocal = false;
	uint8_t last_sec_type = 0;
	std::string last_sec_payload;

	DualIdentityMesh(mesh::Radio &r, mesh::MillisecondClock &ms, mesh::RNG &rng,
			 mesh::RTCClock &rtc, mesh::PacketManager &mgr, mesh::MeshTables &t)
		: mesh::Mesh(r, ms, rng, rtc, mgr, t) {}

	using mesh::Mesh::onRecvPacket;

	bool allowPacketForward(const mesh::Packet *) override { return allow_fwd; }
	const mesh::LocalIdentity *getSecondaryIdentity() const override { return &sec_id; }

	int searchPeersByHash(const uint8_t *hash) override
	{
		return client_id.isHashMatch(hash) ? 1 : 0;
	}
	void getPeerSharedSecret(uint8_t *dest_secret, int) override
	{
		memcpy(dest_secret, client_primary_secret, PUB_KEY_SIZE);
	}
	void onAnonDataRecv(mesh::Packet *, const uint8_t *, const mesh::Identity &, uint8_t *, size_t) override
	{
		primary_anon_calls++;
	}
	void onPeerDataRecv(mesh::Packet *, uint8_t, int, const uint8_t *, uint8_t *, size_t) override
	{
		primary_peer_calls++;
	}
	bool onPeerPathRecv(mesh::Packet *, int, const uint8_t *, uint8_t *, uint8_t, uint8_t, uint8_t *, uint8_t) override
	{
		primary_path_calls++;
		return false;
	}

	int searchSecondaryPeersByHash(const uint8_t *hash) override
	{
		return client_id.isHashMatch(hash) ? 1 : 0;
	}
	void getSecondaryPeerSharedSecret(uint8_t *dest_secret, int) override
	{
		memcpy(dest_secret, client_sec_secret, PUB_KEY_SIZE);
	}
	void onSecondaryAnonDataRecv(mesh::Packet *, const uint8_t *, const mesh::Identity &, uint8_t *data, size_t len) override
	{
		sec_anon_calls++;
		last_sec_payload.assign((const char *)data, len);
	}
	void onSecondaryPeerDataRecv(mesh::Packet *, uint8_t type, int, const uint8_t *, uint8_t *data, size_t len) override
	{
		sec_peer_calls++;
		last_sec_type = type;
		last_sec_payload.assign((const char *)data, len);
	}
	bool onSecondaryPeerPathRecv(mesh::Packet *, int, const uint8_t *, uint8_t *, uint8_t, uint8_t, uint8_t *, uint8_t) override
	{
		sec_path_calls++;
		return sec_path_reciprocal;
	}
	void onAdvertRecv(mesh::Packet *, const mesh::Identity &, uint32_t, const uint8_t *, size_t) override
	{
		advert_calls++;
	}
};

struct Fixture {
	test_support::Clock clock;
	test_support::Radio radio;
	StaticPoolPacketManager pool;
	Rng rng;
	Rtc rtc;
	Tables tables;
	DualIdentityMesh mesh{radio, clock, rng, rtc, pool, tables};

	Fixture()
	{
		uint8_t s1[32] = {1}, s2[32] = {2}, s3[32] = {3};
		mesh.self_id.fromSeed(s1);
		mesh.sec_id.fromSeed(s2);
		mesh.client_id.fromSeed(s3);
		mesh.self_id.calcSharedSecret(mesh.client_primary_secret, mesh.client_id);
		mesh.sec_id.calcSharedSecret(mesh.client_sec_secret, mesh.client_id);
	}
	~Fixture()
	{
		while (pool.getOutboundTotal() > 0) {
			pool.free(pool.removeOutboundByIdx(0));
		}
	}
};

bool listsWord(const std::string &line, const std::string &word)
{
	for (size_t at = line.find(word); at != std::string::npos; at = line.find(word, at + 1)) {
		const size_t end = at + word.size();
		if (end == line.size() || line[end] == ' ' || line[end] == '/') {
			return true;
		}
	}
	return false;
}

}  // namespace

TEST(vroom_name, "UNIT-VROOM-001", "Derived room advert names reflect access mode, respect 31-byte cap, and never split UTF-8") {
	char out[32];
	vroom::formatRoomName("Hilltop", vroom::ACCESS_OPEN, out, sizeof(out));
	CHECK(std::string(out) == "Hilltop Room");
	vroom::formatRoomName("Hilltop", vroom::ACCESS_READ_ONLY, out, sizeof(out));
	CHECK(std::string(out) == "Hilltop Room [RO]");
	vroom::formatRoomName("Hilltop", vroom::ACCESS_PRIVATE, out, sizeof(out));
	CHECK(std::string(out) == "Hilltop Room [Priv]");

	vroom::formatRoomName("", vroom::ACCESS_PRIVATE, out, sizeof(out));
	CHECK(std::string(out) == "Node Room [Priv]");

	// 31-byte cap: " Room [Priv]" is 12 bytes, leaving 19 bytes for node_name.
	vroom::formatRoomName("VeryLongRepeaterNodeNameExceedingLimit", vroom::ACCESS_PRIVATE, out, sizeof(out));
	CHECK(strlen(out) == 31);
	CHECK(std::string(out) == "VeryLongRepeaterNod Room [Priv]");

	// UTF-8 safety: 18 ASCII bytes + 2-byte U+00E9 ("é") = 20 bytes; budget 19 must drop "é" cleanly.
	vroom::formatRoomName("123456789012345678\xC3\xA9", vroom::ACCESS_PRIVATE, out, sizeof(out));
	CHECK(std::string(out) == "123456789012345678 Room [Priv]");
}

TEST(vroom_policy, "UNIT-VROOM-002", "Access modes, login permission resolution, post gating, and secondary hash checks enforce room policy") {
	uint8_t mode = 9;
	CHECK(vroom::parseAccess("open", mode) && mode == vroom::ACCESS_OPEN);
	CHECK(vroom::parseAccess("readonly", mode) && mode == vroom::ACCESS_READ_ONLY);
	CHECK(vroom::parseAccess("ro", mode) && mode == vroom::ACCESS_READ_ONLY);
	CHECK(vroom::parseAccess("private", mode) && mode == vroom::ACCESS_PRIVATE);
	CHECK(vroom::parseAccess("priv", mode) && mode == vroom::ACCESS_PRIVATE);
	CHECK(!vroom::parseAccess("public", mode));
	CHECK(std::string(vroom::accessName(vroom::ACCESS_OPEN)) == "open");
	CHECK(std::string(vroom::accessName(vroom::ACCESS_READ_ONLY)) == "readonly");
	CHECK(std::string(vroom::accessName(vroom::ACCESS_PRIVATE)) == "private");

	vroom::Config cfg;
	vroom::init(cfg);
	CHECK(cfg.enabled == 0 && cfg.access == vroom::ACCESS_OPEN && cfg.password[0] == '\0');

	uint8_t perms = 0xFF;
	// Open mode: blank login gets RW; admin gets ADMIN.
	CHECK(vroom::resolveLoginPerms(cfg, false, false, perms) && perms == vroom::ROLE_READ_WRITE && vroom::canPost(perms));
	CHECK(vroom::resolveLoginPerms(cfg, true, false, perms) && perms == vroom::ROLE_ADMIN && vroom::canPost(perms));

	// Read-only mode: blank login gets GUEST (cannot post); member password gets RW; admin gets ADMIN.
	cfg.access = vroom::ACCESS_READ_ONLY;
	strcpy(cfg.password, "secret");
	CHECK(vroom::resolveLoginPerms(cfg, false, false, perms) && perms == vroom::ROLE_GUEST && !vroom::canPost(perms));
	CHECK(vroom::resolveLoginPerms(cfg, false, true, perms) && perms == vroom::ROLE_READ_WRITE && vroom::canPost(perms));
	CHECK(vroom::resolveLoginPerms(cfg, true, false, perms) && perms == vroom::ROLE_ADMIN && vroom::canPost(perms));

	// Private mode: blank/wrong rejected; member password gets RW; admin gets ADMIN.
	cfg.access = vroom::ACCESS_PRIVATE;
	CHECK(!vroom::resolveLoginPerms(cfg, false, false, perms));
	CHECK(vroom::resolveLoginPerms(cfg, false, true, perms) && perms == vroom::ROLE_READ_WRITE && vroom::canPost(perms));
	CHECK(vroom::resolveLoginPerms(cfg, true, false, perms) && perms == vroom::ROLE_ADMIN && vroom::canPost(perms));

	// Corrupt config bytes sanitize cleanly.
	cfg.enabled = 5;
	cfg.access = 9;
	vroom::sanitize(cfg);
	CHECK(cfg.enabled == 0 && cfg.access == vroom::ACCESS_OPEN);

	uint8_t a[32] = {0xAA};
	uint8_t b[32] = {0xAA};
	CHECK(!vroom::isValidSecondaryHash(a, b));
	b[0] = 0xAB;
	CHECK(vroom::isValidSecondaryHash(a, b));
}

TEST(vroom_mesh_routing, "UNIT-VROOM-003", "Secondary identity skips self-adverts, agrees on ECDH, and forwards floods on primary self_id") {
	Fixture f;
	uint8_t sender_seed[32] = {4};
	mesh::LocalIdentity sender_id;
	sender_id.fromSeed(sender_seed);
	CHECK(vroom::isValidSecondaryHash(f.mesh.self_id.pub_key, f.mesh.sec_id.pub_key));

	uint8_t sender_sec_secret[PUB_KEY_SIZE];
	sender_id.calcSharedSecret(sender_sec_secret, f.mesh.sec_id);
	uint8_t sec_sender_secret[PUB_KEY_SIZE];
	f.mesh.sec_id.calcSharedSecret(sec_sender_secret, sender_id);
	CHECK(memcmp(sender_sec_secret, sec_sender_secret, PUB_KEY_SIZE) == 0);

	// 1. Secondary self advert is ignored.
	mesh::Packet *sec_adv = f.mesh.createAdvert(f.mesh.sec_id, (const uint8_t *)"\x03R", 2);
	CHECK(sec_adv != nullptr);
	sec_adv->header |= ROUTE_TYPE_FLOOD;
	auto act = f.mesh.onRecvPacket(sec_adv);
	CHECK(f.mesh.advert_calls == 0 && act == ACTION_RELEASE);
	f.pool.free(sec_adv);

	// 2. Third-party flood advert is accepted and forwarded using primary self_id hash, not sec_id.
	mesh::Packet *other_adv = f.mesh.createAdvert(sender_id, (const uint8_t *)"\x01N", 2);
	CHECK(other_adv != nullptr);
	other_adv->header |= ROUTE_TYPE_FLOOD;
	act = f.mesh.onRecvPacket(other_adv);
	CHECK(f.mesh.advert_calls == 1 && (act >> 24) >= 1);
	CHECK(other_adv->getPathHashCount() == 1 && other_adv->path[0] == f.mesh.self_id.pub_key[0]);
	CHECK(other_adv->path[0] != f.mesh.sec_id.pub_key[0]);
	f.pool.free(other_adv);
}

TEST(vroom_zcmd_help, "UNIT-VROOM-004", "z.room.* keys appear in the z. help tree only when vroom=true and fit remote replies") {
	char root_off[256] = "";
	char root_on[256] = "";
	zcmd::appendGroups(root_off, sizeof(root_off), 0, "", true, false);
	zcmd::appendGroups(root_on, sizeof(root_on), 0, "", true, true);
	CHECK(!listsWord(root_off, " room"));
	CHECK(listsWord(root_on, " room"));

	char out[161] = "";
	CHECK(!zcmd::helpReply("get z.room help", true, out, sizeof(out), false));
	CHECK(zcmd::helpReply("get z.room help", true, out, sizeof(out), true));
	CHECK(std::string(out) == "> z.room: on access password post advert acl");

	for (const zcmd::Help &h : zcmd::kHelp) {
		if (strncmp(h.key, "room.", 5) != 0) {
			continue;
		}
		std::string cmd = std::string("get z.") + h.key + " help";
		CHECK(zcmd::helpReply(cmd.c_str(), true, out, sizeof(out), true));
		CHECK(strlen(out) < 158);
		CHECK(listsWord("> z.room: on access password post advert acl", std::string(" ") + (h.key + 5)));
	}
}

TEST(vroom_blank_relogin, "UNIT-VROOM-005", "A blank-password re-login lifts a guest to read-write only in an open room") {
	vroom::Config cfg;
	vroom::init(cfg);

	cfg.access = vroom::ACCESS_OPEN;
	CHECK(vroom::blankReloginPerms(cfg, vroom::ROLE_GUEST) == vroom::ROLE_READ_WRITE);
	CHECK(vroom::blankReloginPerms(cfg, vroom::ROLE_READ_ONLY) == vroom::ROLE_READ_WRITE);
	CHECK(vroom::blankReloginPerms(cfg, vroom::ROLE_READ_WRITE) == vroom::ROLE_READ_WRITE);
	CHECK(vroom::blankReloginPerms(cfg, 0x80 | vroom::ROLE_GUEST) == (0x80 | vroom::ROLE_READ_WRITE));

	cfg.access = vroom::ACCESS_READ_ONLY;
	CHECK(vroom::blankReloginPerms(cfg, vroom::ROLE_GUEST) == vroom::ROLE_GUEST);
	CHECK(vroom::blankReloginPerms(cfg, vroom::ROLE_READ_WRITE) == vroom::ROLE_READ_WRITE);

	cfg.access = vroom::ACCESS_PRIVATE;
	CHECK(vroom::blankReloginPerms(cfg, vroom::ROLE_GUEST) == vroom::ROLE_GUEST);
	CHECK(vroom::blankReloginPerms(cfg, vroom::ROLE_READ_WRITE) == vroom::ROLE_READ_WRITE);
}
