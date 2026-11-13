/*
 * SPDX-License-Identifier: MIT
 * Receive limits of the repeater, one set per protocol: frame types dropped, one
 * advert (MeshCore) or NodeInfo (Meshtastic) per node per window, and a ceiling of
 * frames a minute from one neighbouring repeater. A frame over a limit is not
 * repeated and not processed. Pure code, host tested; persistence is prefs.json's zc{}.
 */

#pragma once

#include <mesh/Packet.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ZCommand.h"

namespace fw {

static constexpr size_t kAdvertSlots = 16;  /* nodes remembered */
static constexpr size_t kHopSlots = 8;      /* neighbouring repeaters counted */
static constexpr uint32_t kRateWindowMs = 60000;
static constexpr size_t kHopMax = 3;        /* bytes of a relay name: a MeshCore path hash is up to 3 */
static constexpr size_t kVerdicts = 32;     /* frames whose verdict is remembered */
static constexpr size_t kRuleMax = 8;       /* one bit of Prefs::drop each */
static constexpr uint8_t kNoRule = 0xFF;    /* a frame no rule covers */

/* The frame types a protocol lets the operator drop, by name. */
struct Rules {
	const char *const *names;
	uint8_t count;
};

/* MeshCore: the payload types a drop rule silences. CONTROL (neighbour discovery) is none of them. */
enum McRule : uint8_t { RULE_ADVERT, RULE_REQUEST, RULE_TEXT, RULE_GROUP, RULE_ACK, RULE_PATH, RULE_TRACE, RULE_COUNT };
static const char *const kMcRuleName[RULE_COUNT] = {"advert", "request", "text", "group", "ack", "path", "trace"};
static constexpr Rules kMcRules = {kMcRuleName, RULE_COUNT};

/* The rule of a payload type, kNoRule for a type no rule covers. */
inline uint8_t ruleOf(uint8_t payload_type)
{
	switch (payload_type) {
	case PAYLOAD_TYPE_ADVERT:
		return RULE_ADVERT;
	case PAYLOAD_TYPE_REQ:
	case PAYLOAD_TYPE_RESPONSE:
	case PAYLOAD_TYPE_ANON_REQ:
		return RULE_REQUEST;
	case PAYLOAD_TYPE_TXT_MSG:
		return RULE_TEXT;
	case PAYLOAD_TYPE_GRP_TXT:
	case PAYLOAD_TYPE_GRP_DATA:
		return RULE_GROUP;
	case PAYLOAD_TYPE_ACK:
	case PAYLOAD_TYPE_MULTIPART:
		return RULE_ACK;
	case PAYLOAD_TYPE_PATH:
		return RULE_PATH;
	case PAYLOAD_TYPE_TRACE:
		return RULE_TRACE;
	default:
		return kNoRule;
	}
}

/* Plain data: NodePrefs holds one per protocol and is cleared with memset. 0 = no limit. */
struct Prefs {
	uint8_t drop;            /* bit per rule: that frame type is dropped */
	uint8_t advert_gap_min;  /* minutes between two adverts (NodeInfo) of one node that are kept */
	uint8_t hop_rate;        /* frames a minute accepted from one neighbouring repeater */
};

/* Since boot; not persisted. */
struct Counters {
	uint32_t gap;   /* adverts dropped inside the window */
	uint32_t rate;  /* frames dropped over the ceiling */
	uint32_t rule[kRuleMax];  /* frames dropped by each drop rule */
};

inline bool within(uint32_t now, uint32_t since, uint32_t span)
{
	return (uint32_t)(now - since) < span;
}

/* RAM only. */
class Firewall {
	struct AdvertSeen {
		uint32_t key;
		uint32_t at_ms;
		bool used;
	};
	struct HopRate {
		uint8_t prefix[kHopMax];
		uint8_t size;
		uint8_t count;
		uint32_t window_ms;
	};
	uint32_t _verdict_hash[kVerdicts] = {};
	uint32_t _verdict_dropped = 0;  /* bit per entry */
	uint8_t _verdict_next = 0;
	AdvertSeen _adverts[kAdvertSlots] = {};
	HopRate _hops[kHopSlots] = {};
	uint8_t _advert_next = 0;
	uint8_t _hop_next = 0;
	Counters _counters = {};

public:
	const Counters &counters() const { return _counters; }

