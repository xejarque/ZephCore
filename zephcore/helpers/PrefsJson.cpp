/*
 * SPDX-License-Identifier: MIT
 * PrefsJson - prefs.json for both roles. See PrefsJson.h.
 */

#include "PrefsJson.h"

namespace {

/* radio{} and gps{}: the same keys in both of upstream's trees. Not written:
 * "cad" (ZephCore always runs CAD), "agc_int" (periodic AGC reset removed),
 * "fem_txgain" (no such setting here). */
class RadioJson : public ConfigSerializer {
	NodePrefs *_p;

protected:
	void structure() override
	{
		def("freq", _p->freq);
		def("bw", _p->bw);
		def("sf", _p->sf);
		def("cr", _p->cr);
		def("int_thr", _p->interference_threshold);
		def("rxgain", _p->rx_boost);
		def("fem_rxgain", _p->fem_rxgain);
		def("tx", _p->tx_power_dbm);
		def("af", _p->airtime_factor);
		def("rxdelay", _p->rx_delay_base);
		def("f_txdelay", _p->tx_delay_factor);
		def("d_txdelay", _p->direct_tx_delay_factor);
		def("hash_mode", _p->path_hash_mode);
		def("multi_ack", _p->multi_acks);
	}

public:
	explicit RadioJson(NodePrefs *p) : _p(p) {}
};

class GpsJson : public ConfigSerializer {
	NodePrefs *_p;

protected:
	void structure() override
	{
		def("en", _p->gps_enabled);
		def("int", _p->gps_interval);
		def("adv_loc", _p->advert_loc_policy);
	}

public:
	explicit GpsJson(NodePrefs *p) : _p(p) {}
};

/* zc{}: ZephCore's own settings, common to both roles */
class ZcCommonJson : public ConfigSerializer {
protected:
	NodePrefs *_p;

