#!/usr/bin/env python3
"""
Quick test to verify the server responds properly with Connection: close header
"""

import requests
import io

def test_server_connection_close():
    """Test that server properly closes connections"""
    
    scf_url = "https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/"
    
    print("=== Testing Server Connection Close ===")
    
    # Test POST with error_log file
    error_log_content = """E (12345) SYSTEM: Test connection close
E (12346) WIFI: Connection test
"""
    
    boundary = "----ESP32_ERROR_LOG_BOUNDARY"
    multipart_body = ""
    multipart_body += "--" + boundary + "\r\n"
    multipart_body += "Content-Disposition: form-data; name=\"error_log\"; filename=\"err.txt\"\r\n"
    multipart_body += "Content-Type: text/plain\r\n"
    multipart_body += "\r\n"
    multipart_body += error_log_content
    multipart_body += "\r\n--" + boundary + "--\r\n"
    
    headers = {
        "Device-Id": "AA:BB:CC:DD:EE:FF",
        "Client-Id": "test-client-id",
        "User-Agent": "Xiaozhi-ErrorLog/1.0",
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Content-Length": str(len(multipart_body))
    }
    
    try:
        print("Sending POST request...")
        response = requests.post(scf_url, data=multipart_body, headers=headers, timeout=10)
        
        print(f"Response status: {response.status_code}")
        print(f"Response headers: {dict(response.headers)}")
        
        # Check if Connection: close header is present
        connection_header = response.headers.get('Connection', '').lower()
        print(f"Connection header: '{connection_header}'")
        
        if connection_header == 'close':
            print("✅ Server properly closes connection!")
        else:
            print("⚠️ Server may not be closing connection properly")
            
        if response.status_code == 200:
            print("✅ Request successful!")
            try:
                data = response.json()
                print(f"Response: {data}")
            except:
                print(f"Raw response: {response.text}")
        else:
            print(f"❌ Request failed: {response.text}")
            
    except requests.exceptions.Timeout:
        print("❌ Request timed out - server not closing connection")
    except Exception as e:
        print(f"❌ Request failed: {e}")

if __name__ == "__main__":
    test_server_connection_close()

