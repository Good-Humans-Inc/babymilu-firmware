# WebSocket Audio Issues - Analysis and Fixes

## Issues Identified from Logs

### Issue 1: Duplicate Query Parameters in URL ✅ FIXED
**Problem**: URL had duplicate `device-id` and `client-id` parameters:
```
ws://136.117.60.16:8000/xiaozhi/v1/?device-id=8C:BF:EA:8F:38:A0&client-id=8C:BF:EA:8F:38:A0&device-id=8c:bf:ea:8f:38:a0&client-id=de7c2c79-cfd1-409c-975d-3d1438da4879
```

**Root Cause**: The `ws_start` message from MQTT already includes query parameters, but the code was adding them again.

**Fix**: Check if query parameters already exist before adding them:
```cpp
bool has_device_id = url.find("device-id=") != std::string::npos;
bool has_client_id = url.find("client-id=") != std::string::npos;
if (!has_device_id || !has_client_id) {
    // Only add missing parameters
}
```

### Issue 2: Listen Mode is "auto" instead of "manual" ⚠️ NEEDS INVESTIGATION
**Problem**: Logs show `"mode":"auto"` when it should be `"mode":"manual"` for button-triggered listening:
```
I (119255) Protocol: Sending listen start: {"session_id":"...","type":"listen","state":"start","mode":"auto"}
```

**Possible Causes**:
1. Listening is being triggered by wake word/automatic transition, not button press
2. `listening_mode_` is being set to `kListeningModeAutoStop` before `SetDeviceState()` is called
3. Race condition where mode is reset after being set

**Note**: If listening is triggered by wake word (`WakeWordInvoke`), it correctly sets `kListeningModeAutoStop`. But if triggered by button press (`StartListening`), it should set `kListeningModeManualStop`.

### Issue 3: Server Not Receiving/Processing Audio ❓ MAIN ISSUE
**Problem**: Frames are being sent but server isn't logging "收到listen消息" or processing audio.

**Observations from Logs**:
- ✅ WebSocket connection established successfully
- ✅ Hello message sent and server hello received
- ✅ Session ID matches: `89d89747-8688-4029-954d-8ca981a04dea`
- ✅ Listen start message sent: `{"type":"listen","state":"start","mode":"auto"}`
- ✅ Opus frames being sent: `I (119355) WS: Sending Opus frame 1, bytes=zu`
- ❌ Server not logging "收到listen消息"
- ❌ No ASR/LLM/TTS processing

**Possible Causes**:
1. **Listen message format issue**: Server might not recognize `"mode":"auto"` or expect different format
2. **Timing issue**: Listen message sent before connection fully ready
3. **Session mismatch**: Session ID in listen message doesn't match server's expected session
4. **URL issue**: Duplicate query parameters causing server to reject connection
5. **Protocol version mismatch**: Server expects different message format for version 3

## Fixes Applied

### ✅ Fix 1: Prevent Duplicate Query Parameters
- Check if URL already has `device-id` and `client-id` before adding
- Only add missing parameters

### ✅ Fix 2: Improved Logging
- Fixed log format issue (`bytes=zu` → proper size_t cast)
- Better logging for URL with/without query params

### ✅ Fix 3: Connection Check in SendAudio
- Added `IsConnected()` check before sending audio
- Prevents sending to disconnected websocket

## Next Steps to Debug

1. **Verify listen message format**: Check if server expects `"mode":"manual"` specifically
2. **Check session ID**: Verify session_id in listen message matches server's session
3. **Test with manual mode**: Force `kListeningModeManualStop` to see if server responds
4. **Check server logs**: Verify if server is receiving the listen message at all
5. **Test URL format**: Verify server accepts URLs with query parameters

## Expected Behavior

When working correctly:
1. Server logs: "收到listen消息 ... state:start"
2. Server receives Opus frames and processes ASR
3. Server sends LLM response
4. Server sends TTS audio
5. Server logs: "state:stop" after listen stop

## Code Changes Summary

1. **websocket_protocol.cc**:
   - Fixed duplicate query parameter issue
   - Added connection check in SendAudio()
   - Fixed log format for frame size

2. **No changes needed** (but verify):
   - Listen message format is correct
   - Session ID handling is correct
   - Protocol version handling is correct

