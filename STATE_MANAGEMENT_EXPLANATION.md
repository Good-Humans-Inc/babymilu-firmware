# Detailed State Management Explanation

## Key Concepts

### 1. **Device State (`device_state_`)** - EXCLUSIVE States
The device can only be in ONE state at a time. These are mutually exclusive:

```cpp
enum DeviceState {
    kDeviceStateIdle,        // Device is idle/standby
    kDeviceStateListening,   // Device is listening to microphone and streaming audio
    kDeviceStateSpeaking,    // Device is playing audio (TTS) to speakers
    kDeviceStateConnecting,  // Device is establishing connection
    // ... other states
};
```

**CRITICAL**: When `device_state_` changes, it calls `SetDeviceState()` which:
- Stops previous state's activities
- Starts new state's activities
- These states **CANNOT overlap**

### 2. **Listening Mode (`listening_mode_`)** - Configuration Parameter
This is NOT a state, but a **configuration** that controls HOW listening behaves:

```cpp
enum ListeningMode {
    kListeningModeAutoStop,      // Stops listening automatically after silence
    kListeningModeManualStop,    // Stops listening only when user/server stops it
    kListeningModeRealtime       // Keeps listening even while speaking (AEC mode)
};
```

**CRITICAL**: `listening_mode_` is **preserved** across state changes. It's a setting, not a state.

---

## Question 1: Why Auto-Set Listening When WebSocket Starts Wasn't Really Listening?

### The Problem

When `ws_start` opens WebSocket and automatically calls `SetListeningMode()`, it triggers:

```cpp
// Line 1777 in OpenWebSocketConnection()
SetListeningMode(kListeningModeManualStop);
```

Which calls:
```cpp
// Line 1392-1395
void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;  // Store the mode
    SetDeviceState(kDeviceStateListening);  // Change device state
}
```

Then `SetDeviceState(kDeviceStateListening)` executes (lines 1432-1487):
1. Sets display to "LISTENING"
2. Tries to send listen start message
3. **CRITICAL**: Checks if `audio_processor_->IsRunning()` at line 1441

### Why It Might Not Work

**The audio processor might already be running** from a previous state, so the code at lines 1441-1487 is **skipped**:

```cpp
if (!audio_processor_->IsRunning()) {  // <-- This check!
    // Send listen start message
    // Start audio capture
}
```

If `audio_processor_->IsRunning()` is `true`, it does NOT:
- Send the listen start message to server
- Start audio capture (already running, but maybe not sending?)
- Reset encoder state properly

**The fix**: The check should ensure the connection is ready AND properly initialized.

---

## Question 2: Can Device Be in Listening Mode and Speaking Mode at the Same Time?

### Answer: **NO - States are EXCLUSIVE, but Mode can be Realtime**

The device can **NEVER** have:
- `device_state_ == kDeviceStateListening` AND
- `device_state_ == kDeviceStateSpeaking` 

At the same time. Only ONE `device_state_` value exists.

### However, Listening MODE can affect Speaking behavior:

When in `kDeviceStateSpeaking` state, the code checks `listening_mode_`:

```cpp
// Line 1492-1500
case kDeviceStateSpeaking:
    if (listening_mode_ != kListeningModeRealtime) {
        audio_processor_->Stop();  // Stop microphone capture
        wake_word_->StartDetection();
    }
    // If listening_mode_ == kListeningModeRealtime:
    //   - audio_processor_ KEEPS RUNNING (microphone active while speaking)
    //   - This is for AEC (Acoustic Echo Cancellation)
```

So:
- **State**: Exclusive (Listening OR Speaking, never both)
- **Mode**: Can be Realtime (microphone keeps running while speaking for AEC)

---

## Question 3: Why Does lx-alarm-testing Work But Current Branch Doesn't?

### Both Branches Go to Idle After TTS - But Why One Works?

The key difference is **WHAT HAPPENS BEFORE TTS STARTS**:

### lx-alarm-testing Branch Flow:

```
1. ws_start arrives → OpenWebSocketConnection()
2. WebSocket opens successfully
3. Line 1777: SetListeningMode(kListeningModeManualStop)
   → Sets device_state_ to LISTENING
   → Sends listen:start message to server
   → Starts audio capture and streaming
4. Server receives audio, processes it
5. Server sends TTS start → device_state_ changes to SPEAKING
   → audio_processor_ stops (line 1494)
   → But listening_mode_ is STILL kListeningModeManualStop (preserved!)
6. TTS plays
7. TTS stops → Line 1725: Check listening_mode_
   → Since it's ManualStop → Goes to IDLE
8. BUT: Server already has the session and can send "listen" message
   → OR server can send another ws_start to restart
```

