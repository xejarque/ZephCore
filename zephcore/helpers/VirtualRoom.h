/*
 * SPDX-License-Identifier: MIT
 * VirtualRoom - pure logic for the virtual Room Server inside RepeaterMesh.
 *
 * Host-tested, zero Zephyr dependencies so NodePrefs.h stays lightweight.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ZCommand.h"

namespace vroom {

enum AccessMode : uint8_t {
	ACCESS_OPEN = 0,
	ACCESS_READ_ONLY = 1,
	ACCESS_PRIVATE = 2,
};

/* Mirror ClientACL.h role bits without pulling Mesh.h into NodePrefs.h. */
static constexpr uint8_t ROLE_MASK = 3;
static constexpr uint8_t ROLE_GUEST = 0;
static constexpr uint8_t ROLE_READ_ONLY = 1;
static constexpr uint8_t ROLE_READ_WRITE = 2;
static constexpr uint8_t ROLE_ADMIN = 3;

static constexpr size_t ADVERT_NAME_MAX = 31;

struct Config {
	uint8_t enabled;
	uint8_t access;
	char password[16];
};

inline void init(Config& c)
{
	c.enabled = 0;
	c.access = ACCESS_OPEN;
	memset(c.password, 0, sizeof(c.password));
}

inline void sanitize(Config& c)
{
	if (c.enabled > 1) {
		c.enabled = 0;
	}
	if (c.access > ACCESS_PRIVATE) {
		c.access = ACCESS_OPEN;
	}
	c.password[sizeof(c.password) - 1] = '\0';
}

inline bool parseAccess(const char* s, uint8_t& out)
{
	if (!s) {
		return false;
	}
	if (strcmp(s, "open") == 0) {
		out = ACCESS_OPEN;
		return true;
	}
	if (strcmp(s, "readonly") == 0 || strcmp(s, "ro") == 0) {
		out = ACCESS_READ_ONLY;
		return true;
	}
	if (strcmp(s, "private") == 0 || strcmp(s, "priv") == 0) {
		out = ACCESS_PRIVATE;
		return true;
	}
	return false;
}

inline const char* accessName(uint8_t a)
{
	switch (a) {
	case ACCESS_READ_ONLY:
		return "readonly";
	case ACCESS_PRIVATE:
		return "private";
	default:
		return "open";
	}
}

/* Derived advert name "<node_name> Room" / " Room [RO]" / " Room [Priv]",
 * capped to min(cap - 1, ADVERT_NAME_MAX) while preserving the full suffix
 * and never splitting a UTF-8 codepoint inside node_name. */
inline size_t formatRoomName(const char* node_name, uint8_t access, char* out, size_t cap)
{
	if (!out || cap == 0) {
		return 0;
	}
	const char* suffix = " Room";
	if (access == ACCESS_READ_ONLY) {
		suffix = " Room [RO]";
	} else if (access == ACCESS_PRIVATE) {
		suffix = " Room [Priv]";
	}

	const size_t max_total = (cap - 1 < ADVERT_NAME_MAX) ? (cap - 1) : ADVERT_NAME_MAX;
	const size_t suf_len = strlen(suffix);

	if (max_total <= suf_len) {
		const size_t n = zcmd::utf8Cut(suffix, max_total);
		memcpy(out, suffix, n);
		out[n] = '\0';
		return n;
	}

	const char* base = (node_name && node_name[0]) ? node_name : "Node";
	const size_t base_budget = max_total - suf_len;
	const size_t base_len = zcmd::utf8Cut(base, base_budget);

	memcpy(out, base, base_len);
	memcpy(out + base_len, suffix, suf_len);
	out[base_len + suf_len] = '\0';
	return base_len + suf_len;
}

inline bool resolveLoginPerms(const Config& cfg, bool admin_match, bool room_pw_match, uint8_t& perms_out)
{
	if (admin_match) {
		perms_out = ROLE_ADMIN;
		return true;
	}
	if (cfg.password[0] != '\0' && room_pw_match) {
		perms_out = ROLE_READ_WRITE;
		return true;
	}
	if (cfg.access == ACCESS_OPEN) {
		perms_out = ROLE_READ_WRITE;
		return true;
	}
	if (cfg.access == ACCESS_READ_ONLY) {
		perms_out = ROLE_GUEST;
		return true;
	}
	return false;
}

inline bool canPost(uint8_t perms)
{
	return (perms & ROLE_MASK) >= ROLE_READ_WRITE;
}

/* Role of a known client logging in again with a blank password: an open room
 * lifts a guest to read-write; any other mode keeps the stored role. */
inline uint8_t blankReloginPerms(const Config& cfg, uint8_t perms)
{
	if (cfg.access != ACCESS_OPEN || canPost(perms)) {
		return perms;
	}
	return (uint8_t)((perms & ~ROLE_MASK) | ROLE_READ_WRITE);
}

static constexpr const char* READ_ONLY_REPLY = "Error: room is read-only";

inline bool isValidSecondaryHash(const uint8_t* primary_pub, const uint8_t* room_pub)
{
	return primary_pub && room_pub && primary_pub[0] != room_pub[0];
}

}  // namespace vroom
