# BabyMilu Firmware Codebase Walkthrough

**Version:** 1.0  
**Last Updated:** December 2024  
**Project:** BabyMilu ESP32 Firmware (Xiaozhi Assistant)  
**ESP-IDF Version:** v5.5.1  
**Firmware Version:** 1.7.6

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Architecture Overview](#2-architecture-overview)
3. [Configuration System](#3-configuration-system)
4. [Protocols and Endpoints](#4-protocols-and-endpoints)
5. [Key Features and Code Locations](#5-key-features-and-code-locations)
6. [Board Support System](#6-board-support-system)
7. [Debug Guide](#7-debug-guide)
8. [Build and Deployment](#8-build-and-deployment)
9. [Production Considerations](#9-production-considerations)
10. [Code Organization](#10-code-organization)

---

## 1. Executive Summary

### 1.1 Project Overview

BabyMilu is an ESP32-based voice assistant firmware that provides:
- **Voice Interaction**: Wake word detection, speech-to-text, text-to-speech
- **Dual Protocol Support**: MQTT (control) and WebSocket (audio streaming)
- **Multi-Board Support**: 70+ ESP32 board variants
- **IoT Control**: MCP (Model Context Protocol) for device control
- **OTA Updates**: Over-the-air firmware updates
- **Multi-Language**: Chinese (Simplified/Traditional), English, Japanese

### 1.2 Technology Stack

- **Framework**: ESP-IDF v5.5.1
- **Language**: C++17 with C interop
- **Build System**: CMake
- **Audio Codec**: Opus (encoding/decoding)
- **Networking**: WiFi, BLE (for configuration), ML307 (4G modem option)
- **Storage**: NVS (Non-Volatile Storage), SPIFFS, SD Card (optional)

### 1.3 Key Design Principles

1. **Modular Architecture**: Board-agnostic core with board-specific implementations
2. **Protocol Flexibility**: Dual-protocol design (MQTT + WebSocket)
3. **Graceful Degradation**: Fallback mechanisms for network/protocol failures
4. **Production Ready**: OTA updates, error handling, logging, state management

---

## 2. Architecture Overview

### 2.1 System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │  Audio   │  │  Wake    │  │  Display │  │   LED    │    │
│  │ Processor│  │   Word   │  │          │  │          │    │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘    │
├─────────────────────────────────────────────────────────────┤
│                    Protocol Layer                            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│  │   MQTT   │  │WebSocket │  │   MCP    │                  │
│  │(Primary) │  │(On-Demand)│ │  Server  │                  │
│  └──────────┘  └──────────┘  └──────────┘                  │
├─────────────────────────────────────────────────────────────┤
│                    Board Abstraction Layer                   │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │  Audio   │  │  Display │  │  Network │  │  Camera  │   │
│  │  Codec │  │          │  │  (WiFi/BLE)│  │ (Optional)│   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
├─────────────────────────────────────────────────────────────┤
│                    Hardware Layer (ESP32)                   │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │   I2S    │  │   I2C    │  │   SPI    │  │   GPIO   │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 Core Components

#### 2.2.1 Application (`main/application.cc`)

**Purpose**: Main application controller managing device state, audio processing, and protocol coordination.

**Key Responsibilities**:
- Device state machine management
- Audio encoding/decoding coordination
- Protocol selection and routing
- Wake word detection handling
- Background task management

**State Machine**:
```cpp
enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateAudioTesting,
    kDeviceStateFatalError
};
```

**Key Methods**:
- `Start()`: Initializes all subsystems
- `MainEventLoop()`: Main event processing loop
- `AudioLoop()`: Audio I/O processing
- `GetActiveProtocol()`: Returns active protocol for audio (WebSocket priority)
- `OpenWebSocketConnection()`: Opens WebSocket for conversations

#### 2.2.2 Board Abstraction (`main/boards/common/board.h`)

**Purpose**: Hardware abstraction layer for board-specific implementations.

**Key Interfaces**:
- `GetAudioCodec()`: Returns audio codec instance
- `GetDisplay()`: Returns display instance
- `GetLed()`: Returns LED controller
- `CreateHttp()`, `CreateWebSocket()`, `CreateMqtt()`, `CreateUdp()`: Network factory methods
- `StartNetwork()`: Initializes network (WiFi/BLE/ML307)

**Board Types**: 70+ board implementations in `main/boards/`

#### 2.2.3 Protocol Layer (`main/protocols/`)

**Base Protocol** (`protocol.h`):
- Abstract interface for all protocols
- Audio streaming interface
- JSON message handling
- Callback system

**MQTT Protocol** (`mqtt_protocol.cc`):
- Always connected (if configured)
- Handles control messages (`ws_start`, wake word)
- Supports MQTT+UDP audio (legacy)
- Topic pattern: `xiaozhi/{mac}/up` (publish), `xiaozhi/{mac}/down` (subscribe)

**WebSocket Protocol** (`websocket_protocol.cc`):
- Created on-demand for audio conversations
- Supports protocol versions 2 and 3
- Default URL fallback: `ws://136.117.60.16:8000/xiaozhi/v1/`
- Binary audio streaming (Opus frames)

### 2.3 Data Flow

#### 2.3.1 Audio Input Flow

```
Microphone → AudioCodec → AudioProcessor → OpusEncoder → Protocol → Server
     ↓            ↓              ↓              ↓
  I2S Input   Resample      VAD/AEC      Frame Encode
```

#### 2.3.2 Audio Output Flow

```
Server → Protocol → OpusDecoder → Resampler → AudioCodec → Speaker
                           ↓            ↓           ↓
                    Frame Decode   Rate Match   I2S Output
```

#### 2.3.3 Message Flow

```
Server → Protocol → OnIncomingJson() → Application → State Machine
                                              ↓
                                    Display/LED/Animation Updates
```

---

## 3. Configuration System

### 3.1 Build-Time Configuration

**Location**: `main/Kconfig.projbuild`

**Key Options**:

1. **OTA URL** (`CONFIG_OTA_URL`)
   - Default: Empty (can be set in menuconfig)
   - Format: `http://host:port/path` or `https://host:port/path`
   - Used for firmware version checks and updates

2. **Board Type** (`CONFIG_BOARD_TYPE_*`)
   - 70+ board options
   - Each board has specific hardware configuration
   - Selected in menuconfig: `Xiaozhi Assistant → Board Type`

3. **Language** (`CONFIG_LANGUAGE_*`)
   - Options: `ZH_CN`, `ZH_TW`, `EN_US`, `JA_JP`
   - Affects UI strings and audio prompts

4. **Audio Processing**:
   - `CONFIG_USE_AUDIO_PROCESSOR`: Enable noise reduction (requires ESP32-S3 + PSRAM)
   - `CONFIG_USE_AFE_WAKE_WORD`: AFE-based wake word (ESP32-S3/P4 + PSRAM)
   - `CONFIG_USE_ESP_WAKE_WORD`: ESP wake word (ESP32-C3/C5/C6, ESP32 with PSRAM)
   - `CONFIG_USE_DEVICE_AEC`: Device-side acoustic echo cancellation
   - `CONFIG_USE_SERVER_AEC`: Server-side AEC (unstable)

5. **IoT Protocol** (`CONFIG_IOT_PROTOCOL_*`):
   - `MCP`: Model Context Protocol (recommended)
   - `XIAOZHI`: Legacy IoT protocol (deprecated)

### 3.2 Runtime Configuration (NVS)

**Location**: `main/settings.cc`

**Settings Namespaces**:

1. **MQTT** (`"mqtt"`):
   - `endpoint`: MQTT broker address (e.g., `mqtt://host:1883`)
   - `client_id`: Client identifier (default: MAC address)
   - `publish_topic`: Topic for publishing (default: `xiaozhi/{mac}/up`)
   - `subscribe_topic`: Topic for subscribing (default: `xiaozhi/{mac}/down`)
   - `username`: MQTT username (optional)
   - `password`: MQTT password (optional)
   - `keepalive`: Keepalive interval in seconds (default: 120)

2. **WebSocket** (`"websocket"`):
   - `url`: WebSocket server URL (e.g., `wss://server.com/xiaozhi/v1/`)
   - `version`: Protocol version (2 or 3, default: 2)
   - `token`: Authentication token (optional)

3. **WiFi** (`"wifi"`):
   - Managed by `SsidManager` (multiple SSIDs supported)
   - Stored as list of SSID/password pairs

4. **Network** (`"network"`):
   - `type`: Network type (0=WiFi, 1=ML307)

5. **Animation** (`"animation"`):
   - `server_url`: Animation update server URL
   - `enabled`: Auto-update enabled flag
   - `check_interval`: Check interval in seconds

### 3.3 Configuration Priority

1. **Runtime Settings** (NVS) - Highest priority
2. **OTA Server Response** - Fetched on startup
3. **Build-Time Defaults** - CMake definitions
4. **Hardcoded Fallbacks** - Last resort

### 3.4 Configuration Examples

**Build-Time MQTT Configuration**:
```bash
idf.py build -DDEFAULT_MQTT_ENDPOINT="mqtt.example.com:1883" \
             -DDEFAULT_MQTT_PUBLISH_TEMPLATE="xiaozhi/%s/up"
```

**Runtime Configuration via Code**:
```cpp
Settings mqtt_settings("mqtt", true);
mqtt_settings.SetString("endpoint", "mqtt://broker.com:1883");
mqtt_settings.SetString("client_id", "device-001");
```

---

## 4. Protocols and Endpoints

### 4.1 MQTT Protocol

**Implementation**: `main/protocols/mqtt_protocol.cc`

**Connection Flow**:
1. Device reads MQTT config from settings or OTA response
2. Connects to MQTT broker
3. Subscribes to `{subscribe_topic}` (e.g., `xiaozhi/{mac}/down`)
4. Publishes to `{publish_topic}` (e.g., `xiaozhi/{mac}/up`)

**Message Types**:

1. **Hello Message** (Device → Server):
   ```json
   {
     "type": "hello",
     "version": 3,
     "transport": "udp",
     "features": {
       "aec": true,
       "mcp": true
     },
     "audio_params": {
       "format": "opus",
       "sample_rate": 16000,
       "channels": 1,
       "frame_duration": 60
     }
   }
   ```

2. **ws_start Message** (Server → Device):
   ```json
   {
     "type": "ws_start",
     "wss": "wss://server.com/xiaozhi/v1/",
     "version": 3
   }
   ```
   - Triggers WebSocket connection
   - URL validated (rejects localhost/loopback)
   - Saved to settings

3. **Goodbye Message** (Device → Server):
   ```json
   {
     "type": "goodbye",
     "session_id": "..."
   }
   ```

**Endpoints**:
- **Broker**: Configured via settings or OTA
- **Default Port**: 1883 (non-TLS), 8883 (TLS)
- **Topic Pattern**: `xiaozhi/{mac_address}/up` (publish), `xiaozhi/{mac_address}/down` (subscribe)

**UDP Audio Channel** (Legacy):
- Encrypted AES-CTR audio packets
- UDP server/port/key from server hello response
- Format: `|type 1u|flags 1u|payload_len 2u|ssrc 4u|timestamp 4u|sequence 4u|payload|`

### 4.2 WebSocket Protocol

**Implementation**: `main/protocols/websocket_protocol.cc`

**Connection Flow**:
1. Device reads WebSocket URL from settings (or uses default)
2. Adds query parameters: `?device-id={mac}&client-id={uuid}`
3. Connects to WebSocket server
4. Sends hello message
5. Waits for server hello response
6. Audio channel opened

**Default URL**: `ws://136.117.60.16:8000/xiaozhi/v1/`

**Protocol Versions**:
- **Version 2**: BinaryProtocol2 wrapper
- **Version 3**: Raw Opus frames (preferred)

**Message Types**:

1. **Hello Message** (Device → Server):
   ```json
   {
     "type": "hello",
     "version": 3,
     "transport": "websocket",
     "features": {
       "aec": true,
       "mcp": true
     },
     "audio_params": {
       "format": "opus",
       "sample_rate": 16000,
       "channels": 1,
       "frame_duration": 60
     }
   }
   ```

2. **Audio Frames** (Binary):
   - Version 2: BinaryProtocol2 wrapper
   - Version 3: Raw Opus frames (60ms, 16kHz, mono)

3. **JSON Messages** (Text):
   - TTS state changes
   - STT transcripts
   - LLM emotions
   - MCP messages
   - System commands

**Endpoints**:
- **Base URL**: From settings or default: `ws://136.117.60.16:8000/xiaozhi/v1/`
- **Query Params**: `?device-id={mac}&client-id={uuid}`
- **Headers**: `Authorization`, `Protocol-Version`, `Device-Id`, `Client-Id`

### 4.3 MCP (Model Context Protocol)

**Implementation**: `main/mcp_server.cc`

**Purpose**: IoT device control and status reporting

**Protocol**: JSON-RPC 2.0 wrapped in protocol messages

**Message Format**:
```json
{
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "method": "tools/list",
    "params": { ... },
    "id": 1,
    "result": { ... }
  }
}
```

**Methods**:

1. **initialize**: Initialize MCP session
   - Client sends capabilities (e.g., vision URL for camera)
   - Device responds with protocol version and server info

2. **tools/list**: Get available tools
   - Supports pagination via `cursor`
   - Returns tool definitions with schemas

3. **tools/call**: Execute a tool
   - Parameters: `name`, `arguments`, `stackSize`
   - Returns execution result or error

**Common Tools** (defined in `McpServer::AddCommonTools()`):
- `self.get_device_status`: Get device status (audio, screen, battery, network)
- `self.audio_speaker.set_volume`: Set audio volume (0-100)
- `self.screen.set_brightness`: Set screen brightness (0-100)
- `self.screen.set_theme`: Set screen theme (light/dark)
- `self.camera.take_photo`: Take photo and explain
- `self.animation_updater.*`: Animation updater controls

**Documentation**: `docs/mcp-protocol.md`

### 4.4 OTA Protocol

**Implementation**: `main/ota.cc`

**Endpoint**: Configured via `CONFIG_OTA_URL` or default: `http://136.117.60.16:8003/xiaozhi/ota/`

**Request Format**:
- **Method**: GET or POST (if device info provided)
- **Headers**:
  - `Device-Id`: MAC address
  - `Client-Id`: Device UUID
  - `Activation-Version`: "1" or "2" (if serial number)
  - `Serial-Number`: (if available)
  - `User-Agent`: `{BOARD_NAME}/{version}`
  - `Accept-Language`: Language code

**Response Format**:
```json
{
  "firmware": {
    "version": "1.7.7",
    "url": "http://server.com/firmware.bin"
  },
  "mqtt": {
    "endpoint": "mqtt://broker.com:1883",
    "client_id": "...",
    "publish_topic": "...",
    "subscribe_topic": "..."
  },
  "websocket": {
    "url": "wss://server.com/xiaozhi/v1/",
    "version": 3,
    "token": "..."
  },
  "activation": {
    "code": "123456",
    "message": "Please enter activation code"
  },
  "server_time": 1234567890
}
```

**Flow**:
1. Device checks OTA URL on startup
2. Compares firmware version
3. Downloads and installs if newer version available
4. Extracts MQTT/WebSocket config from response
5. Updates settings accordingly

---

## 5. Key Features and Code Locations

### 5.1 Audio Processing

**Location**: `main/audio_processing/`

**Components**:

1. **Audio Processor** (`audio_processor.h`):
   - Base interface for audio processing
   - Implementations:
     - `afe_audio_processor.cc`: AFE-based processing (ESP32-S3/P4)
     - `no_audio_processor.cc`: No processing (fallback)

2. **Wake Word Detection** (`wake_word.h`):
   - Base interface for wake word
   - Implementations:
     - `afe_wake_word.cc`: AFE-based (ESP32-S3/P4)
     - `esp_wake_word.cc`: ESP wake word (ESP32-C3/C5/C6)
     - `no_wake_word.cc`: Disabled

3. **Audio Debugger** (`audio_debugger.cc`):
   - UDP-based audio debugging
   - Sends raw audio to debug server
   - Configurable via `CONFIG_AUDIO_DEBUG_UDP_SERVER`

**Audio Codecs** (`main/audio_codecs/`):
- `es8311_audio_codec.cc`: ES8311 codec
- `es8374_audio_codec.cc`: ES8374 codec
- `es8388_audio_codec.cc`: ES8388 codec
- `box_audio_codec.cc`: ESP-BOX codec
- `no_audio_codec.cc`: No codec (fallback)

**Key Files**:
- `main/application.cc:1236-1365`: Audio loop implementation
- `main/application.cc:1026-1054`: Audio encoding pipeline
- `main/application.cc:1249-1308`: Audio decoding pipeline

### 5.2 Display System

**Location**: `main/display/`

**Components**:

1. **Display Interface** (`display.h`):
   - Abstract display interface
   - Methods: `SetStatus()`, `SetEmotion()`, `SetChatMessage()`, `ShowNotification()`

2. **Implementations**:
   - `lcd_display.cc`: LCD display (ST7789, ILI9341, etc.)
   - `oled_display.cc`: OLED display (SSD1306, SH1106)

**Board-Specific**: Each board implements display initialization in board files

### 5.3 LED Control

**Location**: `main/led/`

**Components**:
- `single_led.cc`: Single LED control
- `circular_strip.cc`: Circular LED strip
- `gpio_led.cc`: GPIO-based LED

**State Indication**:
- Idle: Off or dim
- Listening: Red/pulsing
- Speaking: Blue/pulsing
- Error: Red blinking

### 5.4 Animation System

**Location**: `main/animation/`

**Components**:

1. **Animation Updater** (`animation_updater.cc`):
   - Downloads animations from server
   - Supports SD card and SPIFFS storage
   - Server URL configurable via settings
   - Auto-update with configurable interval

2. **Animation Player** (`animation.cc`):
   - Plays animation frames
   - Supports multiple animation types (normal, happy, sad, etc.)
   - Frame format: Binary image data

**Storage**:
- **SPIFFS**: `spiffs_data/` directory (compiled into firmware)
- **SD Card**: `/animations/` directory (runtime loading)

**Key Files**:
- `main/animation/animation_updater.cc`: Update logic
- `main/animation/animation.cc`: Playback logic
- `main/sd_card.cc`: SD card support

### 5.5 Network Management

**Location**: `main/boards/common/`

**Components**:

1. **WiFi Board** (`wifi_board.cc`):
   - WiFi station mode
   - BLE provisioning (nimBLE)
   - SSID management (multiple networks)
   - Auto-reconnect with retry

2. **ML307 Board** (`ml307_board.cc`):
   - 4G modem support (ML307 module)
   - AT command interface
   - Network switching capability

3. **Dual Network Board** (`dual_network_board.cc`):
   - Supports both WiFi and ML307
   - Runtime network switching
   - Network type stored in settings

**Key Files**:
- `main/boards/common/wifi_board.cc`: WiFi implementation
- `main/boards/common/ssid_manager.h`: SSID management
- `main/boards/common/wifi_station.h`: WiFi station wrapper

### 5.6 IoT Control (MCP)

**Location**: `main/mcp_server.cc`, `main/iot/`

**Components**:

1. **MCP Server** (`mcp_server.cc`):
   - JSON-RPC 2.0 server
   - Tool registration and execution
   - Common tools (volume, brightness, camera, etc.)

2. **Thing Manager** (`iot/thing_manager.cc`):
   - Legacy IoT protocol (deprecated)
   - Device registration and control
   - State management

**Key Files**:
- `main/mcp_server.cc`: MCP server implementation
- `main/iot/thing_manager.cc`: Legacy IoT manager
- `docs/mcp-protocol.md`: MCP protocol documentation

### 5.7 OTA Updates

**Location**: `main/ota.cc`

**Features**:
- Version checking
- Firmware download and installation
- Rollback protection
- Activation code support
- Server time synchronization

**Key Files**:
- `main/ota.cc`: OTA implementation
- `main/ota.h`: OTA interface

### 5.8 SD Card Support

**Location**: `main/sd_card.cc`, `main/sd_card_startup.cc`

**Features**:
- SD card initialization
- Animation file loading
- Text file reading
- Persistent mounting

**Key Files**:
- `main/sd_card.cc`: SD card operations
- `main/sd_card_startup.cc`: Startup initialization
- `docs/sd-card-functionality.md`: Documentation

---

## 6. Board Support System

### 6.1 Board Structure

Each board has its own directory in `main/boards/{board-name}/`:

```
boards/
├── {board-name}/
│   ├── {board_name}_board.cc    # Board implementation
│   ├── {board_name}_board.h     # Board header
│   ├── config.h                  # Hardware configuration
│   ├── config.json               # Build configuration
│   └── README.md                 # Board documentation
```

### 6.2 Board Implementation

**Base Class**: `Board` (`main/boards/common/board.h`)

**Required Methods**:
- `GetBoardType()`: Returns board identifier
- `GetAudioCodec()`: Returns audio codec instance
- `CreateHttp()`, `CreateWebSocket()`, `CreateMqtt()`, `CreateUdp()`: Network factories
- `StartNetwork()`: Initialize network

**Optional Methods**:
- `GetDisplay()`: Display instance
- `GetLed()`: LED controller
- `GetCamera()`: Camera instance
- `GetBacklight()`: Backlight controller
- `GetBatteryLevel()`: Battery status

### 6.3 Configuration File (`config.h`)

**Typical Contents**:
```c
// Audio configuration
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_10
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_11
// ... more GPIO definitions

// Display configuration
#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 320
// ... display pins

// Button configuration
#define BUTTON_GPIO GPIO_NUM_0
```

### 6.4 Adding a New Board

1. Create board directory: `main/boards/my-board/`
2. Create `config.h` with hardware definitions
3. Create `my_board.cc` implementing `Board` interface
4. Add board type to `main/Kconfig.projbuild`
5. Add board case to `main/CMakeLists.txt`
6. Create `config.json` for build configuration

**Documentation**: `main/boards/README.md`

---

## 7. Debug Guide

### 7.1 Logging System

**ESP-IDF Logging**: Uses `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`

**Key Log Tags**:
- `Application`: Main application flow
- `MQTT`: MQTT protocol messages
- `WS`: WebSocket protocol messages
- `Ota`: OTA update process
- `Audio`: Audio processing
- `Board`: Board initialization
- `WiFi`: WiFi connection status
- `MCP`: MCP protocol messages

**Log Levels**:
- `ESP_LOGI`: Informational (normal operation)
- `ESP_LOGW`: Warnings (non-critical issues)
- `ESP_LOGE`: Errors (critical failures)
- `ESP_LOGD`: Debug (detailed debugging)

### 7.2 Serial Monitor

**Command**:
```bash
idf.py monitor
```

**Useful Filters**:
```bash
# Filter by tag
idf.py monitor --print-filter="MQTT:*"

# Filter by level
idf.py monitor --print-filter="*:E"  # Errors only
```

### 7.3 Common Debug Scenarios

#### 7.3.1 MQTT Connection Issues

**Symptoms**:
- `MQTT: Failed to connect to endpoint`
- `MQTT: MQTT endpoint is not configured`

**Debug Steps**:
1. Check settings: `Settings("mqtt").GetString("endpoint")`
2. Verify OTA response includes MQTT config
3. Check network connectivity
4. Verify broker address and port
5. Check ACL/permissions on broker

**Logs to Check**:
```
I (xxx) MQTT: MQTT config: endpoint=..., client_id=..., up_topic=..., down_topic=...
I (xxx) MQTT: Connecting to endpoint ...
I (xxx) MQTT: Connected to endpoint
I (xxx) MQTT: Successfully subscribed to topic: ...
```

#### 7.3.2 WebSocket Connection Issues

**Symptoms**:
- `WS: Failed to connect to websocket server`
- `WS: No valid WebSocket URL configured`

**Debug Steps**:
1. Check settings: `Settings("websocket").GetString("url")`
2. Verify default URL is accessible: `ws://136.117.60.16:8000/xiaozhi/v1/`
3. Check URL validation (no localhost/loopback)
4. Verify server accepts WebSocket connections
5. Check firewall/network settings

**Logs to Check**:
```
I (xxx) WS: Using WebSocket URL from settings: ...
I (xxx) WS: Connecting to websocket server: ... with version: 3
I (xxx) WS: Sending hello message: ...
I (xxx) WS: Session ID: ...
```

#### 7.3.3 Audio Issues

**Symptoms**:
- No audio input/output
- Audio distortion
- Audio dropouts

**Debug Steps**:
1. Check audio codec initialization
2. Verify sample rates match (input/output/server)
3. Check Opus encoder/decoder state
4. Monitor audio queue sizes
5. Use audio debugger: `CONFIG_USE_AUDIO_DEBUGGER`

**Logs to Check**:
```
I (xxx) Application: Audio config: input_sample_rate=..., Opus encoder=16000Hz mono
I (xxx) Application: Starting audio capture and streaming...
I (xxx) Application: Too many audio packets in queue, drop the oldest packet
```

#### 7.3.4 OTA Update Issues

**Symptoms**:
- `Ota: Failed to check version`
- `Ota: Failed to get firmware`

**Debug Steps**:
1. Verify OTA URL is configured: `CONFIG_OTA_URL`
2. Check HTTP connectivity
3. Verify server response format
4. Check firmware URL accessibility
5. Verify partition table has OTA partitions

**Logs to Check**:
```
I (xxx) Ota: Current version: 1.7.6
I (xxx) Ota: OTA server response: ...
I (xxx) Ota: Has new version: true
I (xxx) Ota: Upgrading firmware from ...
```

### 7.4 Heap Monitoring

**Location**: `main/system_info.cc`

**Functions**:
- `SystemInfo::PrintHeapStats()`: Print heap usage
- `SystemInfo::PrintTaskList()`: Print task list
- `SystemInfo::PrintTaskCpuUsage()`: Print CPU usage

**Usage**:
```cpp
SystemInfo::PrintHeapStats();  // Called periodically in Application::OnClockTimer()
```

### 7.5 Audio Debugger

**Configuration**: `CONFIG_USE_AUDIO_DEBUGGER`

**UDP Server**: `CONFIG_AUDIO_DEBUG_UDP_SERVER` (default: `192.168.2.100:8000`)

**Purpose**: Sends raw audio data to external debug server for analysis

**Key Files**:
- `main/audio_processing/audio_debugger.cc`

### 7.6 GDB Debugging

**Setup**:
```bash
idf.py openocd  # In one terminal
idf.py gdb      # In another terminal
```

**Breakpoints**:
```gdb
break application.cc:613  # Application::Start()
break mqtt_protocol.cc:235  # MQTT connection
```

---

## 8. Build and Deployment

### 8.1 Prerequisites

1. **ESP-IDF v5.5.1**: Installed and environment set
2. **Python 3.8+**: For build scripts
3. **CMake 3.16+**: Build system
4. **Board Hardware**: ESP32 variant

### 8.2 Build Process

**1. Configure Project**:
```bash
idf.py menuconfig
```

**Key Selections**:
- `Xiaozhi Assistant → Board Type`: Select your board
- `Xiaozhi Assistant → Default Language`: Select language
- `Xiaozhi Assistant → OTA URL`: Set OTA server URL (optional)
- `Xiaozhi Assistant → IoT Protocol`: Select MCP (recommended)

**2. Build**:
```bash
idf.py build
```

**3. Flash**:
```bash
idf.py flash
```

**4. Monitor**:
```bash
idf.py monitor
```

### 8.3 Build-Time Configuration

**MQTT Endpoint**:
```bash
idf.py build -DDEFAULT_MQTT_ENDPOINT="mqtt.example.com:1883" \
             -DDEFAULT_MQTT_PUBLISH_TEMPLATE="xiaozhi/%s/up"
```

### 8.4 Release Build

**Script**: `scripts/release.py`

**Usage**:
```bash
python scripts/release.py {board-type}
```

**Output**: Creates release package with:
- Firmware binary
- Partition table
- Bootloader
- Flash script

### 8.5 Partition Table

**Location**: `partitions/v1/`

**Default Partitions**:
- `factory`: Main application
- `ota_0`, `ota_1`: OTA partitions
- `nvs`: Non-volatile storage
- `spiffs`: SPIFFS filesystem (animations)
- `otadata`: OTA data

---

## 9. Production Considerations

### 9.1 Security

**Current State**:
- MQTT password support (optional)
- WebSocket token support (optional)
- No TLS by default (can be enabled)

**Recommendations**:
- Enable TLS for MQTT (`mqtts://`)
- Use WSS for WebSocket (`wss://`)
- Implement certificate pinning
- Secure OTA updates with signatures

### 9.2 Error Handling

**Strategies**:
- Graceful degradation (fallback protocols)
- Retry mechanisms (network, OTA)
- State machine prevents invalid transitions
- Error logging with context

**Key Files**:
- `main/application.cc`: State machine error handling
- `main/protocols/*.cc`: Protocol error callbacks

### 9.3 Memory Management

**Constraints**:
- ESP32: Limited RAM (varies by chip)
- PSRAM: Required for audio processing on ESP32-S3
- Heap fragmentation: Managed via FreeRTOS

**Best Practices**:
- Use `std::unique_ptr` for RAII
- Avoid large stack allocations
- Monitor heap usage regularly
- Use static allocation where possible

### 9.4 Performance Optimization

**Audio Processing**:
- Opus complexity set based on board type
- Resampling optimized for common rates
- Audio queue size limits prevent overflow

**Network**:
- Connection pooling (reuse HTTP/WebSocket)
- Keepalive intervals configurable
- Timeout values tuned for network conditions

### 9.5 Testing

**Unit Tests**: Not currently implemented

**Integration Tests**:
- Manual testing with serial monitor
- Audio flow verification: `AUDIO_FLOW_VERIFICATION.md`
- MQTT debugging: `MQTT_DEBUGGING_GUIDE.md`

**Test Scripts**: `scripts/` directory

### 9.6 Deployment Checklist

- [ ] Board type selected in menuconfig
- [ ] OTA URL configured (if using OTA)
- [ ] MQTT endpoint configured (build-time or OTA)
- [ ] Language selected
- [ ] Audio processing options selected (if supported)
- [ ] Partition table verified
- [ ] Firmware version set in `CMakeLists.txt`
- [ ] Build and flash successful
- [ ] Network connectivity verified
- [ ] Protocol connections tested
- [ ] Audio I/O tested
- [ ] OTA update tested (if applicable)

---

## 10. Code Organization

### 10.1 Directory Structure

```
babymilu-firmware/
├── main/                          # Main application code
│   ├── animation/                 # Animation system
│   ├── audio_codecs/              # Audio codec implementations
│   ├── audio_processing/           # Audio processing (VAD, AEC, wake word)
│   ├── boards/                    # Board-specific implementations
│   │   ├── common/                # Common board utilities
│   │   └── {board-name}/          # Individual board implementations
│   ├── display/                   # Display drivers
│   ├── iot/                       # IoT control (legacy)
│   ├── led/                       # LED controllers
│   ├── protocols/                 # Communication protocols
│   ├── assets/                    # Language assets and sounds
│   ├── application.cc             # Main application controller
│   ├── main.cc                    # Entry point
│   ├── mcp_server.cc              # MCP server implementation
│   ├── ota.cc                     # OTA update logic
│   ├── settings.cc                # Settings management
│   └── system_info.cc             # System information
├── docs/                          # Documentation
├── scripts/                       # Build and utility scripts
├── partitions/                    # Partition table definitions
├── animations/                    # Animation binary files
├── images/                        # Animation source images
├── CMakeLists.txt                 # Root CMake file
└── README.md                      # Project README
```

### 10.2 Key Design Patterns

1. **Singleton Pattern**: `Application`, `Board`, `ThingManager`, `McpServer`
2. **Factory Pattern**: Board creation, protocol creation
3. **Strategy Pattern**: Protocol selection, audio processing
4. **Observer Pattern**: Callback system for protocols
5. **RAII**: Smart pointers for resource management

### 10.3 Coding Conventions

**Naming**:
- Classes: `PascalCase` (e.g., `Application`, `MqttProtocol`)
- Functions: `PascalCase` (e.g., `StartListening()`, `GetActiveProtocol()`)
- Variables: `snake_case` (e.g., `device_state_`, `audio_decode_queue_`)
- Constants: `UPPER_SNAKE_CASE` (e.g., `OPUS_FRAME_DURATION_MS`)

**File Organization**:
- One class per file (usually)
- Headers in same directory as implementation
- Board-specific code in `boards/` directory

### 10.4 Dependencies

**ESP-IDF Components**:
- `esp_event`: Event loop
- `nvs_flash`: Non-volatile storage
- `esp_http_client`: HTTP client
- `mqtt`: MQTT client
- `driver`: Hardware drivers
- `bt`: Bluetooth (for BLE provisioning)

**External Libraries**:
- `cJSON`: JSON parsing
- `opus`: Audio codec (managed component)

---

## Appendix A: Quick Reference

### A.1 Key File Locations

| Component | File Location |
|-----------|--------------|
| Main Entry | `main/main.cc` |
| Application Controller | `main/application.cc` |
| MQTT Protocol | `main/protocols/mqtt_protocol.cc` |
| WebSocket Protocol | `main/protocols/websocket_protocol.cc` |
| MCP Server | `main/mcp_server.cc` |
| OTA Updates | `main/ota.cc` |
| Settings | `main/settings.cc` |
| Board Base | `main/boards/common/board.h` |
| Audio Processing | `main/audio_processing/` |
| Display | `main/display/` |

### A.2 Configuration Namespaces

| Namespace | Purpose | Key Fields |
|-----------|---------|------------|
| `mqtt` | MQTT config | `endpoint`, `client_id`, `publish_topic`, `subscribe_topic` |
| `websocket` | WebSocket config | `url`, `version`, `token` |
| `wifi` | WiFi credentials | SSID/password pairs |
| `network` | Network type | `type` (0=WiFi, 1=ML307) |
| `animation` | Animation updater | `server_url`, `enabled`, `check_interval` |

### A.3 Default Endpoints

| Service | Default URL |
|---------|-------------|
| WebSocket | `ws://136.117.60.16:8000/xiaozhi/v1/` |
| OTA Server | `http://136.117.60.16:8003/xiaozhi/ota/` |
| Animation Server | GitHub raw URL (see `animation_updater.cc`) |

### A.4 Log Tags

| Tag | Component |
|-----|-----------|
| `Application` | Main application |
| `MQTT` | MQTT protocol |
| `WS` | WebSocket protocol |
| `Ota` | OTA updates |
| `Audio` | Audio processing |
| `Board` | Board initialization |
| `WiFi` | WiFi connection |
| `MCP` | MCP protocol |

---

## Appendix B: Troubleshooting

### B.1 Common Issues

**Issue**: Device doesn't connect to WiFi
- **Solution**: Check BLE provisioning, verify credentials, check network availability

**Issue**: MQTT connection fails
- **Solution**: Verify endpoint configuration, check broker accessibility, verify credentials

**Issue**: WebSocket connection fails
- **Solution**: Check URL validity, verify server accessibility, check default URL fallback

**Issue**: Audio not working
- **Solution**: Check codec initialization, verify sample rates, check audio queue

**Issue**: OTA update fails
- **Solution**: Verify OTA URL, check network connectivity, verify firmware URL accessibility

### B.2 Debug Commands

```bash
# Build and flash
idf.py build flash

# Monitor serial output
idf.py monitor

# Clean build
idf.py fullclean

# Menuconfig
idf.py menuconfig

# GDB debugging
idf.py openocd  # Terminal 1
idf.py gdb      # Terminal 2
```

---

## Appendix C: Further Reading

### C.1 Documentation Files

- `docs/MQTT_WEBSOCKET_ARCHITECTURE.md`: Protocol architecture details
- `docs/mcp-protocol.md`: MCP protocol specification
- `docs/mcp-usage.md`: MCP usage guide
- `docs/websocket.md`: WebSocket implementation details
- `docs/sd-card-functionality.md`: SD card support
- `MQTT_DEBUGGING_GUIDE.md`: MQTT debugging guide
- `AUDIO_FLOW_VERIFICATION.md`: Audio flow verification

### C.2 External Resources

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [Model Context Protocol](https://modelcontextprotocol.io/)
- [Opus Codec](https://opus-codec.org/)

---

**Document End**

For questions or contributions, please refer to the project repository or contact the development team.

