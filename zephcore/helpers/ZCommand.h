/*
 * SPDX-License-Identifier: MIT
 * What every "z." command shares: the on/off value, and the help that walks the "z." keys as a tree:
 * "help" lists the groups, "<get|set> z.<group> help" their children, "<get|set> z.<key> help" the usage.
 * Pure code, host tested.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace zcmd {

/* on / 1 -> 1, off / 0 -> 0. */
inline bool parseOnOff(const char *v, uint8_t &b)
{
	if (strcmp(v, "on") == 0 || strcmp(v, "1") == 0) { b = 1; return true; }
	if (strcmp(v, "off") == 0 || strcmp(v, "0") == 0) { b = 0; return true; }
	return false;
}

/* Tenths of a dB from an SNR in quarters of a dB, rounded half away from zero: 13 -> 33, -13 -> -33. */
inline int snrTenths(int8_t snr_x4)
{
	const int t = snr_x4 * 5;

	return (t + (t >= 0 ? 1 : -1)) / 2;
}

/* Bytes of s to keep when it is cut to max without splitting a UTF-8 character. */
inline size_t utf8Cut(const char *s, size_t max)
{
	size_t n = strlen(s);

	if (n > max) {
		n = max;
		while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) {
			n--;
		}
	}
	return n;
}

/* "<get|set> z.<key> [value]" taken apart: the key after "z." and, when it starts with "mc." or
 * "mt.", which protocol and the key after that. */
struct Parsed {
	bool ok = false;      /* the line starts with "get z." or "set z." */
	bool get = false;
	char proto = 0;       /* 'c' MeshCore, 't' Meshtastic, 0 for a key of neither */
	const char *key = ""; /* what follows, up to the end of the line */
};

inline Parsed parse(const char *command)
{
	Parsed p;

	if (strncmp(command, "get z.", 6) == 0) {
		p.get = true;
	} else if (strncmp(command, "set z.", 6) != 0) {
		return p;
	}
	p.ok = true;
	p.key = command + 6;
	if (p.key[0] == 'm' && (p.key[1] == 'c' || p.key[1] == 't') && p.key[2] == '.') {
		p.proto = p.key[1];
		p.key += 3;
	}
	return p;
}

/* "<name> <value>": name takes the first word of key (at most cap - 1 bytes), the value is what follows
 * its space, "" when there is none. */
inline const char *splitValue(const char *key, char *name, size_t cap)
{
	size_t n = 0;

	while (key[n] && key[n] != ' ' && n < cap - 1) {
		name[n] = key[n];
		n++;
	}
	name[n] = '\0';
	return key[n] == ' ' ? key + n + 1 : "";
}

struct Help {
	const char *key;   /* after "z." */
	const char *text;  /* usage: syntax, what it does, default */
	bool free_text;    /* "set" takes any text, so "set <key> help" sets it */
};

/* Every key of the tree, in the order the groups list them. A key that is also a group ("mt.relay" with
 * "mt.relay.opaque") says so in its usage. */