### Current Branch Flow (After Our Changes):

```
1. ws_start arrives → OpenWebSocketConnection()
2. WebSocket opens successfully
3. Line 1777: SetListeningMode(kListeningModeManualStop)  [SAME]
   → Sets device_state_ to LISTENING
   → Sends listen:start message
   → Starts audio capture
4. Server receives audio, processes it
5. Server sends TTS start → device_state_ to SPEAKING [SAME]
6. TTS plays [SAME]
7. TTS stops → Line 1725: Goes to IDLE [SAME]
8. BUT: We REMOVED automatic transitions, so device stays idle
   → Server must send explicit "listen" message to restart
```

### The Real Difference:

**lx-alarm-testing** works because:
- The WebSocket connection is already established
- The server knows the device is ready
- When server sends "listen" message (which you added), device starts listening again

**Current branch** should work the same IF:
- Server sends explicit `{"type":"listen","state":"start"}` message after TTS
- Your "listen" message handler (lines 1010-1025) handles it properly

The issue might be that the server isn't sending the "listen" message, or there's a timing issue.

---

## Question 4: Detailed State Transition Flow

### State Transition Diagram:

```
IDLE
  ↓ [ws_start arrives]
CONNECTING (WebSocket opening)
  ↓ [WebSocket opened]
LISTENING
  ↓ [SetListeningMode called]
  → listening_mode_ stored (e.g., ManualStop)
  → listen:start message sent
  → audio_processor_ starts
  → microphone streaming begins
  
LISTENING
  ↓ [TTS start message arrives]
SPEAKING
  → audio_processor_ stops (unless Realtime mode)
  → TTS audio playback starts
  → listening_mode_ preserved (still ManualStop)
  
SPEAKING
  ↓ [TTS stop message arrives]
  → Check listening_mode_:
    - If ManualStop → IDLE
    - If AutoStop → LISTENING
    - If Realtime → LISTENING (already was listening)
```

### Critical Code Paths:

#### 1. When WebSocket Opens (Line 1774-1782):
```cpp
if (device_state_ == kDeviceStateIdle) {
    SetListeningMode(kListeningModeManualStop);
    // This immediately:
    // 1. Stores listening_mode_ = ManualStop
    // 2. Calls SetDeviceState(LISTENING)
    // 3. Sends listen:start message
    // 4. Starts audio capture
}
```

#### 2. When TTS Starts (Line 1717-1719):
```cpp
if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
    SetDeviceState(kDeviceStateSpeaking);
    // State changes from LISTENING → SPEAKING
    // listening_mode_ is preserved (still ManualStop)
}
```

#### 3. When TTS Stops (Line 1725-1729):
```cpp
if (listening_mode_ == kListeningModeManualStop) {
    SetDeviceState(kDeviceStateIdle);  // Go to idle
} else {
    SetDeviceState(kDeviceStateListening);  // Go back to listening
}
```

### Key Insight: State vs Mode

- **State** (`device_state_`): What the device is DOING right now (Idle/Listening/Speaking)
- **Mode** (`listening_mode_`): How listening should BEHAVE when it happens

Think of it like:
- State = "What am I doing?" (Reading/Writing/Eating)
- Mode = "How should I read?" (Fast/Slow/Careful)

The mode persists even when you stop the activity!

---

## Why "Listening" State Might Not Actually Be Listening

The listening state only starts audio capture if:

1. `audio_processor_->IsRunning()` is FALSE (line 1441)
2. Connection is open (line 1445)
3. Protocol is available (line 1444)

If any of these fail, you'll see:
- Device shows "LISTENING" state
- LED shows listening (red light)
- But no audio is being sent!

This is why you might have seen the state change but audio wasn't actually streaming.

---

## Summary

1. **Device states are EXCLUSIVE** - only one at a time
2. **Listening mode is a CONFIGURATION** - persists across state changes
3. **Realtime mode** allows microphone to keep running while speaking (for AEC)
4. **Both branches go to idle** after TTS, but lx-alarm-testing works because server manages the flow properly
5. **The "listening" state might not actually listen** if audio processor is already running or connection isn't ready

The key to making it work: Ensure the server sends explicit "listen" messages after TTS, or the connection stays open and ready for the next interaction.

