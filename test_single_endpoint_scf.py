#!/usr/bin/env python3
"""
Test script for the single-endpoint SCF error log handler
All actions use POST to the root endpoint with different body parameters
"""

import requests
import io

def test_error_log_upload():
    """Test uploading an error log file"""
    
    # Sample error log content
    error_log_content = """E (12345) SYSTEM: Test error message 1
E (12346) WIFI: Connection failed
E (12347) ANIMATION: Failed to load frame
E (12348) SD_CARD: Mount failed
"""
    
    # Create a file-like object
    error_log_file = io.StringIO(error_log_content)
    
    # Prepare the multipart form data
    files = {
        'error_log': ('err.txt', error_log_file, 'text/plain')
    }
    
    # SCF endpoint URL
    scf_url = "http://localhost:9000/"
    
    try:
        print(f"Testing error log upload to: {scf_url}")
        print(f"Error log content:\n{error_log_content}")
        
        # Send POST request
        response = requests.post(scf_url, files=files, timeout=30)
        
        print(f"\nResponse Status Code: {response.status_code}")
        print(f"Response Content: {response.text}")
        
        if response.status_code == 200:
            print("✅ Upload successful!")
        else:
            print("❌ Upload failed!")
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")
    except Exception as e:
        print(f"❌ Unexpected error: {e}")

def test_get_logs():
    """Test getting logs using action parameter"""
    
    scf_url = "http://localhost:9000/"
    
    try:
        print(f"\nTesting get logs action: {scf_url}")
        
        # Send POST request with action parameter
        data = {'action': 'get_logs'}
        response = requests.post(scf_url, data=data, timeout=10)
        
        print(f"Response Status Code: {response.status_code}")
        print(f"Response Content: {response.text}")
        
        if response.status_code == 200:
            print("✅ Get logs successful!")
        else:
            print("❌ Get logs failed!")
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")

def test_clear_logs():
    """Test clearing logs using action parameter"""
    
    scf_url = "http://localhost:9000/"
    
    try:
        print(f"\nTesting clear logs action: {scf_url}")
        
        # Send POST request with action parameter
        data = {'action': 'clear'}
        response = requests.post(scf_url, data=data, timeout=10)
        
        print(f"Response Status Code: {response.status_code}")
        print(f"Response Content: {response.text}")
        
        if response.status_code == 200:
            print("✅ Clear logs successful!")
        else:
            print("❌ Clear logs failed!")
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")

def test_get_endpoint():
    """Test the GET endpoint"""
    
    scf_url = "http://localhost:9000/"
    
    try:
        print(f"\nTesting GET endpoint: {scf_url}")
        response = requests.get(scf_url, timeout=10)
        
        print(f"Response Status Code: {response.status_code}")
        print(f"Response Content: {response.text}")
        
        if response.status_code == 200:
            print("✅ GET endpoint working!")
        else:
            print("❌ GET endpoint failed!")
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Request failed: {e}")

if __name__ == "__main__":
    print("=== Single-Endpoint SCF Error Log Handler Test ===")
    
    # Test sequence
    test_error_log_upload()
    test_get_logs()
    test_clear_logs()
    test_get_logs()  # Should show no logs after clear
    test_get_endpoint()
    
    print("\n=== Test Complete ===")
    print("\nUsage Summary:")
    print("1. Upload error log: POST with 'error_log' file")
    print("2. Get logs: POST with 'action=get_logs'")
    print("3. Clear logs: POST with 'action=clear'")
    print("4. Get endpoint: GET request")
    print("\nTo run your SCF server:")
    print("python scf_error_log_handler.py")
    print("\nThen run this test script:")
    print("python test_single_endpoint_scf.py")

