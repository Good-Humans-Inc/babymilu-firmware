# Wi-Fi Priority Synchronization

Wi-Fi passwords remain device-local. BLE BM2/BM1 writes credentials and NVS is
the only password store.

After MQTT connects, firmware subscribes to both its normal `/down` command
topic and the retained `/down/wifi` desired-state topic. A
`wifi_priority_update` message contains only a command ID, revision, ranked
SSIDs, and deletion tombstones.

For a newer revision firmware:

1. selects credentials it already has in the requested order;
2. removes explicitly deleted SSIDs;
3. appends locally known SSIDs omitted by the cloud;
4. rebuilds and persists `SsidManager` in that order;
5. saves the applied revision in the `wifi` NVS namespace; and
6. publishes `wifi_priority_applied` with the resulting SSID order.

Stale revisions cannot roll back NVS. Re-delivery of the current revision is a
safe no-op and is acknowledged again. A newly BLE-provisioned credential is
appended at lowest priority. If ten credentials already exist, provisioning is
rejected with `WIFI_NETWORK_LIMIT`; no existing network is silently evicted.
