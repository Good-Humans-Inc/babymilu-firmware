# BLE WiFi Setup Guide

This guide explains how to use the BLE (Bluetooth Low Energy) WiFi configuration feature.

## How It Works

1. **Device starts BLE server** with name "Xiaozhi-WiFi"
2. **Client connects** via Bluetooth Low Energy
3. **Client sends WiFi credentials** in specific format
4. **Device saves credentials** and connects to WiFi

## BLE Connection

### Device Name
- **BLE Device Name**: `Xiaozhi-WiFi`
- **Service UUID**: `0x180` (Generic Access)
- **Read Characteristic**: `0xFEF4` (for status messages)
- **Write Characteristic**: `0xDEAD` (for sending data)

## Data Format

Send WiFi credentials using these formats:

### Method 1: Separate SSID and Password
```
ssid:YOUR_WIFI_NAME
pwd:YOUR_WIFI_PASSWORD
```

### Method 2: Combined Format (Recommended)
```
wifi:YOUR_WIFI_NAME:YOUR_WIFI_PASSWORD
```

## Example Client Code (Python)

```python
import asyncio
from bleak import BleakClient, BleakScanner

async def configure_wifi():
    # Scan for devices
    devices = await BleakScanner.discover()
    xiaozhi_device = None
    
    for device in devices:
        if device.name == "Xiaozhi-WiFi":
            xiaozhi_device = device
            break
    
    if not xiaozhi_device:
        print("Xiaozhi device not found")
        return
    
    # Connect to device
    async with BleakClient(xiaozhi_device.address) as client:
        print(f"Connected to {xiaozhi_device.name}")
        
        # Read initial status
        status = await client.read_gatt_char("0000fef4-0000-1000-8000-00805f9b34fb")
        print(f"Status: {status.decode()}")
        
        # Send WiFi credentials (combined format)
        wifi_ssid = "YOUR_WIFI_NAME"
        wifi_password = "YOUR_WIFI_PASSWORD"
        credentials = f"wifi:{wifi_ssid}:{wifi_password}"
        
        await client.write_gatt_char("0000dead-0000-1000-8000-00805f9b34fb", 
                                   credentials.encode())
        
        # Read final status
        status = await client.read_gatt_char("0000fef4-0000-1000-8000-00805f9b34fb")
        print(f"Status: {status.decode()}")

# Run the example
asyncio.run(configure_wifi())
```

## Example Client Code (JavaScript/Node.js)

```javascript
const { BleakClient } = require('bleak');

async function configureWifi() {
    const client = new BleakClient();
    
    try {
        // Scan for devices
        const devices = await client.scan();
        const xiaozhiDevice = devices.find(d => d.name === 'Xiaozhi-WiFi');
        
        if (!xiaozhiDevice) {
            console.log('Xiaozhi device not found');
            return;
        }
        
        // Connect to device
        await client.connect(xiaozhiDevice.address);
        console.log('Connected to Xiaozhi-WiFi');
        
        // Send WiFi credentials
        const wifiSsid = 'YOUR_WIFI_NAME';
        const wifiPassword = 'YOUR_WIFI_PASSWORD';
        const credentials = `wifi:${wifiSsid}:${wifiPassword}`;
        
        await client.writeCharacteristic('0000dead-0000-1000-8000-00805f9b34fb', 
                                       Buffer.from(credentials));
        
        console.log('WiFi credentials sent');
        
    } finally {
        await client.disconnect();
    }
}

configureWifi();
```

## Status Messages

The device will send these status messages:

- `"Ready for WiFi configuration"` - Initial state
- `"SSID received, send password"` - After SSID is received
- `"Password received, connecting..."` - After password is received
- `"WiFi credentials saved, connecting..."` - After successful save

## Troubleshooting

### Common Issues

1. **Device not found**: Ensure the device is powered on and BLE is enabled
2. **Connection failed**: Check if another device is already connected
3. **WiFi connection failed**: Verify the SSID and password are correct

### Debug Logs

Enable debug logging to see BLE communication:

```
CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y
```

## Integration Notes

- BLE server starts automatically when WifiBoard is created
- Device advertises as "Xiaozhi-WiFi" 
- BLE runs alongside normal WiFi functionality
- Credentials are saved using the existing SSID manager
- Device will attempt to connect to WiFi after receiving credentials
