# EchoEar Firmware

This repository is an EchoEar-only ESP-IDF firmware tree. Multi-board support has
been removed from the build, and the only board implementation compiled by CMake
is `main/boards/echoear`.

## Current Build Shape

- Target board: EchoEar.
- Target chip: ESP32-S3.
- Board sources: `main/boards/echoear/*.cc` plus shared support from
  `main/boards/common`.
- Kconfig board option: `CONFIG_BOARD_TYPE_ECHOEAR`.
- CMake board selection: `main/CMakeLists.txt` sets `BOARD_TYPE` to `echoear`.
- Shared docs for agents: `docs/AGENT_CONTEXT.md`.

## Useful Commands

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
idf.py flash monitor
```

Optional MQTT defaults can be injected at build time:

```powershell
idf.py build -DDEFAULT_MQTT_ENDPOINT="mqtt.example.com:1883" `
             -DDEFAULT_MQTT_PUBLISH_TEMPLATE="xiaozhi/%s/up"
```

## Runtime Assets

EchoEar uses SD-card animation assets:

- `/sdcard/test.bin`: packed 21-GIF emotion bundle.
- `/sdcard/startup.gif`: startup display GIF, separate from `test.bin`.
- `/sdcard/startup.wav`: optional startup audio asset.
- `/sdcard/err.txt`: warning/error log file uploaded on the next network-ready
  boot, then recreated for future logs.

Use `crop_and_pack_gifs.py` to prepare `test.bin` and the root `startup.gif`
asset.

## Active Documentation

- `docs/AGENT_CONTEXT.md`: concise current behavior for LLM/agent work.
- `CODEBASE_WALKTHROUGH.md`: current codebase map.
- `docs/MQTT_WEBSOCKET_ARCHITECTURE.md`: MQTT/WebSocket architecture.
- `docs/websocket.md`: WebSocket protocol reference.
- `docs/ble-wifi-setup-guide.md`: BLE WiFi provisioning.
- `MIGRATION_13_TO_20_GIFS_GUIDE.md`: current 21-GIF asset workflow.
- `STARTUP_GIF_RENDER_FIX.md`: startup GIF display notes.

## Cleanup Note

Historical board galleries, non-EchoEar board sources, and unrelated scratch docs
have been removed or marked as archived. New documentation should describe the
EchoEar-only build unless it is explicitly labeled as historical.
