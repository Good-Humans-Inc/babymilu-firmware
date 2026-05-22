# LVGL Image Converter

Status: legacy/support tooling.

This folder contains LVGL image conversion helpers. These tools are not the
normal EchoEar emotion asset path.

For current EchoEar animation assets, use the GIF packer at the repository root:

```powershell
python .\crop_and_pack_gifs.py .\gif_input .\test.bin
```

Current runtime assets:

- `/sdcard/test.bin`: 20 packed GIFs.
- `/sdcard/startup.gif`: startup GIF.
- `/sdcard/startup.wav`: optional startup audio.
