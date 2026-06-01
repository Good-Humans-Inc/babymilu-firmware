# EchoEar Firmware

This file is kept for old English README links. The current repository is
EchoEar-only.

Use [README.md](README.md) and [docs/AGENT_CONTEXT.md](docs/AGENT_CONTEXT.md)
as the active references.

## Current Scope

- Board: EchoEar only.
- Target: ESP32-S3.
- Active board source: `main/boards/echoear/echoear.cc`.
- Shared board support: `main/boards/common`.
- Removed content: old multi-board galleries, custom board guide, and 70+ board
  support claims.

## Build

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

## Runtime Assets

- `/sdcard/test.bin`: 21 packed GIFs.
- `/sdcard/startup.gif`: separate startup GIF.
- `/sdcard/startup.wav`: optional startup audio.
- `/sdcard/err.txt`: warning/error log uploaded on next startup.
