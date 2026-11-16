# 0017: Meshtastic phone app as a companion client

Status: accepted

## Decision

With `CONFIG_ZEPHTACORE_MT_APP` the companion also serves the Meshtastic client
protocol over BLE: a second GATT service next to the MeshCore one, so either phone
app can connect (one at a time). The repeater never carries it.

- The protobuf messages are written by hand (`MtProto.h`, `MtPhoneWire`), as the
  rest of the bridge: no generated code, the licence stays MIT.
- The session (`MtPhoneApi`) is pure code over a `PhoneHost` interface; the glue
  that reaches the node's tables is in `CompanionMeshtastic.cpp`.
- The app has 8 channel slots: the Meshtastic channels first, then the MeshCore
  ones as `MC:<name>`, in slot order. Every MeshCore contact is a node whose number
  is the start of its public key.
- Private messages: Meshtastic nodes by PKI (their key comes from their NodeInfo,
  which the app's user-info request asks for); MeshCore contacts by MeshCore.
- Traceroute: Meshtastic nodes on a channel (PKI is never used for it, so the nodes
  on the way can annotate the route); MeshCore contacts by path discovery.
- GATT layout version 3 with the option, 2 without it.
- Advertising carries the Meshtastic UUID in the scan response and the name cut to
  11 characters, which MeshCore apps see too.

## Limits

Routes of 7 hops each way, SNR per hop reported as not measured, no
configuration from the app (settings stay on the CLI), reactions are not bridged.