	void structure() override
	{
		def("leds_off", _p->leds_disabled);
		def("leds_radio", _p->leds_radio_mode);
		def("leds_hb", _p->leds_hb_mode);
		def("leds_brightness", _p->led_brightness);
		def("rx_dc", _p->rx_duty_cycle);
		def("mts", _p->meshtimesync);
		def("cad_auto", _p->cad_auto);
		def("cad_off", _p->cad_offset);
		def("cad_base", _p->cad_base);
		def("probe_int", _p->probe_interval);
		def("busycap", _p->cad_busycap);
		def("xsf", (void *)_p->extra_sf, EXTRA_SF_MAX);
		def("disp_rot", _p->display_rotate);
		def("in_rot", _p->input_rotate);
		def("pwr_sav_set", _p->powersaving_set);
		def("gps_stby", _p->gps_standby_max);
		def("ping_mc_en", _p->ping.mc.enable);
		def("ping_mc_trig", _p->ping.mc.trigger, sizeof(_p->ping.mc.trigger));
		def("ping_mt_en", _p->ping.mt.enable);
		def("ping_mt_trig", _p->ping.mt.trigger, sizeof(_p->ping.mt.trigger));
		def("ping_rssi", _p->ping.rssi);
		def("ping_snr", _p->ping.snr);
		def("ping_nd", _p->ping.ignore_direct);
		def("ping_nr", _p->ping.ignore_remote);
		def("fw_drop", _p->fw.drop);
		def("fw_gap", _p->fw.advert_gap_min);
		def("fw_rate", _p->fw.hop_rate);
		def("mfw_drop", _p->mtfw.drop);
		def("mfw_gap", _p->mtfw.advert_gap_min);
		def("mfw_rate", _p->mtfw.hop_rate);
	}

public:
	explicit ZcCommonJson(NodePrefs *p) : _p(p) {}
};

#ifdef CONFIG_ZEPHTACORE_MESHTASTIC
/* One channel of the companion's table: a value of prefs.json is at most
 * CONFIG_MAX_TOKEN_LEN long, so the table is an object per entry. */
class MtChannelJson : public ConfigSerializer {
	mt::ChannelEntry *_e;

protected:
	void structure() override
	{
		def("n", _e->name, sizeof(_e->name));
		def("l", _e->psk_len);
		def("k", (void *)_e->psk, sizeof(_e->psk));
	}

public:
	explicit MtChannelJson(mt::ChannelEntry *e) : _e(e) {}
};

class MtChannelsJson : public ConfigSerializer {
	NodePrefs *_p;

protected:
	void structure() override
	{
		for (int i = 0; i < mt::kTableMax; i++) {
			if (_context->op() == WRITE && _p->mt.channels[i].name[0] == '\0') {
				continue;  /* an empty slot is the default: not written */
			}
			char key[3] = {'c', (char)('0' + i), '\0'};
			MtChannelJson e(&_p->mt.channels[i]);

			def(key, e);
		}
	}

public:
	explicit MtChannelsJson(NodePrefs *p) : _p(p) {}
};

/* mt{}: the Meshtastic side of both roles. */
class MtJson : public ConfigSerializer {
	NodePrefs *_p;
	MtChannelsJson _chs;

protected:
	void structure() override
	{
		def("en", _p->mt.enable);
		def("role", _p->mt.role);
		def("hops", _p->mt.hops);
		def("relay", _p->mt.relay);
		def("opaque", _p->mt.relay_opaque);
		def("zh", _p->mt.zerohop);
		def("zh_nodes", (void *)_p->mt.zerohop_nodes, sizeof(_p->mt.zerohop_nodes));
		def("ni_s", _p->mt.nodeinfo_s);
		def("tel_s", _p->mt.telemetry_s);
		def("ch", _p->mt.channel, sizeof(_p->mt.channel));
		def("psk_len", _p->mt.psk_len);
		def("psk", (void *)_p->mt.psk, sizeof(_p->mt.psk));
		def("chs", _chs);
		def("dr", _p->mt.defaults_rev);
	}

public:
	explicit MtJson(NodePrefs *p) : _p(p), _chs(p) {}
};
#endif

/* ── Companion: upstream examples/companion_radio/NodePrefs.h ──────── */

class RepeatDisableJson : public ConfigSerializer {
protected:
	void structure() override { def("disable", disable); }

public:
	uint8_t disable = 1;
};

class CompJson : public ConfigSerializer {
	NodePrefs *_p;

protected:
	void structure() override
	{
		def("auto_max", _p->autoadd_max_hops);
		def("defs_nm", _p->default_scope_name, sizeof(_p->default_scope_name));
		def("defs_key", (void *)_p->default_scope_key, sizeof(_p->default_scope_key));
		def("pin", _p->ble_pin);
		def("buzz_q", _p->buzzer_quiet);
		def("auto_add", _p->autoadd_config);
		def("man_add", _p->manual_add_contacts);
		def("tel_base", _p->telemetry_mode_base);
		def("tel_loc", _p->telemetry_mode_loc);
		def("tel_env", _p->telemetry_mode_env);
		def("tz_offset", _p->tz_offset);
	}

public:
	explicit CompJson(NodePrefs *p) : _p(p) {}
};

class WifiJson : public ConfigSerializer {
	NodePrefs *_p;

protected:
	void structure() override
	{
		def("ssid", _p->wifi_ssid, sizeof(_p->wifi_ssid));
		def("pwd", _p->wifi_pwd, sizeof(_p->wifi_pwd));
		def("enabled", _p->wifi_enabled);
	}

public:
	explicit WifiJson(NodePrefs *p) : _p(p) {}
};

class ZcCompanionJson : public ZcCommonJson {
protected:
	void structure() override
	{
		ZcCommonJson::structure();
		def("ble_off", _p->ble_disabled);
		def("bright", _p->display_brightness);
		def("wake", _p->wake_on_msg);
		def("scr_off", _p->screen_off_secs);
		def("auto_off_mv", _p->auto_shutdown_mv);
		def("aoff_set", _p->auto_shutdown_set);
		def("vc_en", _p->v_contact_enabled);
		def("vc_flags", _p->v_contact_flags);
		def("vbat_mv", _p->v_battery_alert_mv);
		def("adc_mult", _p->adc_multiplier);
		def("pwr_sav_en", _p->powersaving_enabled);
		def("og_home", _p->offgrid_home_freq);
	}

public:
	explicit ZcCompanionJson(NodePrefs *p) : ZcCommonJson(p) {}
};

class CompanionJson : public ConfigSerializer {
	NodePrefs *_p;
	RadioJson _radio;
	GpsJson _gps;
	RepeatDisableJson _repeat;
	CompJson _comp;
	WifiJson _wifi;
	ZcCompanionJson _zc;
#ifdef CONFIG_ZEPHTACORE_MESHTASTIC
	MtJson _mt;
#endif

protected:
	void structure() override
	{
		def("name", _p->node_name, sizeof(_p->node_name));
		def("lat", _p->node_lat);
		def("lon", _p->node_lon);
		def("radio", _radio);
		def("gps", _gps);
		def("repeat", _repeat);
		def("comp", _comp);
		def("wifi", _wifi);
		def("zc", _zc);
#ifdef CONFIG_ZEPHTACORE_MESHTASTIC
		def("mt", _mt);
#endif
	}

public:
	explicit CompanionJson(NodePrefs *p)
		: _p(p), _radio(p), _gps(p), _comp(p), _wifi(p), _zc(p)
#ifdef CONFIG_ZEPHTACORE_MESHTASTIC
		, _mt(p)
#endif
	{
		/* upstream: repeat.disable; ours: client_repeat (1 = forward) */
		_repeat.disable = p->client_repeat ? 0 : 1;
	}

