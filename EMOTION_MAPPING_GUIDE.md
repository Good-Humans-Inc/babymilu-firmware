# EchoEar Emotion Mapping Guide

This document describes the current EchoEar emotion mapping used by
`LcdDisplay::SetEmotion()` and the GIF animation runtime.

## Current Runtime

Primary files:

- `main/display/lcd_display.cc`
- `main/animation/animation.h`
- `main/animation/animation.cc`
- `crop_and_pack_gifs.py`

Animations are loaded from `/sdcard/test.bin`, which contains the 20 packed GIF
assets. `startup.gif` is separate and is not part of `test.bin`.

## Current Animation Types

`AnimationType_e` currently contains:

- `ANIMATION_NORMAL`
- `ANIMATION_BLUSH`
- `ANIMATION_ANGRY`
- `ANIMATION_STARRY`
- `ANIMATION_SHY`
- `ANIMATION_SLEEP`
- `ANIMATION_HEARTY`
- `ANIMATION_LAUGH`
- `ANIMATION_SAD`
- `ANIMATION_SILENCE`
- `ANIMATION_LISTENING`
- `ANIMATION_SMIRK`
- `ANIMATION_WIFI`
- `ANIMATION_BATTERY`
- `ANIMATION_CRY`

Older names such as `FIRE`, `INSPIRATION`, `QUESTION`, `TALK`, `HAPPY`, and
`EMBARRESSED` are not current enum values.

## Accepted Emotion Strings

The current mapping is intentionally narrow and mostly exact:

| Input string | Animation |
| --- | --- |
| `normal` | normal |
| `smirk` | smirk |
| `happy` | smirk |
| `heart` | hearty |
| `blush` | blush |
| `embarressed` | blush |
| `sad` | sad |
| `laugh` | laugh |
| `sleep` | sleep |
| `sleepy` | sleep |
| `relaxed` | sleep |
| `starry` | starry |
| `cry` | cry |
| `angry` | angry |
| `listening` | listening |
| `silence` | silence |
| `wifi` | wifi |
| `battery` | battery |

Unknown strings fall back to `ANIMATION_NORMAL`.

## Overrides

- Volume 0 locks the display to `silence`.
- Low battery can override with `battery`.
- WiFi override is limited to specific normal-animation states such as audio
  testing or connecting; it is not a blanket disconnected-state override.

## Agent Notes

- Keep the misspelled `embarressed` documented while the code accepts that exact
  string.
- Do not document old frame-based `normal_all.bin` behavior as the active path.
- Use `MIGRATION_13_TO_20_GIFS_GUIDE.md` for asset packaging details.
