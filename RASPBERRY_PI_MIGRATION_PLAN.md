# Raspberry Pi 5 Migration Plan

## Executive Summary

**Recommendation: Build a Python-based system with a hybrid architecture** - Keep the core logic similar but leverage Python's ecosystem for AI/ML while maintaining the proven state machine and animation patterns from your ESP32 firmware.

## Current Architecture Analysis

### Key Components in ESP32 Firmware:
1. **Animation System**: LVGL-based, emotion-driven animations (8 types, 28 frames)
2. **Audio Pipeline**: Opus encoding/decoding, AFE processing, wake word detection
3. **State Machine**: Complex device states (idle, listening, speaking, connecting, etc.)
4. **Communication**: WebSocket/MQTT protocols (currently server-based)
5. **Display**: LVGL rendering with emotion-based animation switching
6. **Hardware Abstraction**: Board abstraction layer for different ESP32 boards

## Migration Strategy: Python-Based System

### Why Python Makes Sense:
✅ **AI/ML Ecosystem**: Excellent libraries for LLM (transformers, llama.cpp), TTS (piper, coqui-tts), ASR (whisper, vosk)
✅ **LLM 8850 Integration**: Python bindings available for AI accelerators
✅ **Rapid Development**: Faster iteration for AI features
✅ **Ecosystem**: Rich libraries for audio (PyAudio, sounddevice), display (Pillow, pygame), async (asyncio)
✅ **Maintainability**: Easier to maintain and extend than C++

### Why NOT Direct Port:
❌ **ESP-IDF Dependencies**: Heavy reliance on FreeRTOS, ESP-IDF APIs
❌ **Hardware-Specific**: Board abstraction layer tied to ESP32 peripherals
❌ **Memory Constraints**: ESP32 optimizations not needed on RPi5
❌ **Real-time Requirements**: Different threading model (asyncio vs FreeRTOS)

## Recommended Architecture

### High-Level Design

```
┌─────────────────────────────────────────────────────────┐
│                    Raspberry Pi 5                        │
│                                                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │   Display    │  │    Audio     │  │   LLM 8850   │ │
│  │   Manager    │  │   Pipeline   │  │  Accelerator │ │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘ │
│         │                  │                  │         │
│  ┌──────┴─────────────────┴──────────────────┴──────┐ │
│  │           Application State Machine                 │ │
│  │  (Similar to ESP32 but Python-based)                │ │
│  └──────┬─────────────────┬──────────────────┬───────┘ │
│         │                 │                  │         │
│  ┌──────┴──────┐  ┌───────┴──────┐  ┌───────┴──────┐ │
│  │  Animation │  │ Local AI     │  │  Voice       │ │
│  │  Controller│  │ Pipeline     │  │  Control      │ │
│  │  (LVGL/PIL)│  │ (LLM/TTS/ASR)│  │  (Wake Word)  │ │
│  └────────────┘  └──────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────┘
```

## Implementation Plan

### Phase 1: Core Infrastructure (Week 1-2)

#### 1.1 Python Project Structure
```
babymilu-rpi5/
├── main.py                 # Entry point
├── application.py          # Main state machine (port from application.cc)
├── state_machine.py        # Device state management
├── config/
│   └── settings.py        # Configuration management
├── hardware/
│   ├── display.py         # Display abstraction (LVGL or PIL/pygame)
│   ├── audio.py           # Audio I/O (PyAudio/sounddevice)
│   └── gpio.py            # GPIO control (RPi.GPIO or gpiozero)
├── animation/
│   ├── loader.py          # Load animations from files
│   ├── controller.py      # Animation state management
│   └── renderer.py        # Display rendering
├── audio/
│   ├── pipeline.py        # Audio processing pipeline
│   ├── wake_word.py       # Wake word detection
│   └── codec.py           # Opus encoding/decoding (opuslib)
├── ai/
│   ├── llm.py             # LLM integration (LLM 8850)
│   ├── tts.py             # Text-to-speech (local)
│   └── asr.py             # Speech recognition (local)
└── utils/
    ├── logger.py          # Logging
    └── async_helpers.py   # Async utilities
```

