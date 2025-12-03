# Deep Analysis: TTS Stop Not Resuming Listening

## Problem Statement

After TTS completes, the device goes to **IDLE** instead of automatically resuming **LISTENING**, even though:
1. WebSocket is still open (remote wakeup scenario)
2. Code was added to check for remote wakeup and resume listening
3. No log messages appear from the TTS stop handler

## Critical Observations from Logs

### Test 1 Logs:
```
I (72565) Application: STATE: speaking
I (72635) Application: STATE: idle  ← Goes to idle, NO log from TTS stop handler!
```

### Test 2 Logs:
```
I (370795) Application: STATE: speaking
I (376915) Application: STATE: idle  ← Goes to idle, NO log from TTS stop handler!
```

**KEY FINDING**: No log messages from TTS stop handlers appear in either test case!

Expected logs that are MISSING:
- `"TTS stopped (WebSocket), remote wakeup detected - automatically resuming listening"`
- `"TTS stopped (WebSocket), going to idle (manual stop mode)"`
- `"TTS stopped (WebSocket), automatically resuming listening (auto stop mode)"`
- `"Forwarding WebSocket message type 'tts' to Application::OnIncomingJson"`

## Root Cause Analysis

### Hypothesis 1: TTS Stop Message Never Arrives

**Check**: Look for WebSocket message logs when TTS stops.

**Evidence from logs**: No "WS RX message type: tts" or "Forwarding WebSocket message type 'tts'" logs.

**Conclusion**: **Either the server isn't sending TTS stop, OR it's being sent but not logged properly.**

### Hypothesis 2: TTS Stop Handler Has a Critical Bug

Let's examine the WebSocket TTS stop handler (lines 1753-1777):

```cpp
} else if (strcmp(state->valuestring, "stop") == 0) {
    Schedule([this]() {
        background_task_->WaitForCompletion();
        if (device_state_ == kDeviceStateSpeaking) {
            // Check if remote wakeup scenario...
```

**CRITICAL BUG #1**: **Missing NULL check for `state`!**

Compare with the primary protocol handler (line 859):
- Primary protocol: Checks `if (strcmp(type->valuestring, "tts") == 0)` then gets state
- WebSocket protocol: Gets state without checking if it exists!

**If `state` is NULL, `strcmp(state->valuestring, "stop")` will CRASH!**

But wait, the code checks `if (cJSON_IsString(state))` is missing in WebSocket handler!

Let me check line 1745 more carefully...

**CRITICAL BUG #2**: **Missing validation check!**

Primary protocol handler structure:
```cpp
if (strcmp(type->valuestring, "tts") == 0) {
    auto state = cJSON_GetObjectItem(root, "state");
    if (strcmp(state->valuestring, "start") == 0) {  // ← NO NULL CHECK!
```

Actually, wait - the primary protocol ALSO doesn't check if state is NULL before using it!

**CRITICAL BUG #3**: **Race Condition in State Check**

The TTS stop handler checks:
```cpp
if (device_state_ == kDeviceStateSpeaking) {
```

But what if:
1. TTS stop message arrives
2. Audio decode queue empties
3. Something else sets device to idle
4. TTS stop handler runs
5. `device_state_` is NO LONGER `kDeviceStateSpeaking`!

**The handler silently exits without doing anything!**

### Hypothesis 3: Something Else Sets Device to Idle

Looking at all places that set device to idle:
- Line 467: `ToggleChatState()`
- Line 535: `ExitAudioTestingMode()`  
- Line 613: Error handling
- Line 818: Network error
- Line 852: Network error
- **Line 884: TTS stop handler (primary protocol)**
- Line 991: Alert dismissal
- Line 1118: Audio channel closed (primary protocol)
- Line 1124: Audio channel closed
- **Line 1731: Audio channel closed (WebSocket)**
- **Line 1770: TTS stop handler (WebSocket)**

**CRITICAL QUESTION**: Could `OnAudioChannelClosed` be called when TTS stops?

Let's check the WebSocket audio channel closed handler:
```cpp
websocket_protocol_->OnAudioChannelClosed([this, &board = Board::GetInstance()]() {
    board.SetPowerSaveMode(true);
    Schedule([this]() {
        SetDeviceState(kDeviceStateIdle);  // ← LINE 1731
    });
});
```

