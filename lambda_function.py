#!/usr/bin/env python3
"""
AWS Lambda function for Xiaozhi Animation Updater
Handles device registration and returns GitHub download links
"""

import json
import urllib.parse
from datetime import datetime

# Available GitHub links for animations
ANIMATION_POOL = {
    "normal1.bin": {
        "github_url": "https://github.com/Jackson-hangxuan/postman_test/raw/refs/heads/main/normal1.bin",
        "size": 131096,
        "version": "1.0"
    },
    "normal2.bin": {
        "github_url": "https://github.com/Jackson-hangxuan/lambda_test_1/raw/refs/heads/main/normal1.bin", 
        "size": 128432,
        "version": "1.0"
    }
}

# In-memory device registry (in production, use DynamoDB or RDS)
device_registry = {}

# Track which animations are assigned to which devices
animation_assignments = {}

def lambda_handler(event, context):
    """
    AWS Lambda handler for animation update requests
    """
    try:
        # Extract HTTP method and path
        http_method = event.get('httpMethod', 'GET')
        path = event.get('path', '')
        query_params = event.get('queryStringParameters') or {}
        
        print(f"[{datetime.now().strftime('%H:%M:%S')}] {http_method} {path}")
        
        # Handle different endpoints
        if path == '/api/animations/check':
            return handle_check_updates(query_params)
        else:
            return create_error_response(404, "Not Found")
            
    except Exception as e:
        print(f"Error processing request: {str(e)}")
        return create_error_response(500, f"Internal Server Error: {str(e)}")

def handle_check_updates(query_params):
    """
    Handle /api/animations/check endpoint
    Register device and assign unique animation with GitHub link
    """
    device_id = query_params.get('device_id', 'unknown')
    version = query_params.get('version', '1.0')
    
    print(f"  Device ID: {device_id}, Version: {version}")
    
    # Register device with timestamp
    device_registry[device_id] = {
        'first_seen': datetime.now().isoformat(),
        'last_seen': datetime.now().isoformat(),
        'version': version,
        'request_count': device_registry.get(device_id, {}).get('request_count', 0) + 1
    }
    
    # Check if device already has an assigned animation
    if device_id in animation_assignments:
        # Device already registered, return their assigned animation
        assigned_filename = animation_assignments[device_id]
        assigned_info = ANIMATION_POOL[assigned_filename]
        
        animations = [{
            "filename": assigned_filename,
            "download_url": assigned_info["github_url"],
            "size": assigned_info["size"],
            "version": assigned_info["version"]
        }]
        
        print(f"  Device {device_id} already registered, returning assigned animation: {assigned_filename}")
    else:
        # New device - assign first available animation
        available_animations = [filename for filename in ANIMATION_POOL.keys() 
                              if filename not in animation_assignments.values()]
        
        if not available_animations:
            # No animations available
            animations = []
            print(f"  No animations available for new device {device_id}")
        else:
            # Assign first available animation
            assigned_filename = available_animations[0]
            animation_assignments[device_id] = assigned_filename
            assigned_info = ANIMATION_POOL[assigned_filename]
            
            animations = [{
                "filename": assigned_filename,
                "download_url": assigned_info["github_url"],
                "size": assigned_info["size"],
                "version": assigned_info["version"]
            }]
            
            print(f"  Assigned animation {assigned_filename} to device {device_id}")
    
    # Create response
    response = {
        "has_updates": len(animations) > 0,
        "animations": animations,
        "server_time": datetime.now().isoformat(),
        "device_id": device_id,
        "current_version": version,
        "registered_devices": len(device_registry),
        "assigned_animations": len(animation_assignments)
    }
    
    print(f"  Response: {len(animations)} animations available for device {device_id}")
    print(f"  Total registered devices: {len(device_registry)}")
    print(f"  Animation assignments: {animation_assignments}")
    
    return create_json_response(200, response)


