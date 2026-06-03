# Stage 1 Local Tools

This folder contains local-only validation helpers for the ground-up BabyMilu
firmware.

Run:

```powershell
python tools/contract_harness.py
```

The harness validates endpoint defaults, OTA manifest parsing, activation-field
ignore behavior, WebSocket URL generation, hello payload shape, listen
start/stop JSON, and canonical `connectionType` propagation. It does not call
cloud services or mutate local/cloud resources.
