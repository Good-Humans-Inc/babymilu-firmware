# MQTT EOF Error After ws_start - Debugging Guide

## Error Description

After receiving a `ws_start` message via MQTT, the MQTT connection is being closed by the server/broker, resulting in these errors:

```
E (1823948) mqtt_client: esp_mqtt_handle_transport_read_error: transport_read(): EOF
E (1823948) mqtt_client: esp_mqtt_handle_transport_read_error: transport_read() error: errno=128
I (1823958) esp_mqtt: MQTT error occurred: ESP_ERR_ESP_TLS_TCP_CLOSED_FIN
E (1823958) mqtt_client: mqtt_process_receive: mqtt_message_receive() returned -2
```

## What These Errors Mean

1. **`transport_read(): EOF`** - The TCP connection was closed by the server (End of File)
2. **`errno=128`** - ECONNRESET (Connection reset by peer)
3. **`ESP_ERR_ESP_TLS_TCP_CLOSED_FIN`** - TCP connection closed with FIN packet (graceful close)
4. **`mqtt_message_receive() returned -2`** - Error code -2 (ESP_ERR_INVALID_RESPONSE or similar)

## Root Cause Analysis

The MQTT connection **should remain open** after `ws_start` because:
- MQTT is used for control messages (ws_start, wake word triggers, etc.)
- WebSocket is used for audio streaming
- Both can coexist simultaneously

**The connection is being closed by the server/broker**, not by the device. This suggests:

### Possible Server-Side Issues:

1. **Server Configuration (config.yaml)**
   - Server might be configured to close MQTT connections after sending `ws_start`
   - This would be incorrect behavior - MQTT should stay open
   - Check server logs for any connection cleanup logic

2. **MQTT Broker Configuration**
   - Broker might have connection limits or policies that close idle connections
   - Check broker keepalive settings
   - Check if broker has a "close after message" policy

3. **Server Application Logic**
   - Server application might be explicitly closing MQTT connection after `ws_start`
   - This is likely a bug - the server should keep MQTT open for future control messages

4. **Network/Firewall Issues**
   - Intermediate firewall or NAT might be closing idle connections
   - Check for TCP timeout settings

## Debugging Steps

### 1. Check Enhanced Logs

With the updated code, you should now see:
```
I (...) MQTT: Received ws_start message. MQTT connection state: connected
I (...) MQTT: Server requests WebSocket connection: wss://...
I (...) MQTT: MQTT connection state after processing ws_start: connected (should remain connected)
W (...) MQTT: MQTT disconnected from endpoint
W (...) MQTT: MQTT connection state after disconnect callback: disconnected
W (...) MQTT: MQTT disconnected after ws_start - this may indicate server is closing MQTT connection
```

**Key Questions:**
- Does the disconnect happen immediately after `ws_start`?
- How long after `ws_start` does the disconnect occur?
- Does it happen every time or intermittently?

### 2. Check Server-Side Logs

Look for:
- Any code that closes MQTT connections after sending `ws_start`
- Broker logs showing why connections are closed
- Any connection cleanup or timeout logic

### 3. Check Server Configuration (config.yaml)

Look for:
```yaml
# Check for any MQTT connection management settings
mqtt:
  # Look for settings like:
  close_after_ws_start: true  # This would be wrong!
  connection_timeout: ...
  max_idle_time: ...
```

### 4. Test MQTT Connection Persistence

1. **Before ws_start**: Verify MQTT is connected and subscribed
2. **Send ws_start**: Monitor if connection closes
3. **Check timing**: How long after ws_start does it close?

### 5. Monitor Network Traffic

Use Wireshark or tcpdump to see:
- Who initiates the TCP FIN (client or server)?
- Is there a TCP RST or graceful FIN?
- What's the timing relative to ws_start message?

## Expected Behavior

**Correct behavior:**
1. Device connects to MQTT broker
2. Device subscribes to control topic
3. Server sends `ws_start` message
4. Device opens WebSocket connection
5. **MQTT connection remains open** for future control messages
6. Both connections coexist

**Current (incorrect) behavior:**
1. Device connects to MQTT broker
2. Device subscribes to control topic
3. Server sends `ws_start` message
4. **MQTT connection is closed by server** ❌
5. Device can't receive future control messages

## Solutions

### Solution 1: Fix Server Configuration (Recommended)

The server should **NOT** close MQTT connections after `ws_start`. Check:

1. **Server application code**: Remove any logic that closes MQTT after `ws_start`
2. **config.yaml**: Remove any settings that cause connection closure
3. **Broker configuration**: Ensure broker doesn't close idle connections

### Solution 2: Device-Side Workaround (Temporary)

The device now has automatic reconnection logic:
- When MQTT disconnects, it will attempt to reconnect after 10 seconds
- This ensures MQTT stays available for control messages

However, this is a **workaround** - the root cause should be fixed on the server side.

### Solution 3: Increase Keepalive

If the issue is keepalive-related:
- Current keepalive: 120 seconds (default)
- Can be configured in settings: `mqtt.keepalive`

Try increasing it:
```cpp
Settings settings("mqtt", true);
settings.SetInt("keepalive", 300);  // 5 minutes
```

## Verification

After fixing the server-side issue, you should see:
```
I (...) MQTT: Received ws_start message. MQTT connection state: connected
I (...) MQTT: Server requests WebSocket connection: wss://...
I (...) MQTT: MQTT connection state after processing ws_start: connected (should remain connected)
I (...) WS: Connecting to websocket server: wss://...
I (...) WS: WebSocket connection opened successfully
# No MQTT disconnect errors
# MQTT remains connected
```

## Additional Diagnostics

### Check MQTT Keepalive

The device sends MQTT PINGREQ packets every `keepalive/2` seconds. If the server isn't responding:
- Check broker logs for PINGREQ/PINGRESP
- Verify broker is configured to handle keepalive properly

### Check for Connection Limits

Some brokers have connection limits:
- Check if broker has a "one connection per client_id" policy
- If WebSocket and MQTT use the same client_id, this could cause issues

### Check Server Application Logic

Look for code patterns like:
```python
# BAD - Don't do this
def handle_ws_start(client):
    send_ws_start_message(client)
    client.disconnect()  # ❌ This closes MQTT connection

# GOOD - Keep connection open
def handle_ws_start(client):
    send_ws_start_message(client)
    # Connection stays open for future messages
```

## Summary

**The issue is server-side**: The server/broker is closing the MQTT connection after sending `ws_start`. This is incorrect behavior - MQTT should remain open for control messages.

**Next steps:**
1. Check server logs and configuration
2. Look for code that closes MQTT after `ws_start`
3. Verify broker configuration doesn't close idle connections
4. The device will now automatically reconnect, but the root cause should be fixed on the server

