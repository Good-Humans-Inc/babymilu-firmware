# Fix ESP-IDF Component Hash Mismatch

Hash mismatches usually mean a registry component under `managed_components/`
was edited, partially downloaded, or affected by local filesystem changes.

## Current Repository Policy

The full `managed_components/` tree is ignored. ESP-IDF should download registry
components itself.

Only these local patch files are intentionally tracked:

- `managed_components/78__esp-wifi-connect/wifi_station.cc`
- `managed_components/78__esp-wifi-connect/include/wifi_station.h`

## Recommended Fix

```powershell
Remove-Item -Recurse -Force managed_components -ErrorAction SilentlyContinue
idf.py reconfigure
idf.py build
```

If the custom `78__esp-wifi-connect` files are missing after cleanup, restore
them from Git and rerun configure.

## Avoid

- Do not commit every managed component.
- Do not disable hash checks permanently unless debugging a local emergency.
- Do not edit downloaded registry components directly; patch through tracked
  files or project components instead.