inline constexpr Help kHelp[] = {
#ifdef CONFIG_ZEPHTACORE_MESHTASTIC
	{"mt.enable", "get|set on|off|1|0: Meshtastic side on or off, default on"},
	{"mt.role", "get|set client|client_base|router|router_late|repeater (companion: client_mute|client): role; default client_base, companion client_mute"},
	{"mt.relay", "get|set off|essential|all: what is repeated, default essential (companion off)"},
	{"mt.relay.opaque", "get|set on|off|1|0: also repeat channels we hold no key for, default on"},
	{"mt.hops", "get|set 0-7: hop_limit of the frames we send, default 3 (7 on the Zephtacore build)"},
	{"mt.zerohop", "get|set off|auto: keep hop_limit of what a fixed neighbour repeated, default off"},
	{"mt.zerohop.add", "set <node>: add a fixed neighbour, 8 hex digits, at most 8 nodes"},
	{"mt.zerohop.remove", "set <node>: remove a fixed neighbour, 8 hex digits"},
	{"mt.zerohop.clear", "set: remove all the fixed neighbours"},
	{"mt.nodeinfo", "get|set 0 or 600-604800: NodeInfo interval in seconds, 0 never, default 43200"},
	{"mt.telemetry", "get|set 0 or 60-86400: device metrics interval in seconds, 0 never, default 0 (repeater)"},
	{"mt.channel", "get|set <name> [default|<base64 key>]: primary channel (repeater); default: no name, default key", true},
	{"mt.channel.add", "set <name> [default|<base64 key>]: add a channel (companion)", true},
	{"mt.channel.remove", "set <name>: remove a channel (companion)", true},
	{"mt.channels", "get: the channels and their keys (companion)"},
	{"mt.stats", "get: heard, relayed, opaque, dropped, shed, long, kept"},
	{"mt.neighbors", "get [page] [recent]: Meshtastic nodes heard directly, best SNR first"},
	{"mt.ping.enable", "get|set on [<command>]|off|1 [<command>]|0: bot on (needs a command) or off, default off"},
	{"mt.ping.trigger", "get|set <command>: text to answer, 1-15 characters, no spaces, default none", true},
	{"mt.ping.status", "get: state, command, answered and ignored"},
	{"mt.hopstats", "get: frames heard by hops crossed, ? when the sender set no hop_start (repeater)"},
	{"mt.relays", "get: relay byte of the frames heard, most frames first (repeater)"},
	{"mt.fw.drop", "get|set <rule> on|off|1|0 or none: ports dropped: text position nodeinfo telemetry traceroute other, default none (repeater)"},
	{"mt.fw.gap", "get|set 0-255: minutes between two NodeInfo of one node that are kept, default 0 no limit (repeater)"},
	{"mt.fw.rate", "get|set 0-255: frames a minute from one neighbouring repeater, default 0 no limit (repeater)"},
	{"mt.fw.stats", "get: frames dropped by each limit since boot (repeater)"},
#endif
	{"mc.neighbors", "get [page] [recent]: MeshCore repeaters heard directly, best SNR first"},
	{"mc.ping.enable", "get|set on [<command>]|off|1 [<command>]|0: bot on (needs a command) or off, default off"},
	{"mc.ping.trigger", "get|set <command>: text to answer, 1-15 characters, no spaces, default none", true},
	{"mc.ping.status", "get: state, command, answered and ignored"},
	{"mc.hopstats", "get: flood frames heard by hops crossed, 7+ at the end (repeater)"},
	{"mc.relays", "get: last-hop repeater of the flood frames heard, most frames first (repeater)"},
	{"mc.fw.drop", "get|set <rule> on|off|1|0 or none: payload types dropped: advert request text group ack path trace, default none (repeater)"},
	{"mc.fw.gap", "get|set 0-255: minutes between two adverts of one key that are kept, default 0 no limit (repeater)"},
	{"mc.fw.rate", "get|set 0-255: flood frames a minute from one neighbouring repeater, default 0 no limit (repeater)"},
	{"mc.fw.stats", "get: frames dropped by each limit since boot (repeater)"},
	{"ping.snr", "get|set on|off|1|0: show the SNR in the answer, default on"},
	{"ping.rssi", "get|set on|off|1|0: show the RSSI in the answer, default off"},
	{"ping.ignore.direct", "get|set on|off|1|0: do not answer pings heard with 0 hops, default off"},
	{"ping.ignore.remote", "get|set on|off|1|0: do not answer pings heard with 1 or more hops, default off"},
	{"autoshutdown", "get|set 0 or 2900-4200 mV: power off under this battery voltage, 0 off, wakes on recovery; default 0 (companion nRF52: 3200)"},
	{"info", "get: version, uptime, battery, flash, heap and contacts"},
	{"stats", "get: MeshCore counters, then the Meshtastic ones"},
	{"rescue", "get: the copy of identity, settings and regions, and files restored (repeater, room server)"},
	{"tx.stalls", "get: times TX could not go out for 4 s, per cause"},
};

inline constexpr size_t kHelpCount = sizeof(kHelp) / sizeof(kHelp[0]);

/* The Meshtastic keys exist only in builds that have it. */
inline bool helpListed(const Help &h, bool meshtastic)
{
	return meshtastic || strncmp(h.key, "mt.", 3) != 0;
}

/* Whether h is listed under the group key[0..n), n 0 for the root (every listed key). */
inline bool underGroup(const Help &h, bool meshtastic, const char *key, size_t n)
{
	if (!helpListed(h, meshtastic)) {
		return false;
	}
	return n == 0 || (strncmp(h.key, key, n) == 0 && h.key[n] == '.');
}

