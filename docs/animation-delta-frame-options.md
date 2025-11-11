# Memory-Efficient Animation Options: Delta/Overlay Frames

## Goal
Reduce memory usage for normal animation by:
- Keeping `normal1` as full base frame (~128KB)
- Using delta/overlay for `normal2` and `normal3` instead of full frames
- **Target savings: 2 × 128KB = 256KB**

## Current System Analysis

### Current Flow:
```
plat_animation_task() 
  → get_animation(now_animation)
  → current_anim->imges[current_anim->animations[pos]]
  → display->SetEmotionImg(lv_image_dsc_t*)
  → lv_img_set_src(emotion_label_, img)
```

### Current Memory Usage:
- `normal1`: Full frame (128KB)
- `normal2`: Full frame (128KB)  
- `normal3`: Full frame (128KB)
- **Total: 384KB**

### Target Memory Usage:
- `normal1`: Full frame (128KB)
- `normal2`: Delta/overlay (~10-30KB typical)
- `normal3`: Delta/overlay (~10-30KB typical)
- **Total: ~148-188KB (savings: 196-236KB)**

---

## Option 1: Delta Frame with On-the-Fly Composition ⭐ **RECOMMENDED**

### Concept:
Store only pixels that differ from `normal1`, compose full frame in RAM when needed.

### Implementation:
```c
// New structure for delta frames
typedef struct {
    uint16_t x, y;           // Pixel position
    uint16_t color;          // RGB565 color value
} DeltaPixel_t;

typedef struct {
    uint32_t count;           // Number of changed pixels
    DeltaPixel_t* pixels;     // Array of changed pixels
} DeltaFrame_t;

// Modified Animation_t structure
typedef struct _Animation_t {
    const lv_image_dsc_t **imges;
    int *animations;
    int len;
    bool use_spiffs;
    lv_image_dsc_t **spiffs_imgs;
    
    // NEW: Delta frame support
    bool use_delta_frames;
    lv_image_dsc_t* base_frame;      // normal1 (full frame)
    DeltaFrame_t* delta_frames;       // normal2, normal3 (deltas)
    lv_image_dsc_t* composed_frames;  // Composed frames in RAM (cached)
} Animation_t;
```

### Memory Layout:
```
normal1 (base): 128KB (full frame, permanent)
normal2 (delta): ~15KB (only changed pixels)
normal3 (delta): ~15KB (only changed pixels)
Composed cache: 256KB (normal2 + normal3, allocated on-demand)
```

### Pros:
- ✅ Significant memory savings (196-236KB)
- ✅ No LVGL API changes needed
- ✅ Can cache composed frames for performance
- ✅ Flexible - can compose on-demand or pre-compose

### Cons:
- ⚠️ Requires composition step (CPU overhead)
- ⚠️ Need temporary buffer for composition
- ⚠️ More complex loading logic

### Implementation Steps:
1. Create delta frame format in binary file
2. Modify `animation_load_all_from_mega_file()` to detect delta frames
3. Add `animation_compose_delta_frame()` function
4. Modify `plat_animation_task()` to compose frames before display
5. Optionally cache composed frames

---

## Option 2: LVGL Canvas-Based Composition

### Concept:
Use LVGL's canvas API to draw base frame + overlay in real-time.

### Implementation:
```c
// Use LVGL canvas for composition
lv_obj_t* canvas = lv_canvas_create(parent);
lv_canvas_set_buffer(canvas, buffer, width, height, LV_COLOR_FORMAT_RGB565);

// Draw base frame
lv_draw_img_dsc_t img_dsc;
lv_draw_img_dsc_init(&img_dsc);
lv_canvas_draw_img(canvas, &normal1_img, &img_dsc);

// Draw delta overlay
lv_canvas_draw_polygon(canvas, delta_points, delta_count, &poly_dsc);
```

### Pros:
- ✅ Uses LVGL native APIs
- ✅ No manual pixel manipulation
- ✅ Can leverage LVGL optimizations

### Cons:
- ⚠️ Canvas requires buffer allocation (128KB per canvas)
- ⚠️ May be slower than direct composition
- ⚠️ More complex rendering pipeline

