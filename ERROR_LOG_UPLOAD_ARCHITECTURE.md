# Error Log Upload Architecture Blueprint

## Overview
This document outlines the architecture and implementation plan for uploading `err.txt` from `/sdcard/` to a Google Cloud endpoint.

## Important Note: Google Cloud Services
**Google Cloud does not have "Lambda functions"** - that's an AWS service. Google Cloud provides:
- **Cloud Functions** (1st gen and 2nd gen) - Serverless functions
- **Cloud Run** - Containerized serverless platform (recommended for HTTP endpoints)
- **App Engine** - Fully managed platform

For HTTP endpoints with file uploads, **Cloud Functions (2nd gen)** or **Cloud Run** are recommended.

---

## Architecture Overview

```
ESP32 Device                          Google Cloud
┌─────────────┐                       ┌─────────────────────┐
│  SD Card    │                       │                     │
│  /sdcard/   │                       │  Cloud Function/    │
│  err.txt    │                       │  Cloud Run          │
│             │                       │                     │
│  ┌───────┐  │                       │  ┌───────────────┐  │
│  │ Read  │  │  HTTPS POST Request   │  │  Receive &    │  │
│  │ File  │  │──────────────────────▶│  │  Store File   │  │
│  └───────┘  │  multipart/form-data  │  │  (Cloud       │  │
│      │      │  or raw binary        │  │   Storage)    │  │
│      │      │                       │  └───────────────┘  │
│  ┌───────┐  │  ◀───────────────────│                     │
│  │ HTTP  │  │  200 OK Response      │                     │
│  │Client │  │                       │                     │
│  └───────┘  │                       │                     │
└─────────────┘                       └─────────────────────┘
```

---

## Google Cloud Products to Set Up

### Option 1: Cloud Functions (2nd Gen) - Recommended
**Product Name:** Cloud Functions (2nd generation)
- **Trigger Type:** HTTP trigger
- **Runtime:** Python 3.9+ or Node.js 18+
- **Authentication:** Can be configured as public or require authentication
- **URL Format:** `https://{REGION}-{PROJECT_ID}.cloudfunctions.net/{FUNCTION_NAME}`

**Advantages:**
- Easy to deploy
- Pay-per-invocation
- Built-in HTTPS
- Auto-scaling

**Setup Steps:**
1. Create a Cloud Function with HTTP trigger
2. Configure authentication (or make it public)
3. Set up Cloud Storage bucket for storing uploaded files (optional)
4. Deploy the function

### Option 2: Cloud Run
**Product Name:** Cloud Run
- **Service Type:** Containerized service
- **Protocol:** HTTP/HTTPS
- **URL Format:** `https://{SERVICE_NAME}-{HASH}-{REGION}.a.run.app`

**Advantages:**
- More control over runtime
- Better for larger payloads
- Supports custom containers

---

## Implementation Approach

### Firmware Side (ESP32)

#### 1. File Reading Strategy
- Use existing `SdCard::ReadTextFile()` or read directly from `/sdcard/err.txt`
- Consider file size limits (ESP32 memory constraints)
- Implement chunked reading for large files if needed

#### 2. HTTP Upload Methods

**Method A: Multipart/Form-Data (Recommended)**
- Similar to existing camera upload implementation
- Good compatibility with most HTTP servers
- Can include metadata (device ID, timestamp)

**Method B: Raw Binary POST**
- Simpler implementation
- Requires Cloud Function to handle raw body
- No metadata in body (use headers instead)

**Method C: Base64 Encoded JSON**
- Encodes file content as base64 string
- Larger payload size (~33% overhead)
- Easy to parse on server side

#### 3. Implementation Components Needed

```cpp
// New class or function to handle error log upload
class ErrorLogUploader {
public:
    static bool UploadErrorLog(const std::string& cloud_function_url);
    static bool UploadErrorLogWithMetadata(const std::string& url);
    
private:
    static std::string ReadErrorLogFile();
    static bool UploadMultipart(const std::string& url, const std::string& file_content);
};
```

