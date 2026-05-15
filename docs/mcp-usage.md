# EchoEar MCP Usage

Use this page when an LLM/agent needs to discover and call EchoEar device tools.
For the wire format, see `docs/mcp-protocol.md`.

## Recommended agent flow

1. Wait for the device `hello` message and confirm `features.mcp` is `true`.
2. Send `initialize`.
3. Send `tools/list` with an empty cursor.
4. Continue `tools/list` with `nextCursor` until there is no next cursor.
5. Choose a tool by name and schema.
6. Send `tools/call` with only schema-valid arguments.
7. Read the returned text content. Many tools return JSON encoded as text.

## Practical examples

Initialize:

```json
{
  "jsonrpc": "2.0",
  "method": "initialize",
  "params": { "capabilities": {} },
  "id": 1
}
```

List tools:

```json
{
  "jsonrpc": "2.0",
  "method": "tools/list",
  "params": { "cursor": "" },
  "id": 2
}
```

Get status before changing audio:

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.get_device_status",
    "arguments": {}
  },
  "id": 3
}
```

Set speaker volume:

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.audio_speaker.set_volume",
    "arguments": { "volume": 45 }
  },
  "id": 4
}
```

Trigger an animation update check:

```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.animation_updater.check_now",
    "arguments": {}
  },
  "id": 5
}
```

## Current tool behavior

- `self.get_device_status` is the safest first call before device control.
- `self.audio_speaker.set_volume` accepts integer `volume` from 0 to 100.
- Animation updater tools control the update task and its server/check interval.
- Screen/camera tools are conditional. Do not assume they exist; rely on
  `tools/list`.
- There is no active MCP tool for clearing WiFi credentials in the current code.

## Things agents should avoid

- Do not use old ESP-Hi, chassis, dog, or RGB light tool examples. Those are not
  EchoEar's current common tools.
- Do not call tools that were not returned by `tools/list`.
- Do not send string numbers for integer arguments.
- Do not omit required arguments.
- Do not depend on JSON-RPC notification replies; notifications are ignored.
