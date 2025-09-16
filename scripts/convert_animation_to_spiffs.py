#!/usr/bin/env python3
"""
Convert LVGL animation C files to SPIFFS binary format

This script extracts the lv_image_dsc_t data from existing animation C files
and creates binary files suitable for SPIFFS storage.

Usage:
    python convert_animation_to_spiffs.py normal1.c normal1.bin
"""

import sys
import re
import struct

def extract_animation_data(c_file_path):
    """Extract lv_image_dsc_t data from a C file"""
    
    with open(c_file_path, 'r') as f:
        content = f.read()
    
    # Extract header information
    header_match = re.search(r'\.header\.magic = (LV_IMAGE_HEADER_MAGIC)', content)
    cf_match = re.search(r'\.header\.cf = (LV_COLOR_FORMAT_\w+)', content)
    flags_match = re.search(r'\.header\.flags = (\d+)', content)
    w_match = re.search(r'\.header\.w = (\d+)', content)
    h_match = re.search(r'\.header\.h = (\d+)', content)
    stride_match = re.search(r'\.header\.stride = (\d+)', content)
    data_size_match = re.search(r'\.data_size = sizeof\((\w+_map)\)', content)
    
    if not all([header_match, cf_match, flags_match, w_match, h_match, stride_match, data_size_match]):
        raise ValueError("Could not extract header information from C file")
    
    # Extract pixel data array
    array_name = data_size_match.group(1)
    array_pattern = rf'uint8_t {array_name}\[\] = \{{(.*?)\}};'
    array_match = re.search(array_pattern, content, re.DOTALL)
    
    if not array_match:
        raise ValueError(f"Could not find pixel data array {array_name}")
    
    # Parse hex values from the array
    hex_values = []
    for line in array_match.group(1).split('\n'):
        # Extract hex values from each line
        hex_matches = re.findall(r'0x([0-9a-fA-F]{2})', line)
        for hex_val in hex_matches:
            hex_values.append(int(hex_val, 16))
    
    return {
        'width': int(w_match.group(1)),
        'height': int(h_match.group(1)),
        'stride': int(stride_match.group(1)),
        'flags': int(flags_match.group(1)),
        'color_format': cf_match.group(1),
        'data': bytes(hex_values)
    }

def create_spiffs_binary(animation_data, output_path):
    """Create SPIFFS binary file from animation data"""
    
    # LVGL image header structure (simplified)
    # This matches the lv_image_header_t structure
    magic = 0x4C56474C  # "LVGL" in little endian
    cf_value = get_color_format_value(animation_data['color_format'])
    
    # Pack header
    header = struct.pack('<IIIIII', 
        magic,                    # magic
        cf_value,                 # color_format
        animation_data['flags'],  # flags
        animation_data['width'],  # w
        animation_data['height'], # h
        animation_data['stride']  # stride
    )
    
    # Write binary file
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(animation_data['data'])
    
    print(f"Created {output_path} ({len(header + animation_data['data'])} bytes)")

def get_color_format_value(cf_name):
    """Convert color format name to numeric value"""
    # These values match LVGL's color format definitions from LVGLImage.py
    cf_map = {
        'LV_COLOR_FORMAT_RGB565': 0x12,     # RGB565
        'LV_COLOR_FORMAT_RGB565A8': 0x14,   # RGB565A8
        'LV_COLOR_FORMAT_RGB888': 0x0F,     # RGB888
        'LV_COLOR_FORMAT_ARGB8888': 0x10,   # ARGB8888
        'LV_COLOR_FORMAT_XRGB8888': 0x11,   # XRGB8888
        'LV_COLOR_FORMAT_ARGB8565': 0x13,   # ARGB8565
        'LV_COLOR_FORMAT_L8': 0x06,         # L8
        'LV_COLOR_FORMAT_I1': 0x07,         # I1
        'LV_COLOR_FORMAT_I2': 0x08,         # I2
        'LV_COLOR_FORMAT_I4': 0x09,         # I4
        'LV_COLOR_FORMAT_I8': 0x0A,         # I8
        'LV_COLOR_FORMAT_A1': 0x0B,         # A1
        'LV_COLOR_FORMAT_A2': 0x0C,         # A2
        'LV_COLOR_FORMAT_A4': 0x0D,         # A4
        'LV_COLOR_FORMAT_A8': 0x0E,         # A8
    }
    return cf_map.get(cf_name, 0x12)  # Default to RGB565

def main():
    if len(sys.argv) != 3:
        print("Usage: python convert_animation_to_spiffs.py <input.c> <output.bin>")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    try:
        print(f"Converting {input_file} to {output_file}...")
        animation_data = extract_animation_data(input_file)
        create_spiffs_binary(animation_data, output_file)
        print("Conversion completed successfully!")
        
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
