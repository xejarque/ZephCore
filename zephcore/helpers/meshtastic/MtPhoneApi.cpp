/*
 * SPDX-License-Identifier: MIT
 * Meshtastic client session. See MtPhoneApi.h.
 */

#include "MtPhoneApi.h"

#include <string.h>

namespace mt {

/* Routing.Error NO_CHANNEL: the QueueStatus result for a packet we cannot send. */
static constexpr int32_t kQueueNoChannel = 8;

uint32_t mcNodeNum(const uint8_t pub[32])
{
	uint32_t n = ((uint32_t)pub[0] << 24) | ((uint32_t)pub[1] << 16) | ((uint32_t)pub[2] << 8) | pub[3];

	if (n < 4 || n == kBroadcast) {
		n = 0x4D430000u | ((uint32_t)pub[4] << 8) | pub[5];
	}
	return n;
}

uint32_t syntheticNode(const uint8_t *bytes, size_t n)
{
	uint32_t h = 2166136261u;  /* FNV-1a */

	for (size_t i = 0; i < n; i++) {
		h = (h ^ bytes[i]) * 16777619u;
	}
	h |= 0x80000000u;
	return h == kBroadcast ? h - 1 : h;
}

void PhoneApi::reset()
{
	_phase = Phase::IDLE;
	_ready = false;
	_head = 0;
	_count = 0;
}

bool PhoneApi::onToRadio(const uint8_t *buf, size_t len)
{
	ToRadio t;

	if (!decodeToRadio(buf, len, t)) {
		return false;
	}
	switch (t.kind) {
	case ToKind::WANT_CONFIG:
		reset();
		_config_id = t.want_config_id;
		_index = 0;
		_phase = _config_id == kNodeInfoNonce ? Phase::NODES : Phase::MY_INFO;
		_from_num++;
		break;
	case ToKind::DISCONNECT:
		reset();
		break;
	case ToKind::PACKET: {
		uint8_t frame[32];
		bool sent = t.packet.has_decoded && _host.sendFromPhone(t.packet);
		size_t n = encodeFromRadioQueueStatus(sent ? 0 : kQueueNoChannel, kPhoneQueue - _count,
						      kPhoneQueue, t.packet.id, frame, sizeof(frame));

		if (n) {
			enqueue(frame, n);
		}
		break;
	}
	default:
		break;
	}
	return true;
}

size_t PhoneApi::readConfig(uint8_t *out, size_t cap)
{
	for (;;) switch (_phase) {
	case Phase::MY_INFO:
		_phase = Phase::METADATA;
		return encodeFromRadioMyInfo(_host.selfNode(), _host.rebootCount(), _host.nodeCount(),
					     out, cap);
	case Phase::METADATA:
		_phase = Phase::CHANNELS;
		_index = 0;
		return encodeFromRadioMetadata(_host.metadata(), out, cap);
	case Phase::CHANNELS: {
		/* The app expects all of its slots; an empty one is sent disabled. */
		PhoneChannel ch = {};
		uint8_t i = (uint8_t)_index++;

		if (_index >= kPhoneChannels) {
			/* Stage 1 ends here: an app that asked for the config alone gets its nodes next time. */
			_phase = _config_id == kConfigNonce ? Phase::COMPLETE : Phase::NODES;
			_index = 0;
		}
		if (!_host.channelAt(i, ch)) {
			return encodeFromRadioChannel(i, CHANNEL_DISABLED, nullptr, nullptr, 0, out, cap);
		}
		return encodeFromRadioChannel(i, ch.role, ch.name, ch.psk, ch.psk_len, out, cap);
	}
	case Phase::NODES: {
		PhoneNode n = {};

		if (_index >= _host.nodeCount()) {
			_phase = Phase::COMPLETE;
			continue;
		}
		if (!_host.nodeAt(_index++, n)) {
			continue;  /* a vanished entry is skipped */
		}
		return encodeFromRadioNodeInfo(n, out, cap);
	}
	case Phase::COMPLETE:
		_phase = Phase::IDLE;
		_ready = _config_id != kConfigNonce;  /* the nodes are still to come */
		return encodeFromRadioConfigComplete(_config_id, out, cap);
	default:
		return 0;
	}
}

size_t PhoneApi::read(uint8_t *out, size_t cap)
{
	if (_phase != Phase::IDLE) {
		return readConfig(out, cap);
	}
	if (_count == 0) {
		return 0;
	}
	size_t n = _len[_head];

	if (n > cap) {
		return 0;
	}
	memcpy(out, _q[_head], n);
	_head = (uint8_t)((_head + 1) % kPhoneQueue);
	_count--;
	return n;
}

void PhoneApi::enqueue(const uint8_t *frame, size_t n)
{
	if (n > kPhoneFrame) {
		return;
	}
	if (_count == kPhoneQueue) {  /* drop the oldest, as Meshtastic does */
		_head = (uint8_t)((_head + 1) % kPhoneQueue);
		_count--;
	}
	uint8_t tail = (uint8_t)((_head + _count) % kPhoneQueue);

	memcpy(_q[tail], frame, n);
	_len[tail] = (uint16_t)n;
	_count++;
	_from_num++;
}

void PhoneApi::pushPacket(const PhonePacket &p)
{
	uint8_t frame[kPhoneFrame];

	if (!_ready) {
		return;
	}
	size_t n = encodeFromRadioPacket(p, frame, sizeof(frame));

	if (n) {
		enqueue(frame, n);
	}
}

void PhoneApi::pushNode(const PhoneNode &n)
{
	uint8_t frame[kPhoneFrame];

	if (!_ready) {
		return;
	}
	size_t len = encodeFromRadioNodeInfo(n, frame, sizeof(frame));

	if (len) {
		enqueue(frame, len);
	}
}

} /* namespace mt */
