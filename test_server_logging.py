#!/usr/bin/env python3
"""
Test script to verify the updated server behavior:
- Only POST requests with error_log files are logged to request_logs
- GET requests are not logged to request_logs
"""

import requests
import io

def test_server_logging_behavior():
    """Test that only POST requests with err.txt are logged"""
    
    scf_url = "https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/"
    
    print("=== Testing Server Logging Behavior ===")
    
    # Test 1: Send POST with error_log file (should be logged)
    print("\n1. Testing POST with error_log file...")
    error_log_content = """E (12345) SYSTEM: Test error message for logging
E (12346) WIFI: Connection test
E (12347) ANIMATION: Test upload
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
        "User-Agent": "Xiaozhi-ErrorLog/1.0",
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Content-Length": str(len(multipart_body))
    }
    
    try:
        response = requests.post(scf_url, data=multipart_body, headers=headers, timeout=30)
        print(f"POST with error_log: Status {response.status_code}")
        if response.status_code == 200:
            print("✅ POST with error_log successful")
        else:
            print(f"❌ POST with error_log failed: {response.text}")
    except Exception as e:
        print(f"❌ POST with error_log failed: {e}")
    
    # Test 2: Send GET request (should NOT be logged to request_logs)
    print("\n2. Testing GET request...")
    try:
        response = requests.get(f"{scf_url}?action=get_logs", timeout=30)
        print(f"GET request: Status {response.status_code}")
        if response.status_code == 200:
            print("✅ GET request successful")
            try:
                data = response.json()
                print(f"Total logs in request_logs: {data.get('total_logs', 0)}")
                print(f"Error log has content: {data.get('error_log', {}).get('has_content', False)}")
            except:
                print("Could not parse GET response")
        else:
            print(f"❌ GET request failed: {response.text}")
    except Exception as e:
        print(f"❌ GET request failed: {e}")
    
    # Test 3: Send POST without error_log file (should NOT be logged to request_logs)
    print("\n3. Testing POST without error_log file...")
    try:
        response = requests.post(scf_url, data={"test": "data"}, timeout=30)
        print(f"POST without error_log: Status {response.status_code}")
        if response.status_code == 200:
            print("✅ POST without error_log successful")
        else:
            print(f"❌ POST without error_log failed: {response.text}")
    except Exception as e:
        print(f"❌ POST without error_log failed: {e}")
    
    # Test 4: Check final state
    print("\n4. Checking final server state...")
    try:
        response = requests.get(f"{scf_url}?action=get_logs", timeout=30)
        if response.status_code == 200:
            data = response.json()
            print(f"Final total logs: {data.get('total_logs', 0)}")
            print(f"Error log has content: {data.get('error_log', {}).get('has_content', False)}")
            
            # Show request logs
            request_logs = data.get('request_logs', [])
            print(f"\nRequest logs ({len(request_logs)} entries):")
            for i, log in enumerate(request_logs[-5:], 1):  # Show last 5 entries
                print(f"  {i}. {log}")
                
        else:
            print(f"❌ Final check failed: {response.text}")
    except Exception as e:
        print(f"❌ Final check failed: {e}")

if __name__ == "__main__":
    test_server_logging_behavior()

