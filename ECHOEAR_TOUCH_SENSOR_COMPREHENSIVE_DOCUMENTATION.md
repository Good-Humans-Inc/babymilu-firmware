# EchoEar Touch Documentation

EchoEar has two touch-related systems. Keep them distinct when editing code or
docs.

## GPIO7 Capacitive Touch Button

Primary files:

- `main/boards/echoear/echoear.cc`
- `main/boards/echoear/config.h`
- `main/boards/echoear/touch.h`

Configuration:

- Touch channel: GPIO7 / touch channel 7.
- Threshold: `LIGHT_TOUCH_THRESHOLD` is `0.01`.
- Component: `touch_button_sensor`.

Runtime behavior:

1. `InitializeTouchButton()` creates the touch sensor.
2. A low-level event task calls `touch_button_sensor_handle_events()`.
3. Callback work is queued into an app-level queue.
4. `touch_button_app_task` handles the queued app action outside the sensor
   callback.
5. Touch events are ignored while the device is speaking or listening.

The old periodic `touch_log_task` is not created by default.

## CST816S Screen Touch

The round LCD touch controller is CST816S:

- I2C address: `0x15`
- IRQ: GPIO10
- Reset pin: not connected

Current code probes and handles CST816S custom gesture/touch data, but it is not
registered as a normal LVGL input driver. Treat LVGL integration as not active
unless the code changes.

## Common Troubleshooting

- If GPIO7 is too sensitive, adjust `LIGHT_TOUCH_THRESHOLD` in
  `main/boards/echoear/config.h`.
- If no GPIO7 events arrive, confirm touch component init succeeds and that the
  device is not currently speaking/listening.
- If CST816S events are missing, check I2C bus init and GPIO10 IRQ wiring.

## Agent Notes

Avoid stale references to `main/boards/EchoEar`. The active path is lowercase:
`main/boards/echoear`.
