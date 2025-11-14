# Find FFmpeg on Windows - Run this script

Write-Host "Searching for FFmpeg..." -ForegroundColor Yellow

# Method 1: Check Chocolatey default location
$chocoPath = "C:\ProgramData\chocolatey\lib\ffmpeg"
if (Test-Path $chocoPath) {
    Write-Host "`nFound Chocolatey FFmpeg directory: $chocoPath" -ForegroundColor Green
    $ffmpeg = Get-ChildItem $chocoPath -Recurse -Filter "ffmpeg.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ffmpeg) {
        Write-Host "FFmpeg found at: $($ffmpeg.FullName)" -ForegroundColor Green
        Write-Host "`nTo use it, run:" -ForegroundColor Cyan
        Write-Host "& `"$($ffmpeg.FullName)`" -version" -ForegroundColor White
        return $ffmpeg.FullName
    }
}

# Method 2: Search common installation locations
$searchPaths = @(
    "C:\Program Files\ffmpeg\bin\ffmpeg.exe",
    "C:\Program Files (x86)\ffmpeg\bin\ffmpeg.exe",
    "C:\ffmpeg\bin\ffmpeg.exe",
    "$env:LOCALAPPDATA\Microsoft\WindowsApps\ffmpeg.exe",
    "$env:ProgramFiles\ffmpeg\bin\ffmpeg.exe"
)

Write-Host "`nChecking common locations..." -ForegroundColor Yellow
foreach ($path in $searchPaths) {
    if (Test-Path $path) {
        Write-Host "Found at: $path" -ForegroundColor Green
        return $path
    }
}

# Method 3: Search entire C: drive (slow but thorough)
Write-Host "`nSearching C: drive (this may take a while)..." -ForegroundColor Yellow
$ffmpeg = Get-ChildItem C:\ -Recurse -Filter "ffmpeg.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($ffmpeg) {
    Write-Host "Found at: $($ffmpeg.FullName)" -ForegroundColor Green
    return $ffmpeg.FullName
}

Write-Host "`nFFmpeg not found. It may not be installed correctly." -ForegroundColor Red
Write-Host "Try reinstalling: choco install ffmpeg --force" -ForegroundColor Yellow

