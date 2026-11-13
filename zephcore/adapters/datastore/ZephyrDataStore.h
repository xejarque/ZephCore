/*
 * SPDX-License-Identifier: MIT
 * Zephyr DataStore - LittleFS-backed persistence with optional QSPI flash
 *
 * All platforms use DTS-automounted /lfs for identity, prefs, contacts.
 * QSPI /ext overrides contacts path when available (any platform).
 */

#pragma once

#include <mesh/Identity.h>
#include <mesh/MeshCore.h>
#include <NodePrefs.h>
#include <ContactInfo.h>
#include <ChannelDetails.h>

class DataStoreHost {
public:
	virtual bool onContactLoaded(const ContactInfo &contact) = 0;
	virtual bool getContactForSave(uint32_t idx, ContactInfo &contact) = 0;
	virtual bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails &ch) = 0;
	virtual bool getChannelForSave(uint8_t channel_idx, ChannelDetails &ch) = 0;
};

class ZephyrDataStore {
public:
	explicit ZephyrDataStore(mesh::RTCClock &clock);
	void begin();
	bool formatFileSystem();
	bool loadMainIdentity(mesh::LocalIdentity &identity);
	bool saveMainIdentity(const mesh::LocalIdentity &identity);
	void loadPrefs(NodePrefs &prefs);
	void savePrefs(const NodePrefs &prefs);
	void loadContacts(DataStoreHost *host);
	void saveContacts(DataStoreHost *host);
	/* Meshtastic node public keys (Zephtacore PKI): 36 byte records, node (u32 LE)
	 * then the X25519 key, appended as learned. */
	enum MtKeyResult { MTKEY_STORED, MTKEY_SAME, MTKEY_CONFLICT, MTKEY_FAILED };
	typedef bool (*MtKeyKeep)(uint32_t node, void *ctx);
	bool getMtKey(uint32_t node, uint8_t key[32]);
	/* A node's first key is kept: a different one is MTKEY_CONFLICT, as in
	 * Meshtastic. At max_keys records, those keep() refuses are dropped first; when
	 * every record is kept, the file is not rewritten again until epoch changes. */
	MtKeyResult putMtKey(uint32_t node, const uint8_t key[32], uint32_t max_keys, MtKeyKeep keep,
			     void *ctx, uint32_t epoch);
	bool removeMtKey(uint32_t node);

	void loadChannels(DataStoreHost *host);
	void saveChannels(DataStoreHost *host);
	/* Revision of the default channels the list was last given; 0 when never. */
	uint8_t loadMtDefaultsRev();
	bool saveMtDefaultsRev(uint8_t rev);
	uint8_t getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]);
	bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len);
	bool deleteBlobByKey(const uint8_t key[], int key_len);
	/* Used/total KiB for BLE storage report: /ext when mounted, else /lfs (Arduino parity). */
	uint32_t getStorageUsedKb() const;
	uint32_t getStorageTotalKb() const;

	/* Factory reset: erase every storage region, keep the volume marked as
	 * ours so the next boot does not format it again. */
	bool factoryReset();

	/* First boot of ZephCore on this volume: format a foreign one, repair
	 * an old bond store. Must run before bt_enable(). */
	void adoptVolume();

	/* Check if external QSPI flash is available */
	bool hasExternalStorage() const { return _has_ext_fs; }
	uint32_t getExternalStorageKb() const;


	static bool mount();
	static void unmount();
	static const char *mountPoint() { return MNT_POINT; }
	static const char *extMountPoint() { return EXT_MNT_POINT; }

private:
	/* Internal flash (always available) - identity, prefs */
	static constexpr const char *MNT_POINT = "/lfs";
	static constexpr const char *PREFS_JSON_FILE = "/lfs/prefs.json";
	/* Legacy binary prefs (PrefsCodec): read once to migrate, then kept for
	 * firmware from before prefs.json. */
	static constexpr const char *PREFS_FILE = "/lfs/new_prefs";
	static constexpr const char *MAIN_ID_FILE = "/lfs/_main.id";

	/* External QSPI flash (optional) - contacts, channels, blobs */
	static constexpr const char *EXT_MNT_POINT = "/ext";
	static constexpr const char *EXT_CONTACTS_FILE = "/ext/contacts3";
	static constexpr const char *EXT_CHANNELS_FILE = "/ext/channels2";
	static constexpr const char *EXT_MTKEYS_FILE = "/ext/mtkeys";
	static constexpr const char *EXT_MT_REV_FILE = "/ext/mt_rev";
	static constexpr const char *EXT_ADV_BLOBS_FILE = "/ext/adv_blobs";

	/* Fallback to internal if no external */
	static constexpr const char *INT_CONTACTS_FILE = "/lfs/contacts3";
	static constexpr const char *INT_CHANNELS_FILE = "/lfs/channels2";
	static constexpr const char *INT_MTKEYS_FILE = "/lfs/mtkeys";
	static constexpr const char *INT_MT_REV_FILE = "/lfs/mt_rev";
	static constexpr const char *INT_ADV_BLOBS_FILE = "/lfs/adv_blobs";

	mesh::RTCClock *_clock;
	bool _has_ext_fs;
	bool _mtkeys_all_in_use = false;
	uint32_t _mtkeys_epoch = 0;

	/* Get path based on external availability */
	const char *contactsFile() const { return _has_ext_fs ? EXT_CONTACTS_FILE : INT_CONTACTS_FILE; }
	const char *mtRevFile() const { return _has_ext_fs ? EXT_MT_REV_FILE : INT_MT_REV_FILE; }
	const char *mtKeysFile() const { return _has_ext_fs ? EXT_MTKEYS_FILE : INT_MTKEYS_FILE; }
	bool rewriteMtKeys(MtKeyKeep keep, void *ctx, uint32_t *kept);
	const char *channelsFile() const { return _has_ext_fs ? EXT_CHANNELS_FILE : INT_CHANNELS_FILE; }
	const char *advBlobsFile() const { return _has_ext_fs ? EXT_ADV_BLOBS_FILE : INT_ADV_BLOBS_FILE; }
	int maxBlobRecs() const { return _has_ext_fs ? 100 : 20; }

	void checkAdvBlobFile();
	bool loadLegacyPrefs(NodePrefs &prefs);
	bool hasInitMarker() const;
	void writeInitMarker();
	void formatNVSOnly();
	bool hasPrefs() const;
	bool prefsRadioImplausible() const;
	bool hasOldSettingsFile() const;
	void migrateToExternalFS();
	bool copyFile(const char *src, const char *dst);
};