#### 1.2 Key Dependencies
```python
# requirements.txt
# Audio
pyaudio>=0.2.11
sounddevice>=0.4.6
opuslib>=3.0.1
numpy>=1.24.0

# AI/ML
transformers>=4.35.0
llama-cpp-python>=0.2.0  # For LLM 8850 integration
whisper>=1.1.0           # ASR
piper-tts>=1.0.0         # TTS (lightweight)
# OR coqui-tts>=0.20.0    # TTS (higher quality)

# Display
Pillow>=10.0.0
pygame>=2.5.0            # Alternative to LVGL
# OR lvgl-python>=0.1.0   # If LVGL Python bindings available

# Hardware
RPi.GPIO>=0.7.1
gpiozero>=1.6.2

# Async/Networking
aiohttp>=3.9.0
websockets>=12.0
paho-mqtt>=2.0.0

# Utilities
asyncio>=3.4.3
pydantic>=2.5.0
```

### Phase 2: State Machine Port (Week 2-3)

#### 2.1 Device States (Direct Port)
```python
# state_machine.py
from enum import Enum

class DeviceState(Enum):
    UNKNOWN = "unknown"
    STARTING = "starting"
    WIFI_CONFIGURING = "configuring"
    IDLE = "idle"
    CONNECTING = "connecting"
    LISTENING = "listening"
    SPEAKING = "speaking"
    UPGRADING = "upgrading"
    ACTIVATING = "activating"
    AUDIO_TESTING = "audio_testing"
    FATAL_ERROR = "fatal_error"
```

#### 2.2 Application Class (Python Port)
```python
# application.py
import asyncio
from typing import Optional, Callable
from state_machine import DeviceState

class Application:
    _instance = None
    
    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance
    
    def __init__(self):
        self.device_state = DeviceState.UNKNOWN
        self.audio_queue = asyncio.Queue()
        self.main_tasks = asyncio.Queue()
        # ... similar structure to C++ version
        
    async def start(self):
        """Main entry point - port from Application::Start()"""
        # Initialize hardware
        # Start state machine
        # Enter main loop
        
    async def set_device_state(self, state: DeviceState):
        """Port from Application::SetDeviceState()"""
        # Similar logic to C++ version
        
    async def toggle_chat_state(self):
        """Port from Application::ToggleChatState()"""
        # Handle button press / wake word
```

### Phase 3: Animation System (Week 3-4)

#### 3.1 Animation Loader
```python
# animation/loader.py
from PIL import Image
import struct

class AnimationLoader:
    """Load animations from binary files (compatible with ESP32 format)"""
    
    def load_from_bin(self, filepath: str) -> list:
        """Load animation frames from .bin file"""
        # Parse LVGL format or convert to PIL Image
        # Reuse existing animation binary files from ESP32
        
    def load_mega_animation(self, filepath: str) -> dict:
        """Load all animations from mega file (test.bin)"""
        # Parse the mega animation format
        # Return dict: {emotion: [frames]}
```

#### 3.2 Animation Controller
```python
# animation/controller.py
import asyncio
from enum import Enum

class AnimationType(Enum):
    NORMAL = 0
    EMBARRASS = 1
    FIRE = 2
    # ... etc (match ESP32 enum)
    
class AnimationController:
    def __init__(self, display):
        self.current_animation = AnimationType.NORMAL
        self.position = 0
        self.display = display
        self.running = False
        
    async def start(self):
        """Start animation loop"""
        self.running = True
        while self.running:
            await self._render_frame()
            await asyncio.sleep(0.5)  # 500ms per frame
            
    def set_animation(self, animation_type: AnimationType):
        """Change animation based on emotion"""
        self.current_animation = animation_type
        self.position = 0
        
    async def _render_frame(self):
        """Render current frame"""
        # Get current frame from animation
        # Update display
```

### Phase 4: Local AI Pipeline (Week 4-5)

