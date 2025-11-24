# MQTT Debugging Guide

## Current Status
From your logs:
- ✅ Device connects to broker: `35.188.112.96:1883`
- ✅ Connection status shows "connected"
- ❌ Subscription fails: `xiaozhi/cc:ba:97:11:0e:84/down`
- Device config: `client_id=cc:ba:97:11:0e:84`, `username=<empty>`

## Quick Tests (Run in Order)

### Test 1: Verify Broker is Reachable
```bash
# Test if broker accepts connections
mosquitto_sub -h 35.188.112.96 -p 1883 -t 'test/#' -v
```
**Expected**: Should connect and wait (no errors)
**If fails**: Network/firewall issue or broker down

### Test 2: Test Subscription from Your Machine
```bash
# Try subscribing to the exact topic the device uses
mosquitto_sub -h 35.188.112.96 -p 1883 -t 'xiaozhi/cc:ba:97:11:0e:84/down' -v
```
**Expected**: Should subscribe successfully
**If fails**: ACL/permission issue on broker

### Test 3: Test Publishing to Device Topic
In one terminal, keep the subscription from Test 2 running.

In another terminal:
```bash
# Publish a test message
mosquitto_pub -h 35.188.112.96 -p 1883 -t 'xiaozhi/cc:ba:97:11:0e:84/down' -m '{"type":"ping"}'
```
**Expected**: 
- Message appears in Test 2 terminal (broker works)
- Device logs show "MQTT RX topic=..." (device receives it)

### Test 4: Test with Retained Message
```bash
# Publish retained message (device will receive on next connect)
mosquitto_pub -h 35.188.112.96 -p 1883 -t 'xiaozhi/cc:ba:97:11:0e:84/down' -r -m '{"type":"test","message":"retained"}'
```
**Expected**: Device receives message immediately if subscribed, or on next connection

### Test 5: Test Case Sensitivity
```bash
# Try uppercase MAC (as your server might use)
mosquitto_pub -h 35.188.112.96 -p 1883 -t 'xiaozhi/CC:BA:97:11:0E:84/down' -m '{"type":"ping"}'
```
**Expected**: Should work if broker is case-insensitive, but device won't receive if it only subscribes to lowercase

### Test 6: Monitor All Traffic
```bash
# Watch all messages on xiaozhi topics
mosquitto_sub -h 35.188.112.96 -p 1883 -t 'xiaozhi/#' -v
```
**Expected**: See all publishes to any xiaozhi topic

## Gradual Debugging Steps

### Step 1: Verify Device Configuration
Check device logs for:
```
MQTT config: endpoint=35.188.112.96:1883, client_id=cc:ba:97:11:0e:84, username=<empty>, keepalive=120, up_topic=xiaozhi/cc:ba:97:11:0e:84/up, down_topic=xiaozhi/cc:ba:97:11:0e:84/down
Broker parsed: 35.188.112.96:1883
```

**Action**: Confirm these match what you expect

### Step 2: Check Connection Timing
Look for these log sequences:
```
I (...) MQTT: Connected to endpoint 35.188.112.96:1883
I (...) MQTT: MQTT connection status after Connect(): connected
I (...) MQTT: MQTT client fully connected (CONNACK received)  <-- This should appear
I (...) MQTT: Subscribing to topic (from OnConnected): ...   <-- This should appear
```

**If OnConnected callback doesn't fire**: The MQTT library may not support it, or there's a timing issue

### Step 3: Test Direct Publish
Even if subscription fails, try publishing from device:
- Device should be able to publish to `xiaozhi/cc:ba:97:11:0e:84/up`
- Monitor with: `mosquitto_sub -h 35.188.112.96 -p 1883 -t 'xiaozhi/#' -v`

### Step 4: Check Broker ACLs
If subscription fails but connection succeeds, likely causes:
1. **ACL denies SUBSCRIBE**: Broker ACL rules don't allow this client_id to subscribe
2. **Topic pattern mismatch**: Broker requires different topic format
3. **Username/auth required**: Empty username might not have subscribe permissions

**Action**: Check broker logs/ACL configuration for client `cc:ba:97:11:0e:84`

### Step 5: Test with Username/Password
If broker requires auth:
1. Configure username/password in device settings
2. Check logs for: `username=<set>` instead of `<empty>`
3. Retry subscription

### Step 6: Test Retry Mechanism
With the updated code, subscription should retry in OnConnected callback. Watch for:
```
I (...) MQTT: Attempting initial subscription to topic: ...
W (...) MQTT: Initial subscribe failed (may retry in OnConnected): ...
I (...) MQTT: MQTT client fully connected (CONNACK received)
I (...) MQTT: Subscribing to topic (from OnConnected): ...
```

## Common Issues & Solutions

### Issue: Subscription fails but connection succeeds
**Possible causes**:
- Subscribe called before CONNACK (fixed with OnConnected retry)
- Broker ACL denies SUBSCRIBE permission
- Client_id collision (another device using same ID)

**Solution**: 
- Check broker ACLs
- Ensure unique client_id
- Wait for OnConnected callback logs

### Issue: Device never receives messages
**Possible causes**:
- Subscription never succeeded
- Topic case mismatch (device uses lowercase, server uses uppercase)
- Device not connected when message published

**Solution**:
- Verify subscription success in logs
- Test with retained message
- Standardize topic case (use lowercase everywhere)

### Issue: Messages received but not processed
**Check logs for**:
```
MQTT RX topic=... len=... prefix=...
```
**If this appears**: Device is receiving, check message parsing logs

## Expected Log Sequence (After Fix)

```
I (...) MQTT: MQTT config: endpoint=35.188.112.96:1883, client_id=cc:ba:97:11:0e:84, username=<empty>, keepalive=120, up_topic=xiaozhi/cc:ba:97:11:0e:84/up, down_topic=xiaozhi/cc:ba:97:11:0e:84/down
I (...) MQTT: Connecting to endpoint 35.188.112.96:1883
I (...) MQTT: Broker parsed: 35.188.112.96:1883
I (...) MQTT: Connected to endpoint 35.188.112.96:1883
I (...) MQTT: MQTT connection status after Connect(): connected
I (...) MQTT: Attempting initial subscription to topic: xiaozhi/cc:ba:97:11:0e:84/down
I (...) MQTT: MQTT client fully connected (CONNACK received)
I (...) MQTT: Subscribing to topic (from OnConnected): xiaozhi/cc:ba:97:11:0e:84/down
I (...) MQTT: Successfully subscribed to topic (from OnConnected): xiaozhi/cc:ba:97:11:0e:84/down
```

## Next Steps After Testing

1. **If subscription succeeds in OnConnected**: Issue was timing, now fixed
2. **If subscription still fails**: Check broker ACLs and permissions
3. **If messages don't arrive**: Verify topic case matches exactly
4. **If device receives but doesn't process**: Check message parsing logs

