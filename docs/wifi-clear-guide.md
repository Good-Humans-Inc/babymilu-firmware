# WiFi Configuration Clearing Guide

This guide explains how to erase WiFi configuration from the Xiaozhi device's memory.

## Overview

The Xiaozhi device stores WiFi credentials in NVS (Non-Volatile Storage) flash memory. When you want to completely erase all WiFi configuration and force the device to enter WiFi configuration mode, you can use the methods provided in this implementation.

## Methods to Clear WiFi Configuration

### 1. Programmatic Method (Recommended)

You can clear WiFi configuration programmatically using the following methods:

#### From Application Code:
```cpp
#include "application.h"

// Clear WiFi configuration
auto& app = Application::GetInstance();
app.ClearWifiConfiguration();
```

#### From Board Code:
```cpp
#include "board.h"

// Clear WiFi configuration
auto& board = Board::GetInstance();
board.ClearWifiConfiguration();
```

#### From WiFi Board Specifically:
```cpp
#include "boards/common/wifi_board.h"

// If you have a WifiBoard instance
WifiBoard wifi_board;
wifi_board.ClearWifiConfiguration();
```

### 2. MCP Server Tool

The device now includes an MCP (Model Context Protocol) tool that can be called remotely:

**Tool Name:** `self.wifi.clear_configuration`

**Description:** Clear all WiFi configuration and credentials from the device memory.

**Parameters:** None required

**Usage Example:**
```json
{
  "tool": "self.wifi.clear_configuration",
  "arguments": {}
}
```

### 3. Direct NVS Manipulation

For advanced users, you can directly manipulate the NVS storage:

```cpp
#include "settings.h"

// Clear WiFi namespace
{
    Settings settings("wifi", true);
    settings.EraseAll();
}

// Clear websocket settings
{
    Settings settings("websocket", true);
    settings.EraseAll();
}

// Clear SSID manager data
#include "ssid_manager.h"
auto& ssid_manager = SsidManager::GetInstance();
ssid_manager.Clear();
```

## What Gets Cleared

When you call the WiFi clearing methods, the following data is erased:

1. **WiFi Credentials**: All stored SSID and password pairs
2. **WiFi Settings**: Any additional WiFi-related configuration
3. **WebSocket Settings**: WebSocket connection URLs and settings
4. **Force AP Flag**: Resets the force access point mode flag

## Storage Details

WiFi credentials are stored in NVS flash with the following structure:

- **Namespace**: `wifi`
- **Keys**: 
  - `ssid`, `ssid1`, `ssid2`, ..., `ssid9` (up to 10 networks)
  - `password`, `password1`, `password2`, ..., `password9`
- **Additional Settings**: `force_ap` flag

## After Clearing WiFi Configuration

After clearing the WiFi configuration:

1. The device will no longer have any stored WiFi credentials
2. On the next boot, the device will automatically enter WiFi configuration mode
3. The device will create a WiFi access point (AP) for configuration
4. You can connect to the AP and configure new WiFi settings through the web interface

## Implementation Details

### Files Modified

1. **`main/boards/common/board.h`**: Added virtual `ClearWifiConfiguration()` method
2. **`main/boards/common/wifi_board.h`**: Added `ClearWifiConfiguration()` declaration
3. **`main/boards/common/wifi_board.cc`**: Implemented `ClearWifiConfiguration()` method
4. **`main/application.h`**: Added `ClearWifiConfiguration()` method
5. **`main/application.cc`**: Implemented application-level WiFi clearing
6. **`main/mcp_server.cc`**: Added MCP tool for WiFi clearing

### Key Components

- **SsidManager**: Manages WiFi credential storage and provides `Clear()` method
- **Settings**: Provides NVS manipulation with `EraseAll()` method
- **WifiBoard**: Main WiFi board implementation with clearing functionality
- **Application**: High-level application interface for WiFi clearing
- **McpServer**: Remote tool interface for WiFi management

## Usage Examples

### Example 1: Clear WiFi on Button Press

```cpp
#include "button.h"
#include "application.h"

class MyBoard : public WifiBoard {
private:
    Button clear_wifi_button_;

public:
    MyBoard() : clear_wifi_button_(GPIO_NUM_0) {
        clear_wifi_button_.OnPress([this]() {
            // Hold button for 5 seconds to clear WiFi
            if (clear_wifi_button_.GetPressDuration() > 5000) {
                Application::GetInstance().ClearWifiConfiguration();
            }
        });
    }
};
```

### Example 2: Clear WiFi via MCP

```python
# Python example using MCP client
import json

def clear_wifi_configuration():
    message = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": "self.wifi.clear_configuration",
            "arguments": {}
        }
    }
    return json.dumps(message)
```

## Troubleshooting

### Device Still Remembers WiFi

If the device still remembers WiFi after clearing:

1. **Check NVS Storage**: Verify that the NVS flash was properly cleared
2. **Reboot Device**: Ensure the device is rebooted after clearing
3. **Check Implementation**: Verify that all clearing methods are called
4. **Manual NVS Erase**: Use `nvs_flash_erase()` for complete NVS reset

### Device Doesn't Enter Configuration Mode

If the device doesn't enter WiFi configuration mode:

1. **Check Force AP Flag**: Ensure `force_ap` is set to 1
2. **Verify SSID List**: Check that `SsidManager::GetSsidList()` returns empty
3. **Check Boot Sequence**: Verify the WiFi board initialization logic

## Security Considerations

- **Complete Erasure**: The clearing methods completely remove all WiFi credentials
- **No Recovery**: Once cleared, WiFi credentials cannot be recovered
- **Immediate Effect**: Changes take effect immediately (no reboot required for clearing)
- **Configuration Mode**: Device will enter configuration mode on next boot

## Conclusion

The WiFi clearing functionality provides a comprehensive way to reset the device's network configuration. Whether you need to clear WiFi for security reasons, troubleshooting, or simply want to reconfigure the device, the provided methods offer flexible and reliable solutions.














