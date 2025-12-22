# EchoEar Touch Sensor Implementation Guide

## Overview
This guide documents the complete implementation steps to add capacitive touch sensor functionality to the EchoEar board using GPIO7 connected to a copper layer.

## Hardware Setup
- **GPIO Pin**: GPIO7
- **Touch Channel**: Channel 7 (ESP32-S3 maps GPIO7 to touch channel 7)
- **Connection**: Copper layer connected via dupont wire to GPIO7

## Quick Start Checklist

Follow these steps in order:

1. ✅ **Add dependencies** to `main/idf_component.yml`
2. ✅ **Run** `idf.py reconfigure` to download managed components
3. ✅ **Add configuration** to `main/boards/echoear/config.h`
4. ✅ **Add headers** to `main/boards/echoear/EchoEar.cc`
5. ✅ **Add member variables** to `EspS3Cat` class
6. ✅ **Add methods** (`InitializeTouchButton()`, task functions)
7. ✅ **Add static member definition** (outside class)
8. ✅ **Add initialization call** in constructor
9. ✅ **Build and test**: `idf.py build flash monitor`

**Estimated time**: 15-20 minutes

---

## File Modifications Summary

### 1. `main/boards/echoear/config.h`

#### Add Configuration Definitions:
```cpp
// Touch channel definition (GPIO7 = Touch Channel 7 on ESP32-S3)
#define TOUCH_CHANNEL_1        (7)

// Touch threshold definitions
// Lower values = more sensitive (can detect through thicker materials)
// Higher values = less sensitive (requires closer/firmer touch)
#define LIGHT_TOUCH_THRESHOLD  (0.05)  // 0.0 - 1.0 range (adjust for sensitivity)
#define HEAVY_TOUCH_THRESHOLD  (0.4)   // Optional: for heavy press detection
```

**Location**: Add after line 66 (after `#define TOUCH_PAD1 GPIO_NUM_7`)

---

### 2. `main/idf_component.yml`

#### Add Dependencies:
```yaml
  espressif/button: ~4.1.3
  espressif/touch_button_sensor: "*"
  espressif/touch_sensor_lowlevel: "*"
  espressif/knob: ^1.0.0
```

**Location**: Add after `espressif/button: ~4.1.3` (around line 28)

**Note**: After adding, run `idf.py reconfigure` to download the managed components.

---

### 3. `main/boards/echoear/EchoEar.cc`

#### A. Add Required Headers (after line 20, after `#include "touch.h"`):
```cpp
#include <touch_sensor_lowlevel.h>
#include <touch_button_sensor.h>
```

#### B. Add Member Variables to `EspS3Cat` class (in private section, around line 394):
```cpp
    touch_button_handle_t touch_button_handle_ = nullptr;  // Touch button sensor handle for GPIO7
    static volatile uint32_t touch_event_count_;  // Counter for touch events
    QueueHandle_t touch_action_queue_ = nullptr;  // Queue for deferring touch actions
```

#### C. Add Static Member Variable Definition (after class definition, before `DECLARE_BOARD`):
```cpp
// Static member variable definition
volatile uint32_t EspS3Cat::touch_event_count_ = 0;
```

