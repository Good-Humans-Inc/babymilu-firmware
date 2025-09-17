#!/usr/bin/env python3
"""
Test script for uploading animation files to the device
"""

import requests
import json
import sys
import os

def upload_file(device_ip, filename, file_path):
    """Upload a file to the device"""
    url = f"http://{device_ip}:8080/upload"
    params = {"filename": filename}
    
    try:
        with open(file_path, 'rb') as f:
            response = requests.post(url, params=params, data=f)
        
        if response.status_code == 200:
            result = response.json()
            print(f"✅ Upload successful: {filename}")
            print(f"   Response: {result}")
            return True
        else:
            print(f"❌ Upload failed: {filename}")
            print(f"   Status: {response.status_code}")
            print(f"   Response: {response.text}")
            return False
            
    except Exception as e:
        print(f"❌ Upload error: {filename}")
        print(f"   Error: {e}")
        return False

def delete_file(device_ip, filename):
    """Delete a file from the device"""
    url = f"http://{device_ip}:8080/delete"
    params = {"filename": filename}
    
    try:
        response = requests.delete(url, params=params)
        
        if response.status_code == 200:
            result = response.json()
            print(f"✅ Delete successful: {filename}")
            print(f"   Response: {result}")
            return True
        else:
            print(f"❌ Delete failed: {filename}")
            print(f"   Status: {response.status_code}")
            print(f"   Response: {response.text}")
            return False
            
    except Exception as e:
        print(f"❌ Delete error: {filename}")
        print(f"   Error: {e}")
        return False

def list_files(device_ip):
    """List files on the device"""
    url = f"http://{device_ip}:8080/list"
    
    try:
        response = requests.get(url)
        
        if response.status_code == 200:
            manifest = response.json()
            print("📁 Files on device:")
            print(json.dumps(manifest, indent=2))
            return manifest
        else:
            print(f"❌ List failed")
            print(f"   Status: {response.status_code}")
            print(f"   Response: {response.text}")
            return None
            
    except Exception as e:
        print(f"❌ List error: {e}")
        return None

def main():
    if len(sys.argv) < 2:
        print("Usage: python test_upload.py <device_ip> [command] [args...]")
        print("Commands:")
        print("  upload <filename> <file_path>  - Upload a file")
        print("  delete <filename>              - Delete a file")
        print("  list                           - List files")
        print("  test                           - Run test sequence")
        print("")
        print("Examples:")
        print("  python test_upload.py 192.168.1.100 list")
        print("  python test_upload.py 192.168.1.100 upload test.bin test_animation.bin")
        print("  python test_upload.py 192.168.1.100 test")
        sys.exit(1)
    
    device_ip = sys.argv[1]
    command = sys.argv[2] if len(sys.argv) > 2 else "test"
    
    print(f"🔗 Connecting to device: {device_ip}:8080")
    
    if command == "list":
        list_files(device_ip)
        
    elif command == "upload":
        if len(sys.argv) < 5:
            print("❌ Upload command requires filename and file_path")
            sys.exit(1)
        filename = sys.argv[3]
        file_path = sys.argv[4]
        upload_file(device_ip, filename, file_path)
        
    elif command == "delete":
        if len(sys.argv) < 4:
            print("❌ Delete command requires filename")
            sys.exit(1)
        filename = sys.argv[3]
        delete_file(device_ip, filename)
        
    elif command == "test":
        print("🧪 Running test sequence...")
        
        # Create test files
        print("\n1. Creating test animation files...")
        os.system("python scripts/create_test_animation.py test1.bin 64 64 3")
        os.system("python scripts/create_test_animation.py test2.bin 32 32 2")
        
        # List initial files
        print("\n2. Listing initial files...")
        list_files(device_ip)
        
        # Upload test files
        print("\n3. Uploading test files...")
        upload_file(device_ip, "test1.bin", "test1.bin")
        upload_file(device_ip, "test2.bin", "test2.bin")
        
        # List files after upload
        print("\n4. Listing files after upload...")
        list_files(device_ip)
        
        # Delete one file
        print("\n5. Deleting test1.bin...")
        delete_file(device_ip, "test1.bin")
        
        # List files after deletion
        print("\n6. Listing files after deletion...")
        list_files(device_ip)
        
        # Clean up local files
        print("\n7. Cleaning up local test files...")
        if os.path.exists("test1.bin"):
            os.remove("test1.bin")
        if os.path.exists("test2.bin"):
            os.remove("test2.bin")
        
        print("\n✅ Test sequence completed!")
        
    else:
        print(f"❌ Unknown command: {command}")
        sys.exit(1)

if __name__ == "__main__":
    main()
