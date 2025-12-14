# Touch Sensor Integration Guide for EchoEar Board

## Overview
This guide documents all required APIs, headers, and configurations needed to transplant touch sensor functionality from the bread-compact-wifi board to the EchoEar board. The EchoEar board uses **GPIO7** connected to a thin copper layer for touch detection.

## Hardware Information
- **GPIO Pin**: GPIO7
- **Touch Channel**: Channel 7 (on ESP32-S3, GPIO7 maps to touch channel 7)
- **Note**: GPIO7 is already defined as `TOUCH_PAD1` in `main/boards/echoear/config.h` (line 66)

## Required Headers

Add these includes to `EchoEar.cc`:

```cpp
#include "touch_button.h"
#include "touch_sensor_lowlevel.h"
```

## Required Dependencies

The `touch_button` component is already included in the root `CMakeLists.txt` (line 8), so no additional CMake changes are needed. The component automatically pulls in:
- `espressif/touch_button_sensor`
- `espressif/touch_sensor_lowlevel`
- `espressif/touch_sensor_fsm`

## Configuration Definitions

Add to `main/boards/echoear/config.h` or directly in `EchoEar.cc`:

```cpp
// Touch channel definition (GPIO7 = Touch Channel 7 on ESP32-S3)
#define TOUCH_CHANNEL_1        (7)

// Touch threshold definitions
#define LIGHT_TOUCH_THRESHOLD  (0.1)  // 0.0 - 1.0 range
#define HEAVY_TOUCH_THRESHOLD  (0.4)  // Optional: for heavy press detection
```

## Core API Functions

### 1. Low-Level Touch Sensor Initialization

**Function**: `touch_sensor_lowlevel_create()`
- **Purpose**: Creates and initializes the low-level touch sensor driver
- **Parameters**:
  ```cpp
  touch_lowlevel_config_t low_config = {
      .channel_num = 1,                           // Number of touch channels
      .channel_list = touch_channel_list,         // Array of channel numbers
      .channel_type = channel_type,               // Array of channel types
  };
  ```
- **Returns**: `esp_err_t` (ESP_OK on success)
- **Usage**:
  ```cpp
  uint32_t touch_channel_list[] = {TOUCH_CHANNEL_1};
  touch_lowlevel_type_t *channel_type = (touch_lowlevel_type_t*)calloc(1, sizeof(touch_lowlevel_type_t));
  channel_type[0] = TOUCH_LOWLEVEL_TYPE_TOUCH;
  
  touch_lowlevel_config_t low_config = {
      .channel_num = 1,
      .channel_list = touch_channel_list,
      .channel_type = channel_type,
  };
  
  esp_err_t ret = touch_sensor_lowlevel_create(&low_config);
  free(channel_type);
  ```

### 2. Touch Button Device Creation

**Function**: `iot_button_new_touch_button_device()`
- **Purpose**: Creates a touch button device integrated with the iot_button framework
- **Parameters**:
  ```cpp
  button_config_t btn_cfg = {
      .long_press_time = 2000,    // Long press duration in ms
      .short_press_time = 300,    // Short press debounce time in ms
  };
  
  button_touch_config_t touch_cfg = {
      .touch_channel = TOUCH_CHANNEL_1,           // Touch channel number
      .channel_threshold = LIGHT_TOUCH_THRESHOLD, // Threshold (0.0-1.0)
      .skip_lowlevel_init = true,                 // true if lowlevel already created
  };
  ```
- **Returns**: `esp_err_t` (ESP_OK on success)
- **Usage**:
  ```cpp
  button_handle_t touch_button_handle = NULL;
  esp_err_t ret = iot_button_new_touch_button_device(&btn_cfg, &touch_cfg, &touch_button_handle);
  ```

### 3. Start Touch Sensor

**Function**: `touch_sensor_lowlevel_start()`
- **Purpose**: Starts the touch sensor operation
- **Returns**: `esp_err_t` (ESP_OK on success)
- **Usage**: Call this after creating all touch buttons and low-level sensor
  ```cpp
  esp_err_t ret = touch_sensor_lowlevel_start();
  ```

### 4. Register Touch Button Callback

**Function**: `iot_button_register_cb()`
- **Purpose**: Registers a callback function for button events
- **Parameters**:
  - `button_handle_t btn`: Button handle from `iot_button_new_touch_button_device()`
  - `button_event_t event`: Event type (e.g., `BUTTON_PRESS_DOWN`, `BUTTON_PRESS_UP`, `BUTTON_SINGLE_CLICK`)
  - `button_cb_type_t cb_type`: Callback type (usually NULL)
  - `button_cb cb`: Callback function
  - `void *user_data`: User data to pass to callback
- **Usage**:
  ```cpp
  void touch_button_callback(void *arg, void *data) {
      button_event_t event = iot_button_get_event(arg);
      ESP_LOGI(TAG, "Touch button event: %s", iot_button_get_event_str(event));
      
      auto& app = Application::GetInstance();
      // Add your touch handling logic here
  }
  
  iot_button_register_cb(touch_button_handle, BUTTON_PRESS_DOWN, NULL, touch_button_callback, NULL);
  ```

