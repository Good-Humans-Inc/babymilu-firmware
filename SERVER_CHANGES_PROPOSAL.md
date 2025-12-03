# Server Changes Proposal for Alarm Mode Listening

## Overview

This document outlines the required server-side changes to work with the updated firmware that ensures automatic listening starts when WebSocket opens via `ws_start`, and provides explicit server control via `listen` messages.

## Current Firmware Behavior (OTA-gcp-test branch)

### Automatic Listening on WebSocket Open

When the server sends `ws_start` message via MQTT:

1. **Device opens WebSocket connection**
2. **Automatically enters listening state** (after 100ms delay to ensure connection is ready)
3. **Sends `listen:start` message** to server with mode "manual"
4. **Starts audio capture and streaming immediately**

### After TTS Completion

When TTS finishes:
- Device goes to **idle** state (respecting `listening_mode_` = ManualStop)
- **WebSocket connection remains open**
- **Device waits for server to control next action**

### Server-Initiated Listening Restart

Server can send `listen:start` message to restart listening:
```json
{
  "type": "listen",
  "state": "start",
  "session_id": "..."
}
```

This will:
- Transition device from idle → listening
- Start audio capture
- Begin streaming microphone audio

---

## Required Server Changes

### 1. **Alarm Mode Flow - After TTS Stops**

**Current Issue**: After alarm TTS finishes, device goes to idle and doesn't automatically start listening.

**Solution**: Server should send `listen:start` message immediately after TTS stops.

#### Recommended Flow:

```
1. Server sends ws_start → Device opens WebSocket
2. Device automatically starts listening → Sends listen:start
3. Server receives audio → Processes it
4. Server sends TTS (alarm greeting)
5. TTS plays
6. TTS stops → Device goes to idle
7. Server immediately sends listen:start → Device restarts listening
8. Server receives audio → Processes user response
```

#### Implementation Example:

```python
# After TTS stop message is sent/confirmed
def on_tts_stop(device_id, session_id):
    # Wait a brief moment (100-200ms) for device to transition to idle
    time.sleep(0.15)
    
    # Send listen:start to restart listening
    send_websocket_message(session_id, {
        "type": "listen",
        "state": "start",
        "session_id": session_id
    })
    
    # Now device will start streaming audio again
```

### 2. **Timing Considerations**

**Important**: After TTS stops, wait **100-200ms** before sending `listen:start` to allow:
- Device to complete state transition to idle
- Audio decoder to finish processing
- Connection to be ready for next interaction

### 3. **Session Management**

