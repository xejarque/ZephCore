// prefs.json (helpers/PrefsJson.cpp over upstream's ConfigSerializer).
#include "test.h"
#include "PrefsJson.h"
#include "PrefsCodec.h"

#include <cstring>
#include <string>

namespace {

class MemStream : public Stream {
public:
    std::string data;
    size_t pos = 0;
    explicit MemStream(std::string s = "") : data(std::move(s)) {}
    size_t write(uint8_t c) override { data.push_back((char)c); return 1; }
    int available() override { return (int)(data.size() - pos); }
    int read() override { return pos < data.size() ? (uint8_t)data[pos++] : -1; }
    int peek() override { return pos < data.size() ? (uint8_t)data[pos] : -1; }
};

NodePrefs defaults() { NodePrefs p; initNodePrefs(&p); return p; }

// Same field values as the codec tests: every serialized field off its default.
NodePrefs sample() {
    NodePrefs p = defaults();
    p.airtime_factor = 3.5f; strcpy(p.node_name, "Node-Test");
    p.node_lat = 47.5; p.node_lon = 19.04;
    strcpy(p.password, "adminpw"); strcpy(p.guest_password, "guestpw");
    p.freq = 869.525f; p.sf = 9; p.cr = 6; p.bw = 125.0f; p.tx_power_dbm = 14;
    p.disable_fwd = 1; p.advert_interval = 30; p.flood_advert_interval = 12;
    p.rx_delay_base = 1.5f; p.tx_delay_factor = 0.7f; p.direct_tx_delay_factor = 0.4f;
    p.backoff_multiplier = 0.6f; p.allow_read_only = 1; p.multi_acks = 1;
    p.flood_max = 20; p.flood_max_unscoped = 10; p.flood_max_advert = 4;
    p.interference_threshold = 7; p.leds_disabled = 1; p.leds_radio_mode = LEDS_RADIO_RX;
    p.leds_hb_mode = LEDS_HB_UNREAD; p.led_brightness = 42; p.powersaving_enabled = 1; p.gps_enabled = 1;
    p.gps_interval = 600; p.advert_loc_policy = ADVERT_LOC_SHARE; p.discovery_mod_timestamp = 123456;
    p.adc_multiplier = 1.25f; strcpy(p.owner_info, "owner info");
    p.rx_boost = 0; p.fem_rxgain = 0; p.rx_duty_cycle = 1;
    p.meshtimesync = 1; p.cad_auto = 0; p.cad_offset = -3; p.probe_interval = 30; p.cad_busycap = 40;
    p.extra_sf[0] = 10; p.extra_sf[1] = 11; p.extra_sf[2] = 0;
    p.display_rotate = 1; p.input_rotate = 1; p.tz_offset = -5; p.cad_base = 21;
    p.manual_add_contacts = 1; p.telemetry_mode_base = 1; p.telemetry_mode_loc = 2;
    p.telemetry_mode_env = 1; p.ble_pin = 123456; p.buzzer_quiet = 1; p.autoadd_config = 0x1E;
    p.client_repeat = 1; p.path_hash_mode = 2; p.autoadd_max_hops = 5; p.loop_detect = 3;
    strcpy(p.default_scope_name, "#hu");
    for (int i = 0; i < 16; i++) p.default_scope_key[i] = (uint8_t)(i + 1);
    p.ble_disabled = 1; p.display_brightness = 50; p.wake_on_msg = 0; p.screen_off_secs = 60;
    p.auto_shutdown_mv = 3300; p.v_contact_enabled = 0; p.v_battery_alert_mv = 3500;
    p.v_contact_flags = 0x81; p.wifi_enabled = 0; p.offgrid_home_freq = 869.618f;
    strcpy(p.wifi_ssid, "MyNet"); strcpy(p.wifi_pwd, "secret");
    return p;
}

std::string companionBytes(const NodePrefs &p) {
    uint8_t b[COMPANION_PREFS_SIZE];
    companionPrefsEncode(p, b, sizeof(b));
    return std::string((const char *)b, sizeof(b));
}
std::string serverBytes(const NodePrefs &p) {
    uint8_t b[SERVER_PREFS_SIZE];
    serverPrefsEncode(p, b, sizeof(b));
    return std::string((const char *)b, sizeof(b));
}

}  // namespace