## Complete Implementation Pattern

Based on `bread-compact-wifi/compact_wifi_board.cc`, here's the complete initialization pattern:

```cpp
class EspS3Cat : public WifiBoard {
private:
    button_handle_t touch_button_handle_ = nullptr;  // Add this member variable
    
    void InitializeTouchButton() {
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
        
        // Step 3: Configure button parameters
        const button_config_t btn_cfg = {
            .long_press_time = 2000,
            .short_press_time = 300,
        };
        
        // Step 4: Configure touch button
        button_touch_config_t touch_cfg = {
            .touch_channel = static_cast<int32_t>(TOUCH_CHANNEL_1),
            .channel_threshold = LIGHT_TOUCH_THRESHOLD,
            .skip_lowlevel_init = true,  // true because we already created lowlevel
        };
        
        // Step 5: Create touch button device
        ret = iot_button_new_touch_button_device(&btn_cfg, &touch_cfg, &touch_button_handle_);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Create touch button failed: %d", ret);
            return;
        }
        ESP_LOGI(TAG, "Touch button created successfully");
        
        // Step 6: Register callback
        iot_button_register_cb(touch_button_handle_, BUTTON_PRESS_DOWN, NULL, 
                               [](void *arg, void *data) {
                                   auto& app = Application::GetInstance();
                                   // Add your touch handling logic
                                   ESP_LOGI(TAG, "Touch button pressed");
                               }, NULL);
        
        // Step 7: Start touch sensor
        ret = touch_sensor_lowlevel_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Touch sensor start failed: %d", ret);
            return;
        }
        
        ESP_LOGI(TAG, "Touch button initialization complete");
    }
    
public:
    EspS3Cat() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        uint8_t pcb_version = DetectPcbVersion();
        InitializeCharge();
        InitializeCst816sTouchPad();
        InitializeSpi();
        Initializest77916Display(pcb_version);
        InitializeButtons();
        InitializeTouchButton();  // Add this call
    }
};
```

## Available Button Events

The following events can be registered with `iot_button_register_cb()`:

- `BUTTON_PRESS_DOWN` - Button pressed down
- `BUTTON_PRESS_UP` - Button released
- `BUTTON_SINGLE_CLICK` - Single click detected
- `BUTTON_DOUBLE_CLICK` - Double click detected
- `BUTTON_MULTIPLE_CLICK` - Multiple clicks detected
- `BUTTON_LONG_PRESS_START` - Long press started
- `BUTTON_LONG_PRESS_HOLD` - Long press holding
- `BUTTON_LONG_PRESS_UP` - Long press released
- `BUTTON_PRESS_REPEAT` - Repeat press (for repeat count)
- `BUTTON_PRESS_REPEAT_DONE` - Repeat press done

## Helper Functions

- `iot_button_get_event()` - Get current button event
- `iot_button_get_event_str()` - Get event as string for logging
- `iot_button_get_ticks_time()` - Get press duration in ticks
- `iot_button_get_repeat()` - Get repeat count
- `iot_button_delete()` - Clean up button (usually not needed, handled by destructor)

## Threshold Tuning

The `channel_threshold` value (0.0 - 1.0) determines touch sensitivity:
- **Lower values (0.05-0.1)**: More sensitive, detects lighter touches
- **Higher values (0.3-0.5)**: Less sensitive, requires firmer touch
- **Recommended starting value**: 0.1

You may need to adjust this based on:
- Copper layer size
- Environmental conditions
- Touch sensitivity requirements

## Important Notes

1. **Channel Number**: On ESP32-S3, GPIO7 directly maps to touch channel 7, so use `7` as the touch channel number.

2. **Initialization Order**: 
   - Create low-level sensor first
   - Then create touch button devices
   - Register callbacks
   - Finally start the sensor

3. **Memory Management**: Remember to free the `channel_type` array after use.

4. **Error Handling**: Always check return values and handle errors appropriately.

5. **Thread Safety**: The touch button callbacks are called from the iot_button task context, so ensure your callback functions are thread-safe.

## Example Integration with Existing EchoEar Functionality

You can integrate touch button events with the existing application logic:

```cpp
iot_button_register_cb(touch_button_handle_, BUTTON_PRESS_DOWN, NULL, 
                       [](void *arg, void *data) {
                           auto& app = Application::GetInstance();
                           // Start listening when touch is detected
                           app.StartListening();
                       }, NULL);

iot_button_register_cb(touch_button_handle_, BUTTON_PRESS_UP, NULL, 
                       [](void *arg, void *data) {
                           auto& app = Application::GetInstance();
                           // Stop listening when touch is released
                           app.StopListening();
                       }, NULL);
```

This pattern allows the copper layer touch to trigger the same functionality as other input methods on the board.

