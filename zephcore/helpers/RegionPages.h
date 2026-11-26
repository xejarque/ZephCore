/*
 * SPDX-License-Identifier: MIT
 * Zephtacore: "get z.mc.regions [page]", the region export cut into replies of
 * whole lines. Pure code, host tested.
 */

#pragma once

#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace region_pages {

static constexpr const char *kCommand = "get z.mc.regions";
static constexpr size_t kLineMax = 72;   /* indent, name, "^ F" and the newline of the deepest region */
static constexpr size_t kTitleMax = 20;  /* "Regions 99/99" and longer page-past-the-last titles */

/* The page asked for: 1 without one, 0 when it is not a plain number of up to
 * 4 digits. False when command is not this command. */
inline bool parseArgs(const char *command, size_t &page)
{
	const size_t n = strlen(kCommand);

	if (strncmp(command, kCommand, n) != 0 || (command[n] != '\0' && command[n] != ' ')) {
		return false;
	}
	const char *arg = command + n;

	while (*arg == ' ') {
		arg++;
	}
	if (*arg == '\0') {
		page = 1;
		return true;
	}
	page = 0;
	size_t digits = 0;

	for (; arg[digits] >= '0' && arg[digits] <= '9' && digits < 4; digits++) {
		page = page * 10 + (size_t)(arg[digits] - '0');
	}
	if (arg[digits] != '\0') {
		page = 0;
	}
	return true;
}

/* Fed the export byte by byte, keeps the lines of one page: as many whole lines
 * as fit in cap after the title. finish() writes "Regions <page>/<pages>" and a
 * newline before each line, or that there is no such page. */
class Pager {
public:
	Pager(char *out, size_t cap, size_t page) : _out(out), _cap(cap), _page(page) {}

	void put(char c)
	{
		if (c != '\n') {
			if (_len < sizeof(_line)) {
				_line[_len++] = c;
			}
			return;
		}
		const size_t need = _len + 1;

		if (_used != 0 && _used + need > room()) {
			_pages++;
			_used = 0;
		}
		_used += need;
		if (_pages == _page && kTitleMax + _body + need < _cap) {
			char *at = _out + kTitleMax + _body;

			at[0] = '\n';
			memcpy(at + 1, _line, _len);
			_body += need;
		}
		_len = 0;
	}

	void finish()
	{
		if (_page < 1 || _page > _pages) {
			snprintf(_out, _cap, "Regions: page %u of %u", (unsigned)_page, (unsigned)_pages);
			return;
		}
		char title[kTitleMax];
		const size_t n = (size_t)snprintf(title, sizeof(title), "Regions %u/%u", (unsigned)_page,
						  (unsigned)_pages);

		memmove(_out + n, _out + kTitleMax, _body);
		memcpy(_out, title, n);
		_out[n + _body] = '\0';
	}

private:
	size_t room() const { return _cap - kTitleMax - 1; }

	char *_out;
	const size_t _cap;
	const size_t _page;
	char _line[kLineMax];
	size_t _len = 0;
	size_t _used = 0;
	size_t _body = 0;
	size_t _pages = 1;
};

}  // namespace region_pages
