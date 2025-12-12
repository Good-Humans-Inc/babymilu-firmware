# Postman Testing Guide for JSON Error Log Upload

This guide shows how to test the new JSON-based error log upload functionality using Postman.

## Setup

1. **Start the SCF Handler**: First, make sure the SCF error log handler is running:
   ```bash
   python scf_error_log_handler.py
   ```
   The server should be running on `http://localhost:9000`

2. **Open Postman**: Create a new request or use an existing collection

## Test 1: Basic JSON Error Log Upload

### Request Configuration

**Method**: `POST`
**URL**: `http://localhost:9000/`

### Headers
```
Content-Type: application/json
Device-Id: AA:BB:CC:DD:EE:FF
Client-Id: xiaozhi-device-001
User-Agent: Xiaozhi-ErrorLog/1.0
```

### Body (JSON)
```json
{
  "error_log_content": "E (12345) SYSTEM: Test error message 1\nE (12346) WIFI: Connection failed\nE (12347) ANIMATION: Failed to load animation file\nE (12348) SD_CARD: Write error on SD card\nE (12349) HTTP: Request timeout",
  "device_id": "AA:BB:CC:DD:EE:FF",
  "client_id": "xiaozhi-device-001"
}
```

### Expected Response
```json
{
  "success": true,
  "message": "Error log processed and stored successfully",
  "action": "error_log_upload",
  "file_size": 234,
  "device_id": "AA:BB:CC:DD:EE:FF",
  "client_id": "xiaozhi-device-001",
  "timestamp": "2024-01-15 10:30:45"
}
```

## Test 2: Error Log with Special Characters

### Body (JSON)
```json
{
  "error_log_content": "E (12345) SYSTEM: Error with \"quotes\" in message\nE (12346) WIFI: Connection failed with \\backslash\\ in path\nE (12347) ANIMATION: Failed to load file with\ttab\tcharacters\nE (12348) SD_CARD: Write error with\r\ncarriage return\r\nand newline",
  "device_id": "AA:BB:CC:DD:EE:FF",
  "client_id": "xiaozhi-device-001"
}
```

## Test 3: Large Error Log Content

### Body (JSON)
```json
{
  "error_log_content": "E (12345) SYSTEM: Test error message 1\nE (12346) WIFI: Connection failed\nE (12347) ANIMATION: Failed to load animation file\nE (12348) SD_CARD: Write error on SD card\nE (12349) HTTP: Request timeout\nE (12350) MEMORY: Out of memory error\nE (12351) GPIO: Pin configuration error\nE (12352) SPIFFS: File system error\nE (12353) OTA: Update failed\nE (12354) AUDIO: Codec initialization failed\nE (12355) DISPLAY: Screen initialization error\nE (12356) CAMERA: Camera initialization failed\nE (12357) SENSOR: Sensor reading error\nE (12358) NETWORK: Network configuration error\nE (12359) STORAGE: Storage access error\nE (12360) TIMER: Timer configuration error",
  "device_id": "AA:BB:CC:DD:EE:FF",
  "client_id": "xiaozhi-device-001"
}
```

## Test 4: Retrieve Uploaded Logs

### Request Configuration

**Method**: `GET`
**URL**: `http://localhost:9000/?action=get_logs`

### Expected Response
```json
{
  "success": true,
  "message": "Logs retrieved successfully",
  "action": "get_logs",
  "timestamp": "2024-01-15 10:30:45",
  "error_log": {
    "has_content": true,
    "content": "E (12345) SYSTEM: Test error message 1\nE (12346) WIFI: Connection failed...",
    "file_size": 234
  },
  "request_logs": [
    "=== POST Request Received at 2024-01-15 10:30:45 ===",
    "Content-Type: application/json",
    "Request Type: ERROR LOG UPLOAD (JSON from firmware)",
    "Device ID: AA:BB:CC:DD:EE:FF",
    "Client ID: xiaozhi-device-001",
    "Error log content size: 234 bytes"
  ],
  "total_logs": 6
}
```

## Test 5: Clear All Logs

### Request Configuration

**Method**: `GET`
**URL**: `http://localhost:9000/?action=clear`

### Expected Response
```json
{
  "success": true,
  "message": "All logs and error data cleared successfully",
  "action": "clear",
  "timestamp": "2024-01-15 10:30:45"
}
```

## Test 6: Invalid JSON (Error Handling)

### Body (Invalid JSON)
```json
{
  "error_log_content": "E (12345) SYSTEM: Test error message 1
  "device_id": "AA:BB:CC:DD:EE:FF",
  "client_id": "xiaozhi-device-001"
}
```

