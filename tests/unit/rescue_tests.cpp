// helpers/RescuePolicy.h: the rescue copy of a repeater or room server (ADR 0021).
#include "test.h"
#include "RescuePolicy.h"

#include <cstring>
#include <map>
#include <set>
#include <string>

namespace {
using rescue::Item;
using rescue::State;

struct FakeStore {
    std::map<std::string, std::string> keys;
    int writes_left = -1;       // -1: unlimited; 0: power is gone, every write fails
    std::string unreadable;     // the key whose read returns an error

    int read(const char *key, void *buf, size_t cap)
    {
        if (unreadable == key) return -5;
        auto found = keys.find(key);
        if (found == keys.end()) return 0;
        size_t n = found->second.size() < cap ? found->second.size() : cap;
        memcpy(buf, found->second.data(), n);
        return (int)n;
    }
    bool write(const char *key, const void *data, size_t len)
    {
        if (writes_left == 0) return false;
        if (writes_left > 0) writes_left--;
        keys[key] = std::string(static_cast<const char *>(data), len);
        return true;
    }
    void erase(const char *key) { keys.erase(key); }
};

struct Sink {
    std::string bytes;
    bool append(const uint8_t *data, size_t len)
    {
        bytes.append(reinterpret_cast<const char *>(data), len);
        return true;
    }
};

struct FakeFiles {
    std::map<Item, std::string> files;
    std::set<Item> damaged;     // there, but its owner cannot load it
    bool writable = true;

    bool loads(Item item) { return files.count(item) != 0 && damaged.count(item) == 0; }
    size_t size(Item item) { return files.count(item) ? files[item].size() : 0; }
    bool read(Item item, size_t offset, uint8_t *buf, size_t len)
    {
        if (!files.count(item) || offset + len > files[item].size()) return false;
        memcpy(buf, files[item].data() + offset, len);
        return true;
    }
    template <class Fill>
    bool replace(Item item, Fill fill)
    {
        Sink sink;
        if (!writable || !fill(sink)) return false;
        files[item] = sink.bytes;
        damaged.erase(item);
        return true;
    }
};

std::string bytes(size_t length, uint32_t seed)
{
    std::string out(length, '\0');
    for (char &c : out) c = (char)(nextRandom(seed) >> 24);
    return out;
}

/* One node: its files and settings storage outlive the boots. */
struct Node {
    FakeStore store;
    FakeFiles files;

    Node()
    {
        files.files = {{Item::Identity, bytes(96, 1)}, {Item::Prefs, bytes(1709, 2)}, {Item::Regions, bytes(5258, 3)}};
    }
};

struct Boot {
    rescue::Copy<FakeStore, FakeFiles> copy;

    explicit Boot(Node &node, uint32_t epoch = 0) : copy(node.store, node.files)
    {
        copy.open();
        copy.restoreLostFiles(epoch);
        copy.completeAtBoot();
    }
};

std::string entry(rescue::Copy<FakeStore, FakeFiles> &copy, Item item)
{
    Sink sink;
    CHECK(copy.copyTo(item, sink));
    return sink.bytes;
}
}

TEST(rescue_created, "UNIT-RESCUE-001", "a configured node creates the copy at boot, byte for byte") {
    Node node;
    Boot boot(node);
    for (Item item : rescue::ITEMS) {
        CHECK(boot.copy.state(item) == State::Valid);
        CHECK(entry(boot.copy, item) == node.files.files[item]);
    }
    CHECK(boot.copy.restores().count == 0);
}

TEST(rescue_restores_missing, "UNIT-RESCUE-002", "a missing file and a file that does not load come back from the copy") {
    Node node;
    Boot first(node);
    const std::map<Item, std::string> before = node.files.files;
    node.files.files.erase(Item::Identity);
    node.files.files[Item::Prefs] = "{";
    node.files.damaged.insert(Item::Prefs);

    Boot second(node, 1795000000);
    CHECK(node.files.files == before);
    CHECK(second.copy.restores().count == 2);
    CHECK(second.copy.restores().last_epoch == 1795000000);
}