---

## Option 3: Sparse Pixel Map (Run-Length Encoded)

### Concept:
Store changed pixels in compressed format (RLE or sparse array).

### Implementation:
```c
typedef struct {
    uint16_t start_x, start_y;
    uint16_t width, height;
    uint16_t* pixel_data;  // Compressed pixel data
} SparseRegion_t;

typedef struct {
    uint32_t region_count;
    SparseRegion_t* regions;
} SparseFrame_t;
```

### Memory Layout:
```
normal1: 128KB (full)
normal2: ~8-12KB (sparse regions)
normal3: ~8-12KB (sparse regions)
```

### Pros:
- ✅ Maximum compression for small changes
- ✅ Good for animations with localized changes
- ✅ Can use region-based updates

### Cons:
- ⚠️ More complex encoding/decoding
- ⚠️ Requires region tracking
- ⚠️ May not save much if changes are widespread

---

## Option 4: Alpha Channel Overlay (RGB565A8)

### Concept:
Store `normal2` and `normal3` as alpha channel overlays that blend with `normal1`.

### Implementation:
```c
// normal2 and normal3 stored as RGB565A8 with:
// - RGB = changed pixels only
// - Alpha = 255 for changed, 0 for unchanged
// - Base frame (normal1) shows through alpha=0 areas

lv_image_dsc_t* overlay_img;  // RGB565A8 format
// LVGL automatically blends with base when alpha < 255
```

### Memory Layout:
```
normal1: 128KB (RGB565)
normal2: ~64KB (RGB565A8, but only changed pixels stored)
normal3: ~64KB (RGB565A8, but only changed pixels stored)
```

### Pros:
- ✅ LVGL handles blending automatically
- ✅ Natural alpha compositing
- ✅ Can use existing LVGL image formats

### Cons:
- ⚠️ Still requires ~64KB per overlay (if many pixels change)
- ⚠️ Need to ensure LVGL supports proper blending
- ⚠️ May need two-pass rendering (base + overlay)

---

## Option 5: Hybrid Approach - Pre-composed Cache

### Concept:
Compose delta frames once at startup, cache composed frames in PSRAM.

### Implementation:
```c
// At startup:
1. Load normal1 (base frame) - 128KB
2. Load normal2_delta - ~15KB
3. Load normal3_delta - ~15KB
4. Compose normal2 = normal1 + normal2_delta → 128KB
5. Compose normal3 = normal1 + normal3_delta → 128KB
6. Store composed frames in PSRAM

// At runtime:
// Just use pre-composed frames (no composition needed)
```

### Memory Layout:
```
Startup: 128KB + 15KB + 15KB = 158KB
After composition: 128KB + 128KB + 128KB = 384KB (same as current)
```

### Pros:
- ✅ No runtime composition overhead
- ✅ Simple implementation (compose once)
- ✅ Can free delta data after composition

### Cons:
- ⚠️ No memory savings (same as current)
- ⚠️ Only saves SPIFFS storage, not RAM

---

## Option 6: On-Demand Composition with LRU Cache

### Concept:
Compose frames on-demand, cache recently used frames with LRU eviction.

### Implementation:
```c
typedef struct {
    lv_image_dsc_t* composed_frame;
    int frame_index;
    uint32_t last_used;
    bool is_cached;
} CachedFrame_t;

// Cache up to 2 composed frames (256KB total)
// When cache full, evict least recently used
```

### Memory Layout:
```
Base: 128KB (permanent)
Delta2: 15KB (permanent)
Delta3: 15KB (permanent)
Cache: 128KB × 2 = 256KB (dynamic, LRU managed)
Total: ~414KB (but only 2 frames cached at once)
```

