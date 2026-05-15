# TTS Stop And Listening Resume Notes

This document replaces an older investigation note with the current expected
behavior and diagnostics.

## Current Expected Behavior

Primary code path: `main/application.cc`.

When WebSocket receives:

```json
{"type":"tts","state":"start"}
```

the app enters speaking state. If it was listening, it sends a listen stop first
to avoid microphone audio interfering with playback.

When WebSocket receives:

```json
{"type":"tts","state":"stop"}
```

the app waits for background audio work to finish, then:

- goes idle if the user aborted speaking;
- resumes listening in alarm mode;
- resumes listening if it was listening before TTS;
- otherwise goes idle for manual stop mode;
- resumes listening for AutoStop mode.

## Alarm Mode

Alarm mode is set by MQTT `ws_start`. In this mode, the expected sequence is:

1. MQTT `ws_start`.
2. WebSocket opens.
3. Firmware enters AutoStop listening mode.
4. Server sends TTS.
5. Firmware stops listening during TTS.
6. Server sends TTS stop.
7. Firmware starts listening for the user response.

## Useful Logs

Look for:

- `Opening WebSocket connection for ws_start`
- `Alarm mode: WebSocket opened via ws_start`
- `TTS starting while listening (WebSocket) - stopping listening before TTS`
- `TTS stop (WebSocket): state=...`
- `TTS stopped (WebSocket), alarm mode - starting listening for user response`
- `Received listen:start from server (WebSocket)`

## Failure Patterns

If the device stays idle after TTS:

- Confirm the server sent `{"type":"tts","state":"stop"}`.
- Confirm the message reached the WebSocket handler, not only MQTT.
- Confirm WebSocket was still open at TTS stop.
- Confirm the user did not abort playback.
- Confirm message fields are present and string typed.

## Caution

Some protocol handlers assume required JSON fields exist after checking only the
message type. Server test payloads should be complete and schema-valid to avoid
undefined behavior or crashes.
