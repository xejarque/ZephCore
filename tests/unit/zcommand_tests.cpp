// helpers/ZCommand.h: the on/off value of the "z." commands and the help that walks the "z." keys as a tree.
#include "test.h"
#include "ZCommand.h"

#include <string>

namespace {
std::string help(const char *command, bool mt = true)
{
    char out[161] = "";
    return zcmd::helpReply(command, mt, out, sizeof(out)) ? std::string(out) : std::string("<not mine>");
}

std::string root(bool mt = true)
{
    char out[256] = "";
    zcmd::appendGroups(out, sizeof(out), 0, "", mt);
    return out;
}

// Whether line lists word (" name") as a whole word: followed by a space, a "/" or the end.
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
}

TEST(zcmd_on_off, "UNIT-ZCMD-001", "on and 1 are on, off and 0 are off, anything else is not a value") {
    uint8_t b = 9;
    CHECK(zcmd::parseOnOff("on", b) && b == 1);
    CHECK(zcmd::parseOnOff("1", b) && b == 1);
    CHECK(zcmd::parseOnOff("off", b) && b == 0);
    CHECK(zcmd::parseOnOff("0", b) && b == 0);
    b = 9;
    CHECK(!zcmd::parseOnOff("", b) && !zcmd::parseOnOff("2", b) && !zcmd::parseOnOff("on ", b) && !zcmd::parseOnOff("ON", b) && b == 9);
}

TEST(zcmd_help_key, "UNIT-ZCMD-002", "help at the end of get or set gives the usage of that key") {
    CHECK(help("get z.mt.enable help").find("> z.mt.enable: get|set on|off|1|0") == 0);
    CHECK(help("set z.mt.relay help").find("off|essential|all") != std::string::npos);
    CHECK(help("get z.mc.ping.enable   help ") == help("set z.mc.ping.enable help"));
    CHECK(help("get z.ping.snr help").find("SNR") != std::string::npos);
    CHECK(help("get z.tx.stalls help").find("> z.tx.stalls:") == 0);
    CHECK(help("get z.mt.neighbors help").find("Meshtastic") != std::string::npos);
}

TEST(zcmd_help_group, "UNIT-ZCMD-003", "help on a group lists its children, whole words only, within the reply") {
    CHECK(help("get z.mt help").find("> z.mt: enable role relay/ hops zerohop/") == 0);
    CHECK(help("get z.mt help").find(" ping/") != std::string::npos);
    CHECK(help("get z.ping help") == "> z.ping: snr rssi ignore/");
    char out[40] = "";
    CHECK(zcmd::helpReply("get z.mt help", true, out, sizeof(out)));
    CHECK(strlen(out) < sizeof(out) && out[strlen(out) - 1] != ':' && strncmp(out, "> z.mt: enable role", 19) == 0 && out[strlen(out) - 1] != ' ');
}

TEST(zcmd_help_other, "UNIT-ZCMD-004", "what is not a help request, or names no key, goes on to its own handler") {
    CHECK(help("get z.mt.enable") == "<not mine>");
    CHECK(help("get z.mt.nope help") == "<not mine>");
    CHECK(help("get z.mt.enable helpme") == "<not mine>");
    CHECK(help("set z.mt.channel.add Foo help") == "<not mine>");  // a channel key
    CHECK(help("get dutycycle help") == "<not mine>");
    CHECK(help("get z. help") == "<not mine>");
}

TEST(zcmd_help_no_meshtastic, "UNIT-ZCMD-005", "a build without Meshtastic has no z.mt help") {
    CHECK(help("get z.mt.enable help", false) == "<not mine>");
    CHECK(help("get z.mt help", false) == "<not mine>");
    CHECK(help("get z.mc help", false).find("> z.mc: neighbors") == 0);
}

TEST(zcmd_help_free_text, "UNIT-ZCMD-006", "set <key> help on a key that takes any text is that value; get help still gives the usage; a refusal leaves the reply alone") {
    CHECK(help("set z.mc.ping.trigger help") == "<not mine>");
    CHECK(help("set z.mt.channel help") == "<not mine>");
    CHECK(help("get z.mc.ping.trigger help").find("> z.mc.ping.trigger:") == 0);
    CHECK(help("set z.mt.hops help").find("0-7") != std::string::npos);   // a fixed-value key keeps its usage
    char out[40] = "sentinel";
    CHECK(!zcmd::helpReply("get z.nothing.here help", true, out, sizeof(out)) && strcmp(out, "sentinel") == 0);
    CHECK(!zcmd::helpReply("set z.mc.ping.trigger help", true, out, sizeof(out)) && strcmp(out, "sentinel") == 0);
}

