# EchoEar VAD Interrupt and Device-Side AEC for Interrupt Feature

## Overview

This document explains how the EchoEar board can leverage Voice Activity Detection (VAD) interrupts and device-side Acoustic Echo Cancellation (AEC) to enable an interrupt feature, allowing users to interrupt the AI assistant during speech playback.

## Background

### EchoEar Hardware

EchoEar is an ESP32-S3-based AI development kit featuring:
- Dual microphone array for beamforming and noise reduction
- Audio codec with reference channel support (ES7210 microphone + ES8311 codec)
- Support for both microphone input and reference (speaker output) channels

### Current Architecture

The audio processing pipeline uses the ESP-ADF Audio Front-End (AFE) library, which provides:
- **Voice Activity Detection (VAD)**: Detects when the user is speaking
- **Acoustic Echo Cancellation (AEC)**: Removes echo from speaker output in microphone input
- **Noise Suppression (NS)**: Reduces background noise

## Key Concepts

### 1. VAD Interrupt Mechanism

VAD interrupt refers to the ability to detect when the user starts speaking and immediately trigger an interrupt event, even when the device is in a speaking state (playing AI response).

**Current Implementation:**

```cpp
// In AfeAudioProcessor::AudioProcessorTask()
if (vad_state_change_callback_) {
    if (res->vad_state == VAD_SPEECH && !is_speaking_) {
        is_speaking_ = true;
        vad_state_change_callback_(true);  // User started speaking
    } else if (res->vad_state == VAD_SILENCE && is_speaking_) {
        is_speaking_ = false;
        vad_state_change_callback_(false); // User stopped speaking
    }
}
```

The VAD callback propagates through the system:
- `AfeAudioProcessor` → `AudioService` → `Application` → Main event loop
- Event: `MAIN_EVENT_VAD_CHANGE` is set when VAD state changes

### 2. Device-Side AEC

Device-side AEC processes audio locally on the ESP32, removing echo from the speaker output before it reaches the microphone input. This enables:

- **Continuous listening**: The device can listen while speaking without echo interference
- **Real-time interaction**: Users can interrupt the AI at any time
- **Lower latency**: No need to send audio to server for echo cancellation

**Configuration:**

```cpp
// In AfeAudioProcessor::Initialize()
#ifdef CONFIG_USE_DEVICE_AEC
    afe_config->aec_init = true;
    afe_config->vad_init = false;  // VAD disabled when AEC enabled
#else
    afe_config->aec_init = false;
    afe_config->vad_init = true;   // VAD enabled when AEC disabled
#endif
```

**Dynamic Toggle:**

```cpp
void AfeAudioProcessor::EnableDeviceAec(bool enable) {
    if (enable) {
        afe_iface_->disable_vad(afe_data_);
        afe_iface_->enable_aec(afe_data_);
    } else {
        afe_iface_->disable_aec(afe_data_);
        afe_iface_->enable_vad(afe_data_);
    }
}
```

**Important Note**: VAD and AEC are mutually exclusive in the current AFE implementation. When device-side AEC is enabled, VAD must be disabled.

### 3. Listening Modes

The system supports three listening modes:

```cpp
enum ListeningMode {
    kListeningModeAutoStop,    // Stops listening when VAD detects silence
    kListeningModeManualStop,  // Requires explicit stop command
    kListeningModeRealtime     // Continuous listening (requires AEC)
};
```

**Mode Selection Logic:**

```cpp
// When wake word detected or manual start
SetListeningMode(aec_mode_ == kAecOff ? 
    kListeningModeAutoStop : 
    kListeningModeRealtime);
```

- **Without AEC**: Uses `kListeningModeAutoStop` - VAD detects speech end and stops listening
- **With Device-Side AEC**: Uses `kListeningModeRealtime` - Continuous listening enabled

## How Interrupt Feature Works

### Architecture Flow

