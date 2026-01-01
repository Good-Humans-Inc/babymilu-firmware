# GIF Animation System Implementation Summary

## Overview

This document summarizes the implementation of GIF-based animations for the EchoEar firmware. The system allows loading GIF files from a single `test.bin` file on the SD card, replacing the previous base+overlay frame system.

## File Structure

### test.bin Format

The `test.bin` file contains multiple GIF files in a structured format:

```
┌─────────────────────────────────────────────────────────────┐
│ Header (12 bytes)                                           │
├─────────────────────────────────────────────────────────────┤
│ Offset  │ Size │ Description                                │
│ 0x00    │ 4    │ Total number of files (uint32_t)          │
│ 0x04    │ 4    │ Checksum (uint32_t, sum of all bytes)      │
│ 0x08    │ 4    │ Combined data length (uint32_t)           │
├─────────────────────────────────────────────────────────────┤
│ File Table (N × 44 bytes per file)                          │
├─────────────────────────────────────────────────────────────┤
│ For each file:                                              │
│ 0x00    │ 32   │ File name (null-padded, max 32 bytes)    │
│ 0x20    │ 4    │ File size (uint32_t, little-endian)       │
│ 0x24    │ 4    │ File offset in data section (uint32_t)    │
│ 0x28    │ 2    │ Width (uint16_t, 0 for GIFs)              │
│ 0x2A    │ 2    │ Height (uint16_t, 0 for GIFs)             │
├─────────────────────────────────────────────────────────────┤
│ Data Section                                                │
├─────────────────────────────────────────────────────────────┤
│ For each file:                                              │
│ 0x00    │ 2    │ Magic bytes: 0x5A 0x5A                    │
│ 0x02    │ N    │ Actual GIF file data                       │
└─────────────────────────────────────────────────────────────┘
```

### Expected GIF Files

| Animation Type | GIF Filename | Description |
|---------------|--------------|-------------|
| ANIMATION_STATIC_NORMAL / ANIMATION_NORMAL | `normal.gif` | Default/idle state |
| ANIMATION_EMBARRESSED | `embarrass.gif` | Embarrassed emotion |
| ANIMATION_FIRE | `fire.gif` | Excited/fire emotion |
| ANIMATION_INSPIRATION | `inspiration.gif` | Inspired/thinking |
| ANIMATION_QUESTION | `question.gif` | Questioning/confused |
| ANIMATION_SHY | `shy.gif` | Shy emotion |
| ANIMATION_SLEEP | `sleep.gif` | Sleepy/tired |
| ANIMATION_HAPPY | `happy.gif` | Happy emotion |

## Tools

### pack_gifs_to_test_bin.py

**Location:** `gif transplant/pack_gifs_to_test_bin.py`

**Usage:**
```bash
python pack_gifs_to_test_bin.py <gif_folder> <output_test.bin>
```

**Example:**
```bash
python pack_gifs_to_test_bin.py animations/ test.bin
```

**Features:**
- Packs all GIF files from a folder into a single `test.bin` file
- Validates GIF format (GIF87a/GIF89a)
- Creates proper file table and data section
- Calculates checksum
- Provides detailed output and file information

**Expected Input:**
- Folder containing GIF files named: `normal.gif`, `happy.gif`, `embarrass.gif`, `fire.gif`, `inspiration.gif`, `question.gif`, `shy.gif`, `sleep.gif`

**Output:**
- Single `test.bin` file ready to copy to SD card

## Code Changes

### Animation System (`main/animation/`)

#### animation.h

**Added to `Animation_t` structure:**
```c
bool use_gif;                   // True if this animation uses GIF
char* gif_path;                 // Path to GIF file (for reference)
uint8_t* gif_data;              // GIF data in memory
size_t gif_data_size;           // Size of GIF data
```

**New Functions:**
- `bool animation_load_gifs_from_test_bin(void)` - Load all GIFs from test.bin
- `bool animation_extract_gif_from_test_bin(const char* gif_name, uint8_t** data, size_t* size)` - Extract a single GIF
- `bool animation_load_gif_animation(Animation_t* anim, const char* gif_name, uint8_t* gif_data, size_t gif_size)` - Load GIF into animation structure

#### animation.cc

**Key Changes:**
1. **Initialization:** GIF fields are initialized when loading animations
2. **Loading Priority:** 
   - First tries to load GIFs from `test.bin`
   - Falls back to frame-based animations if GIFs not found
3. **Animation Task:** Modified to handle GIF animations (no frame cycling needed)
4. **Cleanup:** Updated to free GIF data when cleaning up animations

**Loading Flow:**
```
animation_init()
  └─> animation_load_sd_card_animations()
      ├─> Try: animation_load_gifs_from_test_bin()
      │   └─> For each GIF: animation_extract_gif_from_test_bin()
      │       └─> animation_load_gif_animation()
      └─> Fallback: animation_load_all_from_sd_card() (frame-based)
```

### Display System (`main/display/`)

#### lcd_display.h

**Added:**
- `lv_obj_t* emotion_gif_` - GIF widget for animations
- `void SetEmotionGif(const uint8_t* gif_data, size_t gif_size)` - Set GIF animation

#### lcd_display.cc

**Added:**
- `#include <libs/gif/lv_gif.h>` - LVGL GIF support
- `SetEmotionGif()` implementation:
  - Creates/hides GIF widget as needed
  - Writes GIF data to temporary file (`/sdcard/temp_emotion.gif`)
  - Uses `lv_gif_set_src()` to display GIF
  - Hides image widget when showing GIF

