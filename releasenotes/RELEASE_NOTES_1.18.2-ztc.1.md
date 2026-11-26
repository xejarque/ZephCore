# Zephtacore 1.18.2-ztc.1

Based on ZephCore `1.18.2-zephcore`. Zephtacore, the build that also hears and repeats Meshtastic, repeats less by default and keeps its
default channels and node name in order.

---

## Zephtacore builds in the release

- Every board of the web builder that CI publishes, 37 of them, carries the companion and the repeater also with the
  Meshtastic bridge (`boards/common/zephtacore.conf`), as the `zephtacore` variant of its `zephcore.yml`: files named
  `<board>-<role>-zephtacore-<hash>`, listed in the catalog with the subtitle Zephtacore. The Zephtacore web flasher
  downloads only these. `lilygo_tlora_c6`, `xiao_esp32c6` and `lora_e5_mini` have none: their companion does not fit
  in RAM (the C6 boards) or flash (`lora_e5_mini`). See `docs/PROVIDER_CATALOG.md`.
- On a WiFi companion with Zephtacore the system heap is 3 KB smaller (`wifi_companion_zephtacore.conf`), which is what
  lets the Heltec V3 and the Wireless Tracker build it.
- The web flasher writes nRF52 boards over USB, with the Adafruit bootloader's serial DFU and the `.zip` package of the
  catalog, instead of a UF2 copied by hand (the UF2 copy stays for browsers without Web Serial). The board reboots into
  the bootloader and the browser asks for its port once per step.
- The web flasher shows one flow: the board and role, whether to keep the data or do a clean install, and the install
  button, always visible. The settings to send, the node backup and the console sit apart below it.
- On ESP32 a clean install erases the whole flash while writing. On nRF52 it loads the SoftDevice's formatter package first, which erases the file system and returns the
  board to the bootloader, and then the firmware. The formatter is the one the catalog names for the board. A clean
  install gives the node a new identity.
- The companion of a board whose USB-C is a USB-UART bridge (Heltec V3, ThinkNode M9) now serves the app and the
  text CLI over that UART, as the boards with native USB already do, so the Builder can send its settings by USB and
  observers and bots can use the port. On the Heltec V3 the system heap is 68 KB, 63 KB with
  Zephtacore (`wifi_companion_uart.conf`, `wifi_companion_zephtacore_uart.conf`).
- A push to a branch other than `master` still builds, but no longer replaces the release or `firmware-dist`.
- The catalog gives the SHA-256 of every firmware file, and the web flasher refuses a download that does not match
  it before writing anything.

---

## A repeater or room server survives losing its file system

- A repeater or room server on nRF52 or ESP32 keeps a copy of its identity, settings, regions and virtual room
  identity outside the file system. If the file system is lost, or one of those files no longer loads, the node comes
  back at the next boot with the same key, password, radio parameters, regions and repeat settings of both meshes, so
  it can still be administered over the air. Log in again: the lists of clients and their permissions are not copied.
- The copy is created by itself on a node that is already set up, and follows every administration command. `get
  z.rescue` tells its state and whether a file has been restored. See `docs/CLI_commands.md`.
- `erase`, the formatter and flashing companion firmware remove it. Run `erase` before giving a node away.
- Not on MG24, nRF54L or STM32WL boards.

---

## Web builder and node backup

- **Node backup** in the web flasher, for a repeater or room server on USB: it reads the identity, passwords,
  settings and regions from the node into a JSON file, and restores them on a node (also on a clean install). The file
  is not encrypted: whoever holds it can impersonate and administer the node. Not saved: the client lists, the virtual
  room's identity, a Meshtastic channel key of your own, and what belongs to the board (battery calibration, LEDs,
  buzzer, screen and joystick rotation, time zone). It replaces the settings file the builder used to
  download, and a file of that kind can no longer be loaded.
- **Sending the settings by USB now stops when the node refuses one**, and says which and why. Before, a refusal went
  unnoticed and the page reported success. The passwords sent this way no longer show in the console.
- A companion is no longer sent `password` or the guest password, which it refuses. The guest password and the GPS
  switch are now sent; the altitude field and the firewall checkbox, which sent nothing, are gone. Passwords take at
  most 15 characters.
- On a repeater whose board wakes on battery recovery, the builder asks for the low-battery power-off (off unless
  ticked).
