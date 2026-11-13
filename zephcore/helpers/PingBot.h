/*
 * SPDX-License-Identifier: MIT
 * Ping bot: answers a trigger text on its fixed channels with how the frame
 * was heard and the route it took. Pure code, host tested; persistence is
 * prefs.json's zc{}.
 */

#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ZCommand.h"

namespace ping {

static constexpr size_t kTriggerMax = 15;
static constexpr uint16_t kAnswersPerWindow = 6;  /* answers one protocol gives ... */
static constexpr uint32_t kAnswerWindowS = 60;    /* ... in this many seconds */
static constexpr size_t kNameMax = 14;  /* bytes of a node name in an answer */

/* The only channels the bot answers on. MeshCore: a hashtag channel, whose key is the
 * first 16 bytes of SHA256 of its name. Meshtastic: this name with its default key
 * variant 2 (PSK "Ag=="). */
#define PING_MC_CHANNEL "#bot"
#define PING_MT_CHANNEL "Bots"
static constexpr uint8_t kMtPskIndex = 2;

/* Before each hop of a MeshCore route (U+2193). */
static constexpr const char *kArrow = "\xE2\x86\x93";

enum Protocol : uint8_t { PROTO_MC = 0, PROTO_MT = 1 };

/* What each protocol has of its own. */
struct Proto {
	uint8_t enable;                /* only with a trigger set */
	char trigger[kTriggerMax + 1]; /* empty until the operator picks one */
};

/* Plain data: NodePrefs holds one and is cleared with memset. */
struct Prefs {
	Proto mc;
	Proto mt;
	uint8_t rssi;           /* show RSSI */
	uint8_t snr;            /* show SNR */
	uint8_t ignore_direct;  /* do not answer a ping heard with 0 hops */
	uint8_t ignore_remote;  /* do not answer a ping heard with more than 0 hops */
};

/* Since boot; not persisted. */
struct Counters {
	uint16_t answered;
	uint16_t ignored;  /* a ping the hop filters or the answer limit turned away */
};
struct Stats {
	Counters mc;
	Counters mt;
};

inline Proto &protoOf(Prefs &p, Protocol w) { return w == PROTO_MC ? p.mc : p.mt; }
inline const Proto &protoOf(const Prefs &p, Protocol w) { return w == PROTO_MC ? p.mc : p.mt; }
inline Counters &countersOf(Stats &s, Protocol w) { return w == PROTO_MC ? s.mc : s.mt; }
inline const Counters &countersOf(const Stats &s, Protocol w) { return w == PROTO_MC ? s.mc : s.mt; }

inline bool validTrigger(const char *s)
{
	size_t n = strlen(s);

	if (n == 0 || n > kTriggerMax) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		if ((uint8_t)s[i] <= 0x20 || (uint8_t)s[i] > 0x7E) {
			return false;
		}
	}
	return true;
}

inline void setTrigger(Proto &x, const char *s)
{
	strncpy(x.trigger, s, sizeof(x.trigger) - 1);
	x.trigger[sizeof(x.trigger) - 1] = '\0';
}

inline void sanitize(Proto &x)
{
	x.enable = x.enable > 1 ? 1 : x.enable;
	x.trigger[kTriggerMax] = '\0';
	if (!validTrigger(x.trigger)) {
		x.trigger[0] = '\0';
		x.enable = 0;
	}
}

/* Puts every field back in range. */
inline void sanitize(Prefs &p)
{
	sanitize(p.mc);
	sanitize(p.mt);
	p.rssi = p.rssi > 1 ? 1 : p.rssi;
	p.snr = p.snr > 1 ? 1 : p.snr;
	p.ignore_direct = p.ignore_direct > 1 ? 1 : p.ignore_direct;
	p.ignore_remote = p.ignore_remote > 1 ? 1 : p.ignore_remote;
}

/* Off, no trigger, answering with the SNR to a ping at any distance. */
inline void setDefaults(Prefs &p)
{
	memset(&p, 0, sizeof(p));
	p.snr = 1;
}