### Expected Response
```json
{
  "success": false,
  "message": "ERROR processing JSON error log: Expecting ',' delimiter: line 2 column 3 (char 67)",
  "action": "error_log_upload"
}
```

## Test 7: Missing Required Fields

### Body (Missing error_log_content)
```json
{
  "device_id": "AA:BB:CC:DD:EE:FF",
  "client_id": "xiaozhi-device-001"
}
```

### Expected Response
```json
{
  "success": true,
  "message": "Regular POST request received",
  "action": "regular_post",
  "files_received": [],
  "form_data": {},
  "timestamp": "2024-01-15 10:30:45"
}
```

## Postman Collection Setup

### Environment Variables
Create a Postman environment with these variables:
- `base_url`: `http://localhost:9000`
- `device_id`: `AA:BB:CC:DD:EE:FF`
- `client_id`: `xiaozhi-device-001`

### Pre-request Scripts
You can add this pre-request script to generate dynamic error log content:

```javascript
// Generate a timestamp for unique error messages
const timestamp = new Date().getTime();

// Generate error log content with current timestamp
const errorLogContent = `E (${timestamp}) SYSTEM: Test error message 1
E (${timestamp + 1}) WIFI: Connection failed
E (${timestamp + 2}) ANIMATION: Failed to load animation file
E (${timestamp + 3}) SD_CARD: Write error on SD card
E (${timestamp + 4}) HTTP: Request timeout`;

// Set the request body
pm.request.body.raw = JSON.stringify({
    "error_log_content": errorLogContent,
    "device_id": pm.environment.get("device_id"),
    "client_id": pm.environment.get("client_id")
});
```

### Tests Scripts
Add this test script to verify the response:

```javascript
// Test that the response is successful
pm.test("Status code is 200", function () {
    pm.response.to.have.status(200);
});

// Test that the response is valid JSON
pm.test("Response is valid JSON", function () {
    pm.response.to.be.json;
});

// Test that the response contains success field
pm.test("Response contains success field", function () {
    const jsonData = pm.response.json();
    pm.expect(jsonData).to.have.property('success');
});

// Test that the upload was successful
pm.test("Upload was successful", function () {
    const jsonData = pm.response.json();
    pm.expect(jsonData.success).to.be.true;
    pm.expect(jsonData.action).to.eql("error_log_upload");
});

// Test that device information is preserved
pm.test("Device information is preserved", function () {
    const jsonData = pm.response.json();
    pm.expect(jsonData.device_id).to.eql(pm.environment.get("device_id"));
    pm.expect(jsonData.client_id).to.eql(pm.environment.get("client_id"));
});
```

## Testing Workflow

1. **Start with Test 1** (Basic JSON Error Log Upload)
2. **Verify the upload** with Test 4 (Retrieve Uploaded Logs)
3. **Test edge cases** with Tests 2, 3, 6, and 7
4. **Clear logs** with Test 5 when done testing
5. **Repeat the cycle** to test different scenarios

## Troubleshooting

### Common Issues

1. **Connection Refused**: Make sure the SCF handler is running on port 9000
2. **Invalid JSON**: Check that your JSON is properly formatted
3. **Missing Headers**: Ensure Content-Type is set to application/json
4. **Empty Response**: Check the server logs for error messages

### Server Logs
Watch the terminal where you're running `python scf_error_log_handler.py` to see detailed request logs and any error messages.

## Expected Server Output

When you send a request, you should see output like this in the server terminal:

```
=== POST Request Received at 2024-01-15 10:30:45 ===
Content-Type: application/json
Content-Length: 456
Files: []
Form data: {}
Processing request immediately...
Request Type: ERROR LOG UPLOAD (JSON from firmware)
Device ID: AA:BB:CC:DD:EE:FF
Client ID: xiaozhi-device-001
Error log content size: 234 bytes
=== Received error log content ===
E (12345) SYSTEM: Test error message 1
E (12346) WIFI: Connection failed
E (12347) ANIMATION: Failed to load animation file
=== End error log content ===
=== Sending immediate response ===
Response: {'success': True, 'message': 'Error log processed and stored successfully', 'action': 'error_log_upload', 'file_size': 234, 'device_id': 'AA:BB:CC:DD:EE:FF', 'client_id': 'xiaozhi-device-001', 'timestamp': '2024-01-15 10:30:45'}
```

This comprehensive testing approach will help you verify that the new JSON-based error log upload is working correctly!
