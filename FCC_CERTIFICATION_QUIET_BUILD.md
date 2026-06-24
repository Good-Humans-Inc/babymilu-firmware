# FCC Certification Quiet Build

This build profile is for FCC/RE certification debugging on the EchoEar/BabyMilu
firmware branch used for SAR testing. It keeps the Wi-Fi/BLE stack and existing
TX power caps intact, but removes startup activity that can add unrelated CPU,
LCD, SD-card, audio, and HTTP load during radiated-emissions tests.

## What It Disables

- `startup.wav` playback from SD card.
- Startup GIF and SD animation bundle loading.
- Startup animation update checks and downloads.
- Startup Firestore Wi-Fi ranking request.
- OTA/version check during boot.
- SD error-log upload, SD error-log hook, and test error log writes.
- Periodic chip temperature and battery telemetry logs intended for SD capture.
- Wi-Fi/BLE configuration prompt audio and most transient Wi-Fi overlay messages.
- Ready-success sound after protocol startup.

## What It Keeps

- Normal boot, display, touch, codec, Wi-Fi, BLE, and protocol initialization.
- Existing RF power limits already present in this SAR testing branch:
  Wi-Fi is still capped at 8 dBm and BLE at 3 dBm.
- Battery level reads used by status UI and power logic, without periodic error
  logging in quiet mode.

## Build

For a clean certification build, include the extra defaults file when generating
`sdkconfig`:

```bash
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.fcc-quiet" reconfigure build
```

If you already have an `sdkconfig`, confirm this line is enabled before build:

```text
CONFIG_BABYMILU_CERTIFICATION_QUIET_MODE=y
```

## Pretest Checklist

- Use the same external power condition as the lab setup, for example AC
  110V/60Hz through the certified adapter/fixture.
- Confirm the boot log contains `Certification quiet mode`.
- Do not use this image for normal product UX validation; it intentionally skips
  OTA, startup animation download, startup audio, and certification-irrelevant
  display prompts.
