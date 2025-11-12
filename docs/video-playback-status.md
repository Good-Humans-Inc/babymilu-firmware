# Video Playback Implementation Status

## Current Status

✅ **Framework Implemented**: The video playback system framework is in place and can:
- Detect video playback scripts in `playback.json`
- Parse video configuration (frame directory, count, FPS, audio file)
- Start video playback tasks
- Stream frames from SD card (doesn't load all at once)

⚠️ **Partial Implementation**: Frame loading and display is currently a placeholder. Full implementation requires:

1. **JPG Decoding**: ESP32-S3 has a built-in JPEG decoder that can be used
2. **LVGL Image Creation**: Convert decoded JPEG to LVGL image format
3. **Display Update**: Show frames on the display
4. **Audio Playback**: Synchronize audio with video frames

## What Works Now

- Script detection and parsing ✅
- Video configuration validation ✅
- Frame filename generation ✅
- Frame timing (FPS calculation) ✅
- SD card streaming structure ✅

## What Needs Implementation

### 1. Frame Loading and Display

The `VideoPlaybackTask` function needs to:
- Load JPG frame from SD card
- Decode JPEG using ESP32's JPEG decoder
- Convert to LVGL image format
- Display on screen using existing display system

### 2. Audio Playback

Audio synchronization requires:
- WAV file reading from SD card
- PCM audio playback through existing audio codec
- Synchronization with video frames

## Technical Approach

### For Frame Display

You can use ESP32's built-in JPEG decoder:

```cpp
#include "esp_jpeg_decoder.h"

// Decode JPEG frame
// Convert to RGB565 format
// Create LVGL image descriptor
// Display using existing display system
```

### For Audio

The system already has:
- Opus decoder (for P3 format)
- Audio codec interface
- Audio output system

You can extend it to:
- Read WAV files from SD card
- Decode PCM audio
- Play through existing audio pipeline

## File Size Handling

The system is designed to handle large files (few MBs) by:
- **Streaming**: Frames are loaded one at a time from SD card
- **No full load**: Never loads entire video into memory
- **On-demand**: Only current frame in memory

## Next Steps

To complete video playback:

1. **Implement JPG frame loading** in `VideoPlaybackTask`
2. **Add JPEG decoding** using ESP32's decoder
3. **Integrate with display system** to show frames
4. **Add audio playback** (WAV or P3 format)
5. **Synchronize audio and video**

## Example: What You Can Do Now

Even with partial implementation, you can:
- Test the script parsing
- Verify frame file access
- Check timing calculations
- Prepare your video files

The framework is ready - you just need to complete the frame display and audio playback parts.

## Recommendation

For immediate video shooting needs, you could:
1. Use the existing animation-based script system (works now)
2. Convert your video to a sequence of animations
3. Use the scripted playback with animations

For full video playback, complete the frame display implementation as described above.

