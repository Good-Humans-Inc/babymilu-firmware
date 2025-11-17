# Audio Flow Verification - Deep Dive Analysis

## Complete Audio Flow Path

### 1. Button Press → Start Listening
```
User presses button
  → Application::StartListening()
    → SetDeviceState(kDeviceStateListening)
      → SetDeviceState() switch case kDeviceStateListening:
        → GetActiveProtocol()  // Returns WebSocket if open, else primary protocol
        → SendStartListening(listening_mode_)  // Sends {"type":"listen","mode":"manual","state":"start"}
        → audio_processor_->Start()  // Starts audio capture
```

### 2. Audio Capture → Encoding → Queue
```
AudioLoop() continuously calls OnAudioInput()
  → audio_processor_->Feed(data)  // Feeds raw PCM audio
    → AFE processes audio
      → audio_processor_->OnOutput() callback triggered
        → opus_encoder_->Encode(data)  // Encodes to Opus at 16kHz, 60ms frames
          → Creates AudioStreamPacket with Opus payload
          → Queues packet: audio_send_queue_.emplace_back(packet)
          → Sets event: xEventGroupSetBits(event_group_, SEND_AUDIO_EVENT)
```

### 3. Background Task → Send Audio
```
BackgroundTask::Run() waits for SEND_AUDIO_EVENT
  → When event received:
    → Moves all queued packets: auto packets = std::move(audio_send_queue_)
    → For each packet:
      → GetActiveProtocol()  // Gets WebSocket protocol if open
      → active_protocol->SendAudio(packet)
        → WebsocketProtocol::SendAudio()
          → Checks: websocket_ != nullptr && websocket_->IsConnected()
          → For version 3: Sends raw Opus frame (no wrapper)
            → websocket_->Send(packet.payload.data(), packet.payload.size(), true)
```

### 4. Button Release → Stop Listening
```
User releases button
  → Application::StopListening()
    → GetActiveProtocol()
    → SendStopListening()  // Sends {"type":"listen","mode":"manual","state":"stop"}
    → SetDeviceState(kDeviceStateIdle)
      → audio_processor_->Stop()  // Stops audio capture
```

## Critical Checks ✅

### ✅ Protocol Selection
- **GetActiveProtocol()** correctly returns `websocket_protocol_` if `IsAudioChannelOpened()` returns true
- **IsAudioChannelOpened()** checks: `websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout()`
- **SendAudio()** now also checks `IsConnected()` for safety

### ✅ Audio Format
- **Opus Encoder**: Configured for 16kHz, 1 channel, 60ms frames (960 samples)
- **Input Resampling**: If device input != 16kHz, resamples to 16kHz before encoding
- **Hello Message**: Requests 16kHz from server (fixed in latest changes)

### ✅ WebSocket Protocol Version 3
- **SendAudio()**: Sends raw Opus frames (no BinaryProtocol3 wrapper) ✅
- **Frame Format**: Raw binary Opus data, 16kHz, 60ms frames ✅
- **Logging**: Logs first 5 frames and every 50th frame at INFO level ✅

### ✅ Listen Messages
- **SendStartListening()**: Uses `GetActiveProtocol()` ✅
- **SendStopListening()**: Uses `GetActiveProtocol()` ✅
- **Message Format**: Correct JSON with "type":"listen", "mode":"manual", "state":"start"/"stop" ✅

### ✅ URL Query Parameters
- **WebSocket URL**: Includes `?device-id=<MAC>&client-id=<UUID>` ✅
- **Fallback**: Uses hardcoded URL if settings invalid ✅

## Potential Issues Found & Fixed

### ✅ Issue 1: SendAudio() didn't check IsConnected()
**Fixed**: Added `!websocket_->IsConnected()` check in `SendAudio()`

### ✅ Issue 2: Hello message requested device sample rate instead of 16kHz
**Fixed**: Changed `GetHelloMessage()` to always request 16kHz for WebSocket

## Verification Checklist

- [x] WebSocket URL includes query parameters (device-id, client-id)
- [x] Hello message requests 16kHz sample rate
- [x] Listen start message sent before first audio frame
- [x] Opus frames are raw binary (no wrapper) for version 3
- [x] Opus frames are 16kHz, 60ms (960 samples)
- [x] Listen stop message sent on button release
- [x] GetActiveProtocol() returns WebSocket when open
- [x] SendAudio() checks websocket is connected
- [x] Audio capture starts after listen start message
- [x] Logging added for debugging

## Expected Server Behavior

When firmware is correct, server should log:
1. "收到listen消息 ... state:start" after receiving listen start
2. ASR logs during speech
3. LLM processing logs
4. TTS segments "开始播放 4800 个样本 ..."
5. "state:stop" after receiving listen stop

## Test Sequence

1. **Connect**: Device opens WebSocket with query params
2. **Hello**: Device sends hello requesting 16kHz
3. **Press Button**: Device sends listen start, begins streaming Opus frames
4. **Hold Button**: Device continuously sends Opus frames every 60ms
5. **Release Button**: Device sends listen stop, stops streaming

## Conclusion

✅ **All critical paths verified and fixed**
✅ **Audio should now flow correctly from microphone → Opus encoding → WebSocket**
✅ **Server should receive listen messages and Opus frames as expected**

