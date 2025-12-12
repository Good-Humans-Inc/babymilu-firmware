#!/usr/bin/env python3
"""
Test script to verify if the SCF endpoint returns empty responses for POST requests.
This will help us understand if the issue is with the ESP32 or the SCF endpoint.
"""

import requests
import json
import time

def test_scf_post_response():
    """Test POST request to SCF endpoint and check response"""
    
    print("=== Testing SCF POST Response ===")
    print("Testing against deployed SCF endpoint: https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/")
    
    # Test with the exact same payload as ESP32
    json_payload = {
        "message": "hello world",
        "test": True
    }
    
    print(f"Payload: {json.dumps(json_payload)}")
    print(f"Payload size: {len(json.dumps(json_payload))} bytes")
    
    try:
        print("\nSending POST request...")
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
            print("✅ POST request successful!")
            
            # Check response content
            response_text = response.text
            print(f"Response length: {len(response_text)} bytes")
            
            if response_text:
                print("✅ Response has content!")
                try:
                    response_data = response.json()
                    print(f"Response JSON: {json.dumps(response_data, indent=2)}")
                except json.JSONDecodeError:
                    print(f"Response text (not JSON): {response_text}")
            else:
                print("❌ Response is empty - same issue as ESP32!")
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
    
    return True

def test_scf_get_response():
    """Test GET request to compare with POST"""
    
    print("\n=== Testing SCF GET Response ===")
    
    try:
        print("Sending GET request...")
        start_time = time.time()
        
        response = requests.get(
            'https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/',
            timeout=30
        )
        
        elapsed_time = time.time() - start_time
        print(f"Response time: {elapsed_time:.2f}s")
        print(f"Response status: {response.status_code}")
        
        if response.status_code == 200:
            print("✅ GET request successful!")
            
            response_text = response.text
            print(f"Response length: {len(response_text)} bytes")
            
            if response_text:
                print("✅ GET response has content!")
                try:
                    response_data = response.json()
                    print(f"GET Response JSON: {json.dumps(response_data, indent=2)}")
                except json.JSONDecodeError:
                    print(f"GET Response text (not JSON): {response_text}")
            else:
                print("❌ GET response is also empty!")
                
        else:
            print(f"❌ GET failed with status {response.status_code}")
            
    except requests.exceptions.RequestException as e:
        print(f"❌ GET request failed: {e}")

def test_different_payloads():
    """Test with different payload formats to see what works"""
    
    print("\n=== Testing Different Payload Formats ===")
    
    test_cases = [
        {
            "name": "Minimal JSON",
            "payload": {"test": True}
        },
        {
            "name": "Error log format",
            "payload": {
                "error_log_content": "E (12345) SYSTEM: hello world test",
                "device_id": "AA:BB:CC:DD:EE:FF",
                "client_id": "xiaozhi-device-001"
            }
        },
        {
            "name": "Simple string",
            "payload": {"message": "test"}
        },
        {
            "name": "Empty object",
            "payload": {}
        }
    ]
    
    for test_case in test_cases:
        print(f"\n--- Testing {test_case['name']} ---")
        
        try:
            response = requests.post(
                'https://1379890832-lw33xqs7cm.ap-shanghai.tencentscf.com/',
                json=test_case['payload'],
                headers={
                    'Content-Type': 'application/json',
                    'User-Agent': 'Xiaozhi-Test/1.0'
                },
                timeout=15
            )
            
            print(f"Status: {response.status_code}")
            print(f"Response length: {len(response.text)} bytes")
            
            if response.text:
                print(f"Response: {response.text[:200]}...")  # First 200 chars
            else:
                print("❌ Empty response")
                
        except requests.exceptions.RequestException as e:
            print(f"❌ Request failed: {e}")

if __name__ == "__main__":
    print("Testing SCF Endpoint Response Behavior")
    print("This will help us understand if the empty response issue is consistent")
    print()
    
    # Test 1: POST request (same as ESP32)
    post_success = test_scf_post_response()
    
    # Test 2: GET request (for comparison)
    test_scf_get_response()
    
    # Test 3: Different payload formats
    test_different_payloads()
    
    print("\n=== Test Summary ===")
    print("1. POST Request: Tests the exact same payload as ESP32")
    print("2. GET Request: Tests basic connectivity")
    print("3. Different Payloads: Tests various JSON formats")
    
    if post_success:
        print("\n🎉 POST requests work fine from Python!")
        print("The issue might be with ESP32's response reading or SCF configuration.")
    else:
        print("\n❌ POST requests also fail from Python!")
        print("The issue is with the SCF endpoint configuration.")