TEST(zcmd_parse, "UNIT-ZCMD-007", "A z. line is taken apart once: get or set, the protocol, the key and its value; anything else is not one") {
    auto p = zcmd::parse("get z.mc.fw.rate");
    CHECK(p.ok && p.get && p.proto == 'c' && std::string(p.key) == "fw.rate");
    p = zcmd::parse("set z.mt.ping.enable on /ping");
    CHECK(p.ok && !p.get && p.proto == 't' && std::string(p.key) == "ping.enable on /ping");
    p = zcmd::parse("set z.ping.snr off");
    CHECK(p.ok && p.proto == 0 && std::string(p.key) == "ping.snr off");
    p = zcmd::parse("get z.info");
    CHECK(p.ok && p.get && p.proto == 0 && std::string(p.key) == "info");
    p = zcmd::parse("get z.mx.fw.rate");
    CHECK(p.ok && p.proto == 0 && std::string(p.key) == "mx.fw.rate");
    for (const char *line : {"get zz.mc.fw.rate", "get z", "set x.mc", "", "gets z.mc.fw.rate", "get  z.mc.fw.rate"}) CHECK(!zcmd::parse(line).ok);
    char name[8];
    CHECK(std::string(zcmd::splitValue("rate 12", name, sizeof(name))) == "12" && std::string(name) == "rate");
    CHECK(std::string(zcmd::splitValue("rate", name, sizeof(name))) == "" && std::string(name) == "rate");
    CHECK(std::string(zcmd::splitValue("a b c", name, sizeof(name))) == "b c" && std::string(name) == "a");
    CHECK(std::string(zcmd::splitValue("toolongname 5", name, sizeof(name))) == "" && std::string(name) == "toolong");
}

TEST(zcmd_help_root, "UNIT-ZCMD-008", "help lists the groups of z., with / after one that has more under it; a build without Meshtastic has no mt/") {
    CHECK(root() == "z. groups: mt/ mc/ ping/ autoshutdown info stats rescue tx/");
    CHECK(root(false) == "z. groups: mc/ ping/ autoshutdown info stats rescue tx/");
    CHECK(root().size() <= 60);  // the companion's extras take 106 bytes of its 175-byte frame
}

TEST(zcmd_help_tree, "UNIT-ZCMD-009", "from a group, a / marks a child with more under it; a key with more under it lists them after its usage") {
    CHECK(help("get z.mt.relay help").find("> z.mt.relay: get|set off|essential|all") == 0);
    CHECK(help("get z.mt.relay help").find(" | more: opaque") != std::string::npos);
    CHECK(help("get z.mt.zerohop help").find(" | more: add remove clear") != std::string::npos);
    CHECK(help("get z.mt.zerohop.add help").find("8 hex digits") != std::string::npos);
    CHECK(help("get z.mt.fw help") == "> z.mt.fw: drop gap rate stats");
    CHECK(help("get z.mt.ping help") == "> z.mt.ping: enable trigger status");
    CHECK(help("get z.mc.fw help") == "> z.mc.fw: drop gap rate stats");
    CHECK(help("get z.ping.ignore help") == "> z.ping.ignore: direct remote");
}

TEST(zcmd_help_default, "UNIT-ZCMD-011", "the usage of z.mt.hops names the default the firmware boots with, not the Zephtacore variant's") {
    // zephcore/Kconfig: ZEPHTACORE_MT_HOP_LIMIT default 3. The Zephtacore build
    // overrides it to 7 in boards/common/zephtacore.conf, so the usage names the
    // default and says where the 7 comes from.
    const std::string usage = help("get z.mt.hops help");
    CHECK(usage.find("default 3") != std::string::npos);
    CHECK(usage.find("default 7") == std::string::npos);
    CHECK(usage.find("Zephtacore") != std::string::npos);
}

TEST(zcmd_help_reach, "UNIT-ZCMD-010", "every key is reached from help through the groups above it, and every reply fits the remote reply") {
    for (const zcmd::Help &h : zcmd::kHelp) {
        if (!zcmd::helpListed(h, true)) {
            continue;
        }
        const std::string key = h.key;
        std::string group;
        size_t start = 0;

        for (;;) {
            const size_t dot = key.find('.', start);
            const std::string child = key.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            const std::string listing = group.empty() ? root() : help(("get z." + group + " help").c_str());

            CHECK(listsWord(listing, " " + child));
            CHECK(listing.size() < 158);
            if (dot == std::string::npos) {
                break;
            }
            group = key.substr(0, dot);
            start = dot + 1;
        }
        const std::string usage = help(("get z." + key + " help").c_str());
        CHECK(usage.size() < 158);
    }
}
