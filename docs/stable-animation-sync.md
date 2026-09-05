# Stable Animation Sync

Each device has one current animation bundle and SHA sidecar:

- `gs://milu-public-new/device_bin/<canonical-device-id>/test.bin`
- `gs://milu-public-new/device_bin/<canonical-device-id>/test.bin.sha256`

After generation, the backend publishes a QoS 1 retained
`remote_anim_update` message on the device's MQTT down topic. Retention is
required because provisioning reboots the device before it reconnects and
subscribes. The retained message contains only the stable asset URL and SHA; it
does not contain credentials or user data.

On every MQTT connection firmware subscribes first, reports the valid locally
installed SHA as `animation_sync_status` with status `applied`, and reconciles
the stable object. A matching SHA is a no-op. A different SHA is downloaded to
the 8.3-compatible `test.tmp`, structure- and SHA-validated, atomically
installed using `test.bak`, and
activated by one reboot.

Concurrent reconnect and retained-message triggers are coalesced into a pending
rerun. A dedicated task—not the FreeRTOS timer-service task—waits between
attempts. Transient failures retry after 2, 5, 10, 30, and 60 seconds, then stop
until a new MQTT connection or update command resets the retry budget. This
prevents both a missed update and an unbounded retry/reboot loop.
