# WiFi Provisioning via BLE

This guide explains how to set up WiFi credentials on the device using Bluetooth Low Energy (BLE) and the nRF Connect mobile application.

---

## Prerequisites

1.  **ESP-IDF:** Your development environment must be set up.
2.  **nRF Connect for Mobile:** Install the app on your smartphone (available for [Android](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp) and [iOS](https://apps.apple.com/us/app/nrf-connect-for-mobile/id1054362403)).

---

## Setup Instructions

### 1. Erase Device Flash

Before provisioning, completely erase the device's flash to clear any old WiFi settings or other stored data.

```bash
idf.py erase-flash
```

After erasing, flash the new firmware onto the device.

```bash
idf.py flash monitor
```

### 2. Send WiFi Credentials

Once the device starts up for the first time, follow these steps using the nRF Connect app.

1.  **Scan for Devices:** Launch nRF Connect and start scanning for BLE devices.
2.  **Find and Connect:** Locate your device, advertised as `Xiaozhi-Bluetooth`, and tap **Connect**. It's recommended to enable the "autoConnect" option.
3.  **Locate the Characteristic:**
    * Once connected, find the service listed as **Unknown Service**.
    * Within that service, tap the **Write** characteristic (it will have an upward-facing arrow icon).
4.  **Send SSID:**
    * In the write value dialog, select the **TEXT** type.
    * Enter your network's SSID in the following format: `ssid:YOUR_SSID`
    * Tap **Send**.
5.  **Send Password:**
    * In the same dialog, enter your network's password in the following format: `pwd:YOUR_PASSWORD`
    * Tap **Send**.

> **IMPORTANT:** Do not add any spaces before or after the colon (`:`) in your messages.
> * Correct: `ssid:MyNetwork`
> * Incorrect: `ssid: MyNetwork`

### 3. Finalizing Connection

After both the SSID and password have been sent successfully, the device will automatically save the credentials, restart, and attempt to connect to the configured WiFi network. You can monitor the device's output to confirm a successful connection.
