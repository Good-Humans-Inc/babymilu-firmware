# Animation Memory Management Explanation

## The Key Question
**How can 25-28 animation frames (potentially several MB) fit in only 392KB ROM and switch instantly?**

## The Answer: It's Not in ROM - It's in RAM/PSRAM!

### Critical Misconception
The **392KB ROM** you're seeing is just the **firmware code size** (compiled binary). The animations are **NOT stored in ROM**. They are:

1. **Stored in SPIFFS** (flash filesystem partition) - persistent storage
2. **Loaded into RAM/PSRAM** at runtime - volatile memory

### ESP32-S3 Memory Architecture

```
┌─────────────────────────────────────────────────────────┐
│ FLASH (16MB) - Non-volatile storage                     │
├─────────────────────────────────────────────────────────┤
│ • Firmware code (~392KB)                                │
│ • SPIFFS partition (animations_mega.bin)               │
│ • Other partitions (OTA, NVS, etc.)                    │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│ RAM - Volatile memory (runtime)                         │
├─────────────────────────────────────────────────────────┤
│ • Internal SRAM (~512KB)                                │
│ • External PSRAM (8MB) ← Animations go here!           │
└─────────────────────────────────────────────────────────┘
```

## Memory Allocation Flow

### 1. Startup: Load All Frames into RAM

**Function:** `animation_load_all_from_mega_file()` (line 1330-1517)

```c
// Step 1: Calculate total frames (28 frames)
int total_frames = 3+3+4+4+4+4+2+4 = 28;

// Step 2: Allocate global array for ALL frames
lv_image_dsc_t** all_spiffs_imgs = malloc(28 * sizeof(lv_image_dsc_t*));

// Step 3: Allocate one lv_image_dsc_t structure per frame
for (int i = 0; i < 28; i++) {
    all_spiffs_imgs[i] = malloc(sizeof(lv_image_dsc_t));
}

// Step 4: For each frame, allocate pixel data buffer
for (each frame) {
    img_dsc->data = malloc(data_size);  // Pixel data (largest part!)
    fread(img_dsc->data, ...);          // Load from SPIFFS
}

// Step 5: Point each animation to its frames
spiffs_normal.spiffs_imgs[0] = all_spiffs_imgs[0];  // normal1
spiffs_normal.spiffs_imgs[1] = all_spiffs_imgs[1];  // normal2
spiffs_normal.spiffs_imgs[2] = all_spiffs_imgs[2];  // normal3
// ... and so on for all 8 animations
```

**Key Point:** `all_spiffs_imgs[]` is **NEVER freed** after successful loading. All 28 frames stay in RAM permanently.

### 2. Runtime: Animation Switching (No Memory Allocation!)

**Function:** `animation_set_now_animation(int animation)` (line 265-280)

```c
void animation_set_now_animation(int animation) {
    now_animation = animation;  // Just change a variable!
    pos = 0;
    // NO memory allocation/deallocation!
}
```

**Function:** `plat_animation_task()` (line 237-263)

```c
void plat_animation_task(void *arg) {
    while (1) {
        // Get pointer to current animation (just a pointer lookup!)
        Animation_t* current_anim = get_animation(now_animation);
        
        // Display frame (just dereferencing pointers!)
        display->SetEmotionImg(current_anim->imges[current_anim->animations[pos]]);
        
        pos++;
        vTaskDelay(500ms);
    }
}
```

**Key Point:** Animation switching is **instant** because it's just:
- Changing a variable (`now_animation`)
- Pointer dereferencing (`get_animation()` → `Animation_t*`)
- No memory allocation, no file I/O, no copying

## Memory Layout in RAM

```
┌─────────────────────────────────────────────────────────────┐
│ Global: all_spiffs_imgs[] (28 pointers)                    │
│ Location: Heap (PSRAM - 8MB external memory)               │
│ Lifetime: Permanent (never freed)                           │
├─────────────────────────────────────────────────────────────┤
│ all_spiffs_imgs[0] → lv_image_dsc_t (normal1)              │
│   ├─ Structure: ~40 bytes                                  │
│   └─ data → malloc(131072) [128KB pixel buffer]             │
├─────────────────────────────────────────────────────────────┤
│ all_spiffs_imgs[1] → lv_image_dsc_t (normal2)              │
│   ├─ Structure: ~40 bytes                                   │
│   └─ data → malloc(131072) [128KB pixel buffer]             │
├─────────────────────────────────────────────────────────────┤
│ ... (26 more frames) ...                                    │
├─────────────────────────────────────────────────────────────┤
│ all_spiffs_imgs[27] → lv_image_dsc_t (sleep4)              │
│   ├─ Structure: ~40 bytes                                   │
│   └─ data → malloc(131072) [128KB pixel buffer]             │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ Animation Structures (8 total)                             │
│ Location: Static/Global memory                              │
├─────────────────────────────────────────────────────────────┤
│ spiffs_normal.spiffs_imgs[0] → all_spiffs_imgs[0]          │
│ spiffs_normal.spiffs_imgs[1] → all_spiffs_imgs[1]         │
│ spiffs_normal.spiffs_imgs[2] → all_spiffs_imgs[2]          │
├─────────────────────────────────────────────────────────────┤
│ spiffs_embarrass.spiffs_imgs[0] → all_spiffs_imgs[3]       │
│ ... (all animations point into all_spiffs_imgs[])          │
└─────────────────────────────────────────────────────────────┘
```

