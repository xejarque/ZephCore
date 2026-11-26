// helpers/RegionPages.h: "get z.mc.regions [page]".
#include "test.h"
#include "RegionPages.h"

#include <string>
#include <vector>

namespace {
std::string page(const std::string &exported, size_t cap, size_t number)
{
    std::vector<char> out(cap, 'x');
    region_pages::Pager pager(out.data(), cap, number);
    for (char c : exported) pager.put(c);
    pager.finish();
    return std::string(out.data());
}

/* A map of n regions, as RegionMap::exportTo writes it. */
std::string map(int n)
{
    std::string out = "*^ F\n";
    for (int i = 0; i < n; i++) out += std::string(1 + i % 3, ' ') + "region-with-a-long-name-" + std::to_string(i) + " F\n";
    return out;
}
}

TEST(region_pages_args, "UNIT-REGPAGE-001", "the page is 1 without one, 0 when it is not a plain number, and other commands are not ours") {
    size_t number = 9;
    CHECK(region_pages::parseArgs("get z.mc.regions", number) && number == 1);
    CHECK(region_pages::parseArgs("get z.mc.regions 12", number) && number == 12);
    for (const char *line : {"get z.mc.regions x", "get z.mc.regions 1x", "get z.mc.regions 12345", "get z.mc.regions -1"}) {
        CHECK(region_pages::parseArgs(line, number) && number == 0);
    }
    for (const char *line : {"get z.mc.regionsx", "set z.mc.regions", "region", "get z.mc.relays"}) {
        CHECK(!region_pages::parseArgs(line, number));
    }
}

TEST(region_pages_small, "UNIT-REGPAGE-002", "a map that fits is one page with its lines unchanged") {
    CHECK(page("*^ F\n es F\n  cat\n", 158, 1) == "Regions 1/1\n*^ F\n es F\n  cat");
    CHECK(page("*^ F\n", 158, 2) == "Regions: page 2 of 1");
    CHECK(page("*^ F\n", 158, 0) == "Regions: page 0 of 1");
}

TEST(region_pages_whole, "UNIT-REGPAGE-003", "the pages of a full map fit the reply and give back every line once") {
    const std::string exported = map(32);
    for (size_t cap : {size_t(158), size_t(256)}) {
        std::string joined;
        const std::string first = page(exported, cap, 1);
        const int pages = std::stoi(first.substr(first.find('/') + 1));
        CHECK(pages > 1);
        for (int i = 1; i <= pages; i++) {
            const std::string reply = page(exported, cap, i);
            CHECK(reply.size() < cap);
            CHECK(reply.find("Regions " + std::to_string(i) + "/" + std::to_string(pages) + "\n") == 0);
            joined += reply.substr(reply.find('\n') + 1) + "\n";
        }
        CHECK(joined == exported);
    }
}
