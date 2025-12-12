# Emotion Mappings Documentation

## Overview

This document maps emotion strings to animation types and SD card files in the firmware.

## Animation Types (enum AnimationType_e)

The firmware supports 9 animation types defined in `main/animation/animation.h`:

| Index | Enum Name | SD Card Files | Notes |
|-------|-----------|---------------|-------|
| 0 | `ANIMATION_STATIC_NORMAL` | `normal1.bin`, `normal2.bin`, `normal3.bin` | Static/neutral state |
| 1 | `ANIMATION_EMBARRESSED` | `embarrass1.bin`, `embarrass2.bin`, `embarrass3.bin` | Embarrassed emotion |
| 2 | `ANIMATION_FIRE` | `fire1.bin`, `fire2.bin`, `fire3.bin`, `fire4.bin` | Angry/fire emotion |
| 3 | `ANIMATION_INSPIRATION` | `inspiration1.bin`, `inspiration2.bin`, `inspiration3.bin`, `inspiration4.bin` | Inspiration emotion |
| 4 | `ANIMATION_NORMAL` | `normal1.bin`, `normal2.bin`, `normal3.bin` | Normal (same as STATIC_NORMAL) |
| 5 | `ANIMATION_QUESTION` | `question1.bin`, `question2.bin`, `question3.bin`, `question4.bin` | Question/thinking |
| 6 | `ANIMATION_SHY` | `shy1.bin`, `shy2.bin` | Shy/sad emotion |
| 7 | `ANIMATION_SLEEP` | `sleep1.bin`, `sleep2.bin`, `sleep3.bin`, `sleep4.bin` | Sleepy/sleep emotion |
| 8 | `ANIMATION_HAPPY` | `happy1.bin`, `happy2.bin`, `happy3.bin`, `happy4.bin` | Happy emotion |

## Emotion String Mappings

The firmware maps emotion strings to animation types in `main/display/lcd_display.cc`. Here's the complete mapping:

| Emotion String | Animation Type | SD Card Files Used |
|----------------|----------------|-------------------|
| `"neutral"` | `ANIMATION_STATIC_NORMAL` | normal*.bin |
| `"happy"` | `ANIMATION_HAPPY` | happy*.bin |
| `"laughing"` | `ANIMATION_HAPPY` | happy*.bin |
| `"funny"` | `ANIMATION_HAPPY` | happy*.bin |
| `"sad"` | `ANIMATION_SHY` | shy*.bin |
| `"angry"` | `ANIMATION_FIRE` | fire*.bin |
| `"crying"` | `ANIMATION_EMBARRESSED` | embarrass*.bin |
| `"loving"` | `ANIMATION_INSPIRATION` | inspiration*.bin |
| `"embarrassed"` | `ANIMATION_SHY` | shy*.bin |
| `"surprised"` | `ANIMATION_INSPIRATION` | inspiration*.bin |
| `"shocked"` | `ANIMATION_INSPIRATION` | inspiration*.bin |
| `"thinking"` | `ANIMATION_QUESTION` | question*.bin |
| `"winking"` | `ANIMATION_NORMAL` | normal*.bin |
| `"cool"` | `ANIMATION_INSPIRATION` | inspiration*.bin |
| `"relaxed"` | `ANIMATION_HAPPY` | happy*.bin |
| `"delicious"` | `ANIMATION_HAPPY` | happy*.bin |
| `"kissy"` | `ANIMATION_INSPIRATION` | inspiration*.bin |
| `"confident"` | `ANIMATION_HAPPY` | happy*.bin |
| `"sleepy"` | `ANIMATION_SLEEP` | sleep*.bin |
| `"silly"` | `ANIMATION_HAPPY` | happy*.bin |
| `"confused"` | `ANIMATION_QUESTION` | question*.bin |

## Missing Mappings

The following emotions are **NOT** currently mapped in the code but are mentioned as available in device bins:

### 1. `"normal"` 
- **Status**: ❌ Not mapped
- **Expected**: Should map to `ANIMATION_NORMAL` (index 4) or `ANIMATION_STATIC_NORMAL` (index 0)
- **SD Card Files**: `normal1.bin`, `normal2.bin`, `normal3.bin`
- **Recommendation**: Add `{ANIMATION_NORMAL, "normal"}` to the emotions vector

### 2. `"embarrass"`
- **Status**: ❌ Not mapped
- **Expected**: Should map to `ANIMATION_EMBARRESSED` (index 1)
- **SD Card Files**: `embarrass1.bin`, `embarrass2.bin`, `embarrass3.bin`
- **Note**: Currently only `"embarrassed"` is mapped (to `ANIMATION_SHY`)
- **Recommendation**: Add `{ANIMATION_EMBARRESSED, "embarrass"}` to the emotions vector

