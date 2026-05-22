# MQTT Improvements Implemented

This file summarizes current MQTT behavior.

## Subscription Reliability

`MqttProtocol` subscribes immediately after connect and retries in the
`OnConnected` callback. The callback retry is the reliable point after CONNACK.

## Control Messages

MQTT handles:

- `hello`
- `ws_start`
- `goodbye`
- `remote_anim_update`
- `wifi_reconfig_nimble`
- `wifi_clear_credential`
- `switch_wifi_to`

## `ws_start`

`ws_start` sets alarm mode and opens WebSocket if not already open. It does not
guarantee a fresh WebSocket session if one is already active, because
`OpenWebSocketConnection()` returns early for an open channel.

## Audio Routing

WebSocket takes priority for audio while open. MQTT can remain connected for
control messages.

## Debugging

Inbound MQTT logs include topic, payload length, and a payload prefix. Use
`MQTT_DEBUGGING_GUIDE.md` for broker/ACL checks.
