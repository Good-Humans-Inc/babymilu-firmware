# Touch Sensor Menuconfig Configuration Guide

## Current Status (Already Configured)

Based on your `sdkconfig`, the following are **already enabled**:

✅ **SOC Touch Sensor Support** (Hardware level - cannot be changed)
- `CONFIG_SOC_TOUCH_SENSOR_SUPPORTED=y` - Touch sensor is supported on ESP32-S3
- `CONFIG_SOC_TOUCH_SENSOR_NUM=15` - 15 touch channels available
- `CONFIG_SOC_PM_SUPPORT_TOUCH_SENSOR_WAKEUP=y` - Touch wakeup supported

✅ **Touch Button Sensor Configuration** (Already configured with defaults)
- All touch button sensor parameters are set with default values
- Located in menuconfig under: `Touch Button Sensor Configuration`

✅ **Touch Sensor Lowlevel** (Already enabled)
- `CONFIG_ENABLE_TOUCH_SUPPRESS_DEPRECATE_WARN=y`

## Menuconfig Navigation Paths

### 1. ESP-Driver: Touch Sensor Configurations (Optional - for debugging/performance)

**Path:** `Component config` → `ESP-Driver: Touch Sensor Configurations`

**Options to consider enabling:**

- **`[*] Place Touch Sensor Control Functions into IRAM`** (`CONFIG_TOUCH_CTRL_FUNC_IN_IRAM`)
  - ✅ **Recommended for better performance**
  - Makes touch sensor functions IRAM-safe (executable when flash cache is disabled)
  - Improves driver performance

- **`[*] Touch Sensor ISR IRAM-Safe`** (`CONFIG_TOUCH_ISR_IRAM_SAFE`)
  - ✅ **Recommended if using touch wakeup**
  - Ensures interrupt handler works during flash operations
  - Useful for low-power scenarios

- **`[*] Enable Debug Log`** (`CONFIG_TOUCH_ENABLE_DEBUG_LOG`)
  - ⚠️ **Optional - for debugging only**
  - Enables debug logs for touch driver
  - Disable in production to reduce log noise

- **`[ ] Skip the FSM Check`** (`CONFIG_TOUCH_SKIP_FSM_CHECK`)
  - ❌ **NOT recommended** - Keep disabled
  - Only for runtime reconfiguration during tuning
  - Can cause false triggering in production

### 2. Touch Button Sensor Configuration (Already configured)

**Path:** `Component config` → `Touch Button Sensor Configuration`

**Current settings (defaults are fine):**
- Calibration times: 20
- Debounce inactive: 2
- Smooth coefficient: 400
- Baseline coefficient: 100
- Noise SNR: 4

**Optional for debugging:**
- **`[*] Enable Touch Button Sensor Debug`** (`CONFIG_TOUCH_BUTTON_SENSOR_DEBUG`)
  - Only enable if you need detailed debug logs

### 3. Touch Sensor Lowlevel (Already enabled)

**Path:** `Component config` → `Touch sensor lowlevel`

**Current status:** Already configured correctly

## Recommended Configuration Steps

### Minimum Required (Already Done)
✅ SOC touch sensor support is enabled (hardware level)
✅ Touch button sensor is configured
✅ Touch sensor lowlevel is enabled

### Recommended Optimizations

1. **Open menuconfig:**
   ```bash
   idf.py menuconfig
   ```

2. **Navigate to:** `Component config` → `ESP-Driver: Touch Sensor Configurations`

3. **Enable these options:**
   - `[*] Place Touch Sensor Control Functions into IRAM`
   - `[*] Touch Sensor ISR IRAM-Safe`

4. **Optional (for debugging):**
   - `[*] Enable Debug Log` (disable after testing)

5. **Save and exit** (press `S` then `Q`)

6. **Rebuild:**
   ```bash
   idf.py build
   ```

## Verification Checklist

Before building, verify:

- [x] SOC touch sensor is supported (hardware - always true for ESP32-S3)
- [x] Touch button sensor configuration exists (already present)
- [x] Touch sensor lowlevel is enabled (already present)
- [ ] (Optional) IRAM-safe options enabled for better performance
- [ ] (Optional) Debug log enabled if needed for troubleshooting

## Notes

- The **Touch Element** component mentioned in some guides is for ESP-IDF v6.0+. 
- For ESP-IDF v5.5 (your version), use the **ESP-Driver: Touch Sensor** configurations instead.
- The managed components (`touch_button_sensor`, `touch_sensor_lowlevel`) are already added to `idf_component.yml`.
- All required configurations are already present - you only need to enable optional performance optimizations if desired.

## Troubleshooting

If you still get compilation errors after following this guide:

1. **Update managed components:**
   ```bash
   idf.py reconfigure
   ```

2. **Check if components are downloaded:**
   ```bash
   ls managed_components/espressif__touch_button_sensor
   ls managed_components/espressif__touch_sensor_lowlevel
   ```

3. **If components are missing, manually add them:**
   ```bash
   idf.py add-dependency "espressif/touch_button_sensor=*"
   idf.py add-dependency "espressif/touch_sensor_lowlevel=*"
   ```
