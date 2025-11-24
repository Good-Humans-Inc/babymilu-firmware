# How to Detect Broker ACL Denial from Device Logs

## Overview
When a broker's Access Control List (ACL) denies SUBSCRIBE permission, the device logs will show specific patterns that distinguish ACL issues from other problems (like timing issues or connection failures).

## Key Log Patterns to Look For

### Pattern 1: Connection Succeeds, Subscribe Fails (ACL Denial)

**What you'll see:**
```
I (...) MQTT: Connected to endpoint 35.188.112.96:1883
I (...) MQTT: MQTT connection status after Connect(): connected
I (...) MQTT: MQTT client fully connected (CONNACK received)
I (...) MQTT: Subscribing to topic (from OnConnected): xiaozhi/cc:ba:97:11:0e:84/down
E (...) MQTT: Failed to subscribe to topic (from OnConnected): xiaozhi/cc:ba:97:11:0e:84/down
E (...) MQTT: Connection status after failed subscribe: connected
I (...) MQTT: Testing publish capability to diagnose ACL issue...
I (...) MQTT: Publish succeeded - connection is working, likely ACL denies SUBSCRIBE
E (...) MQTT: DIAGNOSIS: Broker ACL likely denies SUBSCRIBE for client_id=cc:ba:97:11:0e:84 to topic=xiaozhi/cc:ba:97:11:0e:84/down
```

**What this means:**
- ✅ Connection is established (CONNACK received)
- ✅ Connection remains active after subscribe failure
- ✅ Publish works (connection is functional)
- ❌ Subscribe fails (ACL denies SUBSCRIBE permission)

**Conclusion:** This is **definitely an ACL issue** - the broker is rejecting the SUBSCRIBE request for this client_id/topic combination.

---

### Pattern 2: Connection Succeeds, Both Subscribe and Publish Fail

**What you'll see:**
```
I (...) MQTT: Connected to endpoint 35.188.112.96:1883
I (...) MQTT: MQTT client fully connected (CONNACK received)
E (...) MQTT: Failed to subscribe to topic (from OnConnected): xiaozhi/cc:ba:97:11:0e:84/down
I (...) MQTT: Testing publish capability to diagnose ACL issue...
E (...) MQTT: Publish also failed - connection may be broken or ACL denies both PUBLISH and SUBSCRIBE
```

**What this means:**
- ✅ Connection is established
- ❌ Subscribe fails
- ❌ Publish also fails

**Conclusion:** Either:
1. ACL denies both PUBLISH and SUBSCRIBE
2. Connection is broken (unlikely if CONNACK was received)
3. Username/auth required but not provided

---

### Pattern 3: Subscribe Fails Before CONNACK (Timing Issue)

**What you'll see:**
```
I (...) MQTT: Connected to endpoint 35.188.112.96:1883
I (...) MQTT: MQTT connection status after Connect(): connected
W (...) MQTT: Initial subscribe failed (may retry in OnConnected): xiaozhi/cc:ba:97:11:0e:84/down
I (...) MQTT: MQTT client fully connected (CONNACK received)
I (...) MQTT: Subscribing to topic (from OnConnected): xiaozhi/cc:ba:97:11:0e:84/down
I (...) MQTT: Successfully subscribed to topic (from OnConnected): xiaozhi/cc:ba:97:11:0e:84/down
```

**What this means:**
- Initial subscribe fails (timing issue)
- Subscribe succeeds after CONNACK (in OnConnected callback)

**Conclusion:** This is **NOT an ACL issue** - it's a timing problem that's now fixed.

---

### Pattern 4: Connection Fails Immediately

**What you'll see:**
```
I (...) MQTT: Connecting to endpoint 35.188.112.96:1883
E (...) MQTT: Failed to connect to endpoint 35.188.112.96:1883
```

**What this means:**
- Can't establish TCP connection or CONNECT packet rejected

**Conclusion:** This is **NOT an ACL issue** - it's a connection/auth problem.

