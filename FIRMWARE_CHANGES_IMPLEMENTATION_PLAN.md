# Firmware Changes Implementation Plan

This document outlines the implementation plan for three firmware changes to fix MQTT/WebSocket connection issues.

## Overview

The changes address:
1. **MAC Address Normalization**: Prevent topic mismatch between firmware and server
2. **MQTT Reconnect Cleanup**: Prevent double CONNECT race conditions on EOF
3. **WebSocket Duplicate Start Prevention**: Prevent WS flood and malformed CONNECT

---

## Change #1: Normalize MAC Address Before Using in Topics

### Problem
MAC address may be in mixed case, causing topic mismatch with server that expects lowercase.

### Current Implementation
- **Location**: `main/application.cc:791-793`
- **Current Code**:
  ```cpp
  auto mac = SystemInfo::GetMacAddress();
  char up_topic[128];
  snprintf(up_topic, sizeof(up_topic), "xiaozhi/%s/up", mac.c_str());
  ```
- **Note**: `SystemInfo::GetMacAddress()` already returns lowercase (uses `%02x` format), but we should normalize explicitly to ensure consistency.

### Implementation Plan

#### Step 1: Add MAC Normalization Helper Function
**File**: `main/system_info.cc` or `main/system_info.h`

Add a helper function to normalize MAC address:
```cpp
// In system_info.h (add to SystemInfo class)
static std::string GetMacAddressNormalized();

// In system_info.cc
std::string SystemInfo::GetMacAddressNormalized() {
    std::string mac = GetMacAddress();
    std::transform(mac.begin(), mac.end(), mac.begin(), ::tolower);
    return mac;
}
```

**Alternative (Simpler)**: Since `GetMacAddress()` already returns lowercase, we can just ensure normalization at usage points.

#### Step 2: Normalize MAC in Topic Construction
**File**: `main/application.cc:791-793`

**Change**:
```cpp
auto mac = SystemInfo::GetMacAddress();
// Normalize to lowercase to ensure consistency with server
std::transform(mac.begin(), mac.end(), mac.begin(), ::tolower);
char up_topic[128];
snprintf(up_topic, sizeof(up_topic), "xiaozhi/%s/up", mac.c_str());
```

**Required Includes**:
- Add `#include <algorithm>` if not already present

#### Step 3: Verify All MAC Usage Points
**Files to Check**:
- `main/application.cc:791-797` - MQTT topic construction ✅ (primary location)
- `main/protocols/websocket_protocol.cc:171` - WebSocket URL query params
- `main/animation/animation_updater.cc:218-220` - Animation URL (already handles uppercase conversion)

**Action**: Ensure all places that use MAC in MQTT topics normalize it.

### Testing
1. Verify MAC is lowercase in MQTT topic logs
2. Verify subscription succeeds (no ACL mismatch)
3. Verify server can publish to device topic

---

## Change #2: Reconnect Cleanly on EOF

### Problem
On EOF/disconnect, firmware tries to reconnect too fast → double CONNECT → malformed CONNECT packet.

### Current Implementation
- **Location**: `main/protocols/mqtt_protocol.cc:106-108`
- **Current Code**:
  ```cpp
  mqtt_->OnDisconnected([this]() {
      ESP_LOGI(TAG, "Disconnected from endpoint");
  });
  ```
- **Issue**: No cleanup or delay before reconnection attempt.

### Implementation Plan

#### Step 1: Enhance OnDisconnected Handler
**File**: `main/protocols/mqtt_protocol.cc:106-108`

**Change**:
```cpp
mqtt_->OnDisconnected([this]() {
    ESP_LOGI(TAG, "Disconnected from endpoint");
    
    // Clean disconnect to prevent double CONNECT race
    if (mqtt_ != nullptr) {
        // Ensure clean disconnect state
        // Note: mqtt_->Disconnect() may not be needed if already disconnected
        // but we ensure cleanup
    }
    
    // Add delay to prevent immediate reconnection race
    // Schedule reconnection attempt after delay
    vTaskDelay(pdMS_TO_TICKS(200));  // 200ms delay
    
    // Attempt reconnection (if still needed)
    // Note: Reconnection logic may be handled elsewhere (e.g., application layer)
    // This is a safety measure to prevent immediate reconnect
});
```

