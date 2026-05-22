# EchoEar Firmware Codebase Walkthrough

This document is the current high-level map for the EchoEar-only firmware. It is
written for engineers and LLM agents that need to orient quickly before editing.

## Current Repository Contract

- EchoEar is the only supported board.
- `main/boards/echoear/echoear.cc` is the only board implementation compiled.
- `main/boards/common` remains active shared support, not a removable board.
- `main/Kconfig.projbuild` exposes only `BOARD_TYPE_ECHOEAR`.
- `main/CMakeLists.txt` hard-sets `BOARD_TYPE` to `echoear`.
- Build target should be ESP32-S3.

## Main Components

| Area | Primary files | Purpose |
| --- | --- | --- |
| Application state | `main/application.cc`, `main/application.h` | Device state machine, protocol routing, audio send/receive, OTA startup tasks |
| EchoEar board | `main/boards/echoear/echoear.cc`, `main/boards/echoear/config.h` | LCD, touch, BMI270, charge monitor, buttons, power save, SD startup assets |
| Shared board support | `main/boards/common/*` | WiFi, BLE provisioning, HTTP/MQTT/WebSocket factories, backlight, board base classes |
| Audio | `main/audio_codecs/*`, `main/audio_processing/*` | Box codec, AFE/noise processing, wake word, Opus path |
| Display | `main/display/lcd_display.*`, `main/display/display.*` | LCD UI, GIF rendering, emotion display |
| Animation | `main/animation/*`, `crop_and_pack_gifs.py` | SD-card `test.bin` GIF loading, startup asset update, animation update loop |
| Protocols | `main/protocols/mqtt_protocol.*`, `main/protocols/websocket_protocol.*` | MQTT control channel and WebSocket audio channel |
| IoT/MCP | `main/iot/*`, `main/mcp_server.cc` | Device tools exposed to the server/LLM |
| Logs | `main/error_log_uploader.*` | Upload `/sdcard/err.txt`, then capture warnings/errors for next boot |

## Startup Flow

1. EchoEar board initializes I2C, BMI270, charge monitor, CST816 touch, LCD,
   buttons, power save, and GPIO7 touch button.
2. SD card startup runs and tries to show `/sdcard/startup.gif` early.
3. Network starts through `WifiBoard`.
4. Existing `/sdcard/err.txt` is uploaded, then log capture is enabled for later
   warnings/errors.
5. OTA/server configuration is checked through `CheckNewVersion()`.
6. Animation updater checks remote `test.bin`, `startup.gif`, and `startup.wav`
   when network is ready.
7. MQTT starts as the primary control protocol. WebSocket opens on demand for
   audio.

## Protocol Model

- MQTT is the long-lived control channel when configured.
- WebSocket is preferred for audio whenever its channel is open.
- `Application::GetActiveProtocol()` returns WebSocket first, otherwise MQTT.
- MQTT `ws_start` stores a WebSocket URL/version when valid, sets alarm mode, and
  opens WebSocket.
- WebSocket `listen:start` switches to AutoStop listening mode.
- TTS stop on WebSocket can resume listening automatically in alarm or remote
  wake flows.

## WiFi Provisioning

BLE provisioning is provided by `main/boards/common/wifi_board.cc`.

- Advertised BLE name: `BabyMilu`.
- Supported credential formats:
  - `ssid:<name>` then `pwd:<password>`
  - `wifi:<ssid>:<password>`
- Remote MQTT commands can enter BLE config mode or clear saved credentials.
- `ClearWifiConfiguration()` clears saved WiFi entries and WebSocket settings.

## Animation Contract

The current runtime format is GIF-based:

- `test.bin` must contain exactly the 20 packed emotion GIF assets.
- `startup.gif` must be separate at the SD-card root and must not be inside
  `test.bin`.
- `crop_and_pack_gifs.py` is the current packer.
- Older `normal_all.bin` and `animations_mega.bin` docs are legacy references
  only unless the code is explicitly reworked.

## Build Notes

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

`managed_components/` is ignored except for the local custom
`78__esp-wifi-connect` files that are explicitly unignored in `.gitignore`.

## Agent Editing Rules

- Treat deleted non-EchoEar board directories as intentionally removed.
- Do not restore old board docs, board galleries, or multi-board Kconfig lists.
- Prefer updating docs to point at `main/boards/echoear/echoear.cc`.
- Keep documentation current-state first; mark old proposals as historical.