TEST(pjson_companion_roundtrip, "UNIT-PJSON-001", "Companion prefs survive prefs.json field for field") {
    NodePrefs p = sample();
    MemStream s;
    CHECK(companionPrefsToJson(p, s));
    NodePrefs q = defaults();
    CHECK(companionPrefsFromJson(q, s));
    // Every field of the binary layout, as a legacy file migrated to JSON.
    CHECK(companionBytes(q) == companionBytes(p));
    // prefs.json only: not in the binary layout.
    CHECK(q.led_brightness == 42);
    CHECK(q.offgrid_home_freq == 869.618f);
}

TEST(pjson_server_roundtrip, "UNIT-PJSON-002", "Server prefs survive prefs.json field for field") {
    NodePrefs p = sample();
    MemStream s;
    CHECK(serverPrefsToJson(p, s));
    NodePrefs q = defaults();
    CHECK(serverPrefsFromJson(q, s));
    CHECK(serverBytes(q) == serverBytes(p));
    CHECK(q.led_brightness == 42);
}

TEST(pjson_upstream_file, "UNIT-PJSON-003", "A companion prefs.json as upstream v1.17 writes it loads") {
    MemStream s("{name:\"Up\",lat:1.5,lon:2.5,radio:{freq:869.6180,bw:62.5000,sf:8,cr:5,cad:1,"
                "int_thr:0,rxgain:1,fem_rxgain:0,fem_txgain:0,tx:20,af:9.0000,rxdelay:0,"
                "f_txdelay:0.5000,d_txdelay:0.3000,agc_int:0,hash_mode:1,multi_ack:0},"
                "gps:{en:0,int:300,adv_loc:0},repeat:{disable:0},comp:{auto_max:3,defs_nm:\"\","
                "defs_key:\"000102030405060708090A0B0C0D0E0F\",pin:0,buzz_q:1,vibe_q:0,auto_add:0,"
                "man_add:0,tel_base:0,tel_loc:0,tel_env:0,tz_offset:2},custom:{},"
                "wifi:{ssid:\"x\",pwd:\"y\",enabled:1}}");
    NodePrefs q = defaults();
    CHECK(companionPrefsFromJson(q, s));
    CHECK(std::strcmp(q.node_name, "Up") == 0);
    CHECK(q.sf == 8 && q.tx_power_dbm == 20 && q.freq == 869.618f && q.bw == 62.5f);
    CHECK(q.client_repeat == 1);
    CHECK(q.autoadd_max_hops == 3 && q.buzzer_quiet == 1 && q.tz_offset == 2);
    CHECK(q.default_scope_key[15] == 0x0F);
    CHECK(std::strcmp(q.wifi_ssid, "x") == 0 && std::strcmp(q.wifi_pwd, "y") == 0);
    // No "zc" object: ZephCore's own fields keep their defaults.
    NodePrefs d = defaults();
    CHECK(q.cad_auto == d.cad_auto && q.probe_interval == d.probe_interval && q.wake_on_msg == d.wake_on_msg);
}

TEST(pjson_parse_errors, "UNIT-PJSON-004", "A broken prefs.json leaves prefs untouched, an empty object changes nothing") {
    const char *broken[] = { "{name:\"x\"", "{radio:{sf:9}", "name:\"x\"}", "{na-me:1}" };
    for (const char *text : broken) {
        NodePrefs q = sample();
        std::string before = companionBytes(q);
        MemStream s(text);
        if (companionPrefsFromJson(q, s)) throw std::runtime_error(std::string("accepted: ") + text);
        CHECK(companionBytes(q) == before);
    }
    NodePrefs q = sample();
    std::string before = serverBytes(q);
    MemStream s("{}");
    CHECK(serverPrefsFromJson(q, s));
    CHECK(serverBytes(q) == before);
}

