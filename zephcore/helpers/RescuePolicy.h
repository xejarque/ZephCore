/*
 * SPDX-License-Identifier: MIT
 * Zephtacore: the rescue copy a repeater or room server keeps of its identity,
 * preferences and regions (ADR 0021): how a file is stored as checksummed
 * chunks, when it is put back and when the copy is written. Pure code, host
 * tested; the storage and the files are template parameters.
 *
 * Store: int read(key, buf, cap)    bytes read, 0: no such key, <0: error
 *        bool write(key, data, len)
 *        void erase(key)
 * Files: bool loads(item)           there, and its owner can load it
 *        size_t size(item)          0: no file
 *        bool read(item, offset, buf, len)
 *        bool replace(item, fill)   atomic; fill(sink) appends the new bytes
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zephyr/sys/crc.h>

namespace rescue {

enum class Item : uint8_t { Identity, Prefs, Regions };
constexpr Item ITEMS[] = {Item::Identity, Item::Prefs, Item::Regions};
constexpr size_t ITEM_COUNT = sizeof(ITEMS) / sizeof(ITEMS[0]);

enum class State : uint8_t { Absent, Broken, Valid, Unreadable };

constexpr size_t CHUNK_SIZE = 1024;
constexpr size_t MAX_CHUNKS = 6;
constexpr size_t MAX_FILE_SIZE = MAX_CHUNKS * CHUNK_SIZE;
constexpr uint32_t FORMAT = 1;

/* Written after the chunks, so an interrupted write fails its checksum. */
struct Head {
	uint32_t format;
	uint32_t length;
	uint32_t crc;
};

/* How many files were put back, and when the last one was. */
struct Restores {
	uint32_t count;
	uint32_t last_epoch;  /* 0: the clock was not set */
};

constexpr size_t KEY_CAP = 24;
constexpr const char *RESTORES_KEY = "ztc/rc/restores";

inline const char *itemName(Item item)
{
	switch (item) {
	case Item::Identity: return "id";
	case Item::Prefs: return "prefs";
	default: return "regions";
	}
}

inline void headKey(char (&key)[KEY_CAP], Item item)
{
	snprintf(key, KEY_CAP, "ztc/rc/%s/head", itemName(item));
}

inline void chunkKey(char (&key)[KEY_CAP], Item item, size_t index)
{
	snprintf(key, KEY_CAP, "ztc/rc/%s/%u", itemName(item), (unsigned)index);
}

inline size_t chunkCount(size_t length)
{
	return (length + CHUNK_SIZE - 1) / CHUNK_SIZE;
}

inline size_t chunkLength(size_t length, size_t index)
{
	size_t left = length - index * CHUNK_SIZE;

	return left < CHUNK_SIZE ? left : CHUNK_SIZE;
}

inline bool storable(size_t length)
{
	return length > 0 && length <= MAX_FILE_SIZE;
}

template <class Store, class Files>
class Copy {
public:
	Copy(Store &store, Files &files) : _store(store), _files(files) {}

	/* Reads the copy; call before anything else. Storage that cannot be read
	 * is not written either. */
	void open()
	{
		_writable = true;
		for (Item item : ITEMS) {
			_states[index(item)] = inspect(item);
			if (state(item) == State::Unreadable) {
				_writable = false;
			}
		}
	}

	State state(Item item) const { return _states[index(item)]; }

	/* A format must keep the storage while it may hold a copy. */
	bool worthKeeping() const
	{
		for (Item item : ITEMS) {
			if (state(item) == State::Valid || state(item) == State::Unreadable) {
				return true;
			}
		}
		return false;
	}

	/* Puts back each file that does not load and has a whole entry, and notes
	 * which files loaded. Call before their owners load them. A file that
	 * cannot be written back stops all writes, so its entry survives. */
	void restoreLostFiles(uint32_t epoch)
	{
		for (Item item : ITEMS) {
			bool loads = _files.loads(item);

			_loaded_at_boot[index(item)] = loads;
			if (!loads && state(item) == State::Valid) {
				restore(item, epoch);
			}
		}
	}

	/* Creates or repairs entries from the files that loaded at boot, not from
	 * ones this boot created. A whole entry is never replaced here. */
	void completeAtBoot()
	{
		for (Item item : ITEMS) {
			if (_loaded_at_boot[index(item)] && state(item) != State::Valid) {
				save(item);
			}
		}
	}