```
┌─────────────────────────────────────────────────────────────┐
│                    Audio Input Pipeline                     │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  Microphone Input → Audio Codec → AFE Processor            │
│                                                              │
│  ┌────────────────────────────────────────────────────┐    │
│  │  Device-Side AEC (when enabled)                    │    │
│  │  - Removes speaker echo from mic input             │    │
│  │  - Enables continuous listening                    │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
│  ┌────────────────────────────────────────────────────┐    │
│  │  VAD Detection (when AEC disabled)                 │    │
│  │  - Detects speech start/end                        │    │
│  │  - Triggers interrupt events                       │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
│  Processed Audio → Encoder → Network → Server              │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                    Audio Output Pipeline                    │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  Server → Network → Decoder → Audio Codec → Speaker        │
│                                                              │
│  ┌────────────────────────────────────────────────────┐    │
│  │  Reference Channel (for AEC)                       │    │
│  │  - Feeds speaker output to AEC                      │    │
│  │  - Enables echo cancellation                        │    │
│  └────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

### Interrupt Scenarios

#### Scenario 1: Interrupt During AI Speech (Device-Side AEC Enabled)

**State**: `kDeviceStateSpeaking` with `kListeningModeRealtime`

1. **AI is speaking**: Audio output plays through speaker
2. **User starts speaking**: Microphone picks up user voice
3. **AEC processes**: Removes speaker echo from microphone input
4. **VAD detection**: Since AEC is enabled, VAD is disabled, but the system can detect audio activity through other means
5. **Interrupt trigger**: System detects user speech and aborts AI speech
6. **State transition**: `kDeviceStateSpeaking` → `kDeviceStateListening`

**Implementation Note**: Currently, when AEC is enabled, VAD is disabled. To enable interrupt detection during AEC mode, the system would need:
- Alternative voice activity detection mechanism (e.g., energy-based detection)
- Or enable VAD alongside AEC (if AFE supports it)
- Or use server-side VAD results

#### Scenario 2: Interrupt During AI Speech (AEC Disabled)

**State**: `kDeviceStateSpeaking` with `kListeningModeAutoStop`

1. **AI is speaking**: Audio output plays through speaker
2. **User starts speaking**: Microphone picks up user voice + speaker echo
3. **VAD detection**: VAD may be confused by echo, but can still detect user speech
4. **Interrupt trigger**: VAD callback fires when speech detected
5. **State transition**: `kDeviceStateSpeaking` → `kDeviceStateListening`

**Current Limitation**: When AEC is disabled, VAD is enabled, but the device typically stops listening during speaking state:

```cpp
case kDeviceStateSpeaking:
    if (listening_mode_ != kListeningModeRealtime) {
        audio_service_.EnableVoiceProcessing(false);
        // Only AFE wake word can be detected in speaking mode
        audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
    }