/* Who relayed the frame, as the packet gives it. */
struct Route {
	const uint8_t *hash;  /* MeshCore: every hop, the first repeater after the sender first. Meshtastic: the last relay. */
	uint8_t size;         /* bytes per hash */
	uint8_t count;        /* hashes at hash */
};

/* What the radio and the packet say about the frame that carried the text. */
struct Heard {
	uint8_t hops;
	int8_t snr_x4;
	int16_t rssi;
	Route route;
	bool relayed = false;  /* hops is 0 only because the sender set no hop_start; the frame came through a relay */
	bool plain = false;    /* the line is the bare text, with no "<sender>: " */
};

/* The name of the node behind a hash: 0 unknown, 1 known, 2 known among several that share it. */
typedef int (*NameFn)(void *ctx, Protocol w, const uint8_t *hash, uint8_t size, char *out, size_t cap);

/* A channel line is "<sender>: <text>". */
inline const char *messageOf(const char *line, char *sender, size_t cap)
{
	const char *sep = strstr(line, ": ");

	if (!sep) {
		if (cap) {
			sender[0] = '\0';
		}
		return line;
	}
	size_t n = (size_t)(sep - line);

	if (n >= cap) {
		n = cap ? cap - 1 : 0;
	}
	if (cap) {
		memcpy(sender, line, n);
		sender[n] = '\0';
	}
	return sep + 2;
}

inline bool sameText(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
		char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;

		if (x != y) {
			return false;
		}
	}
	return *a == '\0' && *b == '\0';
}

/* Appends to out, never past cap - 1. */
inline void append(char *out, size_t cap, size_t &n, const char *fmt, ...)
{
	if (n >= cap) {
		return;
	}
	va_list ap;

	va_start(ap, fmt);
	int w = vsnprintf(out + n, cap - n, fmt, ap);
	va_end(ap);
	if (w > 0) {
		n = (size_t)w < cap - n ? n + (size_t)w : cap - 1;
	}
}

/* The label of hop i: its name (cut to kNameMax bytes without splitting a UTF-8 character, a ? when
 * several nodes share the hash) or, unknown, the hash in hex. */
inline void hopLabel(const Route &r, uint8_t i, Protocol w, NameFn names, void *ctx, char *out, size_t cap)
{
	const uint8_t *h = r.hash + (size_t)i * r.size;
	char name[40] = {0};
	int found = names ? names(ctx, w, h, r.size, name, sizeof(name)) : 0;

	if (found && name[0]) {
		size_t n = strlen(name);

		if (n > kNameMax) {
			n = kNameMax;
			while (n > 0 && ((uint8_t)name[n] & 0xC0) == 0x80) {
				n--;
			}
		}
		snprintf(out, cap, "%.*s%s", (int)n, name, found == 2 ? "?" : "");
		return;
	}
	size_t o = 0;

	out[0] = '\0';
	for (uint8_t b = 0; b < r.size; b++) {
		append(out, cap, o, "%02x", h[b]);
	}
}

inline void bump(uint16_t &c)
{
	if (c < 0xFFFF) {
		c++;
	}
}

/* The answer to a line heard on the bot's channel for protocol w, or 0 when it is not a ping this node answers.
 * MeshCore, one node per line, the signal (RSSI/SNR) stuck to the last one:
 *   @[Ana] 2 hops
 *   <arrow>Montjuic
 *   <arrow>3f9a(-88/+3.3)
 *   @[Ana] direct (-88/+3.3)
 * Meshtastic, which carries only the last relay:
 *   @[Ana] 2 hops, last Montjuic, SNR 7.5 dB */
