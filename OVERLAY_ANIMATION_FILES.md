# Overlay Animation System - Complete File List

This document lists all files related to the overlay animation system, from image/pixel conversion all the way to display rendering.

## Overview

The overlay animation system uses sparse pixel overlays to optimize animation storage. Instead of storing full frames, it stores only the pixels that differ between frames, overlaying them on a base frame at runtime.

---

## 1. Image/Pixel Conversion Scripts

### Frame Difference Generation
- **`images/frame difference/generate_diff_overlay.py`**
  - Generates overlay pixel headers from frame differences
  - Compares two images and extracts differing pixels
  - Converts RGB to RGB565 format
  - Output: C header files with overlay pixel data

### Overlay Generation from Single Images
- **`scripts/generate_overlay_from_image.py`**
  - Extracts non-white pixels from a single image
  - Converts to RGB565 format
  - Generates C header files

- **`scripts/generate_overlay_header.py`**
  - Parses text files with pixel coordinates
  - Converts to C header format

### Image to Binary Conversion
- **`image_to_spiffs_converter.py`**
  - Converts images to SPIFFS binary format
  - Handles LVGL image format conversion
  - Extracts animation data from C files

- **`scripts/image_to_merged_spiffs.py`**
  - Converts multiple images to merged SPIFFS binary
  - Creates merged animation files

- **`scripts/Image_Converter/LVGLImage.py`**
  - Core LVGL image conversion utilities
  - Handles various color formats (RGB565, etc.)
  - PNG to LVGL binary conversion

### Mega Animation Creation
- **`scripts/create_mega_animations.py`**
  - Creates mega animation files combining all animations
  - **Handles overlay pixel integration**:
    - Loads overlay pixels from header files
    - Generates overlay frames using `_generate_overlay_frames_from_pixels()`
    - Embeds overlay pixels in binary format (OPXL format: 0x4F50584C)
  - Supports normal, embarrass, fire, happy, inspiration animations
  - Uses overlay pixels to build additional frames from base frames

---

## 2. Overlay Pixel Header Files

### Static Overlay Headers (Generated)
- **`main/display/overlay_pixels.h`**
  - Static overlay pixels for normal animation frames 2-3
  - Fallback overlay data

### Frame Difference Overlay Headers
Located in `images/frame difference/`:

- **Embarrass Animation:**
  - `embarrass_overlay2.h` - Overlay for frame 2
  - `embarrass_overlay3.h` - Overlay for frame 3

- **Fire Animation:**
  - `fire_overlay2.h` - Overlay for frame 2
  - `fire_overlay3.h` - Overlay for frame 3
  - `fire_overlay4.h` - Overlay for frame 4

- **Happy Animation:**
  - `happy_overlay2.h` - Overlay for frame 2
  - `happy_overlay3.h` - Overlay for frame 3
  - `happy_overlay4.h` - Overlay for frame 4

- **Inspiration Animation:**
  - `inspiration_overlay2.h` - Overlay for frame 2
  - `inspiration_overlay3.h` - Overlay for frame 3
  - `inspiration_overlay4.h` - Overlay for frame 4

### Source Images for Frame Differences
Located in `images/frame difference/`:
- `embarrass1.png`, `embarrass2.png`, `embarrass3.png`
- `fire1.png`, `fire2.png`, `fire3.png`, `fire4.png`
- `happy1 (Custom).png`, `happy2 (Custom).png`, `happy3 (Custom).png`, `happy4 (Custom).png`
- `inspiration1 (Custom).png`, `inspiration2 (Custom).png`, `inspiration3 (Custom).png`, `inspiration4 (Custom).png`

---

## 3. Animation Core Files

### Animation Data Structures & API
- **`main/animation/animation.h`**
  - Defines `animation_overlay_pixel_t` structure (x, y, color)
  - Defines `animation_overlay_frame_t` structure (pixels array + count)
  - Overlay frame getter functions:
    - `animation_get_normal_overlay_frame()`
    - `animation_get_embarrass_overlay_frame()`
    - `animation_get_fire_overlay_frame()`
    - `animation_get_happy_overlay_frame()`
    - `animation_get_inspiration_overlay_frame()`

### Animation Implementation
- **`main/animation/animation.cc`**
  - **Overlay pixel storage** (lines 32-50):
    - Runtime arrays for each animation type
    - Frame count constants
  - **Overlay management functions**:
    - `animation_clear_normal_overlay_frames()` (line 54)
    - `animation_set_normal_overlay_frame()` (line 67)
    - `animation_get_normal_overlay_frame()` (line 86)
    - Similar functions for embarrass, fire, happy, inspiration
  - **Overlay loading from binary files**:
    - `animation_create_spiffs_animation_from_merged()` (line ~1650)
      - Parses overlay frames with color format `LV_IMAGE_CF_OVERLAY_PIXELS` (0x4F50584C)
      - Reads overlay pixel entries (x, y, color) from binary
      - Stores overlay pixels in runtime arrays
      - Links overlay frames to base frames
    - `animation_create_sd_card_animation_from_merged()` (line ~2450)
      - Same overlay parsing logic for SD card files

