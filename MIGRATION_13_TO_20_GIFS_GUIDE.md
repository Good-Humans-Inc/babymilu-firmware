# Current 20-GIF Asset Guide

This is the active EchoEar animation asset guide. The firmware expects a 20-GIF
bundle at `/sdcard/test.bin` and a separate startup GIF at `/sdcard/startup.gif`.

## Current Files

- Packer: `crop_and_pack_gifs.py`
- Runtime loader: `main/animation/animation.cc`
- Display renderer: `main/display/lcd_display.cc`
- Startup display: `main/boards/echoear/echoear.cc`

## SD Card Layout

```text
/sdcard/
  test.bin       # packed 20 emotion GIFs
  startup.gif    # separate startup GIF, not inside test.bin
  startup.wav    # optional startup audio
```

## Required GIFs In `test.bin`

`crop_and_pack_gifs.py` packs exactly these 20 files:

```text
smirk.gif
smirk_start.gif
heart.gif
heart_start.gif
blush.gif
battery.gif
wifi.gif
silence.gif
sad.gif
sad_start.gif
laugh.gif
laugh_start.gif
sleep.gif
starry.gif
starry_start.gif
cry.gif
normal.gif
angry.gif
angry_start.gif
listening.gif
```

`startup.gif` is processed separately and copied beside `test.bin`.

## Creating Assets

```powershell
python .\crop_and_pack_gifs.py .\gif_input .\test.bin
```

To skip crop/resize and only pack existing GIFs:

```powershell
python .\crop_and_pack_gifs.py .\gif_input .\test.bin --no-crop
```

The script crops/resizes emotion GIFs to 360x360. `startup.gif` is resize-only
and is written as a separate SD-card root asset.

## `test.bin` Contract

The firmware validates the bundle before loading:

- 12-byte header.
- Exactly 20 file table entries.
- Each table entry is 44 bytes.
- GIF data entries are prefixed with `0x5A5A`.
- File is large enough for the expected GIF bundle.
- `startup.gif` must not appear in the bundle.

If validation fails, the firmware skips GIF loading and lets the update path
retry later.

## Emotion Mapping

See `EMOTION_MAPPING_GUIDE.md` for the current string-to-animation table.

Unknown emotions fall back to normal.

## Legacy Note

Older docs referenced frame bundles such as `normal_all.bin`,
`animations_mega.bin`, and older animation enum names. Those are not the current
production path for EchoEar.
