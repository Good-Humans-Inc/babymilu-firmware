# Scripted Playback Guide for Video Shooting

This guide explains how to use the scripted playback system to pre-record interaction animations and conversations for video shooting purposes.

## Overview

The scripted playback system allows you to:
- Create a JSON script file on the SD card that defines a sequence of animations and audio
- Automatically play the script when the boot button is pressed
- Perfect for video shooting where you need consistent, repeatable interactions

## Quick Start

1. **Create a script file** named `playback.json` on your SD card
2. **Insert the SD card** into your ESP32-S3 device
3. **Press the boot button** - the script will play automatically!

## Script File Format

The script file is a JSON file with the following structure:

```json
{
  "sequence": [
    {
      "type": "animation",
      "animation": "happy",
      "duration_ms": 2000
    },
    {
      "type": "animation",
      "animation": "normal",
      "duration_ms": 1000
    },
    {
      "type": "animation",
      "animation": "question",
      "duration_ms": 1500
    }
  ]
}
```

### Script Item Types

#### Animation Items

```json
{
  "type": "animation",
  "animation": "happy",
  "duration_ms": 2000
}
```

- **type**: Must be `"animation"`
- **animation**: One of the following animation names:
  - `"normal"` or `"static_normal"` - Default/idle state
  - `"happy"` - Happy emotion
  - `"fire"` - Excited/fire emotion
  - `"question"` - Questioning/confused
  - `"shy"` - Shy emotion
  - `"sleep"` - Sleepy/tired
  - `"embarrass"` or `"embarrassed"` - Embarrassed emotion
  - `"inspiration"` - Inspired/thinking
- **duration_ms**: Duration to display the animation in milliseconds

#### Audio Items (Future)

```json
{
  "type": "audio",
  "file": "audio1.wav",
  "duration_ms": 3000
}
```

- **type**: Must be `"audio"`
- **file**: Name of the audio file on SD card (e.g., `"audio1.wav"`)
- **duration_ms**: Duration of the audio in milliseconds

> **Note**: Audio playback is planned for future implementation. Currently, only animations are supported.

## Example Scripts

### Example 1: Simple Conversation Flow

```json
{
  "sequence": [
    {
      "type": "animation",
      "animation": "normal",
      "duration_ms": 1000
    },
    {
      "type": "animation",
      "animation": "question",
      "duration_ms": 2000
    },
    {
      "type": "animation",
      "animation": "happy",
      "duration_ms": 2500
    },
    {
      "type": "animation",
      "animation": "normal",
      "duration_ms": 1000
    }
  ]
}
```

This script:
1. Shows normal animation for 1 second
2. Shows question animation for 2 seconds
3. Shows happy animation for 2.5 seconds
4. Returns to normal for 1 second

### Example 2: Emotional Journey

```json
{
  "sequence": [
    {
      "type": "animation",
      "animation": "shy",
      "duration_ms": 1500
    },
    {
      "type": "animation",
      "animation": "happy",
      "duration_ms": 2000
    },
    {
      "type": "animation",
      "animation": "fire",
      "duration_ms": 3000
    },
    {
      "type": "animation",
      "animation": "sleep",
      "duration_ms": 2000
    },
    {
      "type": "animation",
      "animation": "normal",
      "duration_ms": 1000
    }
  ]
}
```

## File Location

Place your `playback.json` file in the **root directory** of your SD card:

```
/sdcard/playback.json
```

## How It Works

1. **On Boot**: The system checks if `playback.json` exists on the SD card
2. **On Boot Button Press**: 
   - If script file exists and is not already playing → Start scripted playback
   - If script file doesn't exist → Normal behavior (toggle chat state)
3. **During Playback**: Animations are displayed in sequence according to the script
4. **After Playback**: System returns to normal operation

## Troubleshooting

### Script Not Playing

1. **Check SD card is mounted**: Look for "SD card mounted successfully" in logs
2. **Check file name**: Must be exactly `playback.json` (case-sensitive)
3. **Check file location**: Must be in root directory of SD card (`/sdcard/`)
4. **Check JSON format**: Use a JSON validator to ensure valid JSON syntax
5. **Check file size**: Script file must be less than 10KB

### Script Playing But Animations Not Showing

1. **Check animation names**: Use exact names from the supported list
2. **Check SD card animations**: Ensure animation files (like `test.bin`) are on SD card
3. **Check duration**: Very short durations (< 100ms) may not be visible

### Logs

Check the serial monitor for detailed logs:
- `ScriptedPlayback`: Script loading and playback status
- `animation`: Animation loading and display status

## Advanced Usage

### Multiple Script Files

Currently, the system only supports `playback.json`. To use different scripts:
1. Rename your script file to `playback.json`
2. Replace the SD card or update the file

### Script Length

- Maximum script file size: 10KB
- Recommended: Keep scripts under 50 items for best performance
- Total playback time: Sum of all `duration_ms` values

### Timing Tips

- **Short durations** (500-1000ms): Quick transitions
- **Medium durations** (1000-2000ms): Normal interactions
- **Long durations** (2000-5000ms): Emphasis or waiting states

## Integration with Existing System

The scripted playback system integrates seamlessly:
- **SD Card**: Uses existing SD card infrastructure
- **Animations**: Uses existing animation system
- **Display**: Uses existing LVGL display system
- **Boot Button**: Intercepts boot button press, falls back to normal behavior if no script

## Future Enhancements

Planned features:
- Audio file playback support
- Multiple script files with selection
- Script looping
- Conditional playback based on device state
- Script scheduling/timing

## Example Use Cases

1. **Product Demo Videos**: Create a script showing all animations in sequence
2. **Tutorial Videos**: Script a conversation flow for demonstration
3. **Marketing Videos**: Showcase emotional expressions in a specific order
4. **Testing**: Verify all animations work correctly

## Support

For issues or questions:
- Check the logs for error messages
- Verify JSON syntax is correct
- Ensure SD card is properly formatted (FAT32)
- Make sure SD card is inserted before boot