	bool save(Stream &s) { return saveSerial(s); }
	bool load(Stream &s)
	{
		bool ok = loadSerial(s);

		_p->client_repeat = _repeat.disable ? 0 : 1;
		return ok;
	}
};

/* ── Servers: upstream helpers/CommonCLI.h ──────────────────────────── */

class RepeatJson : public ConfigSerializer {
	NodePrefs *_p;

protected:
	void structure() override
	{
		def("disable", _p->disable_fwd);
		def("f_max", _p->flood_max);
		def("f_max_uns", _p->flood_max_unscoped);
		def("f_max_adv", _p->flood_max_advert);
		def("loop", _p->loop_detect);
	}

public:
	explicit RepeatJson(NodePrefs *p) : _p(p) {}
};

class RoomJson : public ConfigSerializer {
	NodePrefs *_p;

protected:
	void structure() override { def("rd_only", _p->allow_read_only); }

public:
	explicit RoomJson(NodePrefs *p) : _p(p) {}
};

class VRoomJson : public ConfigSerializer {
	NodePrefs *_p;

protected:
	void structure() override
	{
		def("enabled", _p->vroom.enabled);
		def("access", _p->vroom.access);
		def("pass", _p->vroom.password, sizeof(_p->vroom.password));
	}

public:
	explicit VRoomJson(NodePrefs *p) : _p(p) {}
};

class PowerJson : public ConfigSerializer {
	NodePrefs *_p;

protected:
	void structure() override
	{
		def("adc_mult", _p->adc_multiplier);
		def("pwr_sav_en", _p->powersaving_enabled);
	}

public:
	explicit PowerJson(NodePrefs *p) : _p(p) {}
};

class ZcServerJson : public ZcCommonJson {
protected:
	void structure() override
	{
		ZcCommonJson::structure();
		def("tz_offset", _p->tz_offset);
		def("backoff", _p->backoff_multiplier);
		def("buzz_q", _p->buzzer_quiet);
		def("disc_ts", _p->discovery_mod_timestamp);
		def("gps_set", _p->gps_enabled_set);
		/* `set z.autoshutdown`. The companion's aoff_set marks its 3300 mV
		 * migration, which a server never had. */
		def("auto_off_mv", _p->auto_shutdown_mv);
	}

public:
	explicit ZcServerJson(NodePrefs *p) : ZcCommonJson(p) {}
};

class ServerJson : public ConfigSerializer {
	NodePrefs *_p;
	RadioJson _radio;
	GpsJson _gps;
	RepeatJson _repeat;
	RoomJson _room;
	VRoomJson _vroom;
	PowerJson _power;
	ZcServerJson _zc;
#ifdef CONFIG_ZEPHTACORE_MESHTASTIC
	MtJson _mt;
#endif

protected:
	void structure() override
	{
		def("name", _p->node_name, sizeof(_p->node_name));
		def("pass", _p->password, sizeof(_p->password));
		def("guest", _p->guest_password, sizeof(_p->guest_password));
		def("owner", _p->owner_info, sizeof(_p->owner_info));
		def("adv_int", _p->advert_interval);
		def("f_adv_int", _p->flood_advert_interval);
		def("lat", _p->node_lat);
		def("lon", _p->node_lon);
		def("radio", _radio);
		def("gps", _gps);
		def("repeat", _repeat);
		def("room", _room);
		def("z_room", _vroom);
		def("power", _power);
		def("zc", _zc);
#ifdef CONFIG_ZEPHTACORE_MESHTASTIC
		def("mt", _mt);
#endif
	}

public:
	explicit ServerJson(NodePrefs *p)
		: _p(p), _radio(p), _gps(p), _repeat(p), _room(p), _vroom(p), _power(p), _zc(p)
#ifdef CONFIG_ZEPHTACORE_MESHTASTIC
		, _mt(p)
#endif
	{}

