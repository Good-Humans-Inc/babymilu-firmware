# BLE WiFi Setup Guide

EchoEar uses BLE provisioning through `WifiBoard`.

## Code

- `main/boards/common/wifi_board.cc`
- `main/boards/common/ble_server.c`
- `main/protocols/mqtt_protocol.cc`

## BLE Identity

- Advertised device name: `BabyMilu`.
- On BLE connect, firmware sends readiness text and `MAC:<device-mac>`.

## Credential Formats

Supported formats:

```text
ssid:<ssid>
pwd:<password>
```

or:

```text
wifi:<ssid>:<password>
```

After saving credentials, firmware restarts or restarts networking depending on
the path used.

## Remote Reconfiguration

MQTT `wifi_reconfig_nimble` enters BLE WiFi config mode without clearing
existing credentials. The firmware sets `force_ble_cfg` and `ble_cred_low`, then
reboots into a clean BLE setup path.

The next BLE credential is saved as lowest priority and marked for one-shot use
on the next boot.

## Clear And Reconfigure

MQTT `wifi_clear_credential` clears saved SSIDs, clears `wifi` settings, clears
`websocket` settings, then enters BLE config mode.

## Troubleshooting

- If BLE does not appear, confirm the device rebooted into config mode.
- If the new credential does not win immediately, check the one-shot
  `nxt_boot_ssid` behavior.
- If MQTT WebSocket URL reuse is surprising after a clear, confirm the
  `websocket` settings namespace was erased.
