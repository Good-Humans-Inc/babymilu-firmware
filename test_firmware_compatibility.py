#!/usr/bin/env python3
"""
Test script to verify server compatibility with firmware HTTP client expectations.
This script simulates the firmware's POST request behavior.
"""

import requests
import json
import time

def test_json_post():
    """Test JSON POST request similar to firmware"""
    url = "https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/"
    
    # Simulate firmware's JSON payload
    payload = {
        "message": "hello world",
        "test": True
    }
    
    headers = {
        "Content-Type": "application/json",
        "User-Agent": "Xiaozhi-Test/1.0",
        "Connection": "close"
    }
    
    print(f"Testing JSON POST to: {url}")
    print(f"Payload: {json.dumps(payload)}")
    print(f"Headers: {headers}")
    
    try:
        start_time = time.time()
        response = requests.post(url, json=payload, headers=headers, timeout=15)
        end_time = time.time()
        
        print(f"Response time: {end_time - start_time:.2f} seconds")
        print(f"Status code: {response.status_code}")
        print(f"Response headers: {dict(response.headers)}")
        print(f"Response content: {response.text}")
        
        if response.status_code == 200:
            print("✅ JSON POST test successful!")
            return True
        else:
            print("❌ JSON POST test failed!")
            return False
            
    except requests.exceptions.Timeout:
        print("❌ Request timed out!")
        return False
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")
        return False

def test_simple_post():
    """Test simple text POST request"""
    url = "https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/simple"
    
    # Simulate firmware's JSON payload
    payload = {
        "message": "hello world",
        "test": True
    }
    
    headers = {
        "Content-Type": "application/json",
        "User-Agent": "Xiaozhi-Test/1.0",
        "Connection": "close"
    }
    
    print(f"\nTesting Simple POST to: {url}")
    print(f"Payload: {json.dumps(payload)}")
    print(f"Headers: {headers}")
    
    try:
        start_time = time.time()
        response = requests.post(url, json=payload, headers=headers, timeout=15)
        end_time = time.time()
        
        print(f"Response time: {end_time - start_time:.2f} seconds")
        print(f"Status code: {response.status_code}")
        print(f"Response headers: {dict(response.headers)}")
        print(f"Response content: {response.text}")
        
        if response.status_code == 200:
            print("✅ Simple POST test successful!")
            return True
        else:
            print("❌ Simple POST test failed!")
            return False
            
    except requests.exceptions.Timeout:
        print("❌ Request timed out!")
        return False
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")
        return False

def test_error_log_upload():
    """Test error log upload functionality"""
    url = "https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/"
    
    # Simulate error log upload
    payload = {
        "error_log_content": "Test error log content\nLine 2\nLine 3",
        "device_id": "test_device_123",
        "client_id": "test_client_456"
    }
    
    headers = {
        "Content-Type": "application/json",
        "User-Agent": "Xiaozhi-Test/1.0",
        "Connection": "close"
    }
    
    print(f"\nTesting Error Log Upload to: {url}")
    print(f"Payload size: {len(json.dumps(payload))} bytes")
    
    try:
        start_time = time.time()
        response = requests.post(url, json=payload, headers=headers, timeout=15)
        end_time = time.time()
        
        print(f"Response time: {end_time - start_time:.2f} seconds")
        print(f"Status code: {response.status_code}")
        print(f"Response headers: {dict(response.headers)}")
        print(f"Response content: {response.text}")
        
        if response.status_code == 200:
            print("✅ Error log upload test successful!")
            return True
        else:
            print("❌ Error log upload test failed!")
            return False
            
    except requests.exceptions.Timeout:
        print("❌ Request timed out!")
        return False
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")
        return False

def main():
    print("=== Firmware Compatibility Test ===")
    print("Testing server responses with firmware-like requests...")
    
    results = []
    
    # Test 1: Basic JSON POST
    results.append(test_json_post())
    
    # Test 2: Simple text POST
    results.append(test_simple_post())
    
    # Test 3: Error log upload
    results.append(test_error_log_upload())
    
    # Summary
    print(f"\n=== Test Summary ===")
    print(f"Total tests: {len(results)}")
    print(f"Successful: {sum(results)}")
    print(f"Failed: {len(results) - sum(results)}")
    
    if all(results):
        print("🎉 All tests passed! Server should be compatible with firmware.")
    else:
        print("⚠️ Some tests failed. Check server configuration.")

if __name__ == "__main__":
    main()