TEST(pjson_strings, "UNIT-PJSON-005", "Quotes, backslashes, newlines and UTF-8 in strings round trip") {
    NodePrefs p = sample();
    strcpy(p.node_name, "A \"q\" \\b \xF0\x9F\x93\xA1");
    strcpy(p.owner_info, "line1\nline2\r\"x\"");
    strcpy(p.wifi_pwd, "p\"w\\d");
    MemStream s;
    CHECK(serverPrefsToJson(p, s));
    NodePrefs q = defaults();
    CHECK(serverPrefsFromJson(q, s));
    CHECK(std::strcmp(q.node_name, p.node_name) == 0);
    CHECK(std::strcmp(q.owner_info, p.owner_info) == 0);
    MemStream c;
    CHECK(companionPrefsToJson(p, c));
    NodePrefs r = defaults();
    CHECK(companionPrefsFromJson(r, c));
    CHECK(std::strcmp(r.wifi_pwd, p.wifi_pwd) == 0);
}

TEST(pjson_radio_floats, "UNIT-PJSON-006", "Frequencies and bandwidths survive the 4-decimal float encoding exactly") {
    const float freqs[] = { 433.175f, 433.875f, 868.1f, 869.4125f, 869.525f, 869.618f, 902.125f,
                            915.0f, 916.575f, 918.25f, 923.2f, 2400.0f, 2450.12f };
    const float bws[] = { 7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f };
    for (float f : freqs) {
        for (float bw : bws) {
            NodePrefs p = sample();
            p.freq = f; p.bw = bw;
            MemStream s;
            CHECK(companionPrefsToJson(p, s));
            NodePrefs q = defaults();
            CHECK(companionPrefsFromJson(q, s));
            if (q.freq != f || q.bw != bw)
                throw std::runtime_error("freq " + std::to_string(f) + " bw " + std::to_string(bw));
        }
    }
}

TEST(pjson_bad_radio, "UNIT-PJSON-007", "An implausible radio preset in prefs.json falls back to the defaults") {
    MemStream s("{name:\"Z\",radio:{freq:5000,sf:9,tx:14},gps:{int:900}}");
    NodePrefs q = defaults();
    CHECK(serverPrefsFromJson(q, s));
    CHECK(q.freq == mesh::LoRaConfig::FREQ_MHZ);
    CHECK(q.sf == mesh::LoRaConfig::SPREADING_FACTOR);
    CHECK(q.tx_power_dbm == mesh::LoRaConfig::TX_POWER_DBM);
    CHECK(std::strcmp(q.node_name, "Z") == 0);
    CHECK(q.gps_interval == 900);
}

TEST(pjson_server_gps_upgrade, "UNIT-PJSON-008", "Server gps.en: a file without zc.gps_set is upgraded to on once, then honoured") {
    // Written before slice 10, when servers ran the GPS whatever gps.en said.
    MemStream old_file("{name:\"R\",gps:{en:0,int:172800}}");
    NodePrefs q = defaults();
    CHECK(serverPrefsFromJson(q, old_file));
    CHECK(q.gps_enabled == 1 && q.gps_enabled_set == 1);

    // `gps off` after the upgrade: saved with the marker, so it holds.
    q.gps_enabled = 0;
    MemStream s;
    CHECK(serverPrefsToJson(q, s));
    NodePrefs r = defaults();
    r.gps_enabled = 1;
    CHECK(serverPrefsFromJson(r, s));
    CHECK(r.gps_enabled == 0 && r.gps_enabled_set == 1);

    // The companion has no such history: its gps.en is taken as written.
    MemStream comp("{name:\"C\",gps:{en:0}}");
    NodePrefs c = defaults();
    c.gps_enabled = 1;
    CHECK(companionPrefsFromJson(c, comp));
    CHECK(c.gps_enabled == 0);
}

