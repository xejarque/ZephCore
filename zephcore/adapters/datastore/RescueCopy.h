/*
 * SPDX-License-Identifier: MIT
 * Zephtacore: where the rescue copy of a server node lives (ADR 0021): the
 * settings NVS as its store and the role's files on LittleFS. The rules are in
 * helpers/RescuePolicy.h.
 */

#pragma once

#include <RescuePolicy.h>
#include "ZephyrFsUtil.h"

class RescueStore {
public:
	/* Starts the settings backend. One that does not start has its region
	 * erased once: it may hold another firmware's bytes. */
	void begin();

	int read(const char *key, void *buf, size_t cap);
	bool write(const char *key, const void *data, size_t len);
	void erase(const char *key);

private:
	bool _ready = false;
};

class RescueFiles {
public:
	/* dir holds the role's files, e.g. "/lfs/repeater". */
	explicit RescueFiles(const char *dir) : _dir(dir) {}

	bool loads(rescue::Item item);
	size_t size(rescue::Item item);
	bool read(rescue::Item item, size_t offset, uint8_t *buf, size_t len);

	template <class Fill>
	bool replace(rescue::Item item, Fill fill)
	{
		char path[PATH_CAP];

		pathOf(item, path);
		return zephcore_fs_atomic_write(path, writeWith<Fill>, &fill, "rescue");
	}

private:
	static constexpr size_t PATH_CAP = 48;

	struct FileSink {
		struct fs_file_t *file;

		bool append(const uint8_t *data, size_t len)
		{
			return fs_write(file, data, len) == (ssize_t)len;
		}
	};

	template <class Fill>
	static bool writeWith(struct fs_file_t *file, void *fill)
	{
		FileSink sink{file};

		return (*static_cast<Fill *>(fill))(sink);
	}

	void pathOf(rescue::Item item, char (&path)[PATH_CAP]) const;
	bool regionsLoad() const;

	const char *_dir;
};

using RescueCopy = rescue::Copy<RescueStore, RescueFiles>;
