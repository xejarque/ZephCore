#include "test.h"
#include <cstring>
#include <vector>
#include <helpers/meshtastic/MtPhoneWire.h>

using namespace mt;

namespace {

typedef std::vector<uint8_t> Bytes;

Bytes bytes(const uint8_t *p, size_t n) { return Bytes(p, p + n); }

} // namespace

TEST(mtphone_to_want_config, "UNIT-MTPHONE-001", "ToRadio want_config_id decodes; the heartbeat and disconnect variants are recognised") {
    const uint8_t want[] = {0x18, 0xAC, 0x9E, 0x04};   // want_config_id = 69420
    ToRadio t;
    CHECK(decodeToRadio(want, sizeof(want), t));
    CHECK(t.kind == ToKind::WANT_CONFIG && t.want_config_id == 69420);

    const uint8_t disc[] = {0x20, 0x01};
    CHECK(decodeToRadio(disc, sizeof(disc), t) && t.kind == ToKind::DISCONNECT);

    const uint8_t hb[] = {0x3A, 0x00};                  // heartbeat {}
    CHECK(decodeToRadio(hb, sizeof(hb), t) && t.kind == ToKind::HEARTBEAT);

    const uint8_t xmodem[] = {0x2A, 0x00};              // a variant we do not serve
    CHECK(decodeToRadio(xmodem, sizeof(xmodem), t) && t.kind == ToKind::OTHER);
}

TEST(mtphone_to_packet, "UNIT-MTPHONE-002", "ToRadio packet with a text decodes its addressing, id, ACK request and payload") {
    const uint8_t raw[] = {
        0x0A, 0x19,                                     // packet, 25 bytes
        0x15, 0xFF, 0xFF, 0xFF, 0xFF,                   // to = broadcast
        0x18, 0x01,                                     // channel 1
        0x22, 0x09, 0x08, 0x01, 0x12, 0x05, 'h', 'e', 'l', 'l', 'o',  // decoded: text "hello"
        0x35, 0x44, 0x33, 0x22, 0x11,                   // id
        0x50, 0x01};                                    // want_ack
    ToRadio t;
    CHECK(decodeToRadio(raw, sizeof(raw), t));
    CHECK(t.kind == ToKind::PACKET);
    CHECK(t.packet.to == kBroadcast && t.packet.channel == 1 && t.packet.id == 0x11223344);
    CHECK(t.packet.want_ack && t.packet.has_decoded);
    CHECK(t.packet.data.portnum == PORT_TEXT && t.packet.data.payload_len == 5);
    CHECK(std::memcmp(t.packet.data.payload, "hello", 5) == 0);
}

TEST(mtphone_to_malformed, "UNIT-MTPHONE-003", "A truncated ToRadio is refused, not half read") {
    const uint8_t cut[] = {0x0A, 0x19, 0x15, 0xFF};     // length says 25, 2 follow
    ToRadio t;
    CHECK(!decodeToRadio(cut, sizeof(cut), t));
    const uint8_t varint[] = {0x18, 0xAC};              // varint with no end
    CHECK(!decodeToRadio(varint, sizeof(varint), t));
    const uint8_t bad_data[] = {0x0A, 0x04, 0x22, 0x02, 0x12, 0xFF};  // Data with a bad length
    CHECK(!decodeToRadio(bad_data, sizeof(bad_data), t));
}

TEST(mtphone_from_small, "UNIT-MTPHONE-004", "FromRadio config_complete, queue status, my_info and channel match the golden bytes") {
    uint8_t out[kMaxFromRadio];
    size_t n = encodeFromRadioConfigComplete(69420, out, sizeof(out));
    const uint8_t done[] = {0x38, 0xAC, 0x9E, 0x04};
    CHECK(bytes(out, n) == bytes(done, sizeof(done)));

    n = encodeFromRadioQueueStatus(0, 16, 16, 0, out, sizeof(out));
    const uint8_t qs[] = {0x5A, 0x04, 0x10, 0x10, 0x18, 0x10};
    CHECK(bytes(out, n) == bytes(qs, sizeof(qs)));

    n = encodeFromRadioMyInfo(0x2A, 0, 3, out, sizeof(out));
    const uint8_t mi[] = {0x1A, 0x04, 0x08, 0x2A, 0x78, 0x03};
    CHECK(bytes(out, n) == bytes(mi, sizeof(mi)));

    const uint8_t psk[] = {0x02};
    n = encodeFromRadioChannel(1, CHANNEL_SECONDARY, "Test", psk, 1, out, sizeof(out));
    const uint8_t ch[] = {0x52, 0x0F, 0x08, 0x01, 0x12, 0x09, 0x12, 0x01, 0x02, 0x1A, 0x04,
                          'T', 'e', 's', 't', 0x18, 0x02};
    CHECK(bytes(out, n) == bytes(ch, sizeof(ch)));
}