- A role that is not published keeps its name in the flasher, which offers nothing to flash, instead of turning into
  companion.
- New `get password` (repeater, room server; over USB, and over LoRa to an admin) shows the admin password, and
  `get z.mc.regions [page]` shows the whole region map in pages of whole lines. See `docs/CLI_commands.md`.

---

## Low-battery power-off that wakes again (ProMicro SX1262)

- On the ProMicro SX1262 a node that powers off on a low battery now wakes by itself once the battery recovers to
  about 3.6 to 3.9 V, so a solar repeater no longer needs a manual restart. For repeaters and room servers it is off
  until you set it: `set z.autoshutdown <mV>` (0 = off, at most 3300 mV there; three readings 30 s apart, never on
  external power), and it survives a reboot. Without a battery protection board, set it. The companion, which already
  powered off below 3200 mV, now wakes the same way.
- A `poweroff` or a power-off from the UI stays off until the button, also when the battery crosses that voltage.
- `set autoshutdown` now takes 0 or 2900 to 4200 mV (lower on a board with a battery wake): before, a value outside
  that range was answered OK and then lost at the next reboot. See `docs/CLI_commands.md`.

---

## Reception statistics (repeater)

- `get z.mc.hopstats` and `get z.mt.hopstats` count the frames heard by hops crossed; `get z.mc.relays` and
  `get z.mt.relays` count them by the repeater that delivered them. They help to tune `z.mt.zerohop` and to find
  repeaters that `neighbors` does not list. See `docs/CLI_commands.md`.

---

## Receive limits (repeater)

- `set z.mc.fw.drop <rule> on` drops the flood frames of a payload type (`advert`, `request`, `text`, `group`, `ack`,
  `path`, `trace`). `set z.mc.fw.gap <minutes>` keeps one advert per key per window, and `set z.mc.fw.rate <frames>` caps the flood
  frames a minute accepted from one neighbouring repeater. Both are off by default; `get z.mc.fw.stats` counts what
  they dropped. The gap remembers 48 keys and the rate 16 repeaters; a full table forgets the one written longest ago. A
  1-byte path hash is counted by the rate too, and every repeater with that byte shares its count. The same commands exist for
  Meshtastic as `z.mt.fw.drop`, `.gap` and `.rate` (rules are ports; the gap is per NodeInfo), and a PKI direct text goes
  through them once opened. See `docs/CLI_commands.md`.

---

## Meshtastic (Zephtacore builds)

- Meshtastic channels on the companion are made by command, and the app no longer mixes channels that share a
  key (`Test` and `Bots`, or `SFNarrow`, `Catalunya` and `Barcelona`: a text sent to one showed in the others, and
  one app listed only one of them). `set z.mt.channel.add <name> [<key>]` takes the key as Meshtastic shows it
  (`AQ==`, `Ag==` or 16 bytes in base64), `set z.mt.channel.remove <name>` removes it and `get z.mt.channels` lists
  them. The app lists each as `#mt-<name>`. The default channels are created the same way. A node that held the old
  `MT:<name>` channels needs a factory reset; they are plain private channels now and carry no Meshtastic.
- A Meshtastic text now reaches the channel it was heard on, not the first channel with the same key.

- The repeater starts with `z.mt.relay essential`: text, direct messages, their ACKs, NodeInfo and
  traceroute are repeated. `z.mt.relay all`
  still repeats the rest as before. `essential` also repeats position, so nodes show on the map, and the frames of
  channels the node holds no key for (`z.mt.relay.opaque on`). A repeat is never cancelled when another node sends the frame first: it only waits longer.
  Before, `essential` repeated only the channels the node held a key for.
- The companion repeats nothing unless asked (`set z.mt.relay essential` or `all`), and when asked it
  never drops a repeat because another node sent it first.
- New Meshtastic roles. A repeater now announces `client_base` (a fixed node) and can be set to `client`, `client_base`,
  `router`, `router_late` or `repeater`. A companion announces `client_mute` and can be set to `client_mute` or `client`;
  as `client_mute` it repeats nothing, so a companion that should repeat needs `set z.mt.role client` as well as
  `z.mt.relay`. A node already set up keeps its stored role: on a repeater run `set z.mt.role client_base` to adopt
  the new one.
