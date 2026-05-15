# EchoEar VAD Interrupt And Device-Side AEC

This document describes the current interrupt behavior around VAD, AEC, and TTS
playback. The relevant code is in `main/audio_processing/afe_audio_processor.cc`
and `main/application.cc`.

## Current AFE setup

`AfeAudioProcessor::Initialize()` builds the AFE input format from the board
codec:

- microphone channels are marked `M`
- codec reference channels are marked `R`
- AEC mode is `AEC_MODE_VOIP_HIGH_PERF`
- VAD mode is `VAD_MODE_0`
- noise suppression is enabled when an NS model is available
- AGC is enabled

With `CONFIG_USE_DEVICE_AEC`, both AEC and VAD are initialized:

```cpp
afe_config->aec_init = true;
afe_config->vad_init = true;
```

Without `CONFIG_USE_DEVICE_AEC`, AEC is disabled and VAD remains enabled:

```cpp
afe_config->aec_init = false;
afe_config->vad_init = true;
```

`EnableDeviceAec(true)` currently enables both AEC and VAD. Older notes saying
VAD and AEC are mutually exclusive are stale for this repo state.

## VAD signal path

The AFE task fetches processed audio frames and watches `res->vad_state`.

When VAD changes:

- `VAD_SPEECH` calls the registered VAD callback with `true`
- `VAD_SILENCE` calls it with `false`
- processed PCM is still forwarded to the output callback

Application-level handling stores the current voice-detected state and raises
`MAIN_EVENT_VAD_CHANGE`.

## Interrupt during speaking

Interrupt only runs on this path:

- device state is `kDeviceStateSpeaking`
- AEC mode is `kAecOnDeviceSide`
- VAD currently reports speech

When EchoEar enters speaking with device-side AEC:

- the audio processor is kept running or started
- listening mode is forced to `kListeningModeRealtime`
- wake word detection stops
- the speaking start time is recorded
- VAD debounce state is reset

The main event loop applies two filters before interrupting playback:

- a 1 second grace period after speaking starts
- a 400 ms VAD debounce requirement

After VAD remains active long enough, EchoEar schedules:

```cpp
AbortSpeaking(kAbortReasonNone);
SetListeningMode(kListeningModeRealtime);
```

That stops the current TTS path and returns the device to listening so the user's
speech can be captured.

## Behavior without device-side AEC

When device-side AEC is not active, the interrupt path above is not used. During
speaking, EchoEar normally stops the audio processor unless realtime listening is
already active. AFE wake word detection may run if configured, but ordinary VAD
interrupt during playback is not the expected path.

## Practical expectations

For reliable playback interruption:

- build with device-side AEC support
- ensure the EchoEar codec exposes the expected reference channel
- keep the audio processor running while speaking
- expect the first second of playback to ignore VAD by design
- expect speech shorter than the debounce window to be ignored

## Agent cautions

- Do not document this as a future-only feature; current code includes a VAD
  interrupt path for device-side AEC.
- Do not claim VAD is disabled whenever AEC is enabled; current code enables
  both for device-side AEC.
- Do not treat server-side AEC as equivalent to this path. The local interrupt
  gate specifically checks for `kAecOnDeviceSide`.
- Do not remove the grace/debounce behavior when explaining why a quick sound did
  not interrupt playback.
