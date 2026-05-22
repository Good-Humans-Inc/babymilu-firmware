# Agent Context: EchoEar Firmware

This file is the compact current-state reference for LLM/agent work.

## Scope

- This repository is EchoEar-only.
- Do not reintroduce removed board directories, board gallery images, or
  multi-board Kconfig options.
- Documentation should describe EchoEar behavior by default.

## Build

- Target: ESP32-S3.
- Board symbol: `CONFIG_BOARD_TYPE_ECHOEAR`.
- Board source path: `main/boards/echoear`.
- Shared board support path: `main/boards/common`.
- CMake hard-sets `BOARD_TYPE` to `echoear`.

Useful commands:

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

## EchoEar Hardware Integration

- Audio codec path uses `BoxAudioCodec`.
- LCD is a 360x360 QSPI ST77916 panel.
- Touch controller is CST816 over I2C.
- GPIO7 capacitive touch is handled through `touch_button_sensor`.
- BMI270 is initialized on the shared I2C bus.
- Charge/battery monitoring is implemented in the EchoEar board file.

Primary files:

- `main/boards/echoear/echoear.cc`
- `main/boards/echoear/config.h`
- `main/display/lcd_display.cc`
- `main/animation/animation.cc`

## Network And Protocols

- WiFi/BLE behavior lives in `main/boards/common/wifi_board.cc`.
- BLE provisioning advertises as `BabyMilu`.
- MQTT is the persistent control channel when configured.
- WebSocket is opened on demand and takes priority for audio while open.
- MQTT `ws_start` saves a valid WebSocket URL/version and opens WebSocket.
- `remote_anim_update` triggers the animation updater.
- `wifi_reconfig_nimble` enters BLE WiFi config mode.
- `wifi_clear_credential` clears saved WiFi and WebSocket settings, then enters
  BLE config mode.

## Assets

Current SD-card asset contract:

- `/sdcard/test.bin`: 20 packed emotion GIFs.
- `/sdcard/startup.gif`: separate startup GIF.
- `/sdcard/startup.wav`: optional startup audio.
- `/sdcard/err.txt`: warning/error log uploaded on next startup.

`crop_and_pack_gifs.py` is the active GIF preparation script.

## Documentation Style

Make docs useful to future agents:

- Start with current behavior before history.
- Include exact file paths.
- Label obsolete ideas as historical.
- Avoid stale line numbers unless they are freshly checked.
- Do not reference deleted `docs/v0`, `docs/v1`, or `main/boards/README.md`.
