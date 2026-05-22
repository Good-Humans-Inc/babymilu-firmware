# Audio Flow Verification

This is the current high-level audio routing reference.

## Active Protocol Selection

`Application::GetActiveProtocol()` returns:

1. WebSocket when its audio channel is open.
2. MQTT primary protocol otherwise.

This means WebSocket owns audio during WebSocket sessions, while MQTT can remain
connected for control.

## WebSocket Audio

- Uses raw Opus binary frames for protocol version 3.
- Requests 16 kHz in the hello exchange.
- Sends stop/start listening JSON messages around TTS and listening state.

## MQTT Audio

MQTT remains the primary control channel and fallback protocol. It receives
`ws_start`, WiFi control commands, animation update requests, and other control
messages.

## TTS Flow

1. Listening sends microphone audio through the active protocol.
2. On TTS start, firmware stops listening if needed.
3. Device enters speaking state.
4. On TTS stop, firmware waits for playback work to finish.
5. Alarm or AutoStop modes resume listening; manual stop returns to idle.

## Verification Logs

Useful logs:

- `Opening WebSocket audio channel`
- `WebSocket connection opened successfully`
- `TTS starting while listening`
- `TTS stop (WebSocket)`
- `STATE: listening`
- `STATE: speaking`