**Considerations**:
- Need to check if there's existing reconnection logic in `application.cc` or elsewhere
- May need to coordinate with application-level reconnection
- Should not reconnect if protocol is being shut down

#### Step 2: Check for Existing Reconnection Logic
**Search for**: 
- `StartMqttClient` calls after disconnect
- Application-level reconnection handlers
- Background task reconnection

**Action**: If reconnection is handled elsewhere, modify that logic instead.

#### Step 3: Add Reconnection Flag/State
**File**: `main/protocols/mqtt_protocol.h`

**Add member variable**:
```cpp
private:
    bool reconnecting_ = false;  // Prevent multiple simultaneous reconnection attempts
```

**Modify OnDisconnected**:
```cpp
mqtt_->OnDisconnected([this]() {
    ESP_LOGI(TAG, "Disconnected from endpoint");
    
    // Prevent double reconnection
    if (reconnecting_) {
        ESP_LOGW(TAG, "Reconnection already in progress, skipping");
        return;
    }
    
    reconnecting_ = true;
    
    // Clean disconnect
    // Note: mqtt_ object may already be in disconnected state
    
    // Delay before reconnection attempt
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Attempt reconnection
    // Schedule on application thread to avoid blocking
    Application::GetInstance().Schedule([this]() {
        if (mqtt_ != nullptr && !mqtt_->IsConnected()) {
            ESP_LOGI(TAG, "Attempting MQTT reconnection after disconnect");
            StartMqttClient(false);  // Don't report error on reconnect
        }
        reconnecting_ = false;
    });
});
```

### Alternative Approach (Simpler)
If reconnection is handled at application level, just add delay in OnDisconnected:

```cpp
mqtt_->OnDisconnected([this]() {
    ESP_LOGI(TAG, "Disconnected from endpoint");
    // Delay to prevent double CONNECT race
    vTaskDelay(pdMS_TO_TICKS(200));
    // Reconnection will be handled by application layer
});
```

### Testing
1. Simulate network disconnect (kill broker connection)
2. Verify no double CONNECT in logs
3. Verify clean reconnection after delay
4. Verify no malformed CONNECT packets

---

## Change #3: Ignore ws_start if WebSocket Already Connected

### Problem
Multiple `ws_start` messages can trigger multiple WebSocket connection attempts → WS flood → malformed CONNECT.

### Current Implementation
- **Location**: 
  - `main/protocols/mqtt_protocol.cc:168-200` - `ws_start` message handler
  - `main/application.cc:1721-1726` - `OpenWebSocketConnection()` already has a check
- **Current Code in mqtt_protocol.cc**:
  ```cpp
  } else if (strcmp(type->valuestring, "ws_start") == 0) {
      // Server is redirecting to WebSocket
      server_requested_websocket_ = true;
      auto wss_url = cJSON_GetObjectItem(root, "wss");
      // ... URL validation ...
      Application::GetInstance().Schedule([this]() {
          Application::GetInstance().OpenWebSocketConnection();
      });
  }
  ```
- **Current Code in application.cc**:
  ```cpp
  void Application::OpenWebSocketConnection() {
      if (websocket_protocol_ && websocket_protocol_->IsAudioChannelOpened()) {
          ESP_LOGI(TAG, "WebSocket connection already open");
          return;
      }
      // ... rest of connection logic ...
  }
  ```

### Implementation Plan

#### Step 1: Enhance ws_start Handler Check
**File**: `main/protocols/mqtt_protocol.cc:168-200`

**Change**: Add check before scheduling WebSocket connection:

