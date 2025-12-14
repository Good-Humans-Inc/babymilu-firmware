# Touch Sensor Sensitivity Guide

## What Determines Touch Sensor Sensitivity?

Capacitive touch sensors detect changes in capacitance when a conductive object (like a finger) approaches the sensor pad. Several factors affect sensitivity:

### 1. **Physical Factors**
- **Distance**: The farther the touch point from the sensor, the weaker the signal
- **Material thickness**: Thicker layers reduce sensitivity
- **Dielectric properties**: Different materials (TPU, fabric, plastic) have different dielectric constants
- **Electrode size**: Larger electrodes are generally more sensitive
- **Ground plane**: Proper grounding improves sensitivity

### 2. **Software Configuration Factors**

#### **Threshold Value** (`channel_threshold`)
- **Range**: 0.0 to 1.0 (normalized)
- **Lower values** = More sensitive (detects through thicker materials)
- **Higher values** = Less sensitive (requires closer/firmer touch)
- **Current setting**: `0.05` (reduced from `0.1` for better sensitivity)

#### **Debounce Times** (`debounce_times`)
- Number of consecutive readings needed to confirm state change
- **Lower values** = Faster response, but may be more prone to noise
- **Higher values** = More stable, but slower response
- **Current setting**: `1` (reduced from `2`)

#### **Other Parameters** (in sdkconfig)
- `CONFIG_TOUCH_BUTTON_SENSOR_SMOOTH_COEF_X1000=400` - Smoothing coefficient
- `CONFIG_TOUCH_BUTTON_SENSOR_NOISE_P_SNR=4` - Positive signal-to-noise ratio
- `CONFIG_TOUCH_BUTTON_SENSOR_NOISE_N_SNR=4` - Negative signal-to-noise ratio

## Why It Works Through TPU But Not Through Additional Soft Toy Layer?

1. **Increased Distance**: The additional layer increases the distance between finger and sensor
2. **Dielectric Properties**: The soft toy material may have different dielectric properties than TPU
3. **Signal Attenuation**: Each layer attenuates the capacitive signal
4. **Threshold Too High**: The original threshold (0.1) may be too high for the combined layers

## How to Improve Sensitivity

### Option 1: Lower the Threshold (Easiest)
```cpp
#define LIGHT_TOUCH_THRESHOLD  (0.05)  // Try values: 0.03, 0.05, 0.08
```

**Recommended values:**
- `0.03` - Very sensitive (may have false triggers)
- `0.05` - Good balance (current setting)
- `0.08` - Moderate sensitivity
- `0.1` - Original setting (less sensitive)

### Option 2: Reduce Debounce Times
```cpp
.debounce_times = 1,  // Faster response (current setting)
```

### Option 3: Adjust via menuconfig
Run `idf.py menuconfig` and navigate to:
- `Component config` → `Touch Button Sensor Configuration`

Adjust:
- `Touch Button Sensor Smooth Coefficient` - Lower for faster response
- `Touch Button Sensor Noise SNR` - Lower for more sensitivity (but more noise)

### Option 4: Use Gold Value Calibration
The `channel_gold_value` can be used for calibration. This sets a reference value for the touch channel, which can improve sensitivity:

```cpp
uint32_t channel_gold_value[] = {0};  // Will be auto-calibrated
touch_button_config_t touch_cfg = {
    // ...
    .channel_gold_value = channel_gold_value,  // Enable calibration
    // ...
};
```

## Testing and Tuning

1. **Start with threshold 0.05** (already set)
2. **Test with your setup** (TPU + soft toy layer)
3. **If still not sensitive enough:**
   - Try `0.03` (very sensitive, may have false triggers)
   - Try `0.02` (extremely sensitive, likely false triggers)
4. **If too sensitive** (false triggers):
   - Increase to `0.08` or `0.1`
   - Increase `debounce_times` to `2` or `3`

## Hardware Improvements (If Software Isn't Enough)

1. **Larger electrode**: Make the copper pad larger
2. **Thinner layers**: Reduce material thickness if possible
3. **Better grounding**: Ensure proper ground connection
4. **Shielding**: Add ground plane around the touch pad
5. **Material choice**: Use materials with higher dielectric constants

## Monitoring Touch Values

You can add code to monitor raw touch values to help tune the threshold:

```cpp
// In touch_button_event_task, add:
uint32_t touch_data = 0;
if (touch_button_sensor_get_data(board->touch_button_handle_, TOUCH_CHANNEL_1, 0, &touch_data) == ESP_OK) {
    ESP_LOGI(TAG, "Touch raw data: %lu", (unsigned long)touch_data);
}
```

This helps you understand what threshold value works best for your setup.

## Current Configuration Summary

- **Threshold**: `0.05` (reduced from `0.1`)
- **Debounce**: `1` (reduced from `2`)
- **Smoothing**: `400/1000 = 0.4` (from sdkconfig)
- **Noise SNR**: `4` (from sdkconfig)

Try the current settings first. If it still doesn't work through the soft toy layer, try lowering the threshold to `0.03` or even `0.02`.
