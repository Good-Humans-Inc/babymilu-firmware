# File Transfer Testing Guide

This guide provides step-by-step instructions for testing the new file transfer capabilities for animations/images over BLE and Wi-Fi.

## Overview

The implementation includes:
- ✅ **HTTP Server** with file upload endpoints (port 8080)
- ✅ **BLE File Transfer** protocol for wireless file uploads
- ✅ **SPIFFS Read-Write** support with atomic file operations
- ✅ **Manifest System** for version management and file tracking
- ✅ **Test Scripts** for automated testing

## Prerequisites

1. **Firmware Built and Flashed**: Ensure you've built and flashed the updated firmware
2. **WiFi Connected**: Device must be connected to WiFi for HTTP server testing
3. **Python Environment**: For running test scripts
4. **Required Python Packages**: `requests` (install with `pip install requests`)

## Testing Methods

### Method 1: HTTP Server Testing (Recommended)

The HTTP server starts automatically when WiFi connects and provides these endpoints:

- `POST /upload?filename=<name>` - Upload animation file
- `DELETE /delete?filename=<name>` - Delete animation file  
- `GET /list` - List available files (returns manifest.json)

#### Step 1: Find Device IP Address

1. Check device logs for WiFi connection message
2. Look for: `"WiFi connected, starting file upload server on port 8080"`
3. Note the device's IP address (e.g., `192.168.1.100`)

#### Step 2: Test File Upload

```bash
# Create test animation file
python scripts/create_test_animation.py test_animation.bin 64 64 3

# Upload to device
python scripts/test_upload.py <DEVICE_IP> upload test_animation.bin test_animation.bin
```

#### Step 3: Test File Listing

```bash
# List files on device
python scripts/test_upload.py <DEVICE_IP> list
```

#### Step 4: Test File Deletion

```bash
# Delete file from device
python scripts/test_upload.py <DEVICE_IP> delete test_animation.bin
```

#### Step 5: Run Complete Test Sequence

```bash
# Run automated test sequence
python scripts/test_upload.py <DEVICE_IP> test
```

### Method 2: Manual HTTP Testing

You can also test using curl or any HTTP client:

```bash
# Upload file
curl -X POST "http://<DEVICE_IP>:8080/upload?filename=test.bin" --data-binary @test_animation.bin

# List files
curl "http://<DEVICE_IP>:8080/list"

# Delete file
curl -X DELETE "http://<DEVICE_IP>:8080/delete?filename=test.bin"
```

### Method 3: BLE File Transfer Testing

The BLE server supports file transfer commands:

1. **Connect to BLE device**: `Xiaozhi-WiFi`
2. **Send commands** to write characteristic (`0xDEAD`):

```
FILE_START:filename:size
FILE_DATA:chunk_data
FILE_CANCEL
```

#### BLE Test Commands

```
# Start file transfer
FILE_START:test.bin:1024

# Send file data (in chunks)
FILE_DATA:binary_data_here

# Cancel transfer
FILE_CANCEL
```

## Expected Results

### Successful Upload
- Device logs show: `"File uploaded successfully: <filename>"`
- HTTP response: `{"success": true, "message": "File uploaded successfully"}`
- Manifest updated with file metadata
- Animations reloaded automatically

### File Listing
Returns JSON manifest with file information:
```json
{
  "version": 1,
  "files": {
    "test_animation.bin": {
      "size": 12345,
      "hash": "a1b2c3d4",
      "timestamp": 1640995200
    }
  }
}
```

### Animation Loading
- Device automatically detects new SPIFFS files
- Falls back to static animations if SPIFFS files not found
- Logs show: `"Using SPIFFS-based normal animation"` or `"Using static normal animation"`

## Troubleshooting

### HTTP Server Not Starting
- Check WiFi connection status
- Verify port 8080 is not blocked
- Check device logs for server startup messages

### Upload Failures
- Verify file format (must be custom LVGL binary format)
- Check available SPIFFS space
- Ensure filename doesn't contain special characters

### BLE Connection Issues
- Ensure device is in WiFi configuration mode
- Check BLE device name: `Xiaozhi-WiFi`
- Verify GATT service UUIDs: `0x180`, characteristics `0xFEF4`, `0xDEAD`

### File Format Issues
- Use `create_test_animation.py` to generate proper format
- Custom format: 6 uint32_t header + raw pixel data
- Header: magic(0x4C56474C), color_format, flags, width, height, stride

## File Format Specification

Animation files use a custom binary format:

```
Header (24 bytes):
- Magic: 0x4C56474C ("LVGL")
- Color Format: 0x0B (RGB565)
- Flags: 0x00
- Width: uint32_t
- Height: uint32_t  
- Stride: uint32_t (width * 2 for RGB565)

Data:
- Raw pixel data in RGB565 format
- Multiple frames concatenated
```

## Test Files to Use

1. **Small Test File**: `test1.bin` (64x64, 3 frames) - ~12KB
2. **Medium Test File**: `test2.bin` (32x32, 2 frames) - ~4KB
3. **Large Test File**: `test3.bin` (128x128, 5 frames) - ~160KB

## Performance Expectations

- **Upload Speed**: ~100-500 KB/s over WiFi
- **BLE Transfer**: ~1-10 KB/s (slower, for small files)
- **File Size Limit**: 1MB per file
- **Concurrent Uploads**: 1 at a time (server limitation)

## Log Monitoring

Monitor device logs for these key messages:

```
I (12345) animation: SPIFFS initialized successfully (read-write mode)
I (12346) WifiBoard: Starting file upload server on port 8080
I (12347) WifiBoard: File upload request received
I (12348) animation: Writing file atomically: test.bin (12345 bytes)
I (12349) animation: Successfully wrote file: /spiffs/test.bin
I (12350) animation: Manifest updated for file: test.bin
I (12351) animation: Reloading animations from manifest...
```

## Next Steps

After successful testing:

1. **Create Production Animation Files**: Use proper animation tools
2. **Implement Web Interface**: Build a web UI for file management
3. **Add File Validation**: Implement proper file format validation
4. **Optimize Transfer**: Add compression and resume capability
5. **Security**: Add authentication for file uploads

## Support

If you encounter issues:

1. Check device logs for error messages
2. Verify network connectivity
3. Test with provided test files first
4. Ensure proper file format
5. Check SPIFFS partition space

The implementation provides a solid foundation for runtime animation updates without requiring firmware recompilation!
