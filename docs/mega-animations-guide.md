# Legacy Mega Animation Tooling

Status: legacy/offline tooling reference.

The active EchoEar runtime uses `/sdcard/test.bin` containing 20 GIF assets, plus
separate `/sdcard/startup.gif` and `/sdcard/startup.wav`.

Older mega-frame files such as `animations_mega.bin` are not the normal current
production path. Do not document them as the first runtime load strategy unless
the firmware code is restored and verified.

## Current Asset Path

Use:

```powershell
python .\crop_and_pack_gifs.py .\gif_input .\test.bin
```

Then copy to SD card:

- `test.bin`
- `startup.gif`

## Legacy Scripts

The scripts in `scripts/create_mega_animations.py` and
`scripts/test_mega_animations.py` describe an older frame-based format. Before
using them, inspect and test the scripts; they are not part of the current
EchoEar GIF bundle flow.

For current behavior, see:

- `MIGRATION_13_TO_20_GIFS_GUIDE.md`
- `EMOTION_MAPPING_GUIDE.md`
