# Startup GIF Render Fix

## Summary

The round EchoEar LCD showed pale white bars or edge pixels while playing the early
`startup.gif`, even though later emotion GIFs rendered cleanly. The root cause was
a startup-only mismatch between the LCD/LVGL background state and the GIF render
path.

## Symptom

- On boot, `startup.gif` displayed with white/pale edge artifacts around the round
  visible area.
- Regular emotion GIFs did not show the same artifacts after the animation system
  was fully initialized.
- Repacking `test.bin` alone was not enough, because the early startup display path
  could still reveal white panel memory/background pixels.

## Root Cause

Two issues overlapped:

1. The LCD was cleared to white (`0xFFFF`) before LVGL started.
2. The startup GIF is displayed very early via `ShowStartupGifFromTestBin()` before
   the normal animation flow has fully settled.

The GIF widget and parent LVGL objects had transparent or light/gray backgrounds,
so any uncovered edge pixels could reveal the initial white clear. This reproduced
the older "surrounding white pixels" bug.

## Code Changes

### `main/display/lcd_display.cc`

- Changed the dark display background to true black (`0x000000`).
- Forced the LCD display theme to dark/black during display construction.
- Changed SPI and RGB LCD startup clears from white (`0xFFFF`) to black (`0x0000`).
- Made `screen`, `container_`, and `content_` backgrounds fully opaque with
  `LV_OPA_COVER`.
- Changed `content_` from chat background color to the main black background for
  full-screen animation surfaces.
- Updated `SetEmotionGif()` so every GIF render, including early `startup.gif`,
  forces the active screen/container/content/GIF object onto an opaque black
  background and removes border/outline/shadow styling.

### `crop_and_pack_gifs.py`

- Updated the startup resize path to flatten every startup frame onto a black
  360x360 canvas.
- Saved startup frames with full-frame disposal (`disposal=2`), no transparency,
  and no optimization, so the generated `startup_resized.gif` behaves more like
  the stable emotion GIFs.
- `startup_resized.gif` is still only an intermediate artifact. The firmware reads
  `startup.gif` from `/sdcard/startup.gif`, where the packer stores the generated
  resized startup asset under the logical name `startup.gif`.

## Regeneration Command

Run this after changing `startup_test/startup.gif` or the packer:

```powershell
Remove-Item .\startup_test\startup_resized.gif -Force
python .\crop_and_pack_gifs.py .\startup_test .\test.bin --no-crop
```

Then copy the regenerated `test.bin` and `startup.gif` to the SD card root.

## Firmware Command

Because the actual white-edge fix is in firmware display code, rebuild and flash:

```powershell
idf.py build
idf.py flash
```

## Validation Notes

- `startup_resized.gif` was verified locally to have 60 frames, each with a full
  `(0, 0, 360, 360)` GIF tile and black background index.
- `test.bin` was regenerated after the packer change.
- The final visual fix depends on flashing the firmware because the startup LCD
  clear and LVGL background state live in `lcd_display.cc`.