#### 4.1 LLM Integration (LLM 8850)
```python
# ai/llm.py
import ctypes
# LLM 8850 Python bindings (check manufacturer SDK)

class LLM8850:
    def __init__(self):
        # Initialize LLM 8850 accelerator
        self.model_loaded = False
        
    def load_model(self, model_path: str):
        """Load LLM model onto accelerator"""
        # Use LLM 8850 SDK
        
    async def generate(self, prompt: str, context: list) -> str:
        """Generate response using local LLM"""
        # Run inference on LLM 8850
        # Return generated text
```

#### 4.2 TTS Integration
```python
# ai/tts.py
import piper_tts  # or coqui-tts

class LocalTTS:
    def __init__(self):
        self.engine = piper_tts.PiperTTS()
        self.engine.load_model("path/to/model")
        
    async def synthesize(self, text: str) -> bytes:
        """Convert text to speech audio"""
        audio = self.engine.synthesize(text)
        return audio
```

#### 4.3 ASR Integration
```python
# ai/asr.py
import whisper

class LocalASR:
    def __init__(self):
        self.model = whisper.load_model("base")  # or "small", "medium"
        
    async def transcribe(self, audio_data: np.ndarray) -> str:
        """Convert speech to text"""
        result = self.model.transcribe(audio_data)
        return result["text"]
```

#### 4.4 Voice Control (Animation/Volume)
```python
# ai/voice_control.py
class VoiceControl:
    def __init__(self, asr, llm):
        self.asr = asr
        self.llm = llm
        self.commands = {
            "change animation": self._handle_animation_change,
            "adjust volume": self._handle_volume_change,
        }
        
    async def process_command(self, audio: np.ndarray):
        """Process voice command"""
        text = await self.asr.transcribe(audio)
        
        # Use LLM to extract intent
        intent = await self._extract_intent(text)
        
        if intent in self.commands:
            await self.commands[intent](text)
            
    async def _handle_animation_change(self, text: str):
        """Parse animation change command"""
        # Extract emotion/animation type
        # Call animation_controller.set_animation()
        
    async def _handle_volume_change(self, text: str):
        """Parse volume change command"""
        # Extract volume level
        # Adjust audio output volume
```

### Phase 5: Audio Pipeline (Week 5-6)

#### 5.1 Audio I/O
```python
# audio/pipeline.py
import sounddevice as sd
import numpy as np
import opuslib

class AudioPipeline:
    def __init__(self):
        self.sample_rate = 16000
        self.channels = 1
        self.opus_encoder = opuslib.Encoder(
            self.sample_rate, self.channels, opuslib.APPLICATION_VOIP
        )
        self.opus_decoder = opuslib.Decoder(
            self.sample_rate, self.channels
        )
        
    async def start_capture(self, callback):
        """Start audio capture"""
        def audio_callback(indata, frames, time, status):
            # Process audio
            asyncio.create_task(callback(indata.copy()))
            
        with sd.InputStream(
            samplerate=self.sample_rate,
            channels=self.channels,
            callback=audio_callback
        ):
            await asyncio.Event().wait()
            
    async def play_audio(self, audio_data: np.ndarray):
        """Play audio output"""
        sd.play(audio_data, samplerate=self.sample_rate)
        await sd.wait()
```

### Phase 6: Integration & Testing (Week 6-7)

#### 6.1 Main Application Flow
```python
# main.py
import asyncio
from application import Application

async def main():
    app = Application()
    await app.start()
    
    # Main event loop
    while True:
        await asyncio.sleep(1)

if __name__ == "__main__":
    asyncio.run(main())
```

