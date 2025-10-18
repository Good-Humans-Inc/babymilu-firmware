# Merged Animation Files Guide

This guide explains how to use the new merged animation file feature for more efficient SPIFFS loading.

## Overview

The merged animation feature allows you to combine multiple animation frames (e.g., `normal1.bin`, `normal2.bin`, `normal3.bin`) into a single file (`normal_all.bin`) for improved loading performance and reduced file system overhead.

## How It Works

### Current Implementation (Individual Files)
- Each animation frame is stored as a separate file
- Files are loaded individually when needed
- Multiple file system operations required

### Merged Implementation (Single File)
- All frames for an animation are stored in one file
- Single file system operation to load all frames
- Reduced metadata overhead
- Better flash wear leveling

## File Format

The merged file format is simple - it's just the concatenation of individual animation files:

```
[Frame 1 Header (24 bytes)] [Frame 1 Pixel Data]
[Frame 2 Header (24 bytes)] [Frame 2 Pixel Data]  
[Frame 3 Header (24 bytes)] [Frame 3 Pixel Data]
```

Each frame header contains 6 `uint32_t` values:
- Magic number (0x4C56474C = "LVGL")
- Color format
- Flags
- Width
- Height
- Stride

## Implementation Details

### Code Changes

1. **New Function**: `animation_create_spiffs_animation_from_merged()`
   - Loads multiple frames from a single merged file
   - Handles memory allocation and LVGL image descriptor setup
   - Provides proper error handling and cleanup

2. **Modified Function**: `animation_load_normal_from_spiffs()`
   - First attempts to load `normal_all.bin` (merged file)
   - Falls back to individual files (`normal1.bin`, `normal2.bin`, `normal3.bin`) if merged file not found
   - Maintains backward compatibility

### Loading Strategy

The system uses a fallback strategy:
1. Try to load `normal_all.bin` (merged file)
2. If that fails, load individual files (`normal1.bin`, `normal2.bin`, `normal3.bin`)
3. If both fail, use static animations

## Creating Merged Files

### Using the Merge Script

The provided Python script can merge normal animation files:

```bash
# Navigate to the project directory
cd /path/to/babymilu-firmware-nimble_and_spiff_normal

# Run the merge script
python scripts/merge_normal_animations.py animations/ animations/normal_all.bin
```

### Script Usage

```bash
python scripts/merge_normal_animations.py <input_directory> <output_file>

# Example:
python scripts/merge_normal_animations.py animations/ animations/normal_all.bin
```

### Script Features

- Validates input files before merging
- Provides detailed progress information
- Validates magic numbers and file sizes
- Creates output directory if needed
- Comprehensive error handling

## Testing Merged Files

### Using the Test Script

Validate your merged files before deploying:

```bash
python scripts/test_merged_animation.py animations/normal_all.bin
```

### Manual Testing

1. Create the merged file using the merge script
2. Upload it to your device's SPIFFS partition
3. Monitor the device logs for loading messages:
   - `"✅ Successfully loaded normal animation from merged file"` - Merged file loaded successfully
   - `"Merged file not found, trying individual files..."` - Fallback to individual files

## Benefits

### Performance Improvements
- **Reduced File System Operations**: 1 file open instead of 3
- **Better Flash Efficiency**: Less metadata overhead
- **Improved Wear Leveling**: Fewer file system operations

### Deployment Benefits
- **Simplified Upload**: Upload 1 file instead of 3
- **Atomic Updates**: Update entire animation set at once
- **Easier Management**: Track one file instead of multiple

### Memory Usage
- **Same Memory Footprint**: No change in RAM usage
- **Efficient Loading**: All frames loaded in one operation

## Backward Compatibility

The implementation maintains full backward compatibility:
- If `normal_all.bin` doesn't exist, falls back to individual files
- If individual files don't exist, falls back to static animations
- No breaking changes to existing functionality

## Future Extensions

This pattern can be extended to other animations:
- `embarrass_all.bin` (embarrass1.bin + embarrass2.bin + embarrass3.bin)
- `fire_all.bin` (fire1.bin + fire2.bin + fire3.bin + fire4.bin)
- `happy_all.bin` (happy1.bin + happy2.bin + happy3.bin + happy4.bin)

Simply modify the respective loading functions to follow the same pattern as `animation_load_normal_from_spiffs()`.

## Troubleshooting

### Common Issues

1. **"Failed to open merged file"**
   - Ensure the merged file exists in the SPIFFS partition
   - Check file permissions and SPIFFS mount

2. **"Invalid image magic"**
   - The merged file may be corrupted
   - Recreate using the merge script

3. **"Failed to allocate memory"**
   - Insufficient RAM for loading frames
   - Check available memory and frame sizes

### Debug Information

The system provides detailed logging:
- File loading progress
- Frame dimensions and sizes
- Memory allocation status
- Error details for troubleshooting

## Example Workflow

1. **Prepare individual files**: Ensure `normal1.bin`, `normal2.bin`, `normal3.bin` exist
2. **Create merged file**: Run the merge script
3. **Validate merged file**: Use the test script
4. **Deploy**: Upload merged file to device
5. **Test**: Monitor device logs for successful loading
6. **Clean up**: Remove individual files if desired (optional)

This approach provides a smooth transition from individual files to merged files while maintaining full backward compatibility.