	/* A copy of a frame already screened gets the same verdict and is not counted again,
	 * so echoes do not inflate the counters or spend the rate. hash 0 is never remembered. */
	bool screened(uint32_t hash, bool &dropped) const
	{
		for (size_t i = 0; hash != 0 && i < kVerdicts; i++) {
			if (_verdict_hash[i] == hash) {
				dropped = (_verdict_dropped >> i) & 1u;
				return true;
			}
		}
		return false;
	}

	void noteScreened(uint32_t hash, bool dropped)
	{
		if (hash == 0) {
			return;
		}
		_verdict_hash[_verdict_next] = hash;
		_verdict_dropped = dropped ? (_verdict_dropped | (1u << _verdict_next)) : (_verdict_dropped & ~(1u << _verdict_next));
		_verdict_next = (uint8_t)((_verdict_next + 1) % kVerdicts);
	}

	/* True when a drop rule silences this frame type: drop the frame. */
	bool ruleDropped(const Prefs &p, uint8_t rule)
	{
		if (rule == kNoRule || rule >= kRuleMax || !(p.drop & (1u << rule))) {
			return false;
		}
		_counters.rule[rule]++;
		return true;
	}

	/* True when an advert of this key was kept less than the gap ago: drop it. */
	bool advertInGap(const Prefs &p, uint32_t key, uint32_t now)
	{
		if (p.advert_gap_min == 0) {
			return false;
		}
		for (const AdvertSeen &s : _adverts) {
			if (s.used && s.key == key) {
				if (within(now, s.at_ms, (uint32_t)p.advert_gap_min * 60000UL)) {
					_counters.gap++;
					return true;
				}
				return false;
			}
		}
		return false;
	}

	/* An advert of this key was accepted: it opens the gap. MeshCore calls it only for an
	 * advert that passed the signature check, or a forged one could silence the real node. */
	void advertKept(const Prefs &p, uint32_t key, uint32_t now)
	{
		if (p.advert_gap_min == 0) {
			return;
		}
		AdvertSeen *slot = nullptr;

		for (AdvertSeen &s : _adverts) {
			if (s.used && s.key == key) {
				slot = &s;
				break;
			}
		}
		if (!slot) {
			slot = &_adverts[_advert_next];
			_advert_next = (uint8_t)((_advert_next + 1) % kAdvertSlots);
			slot->key = key;
			slot->used = true;
		}
		slot->at_ms = now;
	}

	/* True when this repeater, named by its relay bytes, already got hop_rate frames
	 * through this minute: drop the frame. Counts the ones it lets through. */
	bool relayOverRate(const Prefs &p, const uint8_t *relay, size_t size, uint32_t now)
	{
		if (p.hop_rate == 0 || size == 0 || size > kHopMax) {
			return false;
		}
		HopRate *slot = nullptr;

		for (HopRate &h : _hops) {
			if (h.size == size && memcmp(h.prefix, relay, size) == 0) {
				slot = &h;
				break;
			}
		}
		if (!slot) {
			slot = &_hops[_hop_next];
			_hop_next = (uint8_t)((_hop_next + 1) % kHopSlots);
			slot->size = (uint8_t)size;
			memcpy(slot->prefix, relay, size);
			slot->window_ms = now;
			slot->count = 0;
		}
		if (!within(now, slot->window_ms, kRateWindowMs)) {
			slot->window_ms = now;
			slot->count = 0;
		}
		if (slot->count >= p.hop_rate) {
			_counters.rate++;
			return true;
		}
		slot->count++;
		return false;
	}