### Pros:
- ✅ Memory efficient (only cache what's needed)
- ✅ Good for animations that don't switch frequently
- ✅ Automatic cache management

### Cons:
- ⚠️ Composition overhead when cache miss
- ⚠️ More complex cache management
- ⚠️ May cause frame drops on cache miss

---

## Recommended Implementation: Option 1 (Delta Frame with On-the-Fly Composition)

### Why Option 1?
1. **Best memory savings**: 196-236KB saved
2. **Reasonable complexity**: Manageable implementation
3. **Flexible**: Can add caching later if needed
4. **No LVGL changes**: Works with existing API

### Implementation Plan:

#### Phase 1: Delta Frame Format
```python
# New binary format for delta frames
# Header: same as current (6 uint32_t)
# Data: List of (x, y, color) tuples for changed pixels
# Format: [count:uint32][x:uint16, y:uint16, color:uint16] × count
```

#### Phase 2: Loading Logic
```c
// Modify animation_load_all_from_mega_file()
// Detect delta frame format (new magic number or flag)
// Load base frame (normal1) as full frame
// Load delta frames (normal2, normal3) as DeltaFrame_t
```

#### Phase 3: Composition Function
```c
lv_image_dsc_t* animation_compose_delta_frame(
    const lv_image_dsc_t* base_frame,
    const DeltaFrame_t* delta_frame
) {
    // Allocate composed frame
    lv_image_dsc_t* composed = malloc(sizeof(lv_image_dsc_t));
    composed->data = malloc(base_frame->data_size);
    memcpy(composed->data, base_frame->data, base_frame->data_size);
    
    // Apply delta pixels
    for (uint32_t i = 0; i < delta_frame->count; i++) {
        DeltaPixel_t* pixel = &delta_frame->pixels[i];
        uint32_t offset = (pixel->y * base_frame->header.w + pixel->x) * 2;
        *((uint16_t*)(composed->data + offset)) = pixel->color;
    }
    
    return composed;
}
```

#### Phase 4: Display Integration
```c
// Modify plat_animation_task()
if (current_anim->use_delta_frames && pos > 0) {
    // Compose frame on-demand
    lv_image_dsc_t* composed = animation_compose_delta_frame(
        current_anim->base_frame,
        &current_anim->delta_frames[pos - 1]
    );
    display->SetEmotionImg(composed);
    // Optionally cache composed frame
} else {
    // Use full frame (normal1)
    display->SetEmotionImg(current_anim->base_frame);
}
```

---

## File Format Specification

### Delta Frame Binary Format:
```
[Header: 24 bytes - same as current]
  - magic: 0x4C56474C (LVGL)
  - color_format: 0x02 (RGB565)
  - flags: 0x80000000 (DELTA_FRAME flag)
  - width: 256
  - height: 256
  - stride: 512

[Delta Data]
  - pixel_count: uint32_t (number of changed pixels)
  - pixels: [x:uint16, y:uint16, color:uint16] × pixel_count
```

### Example:
```
normal1.bin: Full frame (128KB)
normal2.bin: Delta frame (~15KB)
normal3.bin: Delta frame (~15KB)
```

---

## Memory Comparison

| Approach | normal1 | normal2 | normal3 | Total | Savings |
|----------|---------|---------|---------|-------|---------|
| **Current** | 128KB | 128KB | 128KB | 384KB | - |
| **Option 1** | 128KB | 15KB | 15KB | 158KB | **226KB** |
| **Option 2** | 128KB | 128KB* | 128KB* | 384KB | 0KB* |
| **Option 3** | 128KB | 10KB | 10KB | 148KB | **236KB** |
| **Option 4** | 128KB | 64KB | 64KB | 256KB | **128KB** |
| **Option 5** | 128KB | 128KB | 128KB | 384KB | 0KB |
| **Option 6** | 128KB | 15KB | 15KB | 158KB+ | **226KB** |

*Option 2 uses canvas buffers which are temporary

---

## Next Steps for Implementation

1. **Choose option** (recommend Option 1)
2. **Create delta frame generator script** (Python)
3. **Modify animation loading code** to support delta format
4. **Add composition function** for on-the-fly composition
5. **Update display logic** to handle delta frames
6. **Test memory usage** and performance
7. **Add optional caching** if performance is an issue

---

## Code References

- **Current loading:** `main/animation/animation.cc:1330-1517`
- **Display function:** `main/display/lcd_display.cc:1064-1088`
- **Animation task:** `main/animation/animation.cc:237-263`
- **Animation structure:** `main/animation/animation.h:5-11`

