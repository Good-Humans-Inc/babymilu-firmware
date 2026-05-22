# WiFi Clear And Reconfiguration

This document describes current EchoEar WiFi clear behavior.

## Code

- `main/boards/common/wifi_board.cc`
- `main/protocols/mqtt_protocol.cc`
- `main/application.cc`

## What Clear Does

`WifiBoard::ClearWifiConfiguration()`:

1. Clears all saved SSIDs through `SsidManager`.
2. Erases all settings in the `wifi` namespace.
3. Erases all settings in the `websocket` namespace.

It does not by itself reboot. The MQTT `wifi_clear_credential` command calls
clear, then enters BLE WiFi config mode, which persists BLE flags and reboots.

## Remote Commands

```json
{"type":"wifi_reconfig_nimble"}
```

Preserves credentials and reboots into BLE config mode.

```json
{"type":"wifi_clear_credential"}
```

Clears WiFi and WebSocket settings, then reboots into BLE config mode.

```json
{"type":"switch_wifi_to","message":"SSID_NAME"}
```

Switches to a saved SSID if it exists.

## BLE Config

BLE advertises as `BabyMilu` and accepts:

- `ssid:<ssid>` followed by `pwd:<password>`
- `wifi:<ssid>:<password>`

## Agent Note

When documenting WiFi reset, mention WebSocket settings are cleared too. This is
important because `ws_start` URL/version state lives in the `websocket`
namespace.
