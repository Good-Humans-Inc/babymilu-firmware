@echo off
REM Script to upload animation files to the dedicated animations partition
REM Usage: scripts\upload_animations.bat

echo === Animation SPIFFS Upload Script ===

REM Create temporary directory for SPIFFS data
set TEMP_DIR=temp_animations
if not exist %TEMP_DIR% mkdir %TEMP_DIR%

echo 1. Converting animation files to binary format...

REM Convert C files to binary (you'll need to run these first)
if not exist "normal1.bin" (
    echo Converting normal1.c to normal1.bin...
    python scripts\convert_animation_to_spiffs.py main\animation\img\normal1.c normal1.bin
)

if not exist "normal2.bin" (
    echo Converting normal2.c to normal2.bin...
    python scripts\convert_animation_to_spiffs.py main\animation\img\normal2.c normal2.bin
)

if not exist "normal3.bin" (
    echo Converting normal3.c to normal3.bin...
    python scripts\convert_animation_to_spiffs.py main\animation\img\normal3.c normal3.bin
)

echo 2. Creating SPIFFS image...

REM Copy binary files to SPIFFS directory
copy normal1.bin %TEMP_DIR%\
copy normal2.bin %TEMP_DIR%\
copy normal3.bin %TEMP_DIR%\

REM Generate SPIFFS image
idf.py spiffsgen %TEMP_DIR% spiffs_animations.bin --partition animations

echo 3. Flashing to animations partition...

REM Flash the SPIFFS image to the animations partition
idf.py spiffs-flash --partition animations

echo 4. Cleaning up...
rmdir /s /q %TEMP_DIR%
del spiffs_animations.bin

echo === Upload Complete ===
echo Animation files are now stored in the dedicated 1MB animations partition!
echo You can access them at /spiffs/normal1.bin, /spiffs/normal2.bin, /spiffs/normal3.bin
