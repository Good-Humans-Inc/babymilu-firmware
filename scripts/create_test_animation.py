#!/usr/bin/env python3
"""
Create test animation files for upload testing
This script creates simple test animation files in the correct format
"""

import struct
import sys
import os

def create_test_animation(filename, width=64, height=64, frames=3):
    """
    Create a test animation file in the custom LVGL format
    Format: 6 uint32_t header + raw pixel data
    Header: magic, color_format, flags, width, height, stride
    """
    
    # LVGL magic number
    magic = 0x4C56474C  # "LVGL" in little endian
    
    # Color format: LV_COLOR_FORMAT_RGB565
    color_format = 0x0B  # RGB565
    
    # Flags: no special flags
    flags = 0x00
    
    # Calculate stride (bytes per row)
    stride = width * 2  # 2 bytes per pixel for RGB565
    
    # Create header
    header = struct.pack('<IIIIII', magic, color_format, flags, width, height, stride)
    
    # Create simple test pattern for each frame
    frame_data = bytearray()
    
    for frame in range(frames):
        # Create a simple pattern: solid color that changes per frame
        color = 0xF800 if frame == 0 else (0x07E0 if frame == 1 else 0x001F)  # Red, Green, Blue
        
        for y in range(height):
            for x in range(width):
                # Create a simple pattern
                if (x + y + frame) % 4 == 0:
                    pixel_color = color
                else:
                    pixel_color = 0x0000  # Black
                
                frame_data.extend(struct.pack('<H', pixel_color))
    
    # Write file
    with open(filename, 'wb') as f:
        f.write(header)
        f.write(frame_data)
    
    print(f"Created test animation: {filename}")
    print(f"  Size: {len(header) + len(frame_data)} bytes")
    print(f"  Frames: {frames}")
    print(f"  Dimensions: {width}x{height}")
    print(f"  Color format: RGB565")

def main():
    if len(sys.argv) < 2:
        print("Usage: python create_test_animation.py <output_filename> [width] [height] [frames]")
        print("Example: python create_test_animation.py test_animation.bin 64 64 3")
        sys.exit(1)
    
    filename = sys.argv[1]
    width = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    height = int(sys.argv[3]) if len(sys.argv) > 3 else 64
    frames = int(sys.argv[4]) if len(sys.argv) > 4 else 3
    
    create_test_animation(filename, width, height, frames)

if __name__ == "__main__":
    main()
