# EchoEar Firmware

This file is kept for old Japanese README links. The current active
documentation is in English and describes the EchoEar-only firmware.

See:

- `README.md`
- `docs/AGENT_CONTEXT.md`
- `CODEBASE_WALKTHROUGH.md`

Current board source: `main/boards/echoear/echoear.cc`.

Build:

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```
