# Troubleshooting Windows Build Issue: Missing opus.h

## Problem
Error: `fatal error: opus.h: No such file or directory` on Windows, but build works on other machines.

## Root Cause
The `managed_components/` directory is in `.gitignore`, so components must be downloaded by ESP-IDF Component Manager. If components aren't properly downloaded, this error occurs.

## Solution Steps

### Step 1: Verify Components Exist
Check if these directories exist:
```
managed_components/78__esp-opus/
managed_components/78__esp-opus-encoder/
```

If they don't exist or are incomplete, proceed to Step 2.

### Step 2: Clean and Re-download Components

**Option A: Full Clean (Recommended)**
```bash
# Clean build directory
idf.py fullclean

# Remove managed_components (if it exists but is incomplete)
# On Windows PowerShell:
Remove-Item -Recurse -Force managed_components -ErrorAction SilentlyContinue

# Reconfigure (this triggers component download)
idf.py reconfigure

# Build
idf.py build
```

**Option B: Just Reconfigure**
```bash
idf.py reconfigure
idf.py build
```

### Step 3: Verify Component Files

After running `idf.py reconfigure`, verify these files exist:
- `managed_components/78__esp-opus/include/opus.h` ✅
- `managed_components/78__esp-opus-encoder/include/opus_encoder.h` ✅

### Step 4: Check Component Manager Cache (if still failing)

If components still don't download, clear the component manager cache:

**Windows PowerShell:**
```powershell
# Find cache location (usually in user directory)
$env:IDF_COMPONENT_REGISTRY_URL
# Or check: %USERPROFILE%\.espressif\component_registry\

# Clear cache by deleting the registry cache
Remove-Item -Recurse -Force "$env:USERPROFILE\.espressif\component_registry" -ErrorAction SilentlyContinue

# Then reconfigure
idf.py reconfigure
```

### Step 5: Verify Network/Proxy Issues

If components still fail to download:
- Check internet connection
- If behind corporate proxy, configure ESP-IDF proxy settings
- Check firewall isn't blocking component registry access

### Step 6: Manual Verification

If automatic download fails, verify the `dependencies.lock` file exists and contains:
```yaml
78/esp-opus:
  version: 1.0.5
78/esp-opus-encoder:
  version: 2.3.3
  dependencies:
    - name: 78/esp-opus
      version: ^1.0.5
```

## Common Windows-Specific Issues

1. **Path Length Issues**: Windows has 260 character path limit. If project path is very long, components might fail to download. Consider moving project to shorter path like `C:\esp\project\`

2. **Antivirus**: Some antivirus software blocks component downloads. Temporarily disable or add exception.

3. **Permissions**: Ensure write permissions to project directory.

4. **Git Line Endings**: If using Git, ensure `.gitattributes` doesn't convert component files incorrectly.

## Verification Commands

After fixing, verify with:
```bash
# Check if opus.h exists
Test-Path managed_components\78__esp-opus\include\opus.h

# List all managed components
Get-ChildItem managed_components | Select-Object Name

# Check build output for component paths
idf.py build 2>&1 | Select-String "opus"
```

## Still Not Working?

1. Compare your `dependencies.lock` file with coworker's
2. Check ESP-IDF version matches: `idf.py --version`
3. Verify Python version: `python --version` (should be 3.8+)
4. Check if `idf_component.yml` in `main/` directory is correct

