# SCF Lambda Function

A Flask-based web service that manages device registrations and URL assignments for firmware updates.

## Overview

This service provides a simple device registration system that assigns firmware URLs to devices in a round-robin fashion. It supports multiple actions including device registration, status checking, and system reset.

## API Endpoints

### Base URL
```
https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com
```

All requests are made to the root endpoint with query parameters.

## Request Types

### 1. Device Registration

**Purpose**: Register a new device and get assigned a firmware URL.

**Registration**: At this time allows at most 2 devices to register

**Parameters**:
- `device_id` (required): Unique identifier for the device

**Examples**:

#### First Device Registration
```
https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?device_id=XX:XX
```
**Response**: Returns static .bin 1
```
https://gitee.com/xie-hangxuan/test/raw/master/normal1.bin
```

#### Second Device Registration
```
https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?device_id=YY:YY
```
**Response**: Returns static .bin 2
```
https://gitee.com/xie-hangxuan/test/raw/master/temp/normal1.bin
```

#### Third Device Registration (No URLs Available)
```
https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?device_id=ZZ:ZZ
```
**Response**: No static URLs available, return unavailable string
```
url unavailable
```

#### Empty Device ID
```
https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?device_id=
```
**Response**: 
```
No device_id provided
```

#### Already Registered Device
```
https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?device_id=XX:XX
```
**Response**: Returns the previously assigned URL for that device

### 2. Status Check

**Purpose**: Get system status information including registered devices and URL usage.

**Parameters**:
- `action=status`

**Example**:
```
https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?action=status
```

**Response**: Return a string of registered status
```
Status Report:
Total Devices: 2
URLs Used: 2
URLs Available: 0
Registered Devices: XX:XX, YY:YY
```

### 3. System Reset

**Purpose**: Manually reset the lambda and you could restart registration.

**Parameters**:
- `action=reset`

**Example**:
```
https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?action=reset
```

**Response**:
```
Registrations reset
```

## Available Firmware URLs

The service cycles through the following static URLs:

1. `https://gitee.com/xie-hangxuan/test/raw/master/normal1.bin`
2. `https://gitee.com/xie-hangxuan/test/raw/master/temp/normal1.bin`

**⚠️ Important Limitation**: Only 2 URLs are available. The third device registration will return "url unavailable".

## Error Handling

### Invalid Action
```
https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?action=invalid_action
```

**Response**:
```
Invalid action. Use: register, status, or reset
```

## Usage Examples

### Complete Workflow

1. **Register first device**:
   ```
   https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?device_id=XX:XX
   Response: https://gitee.com/xie-hangxuan/test/raw/master/normal1.bin
   ```

2. **Register second device**:
   ```
   https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?device_id=YY:YY
   Response: https://gitee.com/xie-hangxuan/test/raw/master/temp/normal1.bin
   ```

3. **Check status**:
   ```
   https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?action=status
   Response: Status Report: Total Devices: 2, URLs Used: 2, URLs Available: 0, Registered Devices: XX:XX, YY:YY
   ```

4. **Try to register third device** (no URLs available - only 2 URLs exist):
   ```
   https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?device_id=ZZ:ZZ
   Response: url unavailable
   ```

5. **Reset system**:
   ```
   https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?action=reset
   Response: Registrations reset
   ```

6. **Register device after reset**:
   ```
   https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com/?device_id=AA:AA
   Response: https://gitee.com/xie-hangxuan/test/raw/master/normal1.bin
   ```

## Server Configuration

- **Host**: 0.0.0.0 (all interfaces)
- **Port**: 9000
- **Framework**: Flask

## Running the Service

```bash
python scf_lambda.py
```

The service will be available at `http://localhost:9000/`

## Notes

- Device IDs are case-sensitive
- Once a device is registered, it will always receive the same URL
- The service maintains state in memory (not persistent across restarts)
- URL assignment follows a round-robin pattern
- **Maximum number of devices is limited to 2** (only 2 URLs are available)
- The third device registration will always return "url unavailable"