```cpp
} else if (strcmp(type->valuestring, "ws_start") == 0) {
    // Server is redirecting to WebSocket
    server_requested_websocket_ = true;
    auto wss_url = cJSON_GetObjectItem(root, "wss");
    auto version = cJSON_GetObjectItem(root, "version");
    
    if (cJSON_IsString(wss_url)) {
        std::string url = wss_url->valuestring;
        ESP_LOGI(TAG, "Server requests WebSocket connection: %s", url.c_str());
        
        // Validate URL before saving
        if (!IsValidWebSocketUrl(url)) {
            ESP_LOGW(TAG, "Invalid WebSocket URL received (localhost/invalid): %s, will use default URL instead", url.c_str());
            // Don't save invalid URL, let WebSocket protocol use default
        } else {
            // Save WebSocket URL and version to settings
            Settings ws_settings("websocket", true);
            ws_settings.SetString("url", url);
            if (cJSON_IsNumber(version)) {
                ws_settings.SetInt("version", version->valueint);
                ESP_LOGI(TAG, "WebSocket version: %d", version->valueint);
            }
            ESP_LOGI(TAG, "WebSocket URL saved. Opening WebSocket connection for conversation...");
        }
        
        // Check if WebSocket is already connected before opening
        // Schedule on application thread to check connection state
        Application::GetInstance().Schedule([this]() {
            auto& app = Application::GetInstance();
            // Check if WebSocket protocol exists and is connected
            // Note: Need access to websocket_protocol_ - may need to add getter or check method
            if (app.IsWebSocketConnected()) {  // Need to implement this method
                ESP_LOGI(TAG, "WebSocket already connected, ignoring ws_start");
                return;
            }
            app.OpenWebSocketConnection();
        });
    } else {
        ESP_LOGE(TAG, "ws_start message missing 'wss' field");
    }
    // Don't pass ws_start to on_incoming_json_ as it's a protocol-level message
}
```

#### Step 2: Add WebSocket Connection Check Method
**File**: `main/application.h` and `main/application.cc`

**Add to Application class**:
```cpp
// In application.h (public section)
bool IsWebSocketConnected() const;

// In application.cc
bool Application::IsWebSocketConnected() const {
    return websocket_protocol_ != nullptr && websocket_protocol_->IsAudioChannelOpened();
}
```

#### Step 3: Verify OpenWebSocketConnection Check is Sufficient
**File**: `main/application.cc:1721-1726`

**Current check is good**, but we should also check if connection is in progress:

```cpp
void Application::OpenWebSocketConnection() {
    // If WebSocket protocol already exists and is opened, do nothing
    if (websocket_protocol_ && websocket_protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "WebSocket connection already open, ignoring duplicate open request");
        return;
    }
    
    // Optional: Check if connection is in progress (if we track that state)
    // For now, the IsAudioChannelOpened() check should be sufficient
    
    // ... rest of connection logic ...
}
```

### Alternative Simpler Approach
Since `OpenWebSocketConnection()` already has the check, we can just rely on it:

```cpp
} else if (strcmp(type->valuestring, "ws_start") == 0) {
    // ... URL validation and saving ...
    
    // OpenWebSocketConnection() already checks if connection is open
    // So we can just call it - it will return early if already connected
    Application::GetInstance().Schedule([this]() {
        Application::GetInstance().OpenWebSocketConnection();
    });
}
```

**This is simpler and sufficient** - the existing check in `OpenWebSocketConnection()` should prevent duplicate connections.

### Testing
1. Send multiple `ws_start` messages rapidly
2. Verify only one WebSocket connection is opened
3. Verify no connection flood in logs
4. Verify no malformed CONNECT packets

---

## Implementation Order

1. **Change #3** (WebSocket check) - Simplest, existing check may be sufficient
2. **Change #1** (MAC normalization) - Straightforward string transformation
3. **Change #2** (MQTT reconnect) - Requires understanding reconnection flow

## Files to Modify

1. `main/application.cc` - MAC normalization, WebSocket check method (if needed)
2. `main/protocols/mqtt_protocol.cc` - OnDisconnected handler, ws_start check
3. `main/protocols/mqtt_protocol.h` - Reconnection flag (if needed)
4. `main/application.h` - IsWebSocketConnected() method (if needed)
5. `main/system_info.cc` / `main/system_info.h` - MAC normalization helper (optional)

## Dependencies

- `<algorithm>` header for `std::transform` (Change #1)
- FreeRTOS `vTaskDelay` (already included, Change #2)
- Application scheduling mechanism (already used, Change #3)

## Risk Assessment

- **Change #1**: Low risk - simple string transformation
- **Change #2**: Medium risk - need to coordinate with existing reconnection logic
- **Change #3**: Low risk - existing check should handle it, just need to verify

## Testing Checklist

- [ ] MAC address is lowercase in all MQTT topics
- [ ] MQTT subscription succeeds (no ACL mismatch)
- [ ] MQTT reconnection after disconnect is clean (no double CONNECT)
- [ ] Multiple `ws_start` messages don't create multiple WebSocket connections
- [ ] No malformed CONNECT packets in logs
- [ ] Normal operation (button press, wake word) still works