TEST(rescue_keeps_good_files, "UNIT-RESCUE-003", "a file that loads is left alone and a whole entry is not replaced at boot") {
    Node node;
    Boot first(node);
    const std::string copied = node.files.files[Item::Prefs];
    node.files.files[Item::Prefs] = "newer settings, saved without a command";

    Boot second(node);
    CHECK(node.files.files[Item::Prefs] == "newer settings, saved without a command");
    CHECK(entry(second.copy, Item::Prefs) == copied);
    CHECK(second.copy.restores().count == 0);
}

TEST(rescue_not_from_new_files, "UNIT-RESCUE-004", "files this boot creates on an empty node are not copied until the next boot or command") {
    Node node;
    const std::map<Item, std::string> created = node.files.files;
    node.files.files.clear();
    rescue::Copy<FakeStore, FakeFiles> copy(node.store, node.files);
    copy.open();
    copy.restoreLostFiles(0);
    node.files.files = created;                         // the owners write their defaults
    copy.completeAtBoot();
    CHECK(node.store.keys.empty());

    copy.followFiles();
    CHECK(entry(copy, Item::Identity) == created.at(Item::Identity));
}

TEST(rescue_follows_commands, "UNIT-RESCUE-005", "after a command only the files that changed are written") {
    Node node;
    Boot boot(node);
    node.store.writes_left = 0;
    boot.copy.followFiles();                            // nothing changed: no write is even tried
    CHECK(boot.copy.state(Item::Prefs) == State::Valid);

    node.store.writes_left = 3;                         // two chunks and the head of the preferences
    node.files.files[Item::Prefs][1500] ^= 1;
    boot.copy.followFiles();
    CHECK(node.store.writes_left == 0);
    CHECK(entry(boot.copy, Item::Prefs) == node.files.files[Item::Prefs]);
    CHECK(boot.copy.state(Item::Regions) == State::Valid);
}

TEST(rescue_damaged_not_copied, "UNIT-RESCUE-006", "a file that does not load never reaches the copy") {
    Node node;
    Boot boot(node);
    const std::string copied = node.files.files[Item::Prefs];
    node.files.files[Item::Prefs] = "{";
    node.files.damaged.insert(Item::Prefs);
    boot.copy.followFiles();
    CHECK(entry(boot.copy, Item::Prefs) == copied);
}

TEST(rescue_erase, "UNIT-RESCUE-007", "once writing is stopped for an erase nothing more is written") {
    Node node;
    Boot boot(node);
    node.store.keys.clear();                            // the erase
    boot.copy.stopWriting();
    boot.copy.followFiles();
    boot.copy.completeAtBoot();
    CHECK(node.store.keys.empty());
}

TEST(rescue_chunk_edges, "UNIT-RESCUE-008", "files of one byte, a whole chunk, one more and the maximum go round trip") {
    for (size_t length : {size_t(1), rescue::CHUNK_SIZE, rescue::CHUNK_SIZE + 1, rescue::MAX_FILE_SIZE}) {
        Node node;
        node.files.files[Item::Regions] = bytes(length, 4);
        Boot first(node);
        node.files.files.erase(Item::Regions);
        Boot second(node);
        CHECK(node.files.files[Item::Regions] == bytes(length, 4));
    }
}

TEST(rescue_unstorable, "UNIT-RESCUE-009", "an empty or oversize file is not written and the old entry stays whole") {
    Node node;
    Boot boot(node);
    const std::string copied = node.files.files[Item::Regions];
    node.files.files[Item::Regions] = bytes(rescue::MAX_FILE_SIZE + 1, 5);
    boot.copy.followFiles();
    CHECK(entry(boot.copy, Item::Regions) == copied);
    node.files.files[Item::Regions].clear();
    boot.copy.followFiles();
    CHECK(entry(boot.copy, Item::Regions) == copied);
}

TEST(rescue_shrink, "UNIT-RESCUE-010", "a file that shrinks leaves no old chunks behind") {
    Node node;
    Boot boot(node);
    node.files.files[Item::Regions] = bytes(174, 6);
    boot.copy.followFiles();
    CHECK(node.store.keys.count("ztc/rc/regions/0") == 1);
    CHECK(node.store.keys.count("ztc/rc/regions/1") == 0);
    CHECK(node.store.keys.count("ztc/rc/regions/5") == 0);
    CHECK(entry(boot.copy, Item::Regions) == bytes(174, 6));
}