### Cloud Function Side (Google Cloud)

#### 1. Function Handler (Python Example)
```python
import functions_framework
from google.cloud import storage
import os

@functions_framework.http
def upload_error_log(request):
    """HTTP Cloud Function to receive error log files."""
    # Get uploaded file
    file = request.files.get('err_file')
    device_id = request.form.get('device_id', 'unknown')
    timestamp = request.form.get('timestamp', '')
    
    if not file:
        return {'error': 'No file uploaded'}, 400
    
    # Optional: Store in Cloud Storage
    storage_client = storage.Client()
    bucket = storage_client.bucket(os.environ.get('BUCKET_NAME'))
    blob = bucket.blob(f'error_logs/{device_id}/{timestamp}_err.txt')
    blob.upload_from_string(file.read())
    
    return {'success': True, 'file_id': blob.name}, 200
```

#### 2. Required Google Cloud Services
- **Cloud Functions** or **Cloud Run** (the endpoint)
- **Cloud Storage** (optional, for storing uploaded files)
- **Cloud IAM** (for authentication/authorization)
- **Cloud Logging** (for monitoring, automatic)

---

## Important Considerations

### 1. Security & Authentication

**Options:**
- **API Key in URL/Header:** Simple but less secure
  - Format: `?key=YOUR_API_KEY` or `X-API-Key: YOUR_API_KEY` header
- **OAuth 2.0:** More secure but complex for ESP32
- **Bearer Token:** Simple and secure
  - Format: `Authorization: Bearer YOUR_TOKEN` header
- **Public Endpoint:** Easiest but least secure (only for testing)

**Recommendation:** Use API Key in header for ESP32 devices.

### 2. File Size Limitations

**ESP32 Constraints:**
- Limited heap memory (~200KB+ free typically)
- Recommend limiting file size to < 100KB for safe operation
- For larger files, implement chunked reading

**Cloud Function Limits:**
- Cloud Functions (2nd gen): 32MB request size
- Cloud Run: 32MB by default (can be increased)

### 3. Network Reliability

**Considerations:**
- Implement retry logic with exponential backoff
- Handle network timeouts gracefully
- Queue uploads if device is offline
- Check WiFi connection before attempting upload

### 4. Error Handling

**Firmware Side:**
- Check if SD card is mounted
- Verify file exists before reading
- Handle HTTP errors (4xx, 5xx)
- Log upload attempts and results

**Cloud Function Side:**
- Validate file format/size
- Handle duplicate uploads (idempotency)
- Rate limiting to prevent abuse
- Return meaningful error messages

### 5. Metadata to Include

**Recommended Fields:**
- Device ID (MAC address)
- Device UUID
- Timestamp
- Firmware version
- File size
- Upload attempt number

### 6. Memory Management

**Critical Considerations:**
- Read file in chunks if > 50KB
- Avoid loading entire file into memory if possible
- Use streaming upload with chunked transfer encoding
- Free memory immediately after upload

### 7. Upload Trigger Strategy

**Options:**
1. **Manual Trigger:** User-initiated via command/button
2. **Periodic:** Scheduled upload (e.g., daily)
3. **On Error:** Upload when error log reaches certain size
4. **On Boot:** Upload existing log on device restart
5. **Hybrid:** Combine multiple triggers

**Recommendation:** Implement manual + periodic (e.g., once per day) with configurable interval.

### 8. URL Configuration

**Storage Options:**
- Compile-time constant (in `sdkconfig` or header)
- Runtime configuration (via settings/config file)
- MQTT message (dynamic configuration)

**Recommendation:** Use compile-time constant with option to override via MQTT/settings.

---

## Implementation Steps

### Phase 1: Basic Upload Functionality
1. Create `error_log_uploader.h` and `.cc` files
2. Implement file reading from `/sdcard/err.txt`
3. Implement basic HTTP POST (multipart/form-data)
4. Add error handling and logging
5. Test with a simple HTTP server first

