/*
 * SPDX-License-Identifier: MIT
 * Meshtastic client protocol (what the phone app speaks over BLE): decode of
 * ToRadio and encode of the FromRadio messages the node sends during the
 * config download and afterwards. Pure code: no heap, no platform calls.
 * Field numbers follow mesh.proto and channel.proto.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "MtWire.h"

namespace mt {

/* Largest FromRadio the node writes. */
static constexpr size_t kMaxFromRadio = 320;

/* MeshPacket as it crosses the phone link. For a packet the phone sends,
 * data.payload points into the decoded buffer. */
struct PhonePacket {
	uint32_t from;
	uint32_t to;
	uint32_t id;
	uint32_t channel;  /* channel index in the app's table */
	uint32_t rx_time;  /* unix seconds, 0 when unknown */
	float rx_snr;
	uint8_t hop_limit;
	uint8_t hop_start;
	uint8_t relay_node;
	bool want_ack;
	bool has_decoded;  /* false: encrypted or empty, nothing to bridge */
	Data data;
};

enum class ToKind { NONE, PACKET, WANT_CONFIG, DISCONNECT, HEARTBEAT, OTHER };

struct ToRadio {
	ToKind kind;
	uint32_t want_config_id;
	PhonePacket packet;
};

/* False when the buffer is not well formed protobuf. A well formed message with
 * a variant we do not serve (xmodem, MQTT proxy) is true with kind OTHER. */
bool decodeToRadio(const uint8_t *buf, size_t len, ToRadio &t);

/* FromRadio encoders: bytes written to out, 0 when cap is too small. */
size_t encodeFromRadioPacket(const PhonePacket &p, uint8_t *out, size_t cap);
size_t encodeFromRadioMyInfo(uint32_t node, uint32_t reboot_count, uint32_t nodedb_count,
			     uint8_t *out, size_t cap);

struct PhoneNode {
	uint32_t num;
	User user;
	bool has_position;
	Position position;
	bool has_snr;
	float snr;
	uint32_t last_heard;  /* unix seconds, 0 when unknown */
	bool has_hops;
	uint8_t hops;         /* 0 = heard directly */
};
size_t encodeFromRadioNodeInfo(const PhoneNode &n, uint8_t *out, size_t cap);

enum : uint8_t { CHANNEL_DISABLED = 0, CHANNEL_PRIMARY = 1, CHANNEL_SECONDARY = 2 };
/* psk: 0 bytes (no crypto), 1 byte (default key variant) or 16 bytes. */
size_t encodeFromRadioChannel(uint8_t index, uint8_t role, const char *name, const uint8_t *psk,
			      size_t psk_len, uint8_t *out, size_t cap);

struct PhoneMetadata {
	const char *firmware_version;
	uint8_t role;  /* Config.DeviceConfig.Role */
	uint32_t hw_model;
	bool has_bluetooth;
	bool has_pkc;
};
size_t encodeFromRadioMetadata(const PhoneMetadata &m, uint8_t *out, size_t cap);

size_t encodeFromRadioConfigComplete(uint32_t config_id, uint8_t *out, size_t cap);

/* res: 0 queued; free and maxlen: places in our queue toward the radio. */
size_t encodeFromRadioQueueStatus(int32_t res, uint32_t free_slots, uint32_t maxlen,
				  uint32_t packet_id, uint8_t *out, size_t cap);

/* RouteDiscovery, the payload of a traceroute reply: the nodes between us and the
 * target (route) and on the way back (route_back), as the app lists them. The
 * SNR lists say "not measured". Bytes written, 0 when cap is too small. */
size_t encodeRouteDiscovery(const uint32_t *route, size_t n, const uint32_t *route_back, size_t nb,
			    uint8_t *out, size_t cap);

} /* namespace mt */