## Memory Size Calculation

### Per Frame Memory:
- **`lv_image_dsc_t` structure:** ~40 bytes
- **Pixel data buffer:** ~128KB (256×256×2 bytes for RGB565)
- **Total per frame:** ~128KB

### Total Memory for 28 Frames:
- **28 frames × 128KB = ~3.5MB**

### Where Does This Fit?

**ESP32-S3 with PSRAM:**
- Internal SRAM: ~512KB
- External PSRAM: **8MB** (fixed size on Sensecap Watcher)
- **Total available RAM: ~8.5MB**

**Configuration:** `sdkconfig.defaults.esp32s3`
```
CONFIG_SPIRAM=y                    # PSRAM enabled (SPIRAM is ESP-IDF config name)
CONFIG_SPIRAM_MODE_OCT=y           # Octal mode PSRAM (8MB)
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=49152
```

**Result:** 3.5MB of animations easily fits in 8MB PSRAM!

## Why Animation Switching is Instant

### Traditional Approach (Slow):
```
Switch Animation:
1. Free old animation frames from memory
2. Load new animation frames from SPIFFS
3. Allocate memory for new frames
4. Copy pixel data from SPIFFS to RAM
→ Takes 100-500ms per animation
```

### Current Approach (Fast):
```
Switch Animation:
1. Change now_animation variable (1 CPU cycle)
2. Pointer dereference (1 CPU cycle)
→ Takes <1ms, appears instant!
```

**Code Evidence:**
```c
// Line 260: Just pointer dereferencing!
display->SetEmotionImg(current_anim->imges[current_anim->animations[pos]]);
//     ↑                    ↑              ↑
//     |                    |              └─ Array index (0,1,2...)
//     |                    └─ Pointer to frame array
//     └─ Display function (just passes pointer)
```

## Memory Cleanup (When Does It Happen?)

### Cleanup Only Occurs:
1. **On reload:** When `animation_load_all_from_mega_file()` is called again
   - Line 1364: `animation_cleanup_spiffs_animation()` called for all animations
   - Old `all_spiffs_imgs[]` is freed
   - New `all_spiffs_imgs[]` is allocated

2. **On failure:** If loading fails, cleanup happens (line 1502-1513)

3. **Never during normal operation:** Once loaded, frames stay in memory until reboot or explicit reload

### Why No Cleanup During Switching?
- **All animations share the same global array** (`all_spiffs_imgs[]`)
- **No animation "owns" its frames** - they're all in one shared pool
- **Switching just changes which frames to display**, not which frames exist in memory

## Summary

### Key Points:

1. ✅ **Animations are in RAM/PSRAM, not ROM**
   - ROM (392KB) = firmware code only
   - RAM/PSRAM (8MB external) = runtime data including animations

2. ✅ **All 28 frames loaded at startup**
   - `all_spiffs_imgs[]` holds all frames permanently
   - ~3.5MB total memory usage

3. ✅ **Animation switching is instant**
   - Just changes a variable and dereferences pointers
   - No memory allocation, no file I/O, no copying

4. ✅ **Memory never freed during normal operation**
   - Frames stay in memory until reboot or reload
   - This is by design for instant switching

5. ✅ **PSRAM provides enough space**
   - ESP32-S3 has 8MB external PSRAM
   - 3.5MB animations fit comfortably

## Code References

- **Main loading:** `main/animation/animation.cc:1330-1517`
- **Animation switching:** `main/animation/animation.cc:265-280`
- **Display task:** `main/animation/animation.cc:237-263`
- **Memory cleanup:** `main/animation/animation.cc:1108-1132`
- **PSRAM config:** `sdkconfig.defaults.esp32s3:7-11` (CONFIG_SPIRAM is ESP-IDF's name for PSRAM)

