# Fix Hash Mismatch Error for Managed Components

## Problem
ESP-IDF checks file hashes to ensure components haven't been modified. When components are committed to git, line endings or other changes can cause hash mismatches.

## Solution Options

### Option 1: Delete and Re-download Components (Recommended)

Have your coworker run these commands:

```powershell
# Delete managed_components directory
Remove-Item -Recurse -Force managed_components

# Reconfigure (this will re-download components with correct hashes)
idf.py reconfigure

# Build
idf.py build
```

**Why this works**: ESP-IDF will re-download components from the registry with correct hashes, matching what's in `dependencies.lock`.

### Option 2: Disable Hash Verification (Less Secure, But Faster)

If Option 1 doesn't work or you want to skip hash checking entirely, you can disable it by setting an environment variable:

**Windows PowerShell:**
```powershell
$env:IDF_COMPONENT_MANAGER_DISABLE_HASH_CHECK="1"
idf.py reconfigure
idf.py build
```

**Windows CMD:**
```cmd
set IDF_COMPONENT_MANAGER_DISABLE_HASH_CHECK=1
idf.py reconfigure
idf.py build
```

**Permanent (add to your shell profile):**
Add to your PowerShell profile (`$PROFILE`):
```powershell
$env:IDF_COMPONENT_MANAGER_DISABLE_HASH_CHECK="1"
```

### Option 3: Fix Git Line Endings (Preventive)

I've created a `.gitattributes` file to preserve file content. After pulling, your coworker should:

```powershell
# Refresh git attributes
git add .gitattributes
git commit -m "Add gitattributes to preserve managed_components"

# Reset managed_components to match repository
git checkout HEAD -- managed_components/

# Then delete and re-download (Option 1)
Remove-Item -Recurse -Force managed_components
idf.py reconfigure
```

## Recommended Approach

**For your coworker (immediate fix):**
```powershell
# Quick fix - delete and re-download
Remove-Item -Recurse -Force managed_components
idf.py reconfigure
idf.py build
```

**For the team (long-term):**
1. ✅ I've added `.gitattributes` to preserve file content
2. Consider putting `managed_components/` back in `.gitignore` and let ESP-IDF manage it
3. Or use Option 2 to disable hash checking if you're committed to versioning components in git

## Why This Happened

- Git on Windows may convert line endings (CRLF vs LF)
- File permissions might change
- ESP-IDF expects exact file hashes as downloaded from registry
- Committing to git modifies files, breaking hash verification

## Alternative: Don't Commit Managed Components

If hash issues persist, consider:
1. Put `managed_components/` back in `.gitignore`
2. Commit `dependencies.lock` (ensures same versions)
3. Let ESP-IDF download components automatically
4. This is the ESP-IDF recommended approach

