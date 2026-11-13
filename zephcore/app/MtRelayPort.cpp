/*
 * SPDX-License-Identifier: MIT
 * See MtRelayPort.h.
 */

#include "MtRelayPort.h"

#if IS_ENABLED(CONFIG_ZEPHTACORE_MESHTASTIC)

#include <helpers/meshtastic/MtConfig.h>
#include <helpers/meshtastic/MtDiscriminate.h>
#include <stdio.h>
#include <mesh/ContentionTracker.h>
#include <mesh/Utils.h>
#include <psa/crypto.h>
#include <zephyr/kernel.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zephcore_mt, CONFIG_ZEPHCORE_MAIN_LOG_LEVEL);

/* The counter blocks of a frame share one key: it is imported once and kept until the next key. */
static void aesEcb(const uint8_t *key, size_t key_len, const uint8_t in[16], uint8_t out[16])
{
	static uint8_t cached[32];
	static size_t cached_len;
	static psa_key_id_t cached_id;
	size_t n = 0;

	if (cached_len != key_len || memcmp(cached, key, key_len) != 0) {
		psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;

		if (cached_len) {
			psa_destroy_key(cached_id);
			cached_len = 0;
		}
		psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
		psa_set_key_bits(&attr, key_len * 8);
		psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
		psa_set_key_algorithm(&attr, PSA_ALG_ECB_NO_PADDING);
		if (psa_import_key(&attr, key, key_len, &cached_id) != PSA_SUCCESS) {
			memset(out, 0, 16);
			return;
		}
		memcpy(cached, key, key_len);
		cached_len = key_len;
	}
	if (psa_cipher_encrypt(cached_id, PSA_ALG_ECB_NO_PADDING, in, 16, out, 16, &n) != PSA_SUCCESS) {
		memset(out, 0, 16);
	}
}

void MtRelayPort::aesBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
	aesEcb(key, 16, in, out);
}

void MtRelayPort::aesBlock256(const uint8_t key[32], const uint8_t in[16], uint8_t out[16])
{
	aesEcb(key, 32, in, out);
}

void MtRelayPort::initSelf(mt::Bridge &bridge, const mesh::LocalIdentity &id, const char *name,
			   mesh::RNG &rng)
{
	char short_name[5];
	uint32_t seed = 0;

	rng.random((uint8_t *)&seed, sizeof(seed));
	mt::shortName(name, short_name);
	bridge.setSelf(mt::nodeFromPublicKey(id.pub_key), name, short_name, seed);
}

void MtRelayPort::renameSelf(mt::Bridge &bridge, const char *name)
{
	char short_name[5];

	mt::shortName(name, short_name);
	bridge.setNames(name, short_name);
}

static constexpr uint32_t kFirstNodeInfoMs = 30000;
static constexpr uint32_t kFirstTelemetryMs = 60000;

MtRelayPort::Policy MtRelayPort::policyFor(const mt::RepeaterPrefs &p)
{
	return {p.enable != 0, p.relay == mt::RELAY_ALL, mt::roleYieldsToEcho(p.role),
		mt::roleSendsEarly(p.role), p.zerohop == mt::ZEROHOP_AUTO, p.zerohop_nodes};
}

void MtRelayPort::apply(const mt::RepeaterPrefs &p)
{
	_bridge.setRole(p.role);
	_bridge.setHopLimit(p.hops);
	_bridge.setRelay(p.enable && p.relay != mt::RELAY_OFF && mt::roleRepeats(p.role));
	_bridge.setRelayOpaque(p.relay_opaque != 0);
}

void MtRelayPort::arm(uint32_t now_ms, const mt::RepeaterPrefs &p)
{
	if (p.enable && p.nodeinfo_s) {
		_nodeinfo.arm(now_ms, kFirstNodeInfoMs);
	} else {
		_nodeinfo.stop();
	}
	if (p.enable && p.telemetry_s) {
		_telemetry.arm(now_ms, kFirstTelemetryMs);
	} else {
		_telemetry.stop();
	}
}