#### D. Add `InitializeTouchButton()` Method (add as private method, before `InitializeButtons()`):
```cpp
    void InitializeTouchButton()
    {
        ESP_LOGI(TAG, "Initializing touch button on GPIO7");
        
        // Step 1: Prepare channel configuration
        uint32_t touch_channel_list[] = {TOUCH_CHANNEL_1};  // GPIO7 = Channel 7
        touch_lowlevel_type_t *channel_type = (touch_lowlevel_type_t*)calloc(1, sizeof(touch_lowlevel_type_t));
        if (channel_type == NULL) {
            ESP_LOGE(TAG, "Memory allocation failed");
            return;
        }
        channel_type[0] = TOUCH_LOWLEVEL_TYPE_TOUCH;
        
        // Step 2: Create low-level touch sensor
        touch_lowlevel_config_t low_config = {
            .channel_num = 1,
            .channel_list = touch_channel_list,
            .channel_type = channel_type,
        };
        
        esp_err_t ret = touch_sensor_lowlevel_create(&low_config);
        free(channel_type);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Touch sensor lowlevel create failed: %d", ret);
            return;
        }
        ESP_LOGI(TAG, "Touch sensor lowlevel created successfully");
        
        // Step 3: Configure touch button sensor
        float channel_threshold[] = {LIGHT_TOUCH_THRESHOLD};
        
        touch_button_config_t touch_cfg = {
            .channel_num = 1,
            .channel_list = touch_channel_list,  // Reuse the array from Step 1
            .channel_threshold = channel_threshold,
            .channel_gold_value = NULL,  // Optional: can be used for calibration
            .debounce_times = 1,  // Reduced for faster response (adjust if needed)
            .skip_lowlevel_init = true,  // true because we already created lowlevel
        };
        
        ESP_LOGI(TAG, "Touch sensor configured with threshold: %.3f (lower = more sensitive)", LIGHT_TOUCH_THRESHOLD);
        
        // Create queue for touch actions to avoid heavy operations in callback
        touch_action_queue_ = xQueueCreate(10, sizeof(touch_state_t));
        if (touch_action_queue_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create touch action queue");
            return;
        }
        
        // Step 4: Create touch button sensor with callback
        touch_event_count_ = 0;  // Initialize counter
        ret = touch_button_sensor_create(&touch_cfg, &touch_button_handle_, 
                                         [](touch_button_handle_t handle, uint32_t channel, touch_state_t state, void *cb_arg) {
                                             // Lightweight callback - just log and queue the action
                                             touch_event_count_++;  // Increment on any touch event
                                             
                                             if (state == TOUCH_STATE_ACTIVE) {
                                                 ESP_LOGI(TAG, "Touch button pressed down (channel %lu)", (unsigned long)channel);
                                             } else {
                                                 ESP_LOGI(TAG, "Touch button released (channel %lu)", (unsigned long)channel);
                                             }
                                             
                                             // Queue the action to be processed in the task (non-blocking)
                                             EspS3Cat* board = static_cast<EspS3Cat*>(cb_arg);
                                             if (board != nullptr && board->touch_action_queue_ != nullptr) {
                                                 touch_state_t state_to_queue = state;
                                                 xQueueSend(board->touch_action_queue_, &state_to_queue, 0);  // Non-blocking
                                             }
                                         }, this);  // Pass 'this' as callback argument
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Create touch button sensor failed: %d", ret);
            return;
        }
        ESP_LOGI(TAG, "Touch button sensor created successfully");
        
        // Create periodic logging task
        xTaskCreatePinnedToCore(touch_log_task, "touch_log_task", 2 * 1024, this, 1, NULL, 1);
        
        // Create task to handle touch button sensor events (larger stack for callback operations)
        xTaskCreatePinnedToCore(touch_button_event_task, "touch_btn_task", 4 * 1024, this, 5, NULL, 1);
        
        // Step 5: Start touch sensor
        ret = touch_sensor_lowlevel_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Touch sensor start failed: %d", ret);
            return;
        }
        
        ESP_LOGI(TAG, "Touch button initialization complete");
    }
```

#### E. Add Static Task Functions (add as private static methods, before `InitializeCharge()`):
```cpp
    static void touch_log_task(void* arg)
    {
        EspS3Cat* board = static_cast<EspS3Cat*>(arg);
        if (board == nullptr) {
            ESP_LOGE(TAG, "Invalid board pointer in touch_log_task");
            vTaskDelete(NULL);
            return;
        }

        uint32_t last_count = 0;
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));  // Wait 1 second
            
            uint32_t current_count = touch_event_count_;
            if (current_count > last_count) {
                ESP_LOGI(TAG, "Touch sensor status: Events detected! Total events: %lu (GPIO7)", 
                         (unsigned long)current_count);
                last_count = current_count;
            } else {
                ESP_LOGI(TAG, "Touch sensor status: No new events (GPIO7)");
            }
        }
    }

    static void touch_button_event_task(void* arg)
    {
        EspS3Cat* board = static_cast<EspS3Cat*>(arg);
        if (board == nullptr) {
            ESP_LOGE(TAG, "Invalid board pointer in touch_button_event_task");
            vTaskDelete(NULL);
            return;
        }

        touch_state_t queued_state;
        while (true) {
            // Handle touch button sensor events
            if (board->touch_button_handle_ != nullptr) {
                touch_button_sensor_handle_events(board->touch_button_handle_);
            }
            
            // Process queued touch actions (defer heavy operations from callback)
            if (board->touch_action_queue_ != nullptr) {
                while (xQueueReceive(board->touch_action_queue_, &queued_state, 0) == pdTRUE) {
                    auto& app = Application::GetInstance();
                    if (queued_state == TOUCH_STATE_ACTIVE) {
                        app.StartListening();
                    } else {
                        app.StopListening();
                    }
                }
            }
            
            vTaskDelay(pdMS_TO_TICKS(10));  // Check every 10ms
        }
    }
```

#### F. Add Initialization Call in Constructor (in `EspS3Cat()` constructor, after `InitializeButtons()`):
```cpp
        InitializeButtons();
        InitializeTouchButton();  // Add this line
```

---

## Required Headers Summary

### In `EchoEar.cc`:
```cpp
#include <touch_sensor_lowlevel.h>    // For touch_sensor_lowlevel_create(), touch_sensor_lowlevel_start()
#include <touch_button_sensor.h>      // For touch_button_sensor_create(), touch_button_sensor_handle_events()
```

**Note**: These headers are from managed components that need to be added to `idf_component.yml`.

---

## Methods/Functions to Add

### In `EspS3Cat` class:

1. **Private Member Variables**:
   - `touch_button_handle_t touch_button_handle_`
   - `static volatile uint32_t touch_event_count_`
   - `QueueHandle_t touch_action_queue_`

2. **Private Methods**:
   - `void InitializeTouchButton()` - Main initialization method

3. **Private Static Methods**:
   - `static void touch_log_task(void* arg)` - Periodic logging task
   - `static void touch_button_event_task(void* arg)` - Touch event handling task

