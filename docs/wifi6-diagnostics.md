# Wi-Fi 6 Office Diagnostics

ESP32-S3 is a 2.4 GHz 802.11 b/g/n client. A Wi-Fi 6 access point can still work
when it also allows legacy 2.4 GHz clients, so the useful question is whether
failures cluster around AP security, BSSID, RSSI, roaming, or 802.11ax-advertising
BSSIDs.

## Capture

Flash the diagnostic firmware, then monitor and filter the important lines:

```bash
idf.py flash monitor
```

Look for lines tagged `[WIFI_DIAG]`.

## Key Lines

- `station_started`: local ESP-IDF version and station protocol mask. On ESP32-S3
  this should read like `local_protocols=11b/11g/11n`, not `11ax`.
- `scan=... summary`: total APs, saved credentials, matching candidates, and
  `ax_candidates`.
- `scan=... candidate`: each matching BSSID for a saved SSID, including channel,
  RSSI, auth mode, ciphers, PHY flags, and `ax=0/1`.
- `connect_attempt`: the exact BSSID selected for that attempt.
- `disconnect`: reason code/name, BSSID, RSSI, and the selected AP metadata.
- `got_ip`: successful DHCP plus the actual associated AP metadata.

## Reading Results

- `got_ip ... ax=1`: Wi-Fi 6 advertisement is not fatal by itself. Investigate
  signal, roaming, DHCP, DNS, or backend reachability next.
- `NO_AP_FOUND_W_COMPATIBLE_SECURITY`, `NO_AP_FOUND_IN_AUTHMODE_THRESHOLD`,
  `AUTH_FAIL`, or `HANDSHAKE_TIMEOUT`: compare auth and ciphers. For demo, ask
  the office to provide a 2.4 GHz WPA2-Personal SSID using AES/CCMP with PMF
  optional.
- `ASSOC_FAIL`, `ASSOC_EXPIRE`, or `BEACON_TIMEOUT` with good RSSI: test with
  802.11r fast roaming, band steering, Wi-Fi 6-only/ax-only mode, and 40 MHz
  channel width disabled on the demo SSID.
- `ax_candidates=0`: the observed failure is not tied to Wi-Fi 6-advertising
  BSSIDs in that scan.

## Fast A/B Test

Collect at least five cold boots in the investor office and five against a known
good phone hotspot or travel router. Compare:

- success rate: count `got_ip`
- failure reasons: count `disconnect reason_name=...`
- AP type: count attempts with `ax=1` vs `ax=0`
- signal: compare `rssi`
- security: compare `auth`, `pairwise`, and `group`

For a stable demo fallback, use a dedicated 2.4 GHz SSID with WPA2-Personal
AES/CCMP, PMF optional, 20 MHz channel width, channels 1/6/11, and legacy
802.11 b/g/n enabled.
