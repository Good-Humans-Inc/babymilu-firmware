# Quick Fix: FFmpeg Installed But Not Found

## The Problem

Chocolatey installed FFmpeg, but PowerShell can't find it. This happens because:
- PATH wasn't updated in your current session
- FFmpeg is installed but PATH wasn't refreshed

## Quick Solutions (Try in Order)

### Solution 1: Refresh PATH (Easiest)

Run this in your current PowerShell window:

```powershell
# Refresh PATH in current session
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")

# Test it
ffmpeg -version
```

If this works, you're done! Continue with video extraction.

### Solution 2: Find and Use Full Path

If Solution 1 doesn't work, find where FFmpeg is installed:

```powershell
# Find FFmpeg executable
Get-ChildItem "C:\ProgramData\chocolatey\lib\ffmpeg" -Recurse -Filter "ffmpeg.exe" | Select-Object FullName
```

This will show you the full path. Then use it directly:

```powershell
# Example (replace with your actual path)
& "C:\ProgramData\chocolatey\lib\ffmpeg\tools\ffmpeg\bin\ffmpeg.exe" -version
```

**Or create an alias for this session:**

```powershell
# Find the path first
$ffmpegPath = (Get-ChildItem "C:\ProgramData\chocolatey\lib\ffmpeg" -Recurse -Filter "ffmpeg.exe").FullName | Select-Object -First 1

# Create alias
Set-Alias ffmpeg $ffmpegPath

# Test
ffmpeg -version
```

### Solution 3: Add to PATH Permanently

1. **Find FFmpeg location:**
   ```powershell
   Get-ChildItem "C:\ProgramData\chocolatey\lib\ffmpeg" -Recurse -Filter "ffmpeg.exe" | Select-Object DirectoryName
   ```
   Note the directory (usually something like `C:\ProgramData\chocolatey\lib\ffmpeg\tools\ffmpeg\bin`)

2. **Add to PATH:**
   - Press `Win + X` → System → Advanced system settings
   - Click "Environment Variables"
   - Under "User variables" (or "System variables"), find "Path" → Edit
   - Click "New" → Paste the directory path from step 1
   - Click OK on all dialogs

3. **Restart PowerShell** completely (close and reopen)

4. **Test:**
   ```powershell
   ffmpeg -version
   ```

## Quick Workaround: Use Full Path for Now

If you just want to get started immediately, you can use the full path in your commands:

```powershell
# Find FFmpeg path
$ffmpeg = (Get-ChildItem "C:\ProgramData\chocolatey\lib\ffmpeg" -Recurse -Filter "ffmpeg.exe").FullName | Select-Object -First 1

# Use it directly
& $ffmpeg -i sylus5.mp4 -vf fps=10 -q:v 3 frames/frame_%04d.jpg
```

Or create a variable:

```powershell
# Set once per session
$ffmpeg = (Get-ChildItem "C:\ProgramData\chocolatey\lib\ffmpeg" -Recurse -Filter "ffmpeg.exe").FullName | Select-Object -First 1

# Then use $ffmpeg instead of ffmpeg
& $ffmpeg -i sylus5.mp4 -vf fps=10 -q:v 3 frames/frame_%04d.jpg
& $ffmpeg -i sylus5.mp4 -ar 16000 -ac 1 -sample_fmt s16 audio.wav
```

## Verify It Works

After applying any solution:

```powershell
ffmpeg -version
```

You should see:
```
ffmpeg version 8.0.0
...
```

If you see version info, you're ready to extract your video!

