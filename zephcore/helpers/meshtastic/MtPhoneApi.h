/*
 * SPDX-License-Identifier: MIT
 * Meshtastic client session (what the phone app drives over BLE): the config
 * download that answers want_config_id, the queue of packets toward the phone,
 * and the packets the phone sends. The transport reads FromRadio one message at
 * a time with read() and tells the phone with fromNum() that one is waiting.
 * Pure code: no heap, the node's tables are reached through PhoneHost.
 */

#pragma once

#include "MtPhoneWire.h"

namespace mt {

/* Channels the app's table holds (channel index 0 to 7). */
static constexpr uint8_t kPhoneChannels = 8;
/* want_config_id of the two-stage handshake: the first asks for the configuration
 * and channels, the second for the nodes. Any other value gets everything. */
static constexpr uint32_t kConfigNonce = 69420;
static constexpr uint32_t kNodeInfoNonce = 69421;
/* Frames waiting for the phone; the oldest is dropped when it is full. */
static constexpr uint8_t kPhoneQueue = 8;
/* Largest queued frame: a text of kMaxTxFrame bytes plus its MeshPacket fields. */
static constexpr size_t kPhoneFrame = 320;
static_assert(kPhoneFrame >= kMaxFromRadio, "a FromRadio must fit a queue slot");

/* The number a MeshCore contact has in the app: the start of its public key, with
 * "MC" in the top half when that would be a reserved value. */
uint32_t mcNodeNum(const uint8_t pub[32]);
/* A number for a name or path hash that matches no contact: stable, top bit set. */
uint32_t syntheticNode(const uint8_t *bytes, size_t n);

struct PhoneChannel {
	bool used;
	uint8_t role;  /* CHANNEL_PRIMARY or CHANNEL_SECONDARY when used */
	char name[16];
	uint8_t psk[16];
	size_t psk_len;
};

class PhoneHost {
public:
	virtual ~PhoneHost() {}
	virtual uint32_t selfNode() = 0;
	virtual uint32_t rebootCount() = 0;
	virtual PhoneMetadata metadata() = 0;
	/* Channel at the app's index; false when the slot is empty. */
	virtual bool channelAt(uint8_t index, PhoneChannel &ch) = 0;
	/* The nodes the app lists, our own first. */
	virtual uint32_t nodeCount() = 0;
	virtual bool nodeAt(uint32_t index, PhoneNode &n) = 0;
	/* A packet the phone wants on the air. False when it cannot be sent. */
	virtual bool sendFromPhone(const PhonePacket &p) = 0;
};

class PhoneApi {
public:
	explicit PhoneApi(PhoneHost &host) : _host(host) {}

	/* One ToRadio written by the phone. False when it was not well formed. */
	bool onToRadio(const uint8_t *buf, size_t len);

	/* Next FromRadio for the phone: bytes written to out, 0 when there is none. */
	size_t read(uint8_t *out, size_t cap);

	/* Queue a packet heard on the air for the phone. Nothing is queued before
	 * the phone has finished the config download or after it disconnects. */
	void pushPacket(const PhonePacket &p);
	/* Same for a node whose name, position or signal changed. */
	void pushNode(const PhoneNode &n);

	/* Counter the phone is notified with: it grows each time a frame is queued. */
	uint32_t fromNum() const { return _from_num; }
	bool hasPending() const { return _phase != Phase::IDLE || _count != 0; }
	/* The link went away: forget the session. */
	void reset();

private:
	enum class Phase { IDLE, MY_INFO, METADATA, CHANNELS, NODES, COMPLETE };

	PhoneHost &_host;
	Phase _phase = Phase::IDLE;
	bool _ready = false;  /* config download finished: packets are queued */
	uint32_t _config_id = 0;
	uint32_t _index = 0;
	uint32_t _from_num = 0;
	uint8_t _head = 0;
	uint8_t _count = 0;
	uint16_t _len[kPhoneQueue] = {};
	uint8_t _q[kPhoneQueue][kPhoneFrame];

	size_t readConfig(uint8_t *out, size_t cap);
	void enqueue(const uint8_t *frame, size_t n);
};

} /* namespace mt */
