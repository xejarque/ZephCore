#include "test.h"
#include <cstring>
#include <string>
#include <vector>
#include <helpers/ContactChange.h>
#include <helpers/meshtastic/MtBridge.h>
#include <helpers/meshtastic/MtPolicy.h>
#include <helpers/meshtastic/MtDiscriminate.h>
#include <helpers/meshtastic/MtConfig.h>
#include <helpers/meshtastic/MtFirewall.h>
#include <helpers/meshtastic/MtZeroHop.h>
#include <mesh/ContentionTracker.h>
#include <mesh/Packet.h>

using namespace mt;

namespace {

// Independent AES-128 block encryption (FIPS-197), so the cipher tests do not
// depend on the production PSA wrapper.
const uint8_t kSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};

uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }

// Key of 16 (AES-128) or 32 (AES-256) bytes.
void refAesKeyed(const uint8_t* key, int key_len, const uint8_t in[16], uint8_t out[16]) {
    const int nk = key_len / 4, rounds = nk + 6, total = 16 * (rounds + 1);
    uint8_t rk[240];
    std::memcpy(rk, key, (size_t)key_len);
    uint8_t rcon = 1;
    for (int i = key_len; i < total; i += 4) {
        uint8_t t[4] = {rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1]};
        if ((i / 4) % nk == 0) {
            uint8_t a = t[0];
            t[0] = (uint8_t)(kSbox[t[1]] ^ rcon); t[1] = kSbox[t[2]]; t[2] = kSbox[t[3]]; t[3] = kSbox[a];
            rcon = xtime(rcon);
        } else if (nk > 6 && (i / 4) % nk == 4) {
            for (int j = 0; j < 4; j++) t[j] = kSbox[t[j]];
        }
        for (int j = 0; j < 4; j++) rk[i + j] = (uint8_t)(rk[i - key_len + j] ^ t[j]);
    }
    uint8_t s[16];
    for (int i = 0; i < 16; i++) s[i] = (uint8_t)(in[i] ^ rk[i]);
    for (int round = 1; round <= rounds; round++) {
        uint8_t t[16];
        for (int i = 0; i < 16; i++) t[i] = kSbox[s[i]];
        uint8_t u[16];  // ShiftRows
        for (int c = 0; c < 4; c++)
            for (int r = 0; r < 4; r++) u[c * 4 + r] = t[((c + r) % 4) * 4 + r];
        if (round < rounds) {
            for (int c = 0; c < 4; c++) {
                uint8_t* p = &u[c * 4];
                uint8_t a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
                p[0] = (uint8_t)(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
                p[1] = (uint8_t)(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
                p[2] = (uint8_t)(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
                p[3] = (uint8_t)((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
            }
        }
        for (int i = 0; i < 16; i++) s[i] = (uint8_t)(u[i] ^ rk[round * 16 + i]);
    }
    std::memcpy(out, s, 16);
}

void refAes(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) { refAesKeyed(key, 16, in, out); }
void refAes256(const uint8_t key[32], const uint8_t in[16], uint8_t out[16]) { refAesKeyed(key, 32, in, out); }

// Independent SHA-256 (FIPS 180-4), one shot.
void refSha256(const uint8_t* msg, size_t len, uint8_t out[32]) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::vector<uint8_t> m(msg, msg + len);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 7; i >= 0; i--) m.push_back((uint8_t)(bits >> (8 * i)));
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)m[off + 4 * i] << 24) | ((uint32_t)m[off + 4 * i + 1] << 16) |
                   ((uint32_t)m[off + 4 * i + 2] << 8) | m[off + 4 * i + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(h[i] >> (24 - 8 * j));
}

const uint8_t kDefaultKey[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

struct Capture : Sink {
    struct Text { uint8_t ch; uint32_t from, to; std::string text; uint8_t hops; };
    std::vector<Text> texts;
    std::vector<std::pair<uint32_t, uint32_t>> ids;  // (packet id, reply id) per text
    std::vector<std::pair<uint32_t, User>> users;
    std::vector<std::pair<uint32_t, Position>> positions;
    void onText(uint8_t ch, uint32_t from, uint32_t to, uint32_t id, uint32_t reply, const char* t, size_t n, int8_t, uint8_t hops, uint8_t) override {
        ids.push_back({id, reply});
        texts.push_back({ch, from, to, std::string(t, n), hops});
    }
    void onNodeInfo(uint32_t from, const User& u) override { users.push_back({from, u}); }
    void onPosition(uint32_t from, const Position& p) override { positions.push_back({from, p}); }
    struct Direct { uint32_t from, id, reply; bool want_ack; std::string text; };
    std::vector<Direct> directs;
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> keys;  // known peer keys
    void onDirectText(uint32_t from, uint32_t id, uint32_t reply, bool want_ack, const char* t, size_t n, int8_t, uint8_t) override {
        directs.push_back({from, id, reply, want_ack, std::string(t, n)});
    }
    struct Ack { uint32_t from, id, reason; };
    std::vector<Ack> acks;
    std::vector<std::pair<uint8_t, uint32_t>> info_requests;  // (channel, from)
    std::vector<uint32_t> unknown_keys;
    void onAck(uint32_t from, uint32_t id, uint32_t reason) override { acks.push_back({from, id, reason}); }
    void onNodeInfoRequest(uint8_t ch, uint32_t from) override { info_requests.push_back({ch, from}); }
    void onUnknownKey(uint32_t from) override { unknown_keys.push_back(from); }
    bool peerKey(uint32_t node, uint8_t key[32]) override {
        for (auto& k : keys) if (k.first == node) { std::memcpy(key, k.second.data(), 32); return true; }
        return false;
    }
};

}  // namespace

TEST(mt_aes_reference, "UNIT-MT-001", "Reference AES-128 matches the FIPS-197 vector") {
    uint8_t key[16], in[16], out[16];
    for (int i = 0; i < 16; i++) { key[i] = (uint8_t)i; in[i] = (uint8_t)(i * 0x11); }
    refAes(key, in, out);
    const uint8_t expect[16] = {0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
    CHECK(std::memcmp(out, expect, 16) == 0);
}

TEST(mt_channel_key_hash, "UNIT-MT-002", "Default key variants and the LongFast channel hash") {
    uint8_t key[16];
    bool enc = false;
    const uint8_t one = 1, two = 2, zero = 0;
    CHECK(expandPsk(&one, 1, key, enc) && enc && std::memcmp(key, kDefaultKey, 16) == 0);
    CHECK(expandPsk(&two, 1, key, enc) && enc && key[15] == 0x02 && std::memcmp(key, kDefaultKey, 15) == 0);
    CHECK(expandPsk(&zero, 1, key, enc) && !enc);
    uint8_t own[16];
    for (int i = 0; i < 16; i++) own[i] = (uint8_t)(0xA0 + i);
    CHECK(expandPsk(own, 16, key, enc) && enc && std::memcmp(key, own, 16) == 0);
    CHECK(!expandPsk(own, 7, key, enc) && !expandPsk(own, 32, key, enc) && !expandPsk(own, 0, key, enc));
    // Meshtastic's default channel: "LongFast" with the default key hashes to 8.
    CHECK(channelHash("LongFast", kDefaultKey, true) == 0x08);
    CHECK(channelHash("SFNarrow", kDefaultKey, true) == 0x20);  // value in meshtacore keys-core.js
    CHECK(channelHash("AB", kDefaultKey, false) == (uint8_t)('A' ^ 'B'));
}

TEST(mt_header, "UNIT-MT-003", "Header fields, flag bits and bounds") {
    uint8_t raw[16];
    Header h = {0x11223344u, 0xAABBCCDDu, 0x01020304u, (uint8_t)(0x63 | 0x08), 0x08, 0x00, 0x7E};
    writeHeader(h, raw);
    const uint8_t expect[16] = {0x44,0x33,0x22,0x11, 0xDD,0xCC,0xBB,0xAA, 0x04,0x03,0x02,0x01, 0x6B,0x08,0x00,0x7E};
    CHECK(std::memcmp(raw, expect, 16) == 0);
    Header g;
    CHECK(parseHeader(raw, 16, g));
    CHECK(g.to == h.to && g.from == h.from && g.id == h.id && g.channel == 8 && g.relay == 0x7E);
    CHECK(g.hopLimit() == 3 && g.hopStart() == 3 && g.wantAck());
    CHECK(!parseHeader(raw, 15, g));
}

TEST(mt_ctr, "UNIT-MT-004", "CTR keystream layout, counter carry and round trip") {
    uint32_t from = 0xAABBCCDDu, id = 0x01020304u;
    std::vector<uint8_t> z(48, 0);
    ctrCrypt(refAes, kDefaultKey, from, id, z.data(), z.size());
    // Independent keystream: block n of AES(key, id64 LE | from LE | n big endian).
    for (uint32_t n = 0; n < 3; n++) {
        uint8_t ctr[16] = {0x04,0x03,0x02,0x01, 0,0,0,0, 0xDD,0xCC,0xBB,0xAA, 0,0,0,(uint8_t)n}, ks[16];
        refAes(kDefaultKey, ctr, ks);
        CHECK(std::memcmp(&z[n * 16], ks, 16) == 0);
    }
    // A partial last block and an in-place round trip.
    std::vector<uint8_t> m(37);
    for (size_t i = 0; i < m.size(); i++) m[i] = (uint8_t)(i * 7 + 1);
    std::vector<uint8_t> c = m;
    ctrCrypt(refAes, kDefaultKey, from, id, c.data(), c.size());
    CHECK(c != m);
    ctrCrypt(refAes, kDefaultKey, from, id, c.data(), c.size());
    CHECK(c == m);
    // The counter carries across the low byte into the next.
    std::vector<uint8_t> big(16 * 258, 0);
    ctrCrypt(refAes, kDefaultKey, from, id, big.data(), big.size());
    uint8_t ctr[16] = {0x04,0x03,0x02,0x01, 0,0,0,0, 0xDD,0xCC,0xBB,0xAA, 0,0,1,0}, ks[16];
    refAes(kDefaultKey, ctr, ks);
    CHECK(std::memcmp(&big[256 * 16], ks, 16) == 0);
}

TEST(mt_protobuf_data, "UNIT-MT-005", "Data golden bytes, round trip and malformed input") {
    const uint8_t hi[2] = {'h', 'i'};
    Data d = {};
    d.portnum = PORT_TEXT; d.payload = hi; d.payload_len = 2;
    uint8_t buf[32];
    size_t n = encodeData(d, buf, sizeof(buf));
    const uint8_t golden[6] = {0x08, 0x01, 0x12, 0x02, 'h', 'i'};
    CHECK(n == 6 && std::memcmp(buf, golden, 6) == 0);
    Data g;
    CHECK(decodeData(buf, n, g) && g.portnum == 1 && g.payload_len == 2 && std::memcmp(g.payload, "hi", 2) == 0);
    CHECK(encodeData(d, buf, 5) == 0);

    Data full = {};
    full.portnum = 300; full.dest = 0x01020304; full.source = 5; full.request_id = 0xFFFFFFFF; full.want_response = true;
    n = encodeData(full, buf, sizeof(buf));
    CHECK(decodeData(buf, n, g) && g.portnum == 300 && g.dest == 0x01020304 && g.source == 5 &&
          g.request_id == 0xFFFFFFFFu && g.want_response);

    // Reply (field 7) and reaction (field 8) are fixed32, after request_id.
    Data rep = {};
    rep.portnum = PORT_TEXT; rep.payload = hi; rep.payload_len = 2; rep.reply_id = 0xAABBCCDDu; rep.emoji = 1;
    n = encodeData(rep, buf, sizeof(buf));
    const uint8_t golden_rep[] = {0x08, 0x01, 0x12, 0x02, 'h', 'i', 0x3D, 0xDD, 0xCC, 0xBB, 0xAA, 0x45, 0x01, 0, 0, 0};
    CHECK(n == sizeof(golden_rep) && std::memcmp(buf, golden_rep, n) == 0);
    CHECK(decodeData(buf, n, g) && g.reply_id == 0xAABBCCDDu && g.emoji == 1);

    CHECK(!decodeData(golden, 5, g));                       // payload runs past the end
    const uint8_t no_port[4] = {0x12, 0x02, 'h', 'i'};
    CHECK(!decodeData(no_port, 4, g));
    const uint8_t port0[2] = {0x08, 0x00};
    CHECK(!decodeData(port0, 2, g));
    const uint8_t port512[3] = {0x08, 0x80, 0x04};
    CHECK(!decodeData(port512, 3, g));
    const uint8_t group[3] = {0x08, 0x01, 0x0B};            // wire type 3 is never valid
    CHECK(!decodeData(group, 3, g));
    const uint8_t unknown[6] = {0x08, 0x01, 0xA8, 0x06, 0x07, 0x00};  // unknown varint field 101, then junk-free end
    CHECK(decodeData(unknown, 5, g) && g.portnum == 1);
}

TEST(mt_protobuf_pos_user, "UNIT-MT-006", "Position and User encode, decode and truncation") {
    Position p = {};
    p.has_lat = p.has_lon = p.has_altitude = true;
    p.lat_i = 411234567; p.lon_i = -22345678; p.altitude = -12; p.time = 1700000000u;
    uint8_t buf[64];
    size_t n = encodePosition(p, buf, sizeof(buf));
    CHECK(n > 0 && buf[0] == 0x0D);  // field 1, fixed32
    Position q;
    CHECK(decodePosition(buf, n, q) && q.has_lat && q.has_lon && q.lat_i == 411234567 &&
          q.lon_i == -22345678 && q.has_altitude && q.altitude == -12 && q.time == 1700000000u);
    CHECK(!decodePosition(buf, 3, q));
    User u = {};
    std::strcpy(u.id, "!aabbccdd"); std::strcpy(u.long_name, "Zeph Node"); std::strcpy(u.short_name, "ZN01");
    u.hw_model = 255;
    n = encodeUser(u, buf, sizeof(buf));
    User v;
    CHECK(decodeUser(buf, n, v) && std::strcmp(v.id, "!aabbccdd") == 0 && std::strcmp(v.long_name, "Zeph Node") == 0 &&
          std::strcmp(v.short_name, "ZN01") == 0 && v.hw_model == 255);
    // Over-long strings are cut to the buffers, never overrun.
    uint8_t longname[8 + 60] = {0x12, 60};
    std::memset(&longname[2], 'x', 60);
    CHECK(decodeUser(longname, 62, v) && std::strlen(v.long_name) == sizeof(v.long_name) - 1);
}

static Bridge makeNode(uint32_t node, const char* name, uint32_t seed) {
    Bridge b(refAes);
    b.setSelf(node, name, "ZN", seed);
    const uint8_t psk = 1;
    CHECK(b.setChannel(3, "LongFast", &psk, 1));
    return b;
}

TEST(mt_bridge_text, "UNIT-MT-007", "Text sent by one node is delivered once on the other") {
    Bridge a = makeNode(0xA1A1A1A1u, "alpha", 7), b = makeNode(0xB2B2B2B2u, "beta", 9);
    uint8_t frame[kMaxFrame], relay[kMaxFrame];
    size_t relay_len = 99;
    size_t n = a.buildText(3, kBroadcast, "hola mundo", 10, frame, sizeof(frame));
    CHECK(n > kHeaderLen && n <= kMaxTxFrame);
    Header h;
    CHECK(parseHeader(frame, n, h) && h.from == 0xA1A1A1A1u && h.to == kBroadcast && h.channel == 0x08 &&
          h.hopLimit() == 3 && h.hopStart() == 3 && h.relay == 0xA1 && !h.wantAck());
    // The text is not visible in the air frame.
    CHECK(std::string((const char*)frame, n).find("hola") == std::string::npos);

    Capture cap;
    CHECK(b.handleFrame(frame, n, -20, cap, relay, &relay_len) == RX_HANDLED);
    CHECK(relay_len == 0);  // relay is off by default
    CHECK(cap.texts.size() == 1 && cap.texts[0].text == "hola mundo" && cap.texts[0].from == 0xA1A1A1A1u &&
          cap.texts[0].ch == 3 && cap.texts[0].hops == 0);
    CHECK(b.handleFrame(frame, n, -20, cap, relay, &relay_len) == RX_DUPLICATE);
    CHECK(cap.texts.size() == 1);
    // Our own frame heard back is not delivered.
    CHECK(a.handleFrame(frame, n, -20, cap, relay, &relay_len) == RX_DUPLICATE);
    CHECK(cap.texts.size() == 1);
    // Packet ids are unique per frame.
    uint8_t f2[kMaxFrame];
    size_t n2 = a.buildText(3, kBroadcast, "dos", 3, f2, sizeof(f2));
    Header h2;
    CHECK(parseHeader(f2, n2, h2) && h2.id != h.id);
    CHECK(a.buildText(9, kBroadcast, "x", 1, f2, sizeof(f2)) == 0);  // unknown channel
    a.setHopLimit(7);
    size_t n3 = a.buildText(3, kBroadcast, "siete", 5, f2, sizeof(f2));
    Header h3;
    CHECK(parseHeader(f2, n3, h3) && h3.hopLimit() == 7 && h3.hopStart() == 7);
    a.setHopLimit(200);
    n3 = a.buildText(3, kBroadcast, "tope", 4, f2, sizeof(f2));
    CHECK(parseHeader(f2, n3, h3) && h3.hopLimit() == 7);
    CHECK(a.buildText(3, kBroadcast, "", 0, f2, sizeof(f2)) == 0);
    std::string big(200, 'z');
    CHECK(a.buildText(3, kBroadcast, big.data(), big.size(), f2, sizeof(f2)) == 0);

    // A reply carries the id of the text it answers, and the receiver sees both ids.
    Bridge r = makeNode(0xD4D4D4D4u, "delta", 11);
    size_t n4 = a.buildText(3, kBroadcast, "re", 2, f2, sizeof(f2), 0x12345678u);
    Capture cr;
    Header h4;
    CHECK(parseHeader(f2, n4, h4) && r.handleFrame(f2, n4, 0, cr, relay, &relay_len) == RX_HANDLED);
    CHECK(cr.ids.size() == 1 && cr.ids[0].first == h4.id && cr.ids[0].second == 0x12345678u);
    // A plain text has no reply id.
    Bridge r2 = makeNode(0xE5E5E5E5u, "eps", 12);
    Capture cp;
    CHECK(r2.handleFrame(frame, n, 0, cp, relay, &relay_len) == RX_HANDLED);
    CHECK(cp.ids.size() == 1 && cp.ids[0].second == 0);
}

TEST(mt_bridge_direct, "UNIT-MT-008", "Direct text reaches only its addressee") {
    Bridge a = makeNode(0xA1A1A1A1u, "alpha", 1), b = makeNode(0xB2B2B2B2u, "beta", 2), c = makeNode(0xC3C3C3C3u, "gamma", 3);
    uint8_t frame[kMaxFrame], relay[kMaxFrame];
    size_t relay_len;
    size_t n = a.buildText(3, 0xB2B2B2B2u, "para beta", 9, frame, sizeof(frame));
    Capture cb, cc;
    CHECK(b.handleFrame(frame, n, 0, cb, relay, &relay_len) == RX_HANDLED && cb.texts.size() == 1 && cb.texts[0].to == 0xB2B2B2B2u);
    CHECK(c.handleFrame(frame, n, 0, cc, relay, &relay_len) == RX_HANDLED && cc.texts.empty());
}

TEST(mt_bridge_nodeinfo_position, "UNIT-MT-009", "NodeInfo and Position reach the sink") {
    Bridge a = makeNode(0xA1A1A1A1u, "Alpha Node", 11), b = makeNode(0xB2B2B2B2u, "beta", 12);
    uint8_t frame[kMaxFrame], relay[kMaxFrame];
    size_t relay_len;
    Capture cap;
    size_t n = a.buildNodeInfo(3, frame, sizeof(frame));
    CHECK(n > 0 && b.handleFrame(frame, n, 0, cap, relay, &relay_len) == RX_HANDLED);
    CHECK(cap.users.size() == 1 && cap.users[0].first == 0xA1A1A1A1u &&
          std::strcmp(cap.users[0].second.long_name, "Alpha Node") == 0 &&
          std::strcmp(cap.users[0].second.id, "!a1a1a1a1") == 0 && std::strcmp(cap.users[0].second.short_name, "ZN") == 0);
    Position p = {};
    p.has_lat = p.has_lon = true; p.lat_i = 401234567; p.lon_i = -34567890; p.time = 1700000123u;
    n = a.buildPosition(3, p, frame, sizeof(frame));
    CHECK(n > 0 && b.handleFrame(frame, n, 0, cap, relay, &relay_len) == RX_HANDLED);
    CHECK(cap.positions.size() == 1 && cap.positions[0].second.lat_i == 401234567 && cap.positions[0].second.lon_i == -34567890);
    // A position without coordinates is dropped.
    p.has_lat = false;
    n = a.buildPosition(3, p, frame, sizeof(frame));
    CHECK(b.handleFrame(frame, n, 0, cap, relay, &relay_len) == RX_HANDLED && cap.positions.size() == 1);
}

TEST(mt_bridge_relay, "UNIT-MT-010", "Relay lowers hop_limit, sets the relay byte and keeps the payload") {
    Bridge a = makeNode(0xA1A1A1A1u, "alpha", 21), r = makeNode(0xD4D4D4D4u, "relay", 22);
    r.setRelay(true);
    uint8_t frame[kMaxFrame], out[kMaxFrame];
    size_t out_len = 0;
    size_t n = a.buildText(3, kBroadcast, "salto", 5, frame, sizeof(frame));
    Capture cap;
    CHECK(r.handleFrame(frame, n, 0, cap, out, &out_len) == RX_HANDLED && out_len == n);
    Header h;
    CHECK(parseHeader(out, out_len, h) && h.hopLimit() == 2 && h.hopStart() == 3 && h.relay == 0xD4 &&
          h.from == 0xA1A1A1A1u && !h.wantAck());
    CHECK(std::memcmp(out + kHeaderLen, frame + kHeaderLen, n - kHeaderLen) == 0);
    // The repeated copy is a duplicate for the relay itself and decodes elsewhere with hops = 1.
    CHECK(r.handleFrame(out, out_len, 0, cap, out, &out_len) == RX_DUPLICATE && out_len == 0);
    Bridge b = makeNode(0xB2B2B2B2u, "beta", 23);
    Capture cb;
    uint8_t scratch[kMaxFrame];
    size_t sl;
    uint8_t copy[kMaxFrame];
    std::memcpy(copy, frame, n);
    copy[12] = (uint8_t)((copy[12] & ~7) | 2);
    CHECK(b.handleFrame(copy, n, 0, cb, scratch, &sl) == RX_HANDLED && cb.texts[0].hops == 1);
    // No hops left: delivered, not repeated.
    Bridge r2 = makeNode(0xE5E5E5E5u, "relay2", 24);
    r2.setRelay(true);
    n = a.buildText(3, kBroadcast, "ultimo", 6, frame, sizeof(frame));
    frame[12] = (uint8_t)(frame[12] & ~7);
    CHECK(r2.handleFrame(frame, n, 0, cap, out, &out_len) == RX_HANDLED && out_len == 0);
    // A frame for the relay alone, or with a next hop set, is not repeated.
    n = a.buildText(3, 0xE5E5E5E5u, "tuyo", 4, frame, sizeof(frame));
    CHECK(r2.handleFrame(frame, n, 0, cap, out, &out_len) == RX_HANDLED && out_len == 0);
    n = a.buildText(3, kBroadcast, "directo", 7, frame, sizeof(frame));
    frame[14] = 0x42;
    CHECK(r2.handleFrame(frame, n, 0, cap, out, &out_len) == RX_HANDLED && out_len == 0);
}

TEST(mt_bridge_foreign_channel, "UNIT-MT-011", "Frames of another channel or key are left to MeshCore") {
    Bridge a(refAes), b(refAes);
    a.setSelf(0xA1A1A1A1u, "a", "A", 1);
    b.setSelf(0xB2B2B2B2u, "b", "B", 2);
    uint8_t ka[16], kb[16];
    for (int i = 0; i < 16; i++) { ka[i] = (uint8_t)i; kb[i] = (uint8_t)(i + 1); }
    CHECK(a.setChannel(0, "secret", ka, 16));
    uint8_t frame[kMaxFrame], out[kMaxFrame];
    size_t out_len, n = a.buildText(0, kBroadcast, "privado", 7, frame, sizeof(frame));
    Capture cap;
    CHECK(b.handleFrame(frame, n, 0, cap, out, &out_len) == RX_NOT_MESHTASTIC);  // no such channel
    CHECK(b.setChannel(1, "secret", kb, 16));  // same name, different key: same hash only by luck
    RxResult r = b.handleFrame(frame, n, 0, cap, out, &out_len);
    CHECK(r == RX_NOT_MESHTASTIC && cap.texts.empty());
    // Table limits and handle reuse.
    Bridge t(refAes);
    t.setSelf(0xA1A1A1A1u, "t", "T", 1);
    const uint8_t psk = 1;
    for (int i = 0; i < kMaxChannels; i++) CHECK(t.setChannel((uint8_t)i, "c", &psk, 1));
    CHECK(!t.setChannel(200, "c", &psk, 1));
    CHECK(t.setChannel(2, "c2", &psk, 1) && t.channelCount() == kMaxChannels);
    t.clearChannels();
    CHECK(t.channelCount() == 0 && !t.hasChannel(2));
}

TEST(mt_bridge_not_meshtastic, "UNIT-MT-012", "MeshCore-like and random frames are not claimed") {
    Bridge b = makeNode(0xB2B2B2B2u, "beta", 5);
    Capture cap;
    uint8_t out[kMaxFrame];
    size_t out_len;
    uint32_t seed = 12345;
    int claimed = 0;
    for (int i = 0; i < 20000; i++) {
        uint8_t raw[kMaxFrame];
        size_t len = 18 + nextRandom(seed) % 100;
        for (size_t j = 0; j < len; j++) raw[j] = (uint8_t)(nextRandom(seed) >> 16);
        raw[13] = 0x08;  // force the hash match so the cipher and decoder are exercised
        if (b.handleFrame(raw, len, 0, cap, out, &out_len) != RX_NOT_MESHTASTIC) claimed++;
    }
    CHECK(claimed <= 20);  // measured and bounded: a random frame passes the whole decoder ~1 in 10^4
    uint8_t tiny[17] = {0};
    CHECK(b.handleFrame(tiny, 17, 0, cap, out, &out_len) == RX_NOT_MESHTASTIC);
    uint8_t oversized[300] = {0};
    CHECK(b.handleFrame(oversized, 300, 0, cap, out, &out_len) == RX_NOT_MESHTASTIC);
    Bridge unset(refAes);
    CHECK(unset.handleFrame(oversized, 40, 0, cap, out, &out_len) == RX_NOT_MESHTASTIC);
}

TEST(mt_rebroadcast_timing, "UNIT-MT-013", "Meshtastic slot, SNR-weighted window and client rebroadcast delay") {
    CHECK(slotTimeMs(11, 250.0f) == 28);  // 2.5 x 8.192 ms + 7.6 ms
    CHECK(slotTimeMs(7, 125.0f) == 10);   // 2.5 x 1.024 ms + 7.6 ms
    CHECK(slotTimeMs(9, 0.0f) == 0);
    CHECK(rebroadcastCw(-20 * 4) == 3);
    CHECK(rebroadcastCw(10 * 4) == 8);
    CHECK(rebroadcastCw(-5 * 4) == 5);
    CHECK(rebroadcastCw(-30 * 4) == 3);  // clamped
    CHECK(rebroadcastCw(20 * 4) == 8);   // clamped
    CHECK(rebroadcastDelayMs(28, 0) == 16 * 28);
    CHECK(rebroadcastDelayMs(28, 255) == 271 * 28);
}

TEST(mt_foreign_hash, "UNIT-MT-014", "Queued Meshtastic relay hashes by sender and id, as the frame heard") {
    uint8_t frame[24];
    for (int i = 0; i < 24; i++) frame[i] = (uint8_t)(i * 7 + 1);
    mesh::Packet pkt;
    pkt.header = mesh::Packet::FOREIGN_HEADER;
    pkt.path_len = 0;
    pkt.payload_len = sizeof(frame);
    std::memcpy(pkt.payload, frame, sizeof(frame));
    const uint32_t h = mesh::ContentionTracker::computeForeignHash32(frame, sizeof(frame));
    CHECK(mesh::ContentionTracker::computePacketHash32(&pkt) == h);
    pkt.payload[12] ^= 0x07;  // hop_limit
    pkt.payload[15] ^= 0xFF;  // relay byte
    pkt.payload[0] ^= 0xFF;   // destination
    CHECK(mesh::ContentionTracker::computePacketHash32(&pkt) == h);
    pkt.payload[8] ^= 0x01;   // packet id
    CHECK(mesh::ContentionTracker::computePacketHash32(&pkt) != h);
}

namespace {

std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; s[i] && s[i + 1]; i += 2) v.push_back((uint8_t)std::stoi(std::string(s + i, 2), nullptr, 16));
    return v;
}

uint32_t g_random = 0x2b796a03u;
uint32_t fixedRandom() { return g_random; }

const PkiCrypto kPki = {refAes256, refSha256, fixedRandom};

Bridge makePkiNode(uint32_t node, const char* name, uint32_t seed, uint8_t seed_byte) {
    Bridge b = makeNode(node, name, seed);
    uint8_t identity[32];
    std::memset(identity, seed_byte, sizeof(identity));
    b.setPki(&kPki, identity);
    return b;
}

void ownKey(uint8_t seed_byte, uint8_t priv[32], uint8_t pub[32]) {
    uint8_t identity[32];
    std::memset(identity, seed_byte, sizeof(identity));
    pkiPrivateKey(kPki, identity, priv);
    pkiPublicKey(priv, pub);
}

}  // namespace

