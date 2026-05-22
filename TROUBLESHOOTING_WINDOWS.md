# Windows Build Troubleshooting

## Missing `opus.h`

If the build cannot find Opus headers, refresh ESP-IDF managed components:

```powershell
idf.py fullclean
Remove-Item -Recurse -Force managed_components -ErrorAction SilentlyContinue
idf.py reconfigure
idf.py build
```

Expected downloaded paths include:

- `managed_components/78__esp-opus/include/opus.h`
- `managed_components/78__esp-opus-encoder/include/opus_encoder.h`

## Component Policy

Do not commit the full `managed_components/` tree. The repo intentionally tracks
only the local `78__esp-wifi-connect` source/header patch files.

## Path Length

If component download or build fails in a deep directory, move the repo to a
shorter path such as:

```text
D:\esp\echoear
```

## Useful Checks

```powershell
idf.py --version
python --version
Test-Path managed_components\78__esp-opus\include\opus.h
Get-ChildItem managed_components | Select-Object Name
```

## Line Ending Warnings

Git may warn that LF will be replaced by CRLF on Windows. Those warnings are not
the same as ESP-IDF hash mismatches.
