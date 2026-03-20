# Migration Guide: 13 GIFs to 20 GIFs Animation System

## Overview

This document provides a complete guide for migrating from the older **13 GIFs animation system** to the current **20 GIFs animation system** with proper emotion mapping. This guide covers the `crop_and_pack_gifs.py` script, the loading/display system, and the complete emotion-to-animation mapping.

---

## Table of Contents

1. [System Comparison](#system-comparison)
2. [GIF Processing Script: `crop_and_pack_gifs.py`](#gif-processing-script-crop_and_pack_gifspy)
3. [Animation Loading System](#animation-loading-system)
4. [Emotion Mapping System](#emotion-mapping-system)
5. [Complete Migration Steps](#complete-migration-steps)
6. [File Structure Reference](#file-structure-reference)

---

## System Comparison

### Old System (13 GIFs)

**Main Animations (10):**
- `normal.gif`
- `embarrass.gif`
- `fire.gif`
- `inspiration.gif`
- `shy.gif`
- `sleep.gif`
- `happy.gif`
- `laugh.gif`
- `sad.gif`
- `talk.gif`

**System GIFs (3):**
- `wifi.gif`
- `battery.gif`
- `silence.gif`

**Total: 13 GIFs**

### New System (20 GIFs)

**Main Emotional Animations (11):**
- `normal.gif` - Neutral baseline
- `smirk.gif` + `smirk_start.gif` - Happy/positive expressions
- `heart.gif` + `heart_start.gif` - Affection/love
- `blush.gif` - Embarrassed/confident
- `sad.gif` + `sad_start.gif` - Sad emotions
- `laugh.gif` + `laugh_start.gif` - Laughing
- `sleep.gif` - Sleepy/relaxed
- `starry.gif` + `starry_start.gif` - Surprised/thinking
- `cry.gif` - Crying
- `angry.gif` + `angry_start.gif` - Angry
- `listening.gif` - Listening state

**System GIFs (3):**
- `silence.gif` - Volume is 0
- `battery.gif` - Low battery indicator
- `wifi.gif` - WiFi disconnected

**Total: 20 GIFs (11 loop GIFs + 5 start GIFs + 3 system GIFs + 1 normal)**

---

## GIF Processing Script: `crop_and_pack_gifs.py`

### Purpose

The `crop_and_pack_gifs.py` script performs two main functions:
1. **Crops and resizes** all GIFs to a standard format
2. **Packs** all GIFs into a single `test.bin` file for SD card loading

### Usage

```bash
python crop_and_pack_gifs.py <gif_folder> <output_test.bin> [--no-crop]
```

**Arguments:**
- `gif_folder`: Path to folder containing GIF files
- `output_test.bin`: Path where test.bin will be written
- `--no-crop`: Skip the crop/resize step (only pack existing GIFs)

**Examples:**
```bash
# Full process: crop + pack
python crop_and_pack_gifs.py gif_folder/ test.bin

# Pack only (skip cropping)
python crop_and_pack_gifs.py gif_folder/ test.bin --no-crop
```

### Configuration

**Crop Settings:**
```python
CROP_BOX = (244, 219, 780, 755)  # (left, top, right, bottom)
TARGET_SIZE = (360, 360)         # Final size after resize
```

**Expected GIF Files (20 total, all required):**
```python
EXPECTED_GIFS = [
    "smirk.gif",
    "smirk_start.gif",
    "heart.gif",
    "heart_start.gif",
    "blush.gif",
    "battery.gif",
    "wifi.gif",
    "silence.gif",
    "sad.gif",
    "sad_start.gif",
    "laugh.gif",
    "laugh_start.gif",
    "sleep.gif",
    "starry.gif",
    "starry_start.gif",
    "cry.gif",
    "normal.gif",
    "angry.gif",
    "angry_start.gif",
    "listening.gif",
]
```

### Script Workflow

1. **Step 1: Crop and Resize (if not using --no-crop)**
   - Opens each GIF file
   - Extracts all frames
   - Crops each frame to `CROP_BOX` region
   - Resizes to `TARGET_SIZE` (360x360)
   - Preserves frame durations and loop settings
   - Saves back to the same file (overwrites)

2. **Step 2: Pack into test.bin**
   - Verifies all 20 expected GIFs exist
   - Creates binary file structure:
     - **Header (12 bytes):**
       - 4 bytes: File count (20)
       - 4 bytes: Checksum (sum of all bytes, masked to 32 bits)
       - 4 bytes: Combined length (table + data)
     - **File Table:**
       - For each GIF: 44 bytes entry
         - 32 bytes: Filename (padded with nulls)
         - 4 bytes: File size (little-endian)
         - 4 bytes: Offset in data section (little-endian)
         - 2 bytes: Width (0 for GIFs, not used)
         - 2 bytes: Height (0 for GIFs, not used)
     - **Data Section:**
       - For each GIF:
         - 2 bytes: Magic bytes `0x5A5A`
         - N bytes: Actual GIF file data

### Output

The script creates `test.bin` which must be copied to the root of the SD card. The firmware automatically loads GIFs from this file during initialization.

---

## Animation Loading System

### Loading Flow

```
animation_init()
  └─> animation_load_sd_card_animations()
      ├─> Try: animation_load_gifs_from_test_bin()
      │   └─> For each GIF: animation_extract_gif_from_test_bin()
      │       └─> animation_load_gif_animation_with_start_loop()
      └─> Fallback: animation_load_all_from_sd_card() (frame-based)
```

### Key Function: `animation_load_gifs_from_test_bin()`

**Location:** `main/animation/animation.cc` (lines 2177-2297)

**Purpose:** Loads all 20 GIFs from `test.bin` and maps them to internal `Animation_t` structures.

**GIF-to-Animation Mapping:**

```cpp
const GifAnimDef gif_anims[] = {
    // Main emotional animations
    {"normal",   "normal.gif",      NULL,               &sd_normal},
    {"smirk",    "smirk.gif",       "smirk_start.gif",  &sd_smirk},
    {"heart",    "heart.gif",       "heart_start.gif",  &sd_happy},       // reuse sd_happy for heart
    {"blush",    "blush.gif",       NULL,               &sd_embarrass},   // reuse sd_embarrass for blush
    {"sad",      "sad.gif",         "sad_start.gif",    &sd_sad},
    {"laugh",    "laugh.gif",       "laugh_start.gif",  &sd_laugh},
    {"sleep",    "sleep.gif",       NULL,               &sd_sleep},
    {"starry",   "starry.gif",      "starry_start.gif", &sd_inspiration}, // reuse sd_inspiration for starry
    {"cry",      "cry.gif",         NULL,               &sd_talk},        // reuse sd_talk for cry
    {"angry",    "angry.gif",       "angry_start.gif",  &sd_fire},        // reuse sd_fire for angry
    {"silence",  "silence.gif",     NULL,               &sd_silence},

    // Extra / status animations
    {"listening","listening.gif",   NULL,               &sd_listening},
    {"battery",  "battery.gif",     NULL,               &sd_battery},
    {"wifi",     "wifi.gif",        NULL,               &sd_wifi},
};
```

**Important Notes:**
- Some GIFs reuse existing `Animation_t` structures (e.g., `heart.gif` → `sd_happy`, `blush.gif` → `sd_embarrass`)
- Start GIFs are optional (can be `NULL`)
- The function extracts GIFs from `test.bin` using `animation_extract_gif_from_test_bin()`
- GIFs are loaded into memory and stored in `Animation_t` structures

### Animation Structure

**Location:** `main/animation/animation.h` (lines 5-22)

```cpp
typedef struct _Animation_t {
    // Frame-based animation (legacy)
    const lv_image_dsc_t **imges;
    int *animations;
    int len;
    bool use_spiffs;
    lv_image_dsc_t **spiffs_imgs;
    
    // GIF support
    bool use_gif;                   // True if this animation uses GIF
    char* gif_path;                 // Path to GIF file (for reference)
    uint8_t* gif_data;              // GIF data in memory (main/loop GIF)
    size_t gif_data_size;           // Size of GIF data
    
    // Start+Loop GIF support
    bool has_start_gif;             // True if this animation has a separate start GIF
    uint8_t* gif_start_data;       // Start GIF data in memory
    size_t gif_start_data_size;    // Size of start GIF data
    uint8_t* gif_loop_data;         // Loop GIF data in memory
    size_t gif_loop_data_size;      // Size of loop GIF data
} Animation_t;
```

### Animation Type Enum

**Location:** `main/animation/animation.h` (lines 25-41)

```cpp
typedef enum _AnimationType_e {
    ANIMATION_NORMAL = 0,         // Maps to sd_normal
    ANIMATION_EMBARRESSED,        // Maps to sd_embarrass
    ANIMATION_FIRE,               // Maps to sd_fire
    ANIMATION_INSPIRATION,        // Maps to sd_inspiration
    ANIMATION_NORMAL,             // Maps to sd_normal
    ANIMATION_SHY,                // Maps to sd_shy
    ANIMATION_SLEEP,              // Maps to sd_sleep
    ANIMATION_HAPPY,              // Maps to sd_happy
    ANIMATION_LAUGH,              // Maps to sd_laugh
    ANIMATION_SAD,                // Maps to sd_sad
    ANIMATION_TALK,               // Maps to sd_talk
    ANIMATION_SILENCE,            // Maps to sd_silence
    ANIMATION_LISTENING,          // Maps to sd_listening
    ANIMATION_SMIRK,              // Maps to sd_smirk
    ANIMATION_NUM
} AnimationType_e;
```

### Animation Type → Animation_t Mapping

**Location:** `main/animation/animation.cc` (lines 105-173)

The `get_animation(int index)` function maps `AnimationType_e` enum values to `Animation_t*` structures:

```cpp
Animation_t* get_animation(int index) {
    switch(index) {
        case 0:  // ANIMATION_NORMAL
            return animation_get_normal_animation();
        case 1:  // ANIMATION_EMBARRESSED
            return animation_get_embarrass_animation();
        case 2:  // ANIMATION_FIRE
            return animation_get_fire_animation();
        case 3:  // ANIMATION_INSPIRATION
            return animation_get_inspiration_animation();
        case 4:  // ANIMATION_NORMAL
            return animation_get_normal_animation();
        case 5:  // ANIMATION_SHY
            return animation_get_shy_animation();
        case 6:  // ANIMATION_SLEEP
            return animation_get_sleep_animation();
        case 7:  // ANIMATION_HAPPY
            return animation_get_happy_animation();
        case 8:  // ANIMATION_LAUGH
            return animation_get_laugh_animation();
        case 9:  // ANIMATION_SAD
            return animation_get_sad_animation();
        case 10: // ANIMATION_TALK
            return animation_get_talk_animation();
        case 11: // ANIMATION_SILENCE
            return animation_get_silence_animation();
        case 12: // ANIMATION_LISTENING
            return animation_get_listening_animation();
        case 13: // ANIMATION_SMIRK
            return animation_get_smirk_animation();
        default:
            return animation_get_normal_animation();
    }
}
```

Each `animation_get_*_animation()` function returns the corresponding `sd_*` structure (e.g., `&sd_normal`, `&sd_smirk`, etc.).

---

## Emotion Mapping System

### Overview

The emotion mapping system connects **emotion strings** (from server/user) to **AnimationType_e** enum values, which are then resolved to **Animation_t** structures for display.

### Emotion Mapping Location

**File:** `main/display/lcd_display.cc`  
**Function:** `LcdDisplay::SetEmotion(const char *emotion)`  
**Lines:** 1124-1221

### Complete Emotion Mapping Table

```cpp
static const std::vector<Emotion> emotions = {
    // Neutral baseline (normal.gif)
    {ANIMATION_NORMAL, "neutral"},

    // Positive / happy styles (smirk.gif + smirk_start.gif)
    {ANIMATION_SMIRK, "happy"},
    {ANIMATION_SMIRK, "laughing"},
    {ANIMATION_SMIRK, "funny"},
    {ANIMATION_SMIRK, "cool"},

    // Heart / affection (heart.gif + heart_start.gif)
    {ANIMATION_HAPPY, "loving"},
    {ANIMATION_HAPPY, "kissy"},

    // Blush / embarrassed (blush.gif mapped via ANIMATION_EMBARRESSED)
    {ANIMATION_EMBARRESSED, "embarrassed"},
    {ANIMATION_EMBARRESSED, "confident"},

    // Starry / surprised (starry.gif + starry_start.gif mapped via ANIMATION_INSPIRATION)
    {ANIMATION_INSPIRATION, "surprised"},
    {ANIMATION_INSPIRATION, "shocked"},
    {ANIMATION_INSPIRATION, "thinking"},

    // Angry (angry.gif + angry_start.gif mapped via ANIMATION_FIRE)
    {ANIMATION_FIRE, "angry"},

    // Sad / crying (sad.gif + sad_start.gif, cry.gif)
    {ANIMATION_SAD, "sad"},
    {ANIMATION_TALK, "crying"},

    // Sleepy / relaxed (sleep.gif)
    {ANIMATION_SLEEP, "sleepy"},
    {ANIMATION_SLEEP, "relaxed"},

    // Silly / shy-ish (blush / mixed)
    {ANIMATION_SHY, "silly"},

    // Listening state (listening.gif)
    {ANIMATION_LISTENING, "listening"},
};
```

### Emotion → Animation Flow

```
Server sends emotion: "happy" (via JSON/WebSocket)
         ↓
Application calls: display->SetEmotion("happy")
         ↓
LcdDisplay::SetEmotion("happy") in lcd_display.cc:1124
         ↓
Search emotions vector by text (right side): "happy"
         ↓
Found: {ANIMATION_SMIRK, "happy"}
         ↓
Extract animation_num (left side): ANIMATION_SMIRK
         ↓
Call: animation_set_now_animation(ANIMATION_SMIRK)
         ↓
Animation system uses get_animation(ANIMATION_SMIRK)
         ↓
Returns: animation_get_smirk_animation()
         ↓
Returns: &sd_smirk (which contains smirk.gif + smirk_start.gif)
         ↓
Display system shows animation with start+loop GIFs
```

### Complete Emotion-to-GIF Mapping Reference

| Emotion String | AnimationType_e | Animation_t | GIF Files | Notes |
|---------------|-----------------|-------------|-----------|-------|
| `"neutral"` | `ANIMATION_NORMAL` | `sd_normal` | `normal.gif` | Baseline state |
| `"happy"` | `ANIMATION_SMIRK` | `sd_smirk` | `smirk.gif` + `smirk_start.gif` | Positive/happy |
| `"laughing"` | `ANIMATION_SMIRK` | `sd_smirk` | `smirk.gif` + `smirk_start.gif` | Same as happy |
| `"funny"` | `ANIMATION_SMIRK` | `sd_smirk` | `smirk.gif` + `smirk_start.gif` | Same as happy |
| `"cool"` | `ANIMATION_SMIRK` | `sd_smirk` | `smirk.gif` + `smirk_start.gif` | Same as happy |
| `"loving"` | `ANIMATION_HAPPY` | `sd_happy` | `heart.gif` + `heart_start.gif` | Affection |
| `"kissy"` | `ANIMATION_HAPPY` | `sd_happy` | `heart.gif` + `heart_start.gif` | Affection |
| `"embarrassed"` | `ANIMATION_EMBARRESSED` | `sd_embarrass` | `blush.gif` | Embarrassed |
| `"confident"` | `ANIMATION_EMBARRESSED` | `sd_embarrass` | `blush.gif` | Same as embarrassed |
| `"surprised"` | `ANIMATION_INSPIRATION` | `sd_inspiration` | `starry.gif` + `starry_start.gif` | Surprised |
| `"shocked"` | `ANIMATION_INSPIRATION` | `sd_inspiration` | `starry.gif` + `starry_start.gif` | Same as surprised |
| `"thinking"` | `ANIMATION_INSPIRATION` | `sd_inspiration` | `starry.gif` + `starry_start.gif` | Same as surprised |
| `"angry"` | `ANIMATION_FIRE` | `sd_fire` | `angry.gif` + `angry_start.gif` | Angry |
| `"sad"` | `ANIMATION_SAD` | `sd_sad` | `sad.gif` + `sad_start.gif` | Sad |
| `"crying"` | `ANIMATION_TALK` | `sd_talk` | `cry.gif` | Crying |
| `"sleepy"` | `ANIMATION_SLEEP` | `sd_sleep` | `sleep.gif` | Sleepy |
| `"relaxed"` | `ANIMATION_SLEEP` | `sd_sleep` | `sleep.gif` | Same as sleepy |
| `"silly"` | `ANIMATION_SHY` | `sd_shy` | (fallback to frame-based) | Silly |
| `"listening"` | `ANIMATION_LISTENING` | `sd_listening` | `listening.gif` | Listening state |
| *(unknown)* | `ANIMATION_TALK` | `sd_talk` | `cry.gif` | Default fallback |

### Special Cases

1. **Volume Lock:** If volume is 0, the system automatically locks to `ANIMATION_SILENCE` (displays `silence.gif`) regardless of emotion input.

2. **WiFi/Battery Override:** When displaying `ANIMATION_NORMAL`, the system checks:
   - If WiFi is disconnected → shows `wifi.gif` instead
   - If battery < 20% → shows `battery.gif` instead

3. **Default Fallback:** If an emotion string is not found in the mapping, it defaults to `ANIMATION_TALK` (which displays `cry.gif` in the 20 GIF system).

---

## Complete Migration Steps

### Step 1: Update GIF Files

1. **Obtain all 20 GIF files:**
   - Ensure you have all files listed in `EXPECTED_GIFS` (see [GIF Processing Script](#gif-processing-script-crop_and_pack_gifspy))
   - Files should be in a single folder (e.g., `gif_folder/`)

2. **Run the crop and pack script:**
   ```bash
   python crop_and_pack_gifs.py gif_folder/ test.bin
   ```
   This will:
   - Crop all GIFs to (244, 219, 780, 755) and resize to 360x360
   - Pack all 20 GIFs into `test.bin`

3. **Copy `test.bin` to SD card:**
   - Place `test.bin` in the root directory of the SD card
   - The firmware will automatically load GIFs from this file

### Step 2: Update Code Files

#### 2.1 Update `main/animation/animation.h`

**Add new animation types (if not present):**
```cpp
typedef enum _AnimationType_e {
    ANIMATION_NORMAL = 0,
    ANIMATION_EMBARRESSED,
    ANIMATION_FIRE,
    ANIMATION_INSPIRATION,
    ANIMATION_NORMAL,
    ANIMATION_SHY,
    ANIMATION_SLEEP,
    ANIMATION_HAPPY,
    ANIMATION_LAUGH,
    ANIMATION_SAD,
    ANIMATION_TALK,
    ANIMATION_SILENCE,
    ANIMATION_LISTENING,  // NEW
    ANIMATION_SMIRK,       // NEW
    ANIMATION_NUM
} AnimationType_e;
```

**Add function declarations:**
```cpp
Animation_t* animation_get_listening_animation(void);
Animation_t* animation_get_smirk_animation(void);
Animation_t* animation_get_battery_animation(void);
Animation_t* animation_get_wifi_animation(void);
```

#### 2.2 Update `main/animation/animation.cc`

**Add Animation_t structures:**
```cpp
static Animation_t sd_normal;
static Animation_t sd_embarrass;
static Animation_t sd_fire;
static Animation_t sd_happy;
static Animation_t sd_inspiration;
static Animation_t sd_shy;
static Animation_t sd_sleep;
static Animation_t sd_laugh;
static Animation_t sd_sad;
static Animation_t sd_talk;
static Animation_t sd_silence;
static Animation_t sd_listening;  // NEW
static Animation_t sd_smirk;       // NEW
static Animation_t sd_battery;      // NEW
static Animation_t sd_wifi;         // NEW
```

**Update `get_animation()` function:**
Add cases for `ANIMATION_LISTENING` and `ANIMATION_SMIRK`:
```cpp
case 12: // ANIMATION_LISTENING
    return animation_get_listening_animation();
case 13: // ANIMATION_SMIRK
    return animation_get_smirk_animation();
```

**Update `animation_load_sd_card_animations()`:**
Initialize new animation structures:
```cpp
INIT_ANIM(sd_listening);
INIT_ANIM(sd_smirk);
INIT_ANIM(sd_wifi);
INIT_ANIM(sd_battery);
```

**Implement `animation_load_gifs_from_test_bin()`:**
Copy the complete function from the current codebase (lines 2177-2297) with the 20 GIF mapping table.

**Implement getter functions:**
```cpp
Animation_t* animation_get_listening_animation(void) {
    if (sd_listening.use_gif && sd_listening.gif_data && sd_listening.gif_data_size > 0) {
        return &sd_listening;
    }
    if (sd_listening.use_spiffs && sd_listening.imges && sd_listening.len > 0) {
        return &sd_listening;
    }
    ESP_LOGW("animation", "No listening animation available from SD card");
    return NULL;
}

Animation_t* animation_get_smirk_animation(void) {
    if (sd_smirk.use_gif && sd_smirk.gif_data && sd_smirk.gif_data_size > 0) {
        return &sd_smirk;
    }
    if (sd_smirk.use_spiffs && sd_smirk.imges && sd_smirk.len > 0) {
        return &sd_smirk;
    }
    ESP_LOGW("animation", "No smirk animation available from SD card");
    return NULL;
}

// Similar for battery and wifi...
```

#### 2.3 Update `main/display/lcd_display.cc`

**Update `SetEmotion()` function:**
Replace the emotion mapping vector with the 20 GIF system mapping (see [Complete Emotion Mapping Table](#complete-emotion-mapping-table)).

### Step 3: Update Animation Display Logic

**Ensure start+loop GIF support:**
- The animation task should handle `has_start_gif` flag
- Play start GIF once, then loop the loop GIF
- See `main/animation/animation.cc` lines 208-350 for reference implementation

### Step 4: Testing

1. **Verify GIF loading:**
   - Check logs for "Successfully loaded GIF animations from test.bin"
   - Verify all 20 GIFs are loaded

2. **Test emotion mapping:**
   - Test each emotion string from the mapping table
   - Verify correct animations are displayed

3. **Test special cases:**
   - Volume = 0 → should show `silence.gif`
   - WiFi disconnected → should show `wifi.gif` when in normal state
   - Battery < 20% → should show `battery.gif` when in normal state

---

## File Structure Reference

### Required Files

```
project_root/
├── crop_and_pack_gifs.py          # GIF processing script
├── gif_folder/                     # Source GIF files (20 total)
│   ├── normal.gif
│   ├── smirk.gif
│   ├── smirk_start.gif
│   ├── heart.gif
│   ├── heart_start.gif
│   ├── blush.gif
│   ├── sad.gif
│   ├── sad_start.gif
│   ├── laugh.gif
│   ├── laugh_start.gif
│   ├── sleep.gif
│   ├── starry.gif
│   ├── starry_start.gif
│   ├── cry.gif
│   ├── angry.gif
│   ├── angry_start.gif
│   ├── listening.gif
│   ├── silence.gif
│   ├── battery.gif
│   └── wifi.gif
├── test.bin                        # Packed output (copy to SD card)
└── main/
    ├── animation/
    │   ├── animation.h             # Animation types and structures
    │   └── animation.cc            # Loading and display logic
    └── display/
        └── lcd_display.cc          # Emotion mapping
```

### SD Card Structure

```
/sdcard/
└── test.bin                        # Must be in root directory
```

---

## Key Differences from 13 GIF System

1. **New GIFs Added:**
   - `smirk.gif` + `smirk_start.gif` (replaces generic happy)
   - `heart.gif` + `heart_start.gif` (new affection animation)
   - `blush.gif` (new embarrassed animation)
   - `starry.gif` + `starry_start.gif` (replaces generic inspiration)
   - `angry.gif` + `angry_start.gif` (replaces generic fire)
   - `listening.gif` (new listening state)

2. **Start+Loop GIF Support:**
   - Some animations now have separate start and loop GIFs
   - Start GIF plays once, then loops the loop GIF
   - Provides smoother animation transitions

3. **Enhanced Emotion Mapping:**
   - More granular emotion-to-animation mapping
   - Better visual representation of emotions
   - Reuses some Animation_t structures for related emotions

4. **Improved Animation Structure:**
   - `Animation_t` now supports `has_start_gif`, `gif_start_data`, `gif_loop_data`
   - Better separation between start and loop phases

---

## Troubleshooting

### GIFs Not Loading

1. **Check SD card:**
   - Ensure `test.bin` is in the root directory
   - Verify SD card is mounted

2. **Check file format:**
   - Verify `test.bin` was created with `crop_and_pack_gifs.py`
   - Check logs for extraction errors

3. **Check animation initialization:**
   - Verify `animation_init()` is called
   - Check that `animation_load_gifs_from_test_bin()` returns true

### Wrong Animation Displayed

1. **Check emotion mapping:**
   - Verify emotion string matches exactly (case-sensitive)
   - Check that mapping exists in `SetEmotion()` function

2. **Check animation type mapping:**
   - Verify `get_animation()` returns correct `Animation_t*`
   - Check that GIF files are loaded into correct structures

### Start GIF Not Playing

1. **Check `has_start_gif` flag:**
   - Verify `has_start_gif` is set to `true` when loading
   - Check that `gif_start_data` is not NULL

2. **Check animation task:**
   - Verify start GIF logic in `plat_animation_task()`
   - Check that start phase is properly handled

---

## Summary

This migration guide provides everything needed to upgrade from the 13 GIF system to the 20 GIF system:

1. **`crop_and_pack_gifs.py`** processes and packs all 20 GIFs into `test.bin`
2. **Loading system** extracts GIFs from `test.bin` and maps them to `Animation_t` structures
3. **Emotion mapping** connects emotion strings to animation types to GIF files
4. **Display system** handles start+loop GIFs and special cases (volume, WiFi, battery)

Follow the migration steps in order, and refer to the troubleshooting section if issues arise.

