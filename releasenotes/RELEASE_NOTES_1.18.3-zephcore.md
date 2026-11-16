# ZephCore 1.18.3-zephcore

Zephtacore, the build that also hears and repeats Meshtastic, repeats less by default and keeps its
default channels and node name in order.

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
  they dropped. The same commands exist for Meshtastic as `z.mt.fw.drop`, `.gap` and `.rate` (rules are ports;
  the gap is per NodeInfo). See `docs/CLI_commands.md`.

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
  traceroute are repeated, and nothing is dropped when another node repeats it first. `z.mt.relay all`
  still repeats the rest as before. `essential` also repeats position, so nodes show on the map, and the frames of
  channels the node holds no key for (`z.mt.relay.opaque on`); both give way when another node repeats them first.
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
- The default `MT:` channels and `#test` / `#bot` are added once per firmware revision. Deleting one in the
  app now sticks; before, it came back on the next restart.
- A Meshtastic node heard for the first time is added as a contact only as your auto-add settings
  allow; with manual adding it shows up as a discovered contact instead.
- Renaming the node changes the name in its Meshtastic NodeInfo too, without a restart.
- Settings of the Meshtastic side live in `prefs.json` only in builds that include it.
- New ping bot on the companion and the repeater: it answers a command of your choice on MeshCore `#bot` and on
  Meshtastic `Bots` with the hops, the SNR and the route (all of it in MeshCore, one hop per line with the RSSI/SNR after the last, the
  last relay in Meshtastic). Every ping is answered, and the companion shows its answer in the app.
  Off until you pick the command: `set z.mc.ping.enable on /ping`, `set z.mt.ping.enable on /ping`; `get z.mc.ping.status`
  shows the counters.
- New `get z.mc.neighbors` and `get z.mt.neighbors` (alias `vecinos`) on the companion and the repeater: the nodes heard directly,
  best SNR first, as `name (SNR, age)`, 4 to a page (`get z.mt.neighbors 2`), or the latest heard first with `recent` (`get z.mt.neighbors recent`). MeshCore lists repeaters heard with no hops, Meshtastic nodes heard with no relay
  in between. Kept in RAM, so a reboot empties them.
- New `get z.info` (version, uptime, battery, flash, heap and contacts in use) and `get z.stats` (MeshCore and Meshtastic counters), each in
  one line that fits a radio reply.
- New dashboard page on 200x200 e-paper companions (ThinkNode M1): MeshCore and Meshtastic side by side, with packet counts, nearest
  neighbours and their signal, radio setup, heap, flash and time on air. It is the first page, and redraws when opened, on a button press
  and once an hour.
- Commands of the Meshtastic side and the new ones hang from `z.`: `z.mt.*`, `z.mc.ping.*`, `z.mt.ping.*`, `z.ping.*`;
  `get tx.stalls` is now `get z.tx.stalls`.
- `help` at the end of a `z.` command (`get z.mt.relay help`) tells its usage, and on a group (`get z.mt help`) lists its keys.
  Every on/off `z.` setting (`z.mt.enable`, `z.mt.relay.opaque`, `z.ping.*`, `z.mc.ping.enable`, `z.mt.ping.enable`) also takes `1` and `0`.

## Radio

- SX126x: changing the sync word between MeshCore and Meshtastic no longer waits for the radio's
  sleep phase when RX duty cycle is on.

---

## Meshtastic phone app (companion)

- With `boards/common/zephtacore_app.conf` the companion also speaks the Meshtastic client protocol over BLE, so the
  Meshtastic app can connect next to the MeshCore one (one at a time). It lists the Meshtastic channels and then the
  MeshCore ones as `MC:<name>`, every contact as a node, and sends and receives text, private messages and
  traceroutes. Settings are still changed from the CLI. The advertised name is cut to 11 characters. See
  `docs/adr/0017-meshtastic-phone-app.md`.