void MtRelayPort::tick(uint32_t now_ms, const mt::RepeaterPrefs &p, uint8_t channel, Source &src)
{
	uint8_t frame[mt::kMaxTxFrame];

	if (_nodeinfo.due(now_ms)) {
		if (p.enable && p.nodeinfo_s) {
			_nodeinfo.arm(now_ms, p.nodeinfo_s * 1000u);
		} else {
			_nodeinfo.stop();
		}
		size_t n = _bridge.buildNodeInfo(channel, frame, sizeof(frame));

		if (n > 0) {
			send(frame, n);
		}
		mt::Position pos = {};

		if (src.position(pos) && (n = _bridge.buildPosition(channel, pos, frame, sizeof(frame))) > 0) {
			send(frame, n);
		}
	}
	if (_telemetry.due(now_ms)) {
		if (p.enable && p.telemetry_s) {
			_telemetry.arm(now_ms, p.telemetry_s * 1000u);
		} else {
			_telemetry.stop();
		}
		mt::DeviceMetrics m = {};
		size_t n;

		if (src.metrics(m) && (n = _bridge.buildTelemetry(channel, m, frame, sizeof(frame))) > 0) {
			send(frame, n);
		}
	}
}

bool MtRelayPort::nextAt(uint32_t &at_ms) const
{
	if (_nodeinfo.armed() && _telemetry.armed()) {
		at_ms = (int32_t)(_telemetry.at() - _nodeinfo.at()) < 0 ? _telemetry.at() : _nodeinfo.at();
		return true;
	}
	if (_nodeinfo.armed() || _telemetry.armed()) {
		at_ms = _nodeinfo.armed() ? _nodeinfo.at() : _telemetry.at();
		return true;
	}
	return false;
}

void MtRelayPort::formatStats(char *out, size_t cap) const
{
	snprintf(out, cap, "> heard=%u relayed=%u opaque=%u yielded=%u dropped=%u shed=%u kept=%u",
		 (unsigned)_stats.heard, (unsigned)_stats.relayed, (unsigned)_stats.opaque,
		 (unsigned)_stats.yielded, (unsigned)_stats.dropped, (unsigned)_stats.shed,
		 (unsigned)_stats.kept);
}

bool MtRelayPort::send(const uint8_t *frame, size_t len)
{
	const uint32_t r = _mesh.getRNG()->nextInt(0, mt::originWindow());

	if (!_mesh.sendForeign(frame, (int)len, 1, mt::originDelayMs(slotMs(), r))) {
		LOG_WRN("mt: frame not queued (len=%u)", (unsigned)len);
		return false;
	}
	return true;
}

uint32_t MtRelayPort::slotMs() const
{
	const mesh::Radio *radio = _mesh.getRadio();

	return mt::slotTimeMs(radio->getActiveSpreadingFactor(), radio->getActiveBandwidthKHzX10() / 10.0f);
}

bool MtRelayPort::yields(uint32_t hash) const
{
	for (uint32_t h : _yield) {
		if (h == hash) {
			return true;
		}
	}
	return false;
}

/* Queues the repeat of a heard frame with the delay of the role. A repeat that
 * may give way to an echo is remembered by hash. */
void MtRelayPort::queue(const uint8_t *relay, size_t len, int8_t snr_x4, mt::RelayClass cls,
			const Policy &policy)
{
	if (len > mt::kMaxTxFrame) {
		LOG_WRN("mt: relay too long (len=%u)", (unsigned)len);
		_stats.shed++;
		return;
	}
	const uint32_t slot = slotMs();
	const uint32_t r = (uint32_t)_mesh.getRNG()->nextInt(0, (int)mt::relayWindow(snr_x4, policy.early));

	if (!_mesh.relayForeign(relay, len, mt::relayDelayMs(slot, policy.early, r))) {
		LOG_WRN("mt: relay not queued (len=%u)", (unsigned)len);
		_stats.shed++;
		return;
	}
	_stats.relayed++;
	if (policy.yields && mt::yieldsOnEcho(cls)) {
		_yield[_yield_next] = mesh::ContentionTracker::computeForeignHash32(relay, (int)len);
		_yield_next = (uint8_t)((_yield_next + 1) % kYieldKeep);
	}
}

