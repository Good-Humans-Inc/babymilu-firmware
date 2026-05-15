# EchoEar MCP Protocol

This document describes the MCP behavior implemented in the current EchoEar
firmware. Treat the code as canonical; this page is an agent-friendly map of the
wire shape and expected flow.

## Active implementation

- Server implementation: `main/mcp_server.cc`
- Transport wrapper: `main/protocols/protocol.cc`
- WebSocket transport: `main/protocols/websocket_protocol.cc`
- MQTT transport: `main/protocols/mqtt_protocol.cc`
- Feature gate: `CONFIG_IOT_PROTOCOL_MCP`

MCP payloads use JSON-RPC 2.0 and are wrapped inside the active device protocol:

```json
{
  "session_id": "server-session-id",
  "type": "mcp",
  "payload": {
    "jsonrpc": "2.0",
    "method": "tools/list",
    "params": { "cursor": "" },
    "id": 2
  }
}
```

Device replies use the same wrapper with a JSON-RPC `result` or `error`.

## Connection flow

1. EchoEar opens the configured protocol connection. WebSocket is preferred for
   audio when available; MQTT remains useful for control and fallback.
2. EchoEar sends `hello` with `features.mcp: true` when MCP is enabled.
3. The server may send MCP requests as `type: "mcp"` messages.
4. EchoEar parses `payload` and handles `initialize`, `tools/list`, and
   `tools/call`.
5. EchoEar sends MCP responses through `Application::SendMcpMessage()`.

Notifications whose method starts with `notifications` are accepted and ignored.
Other methods return a JSON-RPC error.

## Supported methods

### initialize

Request:

```json
{
  "jsonrpc": "2.0",
  "method": "initialize",
  "params": {
    "capabilities": {
      "vision": {
        "url": "https://example.com/vision",
        "token": "optional-token"
      }
    }
  },
  "id": 1
}
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "protocolVersion": "2024-11-05",
    "capabilities": { "tools": {} },
    "serverInfo": {
      "name": "EchoEar",
      "version": "firmware-version"
    }
  }
}
```

If `params.capabilities.vision.url` is supplied and the current board exposes a
camera, the URL/token are passed to the camera explanation client. EchoEar itself
does not add a camera tool unless the board has a camera.

### tools/list

Request:

```json
{
  "jsonrpc": "2.0",
  "method": "tools/list",
  "params": { "cursor": "" },
  "id": 2
}
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "tools": [
      {
        "name": "self.get_device_status",
        "description": "...",
        "inputSchema": {
          "type": "object",
          "properties": {},
          "required": []
        }
      }
    ],
    "nextCursor": "optional-tool-name"
  }
}
```

`tools/list` is paginated by payload size. If `nextCursor` is present, send it as
the next request's `params.cursor`.

### tools/call

Request:

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.audio_speaker.set_volume",
    "arguments": { "volume": 50 },
    "stackSize": 6144
  },
  "id": 3
}
```

`stackSize` is optional. EchoEar runs tool callbacks in a detached thread so a
tool call does not block the main protocol handler.

Success response:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "content": [
      { "type": "text", "text": "true" }
    ],
    "isError": false
  }
}
```

Error response:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "error": {
    "message": "Unknown tool: self.example.missing"
  }
}
```

## Current common tools

The common tools are registered in `McpServer::AddCommonTools()`:

- `self.get_device_status`: returns board/device status JSON.
- `self.audio_speaker.set_volume`: sets speaker output volume, 0 to 100.
- `self.screen.set_brightness`: present only when the board exposes backlight.
- `self.screen.set_theme`: present only when the display exposes a theme.
- `self.camera.take_photo`: present only when the board exposes a camera.
- `self.animation_updater.get_status`
- `self.animation_updater.set_enabled`
- `self.animation_updater.set_server_url`
- `self.animation_updater.set_check_interval`
- `self.animation_updater.check_now`
- `self.animation_updater.start`
- `self.animation_updater.stop`

The WiFi clear MCP tool exists only as commented code. Use BLE/WiFi reset flows
documented elsewhere rather than relying on `self.wifi.clear_configuration`.

## Agent notes

- Every request must include numeric `id`; requests without a valid ID are
  logged and dropped.
- `params`, when present, must be a JSON object.
- Tool arguments are type-checked against each registered `Property`.
- Required arguments without defaults must be present and valid.
- The response `error` object currently contains only `message`, not a numeric
  JSON-RPC error code.
