# Windows Video Setup Guide - Step by Step

This guide will walk you through setting up video playback on Windows, from installing FFmpeg to creating your playback files.

## Step 1: Install FFmpeg on Windows

### Option A: Using Chocolatey (Recommended - Easiest)

1. **Install Chocolatey** (if not already installed):
   - Open PowerShell as Administrator
   - Run:
   ```powershell
   Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
   ```

2. **Install FFmpeg**:
   ```powershell
   choco install ffmpeg
   ```

3. **Close and reopen PowerShell** to refresh PATH

### Option B: Manual Installation

1. **Download FFmpeg**:
   - Go to: https://www.gyan.dev/ffmpeg/builds/
   - Download: `ffmpeg-release-essentials.zip` (or latest version)

2. **Extract**:
   - Extract to: `C:\ffmpeg\`

3. **Add to PATH**:
   - Press `Win + X` → System → Advanced system settings
   - Click "Environment Variables"
   - Under "System variables", find "Path" → Edit
   - Click "New" → Add: `C:\ffmpeg\bin`
   - Click OK on all dialogs

4. **Restart PowerShell** to apply changes

### Verify Installation

Open a new PowerShell window and run:
```powershell
ffmpeg -version
```

You should see FFmpeg version information. If you see an error, FFmpeg is not in your PATH.

### FFmpeg Installed But Not Found? (Common Issue)

If Chocolatey says FFmpeg is installed but you get "not recognized" error:

**Quick Fix - Refresh PATH:**
```powershell
# Refresh environment variables in current session
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")

# Try again
ffmpeg -version
```

**Or find FFmpeg location:**
```powershell
# Find where Chocolatey installed FFmpeg
Get-ChildItem "C:\ProgramData\chocolatey\lib\ffmpeg" -Recurse -Filter "ffmpeg.exe" | Select-Object FullName

# Or check common locations
Get-Command ffmpeg -ErrorAction SilentlyContinue
```

**Or use full path:**
```powershell
# Try common Chocolatey installation path
& "C:\ProgramData\chocolatey\lib\ffmpeg\tools\ffmpeg\bin\ffmpeg.exe" -version
```

**Permanent Fix - Add to PATH:**
1. Find FFmpeg location (use command above)
2. Add that directory to your PATH (see Manual Installation section)
3. Restart PowerShell

## Step 2: Prepare Your Video Files

### Navigate to Your Video Directory

```powershell
cd C:\Users\babym\Desktop\video
```

### Step 2.1: Extract Video Frames

```powershell
# Create frames directory
mkdir frames

# Extract frames at 10 FPS
ffmpeg -i sylus5.mp4 -vf fps=10 -q:v 3 frames/frame_%04d.jpg
```

**What this does:**
- `-i sylus5.mp4`: Input video file
- `-vf fps=10`: Extract at 10 frames per second
- `-q:v 3`: JPEG quality (3 = good balance, lower = better quality but larger files)
- `frames/frame_%04d.jpg`: Output pattern (frame_0001.jpg, frame_0002.jpg, etc.)

**Alternative: Match display resolution** (if you know your display size):
```powershell
# For 240x240 display
ffmpeg -i sylus5.mp4 -vf "fps=10,scale=240:240" -q:v 3 frames/frame_%04d.jpg

