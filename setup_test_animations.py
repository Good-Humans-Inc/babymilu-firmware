#!/usr/bin/env python3
"""
Setup script to create test animation files for the Xiaozhi Animation Updater
Creates sample .bin files in the correct LVGL format
"""

import os
import struct

def create_lvgl_animation_file(filename, width=32, height=32, color_format=0):
    """
    Create a minimal LVGL-compatible .bin file
    
    Args:
        filename: Output filename
        width: Image width in pixels
        height: Image height in pixels
        color_format: LVGL color format (0 = RGB565)
    """
    
    # Calculate stride (bytes per row)
    if color_format == 0:  # RGB565
        bytes_per_pixel = 2
    else:
        bytes_per_pixel = 4  # Default to RGBA8888
    
    stride = width * bytes_per_pixel
    
    # Create header
    header = struct.pack('<IIIIII',
        0x4C56474C,  # Magic number "LVGL" in little endian
        color_format,  # Color format
        0,           # Flags
        width,       # Width
        height,      # Height
        stride       # Stride
    )
    
    # Create dummy pixel data
    pixel_data_size = width * height * bytes_per_pixel
    if color_format == 0:  # RGB565
        # Create a simple pattern: red pixels
        pixel_data = b'\xF8\x00' * (width * height)  # Red in RGB565
    else:  # RGBA8888
        # Create a simple pattern: red pixels with full alpha
        pixel_data = b'\xFF\x00\x00\xFF' * (width * height)  # Red in RGBA8888
    
    # Write file
    with open(filename, 'wb') as f:
        f.write(header)
        f.write(pixel_data)
    
    print(f"Created: {filename} ({len(header + pixel_data)} bytes, {width}x{height})")

def main():
    print("Setting up test animation files...")
    
    # Create animations directory
    animations_dir = "animations"
    if not os.path.exists(animations_dir):
        os.makedirs(animations_dir)
        print(f"Created directory: {animations_dir}")
    
    # Create test animation files
    test_files = [
        ("normal1.bin", 32, 32),
        ("normal2.bin", 32, 32),
        ("normal3.bin", 32, 32),
        ("happy1.bin", 32, 32),
        ("happy2.bin", 32, 32),
        ("sad1.bin", 32, 32),
        ("sad2.bin", 32, 32),
    ]
    
    for filename, width, height in test_files:
        filepath = os.path.join(animations_dir, filename)
        create_lvgl_animation_file(filepath, width, height)
    
    print(f"\nCreated {len(test_files)} test animation files in {animations_dir}/")
    print("\nYou can now run the test server with:")
    print("python test_animation_server.py")

if __name__ == "__main__":
    main()


