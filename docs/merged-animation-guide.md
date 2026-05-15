# Legacy Merged Animation Guide

Status: legacy frame-based tooling reference.

The current EchoEar production animation path is GIF-based:

- `/sdcard/test.bin`: 20 packed GIFs.
- `/sdcard/startup.gif`: separate startup GIF.

Merged frame files such as `normal_all.bin` are not the active production asset
contract. Keep this distinction clear when debugging animation loading.

## Current Replacement

Use `crop_and_pack_gifs.py` and follow `MIGRATION_13_TO_20_GIFS_GUIDE.md`.

## Legacy Use

The old merged-frame scripts may still be useful for offline experiments with
LVGL binary frames, but docs and agents should not assume the firmware loads
`normal_all.bin` during normal EchoEar operation.
