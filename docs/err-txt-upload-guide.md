# Error Log Upload to SCF Guide

This guide explains how to use the new err.txt upload functionality that uploads error logs from the SD card to a Serverless Cloud Function (SCF) via HTTPS.

## Overview

The system automatically captures all `ESP_LOGE()` error messages and writes them to `err.txt` on the SD card. The error log upload functionality **automatically uploads err.txt to SCF** when the animation updater receives an empty response (no animation update needed). This provides centralized error monitoring and analysis without manual intervention.

## Features

- ✅ **Automatic Error Capture**: All `ESP_LOGE()` calls are automatically written to `/sdcard/err.txt`
- ✅ **Automatic Upload**: Error logs are automatically uploaded to SCF when no animation update is needed
- ✅ **HTTPS Upload**: Secure upload to SCF endpoint using JSON string data
- ✅ **Device Information**: Includes device ID, version, and timestamp with each upload
- ✅ **Error Handling**: Graceful handling of missing files, empty files, and network errors
- ✅ **Thread-Safe**: Safe to call from any task/thread

## Automatic Upload Behavior

The error log upload is **automatically triggered** during the animation update check process:

### When It Happens
- **Trigger**: When the animation updater receives an empty HTTPS response (no animation update needed)
- **Frequency**: Every time the animation updater checks for updates (default: every 10 seconds)
- **Condition**: Only uploads if err.txt exists and has content

### Expected Log Output
```
I AnimationUpdater: Empty response received - no update needed, current version is up to date
I AnimationUpdater: No animation update needed - automatically uploading err.txt to SCF
I AnimationUpdater: Uploading err.txt to default SCF URL: https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/
I AnimationUpdater: ✅ Successfully uploaded err.txt content to SCF (1234 bytes)
I AnimationUpdater: ✅ Error logs uploaded successfully during update check
```

### Manual Override
You can still manually trigger uploads if needed:

## API Usage

### Manual Usage (Optional)

```cpp
#include "animation/animation_updater.h"

// Upload err.txt to SCF endpoint (custom URL)
std::string scf_url = "https://your-scf-url.com/";
bool success = AnimationUpdater::GetInstance().UploadErrorLogToScf(scf_url);

// Or use the default SCF URL (simpler)
bool success = AnimationUpdater::GetInstance().UploadErrorLogToDefaultScf();

if (success) {
    ESP_LOGI(TAG, "Error log uploaded successfully");
} else {
    ESP_LOGE(TAG, "Failed to upload error log");
}
```

### Integration Example

```cpp
// In your main.cc or application code
void upload_error_logs_periodically() {
    // Your SCF endpoint URL
    std::string scf_url = "https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/";
    
    // Upload err.txt
    bool success = AnimationUpdater::GetInstance().UploadErrorLogToScf(scf_url);
    
    // Or simply use the default method
    // bool success = AnimationUpdater::GetInstance().UploadErrorLogToDefaultScf();
    
    if (success) {
        ESP_LOGI(TAG, "✅ Error logs uploaded to SCF successfully");
    } else {
        ESP_LOGW(TAG, "⚠️ Failed to upload error logs to SCF");
    }
}
```

## Upload Format

The upload uses `application/json` with the following structure:

### JSON Fields

| Field Name | Type | Description |
|------------|------|-------------|
| `error_log_content` | String | The err.txt file content as escaped JSON string |
| `device_id` | String | Device MAC address |
| `client_id` | String | Device UUID |

### Example Request

```
POST / HTTP/1.1
Host: your-scf-url.com
Content-Type: application/json
User-Agent: Xiaozhi-ErrorLog/1.0
Device-Id: AA:BB:CC:DD:EE:FF
Client-Id: xiaozhi-device-001
Content-Length: 456

{
  "error_log_content": "E (12345) SYSTEM: Error message 1\\nE (12346) WIFI: Connection failed\\nE (12347) ANIMATION: Failed to load frame",
  "device_id": "AA:BB:CC:DD:EE:FF",
  "client_id": "xiaozhi-device-001"
}
```

## SCF Endpoint Requirements

Your SCF endpoint should:

1. **Accept POST requests** with `application/json` on the root path `/`
2. **Parse JSON data** and extract the `error_log_content` field
3. **Return HTTP 200** for successful uploads
4. **Process the error log content** as needed

### Example SCF Handler (Python)

