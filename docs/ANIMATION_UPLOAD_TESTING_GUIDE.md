# 🎭 Animation Upload Testing Guide

## ⚠️ CRITICAL: Pre-Flash Checklist

**BEFORE FLASHING**, ensure these bugs are fixed:
- ✅ Added `#include <unistd.h>` for mkdir()
- ✅ Fixed BLE file transfer logic error
- ✅ Fixed HTTP server memory leaks
- ✅ Added manifest JSON error handling
- ✅ Added file size validation (1MB limit)
- ✅ Added filename validation (security)

## 🚀 Step-by-Step Testing Guide

### **Phase 1: Basic Firmware Flash & Network Test**

#### Step 1: Build and Flash Firmware
```bash
cd /Users/yan/Desktop/babymilu-firmware
idf.py build
idf.py flash monitor
```

#### Step 2: Verify Network Connection
**Expected Log Messages:**
```
I (12345) WifiBoard: WiFi connected, starting file upload server on port 8080
I (12346) WifiBoard: File upload server started successfully
I (12347) WifiBoard: Endpoints:
I (12348) WifiBoard:   POST /upload?filename=<name> - Upload animation file
I (12349) WifiBoard:   DELETE /delete?filename=<name> - Delete animation file
I (12350) WifiBoard:   GET /list - List available files
```

**Note the device IP address** (e.g., `192.168.1.100`)

#### Step 3: Test SPIFFS Initialization
**Expected Log Messages:**
```
I (12351) animation: SPIFFS initialized successfully (read-write mode)
I (12352) animation: SPIFFS Debug Test Complete
```

### **Phase 2: HTTP Server Testing**

#### Step 4: Test Server Endpoints
```bash
# Test file listing (should return empty manifest)
curl "http://<DEVICE_IP>:8080/list"

# Expected response: {"version":1,"files":{}}
```

#### Step 5: Create Test Animation File
```bash
# Create a small test animation
python scripts/create_test_animation.py test1.bin 32 32 2

# Verify file was created
ls -la test1.bin
```

#### Step 6: Test File Upload
```bash
# Upload test file
python scripts/test_upload.py <DEVICE_IP> upload test1.bin test1.bin

# Expected output:
# ✅ Upload successful: test1.bin
#    Response: {"success": true, "message": "File uploaded successfully"}
```

**Expected Device Logs:**
```
I (12353) WifiBoard: File upload request received
I (12354) WifiBoard: Uploading file: test1.bin (1234 bytes)
I (12355) animation: Writing file atomically: test1.bin (1234 bytes)
I (12356) animation: Successfully wrote file: /spiffs/test1.bin
I (12357) animation: Manifest updated for file: test1.bin
I (12358) animation: Reloading animations from manifest...
```

#### Step 7: Verify File Upload
```bash
# List files to verify upload
python scripts/test_upload.py <DEVICE_IP> list

# Expected response with file metadata
```

#### Step 8: Test File Deletion
```bash
# Delete the test file
python scripts/test_upload.py <DEVICE_IP> delete test1.bin

# Expected output:
# ✅ Delete successful: test1.bin
```

### **Phase 3: Error Handling Testing**

#### Step 9: Test Invalid Filenames
```bash
# Test path traversal (should fail)
curl -X POST "http://<DEVICE_IP>:8080/upload?filename=../../../etc/passwd" --data-binary @test1.bin

# Test long filename (should fail)
curl -X POST "http://<DEVICE_IP>:8080/upload?filename=very_long_filename_that_exceeds_limit.bin" --data-binary @test1.bin
```

#### Step 10: Test Large File Upload
```bash
# Create a large test file (>1MB)
dd if=/dev/zero of=large_test.bin bs=1024 count=1025

# Try to upload (should fail)
python scripts/test_upload.py <DEVICE_IP> upload large_test.bin large_test.bin

# Expected: ❌ Upload failed (file too large)
```

### **Phase 4: Animation Loading Testing**

#### Step 11: Test Animation Loading
**Expected Device Logs:**
```
I (12359) animation: Using SPIFFS-based normal animation
I (12360) animation: SPIFFS animation has 3 frames
```