# For 320x240 display
ffmpeg -i sylus5.mp4 -vf "fps=10,scale=320:240" -q:v 3 frames/frame_%04d.jpg
```

### Step 2.2: Count the Frames

```powershell
# Count how many frames were created
(Get-ChildItem frames\*.jpg).Count
```

**Note the number** - you'll need it for the script file.

### Step 2.3: Extract Audio

```powershell
# Extract audio as 16kHz mono WAV
ffmpeg -i sylus5.mp4 -ar 16000 -ac 1 -sample_fmt s16 audio.wav
```

**What this does:**
- `-ar 16000`: 16kHz sample rate
- `-ac 1`: Mono (1 channel)
- `-sample_fmt s16`: 16-bit PCM
- `audio.wav`: Output audio file

## Step 3: Create the Playback Script

Create a file named `playback.json` in your video directory:

```powershell
# Create the JSON file
@"
{
  "type": "video",
  "video": {
    "frame_directory": "frames",
    "frame_prefix": "frame_",
    "frame_format": "jpg",
    "frame_count": 150,
    "fps": 10
  },
  "audio": {
    "file": "audio.wav",
    "sync": true
  }
}
"@ | Out-File -FilePath playback.json -Encoding utf8
```

**Important:** Replace `150` with the actual number of frames you counted in Step 2.2!

### Edit the Script Manually (Recommended)

1. Open `playback.json` in Notepad or any text editor
2. Update `frame_count` with your actual frame count
3. Save the file

## Step 4: Copy Files to SD Card

### Check Your Directory Structure

Your directory should look like this:
```
C:\Users\babym\Desktop\video\
├── sylus5.mp4          (original video - not needed on SD card)
├── frames\
│   ├── frame_0001.jpg
│   ├── frame_0002.jpg
│   ├── frame_0003.jpg
│   └── ... (all your frames)
├── audio.wav
└── playback.json
```

### Copy to SD Card

1. **Insert your SD card** into your computer
2. **Copy the following to SD card root:**
   - `frames\` folder (with all frame images)
   - `audio.wav` file
   - `playback.json` file

**SD Card structure should be:**
```
/sdcard/
├── frames/
│   ├── frame_0001.jpg
│   ├── frame_0002.jpg
│   └── ...
├── audio.wav
└── playback.json
```

## Step 5: Test on Device

1. **Insert SD card** into your ESP32-S3 device
2. **Power on** the device
3. **Press the boot button** - video should start playing!

## Troubleshooting

### FFmpeg Not Found After Installation

1. **Close all PowerShell windows**
2. **Open a new PowerShell window**
3. **Try again**: `ffmpeg -version`

If still not working:
- Check PATH: `$env:PATH` (should include ffmpeg path)
- Restart your computer
- Try manual installation method

### Frame Count Mismatch

If video doesn't play correctly:
1. Re-count frames: `(Get-ChildItem frames\*.jpg).Count`
2. Update `frame_count` in `playback.json`
3. Copy updated `playback.json` to SD card

### Video Too Large

If frames are too large:
```powershell
# Use lower quality (larger number = smaller files)
ffmpeg -i sylus5.mp4 -vf fps=10 -q:v 5 frames/frame_%04d.jpg

# Or lower FPS
ffmpeg -i sylus5.mp4 -vf fps=8 -q:v 3 frames/frame_%04d.jpg
```

### Check Frame Files

```powershell
# List first few frames
Get-ChildItem frames\*.jpg | Select-Object -First 5

# Check frame sizes
Get-ChildItem frames\*.jpg | Measure-Object -Property Length -Sum
```

## Quick Reference Commands

```powershell
# Navigate to video directory
cd C:\Users\babym\Desktop\video

# Extract frames
ffmpeg -i sylus5.mp4 -vf fps=10 -q:v 3 frames/frame_%04d.jpg

# Count frames
(Get-ChildItem frames\*.jpg).Count

# Extract audio
ffmpeg -i sylus5.mp4 -ar 16000 -ac 1 -sample_fmt s16 audio.wav

# Check file sizes
Get-ChildItem frames\*.jpg | Measure-Object -Property Length -Sum
Get-Item audio.wav | Select-Object Name, @{Name="Size(MB)";Expression={[math]::Round($_.Length/1MB,2)}}
```

## Next Steps

Once your video is working:
- Try different FPS (8, 12, 15)
- Adjust quality settings (`-q:v 2` to `-q:v 5`)
- Create multiple videos with different scripts
- Experiment with resolution scaling

