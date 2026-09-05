# BM2 Wi-Fi provisioning

Firmware 2.0.15 negotiates BM2 while preserving BM1 and legacy compatibility.
The capability payload keeps the original `protocols: ["BM1"]` fields unchanged
for strict released clients and advertises BM2 through additive
`supportedProtocols`, `preferredProtocol`, and `resultChannels` fields.

BM1 and BM2 both persist credentials and reboot after the BLE
`CREDENTIALS_STAGED` acknowledgement. Neither protocol starts the Wi-Fi station
inside the memory-constrained BLE configuration process. BM2 confirms success
through the authenticated cloud result. BM1 remains a compatibility fallback:
successful runtime startup clears its pending state, while a failed boot saves
a correlated BLE-readable failure before returning to configuration mode.

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

EchoEar does not instantiate an ESP-SR AFE. Live hardware measurements showed
that even the communication AFE alone left only about 13 KB of internal RAM and
prevented the Wi-Fi driver from starting. EchoEar therefore uses the lightweight
raw-microphone processor, which extracts the microphone channel and sends mono
60 ms Opus frames. Local wake word, on-device AEC/VAD, and the WakeNet model are
disabled; conversation activation uses button/touch/app paths. This preserves
Wi-Fi and voice uplink within the board's internal-RAM budget.
EchoEar may mount the SD card concurrently, but its animation task waits until
both the audio runtime and Wi-Fi initialization are complete before decoding or
loading assets. This keeps transient allocations from fragmenting the internal
heap needed by the speech runtime and Wi-Fi driver. A first-boot BLE
provisioning process remains audio-free. Allocation and task-creation failures
disable the affected audio feature instead of leaving a partially initialized
object that can crash later.

EchoEar release images must be produced with `python3 scripts/release.py
echoear`; this regenerates `sdkconfig` from the board configuration so a stale
development configuration cannot silently re-enable ESP-SR. Wi-Fi driver
initialization is also nonfatal: if any initialization allocation fails, the
station is cleaned up and the device returns to BLE configuration instead of
aborting into a reboot loop.

EchoEar no longer reads a device document directly from the public Firestore
REST API during startup. That unauthenticated request targeted the legacy
`(default)` database, duplicated the device's persisted credential ordering and
MQTT Wi-Fi switching behavior, delayed startup, and increased heap pressure.
Cloud-owned device metadata stays behind authenticated backend APIs.

The reporting URL is configured by `CONFIG_PROVISIONING_RESULT_URL`. Its
default points at the production-facing `device-api-miffy-dev` route. The app
owns attempt creation and polling; firmware possesses only the short-lived
device report credential received over BLE.

Host parser tests run with:

```sh
./scripts/test_wifi_provisioning_protocol.sh
python3 tests/echoear_startup_contract_test.py
```

An EchoEar build uses ESP-IDF 5.5 and must finish before flashing. Physical QA
must cover iOS and Android, valid credentials, wrong credentials, unreachable
runtime, both BLE write modes, cancellation, and a check that no credential or
report token appears in serial/app logs.