### Animation Updater
- **`main/animation/animation_updater.cc`**
  - Downloads animation files (including overlay data)
  - Handles OTA updates for animations
  - Reloads animations after updates

- **`main/animation/animation_updater.h`**
  - Animation updater class definition

---

## 4. Display Rendering Files

### Display Core
- **`main/display/lcd_display.h`**
  - Display class interface
  - `SetEmotionImg()` function declaration

- **`main/display/lcd_display.cc`**
  - **Overlay composition function** (line 1072):
    - `compose_image_with_overlay()` - Creates composed images with overlay pixels
    - Copies base image data
    - Applies sparse overlay pixels on top
    - Returns composed `lv_image_dsc_t`
  - **Overlay application** (line 1203):
    - `SetEmotionImg()` - Main display function
    - Calls `compose_image_with_overlay()` for frames 1-3
    - Uses runtime overlay pixels or falls back to static overlays
  - **Overlay pixel includes**:
    - Includes `overlay_pixels.h` for static fallback overlays
    - Uses runtime overlay frames from animation system

### Display Base
- **`main/display/display.h`**
  - Base display interface
  - Virtual functions for display operations

---

## 5. Documentation

- **`docs/animation-delta-frame-options.md`**
  - Documentation about delta frame options

- **`docs/animation-memory-management-explanation.md`**
  - Memory management for animations

- **`docs/merged-animation-guide.md`**
  - Guide for merged animation files

- **`docs/mega-animations-guide.md`**
  - Guide for mega animation system (includes overlay info)

- **`docs/merged-files-optimization.md`**
  - Optimization details for merged files

---

## 6. Data Flow Summary

```
1. SOURCE IMAGES (PNG/JPG)
   └─> images/frame difference/*.png

2. FRAME DIFFERENCE GENERATION
   └─> images/frame difference/generate_diff_overlay.py
       └─> Generates overlay header files (*.h)

3. OVERLAY HEADER FILES
   └─> images/frame difference/*_overlay*.h
   └─> main/display/overlay_pixels.h (static fallback)

4. MEGA ANIMATION CREATION
   └─> scripts/create_mega_animations.py
       ├─> Loads overlay headers
       ├─> Converts images to LVGL binary
       └─> Embeds overlay pixels as OPXL format (0x4F50584C)
       └─> Output: animations_mega.bin

5. ANIMATION LOADING (Runtime)
   └─> main/animation/animation.cc
       ├─> animation_create_spiffs_animation_from_merged()
       ├─> Parses OPXL format frames
       ├─> Extracts overlay pixels (x, y, color)
       └─> Stores in runtime arrays

6. DISPLAY RENDERING
   └─> main/display/lcd_display.cc
       ├─> SetEmotionImg() called with base frame
       ├─> compose_image_with_overlay() called
       ├─> Retrieves overlay pixels from runtime arrays
       ├─> Composes base frame + overlay pixels
       └─> Displays composed image via LVGL
```

---

## 7. Key Constants & Formats

- **Overlay Pixel Format**: `LV_IMAGE_CF_OVERLAY_PIXELS = 0x4F50584C` ("OPXL")
- **Overlay Entry Size**: 6 bytes (uint16_t x, uint16_t y, uint16_t color)
- **Color Format**: RGB565 (16-bit)
- **Supported Animations**: Normal, Embarrass, Fire, Happy, Inspiration
- **Frame Support**:
  - Normal: frames 2-3 (indices 1-2)
  - Embarrass: frames 2-3 (indices 1-2)
  - Fire: frames 2-4 (indices 1-3)
  - Happy: frames 2-4 (indices 1-3)
  - Inspiration: frames 2-4 (indices 1-3)

---

## 8. Related Utility Scripts

- **`scripts/convert_animation_to_spiffs.py`**
  - Converts C animation files to SPIFFS format

- **`scripts/test_merged_animation.py`**
  - Tests merged animation file format

- **`scripts/test_merged_from_images.py`**
  - Tests merged animation creation from images

---

## Summary

The overlay animation system consists of:
- **12 Python scripts** for conversion and generation
- **13 overlay header files** (11 frame difference + 1 static + 1 base)
- **2 core C++ files** (animation.cc, lcd_display.cc)
- **2 header files** (animation.h, lcd_display.h)
- **Multiple source images** for frame differences

The system optimizes storage by storing only pixel differences between frames, composing them at runtime for display.