```python
from flask import Flask, request
import json

app = Flask(__name__)

@app.route('/', methods=['POST'])
def upload_error_log():
    try:
        # Check if this is JSON data
        if request.content_type and 'application/json' in request.content_type:
            json_data = request.get_json()
            if json_data and 'error_log_content' in json_data:
                # Extract error log content
                error_content = json_data['error_log_content']
                device_id = json_data.get('device_id', 'unknown')
                client_id = json_data.get('client_id', 'unknown')
                
                # Process the error log (you can add your processing logic here)
                print(f"Received error log from device {device_id}:")
                print(error_content)
                
                return json.dumps({
                    "success": True, 
                    "message": "Error log processed successfully",
                    "device_id": device_id,
                    "client_id": client_id
                }), 200
        
        return json.dumps({"success": False, "message": "Invalid request format"}), 400
        
    except Exception as e:
        return json.dumps({"success": False, "message": str(e)}), 500

@app.route('/', methods=['GET'])
def hello_world():
    return 'Hello World - Error Log Upload Service'

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=9000)
```

## Error Handling

The upload function handles various error conditions gracefully:

- **SD Card Not Mounted**: Returns `false`, logs error
- **File Not Found**: Returns `true` (not an error, just nothing to upload)
- **Empty File**: Returns `true` (not an error, just empty file)
- **Network Errors**: Returns `false`, logs error
- **HTTP Errors**: Logs warning but may still return `true` depending on response

## Testing

### Enable Test Code

To test the functionality, uncomment the test code in `main/main.cc`:

```cpp
// Uncomment these lines to test err.txt upload
ESP_LOGI(TAG, "=== Testing err.txt upload functionality ===");
// Use the default SCF URL (simplest)
bool upload_success = AnimationUpdater::GetInstance().UploadErrorLogToDefaultScf();
ESP_LOGI(TAG, "err.txt upload test result: %s", upload_success ? "SUCCESS" : "FAILED");
```

### Generate Test Errors

To generate test error logs:

```cpp
// Generate some test errors
ESP_LOGE(TAG, "Test error 1: This is a test error message");
ESP_LOGE(TAG, "Test error 2: Simulated system failure");
ESP_LOGE(TAG, "Test error 3: Network timeout error");
```

## Integration Points

### Periodic Upload

You can integrate error log uploads into your existing update cycle:

```cpp
// In AnimationUpdater::UpdateLoop()
void AnimationUpdater::UpdateLoop() {
    while (is_running_.load()) {
        // Existing animation update logic...
        
        // Upload error logs every 10 update cycles
        static int upload_counter = 0;
        if (++upload_counter >= 10) {
            upload_counter = 0;
            // Use default SCF URL
            UploadErrorLogToDefaultScf();
        }
        
        vTaskDelay(pdMS_TO_TICKS(check_interval_seconds_ * 1000));
    }
}
```

### Error Event Triggered Upload

Upload error logs when specific errors occur:

```cpp
void handle_critical_error() {
    ESP_LOGE(TAG, "Critical system error occurred");
    
    // Upload error logs immediately using default SCF URL
    AnimationUpdater::GetInstance().UploadErrorLogToDefaultScf();
}
```

## Security Considerations

- **HTTPS Only**: Always use HTTPS endpoints for security
- **Device Authentication**: Consider adding authentication tokens
- **Rate Limiting**: Implement rate limiting to prevent spam
- **Data Privacy**: Ensure error logs don't contain sensitive information

## Troubleshooting

### Common Issues

1. **Upload Fails**: Check SCF URL and network connectivity
2. **Empty File**: No errors have occurred yet (normal)
3. **File Not Found**: SD card not mounted or err.txt not created
4. **Network Timeout**: Increase timeout or check network stability

### Debug Logs

Enable debug logging to see detailed upload process:

```
I AnimationUpdater: Starting err.txt upload to SCF URL: https://...
I AnimationUpdater: Found err.txt file (1234 bytes) - preparing upload
I AnimationUpdater: Uploading err.txt to SCF (5678 bytes total)...
I AnimationUpdater: SCF upload response status: 200
I AnimationUpdater: ✅ Successfully uploaded err.txt to SCF (5678 bytes)
```

## Conclusion

The err.txt upload functionality provides a robust way to collect and analyze error logs from deployed devices. It integrates seamlessly with the existing error logging system and provides comprehensive error handling for production use.
