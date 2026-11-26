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
#include <string.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(zephcore_rescue, CONFIG_ZEPHCORE_DATASTORE_LOG_LEVEL);

namespace {

/* An entry read back as a Stream, chunk by chunk. */
class EntryStream : public Stream {
public:
	EntryStream(rescue::Copy<RescueStore, RescueFiles> &copy, rescue::Item item)
		: _copy(copy), _item(item) {}

	int available() override { return fill() ? (int)(_len - _pos) : 0; }
	int read() override { return fill() ? _data[_pos++] : -1; }
	int peek() override { return fill() ? _data[_pos] : -1; }
	size_t write(uint8_t) override { return 0; }

private:
	bool fill()
	{
		if (_pos < _len) {
			return true;
		}
		_data = _copy.chunk(_item, _next, _len);
		_pos = 0;
		if (_data == nullptr) {
			_len = 0;
			return false;
		}
		_next++;
		return true;
	}

	rescue::Copy<RescueStore, RescueFiles> &_copy;
	const rescue::Item _item;
	const uint8_t *_data = nullptr;
	size_t _len = 0;
	size_t _pos = 0;
	size_t _next = 0;
};

/* The bytes of an identity file, gathered in RAM. */
struct IdentityBytes {
	uint8_t data[PRV_KEY_SIZE + PUB_KEY_SIZE + 32];
	size_t len = 0;

	bool append(const uint8_t *bytes, size_t n)
	{
		if (n > sizeof(data) - len) {
			return false;
		}
		memcpy(data + len, bytes, n);
		len += n;
		return true;
	}
};

}  // namespace

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
	if (!_ready) {
		return false;
	}
	int rc = settings_save_one(key, data, len);

	if (rc != 0) {
		LOG_WRN("%s not written (%d)", key, rc);
	}
	return rc == 0;
}

void RescueStore::erase(const char *key)
{
	if (_ready) {
		settings_delete(key);
	}
}

/* Through NVS itself: erasing the region under the mounted backend would leave
 * it writing at stale addresses. */
void RescueStore::clear()
{
	void *storage;

	if (!_ready || settings_storage_get(&storage) != 0) {
		return;
	}
	struct nvs_fs *nvs = static_cast<struct nvs_fs *>(storage);

	_ready = nvs_clear(nvs) == 0 && nvs_mount(nvs) == 0;
	if (!_ready) {
		LOG_ERR("settings region not cleared - no rescue copy");
	}
}

void RescueCopy::open()
{
	_store.begin();
	_copy.open();
}

void RescueCopy::clearUnlessWorthKeeping()
{
	if (!_copy.worthKeeping()) {
		_store.clear();
	}
}

bool RescueCopy::readIdentity(mesh::LocalIdentity &id)
{
	IdentityBytes file;

	return _copy.copyTo(rescue::Item::Identity, file) &&
	       (id.readFromStorage(file.data, file.len) ||
		id.recoverFromStorage(file.data, file.len));
}

bool RescueCopy::readPrefs(NodePrefs &prefs)
{
	if (_copy.state(rescue::Item::Prefs) != rescue::State::Valid) {
		return false;
	}
	EntryStream in(_copy, rescue::Item::Prefs);

	return serverPrefsFromJson(prefs, in);
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
