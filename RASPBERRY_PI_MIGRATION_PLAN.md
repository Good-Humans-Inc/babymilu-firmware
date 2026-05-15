# Raspberry Pi Migration Plan

Status: archived planning reference.

This repository is currently EchoEar ESP32-S3 firmware, not a Raspberry Pi
runtime. The notes below should be treated as a future product exploration, not
current build documentation.

## Current Baseline To Port From

If this idea is revisited, use the current EchoEar-only codebase as the source
model:

- Board behavior: `main/boards/echoear/echoear.cc`
- State machine: `main/application.cc`
- Protocols: `main/protocols/mqtt_protocol.cc`,
  `main/protocols/websocket_protocol.cc`
- Animation assets: `/sdcard/test.bin` plus `/sdcard/startup.gif`
- Display behavior: `main/display/lcd_display.cc`
- Error logs: `main/error_log_uploader.cc`

Do not base new planning on removed multi-board directories or the old board
gallery docs.

## Suggested Future Architecture

A Raspberry Pi version would likely be a new Python or C++ application rather
than a direct ESP-IDF port.

Likely modules:

- `application`: state machine equivalent to `Application`.
- `audio`: microphone capture, playback, Opus or PCM transport.
- `display`: local rendering for GIF/emotion state.
- `protocols`: MQTT control and WebSocket audio.
- `assets`: loader for the 20-GIF emotion bundle.
- `hardware`: GPIO/touch/battery equivalents if hardware exists.

## Migration Risks

- ESP-IDF APIs, FreeRTOS tasks, NVS, and ESP drivers do not port directly.
- EchoEar-specific LCD/touch/BMI270 code would need replacement hardware
  drivers.
- Wake word/AEC behavior would need a Linux audio pipeline.
- The server contract should remain compatible before changing firmware
  behavior.

## Recommendation

Keep this as an archived idea until there is an actual Raspberry Pi target,
hardware bill of materials, and runtime decision. Current docs and builds should
stay focused on EchoEar ESP32-S3.