**Key Point**: The WebSocket connection stays open after TTS stops. Server should:
- **Keep session alive** (don't close WebSocket)
- **Track session state** (listening/idle/speaking)
- **Use same session_id** for all messages in the conversation

### 4. **Follow-Up Conversations**

For alarm follow-ups (when user doesn't respond initially):

```
1. Device in idle (after TTS)
2. Server waits for silence (e.g., 25-35 seconds)
3. Server sends listen:start → Device starts listening
4. Server sends TTS follow-up
5. TTS plays
6. TTS stops → Device goes to idle
7. Repeat if needed
```

**Implementation**:

```python
def handle_alarm_followup(device_id, session_id, delay_seconds=25):
    # Wait for silence period
    time.sleep(delay_seconds)
    
    # Check if user has spoken (if STT frames received, cancel follow-up)
    if not user_spoke_recently(session_id):
        # Restart listening
        send_websocket_message(session_id, {
            "type": "listen",
            "state": "start",
            "session_id": session_id
        })
        
        # Send follow-up TTS
        send_tts(session_id, "Are you there? I'm here when you're ready.")
```

### 5. **Message Format**

All messages should include `session_id`:

```json
{
  "type": "listen",
  "state": "start",
  "session_id": "1aa37fb8-92ab-4dd5-9d5b-ecfb44ee07c1"
}
```

---

## Complete Alarm Flow Example

### Server-Side Pseudo-Code:

```python
# Alarm triggered
def trigger_alarm(device_id):
    # Step 1: Send ws_start via MQTT
    send_mqtt_message(device_id, {
        "type": "ws_start",
        "wss": "wss://server.example.com/xiaozhi/v1/",
        "version": 3
    })
    
    # Step 2: Wait for WebSocket connection (device opens automatically)
    session_id = wait_for_websocket_connection(device_id)
    
    # Step 3: Device automatically starts listening and sends listen:start
    # Server receives audio and processes it
    
    # Step 4: Send alarm greeting TTS
    send_tts(session_id, "Good morning! Time to wake up...")
    
    # Step 5: After TTS stops
    def on_tts_stop():
        # Wait for device to transition to idle
        time.sleep(0.15)
        
        # Restart listening
        send_websocket_message(session_id, {
            "type": "listen",
            "state": "start",
            "session_id": session_id
        })
        
        # Start follow-up timer (cancels if user speaks)
        start_followup_timer(session_id, delay=25)
    
    # Step 6: If no user response after delay
    def on_followup_timer():
        if not user_spoke(session_id):
            # Send follow-up
            send_websocket_message(session_id, {
                "type": "listen",
                "state": "start",
                "session_id": session_id
            })
            send_tts(session_id, "Are you awake? I'm here when you're ready.")
            # Repeat timer for next follow-up
            start_followup_timer(session_id, delay=35)
```

---

## Summary of Server Changes

### Required Changes:

1. ✅ **Send `listen:start` after TTS stops** (especially for alarms)
   - Wait 100-200ms after TTS stop
   - Include session_id in message

2. ✅ **Implement follow-up logic**
   - Track silence periods
   - Cancel follow-up if user speaks
   - Send `listen:start` before each follow-up TTS

3. ✅ **Keep WebSocket session alive**
   - Don't close connection after TTS
   - Reuse same session_id throughout conversation

4. ✅ **Handle session state tracking**
   - Track when device is listening/idle/speaking
   - Coordinate listen:start with TTS timing

### Optional Improvements:

1. **Silence detection**: Monitor for STT frames to detect user speech
2. **Escalating delays**: Use different follow-up delays (e.g., 25s, 35s, 60s)
3. **Session cleanup**: Close WebSocket only when conversation is truly finished

---

## Testing Checklist

- [ ] Device automatically starts listening when `ws_start` opens WebSocket
- [ ] Device sends `listen:start` message immediately after WebSocket opens
- [ ] Audio streaming begins right away
- [ ] After TTS stops, device goes to idle
- [ ] Server can send `listen:start` to restart listening
- [ ] Device restarts listening when server sends `listen:start`
- [ ] Audio streaming resumes correctly
- [ ] Follow-up alarms work correctly
- [ ] WebSocket connection stays open throughout conversation

---

## Message Flow Diagram

```
Server                    Device
  |                         |
  |--- ws_start (MQTT) ---->|
  |                         | Opens WebSocket
  |                         | Auto-enters listening
  |<-- listen:start -------|
  |<-- audio frames -------|
  |                         |
  |--- TTS start ---------->|
  |--- TTS audio ---------->|
  |--- TTS stop ----------->|
  |                         | Goes to idle
  |                         |
  |--- listen:start ------->| Restarts listening
  |<-- audio frames -------|
  |                         |
  |--- listen:start ------->| (Follow-up)
  |--- TTS start ---------->|
  |--- TTS stop ----------->|
  |                         | Goes to idle
  |                         |
```

---

## Key Takeaway

**The device now works exactly like lx-alarm-testing branch**:
- Automatic listening when WebSocket opens ✅
- Goes to idle after TTS (respecting listening mode) ✅  
- Server controls restart via explicit `listen:start` message ✅

The server needs to **actively manage listening state** by sending `listen:start` messages when it wants the device to listen again, especially after TTS completes.