TEST(mtphone_from_metadata, "UNIT-MTPHONE-005", "FromRadio metadata carries the firmware version and the capability flags") {
    uint8_t out[kMaxFromRadio];
    PhoneMetadata m = {};
    m.firmware_version = "2.7.0";
    m.has_bluetooth = true;
    m.has_pkc = true;
    size_t n = encodeFromRadioMetadata(m, out, sizeof(out));
    const uint8_t md[] = {0x6A, 0x0B, 0x0A, 0x05, '2', '.', '7', '.', '0', 0x28, 0x01, 0x58, 0x01};
    CHECK(bytes(out, n) == bytes(md, sizeof(md)));
}

TEST(mtphone_from_packet, "UNIT-MTPHONE-006", "A packet for the phone keeps its fields when read back as a MeshPacket") {
    PhonePacket p = {};
    p.from = 0x0A0B0C0D;
    p.to = kBroadcast;
    p.id = 0x55667788;
    p.channel = 2;
    p.rx_time = 1700000000;
    p.rx_snr = 6.5f;
    p.hop_limit = 2;
    p.hop_start = 3;
    p.relay_node = 0x9C;
    p.want_ack = true;
    p.has_decoded = true;
    p.data.portnum = PORT_TEXT;
    const char *text = "hi there";
    p.data.payload = (const uint8_t *)text;
    p.data.payload_len = 8;

    uint8_t out[kMaxFromRadio];
    size_t n = encodeFromRadioPacket(p, out, sizeof(out));
    CHECK(n > 2 && out[0] == 0x12 && out[1] == n - 2);  // FromRadio.packet, one byte length

    out[0] = 0x0A;  // the same MeshPacket as ToRadio.packet
    ToRadio t;
    CHECK(decodeToRadio(out, n, t) && t.kind == ToKind::PACKET);
    CHECK(t.packet.from == p.from && t.packet.to == p.to && t.packet.id == p.id);
    CHECK(t.packet.channel == 2 && t.packet.hop_limit == 2 && t.packet.hop_start == 3);
    CHECK(t.packet.relay_node == 0x9C && t.packet.want_ack);
    CHECK(t.packet.data.portnum == PORT_TEXT && t.packet.data.payload_len == 8);
    CHECK(std::memcmp(t.packet.data.payload, text, 8) == 0);
}

TEST(mtphone_from_nodeinfo, "UNIT-MTPHONE-007", "NodeInfo for the phone starts with the node number and its user, and refuses a short buffer") {
    PhoneNode nd = {};
    nd.num = 0x2A;
    std::strcpy(nd.user.id, "!0000002a");
    std::strcpy(nd.user.long_name, "Ana");
    std::strcpy(nd.user.short_name, "ANA");
    nd.has_hops = true;
    nd.hops = 1;

    uint8_t out[kMaxFromRadio];
    size_t n = encodeFromRadioNodeInfo(nd, out, sizeof(out));
    CHECK(n > 6 && out[0] == 0x22 && out[1] == n - 2);
    CHECK(out[2] == 0x08 && out[3] == 0x2A && out[4] == 0x12);  // num, then user
    CHECK(out[n - 2] == 0x48 && out[n - 1] == 0x01);            // hops_away = 1, last field
    CHECK(encodeFromRadioNodeInfo(nd, out, 8) == 0);
}

TEST(mtphone_from_overflow, "UNIT-MTPHONE-008", "Every FromRadio encoder reports 0 when the output buffer is too small") {
    uint8_t out[3];
    PhonePacket p = {};
    p.has_decoded = true;
    p.data.portnum = PORT_TEXT;
    PhoneMetadata m = {};
    m.firmware_version = "2.7.0";
    CHECK(encodeFromRadioPacket(p, out, sizeof(out)) == 0);
    CHECK(encodeFromRadioMyInfo(1, 0, 0, out, sizeof(out)) == 0);
    CHECK(encodeFromRadioChannel(0, CHANNEL_PRIMARY, "x", nullptr, 0, out, sizeof(out)) == 0);
    CHECK(encodeFromRadioMetadata(m, out, sizeof(out)) == 0);
    CHECK(encodeFromRadioConfigComplete(69420, out, sizeof(out)) == 0);
    CHECK(encodeFromRadioQueueStatus(0, 1, 1, 0, out, sizeof(out)) == 0);
}

