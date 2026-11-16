/*
 * SPDX-License-Identifier: MIT
 * Meshtastic on-air format: the 16 byte frame header, AES-CTR payload
 * encryption and the three protobuf messages the bridge reads and writes
 * (Data, Position, User). Pure code: no heap, no platform calls.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mt {

static constexpr uint32_t kBroadcast = 0xFFFFFFFFu;
static constexpr size_t kHeaderLen = 16;
static constexpr size_t kMaxFrame = 255;

/* Data.portnum values the bridge handles. */
enum : uint32_t {
	PORT_TEXT = 1,
	PORT_POSITION = 3,
	PORT_NODEINFO = 4,
	PORT_ROUTING = 5,
	PORT_TELEMETRY = 67,
	PORT_TRACEROUTE = 70,
};

struct Header {
	uint32_t to;
	uint32_t from;
	uint32_t id;
	uint8_t flags;    /* hop_limit 0-2, want_ack 3, via_mqtt 4, hop_start 5-7 */
	uint8_t channel;  /* channel hash */
	uint8_t next_hop;
	uint8_t relay;

	uint8_t hopLimit() const { return flags & 0x07; }
	uint8_t hopStart() const { return (uint8_t)(flags >> 5); }
	bool wantAck() const { return (flags & 0x08) != 0; }
};

/* False when len < kHeaderLen. */
bool parseHeader(const uint8_t *raw, size_t len, Header &h);
void writeHeader(const Header &h, uint8_t out[kHeaderLen]);

/* One AES-128 block encryption (the platform's AES), key 16 bytes. */
typedef void (*AesBlockFn)(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

/* Meshtastic's channel cipher: AES-128-CTR, IV = packet id (u64 LE) | from
 * (u32 LE) | 0, counter in the last four bytes, big endian. Encrypts and
 * decrypts in place. */
void ctrCrypt(AesBlockFn aes, const uint8_t key[16], uint32_t from, uint32_t id,
	      uint8_t *data, size_t len);

/* PSK of 1 byte selects a variant of the default key (0 = no encryption);
 * 16 bytes is the key itself. False for any other length. */
bool expandPsk(const uint8_t *psk, size_t psk_len, uint8_t key[16], bool &encrypted);

/* xor of the name bytes xor the key bytes (none when the channel is clear). */
uint8_t channelHash(const char *name, const uint8_t key[16], bool encrypted);

struct Data {
	uint32_t portnum;
	const uint8_t *payload;  /* points into the decoded buffer */
	size_t payload_len;
	uint32_t dest;
	uint32_t source;
	uint32_t request_id;
	uint32_t reply_id;  /* packet id of the text this one answers (or reacts to) */
	uint32_t emoji;     /* non zero: the payload is a reaction to reply_id */
	bool want_response;
};

/* False when the buffer is not well formed protobuf or has no portnum in 1..511. */
bool decodeData(const uint8_t *buf, size_t len, Data &d);
/* Bytes written, 0 when cap is too small. */
size_t encodeData(const Data &d, uint8_t *out, size_t cap);

/* Routing.error_reason of a routing payload: 0 is an ACK, anything else a NAK
 * (PKI_UNKNOWN_PUBKEY is 35). False when the payload carries none. */
static constexpr uint32_t ROUTING_MAX_RETRANSMIT = 5;
static constexpr uint32_t ROUTING_PKI_UNKNOWN_PUBKEY = 35;
bool decodeRouting(const uint8_t *buf, size_t len, uint32_t &error_reason);

/* RouteDiscovery of a traceroute: the nodes it went through (route) and came back
 * through (route_back), at most max each; n and nb count what was stored. */
bool decodeRouteDiscovery(const uint8_t *buf, size_t len, uint32_t *route, size_t max, size_t &n,
			  uint32_t *back, size_t maxb, size_t &nb);

struct Position {
	bool has_lat, has_lon;
	int32_t lat_i;  /* degrees * 1e7 */
	int32_t lon_i;
	int32_t altitude;
	bool has_altitude;
	uint32_t time;  /* unix seconds, 0 when absent */
};
bool decodePosition(const uint8_t *buf, size_t len, Position &p);
size_t encodePosition(const Position &p, uint8_t *out, size_t cap);

struct User {
	char id[16];
	char long_name[40];
	char short_name[5];
	uint32_t hw_model;
	uint32_t role;
	bool has_public_key;
	uint8_t public_key[32];  /* X25519, field 8 */
};
bool decodeUser(const uint8_t *buf, size_t len, User &u);
size_t encodeUser(const User &u, uint8_t *out, size_t cap);

/* Telemetry.device_metrics. battery_level above 100 means powered. */
struct DeviceMetrics {
	uint32_t battery_level;
	bool has_voltage;
	float voltage;
	float channel_utilization;  /* percent */
	float air_util_tx;          /* percent */
	uint32_t uptime_s;
	uint32_t time;              /* unix seconds, 0 when the clock is not set */
};
size_t encodeTelemetry(const DeviceMetrics &m, uint8_t *out, size_t cap);

} /* namespace mt */
