# MQTT and WebSocket Connection Architecture

**Version:** 1.0  
**Last Updated:** November 2025  
**Branch:** mqtt-alarm-ws-1.85

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [User Journey](#2-user-journey)
3. [Hardcoded Fallbacks](#3-hardcoded-fallbacks)
4. [Flashing Procedure](#4-flashing-procedure)

---

## 1. Architecture Overview

### 1.1 Dual Protocol Design

The firmware implements a **dual-protocol architecture** where both MQTT and WebSocket can coexist simultaneously, each serving different purposes:

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────┐         ┌──────────────┐                  │
│  │   MQTT       │         │  WebSocket    │                  │
│  │ (Primary)    │         │ (On-Demand)  │                  │
│  │              │         │              │                  │
│  │ Always On    │         │ Created When │                  │
│  │              │         │   Needed     │                  │
│  │              │         │              │                  │
│  │ Control Msgs │         │ Audio Stream │                  │
│  │ ws_start     │         │ Conversations│                  │
│  │ Wake Word    │         │              │                  │
│  └──────────────┘         └──────────────┘                  │
│         │                          │                         │
│         └──────────┬───────────────┘                         │
│                    │                                         │
│            GetActiveProtocol()                               │
│         (WebSocket Priority)                                 │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 Protocol Roles

#### MQTT Protocol (Primary)
- **Status:** Always connected (if configured)
- **Purpose:** Control and signaling
- **Responsibilities:**
  - Receives `ws_start` messages from server (server-initiated conversations)
  - Handles wake word detection messages
  - Can handle audio via MQTT+UDP (legacy support, if server supports it)
  - Stays connected even when WebSocket is active

#### WebSocket Protocol (On-Demand)
- **Status:** Created when needed for audio conversations
- **Purpose:** Audio streaming
- **Responsibilities:**
  - User-initiated conversations (button press)
  - Server-initiated conversations (via `ws_start` from MQTT)
  - Takes priority for audio when open
  - Uses default URL if none configured

### 1.3 Audio Routing Logic

The `GetActiveProtocol()` function determines which protocol handles audio:

```cpp
Protocol* Application::GetActiveProtocol() {
    // WebSocket takes priority if opened
    if (websocket_protocol_ && websocket_protocol_->IsAudioChannelOpened()) {
        return websocket_protocol_.get();
    }
    // Fallback to primary protocol (MQTT)
    return protocol_.get();
}
```

**Priority Order:**
1. **WebSocket** (if opened) → Used for all audio conversations
2. **MQTT** (fallback) → Used if WebSocket unavailable

### 1.4 Key Design Principles

1. **Always Available:** WebSocket always has a fallback URL, ensuring manual interactions always work
2. **No Interference:** MQTT and WebSocket serve different purposes and coexist peacefully
3. **Graceful Degradation:** Falls back to MQTT if WebSocket fails
4. **Server Flexibility:** Server can request WebSocket via `ws_start` message

---

## 2. User Journey

### 2.1 Server-Initiated Conversation (Remote Wakeup)

**Trigger:** Server sends `ws_start` message via MQTT

```
┌─────────┐      ┌──────────┐      ┌──────────┐      ┌──────────┐
│ Server  │─────▶│  MQTT    │─────▶│  Device  │─────▶│ WebSocket│
│         │      │  Broker  │      │          │      │          │
└─────────┘      └──────────┘      └──────────┘      └──────────┘
   ws_start         Publish          Receive          Open
   message          Topic            ws_start         Connection
```

**Detailed Flow:**

1. **MQTT Connection Established**
   - Device connects to MQTT broker (configured or default)
   - Subscribes to subscribe topic (typically `xiaozhi/{mac}/down`)
   - MQTT connection stays alive for control messages

2. **Server Sends ws_start**
   ```json
   {
     "type": "ws_start",
     "wss": "wss://server.example.com/xiaozhi/v1/",
     "version": 3
   }
   ```

3. **Device Processes ws_start**
   - `MqttProtocol::OnMessage()` receives the message
   - Validates WebSocket URL (rejects localhost/invalid URLs)
   - Saves URL to settings: `Settings("websocket").SetString("url", url)`
   - Saves version if provided
   - Schedules `Application::OpenWebSocketConnection()`

4. **WebSocket Connection Opens**
   - Creates `WebsocketProtocol` instance if needed
   - Reads WebSocket URL from settings (or uses default if invalid/empty)
   - Adds query parameters: `?device-id={mac}&client-id={uuid}`
   - Connects to WebSocket server
   - Sets up audio callbacks

5. **Device Enters Listening State**
   - If device was idle → Automatically enters listening state
   - Sends `listen:start` message to server
   - Starts audio capture
   - Audio streams via WebSocket

6. **Audio Streaming**
   - All audio packets sent via WebSocket
   - MQTT remains connected for control messages
   - Both protocols active simultaneously

**Code Locations:**
- MQTT ws_start handler: `main/protocols/mqtt_protocol.cc:117-146`
- WebSocket opening: `main/application.cc:1581-1712`
- Remote wakeup: `main/application.cc:1686-1694`

### 2.2 User-Initiated Conversation (Button Press)

**Trigger:** User presses button or wake word detected

```
┌─────────┐      ┌──────────┐      ┌──────────┐
│  User  │─────▶│  Device  │─────▶│ WebSocket│
│ Button │      │          │      │          │
└─────────┘      └──────────┘      └──────────┘
   Press          StartListening   Open
                  ()               Connection
```

**Detailed Flow:**

1. **User Action**
   - User presses button → `Application::StartListening()`
   - OR wake word detected → `Application::WakeWordInvoke()`

2. **WebSocket Connection Attempt**
   - Checks if WebSocket already open
   - If not, calls `Application::OpenWebSocketConnection()`
   - **Always tries WebSocket first** (no MQTT fallback for manual interactions)

3. **WebSocket URL Resolution**
   - Reads URL from settings: `Settings("websocket").GetString("url")`
   - **If empty or invalid → Uses hardcoded default:**
     ```
     ws://136.117.60.16:8000/xiaozhi/v1/
     ```
   - Adds query parameters: `?device-id={mac}&client-id={uuid}`

4. **Connection Established**
   - WebSocket connects to server
   - Device enters listening state
   - Sends `listen:start` message
   - Starts audio capture

5. **Audio Streaming**
   - All audio packets sent via WebSocket
   - MQTT remains connected (for future ws_start messages)

**Code Locations:**
- Manual listening: `main/application.cc:531-587`
- Wake word: `main/application.cc:953-976`
- WebSocket opening: `main/application.cc:1581-1712`

### 2.3 MQTT Fallback (Legacy Support)

**Trigger:** WebSocket fails or server supports MQTT+UDP

```
┌─────────┐      ┌──────────┐      ┌──────────┐
│  User  │─────▶│  Device  │─────▶│   MQTT   │
│ Button │      │          │      │  + UDP   │
└─────────┘      └──────────┘      └──────────┘
   Press          WebSocket         Audio
                  Failed            Channel
```

**Detailed Flow:**

1. **WebSocket Attempt Fails**
   - User presses button
   - WebSocket connection fails
   - Falls back to primary protocol (MQTT)

2. **MQTT Audio Channel Opens**
   - Calls `MqttProtocol::OpenAudioChannel()`
   - Sends hello message to MQTT topic
   - Waits for server hello response
   - If server responds with `ws_start` → Opens WebSocket instead
   - If server responds with hello → Opens UDP channel for audio

3. **Audio via MQTT+UDP**
   - MQTT for control messages
   - UDP for encrypted audio packets
   - Legacy mode (most servers now use WebSocket)

**Code Locations:**
- MQTT audio channel: `main/protocols/mqtt_protocol.cc:258-349`
- Fallback logic: `main/application.cc:563-569`

### 2.4 Protocol State Diagram

```
                    ┌─────────────┐
                    │   Device    │
                    │    Idle     │
                    └──────┬──────┘
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
        ▼                  ▼                  ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│   Server     │  │    User    │  │   Wake      │
│  ws_start    │  │   Button   │  │   Word      │
└──────┬───────┘  └──────┬─────┘  └──────┬──────┘
       │                 │                │
       └─────────┬───────┴────────────────┘
                 │
                 ▼
        ┌─────────────────┐
        │ Open WebSocket  │
        │ (with fallback) │
        └────────┬────────┘
                 │
        ┌────────┴────────┐
        │                 │
        ▼                 ▼
┌──────────────┐  ┌──────────────┐
│  WebSocket   │  │    MQTT     │
│  Success     │  │  Fallback   │
└──────┬───────┘  └──────┬──────┘
       │                  │
       └────────┬─────────┘
                │
                ▼
        ┌──────────────┐
        │   Audio      │
        │  Streaming   │
        └──────────────┘
```

---

## 3. Hardcoded Fallbacks

### 3.1 WebSocket Default URL

**Location:** `main/protocols/websocket_protocol.cc:121`

```cpp
const std::string default_url = "ws://136.117.60.16:8000/xiaozhi/v1/";
```

**Usage:**
- Used when no WebSocket URL is configured in settings
- Used when configured URL is invalid (localhost, loopback, etc.)
- **Ensures manual interactions always work** (no configuration required)

**Validation:**
- Rejects: `127.0.0.1`, `localhost`, `::1`, `0.0.0.0`
- Requires: URL must start with `ws://` or `wss://`
- Invalid URLs are cleared from settings and default is used

### 3.2 MQTT Default Endpoint

**Location:** `main/application.cc:40-45`

```cpp
#ifndef DEFAULT_MQTT_ENDPOINT
#define DEFAULT_MQTT_ENDPOINT ""
#endif

#ifndef DEFAULT_MQTT_PUBLISH_TEMPLATE
#define DEFAULT_MQTT_PUBLISH_TEMPLATE "xiaozhi/%s/up"
#endif
```

**Usage:**
- Can be set at **build time** via CMake:
  ```bash
  idf.py -DDEFAULT_MQTT_ENDPOINT="192.168.1.10:1883" \
         -DDEFAULT_MQTT_PUBLISH_TEMPLATE="xiaozhi/%s/up"
  ```
- If not set at build time, defaults to empty string (no auto-config)
- If empty, device uses OTA config or manual configuration
- First boot seeding: `main/application.cc:693-703`

**Publish Topic Template:**
- Default: `xiaozhi/{mac_address}/up`
- Subscribe topic derived: `xiaozhi/{mac_address}/down`
- `{mac_address}` is replaced with device MAC address

### 3.3 OTA Server URL

**Location:** `main/ota.cc:45`

```cpp
return "http://136.117.60.16:8003/xiaozhi/ota/";
```

**Usage:**
- Used for OTA (Over-The-Air) firmware updates
- Device checks this URL for new firmware versions
- Also used to fetch server configuration (MQTT/WebSocket endpoints)

### 3.4 Animation Server URL

**Location:** `main/animation/animation_updater.cc:23`

```cpp
#define DEFAULT_SERVER_URL "https://github.com/Jackson-hangxuan/postman_test/raw/refs/heads/main"
```

**Usage:**
- Used for downloading animation files
- Fallback if no server URL configured

### 3.5 Summary of Hardcoded Values

| Component | Hardcoded Value | Location | Purpose |
|-----------|----------------|----------|---------|
| **WebSocket URL** | `ws://136.117.60.16:8000/xiaozhi/v1/` | `websocket_protocol.cc:121` | Default WebSocket server for audio |
| **MQTT Endpoint** | `""` (empty, can be set at build) | `application.cc:40` | MQTT broker address |
| **MQTT Topic Template** | `xiaozhi/%s/up` | `application.cc:44` | MQTT publish topic pattern |
| **OTA URL** | `http://136.117.60.16:8003/xiaozhi/ota/` | `ota.cc:45` | OTA update server |
| **Animation URL** | GitHub raw URL | `animation_updater.cc:23` | Animation file server |

### 3.6 URL Validation Rules

**WebSocket URL Validation** (`IsValidWebSocketUrl()`):
- ✅ Must start with `ws://` or `wss://`
- ❌ Cannot contain: `127.0.0.1`, `localhost`, `::1`, `0.0.0.0`
- ❌ Cannot be empty
- Invalid URLs are rejected and default URL is used

**MQTT Endpoint Validation:**
- No hardcoded validation
- Empty string means "not configured"
- Device will use OTA config or manual configuration

---

## 4. Flashing Procedure

### 4.1 Prerequisites

1. **ESP-IDF Environment**
   - ESP-IDF v5.5.1 installed
   - Environment variables set (`IDF_PATH`)

2. **Board Selection**
   - Select board in menuconfig: `idf.py menuconfig`
   - Navigate to: `Xiaozhi Assistant → Board Type`
   - Select: **"Waveshare ESP32-S3-Touch-LCD-1.85"**

3. **Build Configuration**
   - Default language selection
   - OTA URL configuration (optional)

### 4.2 Build-Time Configuration (Optional)

#### 4.2.1 MQTT Endpoint Configuration

Set MQTT broker at build time:

```bash
idf.py build -DDEFAULT_MQTT_ENDPOINT="your-broker.com:1883" \
             -DDEFAULT_MQTT_PUBLISH_TEMPLATE="xiaozhi/%s/up"
```

**Parameters:**
- `DEFAULT_MQTT_ENDPOINT`: MQTT broker address (host:port)
- `DEFAULT_MQTT_PUBLISH_TEMPLATE`: Topic template (default: `xiaozhi/%s/up`)

**Example:**
```bash
idf.py build -DDEFAULT_MQTT_ENDPOINT="mqtt.example.com:1883" \
             -DDEFAULT_MQTT_PUBLISH_TEMPLATE="devices/%s/up"
```

**Note:** If not set, device will use OTA config or require manual configuration.

#### 4.2.2 Build Process

```bash
# 1. Configure project
idf.py menuconfig

# 2. Build with optional MQTT config
idf.py build -DDEFAULT_MQTT_ENDPOINT="broker:1883"

# 3. Flash to device
idf.py flash

# 4. Monitor serial output
idf.py monitor
```

### 4.3 Post-Flash Configuration

#### 4.3.1 WiFi Configuration

1. **First Boot:**
   - Device starts in WiFi configuration mode
   - BLE device "BabyMilu" appears
   - Connect via BLE and configure WiFi credentials

2. **WiFi Connection:**
   - Device connects to configured WiFi
   - If connection fails, returns to BLE config mode

#### 4.3.2 MQTT Configuration (If Not Set at Build)

**Via OTA Config:**
- Device checks OTA server: `http://136.117.60.16:8003/xiaozhi/ota/`
- OTA response includes MQTT configuration
- Device auto-configures from OTA response

**Via Settings (Runtime):**
- Can be configured via BLE or MCP tools
- Settings namespace: `"mqtt"`
- Required fields:
  - `endpoint`: MQTT broker address
  - `client_id`: Client identifier (default: MAC address)
  - `publish_topic`: Topic for publishing (default: `xiaozhi/{mac}/up`)
  - `subscribe_topic`: Topic for subscribing (default: `xiaozhi/{mac}/down`)

#### 4.3.3 WebSocket Configuration

**Automatic (Recommended):**
- Server sends `ws_start` message via MQTT
- Device automatically saves WebSocket URL
- No manual configuration needed

**Manual (If Needed):**
- Settings namespace: `"websocket"`
- Field: `url` - WebSocket server URL
- **Note:** If not configured, device uses hardcoded default

### 4.4 Verification Steps

#### 4.4.1 MQTT Connection Verification

1. **Check Serial Logs:**
   ```
   I (xxx) MQTT: Connected to endpoint
   I (xxx) MQTT: Successfully subscribed to topic: xiaozhi/xx:xx:xx:xx:xx:xx/down
   ```

2. **Test ws_start Message:**
   - Publish to subscribe topic:
     ```json
     {
       "type": "ws_start",
       "wss": "wss://your-server.com/xiaozhi/v1/",
       "version": 3
     }
     ```
   - Device should log:
     ```
     I (xxx) MQTT: Server requests WebSocket connection: wss://...
     I (xxx) MQTT: WebSocket URL saved. Opening WebSocket connection...
     I (xxx) Application: WebSocket connection opened successfully
     ```

#### 4.4.2 WebSocket Connection Verification

1. **Manual Interaction:**
   - Press button on device
   - Check logs:
     ```
     I (xxx) Application: Opening WebSocket connection for manual interaction
     I (xxx) WS: Using WebSocket URL from settings: wss://...
     I (xxx) WS: Connecting to websocket server: wss://... with version: 3
     I (xxx) Application: WebSocket connection opened successfully
     ```

2. **Default URL Fallback:**
   - If no URL configured, should see:
     ```
     W (xxx) WS: No WebSocket URL in settings, using default: ws://136.117.60.16:8000/xiaozhi/v1/
     ```

### 4.5 Troubleshooting

#### 4.5.1 MQTT Not Connecting

**Symptoms:**
- Logs show: `MQTT endpoint is not specified`
- Device doesn't connect to broker

**Solutions:**
1. Check OTA config response
2. Set `DEFAULT_MQTT_ENDPOINT` at build time
3. Configure via settings (BLE/MCP)

#### 4.5.2 WebSocket Not Opening

**Symptoms:**
- Manual interactions fail
- No WebSocket connection logs

**Solutions:**
1. Check if default URL is accessible: `ws://136.117.60.16:8000/xiaozhi/v1/`
2. Verify server accepts WebSocket connections
3. Check firewall/network settings

#### 4.5.3 Both Protocols Active

**Expected Behavior:**
- MQTT connected (control messages)
- WebSocket opened (audio streaming)
- Both should be active simultaneously
- This is **normal and expected**

### 4.6 Configuration Priority

When multiple configuration sources exist, priority order is:

1. **Runtime Settings** (highest priority)
   - Settings stored in NVS
   - Configured via BLE/MCP/OTA

2. **OTA Config**
   - Fetched from OTA server on startup
   - Overrides build-time defaults

3. **Build-Time Defaults**
   - `DEFAULT_MQTT_ENDPOINT` (if set)
   - `DEFAULT_MQTT_PUBLISH_TEMPLATE` (if set)

4. **Hardcoded Fallbacks** (lowest priority)
   - WebSocket: `ws://136.117.60.16:8000/xiaozhi/v1/`
   - OTA: `http://136.117.60.16:8003/xiaozhi/ota/`

---

## 5. Code Reference

### 5.1 Key Files

| File | Purpose |
|------|---------|
| `main/application.cc` | Protocol initialization, audio routing, connection management |
| `main/protocols/mqtt_protocol.cc` | MQTT protocol implementation, ws_start handling |
| `main/protocols/websocket_protocol.cc` | WebSocket protocol implementation, default URL |
| `main/boards/common/wifi_board.cc` | Network initialization, protocol creation |
| `main/ota.cc` | OTA configuration fetching |

### 5.2 Key Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `GetActiveProtocol()` | `application.cc:1567` | Returns protocol for audio (WebSocket priority) |
| `OpenWebSocketConnection()` | `application.cc:1581` | Opens WebSocket connection |
| `StartListening()` | `application.cc:531` | User-initiated conversation start |
| `MqttProtocol::OnMessage()` | `mqtt_protocol.cc:99` | Handles MQTT messages (ws_start) |
| `WebsocketProtocol::OpenAudioChannel()` | `websocket_protocol.cc:112` | Opens WebSocket with default URL fallback |

### 5.3 Settings Namespaces

| Namespace | Fields | Purpose |
|-----------|--------|---------|
| `"mqtt"` | `endpoint`, `client_id`, `publish_topic`, `subscribe_topic`, `username`, `password`, `keepalive` | MQTT broker configuration |
| `"websocket"` | `url`, `version`, `token` | WebSocket server configuration |
| `"wifi"` | `ssid`, `password` | WiFi credentials |

---

## 6. Protocol Message Formats

### 6.1 MQTT ws_start Message

**Topic:** `{subscribe_topic}` (e.g., `xiaozhi/{mac}/down`)

**Message Format:**
```json
{
  "type": "ws_start",
  "wss": "wss://server.example.com/xiaozhi/v1/",
  "version": 3
}
```

**Fields:**
- `type`: Must be `"ws_start"`
- `wss`: WebSocket URL (required, validated)
- `version`: WebSocket protocol version (optional, default: 2)

**Device Response:**
- Validates URL
- Saves to settings
- Opens WebSocket connection
- Enters listening state (if idle)

### 6.2 WebSocket Connection URL

**Base URL:** From settings or default: `ws://136.117.60.16:8000/xiaozhi/v1/`

**With Query Parameters:**
```
ws://136.117.60.16:8000/xiaozhi/v1/?device-id={mac}&client-id={uuid}
```

**Parameters:**
- `device-id`: Device MAC address
- `client-id`: Device UUID

**Note:** If `ws_start` message includes query parameters, they are used as-is (no duplicates added).

---

## 7. Best Practices

### 7.1 For Cloud Engineers

1. **Always send ws_start for server-initiated conversations**
   - Don't rely on device having WebSocket URL pre-configured
   - Include valid WebSocket URL in `ws_start` message
   - Validate URLs before sending (no localhost/loopback)

2. **Support both protocols**
   - MQTT for control/signaling
   - WebSocket for audio streaming
   - Device will automatically route audio to WebSocket when available

3. **Handle URL validation**
   - Device rejects invalid URLs (localhost, etc.)
   - Invalid URLs trigger default URL fallback
   - Always send valid, accessible URLs

### 7.2 For Firmware Engineers

1. **Default URL Maintenance**
   - Default WebSocket URL is hardcoded
   - Update if server changes: `websocket_protocol.cc:121`
   - Test default URL accessibility regularly

2. **Protocol Priority**
   - WebSocket always takes priority for audio
   - MQTT remains connected for control
   - Both can be active simultaneously (this is expected)

3. **Error Handling**
   - WebSocket failures fall back to MQTT gracefully
   - Invalid URLs are rejected and default is used
   - No user-facing errors for protocol fallbacks

---

## 8. Changelog

### Version 1.0 (November 2025)
- Initial dual-protocol architecture
- WebSocket default URL fallback
- MQTT ws_start message handling
- Manual interactions always use WebSocket
- Server-initiated conversations via ws_start
- Comprehensive documentation

---

## 9. Support

For questions or issues:
- Check serial logs for protocol connection status
- Verify hardcoded URLs are accessible
- Ensure MQTT broker is reachable
- Test WebSocket server connectivity

**Key Log Tags:**
- `MQTT`: MQTT protocol messages
- `WS`: WebSocket protocol messages
- `Application`: Application-level protocol routing

---

**Document End**