#include <helpers/meshtastic/MtPhoneApi.h>

namespace {

struct FakeHost : PhoneHost {
    std::vector<PhonePacket> sent;
    bool accept = true;
    uint32_t selfNode() override { return 0x2A; }
    uint32_t rebootCount() override { return 1; }
    PhoneMetadata metadata() override { PhoneMetadata m = {}; m.firmware_version = "2.7.0"; return m; }
    bool channelAt(uint8_t i, PhoneChannel &ch) override {
        if (i > 1) return false;
        ch = {};
        ch.used = true;
        ch.role = i == 0 ? CHANNEL_PRIMARY : CHANNEL_SECONDARY;
        std::strcpy(ch.name, i == 0 ? "SFNarrow" : "Test");
        ch.psk[0] = 1;
        ch.psk_len = 1;
        return true;
    }
    uint32_t nodeCount() override { return 3; }
    bool nodeAt(uint32_t i, PhoneNode &n) override {
        n = {};
        n.num = 0x2A + i;
        std::strcpy(n.user.long_name, "n");
        return true;
    }
    bool sendFromPhone(const PhonePacket &p) override { if (accept) sent.push_back(p); return accept; }
};

// Reads FromRadio until none is left; the first byte of each is its variant tag.
std::vector<uint8_t> drain(PhoneApi &api) {
    std::vector<uint8_t> tags;
    uint8_t buf[kMaxFromRadio];
    for (int i = 0; i < 64; i++) {
        size_t n = api.read(buf, sizeof(buf));
        if (n == 0) break;
        tags.push_back(buf[0]);
    }
    return tags;
}

const uint8_t kWantConfig[] = {0x18, 0xAC, 0x9E, 0x04};    // CONFIG_NONCE 69420: stage 1
const uint8_t kWantNodes[] = {0x18, 0xAD, 0x9E, 0x04};     // NODE_INFO_NONCE 69421: stage 2
const uint8_t kWantLegacy[] = {0x18, 0x01};                // any other id: everything at once

PhonePacket textPacket(uint32_t id) {
    PhonePacket p = {};
    p.from = 7; p.to = kBroadcast; p.id = id;
    p.has_decoded = true;
    p.data.portnum = PORT_TEXT;
    p.data.payload = (const uint8_t *)"x";
    p.data.payload_len = 1;
    return p;
}

} // namespace

TEST(mtphone_api_download, "UNIT-MTPHONE-009", "want_config_id gets my_info, metadata, 8 channels, the nodes and config_complete, in that order") {
    FakeHost host;
    PhoneApi api(host);
    CHECK(api.onToRadio(kWantLegacy, sizeof(kWantLegacy)));
    CHECK(api.hasPending());
    std::vector<uint8_t> tags = drain(api);
    std::vector<uint8_t> want = {0x1A, 0x6A};
    for (int i = 0; i < 8; i++) want.push_back(0x52);
    for (int i = 0; i < 3; i++) want.push_back(0x22);
    want.push_back(0x38);
    CHECK(tags == want);
    CHECK(!api.hasPending());
}

TEST(mtphone_api_queue, "UNIT-MTPHONE-010", "Packets heard are queued only after the download, bump fromNum and are dropped oldest first when full") {
    FakeHost host;
    PhoneApi api(host);
    api.pushPacket(textPacket(1));
    CHECK(!api.hasPending());                                // no session yet
    api.onToRadio(kWantLegacy, sizeof(kWantLegacy));
    api.pushPacket(textPacket(2));
    drain(api);                                              // download only
    CHECK(!api.hasPending());                                // the packet sent mid-download was dropped
    uint32_t before = api.fromNum();
    for (uint32_t id = 1; id <= kPhoneQueue + 1; id++) api.pushPacket(textPacket(id));
    CHECK(api.fromNum() == before + kPhoneQueue + 1);
    uint8_t buf[kMaxFromRadio];
    size_t n = api.read(buf, sizeof(buf));
    buf[0] = 0x0A;
    ToRadio t;
    CHECK(decodeToRadio(buf, n, t) && t.packet.id == 2);     // id 1 was the oldest and went
    int left = 0;
    while (api.read(buf, sizeof(buf))) left++;
    CHECK(left == kPhoneQueue - 1);
}