inline size_t reply(const Prefs &p, Counters &c, Protocol w, const char *line, const Heard &h, NameFn names,
		    void *ctx, char *out, size_t cap)
{
	const Proto &x = protoOf(p, w);

	if (!x.enable || !validTrigger(x.trigger) || cap == 0) {
		return 0;
	}
	char sender[32];
	const char *msg = line;

	sender[0] = '\0';
	if (!h.plain) {
		msg = messageOf(line, sender, sizeof(sender));
	}
	if (!sameText(msg, x.trigger)) {
		return 0;
	}
	const bool direct = h.hops == 0 && !h.relayed;

	if ((direct && p.ignore_direct) || (!direct && p.ignore_remote)) {
		bump(c.ignored);
		return 0;
	}
	const bool mc = w == PROTO_MC;
	const int snr10 = h.snr_x4 * 10 / 4;  /* tenths of a dB, truncated toward zero */
	const int snr_abs = snr10 < 0 ? -snr10 : snr10;
	char tail[40];
	size_t t = 0;

	tail[0] = '\0';
	if (mc && (p.snr || p.rssi)) {
		append(tail, sizeof(tail), t, "(");
		if (p.rssi) {
			append(tail, sizeof(tail), t, "%d", h.rssi);
		}
		if (p.rssi && p.snr) {
			append(tail, sizeof(tail), t, "/");
		}
		if (p.snr) {
			append(tail, sizeof(tail), t, "%c%d.%d", snr10 < 0 ? '-' : '+', snr_abs / 10, snr_abs % 10);
		}
		append(tail, sizeof(tail), t, ")");
	} else if (!mc) {
		if (p.snr) {
			append(tail, sizeof(tail), t, ", SNR %s%d.%d dB", snr10 < 0 ? "-" : "", snr_abs / 10, snr_abs % 10);
		}
		if (p.rssi) {
			append(tail, sizeof(tail), t, ", RSSI %d dBm", h.rssi);
		}
	}
	size_t n = 0;
	bool listed = false;  /* the tail follows a hop, with no space */

	out[0] = '\0';
	if (sender[0]) {
		append(out, cap, n, "@[%s] ", sender);
	}
	if (direct) {
		append(out, cap, n, "direct");
	} else {
		if (h.hops == 0) {
			append(out, cap, n, "relayed");
		} else {
			append(out, cap, n, "%u hop%s", h.hops, h.hops == 1 ? "" : "s");
		}
		if (h.route.hash && h.route.count > 0 && h.route.size > 0) {
			if (!mc) {
				char label[48];

				hopLabel(h.route, (uint8_t)(h.route.count - 1), w, names, ctx, label, sizeof(label));
				append(out, cap, n, ", last %s", label);
			} else {
				listed = true;
				for (uint8_t i = 0; i < h.route.count; i++) {
					char label[48];

					hopLabel(h.route, i, w, names, ctx, label, sizeof(label));
					const size_t need = 1 + strlen(kArrow) + strlen(label);
					const size_t more = i + 1 < h.route.count ? 3 : 0;  /* room for "\n.." */

					if (n + need + more + t >= cap) {
						append(out, cap, n, "\n..");
						break;
					}
					append(out, cap, n, "\n%s%s", kArrow, label);
				}
			}
		}
	}
	if (mc && !listed && tail[0]) {
		append(out, cap, n, " ");
	}
	append(out, cap, n, "%s", tail);
	bump(c.answered);
	return n;
}

struct CmdResult {
	bool handled;  /* the command was a ping one */
	bool changed;  /* a setting changed: save it */
};

/* "get|set z.mc.ping.<key>", "get|set z.mt.ping.<key>" and "get|set z.ping.<key>". */
inline bool isCommand(const char *command)
{
	if (strncmp(command, "get z.", 6) != 0 && strncmp(command, "set z.", 6) != 0) {
		return false;
	}
	const char *rest = command + 6;

	return strncmp(rest, "ping.", 5) == 0 || strncmp(rest, "mc.ping.", 8) == 0 || strncmp(rest, "mt.ping.", 8) == 0;
}

inline bool boolOf(const char *v, uint8_t &b)
{
	return zcmd::parseOnOff(v, b);
}

/* The reply is always written when handled.
 *   z.<proto>.ping.enable        get; set on <command> | on | off, or 1/0 ("on" alone needs a command already set)
 *   z.<proto>.ping.trigger       the command
 *   z.<proto>.ping.status        get only: state, command and counters
 *   z.ping.rssi, .snr, .ignore.direct, .ignore.remote   on|off|1|0, shared by both protocols */
