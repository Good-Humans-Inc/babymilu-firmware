# SD Card Debugging Guide

This guide explains how to set up and debug SD card functionality for the BabyMilu firmware, including animation loading and error logging.

## 📁 Required Files on SD Card

Place the following files in the **root directory** of your SD card:

### Required Files:
- `test.bin` - Contains all animation frames in a mega file format
- `err.txt` - Error log file (will be created automatically if it doesn't exist)

### Example SD Card Structure:
```
/sdcard/
├── test.bin          (3.6MB - animation mega file)
├── err.txt           (0 bytes initially, grows with error logs)
└── [other files]     (optional)
```

## 🚀 Setup Instructions

### 1. Prepare Your SD Card
1. **Format**: Ensure your SD card is formatted as FAT32
2. **Size**: SD card should be at least 4MB (recommend 8GB or larger)
3. **Files**: Copy `test.bin` to the root directory

### 2. Insert SD Card
1. Insert the SD card into the SenseCAP Watcher board
2. Power on the device
3. The system will automatically detect and mount the SD card

## 🔍 Debugging Features

### Automatic Error Logging
The system automatically captures all `ESP_LOGE()` error messages and writes them to `err.txt` on the SD card. This includes:

- System initialization errors
- Animation loading failures
- SD card access issues
- Any other ERROR level logs

### File Size Monitoring
The system provides detailed file size information:

```
I CUSTOM_LOGGING: === SD Card File Sizes After Error Test ===
I CUSTOM_LOGGING: File: TEST.BIN, Size: 3670688 bytes
I CUSTOM_LOGGING: File: ERR.TXT, Size: 69 bytes
I CUSTOM_LOGGING: === End SD Card File Sizes Check ===
```

### Animation Loading Status
The system shows detailed animation loading progress:

```
I animation: ✅ Successfully loaded ALL animations from SD card mega file (28 total frames)
I animation: 🎉 Successfully loaded ALL animations from SD card!
I animation:    - All 8 animation types loaded in one operation
I animation:    - Total of 28 frames loaded from test.bin on SD card
I animation:    - Ultimate optimization achieved!
```

## 🛠️ Troubleshooting

### Common Issues and Solutions

#### 1. SD Card Not Detected
**Symptoms:**
```
E SD_CARD: SD card not found
```

**Solutions:**
- Check SD card is properly inserted
- Verify SD card is not corrupted
- Ensure SD card is FAT32 formatted
- Check SPI connections (MOSI, MISO, CLK, CS pins)

#### 2. Animation Loading Fails
**Symptoms:**
```
E animation: Failed to load animations from SD card
```

**Solutions:**
- Verify `test.bin` exists in root directory
- Check file size (should be ~3.6MB)
- Ensure file is not corrupted
- Verify SD card has sufficient space

#### 3. Error Logging Not Working
**Symptoms:**
- `err.txt` remains 0 bytes
- ERROR messages not appearing in file

**Solutions:**
- Check SD card mount status
- Verify file permissions
- Ensure SD card is writable
- Check for recursion issues in logs

#### 4. File Creation Errors
**Symptoms:**
```
New file fopen failed: errno=22 (Invalid argument)
```

**Solutions:**
- Avoid special characters in filenames
- Use short, simple filenames (e.g., `tst.txt` instead of `test_write.txt`)
- Check file system compatibility

### Debug Commands

#### Check SD Card Status
The system automatically shows SD card debug status:
```
I SD_CARD: === SD Card Debug Status ===
I SD_CARD: Mount status: MOUNTED
I SD_CARD: Mount point: /sdcard
I SD_CARD: SPI host: 1
I SD_CARD: Files on SD card:
I SD_CARD:   TEST.BIN (3670688 bytes)
I SD_CARD:   ERR.TXT (0 bytes)
I SD_CARD: Total files found: 2
I SD_CARD: === End SD Card Debug Status ===
```

#### Test Error Logging
The system includes built-in error logging tests:
```
E CUSTOM_LOGGING: POST-INIT ERROR TEST: System fully initialized, testing SD card error logging
E CUSTOM_LOGGING: POST-INIT ERROR TEST: Timestamp: 1077447628
E CUSTOM_LOGGING: POST-INIT ERROR TEST: This should appear in err.txt on SD card
```

## 📊 Expected Output

### Successful SD Card Initialization
```
I SD_CARD: SD card mounted successfully at /sdcard
I SD_CARD_STARTUP: Successfully read first file from SD card (512 bytes)
I main: SD card file read successfully (512 bytes)
I main: SD card initialized and ready for animation loading
```

### Successful Animation Loading
```
I animation: ✅ Successfully opened mega file: /sdcard/test.bin (3670688 bytes)
I animation: Loading 28 total frames from SD card mega file
I animation: ✅ Successfully loaded ALL animations from SD card!
```

### Error Logging Working
```
I CUSTOM_LOGGING: Custom logging setup complete - ERROR logs will be written to err.txt on SD card
I CUSTOM_LOGGING: File: ERR.TXT, Size: 69 bytes
```

## 🔧 Technical Details

### SD Card Configuration
- **SPI Interface**: Uses SPI2_HOST on SenseCAP Watcher
- **Pins**: MOSI=GPIO5, MISO=GPIO6, CLK=GPIO4, CS=GPIO46
- **Mount Point**: `/sdcard`
- **File System**: FAT32

### File Formats
- **test.bin**: Binary file containing concatenated animation frames
- **err.txt**: Plain text file with ERROR level log messages

### Error Logging System
- **Custom Hook**: Intercepts `ESP_LOGE()` calls
- **Recursion Guard**: Prevents infinite logging loops
- **Thread Safe**: Uses mutex for concurrent access
- **Direct File Operations**: Uses `fopen()`, `fwrite()`, `fclose()`

## 📝 Best Practices

1. **Always check file sizes** after operations to verify success
2. **Monitor error logs** regularly to catch issues early
3. **Use simple filenames** to avoid file system issues
4. **Keep SD card clean** - remove old test files periodically
5. **Verify SD card health** - corrupted cards can cause random failures

## 🆘 Support

If you encounter issues not covered in this guide:

1. Check the `err.txt` file for specific error messages
2. Verify all required files are in the correct locations
3. Ensure SD card is properly formatted and not corrupted
4. Check hardware connections and power supply
5. Review the debug output for specific error codes

---

**Note**: This debugging system is designed to help diagnose issues automatically. The error logging feature ensures that any problems are captured and stored for analysis.
