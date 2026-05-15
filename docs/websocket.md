# WebSocket Protocol Reference

This document describes the current firmware WebSocket behavior.

## Code

- `main/protocols/websocket_protocol.cc`
- `main/protocols/protocol.cc`
- `main/application.cc`

## Connection Setup

WebSocket is opened on demand, usually after MQTT receives `ws_start` or when the
user initiates a conversation and WebSocket is needed for audio.

When opening:

- URL is read from the `websocket` settings namespace or a default.
- `device-id` and `client-id` query params are added when missing.
- `Protocol-Version` header is sent.
- Authorization header is sent when a token is configured.
- Receive buffer is configured for larger server messages.

## Hello

After connecting, firmware sends a `hello` JSON message and requests 16 kHz
audio for the server stream.

The server should reply with a valid `hello` message using WebSocket transport.

## Audio Frames

For current protocol version 3, audio is sent as raw Opus binary frames. JSON
wrappers are not used for those audio packets.

## JSON Messages

Important incoming message types:

- `tts`: `state` is `start`, `stop`, or `sentence_start`.
- `stt`: contains recognized text.
- `llm`: contains an `emotion` string for display.
- `listen`: `state` is `start` or `stop`.
- `mcp`: contains MCP payload for tool/list/call handling.

Malformed messages are not guaranteed to be safely ignored. Include required
fields with correct types.

## Listening Behavior

- `listen:start` sets AutoStop listening.
- `listen:stop` stops listening.
- TTS start while listening sends `listen:stop` before speaking.
- TTS stop in alarm mode resumes listening for the user response.

## MCP

MCP messages can be carried over WebSocket while WebSocket is active. Responses
are sent over the active protocol and session.
