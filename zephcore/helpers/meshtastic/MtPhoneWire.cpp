/*
 * SPDX-License-Identifier: MIT
 * Meshtastic client protocol codec. See MtPhoneWire.h.
 */

#include "MtPhoneWire.h"

#include "MtProto.h"

#include <string.h>

namespace mt {

using namespace proto;

namespace {

/* FromRadio payload_variant field numbers. */
enum {
	FR_PACKET = 2,
	FR_MY_INFO = 3,
	FR_NODE_INFO = 4,
	FR_CONFIG_COMPLETE_ID = 7,
	FR_CHANNEL = 10,
	FR_QUEUE_STATUS = 11,
	FR_METADATA = 13,
};

/* ToRadio payload_variant field numbers. */
enum {
	TR_PACKET = 1,
	TR_WANT_CONFIG_ID = 3,
	TR_DISCONNECT = 4,
	TR_HEARTBEAT = 7,
};

uint32_t floatBits(float f)
{
	uint32_t u;

	memcpy(&u, &f, sizeof(u));
	return u;
}

/* FromRadio { <variant> = inner }. */
size_t wrap(uint32_t variant, const uint8_t *inner, size_t n, uint8_t *out, size_t cap)
{
	Writer w = {out, out + cap};

	w.bytes(variant, inner, n);
	return w.ok ? (size_t)(w.p - out) : 0;
}

bool decodePacket(const uint8_t *buf, size_t len, PhonePacket &p)
{
	Reader r = {buf, buf + len};
	uint32_t num, wt;
	bool error = false;

	memset(&p, 0, sizeof(p));
	while (r.field(num, wt, error)) {
		uint64_t v = 0;
		const uint8_t *d = nullptr;
		size_t n = 0;
		uint32_t f32 = 0;
		bool ok = true;

		if (wt == WT_VARINT) {
			ok = r.varint(v);
		} else if (wt == WT_I32) {
			ok = r.fixed32(f32);
		} else if (wt == WT_LEN) {
			ok = r.bytes(d, n);
		} else {
			return false;
		}
		if (!ok) {
			return false;
		}

		switch (num) {
		case 1: p.from = f32; break;
		case 2: p.to = f32; break;
		case 3: p.channel = (uint32_t)v; break;
		case 4:
			if (!decodeData(d, n, p.data)) {
				return false;
			}
			p.has_decoded = true;
			break;
		case 6: p.id = f32; break;
		case 9: p.hop_limit = (uint8_t)(v & 7); break;
		case 10: p.want_ack = v != 0; break;
		case 15: p.hop_start = (uint8_t)(v & 7); break;
		case 19: p.relay_node = (uint8_t)v; break;
		default: break;  /* priority, encrypted, pki fields: not used by the bridge */
		}
	}
	return !error;
}

} /* namespace */

bool decodeToRadio(const uint8_t *buf, size_t len, ToRadio &t)
{
	Reader r = {buf, buf + len};
	uint32_t num, wt;
	bool error = false;

	memset(&t, 0, sizeof(t));
	t.kind = ToKind::NONE;
	while (r.field(num, wt, error)) {
		uint64_t v = 0;
		const uint8_t *d = nullptr;
		size_t n = 0;

		if (wt == WT_VARINT) {
			if (!r.varint(v)) {
				return false;
			}
		} else if (wt == WT_LEN) {
			if (!r.bytes(d, n)) {
				return false;
			}
		} else if (!r.skip(wt)) {
			return false;
		}

		switch (num) {
		case TR_PACKET:
			if (wt != WT_LEN || !decodePacket(d, n, t.packet)) {
				return false;
			}
			t.kind = ToKind::PACKET;
			break;
		case TR_WANT_CONFIG_ID:
			t.want_config_id = (uint32_t)v;
			t.kind = ToKind::WANT_CONFIG;
			break;
		case TR_DISCONNECT: t.kind = ToKind::DISCONNECT; break;
		case TR_HEARTBEAT: t.kind = ToKind::HEARTBEAT; break;
		default: t.kind = ToKind::OTHER; break;
		}
	}
	return !error;
}

size_t encodeFromRadioPacket(const PhonePacket &p, uint8_t *out, size_t cap)
{
	uint8_t data[kMaxFrame];
	uint8_t inner[kMaxFromRadio];
	Writer w = {inner, inner + sizeof(inner)};

	w.fixed32(1, p.from);
	w.fixed32(2, p.to);
	if (p.channel) {
		w.uint(3, p.channel);
	}
	if (p.has_decoded) {
		size_t n = encodeData(p.data, data, sizeof(data));

		if (n == 0) {
			return 0;
		}
		w.bytes(4, data, n);
	}
	w.fixed32(6, p.id);
	if (p.rx_time) {
		w.fixed32(7, p.rx_time);
	}
	if (p.rx_snr != 0.0f) {
		w.fixed32(8, floatBits(p.rx_snr));
	}
	if (p.hop_limit) {
		w.uint(9, p.hop_limit);
	}
	if (p.want_ack) {
		w.uint(10, 1);
	}
	if (p.hop_start) {
		w.uint(15, p.hop_start);
	}
	if (p.relay_node) {
		w.uint(19, p.relay_node);
	}
	return w.ok ? wrap(FR_PACKET, inner, (size_t)(w.p - inner), out, cap) : 0;
}

size_t encodeFromRadioMyInfo(uint32_t node, uint32_t reboot_count, uint32_t nodedb_count,
			     uint8_t *out, size_t cap)
{
	uint8_t inner[32];
	Writer w = {inner, inner + sizeof(inner)};

	w.uint(1, node);
	if (reboot_count) {
		w.uint(8, reboot_count);
	}
	w.uint(15, nodedb_count);
	return w.ok ? wrap(FR_MY_INFO, inner, (size_t)(w.p - inner), out, cap) : 0;
}

size_t encodeFromRadioNodeInfo(const PhoneNode &n, uint8_t *out, size_t cap)
{
	uint8_t user[kMaxFromRadio / 2];
	uint8_t pos[48];
	uint8_t inner[kMaxFromRadio];
	Writer w = {inner, inner + sizeof(inner)};
	size_t ulen = encodeUser(n.user, user, sizeof(user));

	if (ulen == 0) {
		return 0;
	}
	w.uint(1, n.num);
	w.bytes(2, user, ulen);
	if (n.has_position) {
		size_t plen = encodePosition(n.position, pos, sizeof(pos));

		if (plen == 0) {
			return 0;
		}
		w.bytes(3, pos, plen);
	}
	if (n.has_snr) {
		w.fixed32(4, floatBits(n.snr));
	}
	if (n.last_heard) {
		w.fixed32(5, n.last_heard);
	}
	if (n.has_hops) {
		w.uint(9, n.hops);
	}
	return w.ok ? wrap(FR_NODE_INFO, inner, (size_t)(w.p - inner), out, cap) : 0;
}

size_t encodeFromRadioChannel(uint8_t index, uint8_t role, const char *name, const uint8_t *psk,
			      size_t psk_len, uint8_t *out, size_t cap)
{
	uint8_t settings[64];
	uint8_t inner[96];
	Writer s = {settings, settings + sizeof(settings)};
	Writer w = {inner, inner + sizeof(inner)};

	if (psk_len) {
		s.bytes(2, psk, psk_len);
	}
	if (name && name[0]) {
		s.bytes(3, (const uint8_t *)name, strlen(name));
	}
	if (!s.ok) {
		return 0;
	}
	if (index) {
		w.uint(1, index);
	}
	w.bytes(2, settings, (size_t)(s.p - settings));
	if (role) {
		w.uint(3, role);
	}
	return w.ok ? wrap(FR_CHANNEL, inner, (size_t)(w.p - inner), out, cap) : 0;
}

size_t encodeFromRadioMetadata(const PhoneMetadata &m, uint8_t *out, size_t cap)
{
	uint8_t inner[48];
	Writer w = {inner, inner + sizeof(inner)};

	w.bytes(1, (const uint8_t *)m.firmware_version, strlen(m.firmware_version));
	if (m.has_bluetooth) {
		w.uint(5, 1);
	}
	if (m.role) {
		w.uint(7, m.role);
	}
	if (m.hw_model) {
		w.uint(9, m.hw_model);
	}
	if (m.has_pkc) {
		w.uint(11, 1);
	}
	return w.ok ? wrap(FR_METADATA, inner, (size_t)(w.p - inner), out, cap) : 0;
}

size_t encodeFromRadioConfigComplete(uint32_t config_id, uint8_t *out, size_t cap)
{
	Writer w = {out, out + cap};

	w.uint(FR_CONFIG_COMPLETE_ID, config_id);
	return w.ok ? (size_t)(w.p - out) : 0;
}

size_t encodeFromRadioQueueStatus(int32_t res, uint32_t free_slots, uint32_t maxlen,
				  uint32_t packet_id, uint8_t *out, size_t cap)
{
	uint8_t inner[24];
	Writer w = {inner, inner + sizeof(inner)};

	if (res) {
		w.uint(1, (uint64_t)(int64_t)res);  /* int32: negative values are 10 byte varints */
	}
	w.uint(2, free_slots);
	w.uint(3, maxlen);
	if (packet_id) {
		w.uint(4, packet_id);
	}
	return w.ok ? wrap(FR_QUEUE_STATUS, inner, (size_t)(w.p - inner), out, cap) : 0;
}

/* repeated fixed32 is packed: one length delimited field of 4 byte values. */
static void putPackedFixed32(Writer &w, uint32_t field, const uint32_t *v, size_t n)
{
	if (n == 0) {
		return;
	}
	w.tag(field, WT_LEN);
	w.varint(n * 4);
	for (size_t i = 0; i < n; i++) {
		for (int b = 0; b < 4; b++) {
			w.put((uint8_t)(v[i] >> (8 * b)));
		}
	}
}

/* snr_towards and snr_back: one value per link, -128 = not measured. The app reads the
 * way back only when snr_back is there, and sizes both lists against the route. */
static void putUnknownSnr(Writer &w, uint32_t field, size_t links)
{
	w.tag(field, WT_LEN);
	w.varint(links * 10);  /* -128 as int32 is a 10 byte varint */
	for (size_t i = 0; i < links; i++) {
		w.varint((uint64_t)(int64_t)-128);
	}
}

size_t encodeRouteDiscovery(const uint32_t *route, size_t n, const uint32_t *route_back, size_t nb,
			    uint8_t *out, size_t cap)
{
	Writer w = {out, out + cap};

	putPackedFixed32(w, 1, route, n);
	putUnknownSnr(w, 2, n + 1);
	putPackedFixed32(w, 3, route_back, nb);
	putUnknownSnr(w, 4, nb + 1);
	return w.ok ? (size_t)(w.p - out) : 0;
}

} /* namespace mt */