def create_json_response(status_code, data):
    """
    Create a JSON response with proper headers
    """
    return {
        'statusCode': status_code,
        'headers': {
            'Content-Type': 'application/json',
            'Access-Control-Allow-Origin': '*',
            'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
            'Access-Control-Allow-Headers': 'Content-Type, Device-Id, Client-Id, User-Agent',
            'Cache-Control': 'no-cache'
        },
        'body': json.dumps(data, indent=2)
    }

def create_error_response(status_code, message):
    """
    Create an error response
    """
    return {
        'statusCode': status_code,
        'headers': {
            'Content-Type': 'application/json',
            'Access-Control-Allow-Origin': '*'
        },
        'body': json.dumps({
            "error": message,
            "status_code": status_code,
            "timestamp": datetime.now().isoformat()
        })
    }

# For local testing
if __name__ == "__main__":
    # Test the function locally
    test_event = {
        'httpMethod': 'GET',
        'path': '/api/animations/check',
        'queryStringParameters': {
            'device_id': '8c:bf:ea:8f:39:b8',
            'version': '1.0'
        }
    }
    
    result = lambda_handler(test_event, None)
    print("Test Result:")
    print(json.dumps(result, indent=2))

"""
TEST CASES FOR AWS LAMBDA CONSOLE:

Test Case 1: First Device Registration
{
  "httpMethod": "GET",
  "path": "/api/animations/check",
  "queryStringParameters": {
    "device_id": "8c:bf:ea:8f:39:b8",
    "version": "1.0"
  },
  "headers": {
    "Content-Type": "application/json",
    "User-Agent": "Xiaozhi-Animation/1.0"
  },
  "body": null,
  "isBase64Encoded": false
}
Expected: Status 200, normal1.bin assigned to device

Test Case 2: Second Device Registration
{
  "httpMethod": "GET",
  "path": "/api/animations/check",
  "queryStringParameters": {
    "device_id": "aa:bb:cc:dd:ee:ff",
    "version": "1.0"
  },
  "headers": {
    "Content-Type": "application/json",
    "User-Agent": "Xiaozhi-Animation/1.0"
  },
  "body": null,
  "isBase64Encoded": false
}
Expected: Status 200, normal2.bin assigned to device

Test Case 3: Third Device (No Animations Available)
{
  "httpMethod": "GET",
  "path": "/api/animations/check",
  "queryStringParameters": {
    "device_id": "11:22:33:44:55:66",
    "version": "1.0"
  },
  "headers": {
    "Content-Type": "application/json",
    "User-Agent": "Xiaozhi-Animation/1.0"
  },
  "body": null,
  "isBase64Encoded": false
}
Expected: Status 200, has_updates: false, empty animations array

Test Case 4: First Device Re-request (Should Get Same Animation)
{
  "httpMethod": "GET",
  "path": "/api/animations/check",
  "queryStringParameters": {
    "device_id": "8c:bf:ea:8f:39:b8",
    "version": "1.0"
  },
  "headers": {
    "Content-Type": "application/json",
    "User-Agent": "Xiaozhi-Animation/1.0"
  },
  "body": null,
  "isBase64Encoded": false
}
Expected: Status 200, normal1.bin (same as before)

Test Case 5: Missing Device ID
{
  "httpMethod": "GET",
  "path": "/api/animations/check",
  "queryStringParameters": {
    "version": "1.0"
  },
  "headers": {
    "Content-Type": "application/json"
  },
  "body": null,
  "isBase64Encoded": false
}
Expected: Status 200, device_id: "unknown", gets first available animation

Test Case 6: Invalid Endpoint
{
  "httpMethod": "GET",
  "path": "/api/animations/invalid",
  "queryStringParameters": null,
  "headers": {
    "User-Agent": "Xiaozhi-Animation/1.0"
  },
  "body": null,
  "isBase64Encoded": false
}
Expected: Status 404, Error message "Not Found"

EXECUTION ORDER:
1. Run Test Case 1 → Device A gets normal1.bin
2. Run Test Case 2 → Device B gets normal2.bin  
3. Run Test Case 3 → Device C gets no updates
4. Run Test Case 4 → Device A gets same normal1.bin again
5. Run Test Case 5 → Unknown device gets first available (if any)
6. Run Test Case 6 → Invalid endpoint returns 404
"""
