# EchoEar Firmware

This file is kept for old Xiaozhi README links. The active repo is now
EchoEar-only; old multi-board Xiaozhi hardware gallery content has been removed.

See:

- `README.md`
- `docs/AGENT_CONTEXT.md`
- `CODEBASE_WALKTHROUGH.md`

Current board source: `main/boards/echoear/echoear.cc`.

Current build:

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```
