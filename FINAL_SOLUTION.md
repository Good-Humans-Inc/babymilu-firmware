# ESP-IDF Component Manager Policy

Current policy:

- Do not commit the full `managed_components/` directory.
- Do not rely on a committed `dependencies.lock`; it is currently ignored.
- Let ESP-IDF Component Manager download registry components.
- Keep only the intentionally tracked local `78__esp-wifi-connect` patch files.

Tracked local component files:

- `managed_components/78__esp-wifi-connect/wifi_station.cc`
- `managed_components/78__esp-wifi-connect/include/wifi_station.h`

## Clean Reconfigure

```powershell
Remove-Item -Recurse -Force managed_components -ErrorAction SilentlyContinue
git restore -- managed_components/78__esp-wifi-connect/wifi_station.cc `
              managed_components/78__esp-wifi-connect/include/wifi_station.h
idf.py reconfigure
idf.py build
```

If Git reports the two tracked files are already present, the restore step is a
no-op.