- New zero hop, off by default: `set z.mt.zerohop auto` repeats a frame without spending a hop when it came through a
  fixed neighbour (heard directly, announcing `router`, `router_late` or `client_base`), so a chain of fixed nodes costs
  one hop. `set z.mt.zerohop.add <node>` lists a neighbour by hand, `get z.mt.zerohop` shows the mode and the list,
  and `get z.mt.stats` counts the frames kept (`kept`).
- The default Meshtastic channels (`CONFIG_ZEPHTACORE_DEFAULT_CHANNELS`, as `#mt-<name>` slots) and `#test` / `#bot` are
  added once per firmware revision, which is kept in `prefs.json` as `mt.dr`. Deleting one in the app sticks.
- A direct message to a Meshtastic node that the app retries keeps the packet id of the first try, so the node's ACK still
  answers it. A PKI message that arrives before we know the sender's key is delivered when it is sent again, and a key that no
  longer opens a frame makes us ask for the node's NodeInfo.
- Meshtastic frames up to 255 bytes are repeated: the bytes past the 184th of a queued frame wait in a tail slot (`CONFIG_ZEPHTACORE_FOREIGN_TAIL_SLOTS`, 4 slots of 71 bytes by default). `get z.mt.stats` counts `long` the frames a build without slots cannot repeat, apart from `shed`.
- A Meshtastic node heard for the first time is added as a contact only as your auto-add settings
  allow; with manual adding it shows up as a discovered contact instead.
- A Meshtastic node becomes a contact when its NodeInfo carries a name and a public key, or when it sends us a direct
  text we can open (named `!<node number> (MT)` until its NodeInfo arrives). A Position from an unknown node no longer
  takes a contact slot, and a node whose firmware predates Meshtastic 2.5 (no key) stays out of the contact list and
  the map.
- Renaming the node changes the name in its Meshtastic NodeInfo too, without a restart.
- The contact key of a Meshtastic node is now derived with a secret of the companion, so nobody else can sign MeshCore
  adverts or messages as that contact. Meshtastic contacts saved by an earlier build are no longer recognised: delete
  them in the app; they come back with the node's next NodeInfo or position, and its stored PKI key is kept.
- With `z.mt.enable off` the companion sends nothing as Meshtastic: a message the app sends on a `#mt-` channel or to
  a Meshtastic contact is refused at once, instead of going out on a receiver that could not hear the answer.
- Settings of the Meshtastic side live in `prefs.json` only in builds that include it.
- New ping bot on the companion and the repeater: it answers a command of your choice on MeshCore `#bot` and on
  Meshtastic `Bots` with the hops, the SNR and the route (all of it in MeshCore, one hop per line with the RSSI/SNR after the last, the
  last relay in Meshtastic). Up to 6 pings a minute are answered per protocol, and the companion shows its answer in the app.
  Off until you pick the command: `set z.mc.ping.enable on /ping`, `set z.mt.ping.enable on /ping`; `get z.mc.ping.status`
  shows the counters.
- New `get z.mc.neighbors` and `get z.mt.neighbors` (alias `vecinos`; `recientes` for `recent`; the page is a plain number) on the companion and the repeater: the nodes heard directly,
  best SNR first, as `name (SNR, age)`, 4 to a page (`get z.mt.neighbors 2`), or the latest heard first with `recent` (`get z.mt.neighbors recent`). MeshCore lists repeaters heard with no hops, Meshtastic nodes heard with no relay
  in between. Kept in RAM, so a reboot empties them.
- New `get z.info` (version, uptime, battery, flash, heap and contacts in use) and `get z.stats` (MeshCore and Meshtastic counters), each in
  one line that fits a radio reply. The heap figures come from the kernel's heap counters, which every build now enables
  (`CONFIG_SYS_HEAP_RUNTIME_STATS`).
- With the Meshtastic bridge a new device starts at coding rate 4/6, the SFNarrow preset (4/5 without it). A device that
  already has settings keeps them.
- New dashboard page on 200x200 e-paper companions (ThinkNode M1): MeshCore and Meshtastic side by side, with packet counts, nearest
  neighbours and their signal, radio setup, heap, flash and time on air. It is the first page, and redraws when opened, on a button press
  and once an hour.
- Commands of the Meshtastic side and the new ones hang from `z.`: `z.mt.*`, `z.mc.ping.*`, `z.mt.ping.*`, `z.ping.*`;
  `get tx.stalls` is now `get z.tx.stalls`.
