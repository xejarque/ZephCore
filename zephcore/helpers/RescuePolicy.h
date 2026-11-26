/*
 * SPDX-License-Identifier: MIT
 * Zephtacore: the rescue copy a repeater or room server keeps of its identity,
 * preferences, regions and virtual room identity (ADR 0021): how a file is
 * stored as checksummed chunks, when it is put back and when the copy is
 * written. Pure code, host tested; the storage and the files are template
 * parameters.
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
#include <time.h>
#include <zephyr/sys/crc.h>

namespace rescue {

enum class Item : uint8_t { Identity, Prefs, Regions, RoomIdentity };
constexpr Item ITEMS[] = {Item::Identity, Item::Prefs, Item::Regions, Item::RoomIdentity};
constexpr size_t ITEM_COUNT = sizeof(ITEMS) / sizeof(ITEMS[0]);

enum class State : uint8_t { Absent, Broken, Valid, Unreadable };

constexpr size_t CHUNK_SIZE = 1024;
constexpr size_t MAX_CHUNKS = 6;
constexpr size_t MAX_FILE_SIZE = MAX_CHUNKS * CHUNK_SIZE;
constexpr uint32_t FORMAT = 1;
/* A clock that reads earlier was never set. */
constexpr uint32_t CLOCK_SET_AFTER = 1772323200;  /* 1 Mar 2026 */

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
	case Item::Regions: return "regions";
	default: return "room";
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

inline const char *stateName(State state)
{
	switch (state) {
	case State::Valid: return "ok";
	case State::Absent: return "none";
	case State::Broken: return "broken";
	default: return "unreadable";
	}
}

/* The reply of "get z.rescue", short enough for one remote reply. A node with
 * no virtual room does not name it. */
inline void formatReport(char *out, size_t cap, const State (&states)[ITEM_COUNT],
			 const Restores &restores)
{
	const State room = states[static_cast<size_t>(Item::RoomIdentity)];
	const bool has_room = room != State::Absent;
	const int n = snprintf(out, cap, "> identity %s, prefs %s, regions %s%s%s | restores %u",
			       stateName(states[0]), stateName(states[1]), stateName(states[2]),
			       has_room ? ", room " : "", has_room ? stateName(room) : "",
			       (unsigned)restores.count);

	if (restores.count == 0 || n < 0 || (size_t)n >= cap) {
		return;
	}
	const time_t when = (time_t)restores.last_epoch;
	struct tm utc;

	if (restores.last_epoch == 0 || gmtime_r(&when, &utc) == nullptr) {
		snprintf(out + n, cap - n, ", last at unknown time");
		return;
	}
	snprintf(out + n, cap - n, ", last %04d-%02d-%02d %02d:%02d UTC", utc.tm_year + 1900,
		 utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min);
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

	/* At boot, before the files' owners load them: a file that does not load
	 * comes back from a whole entry, and a file that loads creates or repairs
	 * its entry; a whole entry is never replaced here. A file that cannot be
	 * written back stops all writes, so its entry survives. */
	void atBoot(uint32_t now)
	{
		for (Item item : ITEMS) {
			const bool loads = _files.loads(item);
			const bool whole = state(item) == State::Valid;

			if (loads && !whole) {
				save(item);
			} else if (!loads && whole) {
				restore(item, now);
			}
		}
	}

	/* After an administration command: writes the entries whose file changed. */
	void followFiles()
	{
		for (Item item : ITEMS) {
			if (_files.size(item) > 0 && !holds(item) && _files.loads(item)) {
				save(item);
			}
		}
	}

	/* No more writes until the reboot: the node is being erased. */
	void stopWriting() { _writable = false; }

	/* Chunk i of a whole entry, valid until the next call; nullptr past the
	 * last one or when it cannot be read. */
	const uint8_t *chunk(Item item, size_t i, size_t &length)
	{
		const size_t file_length = _heads[index(item)].length;

		if (state(item) != State::Valid || i >= chunkCount(file_length)) {
			return nullptr;
		}
		char key[KEY_CAP];

		length = chunkLength(file_length, i);
		chunkKey(key, item, i);
		return _store.read(key, _chunk, length) == (int)length ? _chunk : nullptr;
	}

	/* Appends the file's bytes, from a whole entry, to the sink. */
	template <class Sink>
	bool copyTo(Item item, Sink &sink)
	{
		if (state(item) != State::Valid) {
			return false;
		}
		for (size_t i = 0; i < chunkCount(_heads[index(item)].length); i++) {
			size_t length;
			const uint8_t *data = chunk(item, i, length);

			if (data == nullptr || !sink.append(data, length)) {
				return false;
			}
		}
		return true;
	}

	void report(char *out, size_t cap) { formatReport(out, cap, _states, restores()); }

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

	void restore(Item item, uint32_t now)
	{
		if (_files.replace(item, [this, item](auto &sink) { return this->copyTo(item, sink); })) {
			noteRestore(now);
		} else {
			_writable = false;
		}
	}

	void noteRestore(uint32_t now)
	{
		if (!_writable) {
			return;
		}
		Restores r = restores();

		r.count++;
		r.last_epoch = now > CLOCK_SET_AFTER ? now : 0;
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
	bool _writable = false;
	uint8_t _chunk[CHUNK_SIZE];
};

}  // namespace rescue
