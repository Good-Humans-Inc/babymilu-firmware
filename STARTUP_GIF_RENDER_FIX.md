# Startup GIF Render Notes

This document describes the current startup GIF path and the black-background
render fix.

## Current Behavior

Primary files:

- `main/boards/echoear/echoear.cc`
- `main/display/lcd_display.cc`
- `crop_and_pack_gifs.py`

EchoEar reads `/sdcard/startup.gif` directly through
`ShowStartupGifFromSdCard()`. It does not load startup GIF data from `test.bin`.
The heap buffer is kept alive because LVGL continues to reference the GIF data.

Startup audio is separate and can be played from `/sdcard/startup.wav`.

## Why Startup GIF Is Separate

`/sdcard/test.bin` contains the 20 emotion GIFs only. Keeping startup GIF outside
the bundle lets the device show it early before the slower animation bundle load
finishes.

The validator rejects `test.bin` files that contain `startup.gif`.

## Render Fix

The display path uses opaque black backgrounds for the LCD, LVGL screen,
containers, and GIF object. This avoids pale edge pixels around the round LCD
while startup GIF frames are playing.

`crop_and_pack_gifs.py` also flattens startup GIF frames onto a black 360x360
canvas and writes a `startup_resized.gif` intermediate before copying the final
`startup.gif` output.

## Regeneration

```powershell
python .\crop_and_pack_gifs.py .\gif_input .\test.bin
```

Copy both generated outputs to the SD card root:

- `test.bin`
- `startup.gif`

## Validation

Check logs for:

- `Playing startup.gif`
- `Trying to load GIF animations from test.bin`
- `Successfully loaded ... GIF animation(s) from test.bin`