- `help` lists the groups of the `z.` commands. The repeater and room server had no `help` before. `help` at the end of a `z.` command (`get z.mt.relay help`) tells its usage, and on a group (`get z.mt help`) lists its children, with a `/` after any that has more under it;
  `set` of a key that takes text (a ping command, a channel) sets that text even when it is `help`.
  Every on/off `z.` setting (`z.mt.enable`, `z.mt.relay.opaque`, `z.ping.*`, `z.mc.ping.enable`, `z.mt.ping.enable`) also takes `1` and `0`.

## Fewer collisions when the channel is busy (MeshCore and Meshtastic)

- A packet whose turn comes while another node is transmitting no longer goes out the moment that frame ends, when every
  other waiting node would do the same. It draws its wait again, in the window of its protocol and role. Repeats can take
  somewhat longer to finish in a busy area. Not measured on the air yet.
- A packet queued later no longer delays the send of one queued earlier, and none is left waiting for an unrelated wake.
- Meshtastic: a frame waiting to be repeated draws its wait again after every frame, as Meshtastic does, so routers stay
  ahead of clients. `router_late` goes after the client window once it hears another node repeat the frame.
- The ping bots answer in the window of a repeat, so several bots answering one message no longer step on each other.

## Radio

- SX126x: changing the sync word between MeshCore and Meshtastic no longer waits for the radio's
  sleep phase when RX duty cycle is on.
- LR1110 and LR2021 boards (the T1000-E among them) with Zephtacore: nearby MeshCore nodes are heard again.
  The hybrid receive word was `0x1B`, which made the LR1110 drop every strong MeshCore frame, so close nodes only
  arrived repeated and never showed as neighbours. It is now `0x22`, which favours MeshCore: on these chips only weak
  Meshtastic frames are received.

## LilyGo T-Impulse Plus charges its battery

The T-Impulse Plus has a charger chip that starts with charging switched off and has to be set up
by the firmware. ZephCore never did, so the battery only charged after LilyGo's own firmware had
run, and stopped again once the battery ran flat. On USB the chip also restarted the whole
wristband every few minutes, which showed up as dropped Bluetooth connections.

- **The charger is now set up at every start**, the way LilyGo's firmware does it, so the battery
  charges on USB without flashing anything else first.
- **The restarts on USB stop.** ZephCore keeps the charger's watchdog switched off.
- **The GPS works.** ZephCore switched the GPS off when it meant to switch it on, so the
  wristband never found its GPS and the app showed GPS as not supported.

## Other changes

- **A node no longer falls silent after 25 days.** A node whose transmits had all gone out at the first try for
  24.9 days stopped transmitting for up to another 24.9 days, and stopped receiving too once its queue had
  filled. Only a reboot brought it back sooner. Repeaters on a quiet channel and companions that stay on for
  weeks were the ones at risk.
- **Idle nodes sleep longer.** Once a node had heard a packet repeated and then fell quiet, it woke every
  5 seconds for as long as it ran. It now returns to waking only for its regular channel measurement (15 s).
- **Companion: the app's airtime factor is limited to 0–9**, as `set af` is. A larger value used to apply until
  the next restart and then become 9 without notice; from 200 up it switched the duty-cycle limit off. The app
  now gets an error and the setting stays as it was.
- **Duty-cycle budget refills exactly.** After about five days without a transmit (at 10 %) the budget could
  come back almost empty and delay the next transmit, and rounding lost up to 5 % of the allowance while a
  packet was waiting.
- **KISS modem: the encrypt command no longer overwrites memory.** A host that sent the
  `EncryptData` command with a plaintext longer than 192 bytes wrote past a buffer in the modem's
  memory. Plaintexts over 184 bytes, the most a packet can carry, are now refused with
  `EncryptFailed`. Only the computer on the modem's serial port could send it.
- **SX126x recovers from a lost transmit completion and rounds SNR to nearest.** If a transmit never signals
  `TX_DONE`, the driver aborts TX back to standby/sleep and restores RX instead of leaving every subsequent
  transmit failing with `-EBUSY`. Integer-dB SNR values are now rounded to the nearest dB rather than floored.
- **LR1110 and LR2021 boards never transmit when the channel check itself fails.** If the listen-before-talk check did
  not complete, the node used to transmit anyway. It now restores reception and tries again later.
