# MQTT Improvements Implementation Summary

## Overview
This document summarizes the MQTT connection stability improvements implemented to fix EOF errors and connection issues.

## Changes Implemented

### 1. Single Global MQTT Client Handle
**Problem**: MQTT client was being recreated even when already connected, causing connection drops.

**Solution**: 
- **File**: `main/protocols/mqtt_protocol.cc:59-63`
- Check if MQTT client is already connected before recreating
- Only delete and recreate if client doesn't exist or is disconnected
- Reuse existing connection if already connected

**Code**:
```cpp
// Use single global MQTT client - don't recreate if already connected
if (mqtt_ != nullptr && mqtt_->IsConnected()) {
    ESP_LOGI(TAG, "MQTT client already connected, reusing existing connection");
    return true;
}
```

### 2. Single Reconnection Strategy
**Problem**: Both esp-mqtt auto-reconnect and custom reconnection logic were active, causing conflicts.

**Solution**:
- **File**: `managed_components/78__esp-ml307/esp_mqtt.cc:36`
- Disabled esp-mqtt auto-reconnect: `mqtt_config.network.reconnect_timeout_ms = 0`
- **File**: `main/protocols/mqtt_protocol.cc:115-147`
- Implemented our own reconnection with exponential backoff (200-500ms)
- Backoff increases by 100ms on each failed retry, capped at 500ms
- Backoff resets to 200ms on successful connection

**Code**:
```cpp
// Disable esp-mqtt auto-reconnect - we handle reconnection ourselves
mqtt_config.network.reconnect_timeout_ms = 0;

// Our reconnection with backoff
int backoff_ms = reconnect_backoff_ms_;  // Starts at 200ms
// On failure: reconnect_backoff_ms_ = std::min(reconnect_backoff_ms_ + 100, 500);
// On success: reconnect_backoff_ms_ = 200;
```

### 3. Stable Client-ID and Configuration
**Problem**: Client-ID format was inconsistent, keepalive was too high.

**Solution**:
- **File**: `main/application.cc:798-810`
- Format client-id as `esp32-{mac}` (e.g., `esp32-30eda0ada0dc`)
- Remove colons from MAC address for cleaner client-id
- Set default keepalive to 60s (was 120s)
- Ensure MQTT v3.1.1 (default in esp-mqtt)
- Ensure TCP transport for `mqtt://` endpoints

**Code**:
```cpp
// Format: esp32-30eda0ada0dc (MAC without colons)
std::string mac_clean = mac;
mac_clean.erase(std::remove(mac_clean.begin(), mac_clean.end(), ':'), mac_clean.end());
std::string client_id = "esp32-" + mac_clean;

// Keepalive: 60s
int keepalive_interval = settings.GetInt("keepalive", 60);
```

### 4. MQTT Not Restarted on WebSocket Open
**Problem**: Opening WebSocket might have been interfering with MQTT connection.

**Solution**:
- **File**: `main/protocols/mqtt_protocol.cc:228-232`
- `ws_start` handler only opens WebSocket connection
- MQTT connection is left untouched and continues running
- WebSocket and MQTT can coexist independently

**Code**:
```cpp
// ws_start handler - only opens WebSocket, doesn't touch MQTT
Application::GetInstance().Schedule([this]() {
    app.OpenWebSocketConnection();  // Only affects WebSocket
});
```

### 5. EOF Error Handling
**Problem**: EOF errors triggered `MQTT_EVENT_ERROR` but didn't trigger reconnection.

**Solution**:
- **File**: `managed_components/78__esp-ml307/esp_mqtt.cc:80-94`
- Detect EOF/TCP closed errors in `MQTT_EVENT_ERROR` handler
- Treat them as disconnections and trigger `OnDisconnected` callback
- This ensures our reconnection logic handles EOF errors

**Code**:
```cpp
case MQTT_EVENT_ERROR:
    if (event->error_handle->esp_tls_last_esp_err == ESP_ERR_ESP_TLS_TCP_CLOSED_FIN ||
        event->error_handle->esp_tls_last_esp_err == ESP_ERR_ESP_TLS_CONNECTION_CLOSED) {
        connected_ = false;
        if (on_disconnected_callback_) {
            on_disconnected_callback_();  // Triggers our reconnection logic
        }
    }
```

## Configuration Summary

| Setting | Value | Location |
|---------|-------|----------|
| **Client-ID Format** | `esp32-{mac}` (e.g., `esp32-30eda0ada0dc`) | `application.cc:798-810` |
| **Keepalive** | 60 seconds | `mqtt_protocol.cc:70`, `application.cc:810` |
| **Transport** | TCP (for `mqtt://`) | `mqtt_protocol.cc:272`, `esp_mqtt.cc:32` |
| **MQTT Version** | 3.1.1 (default) | esp-mqtt default |
| **Auto-Reconnect** | Disabled (0ms) | `esp_mqtt.cc:36` |
| **Custom Reconnect** | Enabled (200-500ms backoff) | `mqtt_protocol.cc:115-147` |

## Benefits

1. **Stable Connection**: Single MQTT client handle prevents unnecessary reconnections
2. **No Conflicts**: Single reconnection strategy eliminates race conditions
3. **Consistent Identity**: Stable client-id format ensures broker recognizes device
4. **Proper Backoff**: Exponential backoff prevents connection storms
5. **Independent Protocols**: MQTT and WebSocket operate independently
6. **EOF Handling**: EOF errors properly trigger reconnection

## Testing Checklist

- [ ] MQTT client reuses existing connection when already connected
- [ ] EOF errors trigger automatic reconnection with backoff
- [ ] Client-ID format is `esp32-{mac}` (no colons)
- [ ] Keepalive is 60 seconds
- [ ] MQTT uses TCP transport for `mqtt://` endpoints
- [ ] WebSocket opens without affecting MQTT connection
- [ ] Reconnection backoff increases on failures (200→300→400→500ms)
- [ ] Reconnection backoff resets to 200ms on success

