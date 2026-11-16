/*
 * SPDX-License-Identifier: MIT
 * Meshtastic side of Zephtacore: tells a received frame apart from a MeshCore
 * one by decrypting it on our channels, delivers text, node names and
 * positions to a sink, decides what to repeat, and builds the frames we send.
 * Pure code (the AES block function is injected), host tested.
 */

#pragma once

#include "MtPki.h"
#include "MtPolicy.h"
#include "MtWire.h"

namespace mt {

static constexpr int kMaxChannels = 8;
static constexpr int kDedupSize = 64;
/* Largest frame we queue: MeshCore's payload limit (MAX_PACKET_PAYLOAD). */
static constexpr size_t kMaxTxFrame = 184;
static constexpr uint8_t kNoChannel = 0xFF;

/* Meshtastic's contention slot on sub-GHz LoRa (RadioInterface::computeSlotTimeMsec):
 * a 2-symbol CAD plus half a symbol, plus 7.6 ms of propagation, TX/RX
 * turnaround and processing. */
uint32_t slotTimeMs(uint8_t sf, float bw_khz);

/* Contention window exponent of a rebroadcast (RadioInterface::getCWsize): the
 * SNR it was heard at, -20..+10 dB, maps to 3..8, clamped. A faint frame gets a
 * small window, so the node farthest from the sender repeats first. */
uint8_t rebroadcastCw(int8_t snr_x4);

/* Rebroadcast delay of a client node (RadioInterface::getTxDelayMsecWeighted):
 * 2 x 8 slots left to routers, then slot r of the window, r < 2^cw. */
uint32_t rebroadcastDelayMs(uint32_t slot_ms, uint32_t r);

/* Rebroadcast delay of a router, which goes before every client: slot r of
 * 2 x cw (RadioInterface::getTxDelayMsecWeighted, router branch), r < 2 * cw. */
uint32_t earlyDelayMs(uint32_t slot_ms, uint32_t r);

/* A frame of our own waits slot r of 2^3, the smallest contention window
 * (RadioInterface::getTxDelayMsec). */
uint32_t originWindow();
uint32_t originDelayMs(uint32_t slot_ms, uint32_t r);

/* Slots to draw r from: 2 x cw for a router (early), 2^cw for a client. */
uint32_t relayWindow(int8_t snr_x4, bool early);
uint32_t relayDelayMs(uint32_t slot_ms, bool early, uint32_t r);

class Sink {
public:
	virtual ~Sink() {}
	/* text is not NUL terminated. hops is 0 when the sender did not set hop_start.
	 * relay: last byte of the node number that sent us the frame (the sender itself when hops is 0). */
	virtual void onText(uint8_t channel_id, uint32_t from, uint32_t to, uint32_t id,
			    uint32_t reply_id, const char *text, size_t len, int8_t snr_x4,
			    uint8_t hops, uint8_t relay) = 0;
	virtual void onNodeInfo(uint32_t from, const User &user) = 0;
	virtual void onPosition(uint32_t from, const Position &pos) = 0;
	/* A PKI direct message for us. want_ack: the sender asks for an ACK. */
	virtual void onDirectText(uint32_t from, uint32_t id, uint32_t reply_id, bool want_ack,
				  const char *text, size_t len, int8_t snr_x4, uint8_t hops)
	{
		(void)from; (void)id; (void)reply_id; (void)want_ack;
		(void)text; (void)len; (void)snr_x4; (void)hops;
	}
	/* An ACK (error_reason 0) or NAK for a packet of ours, from a node. */
	virtual void onAck(uint32_t from, uint32_t request_id, uint32_t error_reason)
	{
		(void)from; (void)request_id; (void)error_reason;
	}
	/* The answer to a traceroute of ours: the nodes it crossed each way. */
	virtual void onTraceroute(uint32_t from, uint32_t request_id, const uint32_t *route, size_t n,
				  const uint32_t *back, size_t nb)
	{
		(void)from; (void)request_id; (void)route; (void)n; (void)back; (void)nb;
	}
	/* A node sent us its NodeInfo asking for ours. channel_id is the channel it
	 * came on, kNoChannel when it came as PKI. */
	virtual void onNodeInfoRequest(uint8_t channel_id, uint32_t from)
	{
		(void)channel_id; (void)from;
	}
	/* A PKI message for us from a node whose key we do not hold. */
	virtual void onUnknownKey(uint32_t from) { (void)from; }
	/* X25519 public key of a node, false when unknown. */
	virtual bool peerKey(uint32_t node, uint8_t key[32])
	{
		(void)node; (void)key;
		return false;
	}
};

enum RxResult {
	RX_NOT_MESHTASTIC,  /* leave the frame to MeshCore */
	RX_HANDLED,         /* decrypted on one of our channels */
	RX_DUPLICATE,       /* seen before; nothing to deliver */
	RX_OPAQUE,          /* Meshtastic on a channel we hold no key for; only relayed */
	RX_IGNORED,         /* not for MeshCore either, and Meshtastic is off */
	RX_DROPPED,         /* Meshtastic, new, and the screen drops it: nothing is delivered or repeated */
};

/* Decides, for a frame the bridge accepted as new, whether it is dropped. portnum is 0 for a
 * frame no channel of ours opens. */
class Screen {
public:
	virtual ~Screen() {}
	virtual bool drops(const Header &h, uint32_t portnum) = 0;
};

class Bridge {
public:
	explicit Bridge(AesBlockFn aes) : _aes(aes) {}

	/* Our node number (never 0, below 4 or broadcast), names and the seed of
	 * the packet id sequence. */
	void setSelf(uint32_t node, const char *long_name, const char *short_name, uint32_t seed);
	uint32_t self() const { return _self; }
	/* The names in our NodeInfo; the node number and the rest stay. */
	void setNames(const char *long_name, const char *short_name);
	/* Our role in the NodeInfo we send (Config.DeviceConfig.Role). */
	void setRole(uint8_t role) { _user.role = role; }