TEST(mt_pki_references, "UNIT-MT-015", "Reference AES-256 and SHA-256 match their standard vectors") {
    uint8_t key[32], in[16], out[16], hash[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
    for (int i = 0; i < 16; i++) in[i] = (uint8_t)(i * 0x11);
    refAes256(key, in, out);
    CHECK(std::memcmp(out, hex("8ea2b7ca516745bfeafc49904b496089").data(), 16) == 0);
    refSha256((const uint8_t*)"abc", 3, hash);
    CHECK(std::memcmp(hash, hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad").data(), 32) == 0);
}

TEST(mt_pki_key_agreement, "UNIT-MT-016", "X25519 shared key hashes as Meshtastic does; a weak peer key is refused") {
    uint8_t key[32];
    // Wycheproof vector used by Meshtastic's test_crypto: the key is SHA256 of the shared secret.
    CHECK(pkiSharedKey(kPki, hex("10300724f3bea134eb1575245ef26ff9b8ccd59849cd98ce1a59002fe1d5986c").data(),
                       hex("f7e13a1a067d2f4e1061bf9936fde5be6b0c2494a8f809cbac7f290ef719e91c").data(), key));
    CHECK(std::memcmp(key, hex("24becd5dfed9e9289ba2e15b82b0d54f8e9aacb72f5e4248c58d8d74b451ce76").data(), 32) == 0);
    // A low-order point gives an all-zero secret.
    CHECK(!pkiSharedKey(kPki, hex("18630f93598637c35da623a74559cf944374a559114c7937811041fc8605564a").data(),
                        hex("ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f").data(), key));
    // Both sides derive the same key.
    uint8_t pa[32], qa[32], pb[32], qb[32], ka[32], kb[32];
    ownKey(1, pa, qa);
    ownKey(2, pb, qb);
    CHECK(pkiSharedKey(kPki, pa, qb, ka) && pkiSharedKey(kPki, pb, qa, kb) && std::memcmp(ka, kb, 32) == 0);
}

TEST(mt_pki_vector, "UNIT-MT-017", "A real Meshtastic PKI packet opens, and sealing it again reproduces it") {
    // From meshtastic-firmware test/test_crypto: our private key, the sender's public key, packet 0x13b2d662 from 0x0929.
    uint8_t key[32];
    CHECK(pkiSharedKey(kPki, hex("a00330633e63522f8a4d81ec6d9d1e6617f6c8ffd3a4c698229537d44e522277").data(),
                       hex("db18fc50eea47f00251cb784819a3cf5fc361882597f589f0d7ff820e8064457").data(), key));
    CHECK(std::memcmp(key, hex("777b1545c9d6f9a2").data(), 8) == 0);
    std::vector<uint8_t> payload = hex("40df24abfcc30a17a3d9046726099e796a1c036a792b");
    uint8_t plain[16];
    int n = pkiOpen(kPki, key, 0x0929, 0x13b2d662u, payload.data(), payload.size(), plain, sizeof(plain));
    CHECK(n == 10 && std::memcmp(plain, hex("08011204746573744800").data(), 10) == 0);
    Data d;
    CHECK(decodeData(plain, (size_t)n, d) && d.portnum == PORT_TEXT && d.payload_len == 4 && std::memcmp(d.payload, "test", 4) == 0);
    // The extra nonce is the last four bytes, little endian.
    g_random = 0x2b796a03u;
    uint8_t sealed[32];
    CHECK(pkiSeal(kPki, key, 0x0929, 0x13b2d662u, plain, 10, sealed, sizeof(sealed)) == payload.size());
    CHECK(std::memcmp(sealed, payload.data(), payload.size()) == 0);
}

TEST(mt_pki_seal_open, "UNIT-MT-018", "Seal and open round trip; any altered input is refused") {
    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(0x40 + i);
    for (size_t len : {1u, 15u, 16u, 17u, 100u}) {
        std::vector<uint8_t> plain(len), sealed(len + kPkiOverhead), back(len);
        for (size_t i = 0; i < len; i++) plain[i] = (uint8_t)(i * 3 + 1);
        CHECK(pkiSeal(kPki, key, 0xAABBCCDDu, 77, plain.data(), len, sealed.data(), sealed.size()) == sealed.size());
        CHECK(std::memcmp(sealed.data(), plain.data(), len) != 0);
        CHECK(pkiOpen(kPki, key, 0xAABBCCDDu, 77, sealed.data(), sealed.size(), back.data(), back.size()) == (int)len);
        CHECK(back == plain);
        CHECK(pkiOpen(kPki, key, 0xAABBCCDEu, 77, sealed.data(), sealed.size(), back.data(), back.size()) == -1);  // from
        CHECK(pkiOpen(kPki, key, 0xAABBCCDDu, 78, sealed.data(), sealed.size(), back.data(), back.size()) == -1);  // id
        for (size_t i = 0; i < sealed.size(); i++) {  // cipher, tag and extra nonce
            std::vector<uint8_t> bad = sealed;
            bad[i] ^= 0x01;
            CHECK(pkiOpen(kPki, key, 0xAABBCCDDu, 77, bad.data(), bad.size(), back.data(), back.size()) == -1);
        }
        uint8_t other[32];
        std::memcpy(other, key, 32);
        other[5] ^= 0x80;
        CHECK(pkiOpen(kPki, other, 0xAABBCCDDu, 77, sealed.data(), sealed.size(), back.data(), back.size()) == -1);
    }
    uint8_t out[8];
    CHECK(pkiOpen(kPki, key, 1, 1, out, kPkiOverhead, out, sizeof(out)) == -1);  // nothing to open
    CHECK(pkiSeal(kPki, key, 1, 1, out, 4, out, 10) == 0);                         // cap too small
}

TEST(mt_pki_nodeinfo_key, "UNIT-MT-019", "User carries the public key; our NodeInfo publishes it") {
    User u = {};
    std::strcpy(u.id, "!00000001");
    u.has_public_key = true;
    for (int i = 0; i < 32; i++) u.public_key[i] = (uint8_t)(0xC0 + i);
    uint8_t buf[128];
    size_t n = encodeUser(u, buf, sizeof(buf));
    User v;
    CHECK(n > 0 && decodeUser(buf, n, v) && v.has_public_key && std::memcmp(v.public_key, u.public_key, 32) == 0);
    u.has_public_key = false;
    n = encodeUser(u, buf, sizeof(buf));
    CHECK(decodeUser(buf, n, v) && !v.has_public_key);
    // A key of the wrong length is ignored.
    uint8_t odd[] = {0x42, 0x03, 1, 2, 3};
    CHECK(decodeUser(odd, sizeof(odd), v) && !v.has_public_key);

    uint8_t priv[32], pub[32], frame[kMaxFrame], relay[kMaxFrame];
    size_t relay_len;
    ownKey(7, priv, pub);
    Bridge a = makePkiNode(0xA1A1A1A1u, "alpha", 1, 7), plain = makeNode(0xB2B2B2B2u, "beta", 2);
    Capture cap;
    CHECK(plain.handleFrame(frame, a.buildNodeInfo(3, frame, sizeof(frame)), 0, cap, relay, &relay_len) == RX_HANDLED);
    CHECK(cap.users.size() == 1 && cap.users[0].second.has_public_key && std::memcmp(cap.users[0].second.public_key, pub, 32) == 0);
    Capture none;
    Bridge b = makeNode(0xC3C3C3C3u, "gamma", 3);
    CHECK(plain.handleFrame(frame, b.buildNodeInfo(3, frame, sizeof(frame)), 0, none, relay, &relay_len) == RX_HANDLED);
    CHECK(none.users.size() == 1 && !none.users[0].second.has_public_key);
    // setSelf after setPki keeps the key.
    a.setSelf(0xA1A1A1A2u, "alpha2", "AL", 5);
    Capture again;
    CHECK(plain.handleFrame(frame, a.buildNodeInfo(3, frame, sizeof(frame)), 0, again, relay, &relay_len) == RX_HANDLED);
    CHECK(again.users.size() == 1 && again.users[0].second.has_public_key && std::memcmp(again.users[0].second.public_key, pub, 32) == 0);
}

TEST(mt_pki_direct, "UNIT-MT-020", "A PKI direct text reaches only its addressee, as channel 0, and is acked") {
    uint8_t pa[32], qa[32], pb[32], qb[32], frame[kMaxFrame], relay[kMaxFrame];
    size_t relay_len;
    ownKey(7, pa, qa);
    ownKey(9, pb, qb);
    Bridge a = makePkiNode(0xA1A1A1A1u, "alpha", 1, 7), b = makePkiNode(0xB2B2B2B2u, "beta", 2, 9);
    Bridge c = makePkiNode(0xC3C3C3C3u, "gamma", 3, 11);
    Capture ca, cb, cc;
    ca.keys.push_back({0xB2B2B2B2u, std::vector<uint8_t>(qb, qb + 32)});
    cb.keys.push_back({0xA1A1A1A1u, std::vector<uint8_t>(qa, qa + 32)});
    cc.keys.push_back({0xA1A1A1A1u, std::vector<uint8_t>(qa, qa + 32)});

    size_t n = a.buildDirectText(0xB2B2B2B2u, qb, "hola beta", 9, frame, sizeof(frame), 0x0BADF00Du);
    Header h;
    CHECK(n > kHeaderLen + 9 + kPkiOverhead && n <= kMaxTxFrame && parseHeader(frame, n, h));
    CHECK(h.to == 0xB2B2B2B2u && h.from == 0xA1A1A1A1u && h.channel == 0 && !h.wantAck());
    CHECK(std::string((const char*)frame, n).find("hola") == std::string::npos);

    CHECK(c.handleFrame(frame, n, 0, cc, relay, &relay_len) == RX_NOT_MESHTASTIC);  // not for gamma
    CHECK(cc.directs.empty() && cc.texts.empty());
    CHECK(b.handleFrame(frame, n, 8, cb, relay, &relay_len) == RX_HANDLED && relay_len == 0);
    CHECK(cb.directs.size() == 1 && cb.directs[0].from == 0xA1A1A1A1u && cb.directs[0].id == h.id &&
          cb.directs[0].reply == 0x0BADF00Du && cb.directs[0].text == "hola beta" && !cb.directs[0].want_ack);
    CHECK(cb.texts.empty());
    CHECK(b.handleFrame(frame, n, 8, cb, relay, &relay_len) == RX_DUPLICATE && cb.directs.size() == 1);

    // Without the sender's key the frame is not opened.
    Capture nokey;
    Bridge d = makePkiNode(0xB2B2B2B2u, "beta", 2, 9);
    CHECK(d.handleFrame(frame, n, 0, nokey, relay, &relay_len) == RX_NOT_MESHTASTIC && nokey.directs.empty());

    // The ACK is a PKI frame whose Data answers the packet id.
    size_t k = b.buildAck(0xA1A1A1A1u, h.id, qa, frame, sizeof(frame));
    Header ha;
    CHECK(k > kHeaderLen && parseHeader(frame, k, ha) && ha.to == 0xA1A1A1A1u && ha.channel == 0);
    uint8_t key[32], plain[64];
    CHECK(pkiSharedKey(kPki, pa, qb, key));
    int pn = pkiOpen(kPki, key, 0xB2B2B2B2u, ha.id, frame + kHeaderLen, k - kHeaderLen, plain, sizeof(plain));
    Data d2;
    CHECK(pn > 0 && decodeData(plain, (size_t)pn, d2) && d2.portnum == PORT_ROUTING && d2.request_id == h.id &&
          d2.payload_len == 2 && d2.payload[0] == 0x18 && d2.payload[1] == 0x00);
    CHECK(a.handleFrame(frame, k, 0, ca, relay, &relay_len) == RX_HANDLED && ca.directs.empty() && ca.texts.empty());

    // Limits: weak key, empty or too long text, no PKI.
    uint8_t weak[32];
    std::memcpy(weak, hex("ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f").data(), 32);
    CHECK(a.buildDirectText(0xB2B2B2B2u, weak, "x", 1, frame, sizeof(frame)) == 0);
    CHECK(a.buildDirectText(0xB2B2B2B2u, qb, "", 0, frame, sizeof(frame)) == 0);
    std::string big(200, 'z');
    CHECK(a.buildDirectText(0xB2B2B2B2u, qb, big.data(), big.size(), frame, sizeof(frame)) == 0);
    Bridge nopki = makeNode(0xE5E5E5E5u, "eps", 5);
    CHECK(nopki.buildDirectText(0xB2B2B2B2u, qb, "x", 1, frame, sizeof(frame)) == 0);
    CHECK(!nopki.hasPki() && a.hasPki());
}

TEST(mt_pki_not_for_broadcast, "UNIT-MT-021", "Only unicast text on channel 0 is taken for PKI; channel traffic is untouched") {
    uint8_t pa[32], qa[32], frame[kMaxFrame], relay[kMaxFrame];
    size_t relay_len;
    ownKey(9, pa, qa);
    Bridge a = makePkiNode(0xA1A1A1A1u, "alpha", 1, 7), b = makePkiNode(0xB2B2B2B2u, "beta", 2, 9);
    Capture cb;
    cb.keys.push_back({0xA1A1A1A1u, std::vector<uint8_t>(qa, qa + 32)});
    size_t n = a.buildText(3, kBroadcast, "para todos", 10, frame, sizeof(frame));
    CHECK(b.handleFrame(frame, n, 0, cb, relay, &relay_len) == RX_HANDLED && cb.texts.size() == 1 && cb.directs.empty());
    n = a.buildText(3, 0xB2B2B2B2u, "unicast por canal", 17, frame, sizeof(frame));
    CHECK(b.handleFrame(frame, n, 0, cb, relay, &relay_len) == RX_HANDLED && cb.texts.size() == 2 && cb.directs.empty());
    // A NodeInfo request is unicast on the channel and asks for an answer.
    n = a.buildNodeInfoTo(3, 0xB2B2B2B2u, true, frame, sizeof(frame));
    Header h;
    CHECK(n > kHeaderLen && parseHeader(frame, n, h) && h.to == 0xB2B2B2B2u && h.channel == 0x08);
    ctrCrypt(refAes, kDefaultKey, h.from, h.id, frame + kHeaderLen, n - kHeaderLen);
    Data d;
    CHECK(decodeData(frame + kHeaderLen, n - kHeaderLen, d) && d.portnum == PORT_NODEINFO && d.want_response);
    CHECK(a.buildNodeInfoTo(9, 0xB2B2B2B2u, true, frame, sizeof(frame)) == 0);  // unknown channel
}

namespace {

// Two PKI nodes that know each other's key, and a Capture per side.
struct PkiPair {
    uint8_t pa[32], qa[32], pb[32], qb[32];
    Bridge a, b;
    Capture ca, cb;
    PkiPair() : a(makePkiNode(0xA1A1A1A1u, "alpha", 1, 7)), b(makePkiNode(0xB2B2B2B2u, "beta", 2, 9)) {
        ownKey(7, pa, qa);
        ownKey(9, pb, qb);
        ca.keys.push_back({0xB2B2B2B2u, std::vector<uint8_t>(qb, qb + 32)});
        cb.keys.push_back({0xA1A1A1A1u, std::vector<uint8_t>(qa, qa + 32)});
    }
};

}  // namespace

TEST(mt_routing_decode, "UNIT-MT-022", "Routing error reason: 0 is an ACK, others a NAK, none is not routing") {
    uint32_t reason = 99;
    const uint8_t ack[] = {0x18, 0x00};
    CHECK(decodeRouting(ack, sizeof(ack), reason) && reason == 0);
    const uint8_t nak[] = {0x18, 35};
    CHECK(decodeRouting(nak, sizeof(nak), reason) && reason == ROUTING_PKI_UNKNOWN_PUBKEY);
    const uint8_t other[] = {0x08, 0x01};  // another field, no error_reason
    CHECK(!decodeRouting(other, sizeof(other), reason));
    CHECK(!decodeRouting(ack, 1, reason));
}

TEST(mt_acks, "UNIT-MT-023", "An ACK or NAK for us reaches the sink, by PKI or on a channel; not one for others") {
    PkiPair t;
    uint8_t frame[kMaxFrame], relay[kMaxFrame];
    size_t relay_len;
    // PKI ACK.
    size_t n = t.b.buildAck(0xA1A1A1A1u, 0x1234u, t.qa, frame, sizeof(frame));
    CHECK(t.a.handleFrame(frame, n, 0, t.ca, relay, &relay_len) == RX_HANDLED);
    CHECK(t.ca.acks.size() == 1 && t.ca.acks[0].from == 0xB2B2B2B2u && t.ca.acks[0].id == 0x1234u && t.ca.acks[0].reason == 0);
    // The same packet on a channel (Meshtastic acks on the channel when one is shared):
    // hand-built Routing Data from b to a on the default channel.
    uint8_t routing[] = {0x18, 35};  // PKI_UNKNOWN_PUBKEY
    Data d = {};
    d.portnum = PORT_ROUTING; d.payload = routing; d.payload_len = sizeof(routing); d.request_id = 0x5678u;
    uint8_t pb[32];
    size_t dn = encodeData(d, pb, sizeof(pb));
    Header h = {0xA1A1A1A1u, 0xB2B2B2B2u, 0x77u, 0x63, 0x08, 0, 0};
    writeHeader(h, frame);
    std::memcpy(frame + kHeaderLen, pb, dn);
    ctrCrypt(refAes, kDefaultKey, h.from, h.id, frame + kHeaderLen, dn);
    CHECK(t.a.handleFrame(frame, kHeaderLen + dn, 0, t.ca, relay, &relay_len) == RX_HANDLED);
    CHECK(t.ca.acks.size() == 2 && t.ca.acks[1].id == 0x5678u && t.ca.acks[1].reason == ROUTING_PKI_UNKNOWN_PUBKEY);
    // An ACK addressed to another node is not ours.
    h.to = 0xC3C3C3C3u; h.id = 0x78u;
    writeHeader(h, frame);
    std::memcpy(frame + kHeaderLen, pb, dn);
    ctrCrypt(refAes, kDefaultKey, h.from, h.id, frame + kHeaderLen, dn);
    CHECK(t.a.handleFrame(frame, kHeaderLen + dn, 0, t.ca, relay, &relay_len) == RX_HANDLED && t.ca.acks.size() == 2);
}

TEST(mt_want_ack, "UNIT-MT-024", "A PKI direct text can ask for an ACK, and the receiver sees it") {
    PkiPair t;
    uint8_t frame[kMaxFrame], relay[kMaxFrame];
    size_t relay_len;
    size_t n = t.a.buildDirectText(0xB2B2B2B2u, t.qb, "con ack", 7, frame, sizeof(frame), 0, true);
    Header h;
    CHECK(n > 0 && parseHeader(frame, n, h) && h.wantAck());
    CHECK(t.b.handleFrame(frame, n, 0, t.cb, relay, &relay_len) == RX_HANDLED);
    CHECK(t.cb.directs.size() == 1 && t.cb.directs[0].want_ack && t.cb.directs[0].id == h.id);
    n = t.a.buildDirectText(0xB2B2B2B2u, t.qb, "sin ack", 7, frame, sizeof(frame));
    CHECK(parseHeader(frame, n, h) && !h.wantAck());
}

TEST(mt_nodeinfo_request, "UNIT-MT-025", "A unicast NodeInfo asking for ours reaches the sink; a broadcast one does not") {
    Bridge a = makePkiNode(0xA1A1A1A1u, "alpha", 1, 7), b = makeNode(0xB2B2B2B2u, "beta", 2);
    uint8_t frame[kMaxFrame], relay[kMaxFrame];
    size_t relay_len;
    Capture cap;
    size_t n = a.buildNodeInfoTo(3, 0xB2B2B2B2u, true, frame, sizeof(frame));
    CHECK(b.handleFrame(frame, n, 0, cap, relay, &relay_len) == RX_HANDLED);
    CHECK(cap.users.size() == 1 && cap.info_requests.size() == 1 && cap.info_requests[0].first == 3 &&
          cap.info_requests[0].second == 0xA1A1A1A1u);
    n = a.buildNodeInfoTo(3, 0xB2B2B2B2u, false, frame, sizeof(frame));
    CHECK(b.handleFrame(frame, n, 0, cap, relay, &relay_len) == RX_HANDLED && cap.info_requests.size() == 1);
    n = a.buildNodeInfoTo(3, kBroadcast, true, frame, sizeof(frame));
    CHECK(b.handleFrame(frame, n, 0, cap, relay, &relay_len) == RX_HANDLED && cap.info_requests.size() == 1);
    n = a.buildNodeInfoTo(3, 0xC3C3C3C3u, true, frame, sizeof(frame));  // for another node
    CHECK(b.handleFrame(frame, n, 0, cap, relay, &relay_len) == RX_HANDLED && cap.info_requests.size() == 1);
}

TEST(mt_unknown_key, "UNIT-MT-026", "A PKI frame for us from a node with no key calls the sink once; known keys do not") {
    PkiPair t;
    uint8_t frame[kMaxFrame], relay[kMaxFrame];
    size_t relay_len;
    size_t n = t.a.buildDirectText(0xB2B2B2B2u, t.qb, "hola", 4, frame, sizeof(frame));
    Capture strangers;  // knows nobody
    CHECK(t.b.handleFrame(frame, n, 0, strangers, relay, &relay_len) == RX_NOT_MESHTASTIC);
    CHECK(strangers.unknown_keys.size() == 1 && strangers.unknown_keys[0] == 0xA1A1A1A1u && strangers.directs.empty());
    CHECK(t.b.handleFrame(frame, n, 0, t.cb, relay, &relay_len) == RX_HANDLED && t.cb.unknown_keys.empty());
    // Not for us: no call.
    Bridge c = makePkiNode(0xC3C3C3C3u, "gamma", 3, 11);
    Capture cc;
    CHECK(c.handleFrame(frame, n, 0, cc, relay, &relay_len) == RX_NOT_MESHTASTIC && cc.unknown_keys.empty());
}

TEST(mt_node_rate_limit, "UNIT-MT-027", "A node is let through once per interval, wrap-safe, with a bounded table") {
    NodeRateLimit l;
    CHECK(l.allow(1, 1000, 500));
    CHECK(!l.allow(1, 1400, 500));
    CHECK(l.allow(2, 1400, 500));        // another node is independent
    CHECK(l.allow(1, 1500, 500));        // interval elapsed
    CHECK(!l.allow(1, 1999, 500));
    // The clock wrapping around 2^32 does not open or close the window by mistake.
    NodeRateLimit w;
    CHECK(w.allow(7, 0xFFFFFF00u, 1000));
    CHECK(!w.allow(7, 0x00000010u, 1000));
    CHECK(w.allow(7, 0x00000500u, 1000));
    // More nodes than slots: the oldest is forgotten, so it is let through again.
    NodeRateLimit f;
    for (uint32_t i = 0; i < (uint32_t)NodeRateLimit::kSlots; i++) CHECK(f.allow(100 + i, 10 + i, 100000));
    CHECK(f.allow(999, 50, 100000));     // evicts node 100, the oldest
    CHECK(f.allow(100, 60, 100000));     // forgotten
    CHECK(!f.allow(999, 70, 100000));    // still held
}

TEST(mt_favourite_asks, "UNIT-MT-028", "Marking a favourite asks only on the change, and only when a name or key is missing") {
    CHECK(favouriteAsksNodeInfo(false, true, false, false));
    CHECK(favouriteAsksNodeInfo(false, true, true, false));
    CHECK(favouriteAsksNodeInfo(false, true, false, true));
    CHECK(!favouriteAsksNodeInfo(false, true, true, true));
    CHECK(!favouriteAsksNodeInfo(true, true, false, false));   // already a favourite
    CHECK(!favouriteAsksNodeInfo(false, false, false, false)); // not marked
    CHECK(!favouriteAsksNodeInfo(true, false, false, false));  // unmarked
}

TEST(contact_change, "UNIT-MT-029", "Only a new record or a changed name, type or position is worth writing") {
    ContactInfo c = {};
    c.type = 1; c.gps_lat = 10; c.gps_lon = 20;
    std::strcpy(c.name, "Alpha (MT)");
    ContactSnapshot before = {true, 1, 10, 20, {}};
    std::strcpy(before.name, "Alpha (MT)");
    CHECK(!contactChangedSubstantively(before, c));                     // a bare re-advert
    ContactSnapshot unknown = before;
    unknown.known = false;
    CHECK(contactChangedSubstantively(unknown, c));                     // not stored before
    ContactSnapshot t = before; t.type = 2;
    CHECK(contactChangedSubstantively(t, c));
    ContactSnapshot la = before; la.gps_lat = 11;
    CHECK(contactChangedSubstantively(la, c));
    ContactSnapshot lo = before; lo.gps_lon = 21;
    CHECK(contactChangedSubstantively(lo, c));
    ContactSnapshot nm = before; std::strcpy(nm.name, "Beta (MT)");
    CHECK(contactChangedSubstantively(nm, c));
}

TEST(mt_relay_class, "UNIT-MT-030", "Text, ACK, NodeInfo and traceroute survive an echo; position is repeated but yields; the rest only in all") {
    CHECK(relayClassOf(PORT_TEXT) == RelayClass::Always);
    CHECK(relayClassOf(PORT_ROUTING) == RelayClass::Always);
    CHECK(relayClassOf(PORT_NODEINFO) == RelayClass::Always);
    CHECK(relayClassOf(PORT_TRACEROUTE) == RelayClass::Always);
    CHECK(relayClassOf(PORT_POSITION) == RelayClass::Essential);
    CHECK(relayClassOf(67) == RelayClass::CancelOnEcho);  // telemetry
    CHECK(relayClassOf(0) == RelayClass::CancelOnEcho);

    Bridge a = makeNode(0xA1A1A1A1u, "alpha", 1), r = makeNode(0xD4D4D4D4u, "delta", 2);
    r.setRelay(true);
    uint8_t frame[kMaxFrame], relay[kMaxFrame];
    size_t relay_len = 0;
    RelayClass cls = RelayClass::CancelOnEcho;
    Capture cap;
    size_t n = a.buildText(3, kBroadcast, "hola", 4, frame, sizeof(frame));
    CHECK(r.handleFrame(frame, n, 0, cap, relay, &relay_len, &cls) == RX_HANDLED);
    CHECK(relay_len == n && cls == RelayClass::Always);
    Position pos = {};
    pos.has_lat = pos.has_lon = true;
    pos.lat_i = 414000000;
    pos.lon_i = 21700000;
    n = a.buildPosition(3, pos, frame, sizeof(frame));
    CHECK(r.handleFrame(frame, n, 0, cap, relay, &relay_len, &cls) == RX_HANDLED);
    CHECK(relay_len == n && cls == RelayClass::Essential);
}

TEST(mt_relay_table, "UNIT-MT-054", "Repeat and echo decision for every port in off, essential and all, held or opaque channel") {
    struct Row { uint32_t port; bool held; bool essential; bool all; bool yields; };
    const Row rows[] = {
        {PORT_TEXT, true, true, true, false},
        {PORT_ROUTING, true, true, true, false},
        {PORT_NODEINFO, true, true, true, false},
        {PORT_TRACEROUTE, true, true, true, false},
        {PORT_POSITION, true, true, true, true},
        {67, true, false, true, true},  // telemetry
        {0, false, true, true, true},   // opaque channel: port unknown
    };
    for (const Row &r : rows) {
        RelayClass cls = r.held ? relayClassOf(r.port) : RelayClass::Essential;
        CHECK(repeatsInMode(false, cls) == r.essential);
        CHECK(repeatsInMode(true, cls) == r.all);
        CHECK(yieldsOnEcho(cls) == r.yields);
    }
    // A direct message on channel 0 we cannot open is the PKI case: repeated in essential, never yields.
    CHECK(repeatsInMode(false, RelayClass::Always) && !yieldsOnEcho(RelayClass::Always));

    // relay off and relay.opaque off decide in the bridge, before the class.
    Bridge a(refAes);
    a.setSelf(0xA1A1A1A1u, "alpha", "ZN", 1);
    uint8_t psk[16];
    for (int i = 0; i < 16; i++) psk[i] = (uint8_t)(0x40 + i);
    CHECK(a.setChannel(1, "private", psk, 16));
    uint8_t frame[kMaxFrame], relay[kMaxFrame];
    size_t n = a.buildText(1, kBroadcast, "secreto", 7, frame, sizeof(frame));
    while (looksLikeMeshCore(frame, n, 0)) { frame[0] ^= 0x40; }
    Bridge r = makeNode(0xD4D4D4D4u, "delta", 2);
    Capture cap;
    size_t relay_len = 0;
    RelayClass cls = RelayClass::Always;
    r.setRelay(false); r.setRelayOpaque(true);
    CHECK(r.handleFrame(frame, n, 0, cap, relay, &relay_len, &cls) == RX_NOT_MESHTASTIC);
    r.setRelay(true); r.setRelayOpaque(false);
    CHECK(r.handleFrame(frame, n, 0, cap, relay, &relay_len, &cls) == RX_NOT_MESHTASTIC);
    r.setRelayOpaque(true);
    CHECK(r.handleFrame(frame, n, 0, cap, relay, &relay_len, &cls) == RX_OPAQUE && relay_len == n);
    CHECK(cls == RelayClass::Essential && repeatsInMode(false, cls) && yieldsOnEcho(cls));
}

TEST(mt_opaque_relay, "UNIT-MT-031", "A frame of a channel we hold no key for is repeated only when asked, and never as MeshCore") {
    Bridge a(refAes);
    a.setSelf(0xA1A1A1A1u, "alpha", "ZN", 1);
    uint8_t psk[16];
    for (int i = 0; i < 16; i++) psk[i] = (uint8_t)(0x40 + i);
    CHECK(a.setChannel(1, "private", psk, 16));
    uint8_t frame[kMaxFrame], relay[kMaxFrame];
    size_t n = a.buildText(1, kBroadcast, "secreto", 7, frame, sizeof(frame));
    // Force a payload that cannot pass for MeshCore: 16-byte blocks never end at 7.
    while (looksLikeMeshCore(frame, n, 0)) { frame[0] ^= 0x40; }

    Bridge r = makeNode(0xD4D4D4D4u, "delta", 2);  // holds LongFast only
    Capture cap;
    size_t relay_len = 0;
    RelayClass cls = RelayClass::Always;
    CHECK(r.handleFrame(frame, n, 0, cap, relay, &relay_len, &cls) == RX_NOT_MESHTASTIC);
    r.setRelay(true);
    CHECK(r.handleFrame(frame, n, 0, cap, relay, &relay_len, &cls) == RX_NOT_MESHTASTIC);
    r.setRelayOpaque(true);
    CHECK(r.handleFrame(frame, n, 0, cap, relay, &relay_len, &cls) == RX_OPAQUE);
    Header h, hr;
    CHECK(parseHeader(frame, n, h) && relay_len == n && parseHeader(relay, relay_len, hr));
    CHECK(hr.hopLimit() == h.hopLimit() - 1 && hr.relay == 0xD4 && hr.id == h.id && hr.from == h.from);
    CHECK(cls == RelayClass::Essential && cap.texts.empty());
    CHECK(r.handleFrame(frame, n, 0, cap, relay, &relay_len, &cls) == RX_DUPLICATE && relay_len == n);  // the originator again
    frame[12] = (uint8_t)((frame[12] & ~0x07) | 1);  // a copy through a repeater
    CHECK(r.handleFrame(frame, n, 0, cap, relay, &relay_len, &cls) == RX_DUPLICATE && relay_len == 0);
}

TEST(mt_discriminate, "UNIT-MT-032", "A well formed MeshCore frame is never taken for Meshtastic") {
    // GRP_TXT flood: header 0x15, no path, channel hash + MAC + one block.
    uint8_t mc[1 + 1 + 3 + 16] = {0x15, 0x00, 0xAA, 0xBB, 0xCC};
    CHECK(looksLikeMeshCore(mc, sizeof(mc), 0xA1A1A1A1u));
    CHECK(!looksLikeMeshCore(mc, sizeof(mc) - 1, 0));       // not whole blocks
    uint8_t v2[sizeof(mc)];
    std::memcpy(v2, mc, sizeof(mc));
    v2[0] |= 0x80;                                         // payload version 2
    CHECK(!looksLikeMeshCore(v2, sizeof(v2), 0));
    std::memcpy(v2, mc, sizeof(mc));
    v2[1] = 0xC0;                                          // reserved path mode
    CHECK(!looksLikeMeshCore(v2, sizeof(v2), 0));
    v2[1] = 63;                                            // path runs past the frame
    CHECK(!looksLikeMeshCore(v2, sizeof(v2), 0));
    CHECK(!looksLikeMeshCore(mc, 1, 0));
    // Our own node number in the Meshtastic to/from fields vetoes it.
    uint8_t own[sizeof(mc)];
    std::memcpy(own, mc, sizeof(mc));
    own[0] = 0x15; own[1] = 0x00; own[2] = 0; own[3] = 0;
    uint32_t self = 0x00000015u;
    CHECK(!looksLikeMeshCore(own, sizeof(own), self) && looksLikeMeshCore(own, sizeof(own), 0));
    // Transport flood carries two 16-bit codes before the path length.
    uint8_t tr[1 + 4 + 1 + 3 + 16] = {0x14, 1, 2, 3, 4, 0, 0xAA, 0xBB, 0xCC};
    tr[0] = (uint8_t)((PAYLOAD_TYPE_GRP_TXT << 2) | ROUTE_TYPE_TRANSPORT_FLOOD);
    CHECK(looksLikeMeshCore(tr, sizeof(tr), 0));
    CHECK(!looksLikeMeshCore(tr, 5, 0));
}

static std::string mtCmd(RepeaterPrefs& p, const char* cmd, bool* changed = nullptr, bool standalone = true) {
    char reply[96] = "";
    CmdResult r = handleCommand(p, cmd, reply, sizeof(reply), standalone);
    if (changed) *changed = r.changed;
    return r.handled ? std::string(reply) : std::string("<not mine>");
}

TEST(mt_config_cli, "UNIT-MT-033", "mt. commands read and change the settings, reject bad values, and leave other commands alone") {
    RepeaterPrefs p;
    setDefaults(p, 3, RELAY_ALL, ROLE_CLIENT);
    bool ch = false;
    CHECK(mtCmd(p, "get z.mt.enable") == "> on" && mtCmd(p, "get z.mt.role") == "> client");
    CHECK(mtCmd(p, "get z.mt.relay") == "> all" && mtCmd(p, "get z.mt.relay.opaque") == "> on");
    CHECK(mtCmd(p, "get z.mt.channel") == "> SFNarrow (default key)");
    CHECK(mtCmd(p, "get dutycycle") == "<not mine>" && mtCmd(p, "set freq 869") == "<not mine>");
    CHECK(mtCmd(p, "get z.mt.nope").find("Error") == 0);

    CHECK(mtCmd(p, "set z.mt.role router_late", &ch) == "OK" && ch && p.role == ROLE_ROUTER_LATE);
    CHECK(mtCmd(p, "set z.mt.role router_late", &ch) == "OK" && !ch);  // same value: nothing to save
    CHECK(mtCmd(p, "set z.mt.role boss", &ch).find("Error") == 0 && !ch && p.role == ROLE_ROUTER_LATE);
    CHECK(mtCmd(p, "set z.mt.relay essential", &ch) == "OK" && p.relay == RELAY_ESSENTIAL);
    CHECK(mtCmd(p, "set z.mt.enable off", &ch) == "OK" && p.enable == 0);
    CHECK(mtCmd(p, "set z.mt.hops 7", &ch) == "OK" && p.hops == 7);
    CHECK(mtCmd(p, "set z.mt.hops 8", &ch).find("Error") == 0 && p.hops == 7);
    CHECK(mtCmd(p, "set z.mt.hops x", &ch).find("Error") == 0);
    CHECK(mtCmd(p, "set z.mt.nodeinfo 100", &ch) == "OK - 600 s" && p.nodeinfo_s == 600);  // raised to the floor
    CHECK(mtCmd(p, "set z.mt.nodeinfo 0", &ch) == "OK - 0 s" && p.nodeinfo_s == 0);
    CHECK(mtCmd(p, "set z.mt.telemetry 30", &ch) == "OK - 60 s");
}

TEST(mt_config_channel, "UNIT-MT-034", "The channel takes a name and an optional 1 or 16 byte base64 key; a bad key changes nothing") {
    RepeaterPrefs p;
    setDefaults(p, 3, RELAY_ALL, ROLE_CLIENT);
    bool ch = false;
    CHECK(mtCmd(p, "set z.mt.channel Mesh AQ==", &ch) == "OK" && ch);
    CHECK(std::string(p.channel) == "Mesh" && p.psk_len == 1 && p.psk[0] == 1);
    CHECK(mtCmd(p, "set z.mt.channel Mesh AAECAwQFBgcICQoLDA0ODw==", &ch) == "OK" && ch);
    CHECK(p.psk_len == 16 && p.psk[0] == 0 && p.psk[15] == 15);
    CHECK(mtCmd(p, "get z.mt.channel") == "> Mesh (custom key)");
    CHECK(mtCmd(p, "set z.mt.channel Other", &ch) == "OK" && ch && p.psk_len == 16);  // key kept
    CHECK(mtCmd(p, "set z.mt.channel Other AAEC", &ch).find("Error") == 0 && !ch && p.psk_len == 16);
    CHECK(mtCmd(p, "set z.mt.channel Other !!!!", &ch).find("Error") == 0);
    CHECK(mtCmd(p, "set z.mt.channel TooLongChannelName", &ch).find("Error") == 0);
    CHECK(mtCmd(p, "set z.mt.channel", &ch).find("Error") == 0);
    CHECK(mtCmd(p, "set z.mt.channel Mesh default", &ch) == "OK" && ch && p.psk_len == 1 && p.psk[0] == 1);
    uint8_t out[4];
    CHECK(base64Decode("AQ==", out, sizeof(out)) == 1 && out[0] == 1);
    CHECK(base64Decode("AAECAwQ", out, 4) == -1);  // would overflow
}

TEST(mt_config_sanitize, "UNIT-MT-035", "Out of range stored settings are put back in range") {
    RepeaterPrefs p;
    setDefaults(p, 3, RELAY_ALL, ROLE_CLIENT);
    CHECK(p.enable == 1 && p.hops == 3 && p.relay == RELAY_ALL && p.nodeinfo_s == 43200);
    p.enable = 9; p.role = 77; p.hops = 99; p.relay = 9; p.relay_opaque = 3;
    p.psk_len = 7; p.nodeinfo_s = 5; p.telemetry_s = 99999999;
    std::memset(p.channel, 0x01, sizeof(p.channel));  // control characters
    sanitize(p);
    CHECK(p.enable == 1 && p.role == ROLE_CLIENT && p.hops == 7 && p.relay == RELAY_ALL);
    CHECK(p.relay_opaque == 1 && p.psk_len == 1 && p.psk[0] == 1);
    CHECK(p.nodeinfo_s == 600 && p.telemetry_s == 86400);
    CHECK(std::string(p.channel) == "SFNarrow");
    CHECK(roleSendsEarly(ROLE_ROUTER) && !roleSendsEarly(ROLE_ROUTER_LATE));
    CHECK(!roleYieldsToEcho(ROLE_ROUTER) && !roleYieldsToEcho(ROLE_ROUTER_LATE));
    CHECK(roleYieldsToEcho(ROLE_CLIENT) && roleYieldsToEcho(ROLE_REPEATER));
}

TEST(mt_router_delay, "UNIT-MT-036", "A router waits at most 2 x cw slots, before the first slot a client may use") {
    const uint32_t slot = slotTimeMs(11, 250.0f);
    CHECK(earlyDelayMs(slot, 0) == 0);
    for (int snr = -80; snr <= 40; snr += 4) {
        uint8_t cw = rebroadcastCw((int8_t)snr);
        CHECK(earlyDelayMs(slot, 2u * cw - 1) < rebroadcastDelayMs(slot, 0));  // 2*cw <= 16 slots
    }
    // One window and one delay for both roles: a router draws from 2 x cw slots, a client from 2^cw.
    for (int snr = -80; snr <= 40; snr += 4) {
        uint8_t cw = rebroadcastCw((int8_t)snr);
        CHECK(relayWindow((int8_t)snr, true) == 2u * cw && relayWindow((int8_t)snr, false) == (1u << cw));
    }
    CHECK(relayDelayMs(slot, true, 3) == earlyDelayMs(slot, 3) && relayDelayMs(slot, false, 3) == rebroadcastDelayMs(slot, 3));
    Bridge b = makeNode(0xA1A1A1A1u, "alpha", 1);
    b.setRole(ROLE_ROUTER);
    uint8_t frame[kMaxFrame];
    size_t n = b.buildNodeInfo(3, frame, sizeof(frame));
    Bridge c = makeNode(0xB2B2B2B2u, "beta", 2);
    Capture cap;
    uint8_t relay[kMaxFrame];
    size_t relay_len = 0;
    CHECK(c.handleFrame(frame, n, 0, cap, relay, &relay_len) == RX_HANDLED && cap.users.size() == 1);
    CHECK(cap.users[0].second.role == ROLE_ROUTER);
}

// Node numbers whose last byte is the relay id Meshtastic puts in next_hop.
TEST(mt_next_hop, "UNIT-MT-037", "A frame is repeated when it names no next hop or names us; a fallback flood and an originator retry are repeated again") {
    Bridge a = makeNode(0xA1A1A1A1u, "alpha", 1);
    Bridge r = makeNode(0xD4D4D4D4u, "delta", 2);
    r.setRelay(true);
    uint8_t frame[kMaxFrame], heard[kMaxFrame], relay[kMaxFrame];
    size_t relay_len = 0;
    Capture cap;
    size_t n = a.buildText(3, kBroadcast, "uno", 3, frame, sizeof(frame));
    Header h;

    // Named for another node: not repeated, delivered once.
    std::memcpy(heard, frame, n);
    heard[14] = 0x77;
    heard[12] = (uint8_t)((heard[12] & ~0x07) | 2);  // came through a relay, one hop spent
    CHECK(r.handleFrame(heard, n, 0, cap, relay, &relay_len) == RX_HANDLED && relay_len == 0);
    // The same frame as a flood fallback is repeated, without delivering it twice.
    heard[14] = 0;
    CHECK(r.handleFrame(heard, n, 0, cap, relay, &relay_len) == RX_DUPLICATE && relay_len == n);
    CHECK(parseHeader(relay, relay_len, h) && h.hopLimit() == 1 && h.next_hop == 0 && h.relay == 0xD4);
    CHECK(cap.texts.size() == 1);
    // A third copy from a relay is not repeated again.
    CHECK(r.handleFrame(heard, n, 0, cap, relay, &relay_len) == RX_DUPLICATE && relay_len == 0);

    // Named for us: repeated, and the next hop is cleared (no route is kept).
    Bridge r2 = makeNode(0xE5E5E5E5u, "eps", 3);
    r2.setRelay(true);
    size_t n2 = a.buildText(3, kBroadcast, "dos", 3, frame, sizeof(frame));
    frame[14] = 0xE5;
    CHECK(r2.handleFrame(frame, n2, 0, cap, relay, &relay_len) == RX_HANDLED && relay_len == n2);
    CHECK(parseHeader(relay, relay_len, h) && h.next_hop == 0 && h.relay == 0xE5);
    // The originator sends it again (hop_start == hop_limit): repeated again, once per retry.
    frame[14] = 0;
    CHECK(r2.handleFrame(frame, n2, 0, cap, relay, &relay_len) == RX_DUPLICATE && relay_len == n2);
    // A copy from a relay, not the originator, is not.
    frame[12] = (uint8_t)((frame[12] & ~0x07) | 1);
    CHECK(r2.handleFrame(frame, n2, 0, cap, relay, &relay_len) == RX_DUPLICATE && relay_len == 0);

    // Never repeated: no hops left, our own frame, a frame for us alone.
    Bridge r3 = makeNode(0xF6F6F6F6u, "zeta", 4);
    r3.setRelay(true);
    size_t n3 = a.buildText(3, kBroadcast, "tres", 4, frame, sizeof(frame));
    frame[12] = (uint8_t)(frame[12] & ~0x07);
    CHECK(r3.handleFrame(frame, n3, 0, cap, relay, &relay_len) == RX_HANDLED && relay_len == 0);
    n3 = a.buildText(3, 0xF6F6F6F6u, "solo", 4, frame, sizeof(frame));
    CHECK(r3.handleFrame(frame, n3, 0, cap, relay, &relay_len) == RX_HANDLED && relay_len == 0);
}

TEST(mt_telemetry, "UNIT-MT-038", "Device metrics encode as Meshtastic's Telemetry and travel as a TELEMETRY_APP frame") {
    DeviceMetrics m = {};
    m.battery_level = 50; m.has_voltage = true; m.voltage = 3.7f;
    m.channel_utilization = 10.0f; m.air_util_tx = 1.0f; m.uptime_s = 300;
    uint8_t pb[48];
    size_t n = encodeTelemetry(m, pb, sizeof(pb));
    const uint8_t golden[] = {0x12, 0x14, 0x08, 0x32, 0x15, 0xCD, 0xCC, 0x6C, 0x40, 0x1D, 0x00, 0x00, 0x20, 0x41,
                              0x25, 0x00, 0x00, 0x80, 0x3F, 0x28, 0xAC, 0x02};
    CHECK(n == sizeof(golden) && std::memcmp(pb, golden, n) == 0);
    m.time = 0x01020304u;
    size_t nt = encodeTelemetry(m, pb, sizeof(pb));
    CHECK(nt == n + 5 && pb[0] == 0x0D && pb[1] == 0x04 && pb[4] == 0x01);  // time, fixed32 field 1
    m.has_voltage = false;                                                  // powered, no battery
    m.battery_level = 101;
    CHECK(encodeTelemetry(m, pb, sizeof(pb)) > 0 && encodeTelemetry(m, pb, 5) == 0);

    Bridge a = makeNode(0xA1A1A1A1u, "alpha", 1), b = makeNode(0xB2B2B2B2u, "beta", 2);
    uint8_t frame[kMaxFrame], plain[kMaxFrame];
    size_t fn = a.buildTelemetry(3, m, frame, sizeof(frame));
    Header h;
    CHECK(fn > kHeaderLen && parseHeader(frame, fn, h) && h.to == kBroadcast);
    std::memcpy(plain, frame + kHeaderLen, fn - kHeaderLen);
    ctrCrypt(refAes, kDefaultKey, h.from, h.id, plain, fn - kHeaderLen);
    Data d;
    CHECK(decodeData(plain, fn - kHeaderLen, d) && d.portnum == PORT_TELEMETRY && d.payload_len > 0);
    CHECK(a.buildTelemetry(9, m, frame, sizeof(frame)) == 0);  // unknown channel
}

TEST(mt_config_roles, "UNIT-MT-039", "A companion has no channel or telemetry setting; its relay starts off; timers wrap") {
    RepeaterPrefs p;
    setDefaults(p, 7, RELAY_OFF, ROLE_CLIENT);
    CHECK(p.relay == RELAY_OFF && p.hops == 7 && p.enable == 1);
    bool ch = true;
    CHECK(mtCmd(p, "set z.mt.channel Mesh", &ch, false).find("Error") == 0 && !ch);
    CHECK(mtCmd(p, "get z.mt.telemetry", &ch, false).find("Error") == 0);
    CHECK(mtCmd(p, "set z.mt.relay all", &ch, false) == "OK" && ch && p.relay == RELAY_ALL);
    CHECK(mtCmd(p, "set z.mt.hops 2", &ch, false) == "OK" && p.hops == 2);
    Timer t;
    CHECK(!t.armed() && !t.due(0));
    t.arm(0xFFFFFF00u, 0x200);          // crosses the wrap
    CHECK(t.armed() && !t.due(0xFFFFFF00u) && !t.due(0x000000FFu) && t.due(0x00000100u) && t.due(0x5000u));
    t.stop();
    CHECK(!t.due(0x5000u));
}

TEST(mt_discriminate_shapes, "UNIT-MT-040", "Every MeshCore payload type, route and path width passes the shape test; Meshtastic broadcast and version 2/3 first bytes never do") {
    const struct { uint8_t type; size_t fixed; } kTypes[] = {
        {PAYLOAD_TYPE_REQ, 4}, {PAYLOAD_TYPE_RESPONSE, 4}, {PAYLOAD_TYPE_TXT_MSG, 4}, {PAYLOAD_TYPE_PATH, 4},
        {PAYLOAD_TYPE_ANON_REQ, 1 + PUB_KEY_SIZE + 2}, {PAYLOAD_TYPE_GRP_TXT, 3}, {PAYLOAD_TYPE_GRP_DATA, 3},
    };
    const uint8_t kRoutes[] = {ROUTE_TYPE_FLOOD, ROUTE_TYPE_DIRECT, ROUTE_TYPE_TRANSPORT_FLOOD, ROUTE_TYPE_TRANSPORT_DIRECT};
    uint8_t frame[MAX_TRANS_UNIT];

    for (auto t : kTypes) {
        for (uint8_t route : kRoutes) {
            for (uint8_t width = 0; width < 3; width++) {
                for (uint8_t hops = 0; hops < 4; hops++) {
                    for (size_t blocks = 1; blocks < 11; blocks++) {
                        size_t i = 0, payload = t.fixed + 16 * blocks;
                        std::memset(frame, 0x5A, sizeof(frame));
                        frame[i++] = (uint8_t)((t.type << PH_TYPE_SHIFT) | route);
                        if (route == ROUTE_TYPE_TRANSPORT_FLOOD || route == ROUTE_TYPE_TRANSPORT_DIRECT) i += 4;
                        frame[i++] = (uint8_t)((width << 6) | hops);
                        i += (size_t)hops * (width + 1);
                        if (payload > MAX_PACKET_PAYLOAD || i + payload > MAX_TRANS_UNIT) continue;  // not a legal MeshCore frame
                        CHECK(looksLikeMeshCore(frame, i + payload, 0));
                        CHECK(!looksLikeMeshCore(frame, i + payload - 1, 0));
                    }
                }
            }
        }
    }
    // Meshtastic broadcast starts with 0xFF (payload version 3); any "to" whose low byte has a version bit set is not MeshCore.
    Bridge a = makeNode(0xA1A1A1A1u, "alpha", 7);
    uint8_t mt[kMaxFrame];
    size_t n = a.buildText(3, kBroadcast, "hola", 4, mt, sizeof(mt));
    CHECK(n > 0 && !looksLikeMeshCore(mt, n, 0));
    for (uint32_t to : {0x123456C0u, 0x123456FFu, 0x00000080u, 0xDEADBE40u}) {
        n = a.buildText(3, to, "hola", 4, mt, sizeof(mt));
        CHECK(n > 0 && !looksLikeMeshCore(mt, n, 0));
    }
}

TEST(mt_origin_delay, "UNIT-MT-041", "A frame of our own waits 0 to 7 slots, never as long as a client repeat") {
    const uint32_t slot = slotTimeMs(11, 250.0f);     // LongFast
    CHECK(slot == 28 && originWindow() == 8);
    CHECK(originDelayMs(slot, 0) == 0 && originDelayMs(slot, 7) == 7 * slot);
    for (uint32_t r = 0; r < originWindow(); r++) {
        CHECK(originDelayMs(slot, r) < rebroadcastDelayMs(slot, 0));  // the first slot a client repeat can take
    }
}

static std::string toHexStr(const uint8_t* b, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += d[b[i] >> 4]; s += d[b[i] & 15]; }
    return s;
}

TEST(mt_channel_slot, "UNIT-MT-042", "A channel's slot is #mt-<name> in lower case; its secret is the first 16 bytes of the SHA-256 of that name, as for a # channel") {
    char slot[kSlotNameMax];
    slotName("SFNarrow", slot);
    CHECK(std::string(slot) == "#mt-sfnarrow");
    slotName("Test 2", slot);
    CHECK(std::string(slot) == "#mt-test-2");
    slotName("Catalunya12", slot);  // the longest name still fits
    CHECK(std::string(slot) == "#mt-catalunya12");
    // Secrets of #test and #bot as a MeshCore companion holds them (read from a device).
    uint8_t secret[16];
    slotSecret("#test", refSha256, secret);
    CHECK(toHexStr(secret, 16) == "9cd8fcf22a47333b591d96a2b848b73f");
    slotSecret("#bot", refSha256, secret);
    CHECK(toHexStr(secret, 16) == "eb50a1bcb3e4e5d7bf69a57c9dada211");
    // Two channels that share a Meshtastic key get different secrets.
    uint8_t a[16], b[16];
    slotSecret("#mt-test", refSha256, a);
    slotSecret("#mt-bots", refSha256, b);
    CHECK(std::memcmp(a, b, 16) != 0);
}

TEST(mt_channel_table, "UNIT-MT-043", "z.mt.channel.add and .remove keep a table of channels on a companion; names differing only in case, a bad key and a ninth channel are refused") {
    RepeaterPrefs p;
    setDefaults(p, 3, RELAY_OFF, ROLE_CLIENT);
    bool ch = false;
    CHECK(mtCmd(p, "get z.mt.channels", &ch, false) == "> none" && !ch);
    CHECK(mtCmd(p, "set z.mt.channel.add SFNarrow", &ch, false) == "OK" && ch);
    CHECK(mtCmd(p, "set z.mt.channel.add Bots Ag==", &ch, false) == "OK" && ch);
    CHECK(mtCmd(p, "set z.mt.channel.add Mine AAECAwQFBgcICQoLDA0ODw==", &ch, false) == "OK" && ch);
    CHECK(mtCmd(p, "get z.mt.channels", &ch, false) == "> SFNarrow=1 Bots=2 Mine=key");
    const ChannelEntry* e = findChannel(p, "#mt-bots");
    CHECK(e && std::string(e->name) == "Bots" && e->psk_len == 1 && e->psk[0] == 2);
    e = findChannel(p, "#mt-mine");
    CHECK(e && e->psk_len == 16 && e->psk[15] == 0x0F);
    // Same slot name: refused, nothing changes.
    CHECK(mtCmd(p, "set z.mt.channel.add bots", &ch, false).find("Error") == 0 && !ch);
    CHECK(mtCmd(p, "set z.mt.channel.add Bad AAEC", &ch, false).find("Error") == 0 && !ch);
    CHECK(mtCmd(p, "set z.mt.channel.add", &ch, false).find("Error") == 0 && !ch);
    for (int i = 0; i < kTableMax - 3; i++) {
        char cmd[48];
        snprintf(cmd, sizeof(cmd), "set z.mt.channel.add C%d", i);
        CHECK(mtCmd(p, cmd, &ch, false) == "OK");
    }
    CHECK(mtCmd(p, "set z.mt.channel.add Nine", &ch, false).find("Error") == 0 && !ch);
    // Remove: the slot to free is reported, and an unknown name changes nothing.
    char reply[96];
    CmdResult r = handleCommand(p, "set z.mt.channel.remove BOTS", reply, sizeof(reply), false);
    CHECK(r.handled && r.changed && std::string(reply) == "OK" && std::string(r.removed) == "#mt-bots");
    CHECK(!findChannel(p, "#mt-bots"));
    CHECK(mtCmd(p, "set z.mt.channel.remove Bots", &ch, false).find("Error") == 0 && !ch);
    r = handleCommand(p, "set z.mt.channel.add Nine", reply, sizeof(reply), false);
    CHECK(r.changed && std::string(reply) == "OK" && std::string(r.added) == "#mt-nine");  // the freed entry is reused
    CHECK(mtCmd(p, "set z.mt.channel.add Nine", &ch, false).find("Error") == 0 && !ch);
    // A repeater has one channel of its own and no table; a companion has no primary channel to set.
    CHECK(mtCmd(p, "set z.mt.channel.add X", &ch, true).find("Error") == 0 && !ch);
    CHECK(mtCmd(p, "get z.mt.channels", &ch, true).find("Error") == 0);
    CHECK(mtCmd(p, "set z.mt.channel X", &ch, false).find("Error") == 0 && !ch);
}

TEST(mt_channel_sanitize, "UNIT-MT-044", "An entry with a bad name or key is cleared when the settings are put in range") {
    RepeaterPrefs p;
    setDefaults(p, 3, RELAY_OFF, ROLE_CLIENT);
    std::strcpy(p.channels[0].name, "Good");
    p.channels[0].psk_len = 1;
    p.channels[0].psk[0] = 2;
    std::strcpy(p.channels[1].name, "NoKey");
    p.channels[1].psk_len = 5;
    std::strcpy(p.channels[2].name, "bad\x01name");
    p.channels[2].psk_len = 1;
    sanitize(p);
    CHECK(std::string(p.channels[0].name) == "Good" && p.channels[0].psk[0] == 2);
    CHECK(p.channels[1].name[0] == '\0' && p.channels[2].name[0] == '\0');
}

namespace {
bool slotIn(const char* slot, void* ctx) {
    for (const char* const* n = static_cast<const char* const*>(ctx); *n; n++) {
        if (std::strcmp(*n, slot) == 0) { return true; }
    }
    return false;
}

SlotWrite writeSlot(RepeaterPrefs& p, ChannelEntry* held, const char* next, bool own_secret = true) {
    uint8_t secret[16];
    slotSecret(next, refSha256, secret);
    if (!own_secret) { secret[0] ^= 1; }
    return slotWrite(p, held, next, secret, refSha256);
}
}  // namespace

TEST(mt_slot_write, "UNIT-MT-045", "The app moves a channel of the table to another slot and keeps its key; it cannot create a #mt- channel") {
    RepeaterPrefs p;
    setDefaults(p, 3, RELAY_OFF, ROLE_CLIENT);
    const uint8_t one = 1, two = 2;
    CHECK(addChannel(p, "Test", &two, 1) == ADD_OK && addChannel(p, "Bots", &one, 1) == ADD_OK);
    ChannelEntry* test = findChannel(p, "#mt-test");
    // A swap: slot 0 held Test and now gets Bots, slot 1 held Bots and now gets Test. Both stay.
    CHECK(writeSlot(p, test, "#mt-bots") == SLOT_WRITE_KEPT);
    CHECK(writeSlot(p, findChannel(p, "#mt-bots"), "#mt-test") == SLOT_WRITE_KEPT);
    CHECK(findChannel(p, "#mt-test")->psk[0] == 2 && findChannel(p, "#mt-bots")->psk[0] == 1);
    // The app's own channel, whatever its name, is not ours to judge.
    CHECK(writeSlot(p, test, "#bot") == SLOT_WRITE_OTHER && writeSlot(p, nullptr, "Public") == SLOT_WRITE_OTHER);
    // Not created from the app, with or without a slot behind it; a name without its own secret either.
    CHECK(writeSlot(p, nullptr, "#mt-new") == SLOT_WRITE_REFUSED);
    CHECK(writeSlot(p, test, "#mt-bots", false) == SLOT_WRITE_REFUSED);
    CHECK(writeSlot(p, test, "#mt-New_Name") == SLOT_WRITE_REFUSED);
    CHECK(findChannel(p, "#mt-test") == test && !findChannel(p, "#mt-new"));
}

TEST(mt_slot_rename, "UNIT-MT-046", "Renaming the slot of an entry to another #mt- name renames the entry and keeps its key") {
    RepeaterPrefs p;
    setDefaults(p, 3, RELAY_OFF, ROLE_CLIENT);
    const uint8_t two = 2;
    CHECK(addChannel(p, "Test", &two, 1) == ADD_OK);
    ChannelEntry* test = findChannel(p, "#mt-test");
    CHECK(writeSlot(p, test, "#mt-renamed") == SLOT_WRITE_RENAMED);
    CHECK(!findChannel(p, "#mt-test") && findChannel(p, "#mt-renamed") == test);
    CHECK(std::string(test->name) == "renamed" && test->psk_len == 1 && test->psk[0] == 2);
}

TEST(mt_drop_orphans, "UNIT-MT-047", "An entry no slot holds is dropped when the app's writes stop; one a slot holds stays") {
    RepeaterPrefs p;
    setDefaults(p, 3, RELAY_OFF, ROLE_CLIENT);
    const uint8_t one = 1;
    CHECK(addChannel(p, "Test", &one, 1) == ADD_OK && addChannel(p, "Bots", &one, 1) == ADD_OK);
    const char* const slots[] = {"#mt-bots", nullptr};
    CHECK(dropOrphans(p, slotIn, const_cast<void*>(static_cast<const void*>(slots))));
    CHECK(!findChannel(p, "#mt-test") && findChannel(p, "#mt-bots"));
    CHECK(!dropOrphans(p, slotIn, const_cast<void*>(static_cast<const void*>(slots))));
}

TEST(mt_config_on_off_digits, "UNIT-MT-048", "z.mt.enable and z.mt.relay.opaque take 1 and 0 as on and off") {
    RepeaterPrefs p;
    setDefaults(p, 3, RELAY_ALL, ROLE_CLIENT);
    bool ch = false;
    CHECK(mtCmd(p, "set z.mt.enable 0", &ch) == "OK" && ch && p.enable == 0 && mtCmd(p, "get z.mt.enable") == "> off");
    CHECK(mtCmd(p, "set z.mt.enable 1", &ch) == "OK" && ch && p.enable == 1);
    CHECK(mtCmd(p, "set z.mt.relay.opaque 0", &ch) == "OK" && ch && p.relay_opaque == 0);
    CHECK(mtCmd(p, "set z.mt.relay.opaque maybe", &ch) == "Error: on|off|1|0" && !ch);
}

// A header as a repeater would hand it on: hop_start 3, the given hop_limit and relay byte.
static Header relayedHeader(uint8_t hop_limit, uint8_t relay) {
    Header h = {};
    h.from = 0x11111111u;
    h.to = kBroadcast;
    h.id = 7;
    h.flags = (uint8_t)((3 << 5) | hop_limit);
    h.relay = relay;
    return h;
}

TEST(mt_zerohop_keeps, "UNIT-MT-049", "Hops are kept only past the first hop, for one live neighbour with a fixed role or a listed node") {
    ZeroHop z;
    neighbours::Table heard;
    const uint32_t none[kZeroHopNodesMax] = {};
    const uint32_t router = 0xAABBCC42u, client = 0x00DDEE17u, now = 1000;

    heard.note(router, 20, now);
    heard.note(client, 20, now);
    CHECK(!z.keepsHop(relayedHeader(2, 0x42), none, heard, now));  // role not known yet
    z.noteRole(router, ROLE_ROUTER, heard);
    z.noteRole(client, ROLE_CLIENT, heard);
    CHECK(z.keepsHop(relayedHeader(2, 0x42), none, heard, now));
    CHECK(!z.keepsHop(relayedHeader(2, 0x17), none, heard, now));  // a client counts hops
    CHECK(!z.keepsHop(relayedHeader(3, 0x42), none, heard, now));  // first hop
    CHECK(!z.keepsHop(relayedHeader(2, 0), none, heard, now));     // no relay byte
    Header old = relayedHeader(2, 0x42);
    old.flags &= 0x1F;                                             // hop_start not set
    CHECK(!z.keepsHop(old, none, heard, now));
    CHECK(!z.keepsHop(relayedHeader(2, 0x99), none, heard, now));  // not a neighbour

    CHECK(z.keepsHop(relayedHeader(2, 0x42), none, heard, now + kZeroHopLiveS));
    CHECK(!z.keepsHop(relayedHeader(2, 0x42), none, heard, now + kZeroHopLiveS + 1));  // not heard for too long

    heard.note(0x12345642u, 10, now);                              // a second neighbour with the same byte
    CHECK(!z.keepsHop(relayedHeader(2, 0x42), none, heard, now));

    const uint32_t listed[kZeroHopNodesMax] = {0, client};
    CHECK(z.keepsHop(relayedHeader(2, 0x17), listed, heard, now)); // listed by hand, whatever its role
    CHECK(!z.keepsHop(relayedHeader(3, 0x17), listed, heard, now));
}

TEST(mt_zerohop_roles, "UNIT-MT-050", "router, router_late and client_base are fixed; a node that changes role is forgotten; the table favours neighbours") {
    CHECK(roleIsFixed(ROLE_ROUTER) && roleIsFixed(ROLE_ROUTER_LATE) && roleIsFixed(ROLE_CLIENT_BASE));
    CHECK(!roleIsFixed(ROLE_CLIENT) && !roleIsFixed(ROLE_CLIENT_MUTE) && !roleIsFixed(ROLE_REPEATER));
    CHECK(relayByte(0xAABBCC42u) == 0x42 && relayByte(0xAABBCC00u) == 0xFF);

    ZeroHop z;
    neighbours::Table heard;
    const uint32_t base = 0x0A0B0C21u;
    heard.note(base, 8, 5);
    z.noteRole(base, ROLE_CLIENT_BASE, heard);
    CHECK(z.isFixed(base));
    for (uint32_t i = 0; i < 2 * kZeroHopFixedMax; i++) {
        z.noteRole(0x50000000u + i, ROLE_ROUTER, heard);          // far routers, never heard directly
    }
    CHECK(z.isFixed(base));                                        // the neighbour kept its entry
    z.noteRole(base, ROLE_CLIENT, heard);
    CHECK(!z.isFixed(base) && !z.isFixed(0));
}

TEST(mt_zerohop_loop, "UNIT-MT-051", "Two repeaters that keep each other's hops do not pass a frame back and forth") {
    Bridge a = makeNode(0xA1A1A1A1u, "alpha", 7), b = makeNode(0xB2B2B2B2u, "beta", 9), c = makeNode(0xC3C3C3C3u, "gamma", 5);
    b.setRelay(true);
    c.setRelay(true);
    Capture cap;
    uint8_t frame[kMaxFrame], from_b[kMaxFrame], from_c[kMaxFrame], again[kMaxFrame];
    size_t nb = 0, nc = 0, na = 0;
    size_t n = a.buildText(3, kBroadcast, "hola", 4, frame, sizeof(frame));
    CHECK(b.handleFrame(frame, n, 0, cap, from_b, &nb) == RX_HANDLED && nb == n);
    CHECK(c.handleFrame(from_b, nb, 0, cap, from_c, &nc) == RX_HANDLED && nc == n);
    from_c[12] = from_b[12];                                       // c kept the hop_limit b sent
    CHECK(b.handleFrame(from_c, nc, 0, cap, again, &na) == RX_DUPLICATE && na == 0);
    Header hb, hc;
    CHECK(parseHeader(from_b, nb, hb) && parseHeader(from_c, nc, hc));
    CHECK(hc.hopLimit() == hb.hopLimit() && hc.hopStart() > hc.hopLimit() && hc.relay == 0xC3);
}

TEST(mt_zerohop_cli, "UNIT-MT-052", "z.mt.zerohop starts off, takes off or auto, and keeps a list of up to eight nodes") {
    RepeaterPrefs p;
    setDefaults(p, 3, RELAY_ESSENTIAL, ROLE_CLIENT_BASE);
    bool ch = false;
    CHECK(p.zerohop == ZEROHOP_OFF && mtCmd(p, "get z.mt.zerohop") == "> off");
    CHECK(mtCmd(p, "set z.mt.zerohop auto", &ch) == "OK" && ch && p.zerohop == ZEROHOP_AUTO);
    CHECK(mtCmd(p, "set z.mt.zerohop on", &ch) == "Error: off|auto" && !ch);
    CHECK(mtCmd(p, "set z.mt.zerohop.add !a1b2c3d4", &ch) == "OK" && ch && p.zerohop_nodes[0] == 0xA1B2C3D4u);
    CHECK(mtCmd(p, "set z.mt.zerohop.add a1b2c3d4", &ch) == "OK" && !ch);  // already listed
    CHECK(mtCmd(p, "set z.mt.zerohop.add 0000beef", &ch) == "OK" && ch);
    CHECK(mtCmd(p, "get z.mt.zerohop") == "> auto !a1b2c3d4 !0000beef");
    CHECK(mtCmd(p, "set z.mt.zerohop.add 42", &ch).find("Error") == 0 && !ch);
    CHECK(mtCmd(p, "set z.mt.zerohop.add 00000000", &ch).find("Error") == 0 && !ch);
    CHECK(mtCmd(p, "set z.mt.zerohop.add a1b2c3d4x", &ch).find("Error") == 0 && !ch);
    CHECK(mtCmd(p, "set z.mt.zerohop.remove !a1b2c3d4", &ch) == "OK" && ch && p.zerohop_nodes[0] == 0);
    CHECK(mtCmd(p, "set z.mt.zerohop.remove !a1b2c3d4", &ch) == "Error: no such node" && !ch);
    for (unsigned i = 1; i < kZeroHopNodesMax; i++) {
        char cmd[48];
        snprintf(cmd, sizeof(cmd), "set z.mt.zerohop.add 1000000%u", i);
        CHECK(mtCmd(p, cmd, &ch) == "OK" && ch);
    }
    CHECK(mtCmd(p, "set z.mt.zerohop.add 20000001", &ch) == "Error: 8 nodes at most" && !ch);
    CHECK(mtCmd(p, "set z.mt.zerohop.clear", &ch) == "OK" && ch && mtCmd(p, "get z.mt.zerohop") == "> auto");
    CHECK(mtCmd(p, "set z.mt.zerohop.clear", &ch) == "OK" && !ch);
    p.zerohop = 9;
    sanitize(p);
    CHECK(p.zerohop == ZEROHOP_OFF);
}

TEST(mt_roles_by_firmware, "UNIT-MT-053", "A repeater may be client, client_base, router, router_late or repeater; a companion client_mute or client; a muted client repeats nothing") {
    RepeaterPrefs p;
    setDefaults(p, 3, RELAY_ESSENTIAL, ROLE_CLIENT_BASE);
    bool ch = false;
    CHECK(mtCmd(p, "get z.mt.role") == "> client_base");
    CHECK(mtCmd(p, "set z.mt.role client_mute", &ch) == "Error: client|client_base|router|router_late|repeater" && !ch);
    CHECK(mtCmd(p, "set z.mt.role router", &ch) == "OK" && ch && p.role == ROLE_ROUTER);
    CHECK(mtCmd(p, "set z.mt.role client_base", &ch) == "OK" && ch && p.role == ROLE_CLIENT_BASE);

    setDefaults(p, 3, RELAY_OFF, ROLE_CLIENT_MUTE);
    CHECK(mtCmd(p, "get z.mt.role", nullptr, false) == "> client_mute");
    CHECK(mtCmd(p, "set z.mt.role router", &ch, false) == "Error: client_mute|client" && !ch);
    CHECK(mtCmd(p, "set z.mt.role client_base", &ch, false).find("Error") == 0 && !ch);
    CHECK(mtCmd(p, "set z.mt.role client", &ch, false) == "OK" && ch && p.role == ROLE_CLIENT);
    CHECK(mtCmd(p, "set z.mt.role client_mute", &ch, false) == "OK" && ch && p.role == ROLE_CLIENT_MUTE);

    p.role = ROLE_CLIENT_BASE;
    sanitize(p);
    CHECK(p.role == ROLE_CLIENT_BASE);                             // a stored role is kept
    CHECK(!roleRepeats(ROLE_CLIENT_MUTE) && roleRepeats(ROLE_CLIENT) && roleRepeats(ROLE_CLIENT_BASE));
    CHECK(roleYieldsToEcho(ROLE_CLIENT_BASE) && !roleSendsEarly(ROLE_CLIENT_BASE));
}

static Header fwHeader(uint32_t from, uint8_t relay) {
    static uint32_t next_id = 1;   // each call is a frame of its own
    Header h = {};
    h.from = from;
    h.id = next_id++;
    h.relay = relay;
    return h;
}

TEST(mt_fw_screen, "UNIT-FW-012", "Meshtastic: a rule drops its port, a NodeInfo gap and a relay ceiling follow, routing and unopened frames have no type") {
    fw::Prefs p = {};
    fw::Firewall f;
    FwScreen screen(f, p);
    screen.setNow(0);
    CHECK(!screen.drops(fwHeader(1, 0x11), PORT_TEXT));
    p.drop = (1u << FW_TEXT) | (1u << FW_OTHER);
    CHECK(screen.drops(fwHeader(1, 0x11), PORT_TEXT));
    CHECK(screen.drops(fwHeader(1, 0x11), 7));
    CHECK(!screen.drops(fwHeader(1, 0x11), PORT_ROUTING) && !screen.drops(fwHeader(1, 0x11), 0));
    CHECK(!screen.drops(fwHeader(1, 0x11), PORT_POSITION));
    p = {};
    p.advert_gap_min = 10;
    CHECK(!screen.drops(fwHeader(5, 0x11), PORT_NODEINFO));
    screen.setNow(9 * 60000u);
    CHECK(screen.drops(fwHeader(5, 0x11), PORT_NODEINFO));
    CHECK(!screen.drops(fwHeader(6, 0x11), PORT_NODEINFO) && !screen.drops(fwHeader(5, 0x11), PORT_TEXT));
    screen.setNow(10 * 60000u);
    CHECK(!screen.drops(fwHeader(5, 0x11), PORT_NODEINFO));
    p = {};
    p.hop_rate = 2;
    p.advert_gap_min = 10;
    screen.setNow(20 * 60000u);
    CHECK(!screen.drops(fwHeader(8, 0x22), PORT_TEXT) && !screen.drops(fwHeader(8, 0x22), 0));
    CHECK(screen.drops(fwHeader(9, 0x22), PORT_NODEINFO));
    CHECK(!screen.drops(fwHeader(9, 0x33), PORT_NODEINFO));   // another relay, so the gap was not opened by the dropped one
    CHECK(!screen.drops(fwHeader(8, 0), PORT_TEXT) && !screen.drops(fwHeader(8, 0), PORT_TEXT));  // no relay byte: no ceiling
}

TEST(mt_fw_bridge, "UNIT-FW-013", "A frame the screen drops is not delivered, repeated or remembered; a copy of an accepted one is a duplicate, not screened") {
    Bridge a = makeNode(0xA1A1A1A1u, "alpha", 31), r = makeNode(0xD4D4D4D4u, "relay", 32);
    r.setRelay(true);
    fw::Prefs p = {};
    fw::Firewall f;
    FwScreen screen(f, p);
    r.setScreen(&screen);
    uint8_t one[kMaxFrame], two[kMaxFrame], out[kMaxFrame];
    size_t out_len = 0;
    size_t n1 = a.buildText(3, kBroadcast, "uno", 3, one, sizeof(one));
    size_t n2 = a.buildText(3, kBroadcast, "dos", 3, two, sizeof(two));
    Capture cap;
    p.drop = 1u << FW_TEXT;
    CHECK(r.handleFrame(one, n1, 0, cap, out, &out_len) == RX_DROPPED && out_len == 0 && cap.texts.empty());
    CHECK(r.handleFrame(one, n1, 0, cap, out, &out_len) == RX_DROPPED);
    p.drop = 0;
    p.hop_rate = 1;
    CHECK(r.handleFrame(one, n1, 0, cap, out, &out_len) == RX_DROPPED);   // the verdict of a copy is kept
    uint8_t three[kMaxFrame];
    size_t n3 = a.buildText(3, kBroadcast, "tres", 4, three, sizeof(three));
    CHECK(r.handleFrame(three, n3, 0, cap, out, &out_len) == RX_HANDLED && out_len == n3 && cap.texts.size() == 1);
    CHECK(r.handleFrame(two, n2, 0, cap, out, &out_len) == RX_DROPPED && out_len == 0 && cap.texts.size() == 1);
    CHECK(r.handleFrame(three, n3, 0, cap, out, &out_len) == RX_DUPLICATE);
    CHECK(f.counters().rule[FW_TEXT] == 1 && f.counters().rate == 1);   // copies of a dropped frame are not counted again
}

TEST(mt_fw_commands, "UNIT-FW-014", "z.mt.fw.* use the Meshtastic rule names and refuse the MeshCore ones") {
    fw::Prefs p = {};
    fw::Firewall f;
    char out[100];
    auto run = [&](const char *line, bool *changed = nullptr) {
        fw::CmdResult r = fw::handleCommand(p, f, kFwRules, line, out, sizeof(out));
        if (changed) *changed = r.changed;
        return std::string(out);
    };
    bool changed = false;
    CHECK(fw::isCommand("get z.mt.fw.gap") && fw::isMeshtastic("get z.mt.fw.gap") && !fw::isMeshtastic("get z.mc.fw.gap"));
    CHECK(run("set z.mt.fw.drop nodeinfo on", &changed) == "OK" && p.drop == (1u << FW_NODEINFO) && changed);
    CHECK(run("set z.mt.fw.drop traceroute 1") == "OK" && run("get z.mt.fw.drop") == "> nodeinfo 0 traceroute 0");
    CHECK(run("set z.mt.fw.drop advert on").rfind("Error", 0) == 0 && run("set z.mt.fw.drop group on").rfind("Error", 0) == 0);
    CHECK(run("set z.mt.fw.gap 30", &changed) == "OK" && p.advert_gap_min == 30 && changed);
    CHECK(run("set z.mt.fw.rate 12") == "OK" && p.hop_rate == 12 && run("get z.mt.fw.rate") == "> 12");
    CHECK(run("get z.mt.fw.stats") == "> gap 0, rate 0, drop 0");
    CHECK(run("set z.mt.fw.drop none", &changed) == "OK" && p.drop == 0 && changed);
}
