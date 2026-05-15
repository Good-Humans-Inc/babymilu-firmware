# GPIO7 Touch Sensor Guide

GPIO7 capacitive touch is already implemented for EchoEar.

## Current Implementation

- Board file: `main/boards/echoear/echoear.cc`
- Config file: `main/boards/echoear/config.h`
- Touch channel: `TOUCH_CHANNEL_1` = `7`
- Threshold: `LIGHT_TOUCH_THRESHOLD` = `0.01`

`InitializeTouchButton()` creates a `touch_button_sensor` handle and two tasks:

- a sensor event task that pumps low-level touch events;
- an app task that receives queued app-level touch actions.

The app-level task exists so the sensor callback does not directly manipulate
application state.

## Behavior

- Touch activity is ignored while the device is speaking.
- Touch activity is ignored while the device is listening.
- Valid touch events are processed asynchronously through a FreeRTOS queue.
- The old touch debug log task is not started by default.

## When To Edit

Only change this area when you need a different GPIO7 gesture/action mapping or
a different threshold. For normal EchoEar builds, no extra enablement is needed.
