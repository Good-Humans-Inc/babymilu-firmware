# EchoEar Folder Archive

This folder is not part of the active build.

The current EchoEar board implementation is:

- `main/boards/echoear/echoear.cc`
- `main/boards/echoear/config.h`
- `main/boards/echoear/touch.h`

The active build selects EchoEar through `main/CMakeLists.txt`, which hard-sets
`BOARD_TYPE` to `echoear`. Do not use files in `echoear folder/` as the source of
truth for current firmware behavior.

For current setup and build notes, see:

- `README.md`
- `docs/AGENT_CONTEXT.md`
- `CODEBASE_WALKTHROUGH.md`