#### 6.2 Conversation Flow
```python
# In Application class
async def handle_conversation(self):
    """Local conversation pipeline"""
    # 1. Wake word detected or button pressed
    self.set_device_state(DeviceState.LISTENING)
    
    # 2. Capture audio
    audio = await self.audio_pipeline.capture()
    
    # 3. Transcribe (local ASR)
    text = await self.asr.transcribe(audio)
    
    # 4. Check for voice commands
    if await self.voice_control.is_command(text):
        await self.voice_control.process_command(audio)
        return
    
    # 5. Generate response (local LLM)
    response = await self.llm.generate(text, self.conversation_history)
    
    # 6. Synthesize speech (local TTS)
    audio_response = await self.tts.synthesize(response)
    
    # 7. Play response
    self.set_device_state(DeviceState.SPEAKING)
    await self.audio_pipeline.play_audio(audio_response)
    
    # 8. Update emotion/animation
    emotion = await self.llm.extract_emotion(response)
    self.animation_controller.set_animation(emotion)
    
    # 9. Return to idle
    self.set_device_state(DeviceState.IDLE)
```

## Alternative Approaches

### Option A: Hybrid C++/Python (More Complex)
- Keep C++ for real-time audio/display
- Use Python for AI/ML via bindings
- **Pros**: Better performance for audio pipeline
- **Cons**: More complex, harder to maintain

### Option B: Full Python Rewrite (Recommended)
- Pure Python implementation
- Use optimized libraries (numpy, numba) for performance
- **Pros**: Easier development, better AI integration
- **Cons**: Slightly less real-time performance (usually acceptable)

### Option C: Use Existing Framework
- **Rhasspy**: Open-source voice assistant framework
- **Mycroft**: Voice assistant platform
- **Pros**: Pre-built infrastructure
- **Cons**: Less customization, may not match your UX

## Migration Checklist

### Hardware Setup
- [ ] Raspberry Pi 5 with adequate cooling
- [ ] LLM 8850 AI accelerator connected
- [ ] Display (compatible with your ESP32 display or new one)
- [ ] Audio I/O (USB mic/speaker or I2S codec)
- [ ] GPIO for buttons/LEDs
- [ ] SD card for animations

### Software Setup
- [ ] Install Raspberry Pi OS (64-bit recommended)
- [ ] Install Python 3.11+
- [ ] Install LLM 8850 SDK/drivers
- [ ] Install audio drivers
- [ ] Install display drivers

### Development
- [ ] Port state machine
- [ ] Port animation system
- [ ] Implement audio pipeline
- [ ] Integrate local LLM
- [ ] Integrate local TTS
- [ ] Integrate local ASR
- [ ] Implement voice commands
- [ ] Testing & optimization

## Performance Considerations

### LLM 8850 Integration
- Check manufacturer SDK for Python bindings
- May need C++ wrapper if only C API available
- Consider model quantization for faster inference

### Real-time Audio
- Use asyncio for non-blocking I/O
- Consider separate thread for audio processing
- Buffer management similar to ESP32 version

### Display Performance
- PIL/pygame should be sufficient for animation frames
- Consider hardware acceleration if available
- Reuse existing animation binary format

## Estimated Timeline

- **Week 1-2**: Core infrastructure, state machine port
- **Week 3-4**: Animation system, display integration
- **Week 4-5**: Local AI pipeline (LLM/TTS/ASR)
- **Week 5-6**: Audio pipeline, voice commands
- **Week 6-7**: Integration, testing, optimization
- **Total**: ~7 weeks for MVP

## Next Steps

1. **Set up Raspberry Pi 5 development environment**
2. **Test LLM 8850 SDK and Python integration**
3. **Create minimal proof-of-concept** (audio I/O + simple LLM)
4. **Port animation system** (reuse binary files)
5. **Incremental port** of state machine
6. **Integrate AI components** one by one
7. **Test and optimize**

## Resources

- **LVGL Python**: https://github.com/lvgl/lvgl (check Python bindings)
- **Piper TTS**: https://github.com/rhasspy/piper
- **Whisper**: https://github.com/openai/whisper
- **LLM 8850**: Check manufacturer documentation
- **Raspberry Pi Audio**: https://www.raspberrypi.com/documentation/computers/audio.html

## Conclusion

**Python is the right choice** for this migration. It provides:
- Better AI/ML integration
- Faster development
- Easier maintenance
- Sufficient performance for your use case

The architecture should mirror your ESP32 firmware's state machine and animation patterns, but leverage Python's strengths for the AI components.