TEST(mtphone_api_send, "UNIT-MTPHONE-011", "A packet from the phone goes to the host and is answered with a queue status; a refusal says so") {
    FakeHost host;
    PhoneApi api(host);
    const uint8_t pkt[] = {0x0A, 0x19, 0x15, 0xFF, 0xFF, 0xFF, 0xFF, 0x18, 0x01, 0x22, 0x09, 0x08, 0x01,
                           0x12, 0x05, 'h', 'e', 'l', 'l', 'o', 0x35, 0x44, 0x33, 0x22, 0x11, 0x50, 0x01};
    CHECK(api.onToRadio(pkt, sizeof(pkt)));
    CHECK(host.sent.size() == 1 && host.sent[0].id == 0x11223344 && host.sent[0].channel == 1);
    uint8_t buf[kMaxFromRadio];
    size_t n = api.read(buf, sizeof(buf));
    CHECK(n > 2 && buf[0] == 0x5A);                          // queueStatus
    host.accept = false;
    CHECK(api.onToRadio(pkt, sizeof(pkt)));
    n = api.read(buf, sizeof(buf));
    CHECK(n > 2 && buf[0] == 0x5A && buf[2] == 0x08 && buf[3] == 8);  // res = NO_CHANNEL
}

TEST(mtphone_api_reset, "UNIT-MTPHONE-012", "disconnect and a new want_config_id restart the session and empty the queue") {
    FakeHost host;
    PhoneApi api(host);
    api.onToRadio(kWantLegacy, sizeof(kWantLegacy));
    drain(api);
    api.pushPacket(textPacket(5));
    CHECK(api.hasPending());
    const uint8_t disc[] = {0x20, 0x01};
    api.onToRadio(disc, sizeof(disc));
    CHECK(!api.hasPending());
    api.pushPacket(textPacket(6));
    CHECK(!api.hasPending());                                // not ready again until the download ends
    CHECK(!api.onToRadio((const uint8_t *)"\x18\xAC", 2));    // malformed: refused, state kept
}

TEST(mtphone_api_node, "UNIT-MTPHONE-013", "A node heard after the download is queued as NodeInfo; before it, nothing") {
    FakeHost host;
    PhoneApi api(host);
    PhoneNode n = {};
    n.num = 0x99;
    std::strcpy(n.user.long_name, "Bea");
    api.pushNode(n);
    CHECK(!api.hasPending());
    api.onToRadio(kWantLegacy, sizeof(kWantLegacy));
    drain(api);
    api.pushNode(n);
    uint8_t buf[kMaxFromRadio];
    size_t len = api.read(buf, sizeof(buf));
    CHECK(len > 4 && buf[0] == 0x22 && buf[2] == 0x08 && buf[3] == 0x99);
}

TEST(mtphone_api_stages, "UNIT-MTPHONE-014", "The two-stage handshake: 69420 gets config and channels and no nodes, 69421 only the nodes, each closed with its own id") {
    FakeHost host;
    PhoneApi api(host);
    api.onToRadio(kWantConfig, sizeof(kWantConfig));
    std::vector<uint8_t> stage1 = drain(api);
    std::vector<uint8_t> want1 = {0x1A, 0x6A};
    for (int i = 0; i < 8; i++) want1.push_back(0x52);
    want1.push_back(0x38);
    CHECK(stage1 == want1);
    api.pushPacket(textPacket(1));
    CHECK(!api.hasPending());                                // the app is not ready for packets yet
    api.onToRadio(kWantNodes, sizeof(kWantNodes));
    uint8_t buf[kMaxFromRadio];
    std::vector<uint8_t> stage2;
    std::vector<uint8_t> last;
    for (size_t n; (n = api.read(buf, sizeof(buf))) != 0;) {
        stage2.push_back(buf[0]);
        last.assign(buf, buf + n);
    }
    CHECK((stage2 == std::vector<uint8_t>{0x22, 0x22, 0x22, 0x38}));
    CHECK((last == std::vector<uint8_t>{0x38, 0xAD, 0x9E, 0x04}));   // config_complete_id = 69421
    api.pushPacket(textPacket(2));
    CHECK(api.hasPending());                                 // now it is
}