#### Step 12: Test Animation Display
- Observe device display for animation changes
- Verify animations are playing correctly
- Check for any display artifacts or crashes

### **Phase 5: BLE File Transfer Testing**

#### Step 13: Test BLE Connection
1. Put device in WiFi configuration mode
2. Connect to BLE device "Xiaozhi-WiFi"
3. Verify connection established

#### Step 14: Test BLE File Transfer Commands
```
# Send via BLE write characteristic (0xDEAD):
FILE_START:test_ble.bin:1024
FILE_DATA:binary_data_here
FILE_CANCEL
```

**Expected BLE Responses:**
- `FILE_READY` - Transfer started
- `FILE_DATA_OK` - Data chunk received
- `FILE_COMPLETE` - Transfer finished
- `FILE_CANCELLED` - Transfer cancelled

### **Phase 6: Stress Testing**

#### Step 15: Multiple File Uploads
```bash
# Upload multiple files
python scripts/create_test_animation.py test2.bin 64 64 3
python scripts/create_test_animation.py test3.bin 32 32 1

python scripts/test_upload.py <DEVICE_IP> upload test2.bin test2.bin
python scripts/test_upload.py <DEVICE_IP> upload test3.bin test3.bin
```

#### Step 16: Memory and Performance Testing
- Monitor device memory usage during uploads
- Check for memory leaks
- Verify SPIFFS space usage

### **Phase 7: Real Image Testing**

#### Step 17: Test with Actual Images
```bash
# Convert real image to animation
python scripts/convert_image_to_animation.py your_photo.jpg my_animation.bin --width 64 --height 64

# Upload to device
python scripts/test_upload.py <DEVICE_IP> upload my_animation.bin my_animation.bin
```

## 🔍 **Troubleshooting Guide**

### **Common Issues & Solutions**

#### Issue: HTTP Server Not Starting
**Symptoms:** No "File upload server started" message
**Solutions:**
- Check WiFi connection
- Verify port 8080 not blocked
- Check for compilation errors

#### Issue: Upload Fails with 500 Error
**Symptoms:** "Failed to write file" error
**Solutions:**
- Check SPIFFS space: `df -h /spiffs`
- Verify file format (must be custom LVGL format)
- Check device logs for specific error

#### Issue: Animation Not Loading
**Symptoms:** Still using static animations
**Solutions:**
- Check SPIFFS mount: `mount | grep spiffs`
- Verify file format and headers
- Check animation loading logs

#### Issue: BLE Transfer Fails
**Symptoms:** No response to BLE commands
**Solutions:**
- Verify BLE connection established
- Check GATT service UUIDs
- Ensure device in config mode

### **Debug Commands**

```bash
# Check SPIFFS status
idf.py monitor | grep -i spiffs

# Check HTTP server logs
idf.py monitor | grep -i "WifiBoard\|upload"

# Check animation logs
idf.py monitor | grep -i animation

# Check memory usage
idf.py monitor | grep -i "free\|heap"
```

## ✅ **Success Criteria**

**All tests must pass:**
- [ ] HTTP server starts on WiFi connection
- [ ] File uploads work (small files)
- [ ] File deletions work
- [ ] File listing returns manifest
- [ ] Invalid filenames rejected
- [ ] Large files rejected (>1MB)
- [ ] Animations load from SPIFFS
- [ ] BLE file transfer works
- [ ] No memory leaks
- [ ] No crashes during testing

## 🚨 **Critical Failure Points**

**If any of these fail, DO NOT PROCEED:**
- SPIFFS initialization fails
- HTTP server crashes on startup
- Memory allocation failures
- File system corruption
- Animation system crashes

## 📊 **Performance Benchmarks**

**Expected Performance:**
- Upload speed: 100-500 KB/s
- File size limit: 1MB
- Concurrent uploads: 1
- SPIFFS space: ~1MB available
- Memory usage: <50KB overhead

## 🎯 **Next Steps After Testing**

1. **If all tests pass:** Ready for production use
2. **If tests fail:** Fix issues before deployment
3. **Performance issues:** Optimize or increase limits
4. **Security concerns:** Add authentication

**Remember:** Test thoroughly before deploying to production devices!
