# MQTT Debugging Guide

## Current Code

- `main/protocols/mqtt_protocol.cc`
- `main/application.cc`

## Expected Connection Logs

Look for:

- MQTT config endpoint/client/topic logs.
- Broker parsed as host and port.
- Connected to endpoint.
- Initial subscribe attempt.
- `OnConnected` subscribe retry.
- Successful subscribe from `OnConnected`.

The retry is intentional because subscribing before CONNACK completion may fail.

## Useful Incoming Messages

- `ws_start`: opens WebSocket alarm/server-initiated session.
- `remote_anim_update`: triggers animation update loop.
- `wifi_reconfig_nimble`: reboots into BLE WiFi config mode.
- `wifi_clear_credential`: clears WiFi/WebSocket settings and enters BLE mode.
- `switch_wifi_to`: selects a saved SSID.

## Topic Defaults

If `DEFAULT_MQTT_ENDPOINT` is compiled in and no MQTT endpoint exists in NVS,
startup seeds the endpoint and publish topic from
`DEFAULT_MQTT_PUBLISH_TEMPLATE`.

## ACL Checks

If publish works but downlink does not:

- Confirm the device subscribed to the expected down topic.
- Confirm the broker ACL allows that subscription.
- Confirm MAC casing and topic format match the server.
- Check logs for `MQTT RX topic=...`.

## WebSocket Interaction

MQTT remains useful for control even when WebSocket is active for audio.
`Application::GetActiveProtocol()` sends audio/MCP over WebSocket while open.
