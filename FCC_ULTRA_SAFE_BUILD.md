# FCC Ultra-Safe Certification Firmware

This branch is based on the March firmware commit:

`b9a37a65aa7130e4a24374b10955cb9154820a4f`

It is a certification-only image for low-emissions FCC retesting. It is not a production user-experience build.

## Behavior

When `CONFIG_BABYMILU_FCC_ULTRA_SAFE_MODE=y`, the firmware boots the EchoEar board into a silent idle loop and skips startup paths that can create RF, audio, display, storage, or sensor activity.

Disabled or skipped:

- Wi-Fi station scan/connect and BLE Wi-Fi configuration startup
- OTA/version checks
- MQTT and WebSocket protocol startup
- Audio codec, audio loop, audio processor, wake word, and ready sounds
- LCD/display, backlight, SD card, SD animation load, and animation updater startup
- BMI270, CST816S touch, touch button, charge monitor, battery task, and power-save timers
- SD error-log upload and test writes

Kept:

- Normal bootloader/application boot
- EchoEar power-control GPIO setup
- Static idle loop with no periodic application work

## Build

Use the March ESP-IDF environment for this repository. Start from a clean generated `sdkconfig` so an old board choice is not reused, then build with the FCC defaults appended after the normal defaults:

```sh
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.fcc-ultra-safe" set-target esp32s3 reconfigure build
```

Flash as usual after a successful build:

```sh
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.fcc-ultra-safe" flash
```

## Expected Runtime

The device should not advertise BLE, join Wi-Fi, play audio, run wake word detection, load animations, access SD card animations, or start sensor/touch/charge background tasks. A serial log line should indicate `FCC ultra-safe mode active`, then the application remains idle.