4. **Static Member Variable Definition** (outside class):
   - `volatile uint32_t EspS3Cat::touch_event_count_ = 0;`

5. **Constructor Modification**:
   - Add `InitializeTouchButton();` call after `InitializeButtons();`

---

## API Functions Used

### From `touch_sensor_lowlevel.h`:
- `touch_sensor_lowlevel_create()` - Creates low-level touch sensor driver
- `touch_sensor_lowlevel_start()` - Starts touch sensor operation

### From `touch_button_sensor.h`:
- `touch_button_sensor_create()` - Creates touch button sensor instance
- `touch_button_sensor_handle_events()` - Processes touch events (must be called periodically)

### Types Used:
- `touch_lowlevel_config_t` - Low-level configuration structure
- `touch_lowlevel_type_t` - Touch channel type
- `touch_button_config_t` - Touch button sensor configuration
- `touch_button_handle_t` - Touch button sensor handle
- `touch_state_t` - Touch state enum (TOUCH_STATE_ACTIVE, TOUCH_STATE_INACTIVE)

---

## Configuration Parameters

### Threshold Tuning:
- **LIGHT_TOUCH_THRESHOLD**: Default `0.05` (0.0-1.0 range)
  - Lower values = more sensitive (detects through thicker materials)
  - Higher values = less sensitive (requires closer touch)
  - Recommended values: `0.03` (very sensitive), `0.05` (balanced), `0.1` (less sensitive)

### Debounce:
- **debounce_times**: Default `1` (number of consecutive readings to confirm state change)
  - Lower = faster response, may be noisier
  - Higher = more stable, slower response

---

## Initialization Order

1. Create low-level touch sensor (`touch_sensor_lowlevel_create`)
2. Create touch button sensor (`touch_button_sensor_create`)
3. Create tasks (logging and event handling)
4. Start touch sensor (`touch_sensor_lowlevel_start`)

**Important**: The `skip_lowlevel_init` flag must be `true` because we create the low-level sensor separately.

---

## Menuconfig Configuration

No special menuconfig changes are required. The touch sensor support is enabled by default on ESP32-S3.

Optional optimizations (in `Component config` → `ESP-Driver: Touch Sensor Configurations`):
- `[*] Place Touch Sensor Control Functions into IRAM` - Better performance
- `[*] Touch Sensor ISR IRAM-Safe` - For low-power scenarios

---

## Testing

After implementation:
1. Build the project: `idf.py build`
2. Flash to device: `idf.py flash monitor`
3. Look for log messages:
   - "Initializing touch button on GPIO7"
   - "Touch sensor lowlevel created successfully"
   - "Touch button sensor created successfully"
   - "Touch button initialization complete"
4. Test touch detection - logs will show "Touch button pressed down" and "Touch button released"

---

## Troubleshooting

### If touch doesn't work:
1. Check threshold value - try lowering `LIGHT_TOUCH_THRESHOLD` to `0.03` or `0.02`
2. Verify GPIO7 connection to copper layer
3. Check logs for initialization errors
4. Ensure managed components are downloaded (`idf.py reconfigure`)

### If stack overflow occurs:
- **Symptom**: `***ERROR*** A stack overflow in task touch_btn_task has been detected`
- **Solution**: The stack size is already set to 4KB in the guide. If still occurring:
  - Increase to `5 * 1024` or `6 * 1024` in `xTaskCreatePinnedToCore` for `touch_button_event_task`
  - Ensure heavy operations (`StartListening()`/`StopListening()`) are done in the task, not callback

### If false triggers:
- Increase `LIGHT_TOUCH_THRESHOLD` to `0.08` or `0.1`
- Increase `debounce_times` to `2` or `3`

### Common Compilation Errors:

**Error: `touch_button.h: No such file or directory`**
- Solution: Don't include `touch_button.h` - use `touch_button_sensor.h` instead

**Error: `button_touch_config_t` not found**
- Solution: Use `touch_button_config_t` from `touch_button_sensor.h`, not from a non-existent `button_touch.h`

**Error: Conflicting declarations for `touch_button_handle_t`**
- Solution: Don't include `<touch_element/touch_button.h>` - it conflicts with `touch_button_sensor.h`

---

## Files Modified Summary

1. ✅ `main/boards/echoear/config.h` - Add touch channel and threshold definitions
2. ✅ `main/idf_component.yml` - Add touch_button_sensor and touch_sensor_lowlevel dependencies
3. ✅ `main/boards/echoear/EchoEar.cc` - Add headers, member variables, methods, and initialization

**Total**: 3 files to modify

---

## Dependencies

The following managed components are automatically pulled in:
- `espressif/touch_button_sensor` - Touch button sensor API
- `espressif/touch_sensor_lowlevel` - Low-level touch sensor driver
- `espressif/touch_sensor_fsm` - Touch sensor finite state machine (auto-dependency)

---

## Notes

- The touch sensor uses GPIO7 which maps to touch channel 7 on ESP32-S3
- The implementation uses a queue to defer heavy operations (`StartListening()`/`StopListening()`) from the callback to avoid stack overflow
- Periodic logging task reports touch events every second for debugging
- The event handling task runs every 10ms to process touch events