TEST(pjson_tz_fraction, "UNIT-PJSON-009", "A quarter-hour timezone survives prefs.json; upstream's float tz_offset loads") {
    NodePrefs p = sample();
    p.tz_offset = 5.75f;
    MemStream s;
    CHECK(companionPrefsToJson(p, s));
    NodePrefs q = defaults();
    CHECK(companionPrefsFromJson(q, s));
    CHECK(q.tz_offset == 5.75f);
    p.tz_offset = -9.5f;
    MemStream t;
    CHECK(serverPrefsToJson(p, t));
    q = defaults();
    CHECK(serverPrefsFromJson(q, t));
    CHECK(q.tz_offset == -9.5f);
    // Upstream v1.18+ writes the float with four decimals.
    MemStream u("{comp:{tz_offset:5.5000}}");
    q = defaults();
    CHECK(companionPrefsFromJson(q, u));
    CHECK(q.tz_offset == 5.5f);
}

TEST(pjson_server_mt, "UNIT-PJSON-010", "The repeater's Meshtastic settings survive prefs.json; a file without mt{} keeps the defaults") {
    NodePrefs p = defaults();
    p.mt.enable = 0;
    p.mt.role = mt::ROLE_ROUTER;
    p.mt.hops = 5;
    p.mt.relay = mt::RELAY_ESSENTIAL;
    p.mt.relay_opaque = 0;
    p.mt.zerohop = mt::ZEROHOP_AUTO;
    p.mt.zerohop_nodes[1] = 0xA1B2C3D4u;
    p.mt.nodeinfo_s = 3600;
    p.mt.telemetry_s = 900;
    strcpy(p.mt.channel, "Mesh");
    p.mt.psk_len = 16;
    for (int i = 0; i < 16; i++) p.mt.psk[i] = (uint8_t)(0x30 + i);
    p.mt.defaults_rev = 2;
    MemStream s;
    CHECK(serverPrefsToJson(p, s));
    NodePrefs q = defaults();
    CHECK(serverPrefsFromJson(q, s));
    CHECK(q.mt.enable == 0 && q.mt.role == mt::ROLE_ROUTER && q.mt.hops == 5);
    CHECK(q.mt.relay == mt::RELAY_ESSENTIAL && q.mt.relay_opaque == 0);
    CHECK(q.mt.zerohop == mt::ZEROHOP_AUTO && q.mt.zerohop_nodes[0] == 0 && q.mt.zerohop_nodes[1] == 0xA1B2C3D4u);
    CHECK(q.mt.nodeinfo_s == 3600 && q.mt.telemetry_s == 900);
    CHECK(std::string(q.mt.channel) == "Mesh" && q.mt.psk_len == 16 && q.mt.psk[15] == 0x3F);
    CHECK(q.mt.defaults_rev == 2);

    MemStream old("{name:\"R\",lat:0,lon:0,radio:{freq:869.6180,bw:62.5000,sf:8,cr:5,tx:20}}");
    NodePrefs d = defaults();
    CHECK(serverPrefsFromJson(d, old));
    CHECK(d.mt.enable == 1 && d.mt.role == mt::ROLE_CLIENT_BASE && d.mt.relay == mt::RELAY_ESSENTIAL);
    CHECK(d.mt.zerohop == mt::ZEROHOP_OFF && d.mt.defaults_rev == 0);
    CHECK(std::string(d.mt.channel) == "SFNarrow" && d.mt.psk_len == 1 && d.mt.psk[0] == 1);
}

