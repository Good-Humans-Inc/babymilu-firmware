#!/usr/bin/env python3
"""
Simple converter to extract binary data from LVGL C files
"""

import sys
import re
import struct

def extract_binary_data(c_file_path):
    """Extract binary data from LVGL C file"""
    
    with open(c_file_path, 'r') as f:
        content = f.read()
    
    # Find the array data between the braces
    array_pattern = r'uint8_t \w+_map\[\] = \{(.*?)\};'
    array_match = re.search(array_pattern, content, re.DOTALL)
    
    if not array_match:
        print(f"Error: Could not find array data in {c_file_path}")
        return None
    
    # Extract all hex values
    hex_values = []
    for line in array_match.group(1).split('\n'):
        # Find all hex values in the line
        hex_matches = re.findall(r'0x([0-9a-fA-F]{2})', line)
        for hex_val in hex_matches:
            hex_values.append(int(hex_val, 16))
    
    print(f"Extracted {len(hex_values)} bytes from {c_file_path}")
    return bytes(hex_values)

def create_lvgl_binary(data, output_path):
    """Create LVGL binary file with proper header"""
    
    # LVGL image header (simplified for RGB565A8 format)
    # Magic number: 0x4C56474C ("LVGL")
    magic = 0x4C56474C
    color_format = 0x0A  # LV_COLOR_FORMAT_RGB565A8
    flags = 0
    width = 128
    height = 128
    stride = 256  # 128 * 2 bytes per pixel
    
    # Pack header (little endian)
    header = struct.pack('<IIIIII', 
        magic,        # magic
        color_format, # color_format
        flags,        # flags
        width,        # w
        height,       # h
        stride        # stride
    )
    
    # Write binary file
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(data)
    
    print(f"Created {output_path} ({len(header + data)} bytes)")

def main():
    if len(sys.argv) != 3:
        print("Usage: python simple_convert.py <input.c> <output.bin>")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    print(f"Converting {input_file} to {output_file}...")
    
    data = extract_binary_data(input_file)
    if data:
        create_lvgl_binary(data, output_file)
        print("Conversion completed successfully!")
    else:
        print("Conversion failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()
