# Animation Flow Documentation

## Overview

The animation system uses **word-based emotion mapping** (not emoji characters). The server returns emotion words like "happy", "angry", "neutral", etc., and the device maps these words to specific animation types.

## Flow Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│ 1. SERVER RESPONSE (WebSocket/HTTP)                             │
│                                                                  │
│ JSON Format:                                                     │
│ {                                                               │
│   "type": "llm",                                                │
│   "emotion": "happy"  ← WORD (not emoji)                        │
│ }                                                               │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│ 2. APPLICATION LAYER (main/application.cc)                     │
│                                                                  │
│ Location: Line 639-645                                          │
│                                                                  │
│ Parses JSON response:                                           │
│ - Extracts "emotion" field (string value)                       │
│ - Schedules SetEmotion() call on display thread                  │
│                                                                  │
│ Code:                                                           │
│   if (strcmp(type->valuestring, "llm") == 0) {                 │
│     auto emotion = cJSON_GetObjectItem(root, "emotion");       │
│     if (cJSON_IsString(emotion)) {                             │
│       display->SetEmotion(emotion->valuestring);               │
│     }                                                           │
│   }                                                             │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│ 3. DISPLAY LAYER (main/display/lcd_display.cc)                   │
│                                                                  │
│ Function: LcdDisplay::SetEmotion(const char *emotion)          │
│ Location: Line 629-685                                          │
│                                                                  │
│ Emotion Word → Animation Type Mapping:                          │
│                                                                  │
│   "neutral"      → ANIMATION_STATIC_NORMAL                      │
│   "happy"        → ANIMATION_HAPPY                             │
│   "laughing"     → ANIMATION_HAPPY                              │
│   "funny"        → ANIMATION_NORMAL                              │
│   "sad"          → ANIMATION_SHY                                 │
│   "angry"        → ANIMATION_FIRE                               │
│   "crying"       → ANIMATION_SHY                                 │
│   "loving"       → ANIMATION_INSPIRATION                         │
│   "embarrassed"  → ANIMATION_EMBARRESSED                         │
│   "surprised"    → ANIMATION_HAPPY                              │
│   "shocked"      → ANIMATION_INSPIRATION                         │
│   "thinking"     → ANIMATION_QUESTION                            │
│   "winking"      → ANIMATION_NORMAL                              │
│   "cool"         → ANIMATION_INSPIRATION                         │
│   "relaxed"      → ANIMATION_SLEEP                               │
│   "delicious"    → ANIMATION_HAPPY                              │
│   "kissy"        → ANIMATION_INSPIRATION                         │
│   "confident"    → ANIMATION_HAPPY                              │
│   "sleepy"       → ANIMATION_SLEEP                              │
│   "silly"        → ANIMATION_HAPPY                              │
│   "confused"     → ANIMATION_QUESTION                           │
│                                                                  │
│ Default (if no match): ANIMATION_NORMAL                         │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│ 4. ANIMATION SYSTEM (main/animation/animation.cc)                │
│                                                                  │
│ Function: animation_set_now_animation(int animation)           │
│ Location: Line 541-558                                          │
│                                                                  │
│ Sets the current animation type and starts animation task:      │
│ - Validates animation index (0-8)                                │
│ - Sets now_animation global variable                            │
│ - Resets animation position to 0                                │
│ - Creates animation task if not already running                  │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│ 5. ANIMATION TASK (main/animation/animation.cc)                  │
│                                                                  │
│ Function: plat_animation_task(void *arg)                        │
│ Location: Line 478-539                                          │
│                                                                  │
│ Animation Loop:                                                  │
│ - Gets current animation using get_animation(now_animation)      │
│ - Cycles through animation frames                                │
│ - Displays each frame via display->SetEmotionImg()              │
│ - Applies frame-specific delays (334ms default)                  │
│ - Handles overlay composition for certain frames                │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│ 6. LCD DISPLAY (main/display/lcd_display.cc)                      │
│                                                                  │
│ Function: LcdDisplay::SetEmotionImg()                           │
│ Location: Line 1203-1333                                        │
│                                                                  │
│ Displays Animation Frame:                                       │
│ - Creates/updates emotion_label_ (LVGL image object)           │
│ - Applies overlay composition for frames 1-3 (if applicable)    │
│ - Sets image source and displays on screen                       │
│ - Handles frame scaling and positioning                          │
└─────────────────────────────────────────────────────────────────┘
```

## Key Points

### 1. **Server Response Format**
- **Type**: JSON object
- **Format**: `{"type": "llm", "emotion": "happy"}`
- **Emotion Field**: Contains a **word string** (not emoji)
- **Example Values**: "happy", "angry", "neutral", "sad", "thinking", etc.

### 2. **Emotion Word Mapping**
The device maintains a lookup table that maps emotion words to animation types:

| Emotion Word | Animation Type | Animation Index |
|-------------|----------------|----------------|
| "neutral" | ANIMATION_STATIC_NORMAL | 0 |
| "happy", "laughing", "surprised", "delicious", "confident", "silly" | ANIMATION_HAPPY | 8 |
| "angry" | ANIMATION_FIRE | 2 |
| "sad", "crying" | ANIMATION_SHY | 6 |
| "embarrassed" | ANIMATION_EMBARRESSED | 1 |
| "loving", "shocked", "cool", "kissy" | ANIMATION_INSPIRATION | 3 |
| "thinking", "confused" | ANIMATION_QUESTION | 5 |
| "relaxed", "sleepy" | ANIMATION_SLEEP | 7 |
| "funny", "winking" | ANIMATION_NORMAL | 4 |

### 3. **Animation Types**
The system supports 9 animation types (indices 0-8):
- `ANIMATION_STATIC_NORMAL` (0)
- `ANIMATION_EMBARRESSED` (1)
- `ANIMATION_FIRE` (2)
- `ANIMATION_INSPIRATION` (3)
- `ANIMATION_NORMAL` (4)
- `ANIMATION_QUESTION` (5)
- `ANIMATION_SHY` (6)
- `ANIMATION_SLEEP` (7)
- `ANIMATION_HAPPY` (8)

### 4. **Animation Loading**
- Animations are loaded from SD card (`/sdcard/test.bin`)
- Supports "mega file" format containing all animations
- Falls back to individual animation files if mega file not found
- Uses overlay pixel system for efficient frame composition

### 5. **Frame Display**
- Each animation consists of multiple frames (2-4 frames depending on type)
- Frames are displayed in sequence with configurable delays
- Overlay frames (frames 1-3) use sparse pixel overlays for memory efficiency
- Base frame (frame 0) is reused for overlay composition

## Example Flow

**Server sends:**
```json
{
  "type": "llm",
  "emotion": "happy"
}
```

**Device processing:**
1. `Application::HandleWebSocketMessage()` parses JSON
2. Extracts `emotion = "happy"`
3. Calls `display->SetEmotion("happy")`
4. `LcdDisplay::SetEmotion()` finds "happy" in mapping table
5. Maps to `ANIMATION_HAPPY` (index 8)
6. Calls `animation_set_now_animation(8)`
7. Animation task starts playing happy animation frames
8. Each frame is displayed via `SetEmotionImg()`

## Summary

**Answer: The server returns a WORD (like "happy", "angry", "neutral"), NOT an emoji character.**

The device then:
1. Receives the emotion word from the server
2. Maps it to an animation type using a lookup table
3. Plays the corresponding animation sequence
4. Displays frames on the LCD display



