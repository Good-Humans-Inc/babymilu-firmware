# BM2 Wi-Fi provisioning

Firmware 2.0.12 negotiates BM2 while preserving BM1 and legacy compatibility.
The capability payload keeps the original `protocols: ["BM1"]` fields unchanged
for strict released clients and advertises BM2 through additive
`supportedProtocols`, `preferredProtocol`, and `resultChannels` fields.

BM2 reassembles at most four 160-byte raw BLE frames into a 384-byte logical
request. It validates a correlated UUID-v4 attempt, SSID/password byte limits,
and the app-issued report token before saving credentials. Passwords and report
tokens are never logged.

After accepting a request, the device persists the attempt and token in NVS,
reboots, and tries the selected network. Association and DHCP failures are
persisted and exposed over BLE after the device returns to configuration mode.
If OTA/runtime hydration fails, the device similarly reboots into BLE mode with
`internet_failed`. Only successful runtime hydration triggers the authenticated
cloud `connected` report.

The reporting URL is configured by `CONFIG_PROVISIONING_RESULT_URL`. Its
default points at the production-facing `device-api-miffy-dev` route. The app
owns attempt creation and polling; firmware possesses only the short-lived
device report credential received over BLE.

Host parser tests run with:

```sh
./scripts/test_wifi_provisioning_protocol.sh
```

An EchoEar build uses ESP-IDF 5.5 and must finish before flashing. Physical QA
must cover iOS and Android, valid credentials, wrong credentials, unreachable
runtime, both BLE write modes, cancellation, and a check that no credential or
report token appears in serial/app logs.