---

## Diagnostic Flowchart

```
Start: Subscribe fails
│
├─ Connection established? (CONNACK received)
│  │
│  ├─ NO → Connection/auth problem (not ACL)
│  │
│  └─ YES → Connection still active after subscribe failure?
│     │
│     ├─ NO → Connection broken (not ACL)
│     │
│     └─ YES → Can publish work?
│        │
│        ├─ YES → ✅ ACL DENIES SUBSCRIBE (confirmed)
│        │
│        └─ NO → ACL denies both PUBLISH and SUBSCRIBE, or auth issue
```

## What the Enhanced Logs Tell You

### 1. Connection Status Checks
```
I (...) MQTT: Connection status before subscribe: connected
E (...) MQTT: Connection status after failed subscribe: connected
```
- If both show "connected": Connection is fine, likely ACL issue
- If changes to "disconnected": Connection broken, not ACL

### 2. Publish Test
```
I (...) MQTT: Testing publish capability to diagnose ACL issue...
I (...) MQTT: Publish succeeded - connection is working, likely ACL denies SUBSCRIBE
```
- If publish succeeds: Connection works, ACL denies SUBSCRIBE
- If publish fails: Either ACL denies both, or connection broken

### 3. Explicit Diagnosis
```
E (...) MQTT: DIAGNOSIS: Broker ACL likely denies SUBSCRIBE for client_id=cc:ba:97:11:0e:84 to topic=xiaozhi/cc:ba:97:11:0e:84/down
```
- This explicit message appears when:
  - Subscribe fails
  - Connection is still active
  - Publish succeeds
- **This is the definitive ACL denial indicator**

## Common ACL Denial Scenarios

### Scenario 1: Anonymous User Restrictions
**Logs show:**
- `username=<empty>`
- Connection succeeds
- Subscribe fails
- Publish may or may not work

**Solution:** Configure username/password in device settings, or update broker ACL to allow anonymous SUBSCRIBE.

### Scenario 2: Topic Pattern Mismatch
**Logs show:**
- Device subscribes to: `xiaozhi/cc:ba:97:11:0e:84/down`
- Server publishes to: `xiaozhi/CC:BA:97:11:0E:84/down` (uppercase)
- ACL allows uppercase but not lowercase (or vice versa)

**Solution:** Standardize topic case everywhere (prefer lowercase).

### Scenario 3: Client ID Not in ACL
**Logs show:**
- `client_id=cc:ba:97:11:0e:84`
- Connection succeeds (broker allows connection)
- Subscribe fails (ACL doesn't list this client_id for SUBSCRIBE)

**Solution:** Add client_id to broker ACL with SUBSCRIBE permission.

## Next Steps After Detecting ACL Denial

1. **Check broker ACL configuration:**
   - Verify client_id `cc:ba:97:11:0e:84` has SUBSCRIBE permission
   - Verify topic pattern `xiaozhi/+/down` or exact topic is allowed

2. **Test from command line:**
   ```bash
   mosquitto_sub -h 35.188.112.96 -p 1883 -t 'xiaozhi/cc:ba:97:11:0e:84/down' -v
   ```
   - If this also fails: Confirms ACL issue
   - If this works: Device-specific issue (different client_id/auth?)

3. **Check broker logs:**
   - Look for "not authorized" or "ACL denied" messages
   - Check timestamp matches device subscribe attempt

4. **Fix ACL:**
   - Add SUBSCRIBE permission for the client_id
   - Or configure username/password if broker requires auth for SUBSCRIBE

## Summary

**ACL Denial Indicators:**
- ✅ Connection succeeds (CONNACK received)
- ✅ Connection remains active after subscribe failure
- ✅ Publish works (if tested)
- ❌ Subscribe fails
- 📝 Explicit "DIAGNOSIS: Broker ACL likely denies SUBSCRIBE" message

**If you see all of the above → ACL denial confirmed**