**Display Logic:**
- GIF widget and image widget are mutually exclusive
- GIF widget handles its own animation (LVGL manages frame timing)
- Image widget is used for frame-based animations

## Usage

### 1. Prepare GIF Files

Create or obtain GIF files for each animation:
- `normal.gif`
- `happy.gif`
- `embarrass.gif`
- `fire.gif`
- `inspiration.gif`
- `question.gif`
- `shy.gif`
- `sleep.gif`

**Recommendations:**
- Optimize GIFs for file size (reduce colors, optimize frames)
- Use appropriate frame delays (typically 50-100ms per frame)
- Ensure dimensions match display (360x360 for EchoEar)
- Test GIFs before packing

### 2. Create test.bin

```bash
python gif\ transplant/pack_gifs_to_test_bin.py animations/ test.bin
```

This will:
- Validate all GIF files
- Pack them into `test.bin`
- Display file information and checksum

### 3. Deploy to Device

1. Copy `test.bin` to the root of the SD card
2. Insert SD card into device
3. Power on device
4. Firmware will automatically:
   - Detect `test.bin` on SD card
   - Extract GIF files
   - Load them into memory
   - Display animations using LVGL GIF widget

### 4. Verify Loading

Check logs for:
```
✅ Successfully loaded GIF animations from test.bin!
   - Using GIF format for animations
✅ Loaded GIF: normal.gif
✅ Loaded GIF: happy.gif
...
```

## Animation Task Behavior

### GIF Animations
- GIF widget handles frame cycling automatically
- No manual frame switching needed
- Task checks every 1000ms (GIFs animate themselves)
- Uses `SetEmotionGif()` to display

### Frame-Based Animations (Fallback)
- Manual frame cycling every 500ms
- Uses `SetEmotionImg()` to display individual frames
- Same behavior as before

## Memory Management

### GIF Data Storage
- GIF data is loaded into RAM when extracted from `test.bin`
- Each animation stores its GIF data in `Animation_t.gif_data`
- Memory is freed when animation is cleaned up
- Temporary file (`/sdcard/temp_emotion.gif`) is created for LVGL but can be reused

### Memory Considerations
- GIF files should be optimized for size
- Total memory usage = sum of all GIF file sizes
- Consider file size limits based on available RAM

## Backward Compatibility

The system maintains backward compatibility:
1. **First Priority:** Try to load GIFs from `test.bin`
2. **Fallback:** If GIFs not found, try frame-based animations
3. **Final Fallback:** Use static/default animations

This ensures existing `test.bin` files with frame-based animations still work.

## Troubleshooting

### GIFs Not Loading

**Check:**
1. SD card is mounted: `SdCard::IsMounted()`
2. `test.bin` exists in root: `/sdcard/test.bin`
3. GIF files are named correctly (see table above)
4. `test.bin` format is correct (use `read_assets_bin.py` to verify)

### GIF Not Displaying

**Check:**
1. GIF data was extracted successfully (check logs)
2. LVGL GIF support is enabled in Kconfig
3. Temporary file can be created: `/sdcard/temp_emotion.gif`
4. GIF format is valid (GIF87a or GIF89a)

### Memory Issues

**Solutions:**
1. Optimize GIF file sizes
2. Reduce number of frames per GIF
3. Use lower color depth
4. Consider using frame-based animations if memory is limited

## Performance

### Advantages
- **No Runtime Processing:** GIFs are pre-compressed, no overlay application needed
- **Automatic Timing:** Frame delays are handled by GIF format
- **Smooth Animation:** LVGL GIF widget provides smooth playback
- **Easy Updates:** Just replace GIF files and repack

### Considerations
- **File Size:** GIFs may be larger than optimized frame files
- **Memory:** All GIFs loaded into RAM
- **SD Card Access:** Temporary file created for each GIF display

## Future Improvements

1. **Memory-Mapped Access:** Load GIFs directly from SD card without copying to RAM
2. **Streaming:** Stream GIF frames instead of loading entire file
3. **Compression:** Use more efficient compression (WebP animation)
4. **Flash Storage:** Move to flash partition for faster access
5. **Caching:** Cache frequently used GIFs

## Files Modified

1. `main/animation/animation.h` - Added GIF support to Animation_t
2. `main/animation/animation.cc` - Added GIF loading functions
3. `main/display/lcd_display.h` - Added GIF widget and SetEmotionGif()
4. `main/display/lcd_display.cc` - Implemented GIF display
5. `gif transplant/pack_gifs_to_test_bin.py` - New tool for packing GIFs

## Testing Checklist

- [ ] Create GIF files for all 8 animations
- [ ] Pack GIFs into test.bin using script
- [ ] Copy test.bin to SD card
- [ ] Verify GIFs load on device startup
- [ ] Test each animation type
- [ ] Verify smooth playback
- [ ] Check memory usage
- [ ] Test fallback to frame-based animations
- [ ] Verify cleanup on animation change

## Summary

The GIF animation system provides a simpler, more maintainable approach to animations:
- **Standard Format:** Uses widely-supported GIF format
- **Easy Creation:** Create animations with any GIF editor
- **Automatic Playback:** LVGL handles frame timing
- **Backward Compatible:** Falls back to frame-based system if needed
- **Single File:** All animations in one `test.bin` file

The implementation is complete and ready for testing!

