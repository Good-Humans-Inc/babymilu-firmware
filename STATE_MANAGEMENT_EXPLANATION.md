# EchoEar State Management

This page summarizes the current device-state behavior in `main/application.cc`.
It is written for agents that need to reason about listening, speaking, remote
wakeup, and TTS transitions.

## Core concepts

`device_state_` is exclusive. EchoEar can be idle, listening, speaking,
connecting, configuring WiFi, upgrading, activating, audio testing, or in fatal
error, but it is never in two device states at once.

`listening_mode_` is a configuration value, not a separate state:

- `kListeningModeAutoStop`: listen until silence/server flow stops it.
- `kListeningModeManualStop`: listen until an explicit stop/manual transition.
- `kListeningModeRealtime`: keep capture active for realtime/AEC behavior.

Changing listening mode with `SetListeningMode(mode)` stores the mode and then
enters `kDeviceStateListening`.

## Important state effects

### Idle

When entering idle:

- display status becomes standby
- emotion becomes `normal`
- audio processor stops
- wake word detection starts

### Listening

When entering listening:

- display status becomes listening
- emotion becomes `listening`
- IoT states are updated when the Xiaozhi IoT protocol is enabled
- `listen:start` is sent only if an active protocol exists and its audio channel
  is open
- audio capture starts only if the audio processor is not already running
- wake word detection stops

For remote wakeup with an open WebSocket and previous state idle, listening mode
is forced to `kListeningModeAutoStop` so TTS stop can resume listening.

If the protocol is not ready, the state can show listening without sending
`listen:start`; later WebSocket-open handling is expected to start capture when
the channel is ready.

### Speaking

When entering speaking:

- display status becomes speaking
- the decoder is reset for playback
- `speaking_start_time_us_` is recorded for VAD interrupt grace timing
- VAD debounce state is reset

If device-side AEC is enabled, the audio processor is kept running or started,
`listening_mode_` is set to realtime, and wake word detection stops. This is the
path that allows VAD interrupt during playback.

If device-side AEC is not enabled and mode is not realtime, the audio processor
stops during speaking. AFE wake word detection may remain active if that feature
is configured.

## TTS flow

When a `tts:start` message arrives while idle or listening:

1. `state_before_tts_` stores the current state.
2. If EchoEar was listening, it sends `listen:stop` before playback.
3. The device enters speaking.

When `tts:stop` arrives while speaking:

- If playback was aborted by the user/device, EchoEar goes idle and does not
  auto-resume.
- If a WebSocket audio channel is still open, EchoEar treats this as remote
  wakeup flow.
- In alarm mode, EchoEar always resumes listening after the first TTS and clears
  `is_alarm_mode_`.
- In normal remote wakeup, EchoEar resumes listening only if
  `state_before_tts_` was listening.
- In manual-stop mode without remote wakeup, EchoEar goes idle.
- In auto/realtime mode without remote wakeup, EchoEar resumes listening.

## Server listen messages

Current server-side `listen:start` handling sets AutoStop listening mode. This
means server-initiated listening is expected to return to listening after TTS
unless the flow was aborted.

`listen:stop` stops listening and returns toward idle according to the active
handler path.

## WebSocket remote wakeup

`ws_start` opens the WebSocket channel when needed. After the channel opens:

- If the device is idle, EchoEar waits briefly, verifies the channel is still
  open, and enters AutoStop listening.
- If alarm mode is set, EchoEar still sends `listen:start`; the server may send
  TTS first, and listening resumes after TTS stop.
- If the device is speaking, EchoEar aborts speech and enters AutoStop
  listening.
- If the device is already listening but capture is not running, EchoEar sends
  `listen:start` and starts capture.
- If the device is connecting, EchoEar enters AutoStop listening.

## Agent cautions

- Do not infer listening from state alone; also check protocol readiness and
  whether the audio processor is running.
- Do not assume TTS stop always goes idle. Remote WebSocket and alarm mode can
  resume listening automatically.
- Do not assume manual-stop mode survives remote wakeup unchanged; remote wakeup
  from idle forces AutoStop.
- Incoming `tts` handlers currently access `state->valuestring` directly, so
  malformed `tts` messages without a string `state` are not safely ignored.