/* What h.key is after the group key[0..n) and its dot, n 0 for the root. */
inline const char *childOf(const Help &h, size_t n)
{
	return h.key + (n ? n + 1 : 0);
}

/* Length of the first dot-separated word of s. */
inline size_t wordLen(const char *s)
{
	size_t n = 0;

	while (s[n] && s[n] != '.') {
		n++;
	}
	return n;
}

inline bool hasKeysUnder(const char *key, size_t n, bool meshtastic)
{
	for (const Help &h : kHelp) {
		if (underGroup(h, meshtastic, key, n)) {
			return true;
		}
	}
	return false;
}

/* What snprintf left in a buffer of cap bytes, not the length it would have had. */
inline size_t written(int n, size_t cap)
{
	return (size_t)n < cap ? (size_t)n : cap - 1;
}

/* Appends the children of the group key[0..n) (n 0: the root) to reply, which holds used bytes: each
 * word once, in table order, with "/" after one that has children. lead goes before the first word.
 * Stops before a word that does not fit; returns the bytes used. */
inline size_t appendChildren(char *reply, size_t cap, size_t used, const char *key, size_t n,
			     bool meshtastic, const char *lead)
{
	bool first = true;

	for (size_t i = 0; i < kHelpCount; i++) {
		if (!underGroup(kHelp[i], meshtastic, key, n)) {
			continue;
		}
		const char *child = childOf(kHelp[i], n);
		const size_t len = wordLen(child);
		bool seen = false;
		bool more = false;

		for (size_t j = 0; j < kHelpCount; j++) {
			if (!underGroup(kHelp[j], meshtastic, key, n)) {
				continue;
			}
			const char *other = childOf(kHelp[j], n);

			if (wordLen(other) != len || strncmp(other, child, len) != 0) {
				continue;
			}
			seen = seen || j < i;
			more = more || other[len] == '.';
		}
		if (seen) {
			continue;
		}
		const char *pre = first ? lead : "";
		const size_t add = strlen(pre) + 1 + len + (more ? 1 : 0);

		if (used + add >= cap) {
			break;
		}
		used += (size_t)snprintf(reply + used, cap - used, "%s %.*s%s", pre, (int)len, child,
					 more ? "/" : "");
		first = false;
	}
	return used;
}

/* "help" or "?" at the top: the groups of z., and what each one holds, written after lead at used.
 * Returns the bytes used. */
inline size_t appendGroups(char *reply, size_t cap, size_t used, const char *lead, bool meshtastic)
{
	used += written(snprintf(reply + used, cap - used, "%sz. groups:", lead), cap - used);
	return appendChildren(reply, cap, used, "", 0, meshtastic, "");
}

/* "<get|set> z.<key> help": the usage of a key, or the children of a group ("get z.mt help").
 * False, with reply untouched, when the command is not that, so it goes on to its own handler.
 * "set" leaves a key that takes any text alone: "help" is a valid value for it. */
inline bool helpReply(const char *command, bool meshtastic, char *reply, size_t cap)
{
	const Parsed parsed = parse(command);

	if (!parsed.ok) {
		return false;
	}
	const bool get = parsed.get;
	const char *key = command + 6;
	size_t n = strlen(key);

	while (n > 0 && key[n - 1] == ' ') {
		n--;
	}
	if (n < 5 || strncmp(key + n - 4, "help", 4) != 0 || key[n - 5] != ' ') {
		return false;
	}
	n -= 5;
	while (n > 0 && key[n - 1] == ' ') {
		n--;
	}
	if (n == 0) {
		return false;
	}

	for (const Help &h : kHelp) {
		if (helpListed(h, meshtastic) && strlen(h.key) == n && strncmp(h.key, key, n) == 0) {
			if (!get && h.free_text) {
				return false;
			}
			size_t used = written(snprintf(reply, cap, "> z.%s: %s", h.key, h.text), cap);

			if (hasKeysUnder(h.key, n, meshtastic)) {
				appendChildren(reply, cap, used, h.key, n, meshtastic, " | more:");
			}
			return true;
		}
	}
	if (!hasKeysUnder(key, n, meshtastic)) {
		return false;
	}
	const size_t used = written(snprintf(reply, cap, "> z.%.*s:", (int)n, key), cap);

	appendChildren(reply, cap, used, key, n, meshtastic, "");
	return true;
}

} /* namespace zcmd */