	bool save(Stream &s) { return saveSerial(s); }
	bool load(Stream &s) { return loadSerial(s); }
};

void saneRadio(NodePrefs &p)
{
	/* Written as "not inside": every comparison against NaN is false, so a
	 * `freq: nan` from a corrupted file would pass a plain range test and
	 * reach the radio. */
	if (!(p.freq >= ZC_RADIO_FREQ_MIN_MHZ && p.freq <= ZC_RADIO_FREQ_MAX_MHZ) ||
	    !(p.bw >= ZC_RADIO_BW_MIN_KHZ && p.bw <= ZC_RADIO_BW_MAX_KHZ) ||
	    p.sf < 5 || p.sf > 12) {
		p.freq = mesh::LoRaConfig::FREQ_MHZ;
		p.bw = mesh::LoRaConfig::BANDWIDTH_KHZ;
		p.sf = mesh::LoRaConfig::SPREADING_FACTOR;
		p.cr = mesh::LoRaConfig::CODING_RATE;
		p.tx_power_dbm = mesh::LoRaConfig::TX_POWER_DBM;
	}
}

}  // namespace

/* Writing only reads the struct; the serializer API takes references. */

bool companionPrefsToJson(const NodePrefs &p, Stream &out)
{
	CompanionJson j(const_cast<NodePrefs *>(&p));

	return j.save(out);
}

bool serverPrefsToJson(const NodePrefs &p, Stream &out)
{
	ServerJson j(const_cast<NodePrefs *>(&p));

	return j.save(out);
}

bool companionPrefsFromJson(NodePrefs &p, Stream &in)
{
	NodePrefs t = p;
	CompanionJson j(&t);

	/* Absent keys keep the caller's value: clear the marker first, so a file
	 * from before it existed is recognised (NodePrefs.h). */
	t.auto_shutdown_set = 0;
	t.powersaving_set = 0;
	if (!j.load(in)) {
		return false;
	}
	auto_shutdown_upgrade(&t);
	powersaving_upgrade(&t);
	saneRadio(t);
	sanitizeNodePrefs(&t);
	p = t;
	return true;
}

bool serverPrefsFromJson(NodePrefs &p, Stream &in)
{
	NodePrefs t = p;
	ServerJson j(&t);

	/* Absent keys keep the caller's value, so clear the marker first: a
	 * file from before it existed was written by firmware that ran the GPS
	 * regardless of gps.en, and keeps doing so (NodePrefs.h). */
	t.gps_enabled_set = 0;
	t.powersaving_set = 0;
	if (!j.load(in)) {
		return false;
	}
	if (!t.gps_enabled_set) {
		t.gps_enabled = 1;
		t.gps_enabled_set = 1;
	}
	powersaving_upgrade(&t);
	saneRadio(t);
	sanitizeNodePrefs(&t);
	p = t;
	return true;
}
