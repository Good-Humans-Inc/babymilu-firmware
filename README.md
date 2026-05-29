# EchoEar Ground-Up Firmware

Minimal ESP-IDF firmware for basic EchoEar audio interaction.

Included:

- EchoEar ES8311 speaker codec and ES7210 microphone codec.
- Device-side AEC and VAD through ESP-SR AFE.
- Opus microphone uplink to the WebSocket server.
- Opus TTS downlink playback from the server.
- Device sends `listen:start` when VAD speech begins and `listen:stop` when VAD silence returns. The server processes only after `listen:stop`.

Not included:

- Display, touch, SD card, battery, animations, OTA, MQTT, IoT tools, character profiles, or manager APIs.

Configure Wi-Fi and server URL with `idf.py menuconfig` under `EchoEar Ground-Up`, or set the generated `CONFIG_ECHOEAR_*` values in `sdkconfig.defaults`.

Build target:

```powershell
idf.py set-target esp32s3
idf.py build
```

