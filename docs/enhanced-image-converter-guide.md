# Legacy Image Converter Guide

Status: legacy LVGL frame/merged-bin tooling reference.

The current EchoEar animation path is GIF-based and uses:

```powershell
python .\crop_and_pack_gifs.py .\gif_input .\test.bin
```

The older `scripts/image_to_merged_spiffs.py` flow creates LVGL frame binaries
such as `normal_all.bin`. Those files are not the current production animation
contract for EchoEar.

Use this guide only when intentionally working with legacy frame experiments.
For normal EchoEar assets, see `MIGRATION_13_TO_20_GIFS_GUIDE.md`.