```

### Enabling Interrupt Feature

To fully enable interrupt functionality, the following modifications would be needed:

#### Option 1: Enable VAD During AEC Mode (Recommended)

Modify the AFE configuration to allow both AEC and VAD simultaneously:

```cpp
// Proposed modification
void AfeAudioProcessor::EnableDeviceAec(bool enable) {
    if (enable) {
        afe_iface_->enable_aec(afe_data_);
        // Keep VAD enabled for interrupt detection
        // afe_iface_->enable_vad(afe_data_);  // Don't disable VAD
    } else {
        afe_iface_->disable_aec(afe_data_);
        afe_iface_->enable_vad(afe_data_);
    }
}
```

**Benefits**:
- VAD can detect user speech even during AI playback
- AEC removes echo, improving VAD accuracy
- Enables true interrupt capability

**Challenges**:
- May require AFE library support for simultaneous AEC+VAD
- Need to verify AFE API compatibility

#### Option 2: Energy-Based Voice Detection

Implement alternative voice activity detection using audio energy thresholds:

```cpp
// Proposed implementation
bool DetectVoiceActivity(const std::vector<int16_t>& audio_frame) {
    // Calculate RMS energy
    float energy = 0.0f;
    for (int16_t sample : audio_frame) {
        energy += sample * sample;
    }
    energy = sqrt(energy / audio_frame.size());
    
    // Compare against threshold
    return energy > VOICE_ENERGY_THRESHOLD;
}
```

**Benefits**:
- Works independently of AFE VAD
- Can run alongside AEC
- Low computational overhead

**Challenges**:
- Less accurate than ML-based VAD
- May trigger on non-speech sounds
- Requires threshold tuning

#### Option 3: Server-Side VAD with Device-Side AEC

Use server-side VAD results while maintaining device-side AEC:

1. Device sends AEC-processed audio to server
2. Server performs VAD and sends results back
3. Device uses server VAD results for interrupt detection

**Benefits**:
- Leverages server-side ML models
- Device-side AEC reduces echo before transmission
- No local VAD conflicts

**Challenges**:
- Network latency for interrupt detection
- Requires protocol modification
- May not be fast enough for real-time interrupt

## Recommended Implementation

### Phase 1: Enable Continuous Listening with AEC

1. **Enable device-side AEC**:
   ```cpp
   Application::SetAecMode(kAecOnDeviceSide);
   ```

2. **Keep audio processor running during speaking**:
   ```cpp
   case kDeviceStateSpeaking:
       // Keep voice processing enabled for interrupt detection
       if (listening_mode_ == kListeningModeRealtime) {
           audio_service_.EnableVoiceProcessing(true);  // Keep enabled
       }
   ```

### Phase 2: Add Interrupt Detection

1. **Enable VAD alongside AEC** (if supported):
   - Modify `EnableDeviceAec()` to keep VAD enabled
   - Or implement energy-based detection

2. **Handle VAD interrupt during speaking**:
   ```cpp
   void Application::HandleVadChangeEvent() {
       if (GetDeviceState() == kDeviceStateSpeaking && IsVoiceDetected()) {
           // User is interrupting
           AbortSpeaking(kAbortReasonUserInterrupt);
           SetDeviceState(kDeviceStateListening);
       }
   }
   ```

### Phase 3: Optimize Interrupt Latency

1. **Reduce processing delay**: Minimize audio buffer sizes
2. **Prioritize interrupt events**: Higher priority for VAD callbacks
3. **Immediate state transition**: Don't wait for audio queue to drain

## Benefits of Interrupt Feature

1. **Natural Conversation**: Users can interrupt AI responses naturally
2. **Improved UX**: Reduces frustration from long AI responses
3. **Real-time Interaction**: Enables back-and-forth conversation
4. **Power Efficiency**: Can stop unnecessary audio processing when user interrupts

## Technical Considerations

### Audio Codec Configuration

EchoEar uses `BoxAudioCodec` with:
- **Input channels**: Dual microphone array (2 channels)
- **Reference channel**: Speaker output (1 channel) for AEC
- **Input format**: "MR" (Microphone + Reference) when AEC enabled

### AFE Configuration

```cpp
afe_config_t* afe_config = afe_config_init("MR", NULL, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
afe_config->aec_mode = AEC_MODE_VOIP_HIGH_PERF;
afe_config->vad_mode = VAD_MODE_0;
afe_config->vad_min_noise_ms = 100;
```

### Memory Considerations

- AEC requires additional memory for echo cancellation buffers
- VAD models require model storage space
- Running both simultaneously increases memory usage

## Conclusion

The EchoEar board can leverage VAD interrupts and device-side AEC to enable interrupt functionality through:

1. **Device-side AEC**: Enables continuous listening by removing speaker echo
2. **VAD Detection**: Detects when user starts speaking (needs modification to work with AEC)
3. **State Management**: Handles interrupt events and transitions appropriately

The key challenge is enabling VAD detection while AEC is active, which may require:
- AFE library modifications
- Alternative detection mechanisms
- Or server-side VAD integration

With proper implementation, users can naturally interrupt the AI assistant during speech playback, creating a more interactive and responsive experience.
