# Architecture decision records

One decision per file: Context / Decision / Consequences / Status. Recorded 2026-09-25 at the end of the
restructure; most describe decisions taken earlier. The system view is in [../DESIGN.md](../DESIGN.md).

| # | Decision |
|---|---|
| [0001](0001-esp32-boot-and-flash-layout.md) | ESP32 boot: simple boot for plain builds, MCUboot for OTA builds and releases |
| [0002](0002-esp32s3-native-usb-companion.md) | ESP32-S3 companions use native USB; `start dfu` restores USB-Serial-JTAG |
| [0003](0003-devicetree-overlay-precedence.md) | Devicetree overlays: board.overlay first, conf-paired after it |
| [0004](0004-ble-privacy.md) | BLE privacy off on nRF/MG24, on for ESP32-S3 (identity advertising) |
| [0005](0005-zephyr-main-pin.md) | Track Zephyr `main`, pinned; the migration changes not to undo |
| [0006](0006-sx126x-rx-duty-cycle-and-busy-gating.md) | SX126x RX duty cycle and four-layer RX-busy gating |
| [0007](0007-adaptive-contention-window.md) | Adaptive contention window instead of static tx/rx delays |
| [0008](0008-storage-and-settings-backend.md) | Storage layout, power-loss writes, BLE bonds in NVS |
| [0009](0009-wifi-companion-boards.md) | WiFi companion boards and contact counts |
| [0011](0011-esp32-companion-ble-ota.md) | ESP32 companions update over BLE with MCUmgr (sysbuild builds; `ble_ota: false` opts a board out) |
| [0012](0012-meshtastic-pki-direct-messages.md) | Zephtacore: Meshtastic PKI direct messages, our key derived from the identity, node keys in `mtkeys` |
| [0013](0013-meshtastic-settings-in-prefs-json.md) | Zephtacore: Meshtastic settings in `prefs.json` `mt{}`, only in builds with the bridge |
| [0014](0014-cli-z-namespace.md) | Zephtacore: the commands this fork adds hang from `z.` (`z.mt.*`, `z.mc.ping.*`, `z.tx.stalls`, ...) |
| [0015](0015-meshtastic-channels-by-command.md) | Zephtacore: Meshtastic channels by command, shown to the app as `#mt-<name>` hashtag channels with the key kept in `prefs.json` |
| [0016](0016-meshtastic-zero-hop-and-roles.md) | Zephtacore: zero hop between fixed Meshtastic neighbours, off by default; repeaters announce `client_base`, companions `client_mute` |
| [0017](0017-low-battery-auto-shutdown-with-battery-wake.md) | Zephtacore: low-battery auto-shutdown with an LPCOMP battery wake; off by default on repeaters and room servers, companion default kept |
| [0018](0018-lora-headers-without-a-valid-crc.md) | A LoRa header whose CRC fails is not received; the check lives in the SX126x, LR1110 and LR2021 drivers, not in SX127x |
| [0019](0019-recontention-after-a-busy-channel.md) | A packet held by a busy channel draws its wait again; MeshCore and Meshtastic keep their own windows |
| [0020](0020-fork-version-scheme.md) | Fork version scheme (`<upstream-base>-ztc.<n>`, starting at `1.18.2-ztc.1`) within the 20-byte `PACKET_DEVICE_INFO` wire field |
| [0021](0021-rescue-copy-in-settings-nvs.md) | Zephtacore: a repeater or room server keeps a copy of its identity, `prefs.json` and regions in the settings NVS and restores it when a file is lost |