	/* MeshCore: a 1-byte path hash names no repeater, so it is not counted. */
	bool hopOverRate(const Prefs &p, const uint8_t *last_hop, size_t size, uint32_t now)
	{
		return size >= 2 && relayOverRate(p, last_hop, size, now);
	}
};

struct CmdResult {
	bool handled;  /* the command was a firewall one */
	bool changed;  /* a setting changed: save it */
};

/* "get|set z.mc.fw.<key>" for MeshCore, "z.mt.fw.<key>" for Meshtastic. */
inline bool isCommand(const char *command)
{
	return (strncmp(command, "get z.m", 7) == 0 || strncmp(command, "set z.m", 7) == 0) &&
	       (command[7] == 'c' || command[7] == 't') && strncmp(command + 8, ".fw.", 4) == 0;
}

/* True for the Meshtastic one. */
inline bool isMeshtastic(const char *command) { return command[7] == 't'; }

inline bool parseLimit(const char *v, uint8_t &out)
{
	if (*v == '\0') {
		return false;
	}
	unsigned n = 0;

	for (; *v; v++) {
		if (*v < '0' || *v > '9' || (n = n * 10 + (unsigned)(*v - '0')) > 255) {
			return false;
		}
	}
	out = (uint8_t)n;
	return true;
}

inline CmdResult handleDrop(Prefs &p, const Firewall &f, const Rules &rules, bool get, const char *val, char *reply,
			    size_t cap)
{
	if (get) {
		size_t used = (size_t)snprintf(reply, cap, ">");
		bool any = false;

		for (uint8_t r = 0; r < rules.count && used < cap; r++) {
			if (p.drop & (1u << r)) {
				used += (size_t)snprintf(reply + used, cap - used, " %s %u", rules.names[r],
							 (unsigned)f.counters().rule[r]);
				any = true;
			}
		}
		if (!any) {
			snprintf(reply, cap, "> none");
		}
		return {true, false};
	}
	if (strcmp(val, "none") == 0) {
		const bool changed = p.drop != 0;

		p.drop = 0;
		snprintf(reply, cap, "OK");
		return {true, changed};
	}
	char word[16];
	const char *sp = strchr(val, ' ');
	const size_t n = sp ? (size_t)(sp - val) : strlen(val);
	uint8_t on = 0;

	if (n == 0 || n >= sizeof(word) || !sp || !zcmd::parseOnOff(sp + 1, on)) {
		snprintf(reply, cap, "Error: <rule> on|off|1|0, or none");
		return {true, false};
	}
	memcpy(word, val, n);
	word[n] = '\0';
	for (uint8_t r = 0; r < rules.count; r++) {
		if (strcmp(word, rules.names[r]) == 0) {
			const uint8_t before = p.drop;

			p.drop = on ? (uint8_t)(p.drop | (1u << r)) : (uint8_t)(p.drop & ~(1u << r));
			snprintf(reply, cap, "OK");
			return {true, p.drop != before};
		}
	}
	snprintf(reply, cap, "Error: unknown rule");
	return {true, false};
}

/* The reply is always written when handled.
 *   z.<proto>.fw.gap    get|set 0-255: minutes between two adverts (NodeInfo) of one node (0 = no limit)
 *   z.<proto>.fw.rate   get|set 0-255: frames a minute from one neighbouring repeater (0 = no limit)
 *   z.<proto>.fw.drop   get: the rules on, with what each dropped; set <rule> on|off|1|0, or set none
 *   z.<proto>.fw.stats  get only: frames dropped by each limit since boot */
inline CmdResult handleCommand(Prefs &p, const Firewall &f, const Rules &rules, const char *command, char *reply,
			       size_t cap)
{
	if (!isCommand(command)) {
		return {false, false};
	}
	const bool get = command[0] == 'g';
	const char *key = command + 12;
	char name[8];
	size_t n = 0;

	while (key[n] && key[n] != ' ' && n < sizeof(name) - 1) {
		name[n] = key[n];
		n++;
	}
	name[n] = '\0';
	const char *val = key[n] == ' ' ? key + n + 1 : "";
	uint8_t *limit = strcmp(name, "gap") == 0 ? &p.advert_gap_min : strcmp(name, "rate") == 0 ? &p.hop_rate : nullptr;
	uint8_t v;

	if (strcmp(name, "drop") == 0) {
		return handleDrop(p, f, rules, get, val, reply, cap);
	}
	if (strcmp(name, "stats") == 0) {
		if (get) {
			uint32_t dropped = 0;

			for (uint32_t c : f.counters().rule) {
				dropped += c;
			}
			snprintf(reply, cap, "> gap %u, rate %u, drop %u", (unsigned)f.counters().gap, (unsigned)f.counters().rate,
				 (unsigned)dropped);
		} else {
			snprintf(reply, cap, "Error: stats is read only");
		}
		return {true, false};
	}
	if (!limit) {
		snprintf(reply, cap, "Error: unknown firewall setting");
		return {true, false};
	}
	if (get) {
		snprintf(reply, cap, "> %u", (unsigned)*limit);
		return {true, false};
	}
	if (!parseLimit(val, v)) {
		snprintf(reply, cap, "Error: 0 to 255");
		return {true, false};
	}
	const bool changed = *limit != v;

	*limit = v;
	snprintf(reply, cap, "OK");
	return {true, changed};
}

} /* namespace fw */
