# Emotion Mapping System Guide

## Overview

The emotion mapping system connects emotion names (strings like "happy", "sad", etc.) to animation types that are displayed on the device screen. This guide explains where the mapping happens and how to add new emotions.

## Key Files

### 1. Main Emotion Mapping Location
**File:** `main/display/lcd_display.cc`  
**Function:** `LcdDisplay::SetEmotion(const char *emotion)`  
**Lines:** 1123-1193

This is the **primary location** where emotion strings are mapped to animation types for the EchoEar board.

### 2. Animation Type Definitions
**File:** `main/animation/animation.h`  
**Enum:** `AnimationType_e`  
**Lines:** 19-30

Defines all available animation types that can be used.

### 3. Animation Loading Functions
**File:** `main/animation/animation.cc`

Contains functions to load animations from SD card and get animation instances.

---

## Current Emotion Mapping

The emotion mapping is defined in `LcdDisplay::SetEmotion()` at **lines 1131-1152**:

```cpp
static const std::vector<Emotion> emotions = {
    {ANIMATION_STATIC_NORMAL, "neutral"},
    {ANIMATION_HAPPY, "happy"},
    {ANIMATION_HAPPY, "laughing"},
    {ANIMATION_HAPPY, "funny"},
    {ANIMATION_SHY, "sad"},
    {ANIMATION_FIRE, "angry"},
    {ANIMATION_EMBARRESSED, "crying"},
    {ANIMATION_INSPIRATION, "loving"},
    {ANIMATION_SHY, "embarrassed"},
    {ANIMATION_INSPIRATION, "surprised"},
    {ANIMATION_INSPIRATION, "shocked"},
    {ANIMATION_QUESTION, "thinking"},
    {ANIMATION_NORMAL, "winking"},
    {ANIMATION_INSPIRATION, "cool"},
    {ANIMATION_HAPPY, "relaxed"},
    {ANIMATION_HAPPY, "delicious"},
    {ANIMATION_INSPIRATION, "kissy"},
    {ANIMATION_HAPPY, "confident"},
    {ANIMATION_SLEEP, "sleepy"},
    {ANIMATION_HAPPY, "silly"},
    {ANIMATION_QUESTION, "confused"}
};
```

**Important:** The mapping structure is:
- **Left side (animation_num)**: Available system animation types (what the device can display)
- **Right side (text)**: Emotion strings received from the server/user

The lookup searches by the right side (server emotion string) and maps it to the left side (system animation type).

## Available Animation Types

Defined in `main/animation/animation.h`:

```cpp
typedef enum _AnimationType_e {
    ANIMATION_STATIC_NORMAL = 0,  // Static neutral state
    ANIMATION_EMBARRESSED,         // Embarrassed/shy expressions
    ANIMATION_FIRE,                // Excited/fire emotion
    ANIMATION_INSPIRATION,         // Inspired/surprised expressions
    ANIMATION_NORMAL,              // Normal blinking/idle
    ANIMATION_QUESTION,            // Questioning/thinking
    ANIMATION_SHY,                 // Shy/sad expressions
    ANIMATION_SLEEP,               // Sleepy/tired
    ANIMATION_HAPPY,               // Happy/joyful expressions
    ANIMATION_NUM                  // Total count
} AnimationType_e;
```

## Animation Assets

Animation frame files are stored in the `animations/` folder:
- `embarrass1.bin`, `embarrass2.bin`, `embarrass3.bin`
- `fire1.bin`, `fire2.bin`, `fire3.bin`, `fire4.bin`
- `happy1.bin`, `happy2.bin`, `happy3.bin`, `happy4.bin`
- `inspiration1.bin`, `inspiration2.bin`, `inspiration3.bin`, `inspiration4.bin`
- `question1.bin`, `question2.bin`, `question3.bin`, `question4.bin`
- `shy1.bin`, `shy2.bin`
- `sleep1.bin`, `sleep2.bin`, `sleep3.bin`, `sleep4.bin`
- `normal1.bin`, `normal2.bin`, `normal3.bin`, `normal_all.bin`

---

## How to Add a New Emotion

### Scenario 1: Map Server Emotion to Existing Animation Type

If the server sends a new emotion string that should map to an existing animation (e.g., server sends "excited" → should display `ANIMATION_FIRE`):

1. **Edit the emotion mapping** in `main/display/lcd_display.cc`:
   - Go to line 1131 (the `emotions` vector)
   - Add a new entry before the closing brace:
   ```cpp
   {ANIMATION_FIRE, "excited"},  // Server emotion "excited" → System animation ANIMATION_FIRE
   {ANIMATION_QUESTION, "confused"}
   ```

2. **That's it!** When the server sends "excited", it will now display the fire animation.

### Scenario 2: Create a Completely New Animation Type

If the server sends a new emotion that needs its own unique animation (e.g., server sends "excited" → needs new `ANIMATION_EXCITED`):

#### Step 1: Add Animation Type Enum
**File:** `main/animation/animation.h` (line 19-30)

Add the new animation type to the enum:
```cpp
typedef enum _AnimationType_e {
    ANIMATION_STATIC_NORMAL = 0,
    ANIMATION_EMBARRESSED,
    ANIMATION_FIRE,
    ANIMATION_INSPIRATION,
    ANIMATION_NORMAL,
    ANIMATION_QUESTION,
    ANIMATION_SHY,
    ANIMATION_SLEEP,
    ANIMATION_HAPPY,
    ANIMATION_EXCITED,  // ← Add new type here
    ANIMATION_NUM
} AnimationType_e;
```

#### Step 2: Add Animation Storage Variable
**File:** `main/animation/animation.cc` (around line 72-80)

Add a global animation variable:
```cpp
static Animation_t sd_excited = {0};  // Add this
```