TEST(pjson_ping, "UNIT-PJSON-011", "The ping bot's settings survive prefs.json in both roles; an invalid command comes back off") {
    NodePrefs p = defaults();
    CHECK(p.ping.mc.enable == 0 && p.ping.mt.enable == 0 && p.ping.snr == 1 && p.ping.rssi == 0);
    ping::setTrigger(p.ping.mc, "/ping");
    p.ping.mc.enable = 1;
    ping::setTrigger(p.ping.mt, "!eco");
    p.ping.rssi = 1;
    p.ping.snr = 0;
    p.ping.ignore_direct = 1;
    p.ping.ignore_remote = 1;
    MemStream s;
    CHECK(serverPrefsToJson(p, s));
    NodePrefs q = defaults();
    CHECK(serverPrefsFromJson(q, s));
    CHECK(q.ping.mc.enable == 1 && std::string(q.ping.mc.trigger) == "/ping");
    CHECK(q.ping.mt.enable == 0 && std::string(q.ping.mt.trigger) == "!eco");
    CHECK(q.ping.rssi == 1 && q.ping.snr == 0 && q.ping.ignore_direct == 1 && q.ping.ignore_remote == 1);

    MemStream c;
    CHECK(companionPrefsToJson(p, c));
    NodePrefs r = defaults();
    CHECK(companionPrefsFromJson(r, c));
    CHECK(r.ping.mc.enable == 1 && std::string(r.ping.mt.trigger) == "!eco" && r.ping.snr == 0);

    MemStream bad("{zc:{ping_mc_en:1,ping_mc_trig:\"two words\",ping_snr:7}}");
    NodePrefs b = defaults();
    CHECK(serverPrefsFromJson(b, bad));
    sanitizeNodePrefs(&b);
    CHECK(b.ping.mc.enable == 0 && std::string(b.ping.mc.trigger) == "" && b.ping.snr == 1);
}

TEST(pjson_mt_channels, "UNIT-PJSON-012", "A companion's Meshtastic channels survive prefs.json; a file without them has none") {
    NodePrefs p = defaults();
    CHECK(mt::addChannel(p.mt, "SFNarrow", (const uint8_t*)"\x01", 1) == mt::ADD_OK);
    uint8_t key[16];
    for (int i = 0; i < 16; i++) key[i] = (uint8_t)(0x40 + i);
    CHECK(mt::addChannel(p.mt, "Mine", key, 16) == mt::ADD_OK);
    MemStream s;
    CHECK(companionPrefsToJson(p, s));
    NodePrefs q = defaults();
    CHECK(companionPrefsFromJson(q, s));
    CHECK(std::string(q.mt.channels[0].name) == "SFNarrow" && q.mt.channels[0].psk_len == 1 && q.mt.channels[0].psk[0] == 1);
    CHECK(std::string(q.mt.channels[1].name) == "Mine" && q.mt.channels[1].psk_len == 16 && q.mt.channels[1].psk[15] == 0x4F);
    CHECK(q.mt.channels[2].name[0] == '\0');
    CHECK(s.data.find("c0:") != std::string::npos && s.data.find("c1:") != std::string::npos);
    CHECK(s.data.find("c2:") == std::string::npos && s.data.find("c7:") == std::string::npos);   // the empty slots are not written

    MemStream old("{name:\"C\",lat:0,lon:0,mt:{en:1}}");
    NodePrefs d = defaults();
    CHECK(companionPrefsFromJson(d, old));
    CHECK(d.mt.channels[0].name[0] == '\0');
}

TEST(pjson_fw, "UNIT-PJSON-013", "The MeshCore receive limits survive prefs.json; a file without them leaves no limit") {
    NodePrefs p = defaults();
    CHECK(p.fw.advert_gap_min == 0 && p.fw.hop_rate == 0);
    p.fw.advert_gap_min = 30;
    p.fw.hop_rate = 200;
    p.fw.drop = 0x41;
    p.mtfw.drop = 0x12;
    p.mtfw.advert_gap_min = 5;
    p.mtfw.hop_rate = 9;
    MemStream s;
    CHECK(serverPrefsToJson(p, s));
    NodePrefs q = defaults();
    CHECK(serverPrefsFromJson(q, s));
    CHECK(q.fw.advert_gap_min == 30 && q.fw.hop_rate == 200 && q.fw.drop == 0x41);
    CHECK(q.mtfw.drop == 0x12 && q.mtfw.advert_gap_min == 5 && q.mtfw.hop_rate == 9);

    MemStream empty;
    CHECK(serverPrefsToJson(defaults(), empty));
    NodePrefs r = defaults();
    CHECK(serverPrefsFromJson(r, empty));
    CHECK(r.fw.advert_gap_min == 0 && r.fw.hop_rate == 0 && r.mtfw.drop == 0 && r.mtfw.hop_rate == 0);
}

