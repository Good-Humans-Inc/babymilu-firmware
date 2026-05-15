# EchoEar BMI270 IMU Notes

This document replaces the older gesture-focused guide. EchoEar currently
initializes and polls the BMI270 IMU, but does not feed BMI270 gestures into app
behavior.

## Current Code

- Configuration: `main/boards/echoear/config.h`
- Implementation: `main/boards/echoear/echoear.cc`
- Component: `managed_components/espressif__bmi270_sensor`

## Hardware Configuration

- I2C address: `0x68`
- SDA: GPIO2
- SCL: GPIO1
- Optional interrupt pin: GPIO21

EchoEar creates a shared I2C bus with the `i2c_bus` wrapper because the BMI270
component expects that API.

## Runtime Behavior

At board startup:

1. EchoEar initializes the shared I2C bus.
2. BMI270 is created on the shared bus.
3. Accelerometer and gyroscope are enabled.
4. Accelerometer is configured for 100 Hz and +/-4G.
5. Gyroscope is configured for 100 Hz and +/-2000 dps.
6. A FreeRTOS task polls raw accel/gyro data about once per second.

The raw read task is mainly diagnostic. Verbose per-sample logging is mostly
disabled/commented to avoid log noise.

## Not Current Behavior

- No production gesture classifier is wired to BMI270.
- No wrist gesture or tap gesture events are sent to `Application`.
- Removed board examples such as `esp-spot` are not available in this repo.

## Agent Notes

If adding gesture behavior later, update this document only after the code maps
BMI270 events to app actions.