TEST(mtphone_route_discovery, "UNIT-MTPHONE-015", "RouteDiscovery packs the route and the way back as fixed32 lists, with unmeasured SNR per link") {
    uint8_t out[160];
    const uint32_t there[] = {0x11223344, 0x01};
    const uint32_t back[] = {0xAABBCCDD};
    size_t n = encodeRouteDiscovery(there, 2, back, 1, out, sizeof(out));
    const uint8_t route[] = {0x0A, 0x08, 0x44, 0x33, 0x22, 0x11, 0x01, 0x00, 0x00, 0x00};
    const uint8_t snr[] = {0x80, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    CHECK(n == 10 + 2 + 30 + 6 + 2 + 20);
    CHECK(bytes(out, 10) == bytes(route, 10));
    CHECK(out[10] == 0x12 && out[11] == 30 && bytes(out + 12, 10) == bytes(snr, 10));  // 3 links to there
    CHECK(out[42] == 0x1A && out[43] == 4 && out[48] == 0x22 && out[49] == 20);        // back, then 2 links back
    CHECK(encodeRouteDiscovery(there, 2, back, 1, out, 6) == 0);
}

TEST(mtphone_node_numbers, "UNIT-MTPHONE-016", "A MeshCore key gives its first four bytes as node number, reserved values move to MC, synthetic ones have the top bit") {
    uint8_t pub[32] = {0x12, 0x34, 0x56, 0x78, 0xAA, 0xBB};
    CHECK(mcNodeNum(pub) == 0x12345678u);
    pub[0] = pub[1] = pub[2] = 0; pub[3] = 3;                 // below 4: reserved
    CHECK(mcNodeNum(pub) == 0x4D43AABBu);
    pub[0] = pub[1] = pub[2] = pub[3] = 0xFF;                 // broadcast
    CHECK(mcNodeNum(pub) == 0x4D43AABBu);
    const uint8_t name[] = {'b', 'o', 'b'};
    uint32_t a = syntheticNode(name, 3);
    CHECK(a == syntheticNode(name, 3) && (a & 0x80000000u) && a != kBroadcast);
    CHECK(syntheticNode(name, 2) != a);
}

TEST(mtphone_frame_limits, "UNIT-MTPHONE-017", "The longest text and the longest traceroute reply still fit a queue slot") {
    FakeHost host;
    PhoneApi api(host);
    api.onToRadio(kWantLegacy, sizeof(kWantLegacy));
    drain(api);
    uint8_t text[kMaxFrame];
    std::memset(text, 'a', sizeof(text));
    PhonePacket p = textPacket(9);
    p.channel = 7; p.rx_time = 1; p.rx_snr = -7.5f; p.hop_start = 7; p.relay_node = 0xFF;
    p.data.payload = text;
    p.data.payload_len = 233;         // the longest text a frame carries
    p.data.reply_id = 0x11223344;
    uint32_t before = api.fromNum();
    api.pushPacket(p);
    CHECK(api.fromNum() == before + 1);
    uint32_t route[7], back[7];                                // the longest the node answers with
    for (int i = 0; i < 7; i++) route[i] = back[i] = 0x80000000u + i;
    uint8_t rd[2 * 7 * 14 + 32];
    size_t rn = encodeRouteDiscovery(route, 7, back, 7, rd, sizeof(rd));
    CHECK(rn > 0);
    PhonePacket r = {};
    r.from = 1; r.to = 2; r.id = 3; r.rx_time = 1; r.hop_start = 1; r.has_decoded = true;
    r.data.portnum = PORT_TRACEROUTE; r.data.payload = rd; r.data.payload_len = rn; r.data.request_id = 4;
    before = api.fromNum();
    api.pushPacket(r);
    CHECK(api.fromNum() == before + 1);
}

TEST(mtphone_restart_midway, "UNIT-MTPHONE-018", "A want_config in the middle of the download starts it again from my_info") {
    FakeHost host;
    PhoneApi api(host);
    api.onToRadio(kWantConfig, sizeof(kWantConfig));
    uint8_t buf[kMaxFromRadio];
    for (int i = 0; i < 4; i++) CHECK(api.read(buf, sizeof(buf)) > 0);
    api.onToRadio(kWantLegacy, sizeof(kWantLegacy));
    std::vector<uint8_t> tags = drain(api);
    CHECK(tags.size() == 2 + 8 + 3 + 1 && tags[0] == 0x1A && tags.back() == 0x38);
}
