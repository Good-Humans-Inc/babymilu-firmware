# Legacy Merged Binary Test Usage

Status: legacy frame-tooling reference.

`scripts/test_merged_from_images.py` validates old merged LVGL frame binaries
such as `normal_all.bin`. EchoEar's current production animation path uses a
20-GIF `/sdcard/test.bin` bundle instead.

For current GIF assets:

```powershell
python .\crop_and_pack_gifs.py .\gif_input .\test.bin
```

Then copy `test.bin` and `startup.gif` to the SD card root.

Use this guide only when intentionally testing legacy merged-frame files.
