// helpers/McFirewall.h: the advert gap, the per-repeater frame ceiling and the "z.mc.fw.*" commands.
#include "test.h"
#include "McFirewall.h"
#include <mesh/Packet.h>

#include <string>

namespace {
std::string cmd(fw::Prefs &p, const fw::Firewall &f, const char *line, bool *changed = nullptr)
{
    char out[80];
    fw::CmdResult r = fw::handleCommand(p, f, fw::kMcRules, line, out, sizeof(out));
    if (changed) *changed = r.changed;
    return r.handled ? std::string(out) : std::string("(not handled)");
}
constexpr uint32_t kMin = 60000;
const uint8_t kA[3] = {0xa1, 0xa2, 0xa3};
const uint8_t kB[3] = {0xb1, 0xb2, 0xb3};
}

TEST(fw_off_by_default, "UNIT-FW-001", "With no limit set nothing is dropped, however often a key or a repeater is heard") {
    fw::Prefs p = {};
    fw::Firewall f;
    for (int i = 0; i < 300; i++) {
        f.advertKept(p, 7, i);
        CHECK(!f.advertInGap(p, 7, i));
        CHECK(!f.hopOverRate(p, kA, 2, i));
    }
    CHECK(f.counters().gap == 0 && f.counters().rate == 0);
}

TEST(fw_advert_gap, "UNIT-FW-002", "A kept advert closes its key's gap until the window passes; other keys are not affected") {
    fw::Prefs p = {};
    p.advert_gap_min = 10;
    fw::Firewall f;
    CHECK(!f.advertInGap(p, 7, 0));
    f.advertKept(p, 7, 0);
    CHECK(f.advertInGap(p, 7, 9 * kMin));
    CHECK(!f.advertInGap(p, 8, 9 * kMin));
    CHECK(!f.advertInGap(p, 7, 10 * kMin));
    f.advertKept(p, 7, 10 * kMin);
    CHECK(f.advertInGap(p, 7, 11 * kMin));
    CHECK(f.counters().gap == 2);
}

TEST(fw_advert_unverified, "UNIT-FW-003", "An advert nobody kept opens no gap, so a forged one cannot silence the real key") {
    fw::Prefs p = {};
    p.advert_gap_min = 10;
    fw::Firewall f;
    CHECK(!f.advertInGap(p, 7, 0));
    CHECK(!f.advertInGap(p, 7, 1000));
    CHECK(f.counters().gap == 0);
}

TEST(fw_advert_wrap, "UNIT-FW-004", "The gap survives the 32-bit millisecond wrap") {
    fw::Prefs p = {};
    p.advert_gap_min = 1;
    fw::Firewall f;
    const uint32_t t0 = 0xFFFFFFFFu - 30000;
    f.advertKept(p, 7, t0);
    CHECK(f.advertInGap(p, 7, t0 + 59000));
    CHECK(!f.advertInGap(p, 7, t0 + 60000));
}

TEST(fw_advert_slots, "UNIT-FW-005", "Sixteen keys are remembered; the seventeenth takes the oldest slot") {
    fw::Prefs p = {};
    p.advert_gap_min = 10;
    fw::Firewall f;
    for (uint32_t k = 1; k <= 17; k++) f.advertKept(p, k, 0);
    CHECK(!f.advertInGap(p, 1, 1000));
    CHECK(f.advertInGap(p, 2, 1000));
    CHECK(f.advertInGap(p, 17, 1000));
}

TEST(fw_rate, "UNIT-FW-006", "A repeater gets hop_rate frames a minute through, then none until the window passes") {
    fw::Prefs p = {};
    p.hop_rate = 3;
    fw::Firewall f;
    for (int i = 0; i < 3; i++) CHECK(!f.hopOverRate(p, kA, 2, i * 1000));
    CHECK(f.hopOverRate(p, kA, 2, 4000));
    CHECK(f.hopOverRate(p, kA, 2, 59000));
    CHECK(!f.hopOverRate(p, kA, 2, 60000));
    CHECK(f.counters().rate == 2);
}

TEST(fw_rate_per_repeater, "UNIT-FW-007", "Each repeater is counted on its own; a hash of another size is another repeater; a 1-byte hash names none") {
    fw::Prefs p = {};
    p.hop_rate = 1;
    fw::Firewall f;
    CHECK(!f.hopOverRate(p, kA, 2, 0));
    CHECK(f.hopOverRate(p, kA, 2, 1));
    CHECK(!f.hopOverRate(p, kB, 2, 1));
    CHECK(!f.hopOverRate(p, kA, 3, 1));
    for (int i = 0; i < 5; i++) CHECK(!f.hopOverRate(p, kA, 1, i));
}

TEST(fw_rate_slots, "UNIT-FW-008", "Eight repeaters are counted; the ninth takes the oldest slot and starts a fresh count") {
    fw::Prefs p = {};
    p.hop_rate = 1;
    fw::Firewall f;
    for (uint8_t i = 1; i <= 8; i++) { const uint8_t h[2] = {i, i}; CHECK(!f.hopOverRate(p, h, 2, 0)); }
    const uint8_t n[2] = {9, 9};
    CHECK(!f.hopOverRate(p, n, 2, 1));
    const uint8_t evicted[2] = {1, 1}, kept[2] = {3, 3};
    CHECK(!f.hopOverRate(p, evicted, 2, 2));
    CHECK(f.hopOverRate(p, kept, 2, 2));
}

