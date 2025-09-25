# Test Script Usage Guide

## How to Use `test_merged_from_images.py`

The test script validates merged binary files created by `image_to_merged_spiffs.py`. Here's how to use it:

### Basic Usage
```bash
python scripts/test_merged_from_images.py animations/normal_all.bin
```

### With Expected Frame Count
```bash
python scripts/test_merged_from_images.py animations/normal_all.bin --frames 3
```

### Command Line Options

| Option | Description | Example |
|--------|-------------|---------|
| `filepath` | Path to merged binary file | `animations/normal_all.bin` |
| `--frames N` | Expected number of frames (optional) | `--frames 3` |

### Example Output

```
=== Testing Merged Binary File: animations/normal_all.bin ===
File size: 393288 bytes

--- Frame 1 ---
  Magic: 0x4c56474c ✅
  Color format: 18 (RGB565)
  Flags: 0
  Dimensions: 256x256
  Stride: 512
  Expected data size: 131072 bytes
  Actual data size: 131072 bytes ✅
  Frame offset: 0 -> 131096

--- Frame 2 ---
  Magic: 0x4c56474c ✅
  Color format: 18 (RGB565)
  Flags: 0
  Dimensions: 256x256
  Stride: 512
  Expected data size: 131072 bytes
  Actual data size: 131072 bytes ✅
  Frame offset: 131096 -> 262192

--- Frame 3 ---
  Magic: 0x4c56474c ✅
  Color format: 18 (RGB565)
  Flags: 0
  Dimensions: 256x256
  Stride: 512
  Expected data size: 131072 bytes
  Actual data size: 131072 bytes ✅
  Frame offset: 262192 -> 393288

✅ End of file reached
✅ Successfully validated 3 frames

🎉 Merged binary file validation passed!
```

### What the Test Checks

1. **File Structure**: Validates the binary file format
2. **Magic Numbers**: Ensures each frame has the correct LVGL magic number
3. **Frame Headers**: Checks width, height, stride, and color format
4. **Data Integrity**: Verifies pixel data size matches expected size
5. **Frame Count**: Optionally validates expected number of frames
6. **File Completeness**: Ensures no extra or missing data

### Common Issues and Solutions

#### "File not found"
```
❌ File not found: animations/normal_all.bin
```
**Solution**: Check the file path and ensure the file exists.

#### "Invalid magic number"
```
❌ Frame 1: Invalid magic number 0x12345678
```
**Solution**: The file may be corrupted or not a valid merged binary file.

#### "Expected X frames, but found Y"
```
⚠️  Expected 3 frames, but found 2
```
**Solution**: Check if the source images were processed correctly.

#### "Extra bytes at end of file"
```
⚠️  Warning: 1234 extra bytes at end of file
```
**Solution**: The file may contain extra data or be corrupted.

### Integration with Workflow

Use the test script as part of your development workflow:

1. **Create merged file**:
   ```bash
   python scripts/image_to_merged_spiffs.py images/normal/ animations/normal_all.bin
   ```

2. **Test the file**:
   ```bash
   python scripts/test_merged_from_images.py animations/normal_all.bin --frames 3
   ```

3. **Deploy if test passes**:
   - Upload to device SPIFFS partition
   - Monitor device logs for successful loading

### Troubleshooting

If the test fails, check:
- Source images are valid and readable
- All frames have consistent dimensions
- No corrupted files in the source directory
- Sufficient disk space for output file

The test script provides detailed information to help diagnose any issues with the merged binary file.
