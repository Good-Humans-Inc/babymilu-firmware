#!/usr/bin/env python3
"""
Simple test to verify basic POST request functionality with the SCF endpoint.
This sends a minimal "hello world" message to test if the basic POST mechanism works.
"""

import requests
import json
import time

def test_basic_post():
    """Test basic POST request with minimal data"""
    
    print("=== Testing Basic POST Request ===")
    print("Sending minimal 'hello world' message to SCF endpoint")
    
    # Minimal JSON payload
    json_payload = {
        "message": "hello world",
        "test": True
    }
    
    print(f"Payload: {json.dumps(json_payload)}")
    print(f"Payload size: {len(json.dumps(json_payload))} bytes")
    
    try:
        start_time = time.time()
        response = requests.post(
            'https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/',
            json=json_payload,
            headers={
                'Content-Type': 'application/json',
                'User-Agent': 'Xiaozhi-Test/1.0'
            },
            timeout=30
        )
        
        elapsed_time = time.time() - start_time
        print(f"Response time: {elapsed_time:.2f}s")
        print(f"Response status: {response.status_code}")
        print(f"Response headers: {dict(response.headers)}")
        
        if response.status_code == 200:
            try:
                response_data = response.json()
                print("✅ POST request successful!")
                print(f"Response: {json.dumps(response_data, indent=2)}")
                return True
            except json.JSONDecodeError:
                print(f"❌ Response is not valid JSON: {response.text}")
                return False
        else:
            print(f"❌ POST failed with status {response.status_code}")
            print(f"Response: {response.text}")
            return False
            
    except requests.exceptions.Timeout:
        print("❌ Request timed out after 30s")
        return False
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")
        return False

def test_error_log_format():
    """Test with the expected error log format but minimal content"""
    
    print("\n=== Testing Error Log Format (Minimal) ===")
    print("Sending minimal error log content in expected format")
    
    # Minimal error log content
    error_log_content = "E (12345) SYSTEM: hello world test"
    
    json_payload = {
        "error_log_content": error_log_content,
        "device_id": "AA:BB:CC:DD:EE:FF",
        "client_id": "xiaozhi-device-001"
    }
    
    print(f"Error log content: {error_log_content}")
    print(f"Payload size: {len(json.dumps(json_payload))} bytes")
    
    try:
        start_time = time.time()
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
        
        elapsed_time = time.time() - start_time
        print(f"Response time: {elapsed_time:.2f}s")
        print(f"Response status: {response.status_code}")
        
        if response.status_code == 200:
            try:
                response_data = response.json()
                print("✅ Error log format POST successful!")
                print(f"Response: {json.dumps(response_data, indent=2)}")
                
                # Check if it was processed as error log
                if response_data.get("action") == "error_log_upload":
                    print("✅ SCF recognized this as error log upload")
                else:
                    print("⚠️ SCF did not recognize this as error log upload")
                
                return True
            except json.JSONDecodeError:
                print(f"❌ Response is not valid JSON: {response.text}")
                return False
        else:
            print(f"❌ POST failed with status {response.status_code}")
            print(f"Response: {response.text}")
            return False
            
    except requests.exceptions.Timeout:
        print("❌ Request timed out after 30s")
        return False
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")
        return False

def test_get_endpoint():
    """Test GET request to see if endpoint is responsive"""
    
    print("\n=== Testing GET Request ===")
    print("Testing basic GET request to SCF endpoint")
    
    try:
        start_time = time.time()
        response = requests.get(
            'https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/',
            timeout=30
        )
        
        elapsed_time = time.time() - start_time
        print(f"Response time: {elapsed_time:.2f}s")
        print(f"Response status: {response.status_code}")
        
        if response.status_code == 200:
            try:
                response_data = response.json()
                print("✅ GET request successful!")
                print(f"Response: {json.dumps(response_data, indent=2)}")
                return True
            except json.JSONDecodeError:
                print(f"❌ Response is not valid JSON: {response.text}")
                return False
        else:
            print(f"❌ GET failed with status {response.status_code}")
            print(f"Response: {response.text}")
            return False
            
    except requests.exceptions.Timeout:
        print("❌ Request timed out after 30s")
        return False
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")
        return False

if __name__ == "__main__":
    print("Testing Basic POST Functionality")
    print("Testing against deployed SCF endpoint: https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/")
    print()
    
    # Test 1: Basic POST with minimal data
    basic_success = test_basic_post()
    
    # Test 2: GET request to check endpoint responsiveness
    get_success = test_get_endpoint()
    
    # Test 3: Error log format with minimal content
    error_log_success = test_error_log_format()
    
    print("\n=== Test Summary ===")
    print(f"1. Basic POST: {'✅ PASS' if basic_success else '❌ FAIL'}")
    print(f"2. GET Request: {'✅ PASS' if get_success else '❌ FAIL'}")
    print(f"3. Error Log Format: {'✅ PASS' if error_log_success else '❌ FAIL'}")
    
    if basic_success and get_success:
        print("\n🎉 Basic POST functionality is working!")
        if error_log_success:
            print("🎉 Error log format is also working!")
        else:
            print("⚠️ Error log format needs investigation")
    else:
        print("\n❌ Basic POST functionality has issues - check SCF endpoint configuration")

