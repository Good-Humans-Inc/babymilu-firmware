# BMI270 Gesture Sensor Integration Guide

This document provides comprehensive instructions for integrating the BMI270 gesture/motion sensor (accelerometer and gyroscope) from the EchoEar board implementation into another repository.

## Table of Contents

1. [Overview](#overview)
2. [Requirements and Dependencies](#requirements-and-dependencies)
3. [Hardware Specifications](#hardware-specifications)
4. [Initialization Order](#initialization-order)
5. [Complete Code Implementation](#complete-code-implementation)
6. [Configuration Parameters](#configuration-parameters)
7. [Sensor Reading Task](#sensor-reading-task)
8. [Integration Steps](#integration-steps)
9. [Troubleshooting](#troubleshooting)

---

## Overview

The BMI270 is a low-power, high-performance IMU (Inertial Measurement Unit) that includes:
- 3-axis accelerometer
- 3-axis gyroscope
- Advanced gesture recognition capabilities (not enabled in EchoEar implementation)

The EchoEar implementation uses the BMI270 for motion sensing with:
- Accelerometer: 100Hz, 4G range
- Gyroscope: 100Hz, 2000°/s range
- Data reading: Background task polling every 1 second

---

## Requirements and Dependencies

### ESP-IDF Version
- **Required**: ESP-IDF >= 5.3.0 (for `i2c_bus_get_internal_bus_handle()` support)
- **Recommended**: ESP-IDF >= 5.4.0

### Component Dependencies

Add to `idf_component.yml`:
```yaml
dependencies:
  espressif/bmi270_sensor:
    version: ^0.1.0
    rules:
    - if: target in [esp32s3, esp32c5]
```

**Note**: The BMI270 sensor component is only available for ESP32-S3 and ESP32-C5 targets.

### Required Headers

```cpp
#include "bmi270_api.h"          // BMI270 sensor API
#include "i2c_bus.h"             // I2C bus wrapper (provided by bmi270_sensor component)
#include <driver/i2c_master.h>   // ESP-IDF I2C master driver
#include <freertos/FreeRTOS.h>   // FreeRTOS task support
#include <freertos/task.h>       // FreeRTOS task API
#include <esp_log.h>             // ESP logging
```

---

## Hardware Specifications

### I2C Interface
- **I2C Address**: `0x68` (7-bit address)
- **I2C Speed**: 400kHz (standard mode)
- **I2C Pins**: 
  - SDA: Configurable (EchoEar uses GPIO2)
  - SCL: Configurable (EchoEar uses GPIO1)
  - **Note**: BMI270 can share I2C bus with other devices

### Interrupt Pin (Optional)
- **Interrupt Pin**: GPIO_NUM_21 (defined but not used in EchoEar implementation)
- The interrupt pin is defined but not configured in the basic implementation

### Power Supply
- Standard 3.3V I2C operation
- Low power consumption suitable for battery-powered devices

---

## Initialization Order

**CRITICAL**: The initialization order is important. Follow this exact sequence:

1. **Initialize I2C Bus** (using `i2c_bus` wrapper)
   - Must use `i2c_bus_create()` for BMI270 compatibility
   - Create shared I2C bus handle
   - Extract internal master bus handle for other I2C devices

2. **Initialize Other I2C Devices** (if sharing the bus)
   - Can initialize other devices after I2C bus setup
   - Use the internal master bus handle for traditional I2C devices

3. **Initialize BMI270 Sensor**
   - Must be called after I2C bus initialization
   - Creates sensor handle
   - Enables accelerometer and gyroscope
   - Configures sensor parameters

4. **Start Background Reading Task**
   - Created during BMI270 initialization
   - Runs continuously to read sensor data

### Example Initialization Sequence

```cpp
// In board constructor or initialization function:
InitializeI2c();           // Step 1: Initialize I2C bus
// ... other device initialization ...
InitializeBmi270();        // Step 2: Initialize BMI270 (after I2C)
// ... other initialization ...
```

---

## Complete Code Implementation

### 1. Class Member Variables

Add these to your board class header:

```cpp
class YourBoard : public WifiBoard {
private:
    // I2C bus handles
    i2c_bus_handle_t shared_i2c_bus_handle_ = nullptr;  // For BMI270
    i2c_master_bus_handle_t i2c_bus_ = nullptr;         // For other I2C devices
    
    // BMI270 sensor handle
    bmi270_handle_t bmi270_handle_ = nullptr;
    
    // ... other members ...
};
```

### 2. I2C Bus Initialization

**IMPORTANT**: The BMI270 requires the `i2c_bus` wrapper API, not the standard ESP-IDF I2C master API.

```cpp
void InitializeI2c() {
    // Create shared I2C bus using i2c_bus wrapper (REQUIRED for BMI270)
    // All I2C devices (BMI270, and others) share the same physical bus
    i2c_config_t i2c_bus_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = YOUR_I2C_SDA_PIN,      // e.g., GPIO_NUM_2
        .scl_io_num = YOUR_I2C_SCL_PIN,      // e.g., GPIO_NUM_1
        .sda_pullup_en = true,
        .scl_pullup_en = true,
        .master = {
            .clk_speed = 400000,  // 400kHz
        },
        .clk_flags = 0,
    };
    
    shared_i2c_bus_handle_ = i2c_bus_create(I2C_NUM_0, &i2c_bus_cfg);
    if (!shared_i2c_bus_handle_) {
        ESP_LOGE(TAG, "Failed to create shared I2C bus");
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    // Get the internal master bus handle for use with existing I2cDevice classes
    // This is required if you have other I2C devices using the traditional API
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0) && !CONFIG_I2C_BUS_BACKWARD_CONFIG
    i2c_bus_ = i2c_bus_get_internal_bus_handle(shared_i2c_bus_handle_);
#else
    ESP_LOGE(TAG, "ESP-IDF version does not support i2c_bus_get_internal_bus_handle()");
    ESP_ERROR_CHECK(ESP_FAIL);
#endif
    
    if (!i2c_bus_) {
        ESP_LOGE(TAG, "Failed to obtain master bus handle");
        ESP_ERROR_CHECK(ESP_FAIL);
    }
    
    // Now you can initialize other I2C devices using i2c_bus_ handle
    // InitializeOtherI2cDevices();
}
```

### 3. BMI270 Sensor Initialization

```cpp
void InitializeBmi270() {
    // Validate I2C bus handle
    if (!shared_i2c_bus_handle_) {
        ESP_LOGE(TAG, "Shared I2C bus not initialized");
        return;
    }

    // Create BMI270 sensor using the shared I2C bus wrapper
    // bmi270_config_file is provided by the bmi270_api.h header
    esp_err_t ret = bmi270_sensor_create(
        shared_i2c_bus_handle_, 
        &bmi270_handle_, 
        bmi270_config_file,
        BMI2_GYRO_CROSS_SENS_ENABLE | BMI2_CRT_RTOSK_ENABLE
    );
    
    if (ret != ESP_OK || !bmi270_handle_) {
        ESP_LOGE(TAG, "BMI270 create failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "BMI270 sensor created");

    // Enable accelerometer and gyroscope
    const uint8_t sens_list[] = {BMI2_ACCEL, BMI2_GYRO};
    int8_t rslt = bmi270_sensor_enable(sens_list, 2, bmi270_handle_);
    if (rslt != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to enable BMI270 sensors: %d", rslt);
        return;
    }
    ESP_LOGI(TAG, "BMI270 sensors enabled");

    // Configure accelerometer
    struct bmi2_sens_config accel_config = {.type = BMI2_ACCEL};
    rslt = bmi270_get_sensor_config(&accel_config, 1, bmi270_handle_);
    if (rslt == BMI2_OK) {
        accel_config.cfg.acc.odr = BMI2_ACC_ODR_100HZ;           // 100Hz output data rate
        accel_config.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;         // Normal mode, average 4 samples
        accel_config.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;   // Performance optimized mode
        accel_config.cfg.acc.range = BMI2_ACC_RANGE_4G;          // ±4G range
        rslt = bmi270_set_sensor_config(&accel_config, 1, bmi270_handle_);
        if (rslt != BMI2_OK) {
            ESP_LOGW(TAG, "Failed to configure accelerometer: %d", rslt);
        }
    }

    // Configure gyroscope
    struct bmi2_sens_config gyro_config = {.type = BMI2_GYRO};
    rslt = bmi270_get_sensor_config(&gyro_config, 1, bmi270_handle_);
    if (rslt == BMI2_OK) {
        gyro_config.cfg.gyr.odr = BMI2_GYR_ODR_100HZ;            // 100Hz output data rate
        gyro_config.cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;          // Normal bandwidth mode
        gyro_config.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;    // Performance optimized mode
        gyro_config.cfg.gyr.range = BMI2_GYR_RANGE_2000;         // ±2000°/s range
        rslt = bmi270_set_sensor_config(&gyro_config, 1, bmi270_handle_);
        if (rslt != BMI2_OK) {
            ESP_LOGW(TAG, "Failed to configure gyroscope: %d", rslt);
        }
    }

    // Create task to read BMI270 data
    xTaskCreatePinnedToCore(
        Bmi270ReadTask,      // Task function
        "bmi270_task",       // Task name
        4 * 1024,            // Stack size (4KB)
        this,                // Task parameter (pass board instance)
        5,                   // Task priority
        NULL,                // Task handle (not stored)
        1                    // Core ID (core 1)
    );
}
```

### 4. Sensor Reading Task

```cpp
static void Bmi270ReadTask(void* arg) {
    YourBoard* board = static_cast<YourBoard*>(arg);
    if (!board || !board->bmi270_handle_) {
        ESP_LOGE(TAG, "Invalid BMI270 handle in read task");
        vTaskDelete(NULL);
        return;
    }

    struct bmi2_sens_data sensor_data = {0};

    while (true) {
        // Read sensor data
        int8_t rslt = bmi2_get_sensor_data(&sensor_data, board->bmi270_handle_);
        if (rslt == BMI2_OK) {
            // Access accelerometer data
            int16_t accel_x = sensor_data.acc.x;
            int16_t accel_y = sensor_data.acc.y;
            int16_t accel_z = sensor_data.acc.z;
            
            // Access gyroscope data
            int16_t gyro_x = sensor_data.gyr.x;
            int16_t gyro_y = sensor_data.gyr.y;
            int16_t gyro_z = sensor_data.gyr.z;
            
            // Process data here
            ESP_LOGI(TAG, "BMI270 - Accel: X=%d, Y=%d, Z=%d | Gyro: X=%d, Y=%d, Z=%d",
                     accel_x, accel_y, accel_z,
                     gyro_x, gyro_y, gyro_z);
        } else {
            ESP_LOGW(TAG, "Failed to read BMI270 data: %d", rslt);
        }
        
        // Wait 1 second before next reading
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**Note**: Make the task function `static` and pass `this` as the parameter to access instance members.

---

## Configuration Parameters

### Accelerometer Configuration

| Parameter | Value | Description |
|-----------|-------|-------------|
| ODR | `BMI2_ACC_ODR_100HZ` | Output data rate: 100 samples/second |
| BWP | `BMI2_ACC_NORMAL_AVG4` | Bandwidth parameter: Normal mode, 4-sample average |
| Filter Performance | `BMI2_PERF_OPT_MODE` | Performance optimized mode (lower power) |
| Range | `BMI2_ACC_RANGE_4G` | Measurement range: ±4G |

**Available ODR Options**:
- `BMI2_ACC_ODR_0_78HZ` - 0.78 Hz
- `BMI2_ACC_ODR_1_56HZ` - 1.56 Hz
- `BMI2_ACC_ODR_3_12HZ` - 3.12 Hz
- `BMI2_ACC_ODR_6_25HZ` - 6.25 Hz
- `BMI2_ACC_ODR_12_5HZ` - 12.5 Hz
- `BMI2_ACC_ODR_25HZ` - 25 Hz
- `BMI2_ACC_ODR_50HZ` - 50 Hz
- `BMI2_ACC_ODR_100HZ` - 100 Hz (used)
- `BMI2_ACC_ODR_200HZ` - 200 Hz
- `BMI2_ACC_ODR_400HZ` - 400 Hz
- `BMI2_ACC_ODR_800HZ` - 800 Hz
- `BMI2_ACC_ODR_1600HZ` - 1600 Hz

**Available Range Options**:
- `BMI2_ACC_RANGE_2G` - ±2G
- `BMI2_ACC_RANGE_4G` - ±4G (used)
- `BMI2_ACC_RANGE_8G` - ±8G
- `BMI2_ACC_RANGE_16G` - ±16G

### Gyroscope Configuration

| Parameter | Value | Description |
|-----------|-------|-------------|
| ODR | `BMI2_GYR_ODR_100HZ` | Output data rate: 100 samples/second |
| BWP | `BMI2_GYR_NORMAL_MODE` | Normal bandwidth mode |
| Filter Performance | `BMI2_PERF_OPT_MODE` | Performance optimized mode |
| Range | `BMI2_GYR_RANGE_2000` | Measurement range: ±2000°/s |

**Available ODR Options**:
- `BMI2_GYR_ODR_25HZ` - 25 Hz
- `BMI2_GYR_ODR_50HZ` - 50 Hz
- `BMI2_GYR_ODR_100HZ` - 100 Hz (used)
- `BMI2_GYR_ODR_200HZ` - 200 Hz
- `BMI2_GYR_ODR_400HZ` - 400 Hz
- `BMI2_GYR_ODR_800HZ` - 800 Hz
- `BMI2_GYR_ODR_1600HZ` - 1600 Hz
- `BMI2_GYR_ODR_3200HZ` - 3200 Hz

**Available Range Options**:
- `BMI2_GYR_RANGE_2000` - ±2000°/s (used)
- `BMI2_GYR_RANGE_1000` - ±1000°/s
- `BMI2_GYR_RANGE_500` - ±500°/s
- `BMI2_GYR_RANGE_250` - ±250°/s
- `BMI2_GYR_RANGE_125` - ±125°/s

### Sensor Creation Flags

```cpp
BMI2_GYRO_CROSS_SENS_ENABLE | BMI2_CRT_RTOSK_ENABLE
```

- `BMI2_GYRO_CROSS_SENS_ENABLE`: Enables gyroscope cross-sensitivity compensation
- `BMI2_CRT_RTOSK_ENABLE`: Enables critical real-time OS kernel (RTOSK) features

---

## Integration Steps

### Step 1: Add Component Dependency

Add to `idf_component.yml` in your project root or `main/idf_component.yml`:

```yaml
dependencies:
  espressif/bmi270_sensor:
    version: ^0.1.0
    rules:
    - if: target in [esp32s3, esp32c5]
```

### Step 2: Add Required Headers

Add these includes to your board implementation file:

```cpp
#include "bmi270_api.h"
#include "i2c_bus.h"
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
```

### Step 3: Define Configuration Constants

Add to your board's `config.h` or implementation file:

```cpp
// BMI270 configuration
#define BMI270_I2C_ADDR 0x68
#define BMI270_INT_PIN GPIO_NUM_21  // Optional, not used in basic implementation

// I2C pin definitions (adjust to your hardware)
#define YOUR_I2C_SDA_PIN GPIO_NUM_2
#define YOUR_I2C_SCL_PIN GPIO_NUM_1
```

### Step 4: Add Class Members

Add to your board class:

```cpp
private:
    i2c_bus_handle_t shared_i2c_bus_handle_ = nullptr;
    i2c_master_bus_handle_t i2c_bus_ = nullptr;  // If sharing bus with other devices
    bmi270_handle_t bmi270_handle_ = nullptr;
```

### Step 5: Implement I2C Initialization

Implement `InitializeI2c()` as shown in [Complete Code Implementation](#complete-code-implementation) section.

### Step 6: Implement BMI270 Initialization

Implement `InitializeBmi270()` and `Bmi270ReadTask()` as shown in [Complete Code Implementation](#complete-code-implementation) section.

### Step 7: Call Initialization in Correct Order

In your board constructor or initialization function:

```cpp
YourBoard::YourBoard() {
    InitializeI2c();           // Must be first
    // ... initialize other I2C devices if needed ...
    InitializeBmi270();        // After I2C bus is ready
    // ... other initialization ...
}
```

### Step 8: Process Sensor Data

Modify the `Bmi270ReadTask()` function to process sensor data according to your application needs. The task runs continuously and reads sensor data every 1 second by default.

---

## Troubleshooting

### Issue: "Failed to create shared I2C bus"

**Causes**:
- I2C pins are already in use
- Hardware connection issue
- I2C port conflict

**Solutions**:
- Check that I2C pins are not used by other peripherals
- Verify hardware connections (SDA, SCL, power, ground)
- Ensure I2C_NUM_0 is not already initialized elsewhere

### Issue: "ESP-IDF version does not support i2c_bus_get_internal_bus_handle()"

**Cause**: ESP-IDF version is too old

**Solution**: Upgrade to ESP-IDF >= 5.3.0 (recommended >= 5.4.0)

### Issue: "BMI270 create failed"

**Causes**:
- BMI270 sensor not connected or powered
- Wrong I2C address
- I2C bus initialization failed
- Hardware fault

**Solutions**:
- Verify BMI270 is connected to correct I2C pins
- Check I2C address (should be 0x68)
- Verify power supply (3.3V)
- Use I2C scanner to detect device
- Check I2C bus speed (400kHz recommended)

### Issue: "Failed to enable BMI270 sensors"

**Causes**:
- Sensor initialization incomplete
- Sensor hardware issue
- I2C communication failure

**Solutions**:
- Check sensor creation succeeded
- Verify I2C communication with oscilloscope/logic analyzer
- Try reducing I2C speed to 100kHz for testing

### Issue: Sensor data is zero or incorrect

**Causes**:
- Sensor not properly configured
- Wrong data reading function
- Sensor in wrong mode

**Solutions**:
- Verify sensor configuration succeeded
- Check that `bmi2_get_sensor_data()` is being called correctly
- Ensure sensors are enabled before reading

### Issue: Task crashes or stack overflow

**Causes**:
- Stack size too small
- Task accessing invalid memory

**Solutions**:
- Increase stack size (currently 4KB, try 6KB or 8KB)
- Ensure task parameter (board pointer) is valid
- Add null checks before accessing board members

### Debugging Tips

1. **Enable verbose logging**: Set log level to DEBUG for BMI270 related tags
2. **I2C bus scanner**: Create a simple I2C scanner to verify device presence at address 0x68
3. **Check return codes**: Always check return values from BMI270 API functions
4. **Oscilloscope**: Use oscilloscope to verify I2C communication if hardware issues are suspected
5. **Reduced functionality**: Try initializing with just accelerometer first, then add gyroscope

---

## Additional Notes

### Sharing I2C Bus

The BMI270 can share the I2C bus with other devices. The implementation uses:
- `shared_i2c_bus_handle_` (type: `i2c_bus_handle_t`) for BMI270
- `i2c_bus_` (type: `i2c_master_bus_handle_t`) for other traditional I2C devices

Both handles refer to the same physical I2C bus but use different APIs.

### Power Consumption

The current configuration uses performance-optimized mode which balances power consumption and performance. For lower power consumption, consider:
- Reducing ODR (output data rate)
- Using lower performance filter modes
- Implementing sleep/wake cycles

### Interrupt Support

The basic implementation does not use the interrupt pin. To add interrupt support:
1. Configure GPIO interrupt pin
2. Set up interrupt handler
3. Configure BMI270 interrupt mapping (see esp-spot implementation for wrist gesture interrupt example)

### Advanced Features

The BMI270 supports additional features not enabled in this implementation:
- Wrist gesture detection
- Any motion detection
- Significant motion detection
- Step counter
- Activity recognition

Refer to the BMI270 datasheet and the esp-spot board implementation for examples of these advanced features.

---

## References

- **Source Implementation**: `main/boards/echoear/EchoEar.cc`
- **Alternative Implementation** (with wrist gesture): `main/boards/esp-spot/esp_spot_board.cc`
- **Component**: `espressif/bmi270_sensor` (ESP Component Registry)
- **ESP-IDF I2C Driver**: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2c_master.html

---

## Example Complete Integration

See `main/boards/echoear/EchoEar.cc` lines 403-760 for the complete working implementation.

**Key Files**:
- `main/boards/echoear/EchoEar.cc` - Complete implementation
- `main/boards/echoear/config.h` - Hardware pin configuration
- `main/idf_component.yml` - Component dependencies

---

**Document Version**: 1.0  
**Last Updated**: Based on EchoEar implementation  
**Compatible ESP-IDF**: >= 5.3.0 (recommended >= 5.4.0)  
**Compatible Targets**: ESP32-S3, ESP32-C5

