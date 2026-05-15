# WebSocket Audio Notes

This document records the current WebSocket audio behavior.

## Current Behavior

- WebSocket protocol version 3 sends raw Opus binary frames.
- Audio packets are not wrapped in JSON for v3.
- The `hello` request asks for 16 kHz server audio.
- Query params `device-id` and `client-id` are added if missing.
- `Protocol-Version` is sent as a header.
- The active protocol is WebSocket while its audio channel is open.

## Safety Checks

`WebsocketProtocol::SendAudio()` checks that the WebSocket object exists and is
connected before sending.

## `ws_start` URLs

If MQTT `ws_start` includes a URL that already has query params, the WebSocket
code avoids duplicating `device-id` and `client-id`.

Invalid localhost-style URLs are not saved; the WebSocket protocol falls back to
the configured/default URL.

## TTS Interaction

When TTS starts during listening, firmware sends `listen:stop` first. When TTS
stops in alarm mode, firmware resumes listening.