TEST(pjson_nan_radio, "UNIT-PJSON-015", "A radio preset of nan in prefs.json falls back to the defaults") {
    MemStream s("{name:\"Z\",radio:{freq:nan,bw:nan,sf:9,cr:6,tx:14}}");
    NodePrefs q = defaults();
    CHECK(serverPrefsFromJson(q, s));
    CHECK(q.freq == mesh::LoRaConfig::FREQ_MHZ);
    CHECK(q.bw == mesh::LoRaConfig::BANDWIDTH_KHZ);
    CHECK(q.sf == mesh::LoRaConfig::SPREADING_FACTOR);
    CHECK(q.cr == mesh::LoRaConfig::CODING_RATE);
    CHECK(q.tx_power_dbm == mesh::LoRaConfig::TX_POWER_DBM);
    CHECK(std::strcmp(q.node_name, "Z") == 0);
}

TEST(pjson_server_buzzer, "UNIT-PJSON-016", "A server's buzzer mode survives prefs.json; a file without the key keeps the default") {
    NodePrefs p = defaults();
    p.buzzer_quiet = 1;   // silent, as `set buzzer 0` stores it
    MemStream s;
    CHECK(serverPrefsToJson(p, s));
    NodePrefs q = defaults();
    CHECK(serverPrefsFromJson(q, s));
    CHECK(q.buzzer_quiet == 1);

    // 0 (audible) is a choice too, not a missing key.
    p.buzzer_quiet = 0;
    MemStream t;
    CHECK(serverPrefsToJson(p, t));
    NodePrefs r = defaults();
    r.buzzer_quiet = 1;
    CHECK(serverPrefsFromJson(r, t));
    CHECK(r.buzzer_quiet == 0);
}

TEST(pjson_server_autoshutdown, "UNIT-PJSON-014", "A repeater's auto-shutdown threshold survives prefs.json; a file without it keeps the default") {
    NodePrefs p = defaults();
    p.auto_shutdown_mv = 3400;
    MemStream s;
    CHECK(serverPrefsToJson(p, s));
    NodePrefs q = defaults();
    CHECK(serverPrefsFromJson(q, s));
    CHECK(q.auto_shutdown_mv == 3400);

    // 0 (off) is a choice too, not a missing value.
    p.auto_shutdown_mv = 0;
    MemStream off;
    CHECK(serverPrefsToJson(p, off));
    NodePrefs r = defaults();
    CHECK(serverPrefsFromJson(r, off));
    CHECK(r.auto_shutdown_mv == 0);

    // A file written before the keys existed leaves the default alone.
    MemStream old_file("{name:\"R\"}");
    NodePrefs d = defaults();
    uint16_t def_mv = d.auto_shutdown_mv;
    CHECK(serverPrefsFromJson(d, old_file));
    CHECK(d.auto_shutdown_mv == def_mv);
}

TEST(pjson_damaged, "UNIT-PJSON-017", "A server prefs.json cut at any byte, zeroed or reading as erased flash is refused and changes nothing") {
    MemStream whole;
    CHECK(serverPrefsToJson(sample(), whole));
    const std::string json = whole.data;
    const NodePrefs before = defaults();
    std::vector<std::string> damaged = {std::string(json.size(), '\0'), std::string(json.size(), '\xff')};
    for (size_t length = 1; length < json.size(); length++) damaged.push_back(json.substr(0, length));
    for (const std::string &bytes : damaged) {
        NodePrefs p = before;
        MemStream in(bytes);
        CHECK(!serverPrefsFromJson(p, in));
        CHECK(serverBytes(p) == serverBytes(before));
    }
}

