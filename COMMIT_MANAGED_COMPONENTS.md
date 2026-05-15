# Managed Components Policy

Status: historical note replaced by current policy.

## Current Policy

Do not commit the full `managed_components/` tree. ESP-IDF Component Manager is
expected to download registry components during `idf.py reconfigure` or
`idf.py build`.

The exception is the local custom `78__esp-wifi-connect` patch set:

- `managed_components/78__esp-wifi-connect/wifi_station.cc`
- `managed_components/78__esp-wifi-connect/include/wifi_station.h`

These paths are explicitly unignored in `.gitignore` because they contain local
behavior used by the firmware.

## Why The Old Plan Is Obsolete

This file previously proposed committing all managed components to Git to avoid
Windows download/hash issues. That is no longer the desired repository shape.
Committing all components would increase repository size and make component
updates harder.

## Recovery Commands

If a build has incomplete or stale managed components:

```powershell
Remove-Item -Recurse -Force managed_components -ErrorAction SilentlyContinue
idf.py reconfigure
idf.py build
```

Git will preserve the explicitly tracked `78__esp-wifi-connect` files. If they
are missing after cleanup, restore them from Git before rebuilding.

## Related Docs

- `TROUBLESHOOTING_WINDOWS.md`
- `FIX_HASH_MISMATCH.md`
- `FINAL_SOLUTION.md`
