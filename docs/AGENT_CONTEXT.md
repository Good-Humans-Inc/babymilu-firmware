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

## Custom Power-Save Audio Handoff

Current shape after the custom sleep/wake SRAM debugging pass:

- Custom sleep is not ESP-IDF light sleep. It is an application-managed mode
  with reduced CPU frequency, LCD/backlight off, WiFi modem PS enabled, and
  selected runtime objects released.
- Entering custom sleep releases audio runtime SRAM from:
  `AfeAudioProcessor`, `AfeWakeWord`, Opus encoder/decoder, audio debugger,
  `background_task`, and `audio_encode`.
- Waking from custom sleep restores only the manual BOOT-talk audio path:
  codec input, Opus encoder/decoder, `AfeAudioProcessor`, and `audio_encode`.
- Wake word runtime intentionally stays released after custom wake. BOOT click
  opens WebSocket and talks through the manual path. Do not blindly recreate
  `AfeWakeWord` during custom wake unless SRAM budget is revisited.
- `background_task` intentionally stays released after custom wake. It is
  lazily recreated for sound effects, URL playback, and audio testing. Manual
  WebSocket talk must not depend on it.
- WiFi PS is restored on custom wake with `WifiBoard::SetPowerSaveMode(false)`;
  logs should show `[PWR_SAVE] WiFi PS after custom wake exit: 0 (none)`.
- The restore memory gate checks both total internal free SRAM and largest
  contiguous internal block. A failure like `free_sram=17623` with
  `largest_block=7424` means fragmentation, not total SRAM exhaustion.

Important files for this path:

- `main/application.cc`: runtime lifecycle, custom sleep/wake release/restore,
  manual audio input/output, MCP send scheduling.
- `main/application.h`: runtime helper declarations and task members.
- `main/audio_processing/afe_audio_processor.*`: shutdown support and
  `audio_communication` task stack.
- `main/audio_processing/afe_wake_word.*`: shutdown support for WakeNet/AFE.
- `main/background_task.*`: named task worker, timeout-aware completion wait.
- `main/system_info.*`: memory snapshots, heap maps, and task stack high-water
  diagnostics.
- `main/boards/echoear/echoear.cc`: custom sleep board gate, WiFi PS logging,
  CPU frequency changes.
- `main/animation/animation.cc`: `plat_animation_task` stack increased because
  it can touch LVGL and I2C-backed status helpers after wake.
- `main/protocols/websocket_protocol.*`: WebSocket receive JSON routing and
  audio frame logging.
- `managed_components/78__esp-ml307/web_socket.cc` and
  `managed_components/78__esp-ml307/include/web_socket.h`: WebSocket send
  serialization and RX thread stack. These are intentionally tracked despite
  most managed components being ignored.

Debugging history from the 2026-05-28 session:

- Initial problem: after custom wake, SRAM was low and restoring full audio
  runtime left too little room for BOOT-talk tasks. TTS/server hello sometimes
  arrived, but speaker output or mic uplink was unreliable.
- First fix: release AFE, WakeNet, Opus, debugger, background worker, and encode
  worker before entering custom sleep. This raised sleep-mode internal free SRAM
  from roughly single-digit KB to roughly 60-70 KB.
- Speaker output fix: decoded WebSocket audio is now handled in `AudioLoop`;
  output is enabled before PCM writes, and TTS stop waits for audio-output idle
  instead of the unrelated generic background worker.
- Mic uplink fix: Opus mic encoding moved out of `audio_communication` into a
  dedicated `audio_encode` task. Doing encode inline caused
  `audio_communica` stack overflow.
- Stack fixes exposed by wake tests:
  - `audio_communication` stack increased from 4096 to 8192.
  - `plat_animation_task` stack increased from 4096 to 8192 after a panic in
    `i2c_master_transmit_receive`.
  - generic `background_task` reduced from the original large always-on worker
    to a 16 KB lazy worker.
- Memory gate failure after those stack fixes was due to fragmentation. The
  largest contiguous block fell below 8 KB because `background_task` was being
  recreated even though manual BOOT-talk did not need it. The current shape keeps
  that worker released after wake.
- WebSocket crash fix: a double exception occurred while the RX thread handled
  MCP `initialize/tools/list` and the main loop sent the first MCP response.
  WebSocket sends are serialized, verbose MCP payload previews were removed, the
  WebSocket RX pthread stack was raised to 8192, and non-hello JSON handling is
  copied onto the application main loop. The RX thread still parses `hello`
  immediately because `OpenAudioChannel()` waits for server hello.

Known noisy or unresolved items:

- `i2s_channel_disable(1218): the channel has not been enabled yet` appears
  during codec input/output disable and restore. It has been treated as noisy
  unless paired with failed audio I/O.
- Heap fragmentation is still tight. Use
  `SystemInfo::PrintMemorySnapshot("<label>")` around changes to confirm both
  `free_sram` and `largest_block`.
- If another task overflows after wake, check its high-water mark in the memory
  snapshot before increasing stack. The recent task failures were sequentially
  exposed as earlier blockers were removed.
- Avoid reintroducing full wake-word restore, always-on background worker, or
  large MCP payload logging into the custom wake path without rechecking SRAM.

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
