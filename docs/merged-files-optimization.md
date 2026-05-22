# Obsolete Merged Files Optimization Note

Status: obsolete.

This document previously described an `animation_is_using_merged_files()` style
optimization for frame-based merged files. That function is not present in the
current codebase, and the current EchoEar runtime uses a GIF bundle at
`/sdcard/test.bin`.

Current asset references:

- `MIGRATION_13_TO_20_GIFS_GUIDE.md`
- `EMOTION_MAPPING_GUIDE.md`
- `STARTUP_GIF_RENDER_FIX.md`

Do not use this document as implementation guidance unless merged-frame support
is intentionally restored in code.
