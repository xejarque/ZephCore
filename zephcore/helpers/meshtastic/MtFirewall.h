/*
 * SPDX-License-Identifier: MIT
 * Zephtacore: the receive limits of z.mt.fw.* on the Meshtastic side, applied to a
 * frame the bridge has accepted as new (not one it heard before, so a repeat of
 * ours is still cancelled when a neighbour echoes it). Pure code, host tested.
 */

#pragma once

#include "MtBridge.h"

#include "../McFirewall.h"

namespace mt {

/* The ports a drop rule silences. Routing (ACK and NAK) is never one of them. */
enum FwRule : uint8_t { FW_TEXT, FW_POSITION, FW_NODEINFO, FW_TELEMETRY, FW_TRACEROUTE, FW_OTHER, FW_COUNT };
static const char *const kFwRuleName[FW_COUNT] = {"text", "position", "nodeinfo", "telemetry", "traceroute", "other"};
static constexpr fw::Rules kFwRules = {kFwRuleName, FW_COUNT};

/* The rule of a port, fw::kNoRule for a frame this node cannot open (portnum 0) or an ACK. */
inline uint8_t fwRuleOf(uint32_t portnum)
{
	switch (portnum) {
	case 0:
	case PORT_ROUTING:
		return fw::kNoRule;
	case PORT_TEXT:
		return FW_TEXT;
	case PORT_POSITION:
		return FW_POSITION;
	case PORT_NODEINFO:
		return FW_NODEINFO;
	case PORT_TELEMETRY:
		return FW_TELEMETRY;
	case PORT_TRACEROUTE:
		return FW_TRACEROUTE;
	default:
		return FW_OTHER;
	}
}

/* The order of the checks is the MeshCore one: drop rule, gap, rate; a frame that passes all of
 * them opens its node's NodeInfo gap. Frames of a channel this node holds no key for have no
 * port, so only the rate reaches them. */
class FwScreen : public Screen {
public:
	FwScreen(fw::Firewall &f, const fw::Prefs &p) : _f(f), _p(p) {}

	void setNow(uint32_t now_ms) { _now = now_ms; }

	bool drops(const Header &h, uint32_t portnum) override
	{
		/* A dropped frame is not remembered by the bridge: its copies get the same verdict. */
		const uint32_t key = h.from ^ (h.id * 2654435761u);
		bool dropped;

		if (_f.screened(key, dropped)) {
			return dropped;
		}
		dropped = verdict(h, portnum);
		_f.noteScreened(key, dropped);
		return dropped;
	}

private:
	bool verdict(const Header &h, uint32_t portnum)
	{
		if (_f.ruleDropped(_p, fwRuleOf(portnum))) {
			return true;
		}
		const bool nodeinfo = portnum == PORT_NODEINFO;

		if (nodeinfo && _f.advertInGap(_p, h.from, _now)) {
			return true;
		}
		if (h.relay != 0 && _f.relayOverRate(_p, &h.relay, 1, _now)) {
			return true;
		}
		if (nodeinfo) {
			_f.advertKept(_p, h.from, _now);
		}
		return false;
	}

	fw::Firewall &_f;
	const fw::Prefs &_p;
	uint32_t _now = 0;
};

} /* namespace mt */
