# Detect MQTT Broker ACL Denial

This is a troubleshooting note for the current MQTT diagnostic logs in
`main/protocols/mqtt_protocol.cc`. It helps distinguish broker ACL problems from
connection timing or authentication failures.

## Confirmed subscribe ACL pattern

Look for this sequence:

```text
I (...) MQTT: MQTT client fully connected (CONNACK received)
I (...) MQTT: Subscribing to topic (from OnConnected): xiaozhi/<client-id>/down
E (...) MQTT: Failed to subscribe to topic (from OnConnected): xiaozhi/<client-id>/down
E (...) MQTT: Connection status after failed subscribe: connected
I (...) MQTT: Publish succeeded - connection is working, likely ACL denies SUBSCRIBE
E (...) MQTT: DIAGNOSIS: Broker ACL likely denies SUBSCRIBE for client_id=<client-id> to topic=xiaozhi/<client-id>/down
```

Meaning:

- MQTT CONNECT succeeded and CONNACK was received.
- The client remains connected after the failed subscribe.
- A diagnostic publish succeeds.
- Subscribe is denied by broker ACL or topic policy.

## Timing issue pattern

This is not an ACL denial:

```text
I (...) MQTT: MQTT connection status after Connect(): connected
W (...) MQTT: Initial subscribe failed (may retry in OnConnected): xiaozhi/<client-id>/down
I (...) MQTT: MQTT client fully connected (CONNACK received)
I (...) MQTT: Successfully subscribed to topic (from OnConnected): xiaozhi/<client-id>/down
```

The initial subscribe happened before the client was fully ready. The retry in
`OnConnected` succeeded.

## Connection or auth problem pattern

This is usually not a subscribe ACL denial:

```text
I (...) MQTT: Connecting to endpoint <host>:<port>
E (...) MQTT: Failed to connect to endpoint <host>:<port>
```

Check endpoint, network reachability, credentials, broker listener, TLS settings,
and broker authentication logs.

## Common causes

- The broker allows CONNECT but denies SUBSCRIBE for the device client ID.
- The ACL allows publish topics but not `xiaozhi/<client-id>/down`.
- The server and device disagree on topic case or client ID formatting.
- Anonymous users can connect but cannot subscribe.
- Username/password is missing or maps to a restricted broker role.

## Next checks

1. Verify the device's logged `client_id`, `publish_topic`, and
   `subscribe_topic`.
2. Check broker logs for "not authorized", "ACL denied", or equivalent messages
   at the same timestamp.
3. Test with the same credentials and client ID from a command-line MQTT client.
4. Confirm the ACL grants subscribe permission to the exact downlink topic.

Example:

```bash
mosquitto_sub -h <host> -p <port> -i '<client-id>' -t 'xiaozhi/<client-id>/down' -v
```

Use the same username/password as the device if the broker requires them.
