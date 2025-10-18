#!/bin/bash

# Script to upload animation files to the dedicated animations partition
# Usage: ./scripts/upload_animations.sh

echo "=== Animation SPIFFS Upload Script ==="

# Create temporary directory for SPIFFS data
TEMP_DIR="temp_animations"
mkdir -p $TEMP_DIR

echo "1. Converting animation files to binary format..."

# Convert C files to binary (you'll need to run these first)
if [ ! -f "normal1.bin" ]; then
    echo "Converting normal1.c to normal1.bin..."
    python scripts/convert_animation_to_spiffs.py main/animation/img/normal1.c normal1.bin
fi

if [ ! -f "normal2.bin" ]; then
    echo "Converting normal2.c to normal2.bin..."
    python scripts/convert_animation_to_spiffs.py main/animation/img/normal2.c normal2.bin
fi

if [ ! -f "normal3.bin" ]; then
    echo "Converting normal3.c to normal3.bin..."
    python scripts/convert_animation_to_spiffs.py main/animation/img/normal3.c normal3.bin
fi

echo "2. Creating SPIFFS image..."

# Copy binary files to SPIFFS directory
cp normal1.bin $TEMP_DIR/
cp normal2.bin $TEMP_DIR/
cp normal3.bin $TEMP_DIR/

# Generate SPIFFS image
idf.py spiffsgen $TEMP_DIR spiffs_animations.bin --partition animations

echo "3. Flashing to animations partition..."

# Flash the SPIFFS image to the animations partition
idf.py spiffs-flash --partition animations

echo "4. Cleaning up..."
rm -rf $TEMP_DIR
rm -f spiffs_animations.bin

echo "=== Upload Complete ==="
echo "Animation files are now stored in the dedicated 1MB animations partition!"
echo "You can access them at /spiffs/normal1.bin, /spiffs/normal2.bin, /spiffs/normal3.bin"
