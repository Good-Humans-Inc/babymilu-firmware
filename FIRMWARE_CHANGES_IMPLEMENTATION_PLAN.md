# Firmware Networking Changes: Current Status

This document used to be a forward implementation plan. It is now a current-state
summary of the MQTT/WebSocket behavior in the EchoEar-only firmware.

## Current Behavior

Primary files:

- `main/application.cc`
- `main/protocols/mqtt_protocol.cc`
- `main/protocols/websocket_protocol.cc`
- `main/boards/common/wifi_board.cc`

## MQTT Configuration

MQTT defaults can be compiled in from CMake:

```powershell
idf.py build -DDEFAULT_MQTT_ENDPOINT="mqtt.example.com:1883" `
             -DDEFAULT_MQTT_PUBLISH_TEMPLATE="xiaozhi/%s/up"
```

On first boot, `Application` seeds the MQTT endpoint, client ID, and publish
topic if no endpoint is already stored in NVS.

## MQTT Connection Handling

`MqttProtocol` subscribes both immediately after connect and again from the
`OnConnected` callback. This is intentional: the first subscribe may race before
CONNACK is fully processed, and the callback retry provides a more reliable
subscription point.

Inbound MQTT messages are logged with topic, length, and a payload prefix for
debugging.

## WebSocket Redirects

MQTT `ws_start` is treated as a server-initiated conversation/alarm trigger.

When `ws_start` arrives:

1. The firmware validates the `wss` URL.
2. A valid URL and protocol version are saved under the `websocket` settings
   namespace.
3. Alarm mode is enabled.
4. `Application::OpenWebSocketConnection()` is scheduled.

`OpenWebSocketConnection()` is idempotent when an audio channel is already open.
WebSocket takes priority for audio while open.

## Listen Flow

After WebSocket opens from `ws_start`, the app switches to AutoStop listening
mode. In alarm mode, TTS may interrupt listening; after WebSocket TTS stop, the
app resumes listening for the user response.

WebSocket and MQTT JSON handlers also accept `listen:start` and `listen:stop`
messages from the server.

## WiFi Control Messages

MQTT handles these WiFi control messages:

- `wifi_reconfig_nimble`: reboot into BLE WiFi config mode without clearing
  existing credentials.
- `wifi_clear_credential`: clear WiFi and WebSocket settings, then enter BLE
  config mode.
- `switch_wifi_to`: switch to a saved SSID by name.

## Remaining Caution

Some JSON handling paths still assume required fields exist after checking only
the message type. Server messages should include the documented fields exactly,
especially `type`, `state`, `session_id`, and WebSocket URL/version data.
