# Zephtacore 1.18.2-ztc.2

Based on ZephCore `1.18.2-zephcore`.

---

## Web builder and flasher

- The builder no longer sends a TX power unless one is typed: the node keeps its board's default, and the downloaded
  `custom_node.conf` carries none. Before, 22 dBm was sent to every board, above the safe value of a board whose radio
  drives an external amplifier (ProMicro SX1262 with an E22-900M30S: 10 dBm).
- Going back to the builder keeps the settings still waiting to be sent, instead of replacing them with a new name and
  password.
- After an install the pending settings are sent on their own when the browser already has permission for the board's
  port; otherwise the button sends them as before. The flasher first asks the node its role, waits for it to boot, and
  sends nothing to a node of another role.
- A send that stops says so in a dialog and keeps the settings. The admin password counts as set only when the node
  echoes it, and the final dialog says whether one was sent.
- The nRF52 progress bar no longer goes back between the stages of an install.
- The flasher preselects no board: it has to be chosen, unless the builder chose it. The list marks the boards with no
  published build.
- While it installs or sends settings the flasher turns its controls off and warns before the page is left. It closes
  an open serial console before flashing, and the flash controls are off while a backup runs.
- nRF52: the firmware package is downloaded when the board is chosen, and a bootloader port the browser already allows
  is used without asking for it again.
- "Bot de Ping" turns on both bots, MeshCore (`#bot`) and Meshtastic (`Bots`), on a companion and a repeater. Before,
  only the Meshtastic one was turned on.
- A companion set to relay Meshtastic (`essential` or `all`) also gets `set z.mt.role client`; as `client_mute` it
  repeated nothing.
- Choosing GPS on or off for a board with no GPS detected no longer stops the send before the reboot; the final dialog
  says the setting was not applied.
- The pending settings banner goes away once they are sent.
