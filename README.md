# EchoEar BabyMilu Ground Firmware Stage 1

This is a self-contained ESP-IDF Stage 1 firmware under `tmp/` for EchoEar
hardware talking to the current BabyMilu voice-platform server.

It was seeded from `tmp/echoear-ground-firmware` for the clean audio task split
and adapted with current BabyMilu/EchoEar contracts from `firmware/esp32` and
`services/voice-platform`.

## Included

- EchoEar ES8311 speaker codec and ES7210 microphone codec wiring.
- ESP-SR AFE path for audio processing and device-side AEC.
- Raw binary Opus WebSocket audio at 16 kHz mono, 60 ms frames.
- Deeper incoming Opus queue for smoother TTS playback.
- BabyMilu OTA/config hydration from `/babymilu/ota/` using `Device-Id`.
- Runtime persistence for `websocket`, `mqtt`, `server_time`, and optional
  `firmware` update metadata.
- MQTT control path for `ws_start`, `remote_anim_update`,
  `wifi_reconfig_nimble`, `wifi_clear_credential`, `switch_wifi_to`, and
  `set_ota_url`.
- Canonical `connectionType` propagation through WebSocket URL, hello, and
  listen messages.
- Saved Wi-Fi credential scanning with exact SSID matching, one-shot preferred
  SSID, and Firestore `wifiSetting.rankedNetworks` local reordering.
- BLE Wi-Fi provisioning compatible with the legacy BabyMilu phone flow:
  advertise as `BabyMilu`, service `0x0180`, read `0xFEF4`, write `0xDEAD`,
  and accept `ssid:` plus `pwd:` or combined `wifi:SSID:PASSWORD`.
- EchoEar ST77916 QSPI LCD rendering through LVGL GIF.
- Startup media phase: show `/sdcard/startup.gif`, play `/sdcard/startup.wav`,
  then continue Wi-Fi and packed-bundle initialization.
- Three read-only animation updater checks for `/sdcard/startup.gif`,
  `/sdcard/startup.wav`, and `/sdcard/test.bin`.
- Lazy GIF extraction from packed `test.bin` using PSRAM allocations, with
  optional `_start.gif` playback finishing via `LV_EVENT_READY` before the loop
  GIF is shown.

## Explicitly Omitted

- Legacy activation code/challenge flow. Unexpected `activation` fields are
  logged and ignored.
- Old `/xiaozhi/v1/`, port `8003`, and port `8080` defaults.
- Eager boot loading of every GIF.
- Cloud mutations, deploys, Firestore writes, IAM/scheduler/function changes.
- Legacy NimBLE extras such as light/fan test commands or AP fallback
  configuration.

## Endpoint Defaults

Menuconfig defaults are full URLs:

- OTA: `http://35.225.248.38:8000/babymilu/ota/`
- WebSocket: `ws://35.225.248.38:8000/babymilu/v1/`
- MQTT: `mqtt://35.225.248.38:1883`

These are defaults only. OTA hydration wins at runtime and may persist
WebSocket, MQTT, server time, and firmware update metadata.

## Server Contract

- WebSocket path: `/babymilu/v1/`
- Protocol version: `3`
- Uplink/downlink audio: raw binary Opus frames, 16 kHz mono, 60 ms
- Hello includes `audio_params`, `device_id`, `client_id`, and
  `connectionType`
- Listen messages emit `connectionType`, not `mode` or `connection_type`
- `tts:start` or `tts:sentence_start` while listening is treated as
  server-authoritative ASR finalization: firmware sends one `listen:stop`,
  stops uplink capture, and switches to playback
- Local VAD is not used to finalize turns or interrupt TTS in Stage 1
- Server `llm.emotion` strings are preserved:
  `normal`, `smirk`, `heart`, `blush`, `sad`, `laugh`, `sleep`, `starry`,
  `cry`, `angry`
- Utility visuals are `listening`, `wifi`, `battery`, and `silence`
- `remote_anim_update` may include `url` or `message`; the value is treated as
  the remote `test.bin` URL and sibling `startup.gif` / `startup.wav` URLs are
  derived unless explicit animation URLs are already persisted.

## Build

From an ESP-IDF 5.5-capable shell:

```powershell
cd tmp/echoear-babymilu-ground-firmware
idf.py set-target esp32s3
idf.py build
```

## Local Harness

The harness is local-only and does not call cloud services:

```powershell
cd tmp/echoear-babymilu-ground-firmware
python tools/contract_harness.py
```

It validates default endpoints, manifest parsing, activation-field ignore
behavior, WebSocket URL generation, hello payload shape, listen start/stop JSON,
and canonical `connectionType` propagation.

## Hardware Follow-Up

1. Provision Wi-Fi credentials through BLE from the phone app or nRF Connect.
   New BLE credentials are saved at lowest priority, then preferred once on the
   reboot after provisioning.
2. Flash on EchoEar hardware and verify startup shows `/sdcard/startup.gif`,
   plays `/sdcard/startup.wav`, then initializes lazy `/sdcard/test.bin`
   emotion loading.
3. Run a real BabyMilu WebSocket turn with the target `Device-Id`.
4. Trigger MQTT `ws_start` for both `reminder` and `alarm` and verify the
   server sees matching URL and hello `connectionType`.
5. Validate OTA firmware binary update only with a test manifest that includes
   both `firmware.version` and `firmware.url`.
