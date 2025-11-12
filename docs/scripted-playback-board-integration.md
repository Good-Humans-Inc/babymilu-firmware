# Scripted Playback Board Integration

This document explains how to integrate scripted playback into other board configurations.

## Quick Integration

To enable scripted playback on any board, simply replace `app.ToggleChatState()` with `app.HandleBootButtonPress()` in the boot button click handler.

## Example Integration

### Before (Normal Behavior)

```cpp
boot_button_.OnClick([this]() {
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
        ResetWifiConfiguration();
    }
    app.ToggleChatState();  // ← Old way
});
```

### After (With Scripted Playback)

```cpp
boot_button_.OnClick([this]() {
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
        ResetWifiConfiguration();
    }
    app.HandleBootButtonPress();  // ← New way (checks for script first)
});
```

## How It Works

`HandleBootButtonPress()` does the following:

1. **Checks for script file**: Looks for `playback.json` on SD card
2. **If script exists and not playing**: Starts scripted playback
3. **If script doesn't exist or already playing**: Falls back to normal `ToggleChatState()` behavior

This means:
- ✅ **Backward compatible**: Works exactly as before if no script file exists
- ✅ **Automatic**: No configuration needed - just add the script file
- ✅ **Safe**: Won't interfere with normal operation

## Boards Already Updated

The following boards have been updated to use `HandleBootButtonPress()`:

- `esp32-s3-touch-lcd-1.85`
- `sensecap-watcher`

## Updating Other Boards

To update other boards, search for `ToggleChatState()` in the board's `.cc` file and replace it with `HandleBootButtonPress()` in the boot button click handler.

### Search Pattern

Look for patterns like:
- `app.ToggleChatState()`
- `Application::GetInstance().ToggleChatState()`

### Important Notes

- Only replace `ToggleChatState()` in the **boot button click handler**
- Keep other uses of `ToggleChatState()` unchanged (if any)
- The function signature is the same, so no other changes are needed

## Testing

After updating a board:

1. **Without script file**: Should behave exactly as before
2. **With script file**: Should play script on boot button press
3. **During script playback**: Boot button should not interrupt (normal behavior)

## Troubleshooting

If scripted playback doesn't work after integration:

1. Check that `HandleBootButtonPress()` is called (not `ToggleChatState()`)
2. Verify SD card is mounted (check logs)
3. Ensure `playback.json` exists on SD card
4. Check JSON syntax is valid

