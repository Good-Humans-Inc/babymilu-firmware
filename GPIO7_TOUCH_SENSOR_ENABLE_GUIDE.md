# GPIO7 Touch Sensor Enable Guide for EchoEar

This guide explains how to enable the GPIO7 capacitive touch sensor functionality in the EchoEar firmware. The touch sensor allows users to interact with the device by touching a copper pad connected to GPIO7.

## Overview

The GPIO7 touch sensor is a capacitive touch sensor that:
- Maps GPIO7 to Touch Channel 7 (hardware mapping on ESP32-S3)
- Triggers random emotion animations (angry, happy, embarrassed) when touched
- Wakes up the device from power save mode
- Provides touch event logging and monitoring

## Quick Enable Steps

### Step 1: Add Required Includes

Ensure your board file includes these headers:

```cpp
#include <touch_sensor_lowlevel.h>
#include <touch_button_sensor.h>
```

**Location:** Add to the top of your board implementation file (e.g., `main/boards/echoear/echoear.cc`)

### Step 2: Add Configuration Definitions

Add these definitions to your board's `config.h` file:

```cpp
// Touch channel definition (GPIO7 = Touch Channel 7 on ESP32-S3)
#define TOUCH_CHANNEL_1        (7)

// Touch threshold definitions
#define LIGHT_TOUCH_THRESHOLD  (0.05)  // 0.0 - 1.0 range (lower = more sensitive)
```