	/* After an administration command: writes the entries whose file changed. */
	void followFiles()
	{
		for (Item item : ITEMS) {
			if (_files.loads(item) && !holds(item)) {
				save(item);
			}
		}
	}

	/* No more writes until the reboot: the node is being erased. */
	void stopWriting() { _writable = false; }

	/* Appends the file's bytes, from a whole entry, to the sink. */
	template <class Sink>
	bool copyTo(Item item, Sink &sink)
	{
		if (state(item) != State::Valid) {
			return false;
		}
		const size_t length = _heads[index(item)].length;
		char key[KEY_CAP];

		for (size_t i = 0; i < chunkCount(length); i++) {
			const size_t n = chunkLength(length, i);

			chunkKey(key, item, i);
			if (_store.read(key, _chunk, n) != (int)n || !sink.append(_chunk, n)) {
				return false;
			}
		}
		return true;
	}

	Restores restores()
	{
		Restores r = {};

		if (_store.read(RESTORES_KEY, &r, sizeof(r)) != (int)sizeof(r)) {
			return Restores{};
		}
		return r;
	}

private:
	static size_t index(Item item) { return static_cast<size_t>(item); }

	State inspect(Item item)
	{
		Head &head = _heads[index(item)];
		char key[KEY_CAP];

		headKey(key, item);
		int n = _store.read(key, &head, sizeof(head));

		if (n < 0) {
			return State::Unreadable;
		}
		if (n == 0) {
			return State::Absent;
		}
		if (n != (int)sizeof(head) || head.format != FORMAT || !storable(head.length)) {
			return State::Broken;
		}
		uint32_t crc = 0;

		for (size_t i = 0; i < chunkCount(head.length); i++) {
			const size_t expected = chunkLength(head.length, i);

			chunkKey(key, item, i);
			n = _store.read(key, _chunk, expected);
			if (n < 0) {
				return State::Unreadable;
			}
			if (n != (int)expected) {
				return State::Broken;
			}
			crc = crc32_ieee_update(crc, _chunk, expected);
		}
		return crc == head.crc ? State::Valid : State::Broken;
	}

	void restore(Item item, uint32_t epoch)
	{
		if (_files.replace(item, [this, item](auto &sink) { return this->copyTo(item, sink); })) {
			noteRestore(epoch);
		} else {
			_writable = false;
		}
	}

	void noteRestore(uint32_t epoch)
	{
		if (!_writable) {
			return;
		}
		Restores r = restores();

		r.count++;
		r.last_epoch = epoch;
		_store.write(RESTORES_KEY, &r, sizeof(r));
	}

	/* Whether the entry is whole and equal to the file. */
	bool holds(Item item)
	{
		const Head &head = _heads[index(item)];

		if (state(item) != State::Valid || _files.size(item) != head.length) {
			return false;
		}
		uint32_t crc = 0;

		for (size_t i = 0; i < chunkCount(head.length); i++) {
			const size_t n = chunkLength(head.length, i);

			if (!_files.read(item, i * CHUNK_SIZE, _chunk, n)) {
				return false;
			}
			crc = crc32_ieee_update(crc, _chunk, n);
		}
		return crc == head.crc;
	}

	void save(Item item)
	{
		const size_t length = _files.size(item);

		if (!_writable || !storable(length)) {
			return;
		}
		Head head = {FORMAT, (uint32_t)length, 0};
		char key[KEY_CAP];

		_states[index(item)] = State::Broken;
		for (size_t i = 0; i < chunkCount(length); i++) {
			const size_t n = chunkLength(length, i);

			chunkKey(key, item, i);
			if (!_files.read(item, i * CHUNK_SIZE, _chunk, n) ||
			    !_store.write(key, _chunk, n)) {
				return;
			}
			head.crc = crc32_ieee_update(head.crc, _chunk, n);
		}
		headKey(key, item);
		if (!_store.write(key, &head, sizeof(head))) {
			return;
		}
		for (size_t i = chunkCount(length); i < MAX_CHUNKS; i++) {
			chunkKey(key, item, i);
			_store.erase(key);
		}
		_heads[index(item)] = head;
		_states[index(item)] = State::Valid;
	}

	Store &_store;
	Files &_files;
	Head _heads[ITEM_COUNT] = {};
	State _states[ITEM_COUNT] = {};
	bool _loaded_at_boot[ITEM_COUNT] = {};
	bool _writable = false;
	uint8_t _chunk[CHUNK_SIZE];
};

}  // namespace rescue