### 3. `"fire"`
- **Status**: ❌ Not mapped
- **Expected**: Should map to `ANIMATION_FIRE` (index 2)
- **SD Card Files**: `fire1.bin`, `fire2.bin`, `fire3.bin`, `fire4.bin`
- **Note**: Currently only `"angry"` maps to `ANIMATION_FIRE`
- **Recommendation**: Add `{ANIMATION_FIRE, "fire"}` to the emotions vector

### 4. `"inspiration"`
- **Status**: ❌ Not mapped
- **Expected**: Should map to `ANIMATION_INSPIRATION` (index 3)
- **SD Card Files**: `inspiration1.bin`, `inspiration2.bin`, `inspiration3.bin`, `inspiration4.bin`
- **Note**: Multiple emotions map to this (loving, surprised, shocked, cool, kissy)
- **Recommendation**: Add `{ANIMATION_INSPIRATION, "inspiration"}` to the emotions vector

### 5. `"question"`
- **Status**: ❌ Not mapped
- **Expected**: Should map to `ANIMATION_QUESTION` (index 5)
- **SD Card Files**: `question1.bin`, `question2.bin`, `question3.bin`, `question4.bin`
- **Note**: Currently only `"thinking"` and `"confused"` map to `ANIMATION_QUESTION`
- **Recommendation**: Add `{ANIMATION_QUESTION, "question"}` to the emotions vector

### 6. `"sleep"`
- **Status**: ❌ Not mapped
- **Expected**: Should map to `ANIMATION_SLEEP` (index 7)
- **SD Card Files**: `sleep1.bin`, `sleep2.bin`, `sleep3.bin`, `sleep4.bin`
- **Note**: Currently only `"sleepy"` maps to `ANIMATION_SLEEP`
- **Recommendation**: Add `{ANIMATION_SLEEP, "sleep"}` to the emotions vector

### 7. `"talk"` / `"talk1"` / `"talk2"` / `"talk3"` / `"talk4"`
- **Status**: ❌ Not mapped
- **Current Usage**: The code in `application.cc` generates `"talk1"`, `"talk2"`, `"talk3"`, `"talk4"` for speaking animations
- **SD Card Files**: Unknown - need to check if `talk1.bin`, `talk2.bin`, etc. exist
- **Recommendation**: 
  - Add talk animation enum values if separate animations are needed
  - OR map all talk variants to an existing animation (e.g., `ANIMATION_NORMAL`)
  - OR implement special handling for talk animations if they cycle through frames

## Summary of SD Card Files in Device Bin

Based on your device bin, you have these animation files:

| File Name | Animation Type | Status |
|-----------|----------------|--------|
| `normal*.bin` | `ANIMATION_NORMAL` / `ANIMATION_STATIC_NORMAL` | ✅ Loaded, ❌ `"normal"` not mapped |
| `embarrass*.bin` | `ANIMATION_EMBARRESSED` | ✅ Loaded, ❌ `"embarrass"` not mapped |
| `fire*.bin` | `ANIMATION_FIRE` | ✅ Loaded, ❌ `"fire"` not mapped |
| `happy*.bin` | `ANIMATION_HAPPY` | ✅ Loaded, ✅ `"happy"` mapped |
| `inspiration*.bin` | `ANIMATION_INSPIRATION` | ✅ Loaded, ❌ `"inspiration"` not mapped |
| `question*.bin` | `ANIMATION_QUESTION` | ✅ Loaded, ❌ `"question"` not mapped |
| `sleep*.bin` | `ANIMATION_SLEEP` | ✅ Loaded, ❌ `"sleep"` not mapped |
| `talk*.bin` | Unknown | ❓ Unknown if files exist, ❌ not mapped |

## Recommendations

To properly support all emotions from your device bin:

1. **Add missing emotion mappings** to `main/display/lcd_display.cc`:
   - `"normal"` → `ANIMATION_NORMAL`
   - `"embarrass"` → `ANIMATION_EMBARRESSED`
   - `"fire"` → `ANIMATION_FIRE`
   - `"inspiration"` → `ANIMATION_INSPIRATION`
   - `"question"` → `ANIMATION_QUESTION`
   - `"sleep"` → `ANIMATION_SLEEP`

2. **Handle talk animations**:
   - If `talk*.bin` files exist on SD card, add support for loading them
   - Map `"talk1"`, `"talk2"`, `"talk3"`, `"talk4"` appropriately
   - OR implement cycling through frames if it's meant to be one animation

3. **Consider adding direct animation type constants** that match SD card file names for consistency

