# Mobile Upload Options

## Option 1: Web Interface (Easiest)

### Setup:
1. **Run the web server on your computer:**
   ```bash
   python scripts/web_upload_server.py 8000
   ```

2. **Find your computer's IP address:**
   - Windows: `ipconfig`
   - Mac/Linux: `ifconfig` or `ip addr`

3. **Access from phone:**
   - Open browser on phone
   - Go to: `http://<your_computer_ip>:8000`
   - Upload images directly from phone

### Features:
- ✅ Works on any phone with a browser
- ✅ No app installation needed
- ✅ Automatic image conversion
- ✅ Real-time upload to device

## Option 2: Python Script with Image Conversion

### For testing with actual images:

```bash
# Convert any image to animation format
python scripts/convert_image_to_animation.py my_photo.jpg my_animation.bin --width 64 --height 64

# Upload to device
python scripts/test_upload.py <DEVICE_IP> upload my_animation.bin my_animation.bin
```

### Supported formats:
- PNG, JPG, JPEG, BMP, GIF
- Automatic RGB565 conversion
- Resize to any dimensions
- Multiple frames support

## Option 3: Mobile App (Advanced)

### Using React Native or Flutter:
- Create simple camera/gallery picker
- Convert images to RGB565 format
- Upload via HTTP to device
- Real-time progress feedback

### Using Web App (PWA):
- Convert the web interface to Progressive Web App
- Install on phone home screen
- Offline capability
- Native-like experience

## Option 4: BLE Mobile App

### For direct BLE transfer:
- Use BLE libraries (React Native BLE, Flutter BLE)
- Implement the file transfer protocol
- Send images directly via Bluetooth
- No WiFi required

## Quick Start (Recommended)

**For immediate testing:**

1. **Use Python script:**
   ```bash
   # Convert any image
   python scripts/convert_image_to_animation.py path/to/your/image.jpg test.bin
   
   # Upload to device
   python scripts/test_upload.py 192.168.1.100 upload test.bin test.bin
   ```

2. **Use web interface:**
   ```bash
   # Start web server
   python scripts/web_upload_server.py 8000
   
   # Access from phone browser
   # http://<your_computer_ip>:8000
   ```

Both methods will convert your actual images to the proper animation format and upload them to the device!