### Phase 2: Cloud Function Setup
1. Create Cloud Function with HTTP trigger
2. Configure authentication (API key)
3. Set up Cloud Storage bucket (optional)
4. Deploy and test endpoint

### Phase 3: Integration & Robustness
1. Integrate upload function into main application
2. Add retry logic
3. Add upload trigger mechanism
4. Add configuration management
5. Test in various network conditions

### Phase 4: Monitoring & Maintenance
1. Add upload success/failure logging
2. Monitor Cloud Function metrics
3. Set up alerts for upload failures
4. Document API endpoints

---

## Code Structure Reference

Based on existing codebase patterns:

```
main/
├── error_log_uploader.h          # New header file
├── error_log_uploader.cc         # New implementation
├── sd_card.cc                    # Existing (already has file read capabilities)
└── application.cc                # Integration point
```

**Key Dependencies:**
- `Board::CreateHttp()` - HTTP client creation
- `SdCard::ReadTextFile()` - File reading (or direct file I/O)
- `SystemInfo::GetMacAddress()` - Device identification
- Existing HTTP multipart upload pattern (from `esp32_camera.cc`)

---

## Testing Strategy

### 1. Unit Testing (Firmware)
- Mock HTTP client
- Test file reading logic
- Test multipart encoding
- Test error handling

### 2. Integration Testing
- Test with local HTTP server
- Test with Cloud Function endpoint
- Test various file sizes
- Test network failure scenarios

### 3. End-to-End Testing
- Test full upload flow
- Test retry mechanisms
- Test with actual device
- Monitor Cloud Function logs

---

## Cost Considerations (Google Cloud)

### Cloud Functions Pricing
- **Invocation:** $0.40 per million invocations
- **Compute Time:** $0.0000025 per GB-second
- **Networking:** Egress charges apply

**Estimated Cost:** Very low for typical usage (few uploads per day per device)

### Cloud Storage (if storing files)
- **Storage:** $0.020 per GB/month
- **Operations:** $0.05 per 10,000 operations

---

## Security Best Practices

1. **Use HTTPS only** (never HTTP)
2. **Validate API keys** on Cloud Function side
3. **Rate limit** requests per device ID
4. **Sanitize filenames** to prevent path traversal
5. **Set Content-Type** correctly
6. **Log access** for auditing
7. **Encrypt sensitive data** if needed

---

## Example Request/Response

### Request (Multipart/Form-Data)
```
POST /upload-error-log HTTP/1.1
Host: us-central1-myproject.cloudfunctions.net
Content-Type: multipart/form-data; boundary=----ESP32_ERROR_LOG_BOUNDARY
X-API-Key: your-api-key-here
Content-Length: 1234

------ESP32_ERROR_LOG_BOUNDARY
Content-Disposition: form-data; name="device_id"

AA:BB:CC:DD:EE:FF
------ESP32_ERROR_LOG_BOUNDARY
Content-Disposition: form-data; name="timestamp"

2024-01-15T10:30:00Z
------ESP32_ERROR_LOG_BOUNDARY
Content-Disposition: form-data; name="file"; filename="err.txt"
Content-Type: text/plain

[error log content here]
------ESP32_ERROR_LOG_BOUNDARY--
```

### Response
```json
{
  "success": true,
  "file_id": "error_logs/AA:BB:CC:DD:EE:FF/2024-01-15T10:30:00Z_err.txt",
  "uploaded_at": "2024-01-15T10:30:05Z"
}
```

---

## Next Steps

1. **Decide on Cloud Service:** Cloud Functions (2nd gen) or Cloud Run
2. **Set up Google Cloud Project:** Create project, enable APIs
3. **Create Cloud Function:** Deploy HTTP-triggered function
4. **Implement Firmware Code:** Create uploader class
5. **Test Locally:** Test with mock server first
6. **Deploy & Test:** Test end-to-end with actual Cloud Function
7. **Integrate:** Add to main application flow
8. **Monitor:** Set up logging and monitoring

