#!/usr/bin/env python3
"""
Test script to verify the error log upload fix.
This simulates the exact multipart form data that the device sends.
"""

import requests
import io

def test_error_log_upload_with_chunked_encoding():
    """Test error log upload with the same format as the device"""
    
    scf_url = "https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/"
    
    # Simulate error log content (same format as device)
    error_log_content = """E (12345) SYSTEM: Test error message 1
E (12346) WIFI: Connection failed
E (12347) ANIMATION: Failed to load frame
E (12348) SD_CARD: Mount failed
E (12349) HTTP: Upload failed
"""
    
    print("=== Testing Error Log Upload Fix ===")
    print(f"Testing error log upload to: {scf_url}")
    print(f"Error log content:\n{error_log_content}")
    
    # Create the multipart form data exactly like the device does
    boundary = "----ESP32_ERROR_LOG_BOUNDARY"
    
    # Construct multipart body (same as device)
    multipart_body = ""
    
    # Add file field header
    multipart_body += "--" + boundary + "\r\n"
    multipart_body += "Content-Disposition: form-data; name=\"error_log\"; filename=\"err.txt\"\r\n"
    multipart_body += "Content-Type: text/plain\r\n"
    multipart_body += "\r\n"
    
    # Add file content
    multipart_body += error_log_content
    
    # Add multipart footer
    multipart_body += "\r\n--" + boundary + "--\r\n"
    
    print(f"Multipart body size: {len(multipart_body)} bytes")
    print(f"Boundary: {boundary}")
    
    # Prepare headers (same as device - using chunked encoding like camera uploads)
    headers = {
        "Device-Id": "AA:BB:CC:DD:EE:FF",  # Mock device ID
        "Client-Id": "test-client-id",     # Mock client ID
        "User-Agent": "Xiaozhi-ErrorLog/1.0",
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Transfer-Encoding": "chunked"
    }
    
    try:
        # Send POST request
        print("\nSending POST request...")
        response = requests.post(scf_url, data=multipart_body, headers=headers, timeout=30)
        
        print(f"Response status: {response.status_code}")
        print(f"Response headers: {dict(response.headers)}")
        
        if response.status_code == 200:
            print("✅ Request successful!")
            try:
                response_data = response.json()
                print(f"Response data: {response_data}")
                
                # Check if error log was received
                if response_data.get("success") and response_data.get("action") == "error_log_upload":
                    print("✅ Error log upload confirmed!")
                    print(f"File size received: {response_data.get('file_size', 0)} bytes")
                else:
                    print("⚠️ Unexpected response format")
                    
            except Exception as e:
                print(f"⚠️ Could not parse JSON response: {e}")
                print(f"Raw response: {response.text}")
        else:
            print(f"❌ Request failed with status {response.status_code}")
            print(f"Response: {response.text}")
            
    except Exception as e:
        print(f"❌ Request failed: {e}")

if __name__ == "__main__":
    test_error_log_upload_with_chunked_encoding()
