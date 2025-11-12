# Video Playback Guide for ESP32-S3

This guide explains how to play full video files (with audio) on your ESP32-S3 device using the scripted playback system.

## Overview

Since ESP32-S3 doesn't support standard video formats (MP4, AVI, MOV, etc.) directly, we use a **frame sequence + audio** approach:
- **Input video format**: Any format that FFmpeg can read (MP4, AVI, MOV, MKV, etc.)
- **Frame output format**: JPG/JPEG (recommended) or BIN (LVGL binary format)
- **Audio format**: WAV (16kHz, 16-bit, mono) or P3/Opus format
- Frames are loaded from SD card on-demand (streaming) to handle large files
- Audio and video are synchronized during playback

## Video Input Format Requirements

**You can use ANY video format** as input - the system doesn't care about the original format because you'll convert it first. Common formats that work:
- ✅ MP4 (H.264, H.265)
- ✅ AVI
- ✅ MOV
- ✅ MKV
- ✅ WebM
- ✅ Any format FFmpeg supports

**The conversion process handles everything** - you just need to extract frames and audio from your video file.

## Video File Preparation

### Step 1: Convert Video to Frame Sequence

Use FFmpeg to extract frames from your video. **FFmpeg supports many input formats**, so you can use MP4, AVI, MOV, MKV, etc.:

```bash
# Extract frames at 10 FPS from any video format
ffmpeg -i your_video.mp4 -vf fps=10 -q:v 2 frames/frame_%04d.jpg
ffmpeg -i your_video.avi -vf fps=10 -q:v 2 frames/frame_%04d.jpg
ffmpeg -i your_video.mov -vf fps=10 -q:v 2 frames/frame_%04d.jpg
ffmpeg -i your_video.mkv -vf fps=10 -q:v 2 frames/frame_%04d.jpg

# Or at 15 FPS for smoother playback
ffmpeg -i your_video.mp4 -vf fps=15 -q:v 2 frames/frame_%04d.jpg

# Or at 30 FPS (may be too fast for ESP32-S3)
ffmpeg -i your_video.mp4 -vf fps=30 -q:v 2 frames/frame_%04d.jpg
```

**Frame Output Format Options:**

1. **JPG/JPEG (Recommended)**: 
   - Most compatible
   - Smaller file size
   - ESP32-S3 has built-in JPEG decoder
   - Use: `frame_%04d.jpg`

2. **BIN (LVGL Binary Format)**:
   - Faster to load (pre-converted)
   - Smaller file size
   - Requires pre-conversion step
   - Use: `frame_%04d.bin` (after conversion)

**Recommended settings:**
- **FPS**: 10-15 FPS (balance between smoothness and performance)
- **Resolution**: Match your display resolution (e.g., 240x240, 320x240)
- **Quality**: `-q:v 2` (high quality, larger files) to `-q:v 5` (lower quality, smaller files)

### Step 2: Extract Audio

Extract audio from your video (works with any video format that has audio):

```bash
# Extract audio as 16kHz mono WAV (recommended)
# Works with MP4, AVI, MOV, MKV, etc.
ffmpeg -i your_video.mp4 -ar 16000 -ac 1 -sample_fmt s16 audio.wav
ffmpeg -i your_video.avi -ar 16000 -ac 1 -sample_fmt s16 audio.wav
ffmpeg -i your_video.mov -ar 16000 -ac 1 -sample_fmt s16 audio.wav

# Or convert to P3/Opus format (smaller file size)
# First extract as WAV, then use the p3_tools scripts
ffmpeg -i your_video.mp4 -ar 16000 -ac 1 -sample_fmt s16 temp.wav
python scripts/p3_tools/convert_audio_to_p3.py temp.wav audio.p3
```

**Audio Format Requirements:**
- **WAV**: 16kHz sample rate, 16-bit, mono (PCM)
- **P3**: Opus-encoded format (smaller, same quality)

### Step 3: Organize Files on SD Card

Copy files to your SD card:

```
/sdcard/
  ├── video_frames/          # Directory with frame images
  │   ├── frame_0001.jpg
  │   ├── frame_0002.jpg
  │   ├── frame_0003.jpg
  │   └── ...
  ├── audio.wav              # Audio file (or audio.p3)
  └── playback.json          # Script file
```

## Script Format for Video Playback

Create a `playback.json` file with video playback configuration:

```json
{
  "type": "video",
  "video": {
    "frame_directory": "video_frames",
    "frame_prefix": "frame_",
    "frame_format": "jpg",
    "frame_count": 150,
    "fps": 10
  },
  "audio": {
    "file": "audio.wav",
    "sync": true
  }
}
```

### Video Configuration

- **frame_directory**: Directory containing frame images (relative to SD card root)
- **frame_prefix**: Prefix of frame filenames (e.g., "frame_" for "frame_0001.jpg")
- **frame_format**: Image format ("jpg" or "bin" for LVGL format)
- **frame_count**: Total number of frames
- **fps**: Frames per second (playback speed)

