# EchoEar Touch Sensor - Comprehensive Technical Documentation

## Table of Contents
1. [Hardware Overview](#hardware-overview)
2. [GPIO Configuration](#gpio-configuration)
3. [Software Dependencies](#software-dependencies)
4. [Configuration Parameters](#configuration-parameters)
5. [Low-Level Touch Sensor Setup](#low-level-touch-sensor-setup)
6. [Touch Button Sensor Configuration](#touch-button-sensor-configuration)
7. [Event Handling & Callbacks](#event-handling--callbacks)
8. [Logging & Monitoring](#logging--monitoring)
9. [Sensitivity Tuning](#sensitivity-tuning)
10. [Code Flow & Architecture](#code-flow--architecture)
11. [API Reference](#api-reference)
12. [Troubleshooting](#troubleshooting)

---

## Hardware Overview

### Physical Connection
- **GPIO Pin**: GPIO7 (ESP32-S3)
- **Touch Channel**: Channel 7 (ESP32-S3 hardware mapping: GPIO7 ↔ Touch Channel 7)
- **Sensor Type**: Capacitive touch sensor
- **Connection Method**: Copper layer connected to GPIO7 via dupont wire
- **Detection Method**: Capacitive coupling - detects change in capacitance when touched

### ESP32-S3 Touch Sensor Specifications
- **Supported Touch Channels**: 0-13 (14 channels total on ESP32-S3)
- **GPIO7 Mapping**: Fixed hardware mapping to Touch Channel 7
- **No External Components Required**: ESP32-S3 has built-in touch sensor peripheral
- **GPIO Configuration**: Automatically handled by ESP-IDF touch driver (no manual GPIO config needed)

---

## GPIO Configuration

### Important Note
**No explicit GPIO configuration is required!** The ESP-IDF touch sensor driver automatically configures GPIO7 for touch sensor functionality when you initialize the touch sensor.

The touch sensor driver internally:
- Sets GPIO7 to input mode
- Configures the pin for touch sensor functionality
- Manages the capacitive sensing circuit

### GPIO Pin Details
```cpp
// Location: main/boards/EchoEar/config.h
#define TOUCH_CHANNEL_1  (7)  // GPIO7 = Touch Channel 7 (hardware mapping)
```

**Key Points:**
- GPIO7 is a touch-capable pin on ESP32-S3
- The pin number (7) directly corresponds to touch channel number (7)
- No `gpio_config()` call is needed for touch sensors
- The touch driver handles all GPIO setup internally

---

## Software Dependencies

### Component Dependencies
The touch sensor implementation requires the following ESP-IDF managed components:

**File**: `main/idf_component.yml`

```yaml
dependencies:
  espressif/touch_button_sensor: "*"
  espressif/touch_sensor_lowlevel: "*"
  espressif/touch_sensor_fsm: "*"  # Auto-dependency, automatically included
```

**Installation:**
```bash
idf.py reconfigure  # Downloads managed components
```

### Header Files Required

**File**: `main/boards/EchoEar/EchoEar.cc`

```cpp
#include <touch_sensor_lowlevel.h>  // Low-level touch sensor driver
#include <touch_button_sensor.h>    // Touch button sensor API
```

**Purpose:**
- `touch_sensor_lowlevel.h`: Provides low-level touch sensor driver initialization and control
- `touch_button_sensor.h`: Provides high-level touch button API with callbacks and state management

---

## Configuration Parameters

### Touch Channel Definition

**File**: `main/boards/EchoEar/config.h`

```cpp
// Touch channel definition (GPIO7 = Touch Channel 7 on ESP32-S3)
#define TOUCH_CHANNEL_1  (7)
```

**Explanation:**
- Defines which touch channel to use (7 = GPIO7)
- Used throughout the code to reference the touch sensor
- This is a hardware-level mapping, not configurable

### Sensitivity Threshold Configuration

**File**: `main/boards/EchoEar/config.h`

```cpp
// Touch threshold definitions
// Lower values = more sensitive (can detect through thicker materials)
// Higher values = less sensitive (requires closer/firmer touch)
#define LIGHT_TOUCH_THRESHOLD  (0.05)  // 0.0 - 1.0 range (lower = more sensitive)
```

**Threshold Range:**
- **Valid Range**: 0.0 to 1.0 (normalized value)
- **Current Setting**: 0.05 (balanced sensitivity)
- **Lower Values** (0.01-0.03): Very sensitive, detects light touches, may false trigger
- **Mid Values** (0.04-0.06): Balanced sensitivity, good for most applications
- **Higher Values** (0.08-1.0): Less sensitive, requires firm touch, fewer false triggers

**Recommended Values:**
- Through thick materials (1-2mm): `0.02` to `0.03`
- Normal operation: `0.05` (current default)
- Noisy environment: `0.08` to `0.1`

---

## Low-Level Touch Sensor Setup

### Step 1: Channel Configuration

```cpp
// Location: main/boards/EchoEar/EchoEar.cc - InitializeTouchButton()

// Step 1: Prepare channel configuration
uint32_t touch_channel_list[] = {TOUCH_CHANNEL_1};  // GPIO7 = Channel 7

touch_lowlevel_type_t *channel_type = (touch_lowlevel_type_t*)calloc(1, sizeof(touch_lowlevel_type_t));
if (channel_type == NULL) {
    ESP_LOGE(TAG, "[TOUCH] ERROR: Memory allocation failed for channel_type");
    return;
}
channel_type[0] = TOUCH_LOWLEVEL_TYPE_TOUCH;
```

**Details:**
- `touch_channel_list[]`: Array of touch channels to initialize (only channel 7 in our case)
- `channel_type[]`: Array specifying the type for each channel (TOUCH_LOWLEVEL_TYPE_TOUCH)
- Memory is dynamically allocated and freed after use

### Step 2: Low-Level Touch Sensor Creation

```cpp
// Step 2: Create low-level touch sensor
touch_lowlevel_config_t low_config = {
    .channel_num = 1,                      // Number of channels (1 channel)
    .channel_list = touch_channel_list,    // Pointer to channel array
    .channel_type = channel_type,          // Pointer to channel type array
};

esp_err_t ret = touch_sensor_lowlevel_create(&low_config);
free(channel_type);  // Free memory after use

if (ret != ESP_OK) {
    ESP_LOGE(TAG, "[TOUCH] ERROR: Touch sensor lowlevel create failed: %d (%s)", ret, esp_err_to_name(ret));
    return;
}
ESP_LOGI(TAG, "[TOUCH] ✓ Low-level touch sensor created successfully");
```

**Configuration Structure:**
- `channel_num`: Number of touch channels (1 in our case)
- `channel_list`: Pointer to array of channel numbers
- `channel_type`: Pointer to array of channel types

**What This Does:**
- Initializes the ESP32-S3 touch sensor peripheral
- Configures GPIO7 hardware for capacitive sensing
- Sets up the touch sensor driver at the hardware level
- Must be created before creating touch button sensor

**Error Handling:**
- Returns `ESP_OK` on success
- Returns error code on failure (check logs for specific error)

---

## Touch Button Sensor Configuration

### Step 3: Touch Button Configuration

```cpp
// Step 3: Configure touch button sensor
float channel_threshold[] = {LIGHT_TOUCH_THRESHOLD};

touch_button_config_t touch_cfg = {
    .channel_num = 1,                      // Number of channels
    .channel_list = touch_channel_list,    // Channel numbers (reuse from Step 1)
    .channel_threshold = channel_threshold, // Sensitivity thresholds
    .channel_gold_value = NULL,            // Optional: calibration values (NULL = auto-calibrate)
    .debounce_times = 1,                   // Number of consecutive readings to confirm state
    .skip_lowlevel_init = true,            // true = we already created low-level sensor
};
```

**Configuration Parameters:**

1. **channel_num**: Number of touch channels (1)
2. **channel_list**: Array of channel numbers to use
3. **channel_threshold**: Array of threshold values (one per channel)
   - Current: `{0.05}` (from `LIGHT_TOUCH_THRESHOLD`)
4. **channel_gold_value**: Optional calibration values
   - `NULL` = automatic calibration on first run
   - Can be set to pre-calibrated values for consistent behavior
5. **debounce_times**: Debounce filter value
   - `1` = minimal debounce (fastest response, may be noisier)
   - `2-3` = more stable (recommended for noisy environments)
   - Higher = more stable but slower response
6. **skip_lowlevel_init**: Critical flag
   - `true` = don't create low-level sensor (we already did in Step 2)
   - `false` = create low-level sensor (would fail if already created)

### Step 4: Touch Button Sensor Creation with Callback

```cpp
// Step 4: Create touch button sensor with callback
touch_event_count_ = 0;  // Initialize event counter

ret = touch_button_sensor_create(&touch_cfg, &touch_button_handle_, 
                                 [](touch_button_handle_t handle, uint32_t channel, touch_state_t state, void *cb_arg) {
                                     // Lightweight callback - increment counter and log
                                     EspS3Cat::touch_event_count_++;  // Increment on any touch event
                                     
                                     if (state == TOUCH_STATE_ACTIVE) {
                                         ESP_LOGI(TAG, "[TOUCH] Touch button pressed (channel %lu)", (unsigned long)channel);
                                     } else {
                                         ESP_LOGI(TAG, "[TOUCH] Touch button released (channel %lu)", (unsigned long)channel);
                                     }
                                 }, this);
```

**Callback Function Signature:**
```cpp
void callback(touch_button_handle_t handle, uint32_t channel, touch_state_t state, void *cb_arg)
```

**Callback Parameters:**
- `handle`: Touch button sensor handle
- `channel`: Touch channel number (7 in our case)
- `state`: Touch state enum
  - `TOUCH_STATE_ACTIVE`: Touch detected (pressed)
  - `TOUCH_STATE_INACTIVE`: No touch (released)
- `cb_arg`: User data pointer (passed as `this` in our implementation)

**Important Notes:**
- Callback runs in interrupt context - keep it lightweight!
- Only increment counter and log - no heavy operations
- Heavy operations should be done in a task (see Event Handling section)

**Return Value:**
- Returns `ESP_OK` on success
- Returns error code on failure

---

## Event Handling & Callbacks

### Event Processing Task

The touch button sensor requires periodic event processing:

```cpp
// Location: main/boards/EchoEar/EchoEar.cc - touch_button_event_task()

static void touch_button_event_task(void* arg)
{
    EspS3Cat* board = static_cast<EspS3Cat*>(arg);
    if (board == nullptr) {
        ESP_LOGE(TAG, "[TOUCH] ERROR: Invalid board pointer in touch_button_event_task");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "[TOUCH] touch_button_event_task handle=%p", (void*)board->touch_button_handle_);
    ESP_LOGI(TAG, "[TOUCH] touch_button_event_task entering main loop");
    while (true) {
        if (board->touch_button_handle_ != nullptr) {
            touch_button_sensor_handle_events(board->touch_button_handle_);
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // Check every 10ms
    }
}
```

**Task Details:**
- **Task Name**: `touch_btn_task`
- **Stack Size**: 4KB (4096 bytes)
- **Priority**: 5 (higher priority for responsive touch handling)
- **Core**: Pinned to Core 1
- **Polling Interval**: 10ms (100Hz polling rate)

**What `touch_button_sensor_handle_events()` Does:**
- Processes touch sensor readings from hardware
- Compares readings against threshold
- Detects state changes (active/inactive)
- Triggers callback function when state changes
- Must be called periodically (every 10ms recommended)

### Task Creation

```cpp
// Create task to handle touch button sensor events
BaseType_t task_ret2 = xTaskCreatePinnedToCore(
    touch_button_event_task,    // Task function
    "touch_btn_task",           // Task name
    4 * 1024,                   // Stack size (4KB)
    this,                       // Parameter passed to task
    5,                          // Priority (5 = high priority)
    NULL,                       // Task handle (not needed)
    1                           // Core ID (pin to core 1)
);
```

---

## Logging & Monitoring

### Event Counter

A static volatile counter tracks all touch events:

```cpp
// Member variable in EspS3Cat class
static volatile uint32_t touch_event_count_;  // Counter for touch events

// Static member definition (outside class)
volatile uint32_t EspS3Cat::touch_event_count_ = 0;
```

**Usage:**
- Incremented in callback on every touch event (both press and release)
- Used for debugging and monitoring
- `volatile` ensures compiler doesn't optimize it away

### Periodic Logging Task

A dedicated task logs touch sensor status every second:

```cpp
// Location: main/boards/EchoEar/EchoEar.cc - touch_log_task()

static void touch_log_task(void* arg)
{
    ESP_LOGI(TAG, "[TOUCH] touch_log_task started");
    EspS3Cat* board = static_cast<EspS3Cat*>(arg);
    if (board == nullptr) {
        ESP_LOGE(TAG, "[TOUCH] ERROR: Invalid board pointer in touch_log_task");
        vTaskDelete(NULL);
        return;
    }

    uint32_t last_count = 0;
    ESP_LOGI(TAG, "[TOUCH] touch_log_task entering main loop");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // Wait 1 second
        
        uint32_t current_count = EspS3Cat::touch_event_count_;
        if (current_count > last_count) {
            ESP_LOGI(TAG, "[TOUCH] Touch sensor status: Events detected! Total events: %lu (GPIO7)", 
                     (unsigned long)current_count);
            last_count = current_count;
        } else {
            ESP_LOGI(TAG, "[TOUCH] Touch sensor status: No new events (GPIO7), total=%lu", 
                     (unsigned long)current_count);
        }
    }
}
```

**Task Details:**
- **Task Name**: `touch_log_task`
- **Stack Size**: 2KB (2048 bytes)
- **Priority**: 1 (low priority, background monitoring)
- **Core**: Pinned to Core 1
- **Logging Interval**: 1 second

**Log Output Examples:**

**When Touch Events Detected:**
```
[TOUCH] Touch sensor status: Events detected! Total events: 5 (GPIO7)
```

**When No Touch Events:**
```
[TOUCH] Touch sensor status: No new events (GPIO7), total=5
```

**Initialization Logs:**
```
[TOUCH] ===== Starting touch button initialization =====
[TOUCH] GPIO7 -> Touch Channel 7
[TOUCH] Threshold: 0.050
[TOUCH] Step 1: Preparing channel configuration
[TOUCH] Channel list: [7]
[TOUCH] Channel type allocated and set to TOUCH
[TOUCH] Step 2: Creating low-level touch sensor
[TOUCH] Low-level config: channel_num=1
[TOUCH] touch_sensor_lowlevel_create() returned: 0 (ESP_OK)
[TOUCH] ✓ Low-level touch sensor created successfully
[TOUCH] Step 3: Configuring touch button sensor
[TOUCH] Touch button config: channel_num=1, threshold=0.050, debounce=1, skip_lowlevel_init=1
[TOUCH] Step 4: Creating touch button sensor with callback
[TOUCH] Touch event counter initialized to 0
[TOUCH] touch_button_sensor_create() returned: 0 (ESP_OK)
[TOUCH] ✓ Touch button sensor created successfully, handle=0x3fc9xxxx
[TOUCH] Creating periodic logging task
[TOUCH] touch_log_task creation result: 1
[TOUCH] ✓ touch_log_task created successfully
[TOUCH] Creating touch button event handling task
[TOUCH] touch_button_event_task creation result: 1
[TOUCH] ✓ touch_button_event_task created successfully
[TOUCH] Step 5: Starting touch sensor
[TOUCH] touch_sensor_lowlevel_start() returned: 0 (ESP_OK)
[TOUCH] ===== Touch button initialization complete =====
[TOUCH] Touch sensor is now active and monitoring GPIO7
```

**Runtime Touch Event Logs:**
```
[TOUCH] Touch button pressed (channel 7)
[TOUCH] Touch button released (channel 7)
```

### Logging Levels

All touch sensor logs use the `[TOUCH]` prefix for easy filtering:

**To filter touch logs:**
```bash
idf.py monitor | grep TOUCH
```

**Log Levels:**
- `ESP_LOGI`: Normal operation, initialization, status updates
- `ESP_LOGE`: Errors (initialization failures, null pointers, etc.)

---

## Sensitivity Tuning

### Understanding Threshold Values

The threshold value (`LIGHT_TOUCH_THRESHOLD`) determines how sensitive the touch sensor is:

**How It Works:**
- Touch sensor measures capacitance on GPIO7
- When touched, capacitance increases
- Threshold is a normalized value (0.0-1.0) that determines the trigger point
- Lower threshold = triggers with smaller capacitance change (more sensitive)
- Higher threshold = requires larger capacitance change (less sensitive)

### Tuning Process

**Step 1: Start with Default**
```cpp
#define LIGHT_TOUCH_THRESHOLD  (0.05)  // Default balanced sensitivity
```

**Step 2: Test Touch Detection**
- Flash firmware with default threshold
- Monitor logs: `idf.py monitor | grep TOUCH`
- Touch the copper plate
- Check if touch events are detected

**Step 3: Adjust Based on Results**

**If Touch Not Detected (Too Insensitive):**
- Lower the threshold: `0.03` or `0.02`
- Rebuild and test: `idf.py build flash monitor`
- Continue lowering until touch is detected

**If False Triggers (Too Sensitive):**
- Raise the threshold: `0.08` or `0.1`
- Rebuild and test
- Continue raising until false triggers stop

**If Touch Works But Needs Fine-Tuning:**
- Adjust in small increments: `0.04`, `0.05`, `0.06`
- Test each value
- Find the sweet spot where touch works reliably without false triggers

### Recommended Threshold Values by Use Case

| Use Case | Threshold | Notes |
|----------|-----------|-------|
| Direct touch (finger on copper) | 0.05 - 0.06 | Default, balanced |
| Through thin material (0.5mm) | 0.03 - 0.04 | More sensitive |
| Through thick material (1-2mm) | 0.02 - 0.03 | Very sensitive |
| Noisy environment | 0.08 - 0.1 | Less sensitive, fewer false triggers |
| Very noisy environment | 0.15 - 0.2 | Requires firm touch |

### Debounce Tuning

If you experience bouncing (rapid on/off events), increase debounce:

```cpp
touch_button_config_t touch_cfg = {
    // ... other config ...
    .debounce_times = 2,  // Increase from 1 to 2 or 3
};
```

**Debounce Values:**
- `1`: Minimal debounce (current default, fastest response)
- `2`: Moderate debounce (recommended if you see bouncing)
- `3`: Strong debounce (very stable, slower response)

---

## Code Flow & Architecture

### Initialization Flow

```
1. Constructor Called (EspS3Cat::EspS3Cat())
   │
   ├─> InitializeTouchButton()  [CURRENTLY COMMENTED OUT]
       │
       ├─> Step 1: Prepare channel configuration
       │   ├─> Create touch_channel_list[] = {7}
       │   └─> Allocate channel_type[] = {TOUCH_LOWLEVEL_TYPE_TOUCH}
       │
       ├─> Step 2: Create low-level touch sensor
       │   ├─> Configure touch_lowlevel_config_t
       │   ├─> Call touch_sensor_lowlevel_create()
       │   └─> Free channel_type memory
       │
       ├─> Step 3: Configure touch button sensor
       │   ├─> Set threshold array = {LIGHT_TOUCH_THRESHOLD}
       │   └─> Configure touch_button_config_t
       │
       ├─> Step 4: Create touch button sensor
       │   ├─> Register callback function
       │   └─> Call touch_button_sensor_create()
       │
       ├─> Create touch_log_task (background monitoring)
       │
       ├─> Create touch_button_event_task (event processing)
       │
       └─> Step 5: Start touch sensor
           └─> Call touch_sensor_lowlevel_start()
```

### Runtime Flow

```
Hardware (GPIO7) 
    │
    ├─> Capacitance changes when touched
    │
    └─> ESP32-S3 Touch Peripheral
            │
            └─> touch_button_event_task (runs every 10ms)
                    │
                    ├─> touch_button_sensor_handle_events()
                    │       │
                    │       ├─> Read capacitance value
                    │       ├─> Compare to threshold
                    │       ├─> Detect state change
                    │       └─> Trigger callback if state changed
                    │
                    └─> Callback Function (lightweight)
                            │
                            ├─> Increment touch_event_count_
                            └─> Log touch state (pressed/released)

touch_log_task (runs every 1 second)
    │
    └─> Read touch_event_count_
        └─> Log status (events detected or no new events)
```

### Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    Hardware Layer                            │
│  ┌──────────────┐         ┌──────────────────────┐         │
│  │   GPIO7      │────────▶│  ESP32-S3 Touch      │         │
│  │  (Copper     │  Wire   │  Peripheral          │         │
│  │   Layer)     │         │  (Channel 7)         │         │
│  └──────────────┘         └──────────────────────┘         │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│              Low-Level Touch Sensor Driver                   │
│  touch_sensor_lowlevel_create()                             │
│  touch_sensor_lowlevel_start()                              │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│              Touch Button Sensor API                         │
│  touch_button_sensor_create()                               │
│  touch_button_sensor_handle_events()                        │
│                    │                                        │
│                    ▼                                        │
│         Callback Function (lightweight)                     │
│         - Increment counter                                 │
│         - Log touch state                                   │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    FreeRTOS Tasks                            │
│  ┌─────────────────────────┐  ┌──────────────────────────┐ │
│  │ touch_button_event_task │  │ touch_log_task           │ │
│  │ (Priority 5, Core 1)    │  │ (Priority 1, Core 1)     │ │
│  │ Runs every 10ms         │  │ Runs every 1s            │ │
│  │ - Process events        │  │ - Log status             │ │
│  └─────────────────────────┘  └──────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## API Reference

### Low-Level Touch Sensor API

#### `touch_sensor_lowlevel_create()`

**Prototype:**
```cpp
esp_err_t touch_sensor_lowlevel_create(const touch_lowlevel_config_t *config);
```

**Parameters:**
- `config`: Pointer to low-level configuration structure

**Returns:**
- `ESP_OK`: Success
- Error code: Failure

**Configuration Structure:**
```cpp
typedef struct {
    uint32_t channel_num;              // Number of channels
    const uint32_t *channel_list;      // Array of channel numbers
    const touch_lowlevel_type_t *channel_type;  // Array of channel types
} touch_lowlevel_config_t;
```

#### `touch_sensor_lowlevel_start()`

**Prototype:**
```cpp
esp_err_t touch_sensor_lowlevel_start(void);
```

**Description:**
- Starts the touch sensor peripheral
- Must be called after creating touch button sensor
- No parameters needed (uses global low-level sensor)

**Returns:**
- `ESP_OK`: Success
- Error code: Failure

### Touch Button Sensor API

#### `touch_button_sensor_create()`

**Prototype:**
```cpp
esp_err_t touch_button_sensor_create(
    const touch_button_config_t *config,
    touch_button_handle_t *handle,
    touch_button_callback_t callback,
    void *cb_arg
);
```

**Parameters:**
- `config`: Pointer to touch button configuration
- `handle`: Output parameter for sensor handle
- `callback`: Callback function for touch events
- `cb_arg`: User data passed to callback

**Callback Type:**
```cpp
typedef void (*touch_button_callback_t)(
    touch_button_handle_t handle,
    uint32_t channel,
    touch_state_t state,
    void *cb_arg
);
```

**Touch States:**
- `TOUCH_STATE_ACTIVE`: Touch detected
- `TOUCH_STATE_INACTIVE`: No touch

**Returns:**
- `ESP_OK`: Success
- Error code: Failure

#### `touch_button_sensor_handle_events()`

**Prototype:**
```cpp
esp_err_t touch_button_sensor_handle_events(touch_button_handle_t handle);
```

**Parameters:**
- `handle`: Touch button sensor handle

**Description:**
- Must be called periodically (recommended: every 10ms)
- Processes touch sensor readings
- Triggers callbacks on state changes
- Should be called from a FreeRTOS task

**Returns:**
- `ESP_OK`: Success
- Error code: Failure

**Configuration Structure:**
```cpp
typedef struct {
    uint32_t channel_num;                    // Number of channels
    const uint32_t *channel_list;            // Array of channel numbers
    const float *channel_threshold;          // Array of thresholds (0.0-1.0)
    const float *channel_gold_value;         // Optional calibration values (NULL = auto)
    uint8_t debounce_times;                  // Debounce filter (1-10)
    bool skip_lowlevel_init;                 // true if low-level already created
} touch_button_config_t;
```

---

## Troubleshooting

### Touch Sensor Not Detecting Touches

**Symptoms:**
- No touch events in logs
- `touch_event_count_` never increments
- No "Touch button pressed" messages

**Solutions:**

1. **Check Initialization:**
   - Verify `InitializeTouchButton()` is called (currently commented out in constructor)
   - Uncomment lines 718-720 in `EchoEar.cc`:
   ```cpp
   InitializeTouchButton();
   ```

2. **Check Hardware Connection:**
   - Verify copper layer is connected to GPIO7
   - Check wire connection (dupont wire)
   - Ensure good contact

3. **Lower Threshold:**
   ```cpp
   #define LIGHT_TOUCH_THRESHOLD  (0.02)  // Try lower value
   ```

4. **Check Logs for Errors:**
   ```bash
   idf.py monitor | grep -i "touch\|error"
   ```

5. **Verify Dependencies:**
   ```bash
   idf.py reconfigure  # Re-download components
   ```

### False Triggers (Touch Detected When Not Touched)

**Symptoms:**
- Touch events appear in logs without touching
- `touch_event_count_` increments randomly

**Solutions:**

1. **Increase Threshold:**
   ```cpp
   #define LIGHT_TOUCH_THRESHOLD  (0.1)  // Try higher value
   ```

2. **Increase Debounce:**
   ```cpp
   .debounce_times = 3,  // Increase from 1 to 3
   ```

3. **Check for Electrical Noise:**
   - Ensure copper layer is not near other electrical signals
   - Add shielding if needed
   - Check power supply stability

### Initialization Failures

**Symptoms:**
- Error logs: "Touch sensor lowlevel create failed"
- Error logs: "Create touch button sensor failed"
- Error logs: "Touch sensor start failed"

**Solutions:**

1. **Check Error Codes:**
   - Logs show error code and description
   - Common errors:
     - `ESP_ERR_INVALID_ARG`: Invalid configuration
     - `ESP_ERR_NO_MEM`: Out of memory
     - `ESP_ERR_INVALID_STATE`: Already initialized

2. **Verify Configuration:**
   - Check `TOUCH_CHANNEL_1` is valid (7 for GPIO7)
   - Verify threshold is in range (0.0-1.0)
   - Ensure `skip_lowlevel_init` is `true` if low-level already created

3. **Memory Issues:**
   - Check available heap memory
   - Reduce stack sizes if needed (though current sizes are reasonable)

### Stack Overflow

**Symptoms:**
- Error: "***ERROR*** A stack overflow in task touch_btn_task"

**Solutions:**

1. **Increase Stack Size:**
   ```cpp
   xTaskCreatePinnedToCore(touch_button_event_task, "touch_btn_task", 
                           6 * 1024,  // Increase from 4KB to 6KB
                           this, 5, NULL, 1);
   ```

2. **Verify Callback is Lightweight:**
   - Callback should only increment counter and log
   - No heavy operations (malloc, GetInstance(), etc.)
   - Heavy operations should be in task, not callback

### Touch Events Not Logged

**Symptoms:**
- Hardware detects touch (verified with scope/meter)
- No logs appear
- `touch_event_count_` doesn't increment

**Solutions:**

1. **Verify Event Task is Running:**
   - Check logs for "touch_button_event_task started"
   - Ensure task was created successfully

2. **Check Task Priority:**
   - Ensure `touch_button_event_task` has sufficient priority (5 is good)
   - Lower priority tasks may be starved

3. **Verify Handle is Not Null:**
   - Check logs for "touch_button_event_task handle=0x..."
   - Handle should be non-zero
   - If NULL, sensor creation failed

### Build Errors

**Error: `touch_sensor_lowlevel.h: No such file or directory`**

**Solution:**
```bash
idf.py reconfigure  # Download managed components
```

**Error: Conflicting declarations**

**Solution:**
- Only include `touch_sensor_lowlevel.h` and `touch_button_sensor.h`
- Don't include `touch_button.h` or `<touch_element/touch_button.h>`

---

## Summary

### Key Points

1. **No GPIO Configuration Needed**: ESP-IDF touch driver handles GPIO setup automatically
2. **Hardware Mapping**: GPIO7 maps directly to Touch Channel 7 (fixed by ESP32-S3 hardware)
3. **Two-Layer Architecture**: Low-level driver + High-level button API
4. **Sensitivity Tuning**: Threshold value (0.0-1.0) controls sensitivity
5. **Event Processing**: Requires periodic calls to `touch_button_sensor_handle_events()`
6. **Lightweight Callbacks**: Keep callbacks fast, do heavy work in tasks
7. **Current Status**: Implementation is complete but initialization is commented out (lines 718-720)

### Quick Enable Checklist

To enable the touch sensor (currently disabled):

1. ✅ Dependencies already added to `idf_component.yml`
2. ✅ Configuration already in `config.h`
3. ✅ Code already implemented in `EchoEar.cc`
4. ⚠️ **Uncomment initialization call** in constructor (lines 718-720):
   ```cpp
   InitializeTouchButton();
   ```
5. ✅ Rebuild and flash: `idf.py build flash monitor`

### Files Involved

1. **Configuration**: `main/boards/EchoEar/config.h`
   - `TOUCH_CHANNEL_1` definition
   - `LIGHT_TOUCH_THRESHOLD` definition

2. **Implementation**: `main/boards/EchoEar/EchoEar.cc`
   - Headers: `touch_sensor_lowlevel.h`, `touch_button_sensor.h`
   - Member variables: `touch_button_handle_`, `touch_event_count_`
   - Methods: `InitializeTouchButton()`, task functions
   - Constructor: InitializeTouchButton() call (currently commented)

3. **Dependencies**: `main/idf_component.yml`
   - `espressif/touch_button_sensor`
   - `espressif/touch_sensor_lowlevel`

---

**Document Version**: 1.0  
**Last Updated**: Based on current codebase state  
**Board**: EchoEar (ESP32-S3)  
**GPIO**: GPIO7 (Touch Channel 7)