/* Heard again with nothing to repeat. From a repeater, while we hold the frame: a
 * frame that yields is dropped unless that repeater named us next hop, the others
 * move out of its way. The originator sending it again leaves our copy alone. */
void MtRelayPort::echoHeard(const uint8_t raw[], int len, bool may_yield)
{
	mt::Header h;
	uint32_t hash = mesh::ContentionTracker::computeForeignHash32(raw, len);

	if (!mt::parseHeader(raw, (size_t)len, h) || (h.hopStart() > 0 && h.hopStart() == h.hopLimit())) {
		return;
	}
	const bool asked = h.next_hop != 0 && h.next_hop == (uint8_t)_bridge.self();

	if (may_yield && !asked && yields(hash) && _mesh.cancelForeignRelay(hash)) {
		_stats.yielded++;
	} else {
		_mesh.foreignDupeHeard(raw, len);
	}
}

mt::RxResult MtRelayPort::unopened(const uint8_t raw[], int len, uint32_t self)
{
	return mt::looksLikeMeshCore(raw, (size_t)len, self) ? mt::RX_NOT_MESHTASTIC : mt::RX_IGNORED;
}

void MtRelayPort::noteDirect(const uint8_t raw[], int len, int8_t snr_x4)
{
	mt::Header h;

	if (mt::parseHeader(raw, (size_t)len, h) && h.from != _bridge.self() && h.hopStart() > 0 &&
	    h.hopStart() == h.hopLimit()) {
		_neighbours.note(h.from, snr_x4, (uint32_t)(k_uptime_get() / 1000));
	}
}

/* Zero hop: the repeat goes out with the hop_limit the frame came with. */
void MtRelayPort::keepHop(const uint8_t raw[], int len, uint8_t *relay, const Policy &policy)
{
	mt::Header h;

	if (policy.zerohop && mt::parseHeader(raw, (size_t)len, h) &&
	    _zerohop.keepsHop(h, policy.zerohop_nodes, _neighbours, (uint32_t)(k_uptime_get() / 1000))) {
		relay[12] = raw[12];
		_stats.kept++;
	}
}

mt::RxResult MtRelayPort::onFrame(const uint8_t raw[], int len, float snr, mt::Sink &sink,
				  const Policy &policy)
{
	if (!policy.enabled) {
		/* Off: what is not shaped like MeshCore is not ours to repeat as MeshCore either. */
		mt::RxResult r = unopened(raw, len, _bridge.self());

		if (r == mt::RX_IGNORED) {
			_stats.dropped++;
		}
		return r;
	}
	uint8_t relay[mt::kMaxFrame];
	size_t relay_len = 0;
	mt::RelayClass cls;
	const int8_t snr_x4 = (int8_t)(snr * 4.0f);
	mt::RxResult r = _bridge.handleFrame(raw, (size_t)len, snr_x4, sink, relay, &relay_len, &cls);

	if (r == mt::RX_NOT_MESHTASTIC) {
		/* Not opened on our channels: only a MeshCore-shaped frame goes on to the MeshCore stack. */
		r = unopened(raw, len, _bridge.self());
		if (r == mt::RX_IGNORED) {
			_stats.dropped++;
		}
		return r;
	}
	if (r == mt::RX_DROPPED) {
		return r;
	}
	_stats.heard++;
	noteDirect(raw, len, snr_x4);
	if (r == mt::RX_OPAQUE) {
		_stats.opaque++;
	}
	if (r == mt::RX_DUPLICATE && relay_len == 0) {
		echoHeard(raw, len, policy.yields);
		return r;
	}
	if (relay_len > 0 && mt::repeatsInMode(policy.relay_all, cls)) {
		keepHop(raw, len, relay, policy);
		queue(relay, relay_len, snr_x4, cls, policy);
	}
	return r;
}

#endif /* CONFIG_ZEPHTACORE_MESHTASTIC */
