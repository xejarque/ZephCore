/*
 * SPDX-License-Identifier: MIT
 * Zephtacore: the repeater's Meshtastic side. It repeats Meshtastic floods on
 * the radio it shares with MeshCore, announces itself with a NodeInfo on its
 * primary channel, and answers the "z.mt." CLI commands. Nothing here enters the
 * MeshCore routing path.
 */

#include "RepeaterMesh.h"

#if IS_ENABLED(CONFIG_ZEPHTACORE_MESHTASTIC)

#include <helpers/meshtastic/MtConfig.h>
#include <helpers/battery_curve.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zephcore_repeater, CONFIG_ZEPHCORE_MAIN_LOG_LEVEL);

static constexpr uint8_t kPrimaryChannel = 0;
static constexpr uint8_t kBotChannel = 1;
/* Unix time of 2023-11-14: an earlier clock was never set. */
static constexpr uint32_t kClockSetAfter = 1700000000u;

/* The position MeshCore adverts carry, the battery and the radio's utilisation. */
bool RepeaterMesh::MtSource::position(mt::Position &pos)
{
	const NodePrefs &p = m._prefs;

	if (p.advert_loc_policy == ADVERT_LOC_NONE || (p.node_lat == 0 && p.node_lon == 0)) {
		return false;
	}
	const uint32_t now_s = m.getRTCClock()->getCurrentTime();

	pos.has_lat = pos.has_lon = true;
	pos.lat_i = (int32_t)(p.node_lat * 1e7);
	pos.lon_i = (int32_t)(p.node_lon * 1e7);
	pos.time = now_s > kClockSetAfter ? now_s : 0;
	return true;
}

/* Channel utilisation is the share of the time since the last report that the
 * radio spent receiving or sending. */
bool RepeaterMesh::MtSource::metrics(mt::DeviceMetrics &d)
{
	const uint32_t now = (uint32_t)m._ms->getMillis();
	const uint32_t tx = (uint32_t)m.getTotalAirTime();
	const uint32_t rx = (uint32_t)m.getReceiveAirTime();
	const uint32_t span = now - at;
	const uint16_t mv = m._board.getBattMilliVolts();

	if (span > 0) {
		d.channel_utilization = 100.0f * (float)((tx - last_tx) + (rx - last_rx)) / (float)span;
		d.air_util_tx = 100.0f * (float)(tx - last_tx) / (float)span;
		if (d.channel_utilization > 100.0f) {
			d.channel_utilization = 100.0f;
		}
	}
	at = now;
	last_tx = tx;
	last_rx = rx;
	/* No battery reading: powered, which Meshtastic says as a level above 100. */
	d.battery_level = mv ? battery_curve_lookup(&battery_curve_default, mv) : 101;
	d.has_voltage = mv != 0;
	d.voltage = (float)mv / 1000.0f;
	d.uptime_s = (uint32_t)(k_uptime_get() / 1000);
	const uint32_t now_s = m.getRTCClock()->getCurrentTime();

	d.time = now_s > kClockSetAfter ? now_s : 0;
	return true;
}

/* Pushes our settings into the bridge; also the first start. */
void RepeaterMesh::mtApplyPrefs()
{
	const mt::RepeaterPrefs &p = _prefs.mt;

	_radio->setMtListen(p.enable != 0);
	_mt.setScreen(&_mt_screen);
	if (!_mt_ready) {
		MtRelayPort::initSelf(_mt, self_id, _prefs.node_name, *getRNG());
		_mt_ready = true;
	} else {
		MtRelayPort::renameSelf(_mt, _prefs.node_name);
	}
	_mtport.apply(p);
	mtSetChannels();
	_mtport.arm((uint32_t)_ms->getMillis(), p);
}

/* The primary channel, and the ping bot's while the bot is on. */
void RepeaterMesh::mtSetChannels()
{
	const mt::RepeaterPrefs &p = _prefs.mt;

	_mt.clearChannels();
	_mt.setChannel(kPrimaryChannel, p.channel, p.psk, p.psk_len);
	if (_prefs.ping.mt.enable) {
		const uint8_t index = ping::kMtPskIndex;

		_mt.setChannel(kBotChannel, PING_MT_CHANNEL, &index, 1);
	}
}

