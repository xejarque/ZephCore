/*
 * SPDX-License-Identifier: MIT
 * Protobuf wire primitives shared by the Meshtastic codecs (MtWire, MtPhoneWire):
 * a bounds-checked reader and a writer that latches overflow. No heap.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace mt {
namespace proto {

enum { WT_VARINT = 0, WT_I64 = 1, WT_LEN = 2, WT_I32 = 5 };

struct Reader {
	const uint8_t *p;
	const uint8_t *end;

	bool varint(uint64_t &v)
	{
		v = 0;
		for (int shift = 0; shift < 70; shift += 7) {
			if (p >= end) {
				return false;
			}
			uint8_t b = *p++;

			v |= (uint64_t)(b & 0x7F) << shift;
			if (!(b & 0x80)) {
				return true;
			}
		}
		return false;
	}

	bool fixed32(uint32_t &v)
	{
		if (end - p < 4) {
			return false;
		}
		v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
		    ((uint32_t)p[3] << 24);
		p += 4;
		return true;
	}

	bool bytes(const uint8_t *&data, size_t &n)
	{
		uint64_t l;

		if (!varint(l) || l > (uint64_t)(end - p)) {
			return false;
		}
		data = p;
		n = (size_t)l;
		p += n;
		return true;
	}

	/* Next field header; false at a clean end of buffer. */
	bool field(uint32_t &num, uint32_t &wt, bool &error)
	{
		if (p >= end) {
			return false;
		}
		uint64_t tag;

		if (!varint(tag) || (tag >> 3) == 0 || (tag >> 3) > 0x1FFFFFFF) {
			error = true;
			return false;
		}
		num = (uint32_t)(tag >> 3);
		wt = (uint32_t)(tag & 7);
		return true;
	}

	bool skip(uint32_t wt)
	{
		uint64_t v;
		const uint8_t *d;
		size_t n;

		switch (wt) {
		case WT_VARINT:
			return varint(v);
		case WT_I64:
			if (end - p < 8) {
				return false;
			}
			p += 8;
			return true;
		case WT_LEN:
			return bytes(d, n);
		case WT_I32:
			if (end - p < 4) {
				return false;
			}
			p += 4;
			return true;
		default:  /* groups and the reserved types are never valid here */
			return false;
		}
	}
};

struct Writer {
	uint8_t *p;
	uint8_t *end;
	bool ok = true;

	void put(uint8_t b)
	{
		if (p >= end) {
			ok = false;
			return;
		}
		*p++ = b;
	}

	void varint(uint64_t v)
	{
		while (v >= 0x80) {
			put((uint8_t)(v | 0x80));
			v >>= 7;
		}
		put((uint8_t)v);
	}

	void tag(uint32_t num, uint32_t wt) { varint(((uint64_t)num << 3) | wt); }

	void fixed32(uint32_t num, uint32_t v)
	{
		tag(num, WT_I32);
		for (int i = 0; i < 4; i++) {
			put((uint8_t)(v >> (8 * i)));
		}
	}

	void uint(uint32_t num, uint64_t v)
	{
		tag(num, WT_VARINT);
		varint(v);
	}

	void bytes(uint32_t num, const uint8_t *d, size_t n)
	{
		tag(num, WT_LEN);
		varint(n);
		if ((size_t)(end - p) < n) {
			ok = false;
			return;
		}
		memcpy(p, d, n);
		p += n;
	}
};

} /* namespace proto */
} /* namespace mt */
