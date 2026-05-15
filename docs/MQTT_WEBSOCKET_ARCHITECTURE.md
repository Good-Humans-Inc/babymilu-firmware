# MQTT And WebSocket Architecture

This document describes the current EchoEar networking model.

## Primary Files

- `main/application.cc`
- `main/protocols/mqtt_protocol.cc`
- `main/protocols/websocket_protocol.cc`
- `main/boards/common/wifi_board.cc`

## Protocol Roles

- MQTT is the persistent control channel when configured.
- WebSocket is opened on demand for audio and takes priority while open.
- `Application::GetActiveProtocol()` returns WebSocket first, otherwise MQTT.
- MCP messages are routed over the active protocol.

## MQTT Startup

MQTT settings are stored in NVS under the `mqtt` namespace. If no endpoint is
stored and `DEFAULT_MQTT_ENDPOINT` was compiled in, startup seeds:

- endpoint
- client ID from MAC address
- publish topic from `DEFAULT_MQTT_PUBLISH_TEMPLATE`

Build-time defaults:

```powershell
idf.py build -DDEFAULT_MQTT_ENDPOINT="mqtt.example.com:1883" `
             -DDEFAULT_MQTT_PUBLISH_TEMPLATE="xiaozhi/%s/up"
```

MQTT subscribes immediately and retries from the `OnConnected` callback to avoid
CONNACK timing races.

## `ws_start`

MQTT `ws_start` means server-initiated WebSocket conversation/alarm mode.

Expected payload:

```json
{
  "type": "ws_start",
  "wss": "wss://server/path?token=...",
  "version": 3
}
```

Firmware behavior:

1. Set alarm mode.
2. Validate the WebSocket URL.
3. Save valid URL/version in the `websocket` settings namespace.
4. Call `OpenWebSocketConnection()`.
5. If WebSocket is already open, the call returns early and reuses it.
6. Once open, the device enters AutoStop listening mode.

Invalid localhost-style URLs are not saved; WebSocket falls back to its
configured/default URL.

## WebSocket Audio

Current WebSocket v3 behavior:

- Sends a `hello` requesting 16 kHz audio.
- Adds query params for `device-id` and `client-id` when absent.
- Sends `Protocol-Version` and auth headers where configured.
- Uses raw Opus binary frames for audio.
- Uses JSON text frames for control messages.

## Listen And TTS

- Server `listen:start` sets AutoStop listening.
- Server `listen:stop` stops listening.
- TTS start while listening causes firmware to send `listen:stop` first.
- TTS stop is required for the firmware to transition out of speaking.
- In alarm mode, TTS stop resumes listening and clears alarm mode.

## WiFi Control Over MQTT

- `wifi_reconfig_nimble`: preserve existing credentials, set BLE config flags,
  reboot, advertise as `BabyMilu`.
- `wifi_clear_credential`: clear SSIDs plus `wifi` and `websocket` settings,
  then enter BLE config mode.
- `switch_wifi_to`: switch to an existing saved SSID.

## EchoEar Build Note

This repo is EchoEar-only. Ignore old docs that mention selecting Waveshare or
other boards in menuconfig.