	/* channel_id is the caller's own handle (the companion channel index).
	 * False when the table is full or the PSK length is not 1 or 16. */
	bool setChannel(uint8_t channel_id, const char *name, const uint8_t *psk, size_t psk_len);
	void clearChannels();
	int channelCount() const;
	bool hasChannel(uint8_t channel_id) const;

	/* Enables PKI direct messages. seed: 32 bytes of the node identity. crypto
	 * must outlive the bridge. Our public key then goes out in our NodeInfo. */
	void setPki(const PkiCrypto *crypto, const uint8_t seed[32]);
	bool hasPki() const { return _pki != nullptr; }

	void setScreen(Screen *screen) { _screen = screen; }
	void setRelay(bool on) { _relay = on; }
	/* Also repeat frames no channel of ours opens (needs setRelay). */
	void setRelayOpaque(bool on) { _relay_opaque = on; }
	/* hop_limit and hop_start of the frames we originate, 0 to 7. */
	void setHopLimit(uint8_t hops) { _hops = hops > 7 ? 7 : hops; }

	/* relay_out (kMaxFrame bytes) receives the frame to repeat; *relay_len is
	 * its length, 0 when nothing is to be repeated. *relay_class, when given,
	 * says how that repeat is treated once another node repeats it first. */
	RxResult handleFrame(const uint8_t *raw, size_t len, int8_t snr_x4, Sink &sink,
			     uint8_t *relay_out, size_t *relay_len,
			     RelayClass *relay_class = nullptr);

	/* Frame builders: bytes written to out, 0 on failure (unknown channel,
	 * text too long for kMaxTxFrame, cap too small). to is a node number or
	 * kBroadcast. */
	size_t buildText(uint8_t channel_id, uint32_t to, const char *text, size_t len,
			 uint8_t *out, size_t cap, uint32_t reply_id = 0);
	size_t buildNodeInfo(uint8_t channel_id, uint8_t *out, size_t cap);
	size_t buildPosition(uint8_t channel_id, const Position &pos, uint8_t *out, size_t cap);
	size_t buildTelemetry(uint8_t channel_id, const DeviceMetrics &m, uint8_t *out, size_t cap);
	/* Our NodeInfo to one node, asking for theirs when want_response. */
	size_t buildNodeInfoTo(uint8_t channel_id, uint32_t to, bool want_response, uint8_t *out,
			       size_t cap);
	/* PKI frames to a node whose public key is peer_pub. 0 without PKI or on a weak key. */
	size_t buildDirectText(uint32_t to, const uint8_t peer_pub[32], const char *text, size_t len,
			       uint8_t *out, size_t cap, uint32_t reply_id = 0, bool want_ack = false);
	/* Traceroute request to a node, on a channel: PKI is never used for it, so that the
	 * nodes on the way can write themselves into the route. */
	size_t buildTraceroute(uint8_t channel_id, uint32_t to, uint8_t *out, size_t cap);
	size_t buildAck(uint32_t to, uint32_t request_id, const uint8_t peer_pub[32], uint8_t *out,
			size_t cap);

private:
	struct Channel {
		bool used;
		bool encrypted;
		uint8_t id;
		uint8_t hash;
		uint8_t key[16];
	};
	struct Seen {
		uint32_t from;
		uint32_t id;
		bool relayed;
	};

	AesBlockFn _aes;
	Channel _ch[kMaxChannels] = {};
	Seen _seen[kDedupSize] = {};
	uint8_t _seen_next = 0;
	uint32_t _self = 0;
	uint32_t _id_prefix = 0;
	uint32_t _id_counter = 0;
	bool _relay = false;
	bool _relay_opaque = false;
	Screen *_screen = nullptr;
	uint8_t _hops = 3;  /* Meshtastic's own default */
	User _user = {};
	const PkiCrypto *_pki = nullptr;
	uint8_t _pki_priv[32] = {};

	const Channel *find(uint8_t channel_id) const;
	Seen *findSeen(uint32_t from, uint32_t id);
	Seen *remember(uint32_t from, uint32_t id);
	/* True when the frame was heard before (or is ours). Its repeat is then
	 * still filled in when we were not asked to the first time, or when the
	 * originator sends it again. */
	bool heardBefore(const Header &h, const uint8_t *raw, size_t len, uint8_t *relay_out,
			 size_t *relay_len);
	/* We repeat a flood when it has hops left, is not ours or for us alone, and
	 * names no next hop or names us. */
	bool eligible(const Header &h) const;
	uint32_t nextId();
	/* Floods repeat once: next_hop 0, hops left, not for us alone. */
	bool fillRelay(const Header &h, const uint8_t *raw, size_t len, uint8_t *relay_out,
		       size_t *relay_len) const;
	/* ch carries the frame on a channel; pki_key (32 bytes) instead seals it for
	 * one node and sets channel 0. */
	size_t buildFrame(const Channel *ch, const uint8_t *pki_key, uint32_t to, uint32_t port,
			  const uint8_t *payload, size_t payload_len, uint8_t *out, size_t cap,
			  uint32_t reply_id = 0, uint32_t request_id = 0, bool want_response = false,
			  bool want_ack = false);
	void deliverData(const Header &h, const Data &d, uint8_t channel_id, int8_t snr_x4,
			 Sink &sink);
	bool openPki(const Header &h, const uint8_t *raw, size_t plen, Sink &sink, uint8_t *buf,
		     Data &d);
	void deliverPki(const Header &h, const Data &d, int8_t snr_x4, Sink &sink);
};

} /* namespace mt */