#### Step 3: Add Getter Function
**File:** `main/animation/animation.cc` (around line 368-457)

Add a getter function following the pattern:
```cpp
Animation_t* animation_get_excited_animation(void)
{
    if (sd_excited.use_gif && sd_excited.gif_data && sd_excited.gif_data_size > 0) {
        return &sd_excited;
    }
    if (sd_excited.use_spiffs && sd_excited.imges && sd_excited.len > 0) {
        return &sd_excited;
    }
    ESP_LOGW("animation", "No excited animation available from SD card");
    return NULL;
}
```

#### Step 4: Add to get_animation() Switch
**File:** `main/animation/animation.cc` (around line 90-114)

Add a case in the switch statement:
```cpp
Animation_t* get_animation(int index) {
    switch(index) {
        // ... existing cases ...
        case 8: // ANIMATION_HAPPY
            return animation_get_happy_animation();
        case 9: // ANIMATION_EXCITED  ← Add this
            return animation_get_excited_animation();
        default:
            return animation_get_normal_animation();
    }
}
```

#### Step 5: Initialize Animation
**File:** `main/animation/animation.cc` (around line 82-88)

Add initialization in the `INIT_ANIM` section (around line 220-226):
```cpp
INIT_ANIM(sd_excited);  // Add this
```

#### Step 6: Add Loading Function
**File:** `main/animation/animation.cc` (around line 1394-1530)

Add a loading function following the pattern:
```cpp
bool animation_load_excited_from_sd_card(void)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    animation_cleanup_sd_card_animation(&sd_excited);
    
    // Load excited animation from SD card
    const char* excited_frames[] = {"excited1.bin", "excited2.bin", "excited3.bin"};
    
    if (animation_create_sd_card_animation(&sd_excited, excited_frames, 3)) {
        ESP_LOGI("animation", "✅ Successfully loaded excited animation from SD card");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load excited animation from SD card");
        return false;
    }
}
```

#### Step 7: Call Loading Function
**File:** `main/animation/animation.cc` (around line 252-260)

Add the loading call in `animation_load_sd_card_animations()`:
```cpp
bool excited_loaded = animation_load_excited_from_sd_card();
```

And add logging (around line 260-292):
```cpp
if (excited_loaded) {
    ESP_LOGI("animation", "   - Excited animation now uses SD card (excited1.bin, excited2.bin, excited3.bin)");
    ESP_LOGI("animation", "   - Excited SD card animation has %d frames", sd_excited.len);
}
```

#### Step 8: Add Function Declaration
**File:** `main/animation/animation.h` (around line 35-42)

Add the function declaration:
```cpp
Animation_t* animation_get_excited_animation(void);
```

And the loading function (around line 54-61):
```cpp
bool animation_load_excited_from_sd_card(void);
```

#### Step 9: Add Emotion Mapping
**File:** `main/display/lcd_display.cc` (line 1131-1152)

Add the emotion to the mapping (server emotion string → system animation):
```cpp
{ANIMATION_EXCITED, "excited"},  // When server sends "excited", use ANIMATION_EXCITED
```

#### Step 10: Create Animation Assets
Create the animation frame files (e.g., `excited1.bin`, `excited2.bin`, `excited3.bin`) and place them in the `animations/` folder or on the SD card.

---

## Flow Diagram

```
Server sends emotion: "happy" (via JSON/WebSocket)
         ↓
Application calls: display->SetEmotion("happy")
         ↓
LcdDisplay::SetEmotion("happy") in lcd_display.cc:1123
         ↓
Search emotions vector by text (right side): "happy"
         ↓
Found: {ANIMATION_HAPPY, "happy"}
         ↓
Extract animation_num (left side): ANIMATION_HAPPY
         ↓
Call: animation_set_now_animation(ANIMATION_HAPPY)
         ↓
Animation system uses get_animation(ANIMATION_HAPPY)
         ↓
Returns: animation_get_happy_animation()
         ↓
Loads animation from SD card (happy1.bin, happy2.bin, etc.)
         ↓
Displays animation frames on screen
```

**Key Point:** The mapping is **Server Emotion String → System Animation Type**

---

## Notes

1. **Multiple emotions can map to the same animation**: For example, "happy", "laughing", "funny" all map to `ANIMATION_HAPPY`.

2. **Default behavior**: If an emotion string is not found in the mapping, it defaults to `ANIMATION_NORMAL` (see line 1179-1180).

3. **Animation loading**: Animations are loaded from the SD card at runtime. If files are missing, the animation won't display but won't crash.

4. **Other boards**: There are also emotion mappings for other boards (electron-bot, otto-robot, esp-hi) in their respective display files, but the EchoEar board uses `lcd_display.cc`.

---

## Quick Reference: Current Mappings

**Format:** `{System Animation Type, Server Emotion String}`

| Server Emotion String | System Animation Type | Notes |
|----------------------|----------------------|-------|
| "neutral" | ANIMATION_STATIC_NORMAL | Static state |
| "happy", "laughing", "funny", "relaxed", "delicious", "confident", "silly" | ANIMATION_HAPPY | Happy expressions |
| "sad", "embarrassed" | ANIMATION_SHY | Shy/sad expressions |
| "angry" | ANIMATION_FIRE | Fire/excited |
| "crying" | ANIMATION_EMBARRESSED | Embarrassed |
| "loving", "surprised", "shocked", "cool", "kissy" | ANIMATION_INSPIRATION | Inspired expressions |
| "thinking", "confused" | ANIMATION_QUESTION | Questioning |
| "winking" | ANIMATION_NORMAL | Normal blinking |
| "sleepy" | ANIMATION_SLEEP | Sleepy state |

**Note:** Multiple server emotion strings can map to the same system animation type. This allows the server to send different emotion names, but the device will display the same animation.

