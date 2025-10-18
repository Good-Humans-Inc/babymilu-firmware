#!/usr/bin/env python3
"""
Test script for the new JSON-based error log upload approach.
This simulates the firmware sending error log content as JSON string data.
"""

import requests
import json
import time

def test_json_error_log_upload():
    """Test uploading error log content as JSON string data"""
    
    # Test error log content (simulating err.txt content)
    error_log_content = """E (12345) SYSTEM: Test error message 1
E (12346) WIFI: Connection failed
E (12347) ANIMATION: Failed to load animation file
E (12348) SD_CARD: Write error on SD card
E (12349) HTTP: Request timeout
E (12350) MEMORY: Out of memory error
E (12351) GPIO: Pin configuration error
E (12352) SPIFFS: File system error
E (12353) OTA: Update failed
E (12354) AUDIO: Codec initialization failed
E (12355) DISPLAY: Screen initialization error"""

    # Simulate device information
    device_id = "AA:BB:CC:DD:EE:FF"
    client_id = "xiaozhi-device-001"
    
    # Prepare JSON payload
    json_payload = {
        "error_log_content": error_log_content,
        "device_id": device_id,
        "client_id": client_id
    }
    
    print("=== Testing JSON Error Log Upload ===")
    print(f"Error log content:\n{error_log_content}")
    print(f"Device ID: {device_id}")
    print(f"Client ID: {client_id}")
    print(f"JSON payload size: {len(json.dumps(json_payload))} bytes")
    
    # Send POST request with JSON data
    try:
        print("\nSending JSON POST request...")
        response = requests.post(
            'https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/',
            json=json_payload,
            headers={
                'Content-Type': 'application/json',
                'Device-Id': device_id,
                'Client-Id': client_id,
                'User-Agent': 'Xiaozhi-ErrorLog/1.0'
            },
            timeout=30
        )
        
        print(f"Response status: {response.status_code}")
        print(f"Response headers: {dict(response.headers)}")
        
        if response.status_code == 200:
            try:
                response_data = response.json()
                print("✅ JSON upload successful!")
                print(f"Response: {json.dumps(response_data, indent=2)}")
                
                # Verify the response contains expected fields
                if response_data.get("success") and response_data.get("action") == "error_log_upload":
                    print("✅ Response format is correct")
                    if response_data.get("device_id") == device_id:
                        print("✅ Device ID matches")
                    if response_data.get("client_id") == client_id:
                        print("✅ Client ID matches")
                    if response_data.get("file_size") == len(error_log_content):
                        print("✅ File size matches")
                else:
                    print("❌ Response format is incorrect")
                    
            except json.JSONDecodeError:
                print(f"❌ Failed to parse JSON response: {response.text}")
        else:
            print(f"❌ Upload failed with status {response.status_code}")
            print(f"Response: {response.text}")
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")

def test_get_logs():
    """Test retrieving the uploaded logs"""
    print("\n=== Testing Log Retrieval ===")
    
    try:
        response = requests.get('https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/?action=get_logs', timeout=10)
        
        if response.status_code == 200:
            try:
                data = response.json()
                print("✅ Log retrieval successful!")
                print(f"Error log has content: {data.get('error_log', {}).get('has_content', False)}")
                print(f"Total request logs: {data.get('total_logs', 0)}")
                
                if data.get('error_log', {}).get('has_content'):
                    print("✅ Error log content was stored successfully")
                    error_content = data.get('error_log', {}).get('content', '')
                    print(f"Stored content length: {len(error_content)} bytes")
                else:
                    print("❌ No error log content found")
                    
            except json.JSONDecodeError:
                print(f"❌ Failed to parse JSON response: {response.text}")
        else:
            print(f"❌ Log retrieval failed with status {response.status_code}")
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")

def test_clear_logs():
    """Test clearing all logs"""
    print("\n=== Testing Log Clear ===")
    
    try:
        response = requests.get('https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/?action=clear', timeout=10)
        
        if response.status_code == 200:
            try:
                data = response.json()
                print("✅ Log clear successful!")
                print(f"Response: {json.dumps(data, indent=2)}")
            except json.JSONDecodeError:
                print(f"❌ Failed to parse JSON response: {response.text}")
        else:
            print(f"❌ Log clear failed with status {response.status_code}")
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")

if __name__ == "__main__":
    print("Starting JSON Error Log Upload Tests")
    print("Testing against deployed SCF endpoint: https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/")
    print()
    
    # Test the JSON upload
    test_json_error_log_upload()
    
    # Wait a moment for processing
    time.sleep(1)
    
    # Test retrieving logs
    test_get_logs()
    
    # Test clearing logs
    test_clear_logs()
    
    print("\n=== Test Summary ===")
    print("1. JSON Error Log Upload: Tests sending error log content as JSON string")
    print("2. Log Retrieval: Tests getting the stored error log content")
    print("3. Log Clear: Tests clearing all stored data")
    print("\nIf all tests pass, the new string-based approach is working correctly!")