- **Observer runs radio maintenance and AGC recovery.** Silent observers now arm maintenance wakes and run
  `maintenanceLoop()` (noise floor, CAD, SX126x AGC unstick, LR11xx/LR20xx temperature recalibration).
- **Server login and replay hardening.** Repeaters and room servers lock password login after 4 failed attempts in
  180 s, reject empty admin passwords, and start client replay marks at `boot_clock - 10 s`; the companion drops
  remote `TXT_TYPE_CLI_COMMAND` packets stamped `0` and bounds remote replies to the packet buffer.
- **Meshtastic DM reliability.** Direct messages can use up to 255 bytes on the air (spilling past 184 bytes into
  `ForeignTail`), retried DMs are re-ACKed without duplicate delivery, ACKs requesting an ACK are answered at 0 hops
  using the ACK's own packet ID, unicast texts encrypted with a channel key are dropped, and ACK/NodeInfo replies
  use `min(hops_away + 2, own_hop_limit)`.
- **CLI, storage and web flasher hardening.** `time`, `clock sync`, `lat`/`lon`, negative advert intervals, `sf`/`cr`
  and `int.thresh` validate inputs before applying; companion returns `Error: not supported on companion` on
  server-only settings; failed `prefs.json` writes report an error instead of `OK`; room server persists its buzzer
  mute state; `ZephyrFS` writes `"w"` files via `.tmp` + `fs_sync` + rename; and the web flasher verifies the nRF52
  formatter SHA-256 for both `.zip` (DFU) and `.uf2` downloads.
- **Hardware safety.** `oled_sleep()` takes its bus and address from the devicetree I2C panel node and compiles out
  on headless boards; `bmp388` leaves the common I2C auto-probe list because its driver soft-resets `0x7E` before
  reading chip ID.
- **RAK4631 and RAK3401 1W draw less when switched off.** `poweroff` and the low-battery shutdown left the
  WisBlock slot power on, so a GPS module, slot sensors and display (and on the RAK3401 1W the amplifier's 5 V
  supply) kept running while the node was off. They are now switched off too. MeshCore measured a RAK4631 with
  GPS going from about 30 mA to about 1.3 mA when off.

---

## Virtual Room Server inside Repeater (`CONFIG_ZEPHCORE_REPEATER_ROOM`)

- **A Zephtacore repeater can now host an integrated Room Server** (`CONFIG_ZEPHCORE_REPEATER_ROOM=y`, compiled in by
  default on Zephtacore repeaters and off on standard repeaters; off at runtime until `set z.room.on on` or enabled in
  the Web Builder). The node keeps its full repeater identity
  (`ADV_TYPE_REPEATER`) for discovery, neighbors, route names and forwarding, while announcing a secondary virtual room
  identity (`ADV_TYPE_ROOM`, persisted in `/lfs/repeater/room.id` with its own `/lfs/repeater/room_acl`, staggered +30 s
  behind repeater adverts).
- **Access modes and automatic derived names (`z.room.*`).** `set z.room.access open|readonly|private` controls room
  access independently of the repeater's `guest.password`: `open` (default, `"<name> Room"`), `readonly` (guest read
  only, `"<name> Room [RO]"`), and `private` (requires `z.room.password` or the repeater admin password, `"<name> Room [Priv]"`). Changing `z.room.access` or `name` automatically re-advertises the updated room name. Also configurable
  from the Web Builder (`custom_builder_web/builder.html`). See `docs/CLI_commands.md`.
- **Changing the access mode applies at once.** `private` removes every client except admins, who log in again with
  `z.room.password`; `readonly` turns read-write clients into read-only guests; back in `open`, a guest becomes
  read-write the next time it logs in. The room's client list is saved right away.
- **Posting to a read-only room now gets an answer.** The virtual room and the room server ACK the post and reply
  `Error: room is read-only`, so the app stops resending it by flood. Before, the post got no reply at all.
- **Room posts survive a mesh time-sync step.** When the clock is set forward, the room server and the virtual room
  move their stored posts and each client's sync point with it, so a client that logs in after the step still
  receives posts made before it.
- **Mesh time sync is enabled by default on new nodes.** Freshly provisioned nodes across all roles start with
  `meshtimesync` on (`set meshtimesync on`), and the Web Flasher automatically sets `time <epoch>` during setup.
