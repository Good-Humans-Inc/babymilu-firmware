# WiFi Disconnection Recovery

Current recovery behavior is handled by `WifiBoard` and BLE provisioning.

## Full Reset

```powershell
idf.py -p COMXX erase-flash
idf.py build flash monitor
```

This clears NVS and forces first-boot WiFi setup.

## Remote Reconfiguration

MQTT can request BLE setup:

```json
{"type":"wifi_reconfig_nimble"}
```

This preserves existing credentials, reboots, advertises as `BabyMilu`, and saves
the next BLE credential as lowest priority with one-shot next-boot preference.

## Remote Clear

```json
{"type":"wifi_clear_credential"}
```

This clears saved SSIDs, clears `wifi` settings, clears `websocket` settings, and
reboots into BLE config mode.

## BLE Formats

```text
ssid:<ssid>
pwd:<password>
```

or:

```text
wifi:<ssid>:<password>
```