TEST(fw_commands, "UNIT-FW-009", "z.mc.fw.gap and z.mc.fw.rate get and set 0 to 255; anything else is refused; stats is read only") {
    fw::Prefs p = {};
    fw::Firewall f;
    bool changed = false;
    CHECK(cmd(p, f, "get z.mc.fw.gap") == "> 0");
    CHECK(cmd(p, f, "set z.mc.fw.gap 30", &changed) == "OK" && p.advert_gap_min == 30 && changed);
    CHECK(cmd(p, f, "set z.mc.fw.gap 30", &changed) == "OK" && !changed);
    CHECK(cmd(p, f, "set z.mc.fw.rate 255", &changed) == "OK" && p.hop_rate == 255 && changed);
    CHECK(cmd(p, f, "get z.mc.fw.rate") == "> 255");
    CHECK(cmd(p, f, "set z.mc.fw.rate 256").rfind("Error", 0) == 0 && p.hop_rate == 255);
    CHECK(cmd(p, f, "set z.mc.fw.rate -1").rfind("Error", 0) == 0);
    CHECK(cmd(p, f, "set z.mc.fw.rate").rfind("Error", 0) == 0);
    CHECK(cmd(p, f, "set z.mc.fw.rate 1x").rfind("Error", 0) == 0);
    CHECK(cmd(p, f, "get z.mc.fw.stats") == "> gap 0, rate 0, drop 0");
    CHECK(cmd(p, f, "set z.mc.fw.stats 1").rfind("Error", 0) == 0);
    CHECK(cmd(p, f, "get z.mc.fw.other").rfind("Error", 0) == 0);
    CHECK(cmd(p, f, "get z.mc.ping.enable") == "(not handled)");
}

TEST(fw_drop_types, "UNIT-FW-010", "A rule drops its payload types and no others; CONTROL and unknown types always pass; each rule counts its own") {
    fw::Prefs p = {};
    fw::Firewall f;
    CHECK(!f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_ADVERT)));
    p.drop = (1u << fw::RULE_ADVERT) | (1u << fw::RULE_REQUEST) | (1u << fw::RULE_ACK);
    CHECK(f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_ADVERT)));
    CHECK(f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_REQ)) && f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_RESPONSE)) && f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_ANON_REQ)));
    CHECK(f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_ACK)) && f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_MULTIPART)));
    CHECK(!f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_TXT_MSG)) && !f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_GRP_TXT)) && !f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_GRP_DATA)));
    CHECK(!f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_PATH)) && !f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_TRACE)));
    p.drop = 0xFF;
    CHECK(!f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_CONTROL)) && !f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_RAW_CUSTOM)));
    CHECK(f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_TXT_MSG)) && f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_PATH)) && f.ruleDropped(p, fw::ruleOf(PAYLOAD_TYPE_TRACE)));
    CHECK(f.counters().rule[fw::RULE_ADVERT] == 1 && f.counters().rule[fw::RULE_REQUEST] == 3 && f.counters().rule[fw::RULE_ACK] == 2);
}

TEST(fw_drop_command, "UNIT-FW-011", "z.mc.fw.drop turns each rule on or off, lists the ones on with their counts, and none clears them") {
    fw::Prefs p = {};
    fw::Firewall f;
    bool changed = false;
    CHECK(cmd(p, f, "get z.mc.fw.drop") == "> none");
    CHECK(cmd(p, f, "set z.mc.fw.drop advert on", &changed) == "OK" && p.drop == (1u << fw::RULE_ADVERT) && changed);
    CHECK(cmd(p, f, "set z.mc.fw.drop advert 1", &changed) == "OK" && !changed);
    CHECK(cmd(p, f, "set z.mc.fw.drop trace 1", &changed) == "OK" && changed);
    CHECK(cmd(p, f, "get z.mc.fw.drop") == "> advert 0 trace 0");
    CHECK(cmd(p, f, "set z.mc.fw.drop advert off", &changed) == "OK" && p.drop == (1u << fw::RULE_TRACE) && changed);
    CHECK(cmd(p, f, "set z.mc.fw.drop bogus on").rfind("Error", 0) == 0);
    CHECK(cmd(p, f, "set z.mc.fw.drop advert maybe").rfind("Error", 0) == 0);
    CHECK(cmd(p, f, "set z.mc.fw.drop advert").rfind("Error", 0) == 0);
    CHECK(cmd(p, f, "set z.mc.fw.drop none", &changed) == "OK" && p.drop == 0 && changed);
}

TEST(fw_screened, "UNIT-FW-015", "A copy of a frame already screened gets its verdict again and is not counted; the ring keeps 32 frames") {
    fw::Firewall f;
    bool dropped = true;
    CHECK(!f.screened(7, dropped));
    f.noteScreened(7, true);
    f.noteScreened(8, false);
    CHECK(f.screened(7, dropped) && dropped);
    CHECK(f.screened(8, dropped) && !dropped);
    CHECK(!f.screened(9, dropped));
    f.noteScreened(0, true);
    CHECK(!f.screened(0, dropped));
    for (uint32_t h = 100; h < 132; h++) f.noteScreened(h, false);
    CHECK(!f.screened(7, dropped) && f.screened(131, dropped));
    CHECK(f.counters().gap == 0 && f.counters().rate == 0);
}