inline CmdResult handleCommand(Prefs &p, const Stats &st, const char *command, char *reply, size_t cap)
{
	if (!isCommand(command)) {
		return {false, false};
	}
	const bool get = command[0] == 'g';
	const char *rest = command + 6;
	bool shared = false;
	Protocol w = PROTO_MC;
	const char *key;

	if (strncmp(rest, "ping.", 5) == 0) {
		shared = true;
		key = rest + 5;
	} else {
		w = rest[1] == 'c' ? PROTO_MC : PROTO_MT;
		key = rest + 8;
	}
	char name[16];
	size_t n = 0;

	while (key[n] && key[n] != ' ' && n < sizeof(name) - 1) {
		name[n] = key[n];
		n++;
	}
	name[n] = '\0';
	const char *val = key[n] == ' ' ? key + n + 1 : "";
	uint8_t b;
	bool changed = false;

	if (shared) {
		uint8_t *flag = nullptr;

		if (strcmp(name, "rssi") == 0) { flag = &p.rssi; }
		else if (strcmp(name, "snr") == 0) { flag = &p.snr; }
		else if (strcmp(name, "ignore.direct") == 0) { flag = &p.ignore_direct; }
		else if (strcmp(name, "ignore.remote") == 0) { flag = &p.ignore_remote; }
		if (!flag) {
			snprintf(reply, cap, "Error: unknown ping setting");
		} else if (get) {
			snprintf(reply, cap, "> %s", *flag ? "on" : "off");
		} else if (boolOf(val, b)) {
			changed = *flag != b;
			*flag = b;
			snprintf(reply, cap, "OK");
		} else {
			snprintf(reply, cap, "Error: on, off, 1 or 0");
		}
		return {true, changed};
	}
	Proto &x = protoOf(p, w);

	if (strcmp(name, "status") == 0) {
		const Counters &c = countersOf(st, w);

		if (get) {
			snprintf(reply, cap, "> %s %s | answered %u, ignored %u", x.enable ? "on" : "off",
				 x.trigger[0] ? x.trigger : "(no command)", c.answered, c.ignored);
		} else {
			snprintf(reply, cap, "Error: status is read only");
		}
	} else if (strcmp(name, "trigger") == 0) {
		if (get) {
			snprintf(reply, cap, "> %s", x.trigger);
		} else if (validTrigger(val)) {
			changed = strcmp(x.trigger, val) != 0;
			setTrigger(x, val);
			snprintf(reply, cap, "OK");
		} else {
			snprintf(reply, cap, "Error: 1 to %u characters, no spaces", (unsigned)kTriggerMax);
		}
	} else if (strcmp(name, "enable") == 0) {
		if (get) {
			snprintf(reply, cap, "> %s", x.enable ? "on" : "off");
			return {true, false};
		}
		const char *sp = strchr(val, ' ');
		const size_t sn = sp ? (size_t)(sp - val) : strlen(val);
		const char *trig = sp ? sp + 1 : "";
		char word[4] = "";
		uint8_t v = 0;
		const bool is_bool = sn < sizeof(word) && (memcpy(word, val, sn), boolOf(word, v));
		const bool is_on = is_bool && v;
		const bool is_off = is_bool && !v;

		while (*trig == ' ') {
			trig++;
		}
		if ((!is_on && !is_off) || (is_off && *trig)) {
			snprintf(reply, cap, "Error: on <command>, off, 1 <command> or 0");
		} else if (is_on && *trig && !validTrigger(trig)) {
			snprintf(reply, cap, "Error: command is 1 to %u characters, no spaces", (unsigned)kTriggerMax);
		} else if (is_on && !*trig && !validTrigger(x.trigger)) {
			snprintf(reply, cap, "Error: say the command to answer to: set z.%s.ping.enable on <command>",
				 w == PROTO_MC ? "mc" : "mt");
		} else {
			b = is_on ? 1 : 0;
			if (is_on && *trig) {
				changed = strcmp(x.trigger, trig) != 0;
				setTrigger(x, trig);
			}
			changed = changed || x.enable != b;
			x.enable = b;
			snprintf(reply, cap, "OK");
		}
	} else {
		snprintf(reply, cap, "Error: unknown ping setting");
	}
	return {true, changed};
}

} /* namespace ping */