**Location:** `main/boards/echoear/config.h` (or your board's config file)

### Step 3: Add Member Variables to Board Class

Add these private member variables to your board class:

```cpp
private:
    touch_button_handle_t touch_button_handle_ = nullptr;  // Touch button sensor handle for GPIO7
    static volatile uint32_t touch_event_count_;  // Counter for touch events
```

**Location:** In your board class private section (e.g., `main/boards/echoear/echoear.cc`)

### Step 4: Add Static Member Definition

Add this static member definition outside the class:

```cpp
// Static member variable definition
volatile uint32_t YourBoardClass::touch_event_count_ = 0;
```

**Location:** After the class definition, before `DECLARE_BOARD()` macro

### Step 5: Copy the InitializeTouchButton() Function

Copy the complete `InitializeTouchButton()` function from `main/boards/echoear/echoear.cc` (lines 647-801) to your board class.

**Key parts of the function:**
- Configures GPIO7 as Touch Channel 7
- Sets up touch sensor low-level driver
- Creates touch button sensor with callback
- Creates logging and event handling tasks
- Starts the touch sensor

### Step 6: Copy Supporting Task Functions

Copy these static task functions to your board class:

1. **touch_log_task()** - Logs touch sensor status periodically
   - Location: Lines 600-625 in `main/boards/echoear/echoear.cc`

2. **touch_button_event_task()** - Handles touch button sensor events
   - Location: Lines 627-645 in `main/boards/echoear/echoear.cc`

### Step 7: Enable Touch Sensor Initialization

In your board constructor, add the call to initialize the touch sensor:

```cpp
EchoEar() : boot_button_(BOOT_BUTTON_GPIO) {
    // ... other initializations ...
    
    InitializeButtons();
    InitializePowerSaveTimer();
    InitializeEmotionResetTimer();
    
    // Enable GPIO7 touch sensor
    ESP_LOGI(TAG, "[TOUCH] About to call InitializeTouchButton()");
    InitializeTouchButton();
    ESP_LOGI(TAG, "[TOUCH] InitializeTouchButton() returned");
    
    // ... rest of constructor ...
}
```

**Location:** In your board constructor, typically after button initialization

### Step 8: Add Emotion Reset Timer (Optional but Recommended)

If you want the touch sensor to restore the previous emotion after showing a random one, add:

```cpp
private:
    esp_timer_handle_t emotion_reset_timer_ = nullptr;
    std::string previous_emotion_ = "neutral";
```

And add this initialization function:

```cpp
void InitializeEmotionResetTimer() {
    const esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            YourBoardClass* board = static_cast<YourBoardClass*>(arg);
            if (board != nullptr) {
                auto display = board->GetDisplay();
                if (display != nullptr) {
                    display->SetEmotion(board->previous_emotion_.c_str());
                    ESP_LOGI(TAG, "[TOUCH] Emotion restored to previous state: %s", 
                             board->previous_emotion_.c_str());
                }
            }
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "emotion_reset_timer",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &emotion_reset_timer_));
}
```

## Complete Code Reference

### Touch Button Callback (Inside InitializeTouchButton)

The touch button callback handles touch events and triggers random emotions:

```cpp
ret = touch_button_sensor_create(&touch_cfg, &touch_button_handle_, 
                                 [](touch_button_handle_t handle, uint32_t channel, 
                                    touch_state_t state, void *cb_arg) {
    YourBoardClass::touch_event_count_++;
    
    if (state == TOUCH_STATE_ACTIVE) {
        ESP_LOGI(TAG, "[TOUCH] Touch button pressed (channel %lu)", 
                 (unsigned long)channel);
        
        YourBoardClass* board = static_cast<YourBoardClass*>(cb_arg);
        if (board != nullptr) {
            // Wake up power save timer
            if (board->power_save_timer_) {
                board->power_save_timer_->WakeUp();
            }
            
            // Stop any existing emotion reset timer
            if (board->emotion_reset_timer_ != nullptr) {
                esp_timer_stop(board->emotion_reset_timer_);
            }
            
            // Store current emotion state
            board->previous_emotion_ = "neutral";
            
            // Randomly select one of three emotions
            const char* emotions[] = {"angry", "happy", "embarrassed"};
            uint32_t random_index = esp_random() % 3;
            const char* selected_emotion = emotions[random_index];
            
            // Set the random emotion on display
            auto display = board->GetDisplay();
            if (display != nullptr) {
                display->SetEmotion(selected_emotion);
                ESP_LOGI(TAG, "[TOUCH] Random emotion selected: %s", selected_emotion);
            }
            
            // Calculate animation duration and start reset timer
            int frame_counts[] = {4, 4, 3};  // angry, happy, embarrassed
            int frame_count = frame_counts[random_index];
            int64_t animation_duration_us = frame_count * 500 * 1000;
            
            if (board->emotion_reset_timer_ != nullptr) {
                esp_timer_start_once(board->emotion_reset_timer_, animation_duration_us);
            }
        }
    } else {
        ESP_LOGI(TAG, "[TOUCH] Touch button released (channel %lu)", 
                 (unsigned long)channel);
    }
}, this);
```

## Hardware Requirements

1. **GPIO Pin**: GPIO7 (ESP32-S3)
2. **Touch Channel**: Channel 7 (hardware mapping: GPIO7 ↔ Touch Channel 7)
3. **Connection**: Copper pad/layer connected to GPIO7 via dupont wire or PCB trace

**Note:** No explicit GPIO configuration is required! The ESP-IDF touch sensor driver automatically configures GPIO7 for touch sensor functionality when you initialize the touch sensor.

## Verification

After enabling, you should see these log messages on boot:

```
[TOUCH] ===== Starting touch button initialization =====
[TOUCH] GPIO7 -> Touch Channel 7
[TOUCH] Threshold: 0.050
[TOUCH] Step 1: Preparing channel configuration
[TOUCH] Channel list: [7]
[TOUCH] Step 2: Creating low-level touch sensor
[TOUCH] ✓ Low-level touch sensor created successfully
[TOUCH] Step 3: Configuring touch button sensor
[TOUCH] Step 4: Creating touch button sensor with callback
[TOUCH] ✓ Touch button sensor created successfully
[TOUCH] Step 5: Starting touch sensor
[TOUCH] ===== Touch button initialization complete =====
[TOUCH] Touch sensor is now active and monitoring GPIO7
```

When you touch the GPIO7 pad, you should see:

```
[TOUCH] Touch button pressed (channel 7)
[TOUCH] Random emotion selected: happy
[TOUCH] Touch sensor status: Events detected! Total events: 1 (GPIO7)
```

## Troubleshooting

### Touch sensor not initializing
- Check that `touch_sensor_lowlevel.h` and `touch_button_sensor.h` are included
- Verify GPIO7 is not used by another peripheral
- Check ESP-IDF version compatibility (requires ESP-IDF v5.0+)

### No touch events detected
- Verify hardware connection to GPIO7
- Check touch threshold sensitivity (try lowering `LIGHT_TOUCH_THRESHOLD`)
- Ensure copper pad is properly connected
- Check logs for initialization errors

### Touch events but no emotion change
- Verify `GetDisplay()` returns valid display pointer
- Check that emotion animations ("angry", "happy", "embarrassed") exist
- Verify `SetEmotion()` function is implemented in display class

## Files to Reference

- **Main implementation**: `main/boards/echoear/echoear.cc` (lines 647-801)
- **Configuration**: `main/boards/echoear/config.h` (lines 62-67)
- **Documentation**: `ECHOEAR_TOUCH_SENSOR_COMPREHENSIVE_DOCUMENTATION.md`

## Summary Checklist

- [ ] Added touch sensor includes (`touch_sensor_lowlevel.h`, `touch_button_sensor.h`)
- [ ] Added config definitions (`TOUCH_CHANNEL_1`, `LIGHT_TOUCH_THRESHOLD`)
- [ ] Added member variables (`touch_button_handle_`, `touch_event_count_`)
- [ ] Added static member definition (`touch_event_count_ = 0`)
- [ ] Copied `InitializeTouchButton()` function
- [ ] Copied `touch_log_task()` function
- [ ] Copied `touch_button_event_task()` function
- [ ] Added `InitializeTouchButton()` call in constructor
- [ ] Added emotion reset timer (optional)
- [ ] Verified hardware connection to GPIO7
- [ ] Tested touch functionality

## Notes

- The touch sensor uses ESP32-S3's built-in capacitive touch functionality
- GPIO7 is hardwired to Touch Channel 7 (cannot be changed)
- Lower threshold values = more sensitive touch detection
- The touch sensor works independently of the CST816S touchscreen (if present)
- Touch events increment a counter and trigger random emotion animations
