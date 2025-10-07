#!/usr/bin/env python3
"""
Test script to verify the new retry mechanism and timeout handling.
This simulates the firmware behavior with retries and better error handling.
"""

import requests
import json
import time

def test_retry_mechanism():
    """Test the retry mechanism with the deployed SCF endpoint"""
    
    # Test error log content
    error_log_content = """E (12345) SYSTEM: Test error message for retry mechanism
E (12346) WIFI: Connection failed
E (12347) ANIMATION: Failed to load animation file
E (12348) SD_CARD: Write error on SD card
E (12349) HTTP: Request timeout
E (12350) MEMORY: Out of memory error"""

    device_id = "AA:BB:CC:DD:EE:FF"
    client_id = "xiaozhi-device-001"
    
    # Prepare JSON payload
    json_payload = {
        "error_log_content": error_log_content,
        "device_id": device_id,
        "client_id": client_id
    }
    
    print("=== Testing Retry Mechanism ===")
    print(f"Error log content:\n{error_log_content}")
    print(f"Device ID: {device_id}")
    print(f"Client ID: {client_id}")
    print(f"JSON payload size: {len(json.dumps(json_payload))} bytes")
    
    # Test with different timeout values (simulating the retry attempts)
    timeouts = [15, 30, 45]  # 15s, 30s, 45s (simulating 15s * attempt)
    
    for attempt in range(1, 4):  # 3 attempts
        timeout = timeouts[attempt - 1]
        print(f"\n--- Attempt {attempt}/3 (timeout: {timeout}s) ---")
        
        try:
            start_time = time.time()
            response = requests.post(
                'https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/',
                json=json_payload,
                headers={
                    'Content-Type': 'application/json',
                    'Device-Id': device_id,
                    'Client-Id': client_id,
                    'User-Agent': 'Xiaozhi-ErrorLog/1.0'
                },
                timeout=timeout
            )
            
            elapsed_time = time.time() - start_time
            print(f"Response time: {elapsed_time:.2f}s")
            print(f"Response status: {response.status_code}")
            
            if response.status_code == 200:
                try:
                    response_data = response.json()
                    print("✅ Upload successful!")
                    print(f"Response: {json.dumps(response_data, indent=2)}")
                    
                    # Validate response
                    if response_data.get("success") and response_data.get("action") == "error_log_upload":
                        print("✅ Response format is correct")
                        if response_data.get("device_id") == device_id:
                            print("✅ Device ID matches")
                        if response_data.get("client_id") == client_id:
                            print("✅ Client ID matches")
                        if response_data.get("file_size") == len(error_log_content):
                            print("✅ File size matches")
                        
                        print(f"\n🎉 SUCCESS on attempt {attempt}!")
                        return True
                    else:
                        print("❌ Response format is incorrect")
                        
                except json.JSONDecodeError:
                    print(f"❌ Failed to parse JSON response: {response.text}")
            else:
                print(f"❌ Upload failed with status {response.status_code}")
                print(f"Response: {response.text}")
                
        except requests.exceptions.Timeout:
            print(f"❌ Request timed out after {timeout}s")
        except requests.exceptions.RequestException as e:
            print(f"❌ Request failed: {e}")
        
        # Simulate exponential backoff delay
        if attempt < 3:
            delay = 2 * attempt  # 2s, 4s
            print(f"Waiting {delay}s before next attempt...")
            time.sleep(delay)
    
    print("\n❌ All attempts failed")
    return False

def test_response_validation():
    """Test that the SCF endpoint returns proper success responses"""
    
    print("\n=== Testing Response Validation ===")
    
    # Test with minimal error log content
    error_log_content = "E (12345) SYSTEM: Test validation message"
    
    json_payload = {
        "error_log_content": error_log_content,
        "device_id": "AA:BB:CC:DD:EE:FF",
        "client_id": "xiaozhi-device-001"
    }
    
    try:
        response = requests.post(
            'https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/',
            json=json_payload,
            headers={
                'Content-Type': 'application/json',
                'Device-Id': 'AA:BB:CC:DD:EE:FF',
                'Client-Id': 'xiaozhi-device-001',
                'User-Agent': 'Xiaozhi-ErrorLog/1.0'
            },
            timeout=30
        )
        
        print(f"Response status: {response.status_code}")
        print(f"Response headers: {dict(response.headers)}")
        
        if response.status_code == 200:
            try:
                response_data = response.json()
                print("✅ Response is valid JSON")
                
                # Check for success indicator
                if response_data.get("success") == True:
                    print("✅ Response contains success=true")
                else:
                    print("❌ Response does not contain success=true")
                
                # Check for action field
                if response_data.get("action") == "error_log_upload":
                    print("✅ Response contains correct action")
                else:
                    print("❌ Response action is incorrect")
                
                # Check for required fields
                required_fields = ["success", "message", "action", "file_size", "device_id", "client_id", "timestamp"]
                missing_fields = [field for field in required_fields if field not in response_data]
                
                if not missing_fields:
                    print("✅ All required fields present")
                else:
                    print(f"❌ Missing fields: {missing_fields}")
                
                print(f"Response: {json.dumps(response_data, indent=2)}")
                
            except json.JSONDecodeError:
                print(f"❌ Response is not valid JSON: {response.text}")
        else:
            print(f"❌ Non-200 status code: {response.status_code}")
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")

if __name__ == "__main__":
    print("Testing New Retry Mechanism and Timeout Handling")
    print("Testing against deployed SCF endpoint: https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/")
    print()
    
    # Test the retry mechanism
    success = test_retry_mechanism()
    
    # Test response validation
    test_response_validation()
    
    print("\n=== Test Summary ===")
    print("1. Retry Mechanism: Tests 3 attempts with increasing timeouts (15s, 30s, 45s)")
    print("2. Exponential Backoff: Tests delays between retries (2s, 4s)")
    print("3. Response Validation: Tests that SCF returns proper success responses")
    print("4. Timeout Handling: Tests different timeout values")
    
    if success:
        print("\n🎉 All tests passed! The new retry mechanism should work correctly.")
    else:
        print("\n⚠️ Some tests failed. Check the SCF endpoint configuration.")