**If WebSocket closes after TTS (which it shouldn't), this would force idle!**

### Hypothesis 4: Primary Protocol Handler is Called Instead of WebSocket Handler

When WebSocket is open, which handler gets TTS messages?

Looking at the code flow:
1. Server sends TTS stop via WebSocket
2. WebSocket protocol receives it (line 274)
3. Parses JSON (line 275)
4. Gets message type (line 280)
5. **Calls `on_incoming_json_` callback (line 292)**

But wait - there are TWO handlers:
- **Primary protocol handler** (line 826-954): `protocol_->OnIncomingJson([this, display](const cJSON *root) {`
- **WebSocket protocol handler** (line 1737-1816): `websocket_protocol_->OnIncomingJson([this, display](const cJSON *root) {`

**Which one gets called?**

The WebSocket protocol's `OnIncomingJson` callback is set at line 1737:
```cpp
websocket_protocol_->OnIncomingJson([this, display](const cJSON *root) {
```

And it forwards to this handler. So WebSocket messages should go to the WebSocket handler.

**But what if the primary protocol handler is ALSO registered and gets called first?**

Let me check the initialization order...

**CRITICAL FINDING**: Both handlers process TTS messages!

- **Primary protocol handler** (line 858-891): Processes TTS messages from MQTT
- **WebSocket protocol handler** (line 1744-1777): Processes TTS messages from WebSocket

**BUT**: If a TTS message comes via WebSocket, it should ONLY go to WebSocket handler. However, if the server sends it via BOTH protocols, or if there's routing confusion...

Actually, wait. The WebSocket protocol's `OnIncomingJson` is the callback that gets called when WebSocket receives JSON. So WebSocket TTS messages should go to the WebSocket handler.

### Hypothesis 5: Server Doesn't Send TTS Stop Message

**This is the most likely scenario!**

Looking at the user's logs:
- TTS plays: `I (370795) Application: STATE: speaking`
- Device goes to idle: `I (376915) Application: STATE: idle`

**No TTS stop message is logged!**

Expected log if TTS stop arrives:
```
I (376XXX) WS: WS RX text message len=XX data={"type":"tts","state":"stop",...}
I (376XXX) WS: WS RX message type: tts
I (376XXX) WS: Forwarding WebSocket message type 'tts' to Application::OnIncomingJson
```

**These logs are MISSING!**

**Conclusion**: **The server is NOT sending a TTS stop message!**

Instead, the device likely detects TTS is done by:
1. Audio decode queue becomes empty
2. Some timeout mechanism
3. Audio playback finishes

But the code doesn't have automatic detection of "TTS finished playing". The device only responds to explicit TTS stop messages!

## The Real Problem

**The firmware expects an explicit `{"type":"tts","state":"stop"}` message from the server, but the server is NOT sending it!**

Instead, the server likely:
1. Sends TTS audio packets
2. Stops sending audio packets
3. Assumes device knows TTS is done

But the device firmware doesn't automatically detect when audio playback finishes. It only responds to explicit TTS stop messages.

## Solutions

### Option 1: Server Must Send TTS Stop Message (RECOMMENDED)

Server should ALWAYS send:
```json
{"session_id":"...","type":"tts","state":"stop"}
```

After the last TTS audio packet is sent.

### Option 2: Firmware Auto-Detects TTS Completion

Add code to detect when:
- Audio decode queue is empty
- No audio has been received for X seconds
- Device is in speaking state

Then automatically transition to listening/idle.

But this is complex and error-prone.

### Option 3: Fix the Missing Logs

Add logging to understand what's happening:
- Log when TTS stop message is received
- Log the state check results
- Log why device goes to idle

## Immediate Action Items

1. **Verify server sends TTS stop message** - Add logging to confirm
2. **Add NULL checks** - Fix potential crashes in TTS handlers
3. **Add comprehensive logging** - Understand the flow
4. **Check if audio decode queue empty triggers state change** - Investigate alternative paths

## Code Issues Found

### Issue 1: Missing NULL Check (Potential Crash)

**Location**: Line 1745 (WebSocket handler), Line 859 (Primary handler)

**Problem**: `state` might be NULL before calling `strcmp(state->valuestring, ...)`

**Fix**: Add NULL check:
```cpp
auto state = cJSON_GetObjectItem(root, "state");
if (!cJSON_IsString(state)) {
    ESP_LOGW(TAG, "TTS message missing valid state field");
    return;
}
```

### Issue 2: Silent Failure in State Check

**Location**: Line 1756 (WebSocket), Line 870 (Primary)

**Problem**: If `device_state_ != kDeviceStateSpeaking` when handler runs, it silently exits

**Fix**: Add logging:
```cpp
if (device_state_ == kDeviceStateSpeaking) {
    // ... existing code ...
} else {
    ESP_LOGW(TAG, "TTS stop received but device state is %d (not speaking), ignoring", device_state_);
}
```

### Issue 3: Missing Log When TTS Stop Arrives

**Location**: WebSocket protocol message parsing

**Problem**: No log to confirm TTS stop message was received

**Fix**: Already exists at line 282: `ESP_LOGI(TAG, "WS RX message type: %s", type->valuestring);`

But this log is MISSING from user's output, confirming TTS stop message never arrives!

## Conclusion

**The root cause is that the server is NOT sending TTS stop messages, so the firmware's TTS stop handler never runs, and the device has no way to know TTS is finished except through some other mechanism (possibly audio decode queue empty, but that doesn't trigger listening resume).**

The firmware code itself looks correct (with minor improvements needed for NULL checks and logging), but it's not being triggered because the server isn't sending the required message.

