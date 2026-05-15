# IoT And MCP Notes

This directory contains the legacy Thing abstraction and sample Things, but the
active server-facing tool surface is MCP.

## Current Active Path

`Application` calls `McpServer::GetInstance().AddCommonTools()` during startup
when `CONFIG_IOT_PROTOCOL_MCP` is enabled. Tool calls are parsed by
`main/mcp_server.cc` and routed over the active protocol:

- WebSocket while a WebSocket audio channel is open.
- MQTT otherwise.

## Common MCP Tools

The active common tools include:

- `self.get_device_status`
- `self.audio_speaker.set_volume`
- screen brightness/theme tools when a display/backlight supports them
- camera tools when a board provides a camera
- animation update tools where compiled in

EchoEar currently uses WiFi networking and an LCD/backlight path, so tool
availability depends on the runtime capabilities reported by `Board`.

## Legacy Thing Model

The files under `main/iot/things` still show the older Thing declaration style.
They are useful as examples, but they are not the primary place to add active
MCP tools for this repo.

Do not copy old examples that reference deleted board files such as
`compact_wifi_board.cc` or `main/boards/your_board`.
