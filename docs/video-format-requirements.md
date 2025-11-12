# Video Format Requirements

## Quick Answer

**Input Video Format**: **ANY format** - MP4, AVI, MOV, MKV, WebM, etc. (anything FFmpeg can read)

**Output Frame Format**: **JPG/JPEG** (recommended) or **BIN** (LVGL binary)

**Audio Format**: **WAV** (16kHz, 16-bit, mono) or **P3/Opus**

## Detailed Requirements

### Input Video (What You Feed In)

✅ **No specific requirement** - You can use any video format:
- MP4 (H.264, H.265)
- AVI
- MOV
- MKV
- WebM
- Any format that FFmpeg supports

**Why?** Because you'll convert it to frames first. The original format doesn't matter.

### Frame Output Format (After Conversion)

You have two options:

#### Option 1: JPG/JPEG (Recommended) ✅

- **Format**: Standard JPEG images
- **File extension**: `.jpg` or `.jpeg`
- **Why recommended**: 
  - ESP32-S3 has built-in JPEG decoder
  - Smaller file size
  - Easy to create with FFmpeg
  - No additional conversion needed

**Example:**
```bash
ffmpeg -i video.mp4 -vf fps=10 -q:v 3 frames/frame_%04d.jpg
```

#### Option 2: BIN (LVGL Binary) ⚡

- **Format**: Pre-converted LVGL binary format
- **File extension**: `.bin`
- **Why use it**:
  - Faster to load (no decoding needed)
  - Smaller file size
  - Better performance
- **Requires**: Extra conversion step

**Example:**
```bash
# First extract as JPG
ffmpeg -i video.mp4 -vf fps=10 frames/frame_%04d.jpg

# Then convert to BIN
python scripts/image_to_spiffs_converter.py frames/frame_0001.jpg frames/frame_0001.bin
```

### Audio Format

#### Option 1: WAV (Recommended) ✅

- **Sample rate**: 16000 Hz (16 kHz)
- **Channels**: Mono (1 channel)
- **Bit depth**: 16-bit
- **Format**: PCM

**Example:**
```bash
ffmpeg -i video.mp4 -ar 16000 -ac 1 -sample_fmt s16 audio.wav
```

#### Option 2: P3/Opus ⚡

- **Format**: Opus-encoded audio
- **File extension**: `.p3`
- **Why use it**: Smaller file size, same quality
- **Requires**: Conversion from WAV

**Example:**
```bash
# First extract as WAV
ffmpeg -i video.mp4 -ar 16000 -ac 1 -sample_fmt s16 temp.wav

# Then convert to P3
python scripts/p3_tools/convert_audio_to_p3.py temp.wav audio.p3
```

## Complete Example

### Input: Any Video Format
```bash
# Your video can be:
your_video.mp4
your_video.avi
your_video.mov
your_video.mkv
# etc.
```

### Conversion Process
```bash
# 1. Extract frames as JPG (works with any input format)
ffmpeg -i your_video.mp4 -vf fps=10 -q:v 3 frames/frame_%04d.jpg

# 2. Extract audio as WAV
ffmpeg -i your_video.mp4 -ar 16000 -ac 1 -sample_fmt s16 audio.wav
```

### Result on SD Card
```
/sdcard/
  ├── frames/
  │   ├── frame_0001.jpg  ← JPG format
  │   ├── frame_0002.jpg
  │   └── ...
  ├── audio.wav          ← WAV format
  └── playback.json
```

## Summary Table

| Stage | Format | Requirements |
|-------|--------|--------------|
| **Input Video** | Any | MP4, AVI, MOV, MKV, etc. (anything FFmpeg reads) |
| **Frames** | JPG or BIN | JPG: Standard JPEG<br>BIN: LVGL binary (pre-converted) |
| **Audio** | WAV or P3 | WAV: 16kHz, 16-bit, mono<br>P3: Opus-encoded |

## Common Questions

### Q: Can I use 4K video?
**A**: Yes, but you should scale it down to match your display resolution (e.g., 240x240, 320x240) during conversion:
```bash
ffmpeg -i 4k_video.mp4 -vf "fps=10,scale=240:240" -q:v 3 frames/frame_%04d.jpg
```

### Q: Can I use 60 FPS video?
**A**: Yes, but extract at lower FPS (8-15 FPS recommended):
```bash
ffmpeg -i 60fps_video.mp4 -vf fps=10 -q:v 3 frames/frame_%04d.jpg
```

### Q: What about video codec (H.264, H.265, etc.)?
**A**: Doesn't matter - FFmpeg handles all codecs. Just use whatever format you have.

### Q: Can I use animated GIF?
**A**: Yes! FFmpeg can extract frames from GIFs:
```bash
ffmpeg -i animation.gif -vf fps=10 -q:v 3 frames/frame_%04d.jpg
```

### Q: What about video without audio?
**A**: That's fine - just omit the audio file in your `playback.json`:
```json
{
  "type": "video",
  "video": { ... },
  "audio": null  // or omit this field
}
```

## Bottom Line

**You can use ANY video format as input** - the system doesn't care because you convert it first. The only requirements are:
1. **Frames**: JPG or BIN format
2. **Audio**: WAV (16kHz mono) or P3 format
3. **Naming**: Sequential (frame_0001.jpg, frame_0002.jpg, etc.)