void RepeaterMesh::MtSink::onNodeInfo(uint32_t from, const mt::User &user)
{
	m._mtport.neighbours().setName(from, user.short_name);
	m._mtport.noteRole(from, user.role);
}

void RepeaterMesh::MtSink::onText(uint8_t channel_id, uint32_t from, uint32_t to, uint32_t id,
				  uint32_t reply_id, const char *text, size_t len, int8_t snr_x4,
				  uint8_t hops, uint8_t relay)
{
	(void)to; (void)reply_id;
	if (channel_id == kBotChannel) {
		m.mtPing(text, len, id, snr_x4, hops, relay, hops == 0 && relay != 0 && (uint8_t)from != relay);
	}
}

/* Answers as a reply to the message, on the channel it came on. The text has no sender, so no "@[name]", whatever it contains. */
void RepeaterMesh::mtPing(const char *text, size_t len, uint32_t id, int8_t snr_x4, uint8_t hops, uint8_t relay, bool relayed)
{
	char line[ping::kTriggerMax + 2];
	char out[96];
	const ping::Heard heard = {hops, snr_x4, (int16_t)_radio->getLastRSSI(),
				   {&relay, 1, (uint8_t)(hops || relayed ? 1 : 0)}, relayed, true};

	if (!_prefs.mt.enable || len >= sizeof(line)) {
		return;
	}
	memcpy(line, text, len);
	line[len] = '\0';
	const ping::Counters before = _ping_stats.mt;
	size_t n = ping::reply(_prefs.ping, _ping_stats.mt, ping::PROTO_MT, line, heard, nullptr, nullptr,
			       out, sizeof(out));
	uint8_t frame[mt::kMaxTxFrame];

	if (n == 0) {
		return;
	}
	if (!_ping_limit_mt.allow(getRTCClock()->getCurrentTime())) {
		_ping_stats.mt = before;
		_ping_stats.mt.ignored++;
		return;
	}
	n = _mt.buildText(kBotChannel, mt::kBroadcast, out, n, frame, sizeof(frame), id);
	if (n == 0 || !_mtport.send(frame, n)) {
		_ping_stats.mt = before;
	}
}

bool RepeaterMesh::onRecvForeign(const uint8_t raw[], int len, float snr, float rssi)
{
	(void)rssi;
	_mt_screen.setNow((uint32_t)_ms->getMillis());
	const mt::RxResult r = _mtport.onFrame(raw, len, snr, _mt_sink, MtRelayPort::policyFor(_prefs.mt));
	mt::Header h;

	if (r != mt::RX_NOT_MESHTASTIC && r != mt::RX_IGNORED && mt::parseHeader(raw, (size_t)len, h)) {
		_rx_mt.heard(h.hopStart() ? (h.hopStart() >= h.hopLimit() ? h.hopStart() - h.hopLimit() : 0) : rxstats::kUnknownHops,
			     &h.relay, h.relay ? 1 : 0);
	}
	return r != mt::RX_NOT_MESHTASTIC;
}

/* NodeInfo, position and telemetry on the primary channel, as Meshtastic's own default does. */
void RepeaterMesh::mtTick()
{
	_mtport.tick((uint32_t)_ms->getMillis(), _prefs.mt, kPrimaryChannel, _mt_source);
}

namespace {
class MtNeighbours : public neighbours::TableList {
public:
	using TableList::TableList;

protected:
	void label(const neighbours::Entry &e, neighbours::Row &r) const override
	{
		if (e.name[0]) {
			snprintf(r.name, sizeof(r.name), "%s", e.name);
		} else {
			neighbours::nodeLabel(r.name, sizeof(r.name), e.id);
		}
	}
};
} /* namespace */

void RepeaterMesh::mtFormatNeighbours(char *reply, size_t cap, size_t page, bool recent) const
{
	neighbours::format(reply, cap, "Meshtastic neighbors",
			   MtNeighbours(_mtport.neighbours(), (uint32_t)(k_uptime_get() / 1000)), page, recent);
}

void RepeaterMesh::mtFormatStats(char *reply, size_t cap) const
{
	_mtport.formatStats(reply, cap);
}

#endif /* CONFIG_ZEPHTACORE_MESHTASTIC */
