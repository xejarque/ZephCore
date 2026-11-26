# ADR 0021 — Rescue copy of a server node in the settings NVS

Status: accepted (2026-11-26).

## Context

A repeater or room server is reached only over the air. Its identity, settings and regions live in one LittleFS
volume. A volume that does not mount is formatted at boot, and a file that does not load is replaced by a new key or
by factory settings. Either one leaves the owner without remote control, and leaves the node on the wrong radio
parameters, password, regions or Meshtastic repeat settings.

The settings NVS (`storage_partition`) is a separate flash region that no server role uses: Bluetooth is off, so it
holds no bonds. A new partition would move this fork away from upstream's flash map.

## Decision

- A server role keeps a copy of three files in the settings NVS: the identity, `prefs.json` and `regions2`. The ACL
  is not copied.
- Built where the node is a repeater or a room server and the settings backend is NVS
  (`CONFIG_ZEPHTACORE_RESCUE_COPY`). No partition changes.
- The files are copied byte for byte, so the copy does not depend on their formats. A file larger than one NVS entry
  is split into chunks. Each file has its length and CRC-32; an item that does not check out is not used. A file
  over 6 KB is not copied.
- Entries live under `ztc/rc/`. Firmware that does not know them leaves them alone.
- LittleFS is the source of truth. The copy is read only when a file is missing or does not load, and then it wins
  over the legacy binary `prefs` file.
- Writes:
  - at boot, only items that are absent or broken, from files that loaded; a valid item is never replaced;
  - after an administration command, the items whose file changed;
  - automatic saves (radio calibration) do not write the copy;
  - nothing until the reboot after `erase`, after a file that could not be written back, or when the copy could
    not be read.
- A restore writes the file back and counts it. `get z.rescue` answers the state of the copy, the count and the time
  of the last restore when the clock was set. If the filesystem cannot be written, identity and settings are loaded
  into RAM from the copy; regions are not.
- The first-boot format keeps the settings NVS when it holds a valid item, or when reading it failed. The triggers of
  that format do not change. `erase` and the formatter tool still clear everything.
- A settings backend that fails to start on a server role is erased once and started again: the region may hold
  another firmware's bytes.

This amends the rule that settings persist in `prefs.json` only: the copy is a mirror of it, never a second source
while the file loads.

## Consequences

- After the loss of the filesystem the node comes back with the same key, password, radio, regions and repeat
  settings of both meshes. Clients log in again, and permissions granted without a password are granted again.
- The copy holds the last state saved by this fork. Changes made while running other firmware are copied after the
  first administration command back on this one.
- Switching to upstream ZephCore keeps the copy. `erase`, the formatter, or companion firmware (which formats a
  server volume) remove it. Firmware that does not touch the region leaves it, and returning restores the old
  configuration: run `erase` before giving a node away.
- The private key and passwords are stored twice, with the same physical-access exposure as `/lfs`.
- Losing the filesystem and failing to read the NVS in the same boot can still lose the node's configuration.
- MG24, nRF54L and STM32WL keep settings inside LittleFS and get no copy until they have an NVS region.
- Worst-case size is about 7.5 KB (32 regions) of about 12 KB usable in a 16 KB region.