### Audio Configuration

- **file**: Audio filename (WAV or P3 format)
- **sync**: If true, audio and video are synchronized (audio duration determines playback length)

## Example: Complete Video Setup

### 1. Prepare Video

```bash
# Convert video to frames (10 FPS, 240x240 resolution)
ffmpeg -i demo.mp4 -vf "fps=10,scale=240:240" -q:v 3 frames/frame_%04d.jpg

# Extract audio
ffmpeg -i demo.mp4 -ar 16000 -ac 1 -sample_fmt s16 audio.wav
```

### 2. Count Frames

```bash
# Count frames
ls frames/ | wc -l
# Result: 150 frames
```

### 3. Create Script

`playback.json`:
```json
{
  "type": "video",
  "video": {
    "frame_directory": "frames",
    "frame_prefix": "frame_",
    "frame_format": "jpg",
    "frame_count": 150,
    "fps": 10
  },
  "audio": {
    "file": "audio.wav",
    "sync": true
  }
}
```

### 4. Copy to SD Card

```
/sdcard/
  ├── frames/
  │   ├── frame_0001.jpg
  │   ├── frame_0002.jpg
  │   └── ... (150 frames)
  ├── audio.wav
  └── playback.json
```

## File Size Considerations

### Frame Size Optimization

For a few MB video:
- **Resolution**: Match display (e.g., 240x240 = ~57KB per JPG frame)
- **Quality**: Use `-q:v 3` to `-q:v 5` for smaller files
- **FPS**: Lower FPS = fewer frames = smaller total size

**Example calculation:**
- 240x240 JPG at quality 3: ~30-50KB per frame
- 150 frames at 10 FPS (15 seconds): ~4.5-7.5MB
- Audio (WAV, 16kHz mono): ~480KB per second

### Memory Management

The system streams frames from SD card (doesn't load all at once):
- Only 1-2 frames in memory at a time
- Audio is streamed in chunks
- Can handle videos of several MBs

## Performance Tips

1. **Lower FPS**: Use 8-12 FPS for smoother playback on slower SD cards
2. **Optimize JPG quality**: Balance quality vs file size
3. **Use P3 audio**: Smaller than WAV, same quality
4. **Match resolution**: Don't use higher resolution than display
5. **Test SD card speed**: Faster SD cards = smoother playback

## Troubleshooting

### Video Stutters or Drops Frames

- **Lower FPS**: Try 8 FPS instead of 10-15
- **Reduce JPG quality**: Use `-q:v 4` or `-q:v 5`
- **Check SD card speed**: Use Class 10 or faster SD card
- **Reduce resolution**: Match exactly to display size

### Audio Out of Sync

- **Check audio sample rate**: Must be 16000 Hz for WAV
- **Verify frame count**: Ensure frame_count matches actual frames
- **Check FPS setting**: FPS in script must match extraction FPS

### Out of Memory Errors

- **Reduce frame buffer**: System uses minimal memory
- **Check JPG size**: Individual frames should be < 100KB
- **Close other tasks**: Ensure no other heavy tasks running

### Video Not Playing

- **Check file paths**: Ensure frame_directory and audio file paths are correct
- **Verify frame naming**: Must be sequential (frame_0001.jpg, frame_0002.jpg, etc.)
- **Check SD card**: Ensure SD card is mounted and accessible
- **Review logs**: Check serial monitor for error messages

## Advanced: Custom Frame Format

For better performance, you can convert frames to LVGL binary format:

```bash
# Convert JPG frames to LVGL binary format (smaller, faster to load)
python scripts/image_to_spiffs_converter.py frames/frame_0001.jpg frame_0001.bin
```

Then use in script:
```json
{
  "frame_format": "bin"
}
```

## Limitations

- **Maximum video length**: Limited by SD card space and available memory
- **Frame rate**: Recommended 8-15 FPS (30 FPS may be too fast)
- **Resolution**: Should match display resolution for best performance
- **Audio format**: WAV (16kHz, 16-bit, mono) or P3/Opus format

## Example Scripts

### Short Demo Video (5 seconds, 10 FPS)

```json
{
  "type": "video",
  "video": {
    "frame_directory": "demo_frames",
    "frame_prefix": "frame_",
    "frame_format": "jpg",
    "frame_count": 50,
    "fps": 10
  },
  "audio": {
    "file": "demo_audio.wav",
    "sync": true
  }
}
```

### Long Presentation (30 seconds, 12 FPS)

```json
{
  "type": "video",
  "video": {
    "frame_directory": "presentation",
    "frame_prefix": "slide_",
    "frame_format": "jpg",
    "frame_count": 360,
    "fps": 12
  },
  "audio": {
    "file": "presentation_audio.p3",
    "sync": true
  }
}
```