TEST(rescue_interrupted, "UNIT-RESCUE-011", "a write cut at any point leaves a broken entry, repaired at the next boot") {
    for (int writes = 1; writes < 7; writes++) {        // six chunks, then the head
        Node node;
        Boot first(node);
        node.files.files[Item::Regions] = bytes(5258, 7);
        node.store.writes_left = writes;
        first.copy.followFiles();
        CHECK(first.copy.state(Item::Regions) == State::Broken);

        node.store.writes_left = 0;                     // the next boot reads what the cut left
        Boot cut(node);
        CHECK(cut.copy.state(Item::Regions) == State::Broken);

        node.store.writes_left = -1;
        Boot repaired(node);
        CHECK(entry(repaired.copy, Item::Regions) == bytes(5258, 7));
    }
}

TEST(rescue_broken_not_restored, "UNIT-RESCUE-012", "a damaged chunk, a short head or another format is never restored") {
    for (int damage = 0; damage < 3; damage++) {
        Node node;
        Boot first(node);
        if (damage == 0) node.store.keys["ztc/rc/prefs/1"][0] ^= 1;
        if (damage == 1) node.store.keys["ztc/rc/prefs/head"].resize(4);
        if (damage == 2) node.store.keys["ztc/rc/prefs/head"][0] ^= 2;
        node.files.files.erase(Item::Prefs);

        Boot second(node);
        CHECK(second.copy.state(Item::Prefs) == State::Broken);
        CHECK(node.files.files.count(Item::Prefs) == 0);
        CHECK(second.copy.restores().count == 0);
    }
}

TEST(rescue_unreadable, "UNIT-RESCUE-013", "storage that cannot be read is kept through a format and not written in that boot") {
    Node node;
    Boot first(node);
    const std::map<std::string, std::string> kept = node.store.keys;
    node.store.unreadable = "ztc/rc/prefs/head";
    node.files.files[Item::Regions] = bytes(174, 8);

    Boot second(node);
    CHECK(second.copy.state(Item::Prefs) == State::Unreadable);
    CHECK(second.copy.worthKeeping());
    second.copy.followFiles();
    CHECK(node.store.keys == kept);

    node.store.keys.clear();
    node.store.unreadable = "ztc/rc/id/head";           // nothing else to keep it for
    Boot third(node);
    CHECK(third.copy.worthKeeping());
    CHECK(node.store.keys.empty());
}

TEST(rescue_worth_keeping, "UNIT-RESCUE-014", "a format keeps the storage only while an entry is whole or unreadable") {
    Node node;
    node.files.files.clear();
    Boot empty(node);
    CHECK(!empty.copy.worthKeeping());

    node.files.files[Item::Identity] = bytes(96, 9);
    Boot copied(node);
    CHECK(copied.copy.worthKeeping());

    node.store.keys["ztc/rc/id/0"][0] ^= 1;
    node.files.files.clear();
    Boot broken(node);
    CHECK(!broken.copy.worthKeeping());
}

TEST(rescue_restore_failed, "UNIT-RESCUE-015", "a file that cannot be written back keeps its entry and is still readable from it") {
    Node node;
    Boot first(node);
    const std::string identity = node.files.files[Item::Identity];
    node.files.files.erase(Item::Identity);
    node.files.writable = false;

    Boot second(node);
    CHECK(second.copy.restores().count == 0);
    CHECK(entry(second.copy, Item::Identity) == identity);
    node.files.files[Item::Identity] = "a new identity made after the failure";
    second.copy.followFiles();
    CHECK(entry(second.copy, Item::Identity) == identity);
}

TEST(rescue_restore_count, "UNIT-RESCUE-016", "restores add up across boots and an unset clock leaves the time unknown") {
    Node node;
    Boot first(node);
    node.files.files.erase(Item::Prefs);
    Boot second(node, 1795000000);
    node.files.files.erase(Item::Prefs);
    Boot third(node, 0);
    CHECK(third.copy.restores().count == 2);
    CHECK(third.copy.restores().last_epoch == 0);
}
