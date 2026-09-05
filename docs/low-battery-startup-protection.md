# EchoEar low-battery startup protection

EchoEar checks battery voltage before audio, Wi-Fi, BLE provisioning, MQTT, or
animation downloads begin. The protection is intentionally a startup hold, not
a restart or shutdown loop.

## Policy

- Collect five samples and enter protection only when at least four valid
  samples are at or below 3060 mV (approximately 5% using the current gauge).
- Reduce the backlight and show a persistent prompt to plug in the charger.
- Keep audio and networking stopped while protected.
- Resume normal startup only after three consecutive samples at or above
  3180 mV (approximately 15%). This hysteresis prevents repeated transitions
  near the critical threshold.
- A missing or failed gauge does not report `0%` and does not block startup.

The device remains able to sample the charge controller while held, so plugging
it in recovers the same boot without an automatic reboot. The Wi-Fi credentials
and provisioning attempt are not modified by this protection.
