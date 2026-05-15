# Server Contract For WebSocket Alarm Mode

This document describes the current server-facing behavior for MQTT `ws_start`
and WebSocket listening.

## Firmware Entry Points

- MQTT handler: `main/protocols/mqtt_protocol.cc`
- WebSocket handler: `main/protocols/websocket_protocol.cc`
- Application state: `main/application.cc`

## MQTT `ws_start`

The server may publish a control message like:

```json
{
  "type": "ws_start",
  "wss": "wss://example.com/xiaozhi/v1/?token=...",
  "version": 3
}
```

Firmware behavior:

1. Marks the interaction as alarm/server-initiated mode.
2. Saves a valid WebSocket URL and version to NVS.
3. Opens the WebSocket audio channel.
4. Switches to AutoStop listening mode once the channel is ready.

Invalid localhost-style URLs are ignored and the WebSocket protocol falls back to
its configured/default URL.

## WebSocket Listen Messages

The server may explicitly control listening over WebSocket:

```json
{
  "type": "listen",
  "state": "start",
  "session_id": "..."
}
```

```json
{
  "type": "listen",
  "state": "stop",
  "session_id": "..."
}
```

`listen:start` sets AutoStop listening mode. `listen:stop` stops listening.

## TTS And Alarm Flow

Recommended server flow:

1. Send MQTT `ws_start`.
2. Wait for the device to open WebSocket and send/enter listening.
3. Send alarm or prompt TTS over WebSocket.
4. Send `tts` stop when playback is complete.
5. Firmware resumes listening automatically in alarm mode.
6. Use explicit `listen:start` for follow-up turns if needed.

The firmware also handles the case where TTS starts while listening by sending a
listen stop before entering speaking state.

## Message Requirements

Server messages should include all fields expected by the firmware:

- `type` for every JSON message.
- `state` for `tts` and `listen`.
- `session_id` where the server tracks sessions.
- `wss` and `version` for `ws_start`.

Avoid sending partial `tts` or `listen` messages.

## Testing Checklist

- MQTT subscribe succeeds for the device down topic.
- `ws_start` opens WebSocket and logs alarm mode.
- Device sends/enters `listen:start` after WebSocket opens.
- TTS start switches device to speaking.
- TTS stop resumes listening in alarm mode.
- WebSocket close resets alarm mode and returns device to idle.
