/*
 * SPDX-License-Identifier: MIT
 * Zephtacore: storage and files of the rescue copy. See RescueCopy.h.
 */

#include "RescueCopy.h"
#include "IdentityFile.h"
#include "PrefsFile.h"
#include "ZephyrFsFormat.h"

#include <RegionMap.h>
#include <ZephyrFS.h>

#include <errno.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(zephcore_rescue, CONFIG_ZEPHCORE_DATASTORE_LOG_LEVEL);

void RescueStore::begin()
{
	int rc = settings_subsys_init();

	if (rc != 0) {
		LOG_WRN("settings backend failed (%d) - erasing its region", rc);
		zephcore_fs_erase_settings();
		rc = settings_subsys_init();
	}
	_ready = rc == 0;
	if (!_ready) {
		LOG_ERR("settings backend unavailable (%d) - no rescue copy", rc);
	}
}

int RescueStore::read(const char *key, void *buf, size_t cap)
{
	return _ready ? (int)settings_load_one(key, buf, cap) : -ENODEV;
}

bool RescueStore::write(const char *key, const void *data, size_t len)
{
	return _ready && settings_save_one(key, data, len) == 0;
}

void RescueStore::erase(const char *key)
{
	if (_ready) {
		settings_delete(key);
	}
}

void RescueFiles::pathOf(rescue::Item item, char (&path)[PATH_CAP]) const
{
	const char *name = item == rescue::Item::Identity ? "_main.id" :
			   item == rescue::Item::Prefs ? "prefs.json" : "regions2";

	snprintf(path, PATH_CAP, "%s/%s", _dir, name);
}

/* Each file is judged by the loader its owner uses. */
bool RescueFiles::loads(rescue::Item item)
{
	char path[PATH_CAP];

	pathOf(item, path);
	switch (item) {
	case rescue::Item::Identity: {
		mesh::LocalIdentity id;

		return zephcore_identity_load(path, id);
	}
	case rescue::Item::Prefs: {
		NodePrefs probe{};

		return zephcore_prefs_json_load(path, probe, serverPrefsFromJson);
	}
	default:
		return regionsLoad();
	}
}

bool RescueFiles::regionsLoad() const
{
	TransportKeyStore keys;
	RegionMap probe(keys);
	ZephyrFS fs(_dir);

	return probe.load(&fs);
}

size_t RescueFiles::size(rescue::Item item)
{
	char path[PATH_CAP];
	struct fs_dirent entry;

	pathOf(item, path);
	if (!zephcore_fs_exists(path) || fs_stat(path, &entry) != 0) {
		return 0;
	}
	return entry.size;
}

bool RescueFiles::read(rescue::Item item, size_t offset, uint8_t *buf, size_t len)
{
	char path[PATH_CAP];
	struct fs_file_t file;

	pathOf(item, path);
	fs_file_t_init(&file);
	if (fs_open(&file, path, FS_O_READ) != 0) {
		return false;
	}
	bool ok = fs_seek(&file, offset, FS_SEEK_SET) == 0 &&
		  fs_read(&file, buf, len) == (ssize_t)len;

	fs_close(&file);
	return ok;
}
