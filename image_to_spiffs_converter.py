#!/usr/bin/env python3
"""
Merged script: Image to SPIFFS Converter
Converts JPG/PNG images to LVGL C arrays and then to SPIFFS .bin files

Usage:
    python image_to_spiffs_converter.py input.jpg output.bin
    python image_to_spiffs_converter.py input.png output.bin
"""

import sys
import os
import re
import struct
import tempfile
from enum import Enum

try:
    from PIL import Image
except ImportError:
    print("Error: PIL (Pillow) is required but not installed.")
    print("Please install it with: pip install Pillow")
    sys.exit(1)

class ColorFormat(Enum):
    RGB565 = 0x12
    RGB565A8 = 0x14
    RGB888 = 0x0F
    ARGB8888 = 0x10
    XRGB8888 = 0x11
    ARGB8565 = 0x13
    L8 = 0x06
    I1 = 0x07
    I2 = 0x08
    I4 = 0x09
    I8 = 0x0A
    A1 = 0x0B
    A2 = 0x0C
    A4 = 0x0D
    A8 = 0x0E

class LVGLImage:
    def __init__(self):
        self.width = 0
        self.height = 0
        self.color_format = ColorFormat.RGB565
        self.data = b''
        self.stride = 0
        
    def from_image(self, image_path, target_size=(256, 256)):
        """Load image and convert to LVGL format with automatic color detection"""
        with Image.open(image_path) as img:
            # Resize to target size (256x256)
            img = img.resize(target_size, Image.Resampling.LANCZOS)
            self.width, self.height = img.size
            
            # Auto-detect color format based on image mode
            has_alpha = img.mode in ('RGBA', 'LA') or 'transparency' in img.info
            
            if has_alpha:
                img = img.convert('RGBA')
                self.color_format = ColorFormat.RGB565A8
                self.stride = self.width * 3  # 2 bytes RGB + 1 byte alpha
                self.data = self._convert_to_rgb565a8(img)
            else:
                img = img.convert('RGB')
                self.color_format = ColorFormat.RGB565
                self.stride = self.width * 2  # 2 bytes per pixel
                self.data = self._convert_to_rgb565(img)
            
        return self
    
    def _convert_to_rgb565(self, img):
        """Convert PIL image to RGB565 format"""
        data = []
        for y in range(self.height):
            for x in range(self.width):
                r, g, b = img.getpixel((x, y))
                # Convert to RGB565
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                data.extend([rgb565 & 0xFF, (rgb565 >> 8) & 0xFF])
        return bytes(data)
    
    def _convert_to_rgb565a8(self, img):
        """Convert PIL image to RGB565A8 format"""
        data = []
        for y in range(self.height):
            for x in range(self.width):
                r, g, b, a = img.getpixel((x, y))
                # Convert to RGB565
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                data.extend([rgb565 & 0xFF, (rgb565 >> 8) & 0xFF, a])
        return bytes(data)
    
    def to_c_array(self, output_path):
        """Generate C array file"""
        array_name = os.path.splitext(os.path.basename(output_path))[0] + "_map"
        
        with open(output_path, 'w') as f:
            f.write(f"#include \"lvgl.h\"\n\n")
            f.write(f"const uint8_t {array_name}[] = {{\n")
            
            # Write data in hex format
            for i in range(0, len(self.data), 16):
                line_data = self.data[i:i+16]
                hex_values = [f"0x{b:02X}" for b in line_data]
                f.write("    " + ", ".join(hex_values))
                if i + 16 < len(self.data):
                    f.write(",")
                f.write("\n")
            
            f.write("};\n\n")
            
            # Write image descriptor
            f.write(f"const lv_image_dsc_t {os.path.splitext(os.path.basename(output_path))[0]} = {{\n")
            f.write(f"    .header.magic = LV_IMAGE_HEADER_MAGIC,\n")
            f.write(f"    .header.cf = LV_COLOR_FORMAT_{self.color_format.name},\n")
            f.write(f"    .header.flags = 0,\n")
            f.write(f"    .header.w = {self.width},\n")
            f.write(f"    .header.h = {self.height},\n")
            f.write(f"    .header.stride = {self.stride},\n")
            f.write(f"    .data_size = sizeof({array_name}),\n")
            f.write(f"    .data = {array_name}\n")
            f.write("};\n")

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
    # These values match LVGL's color format definitions
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
        print("Usage: python image_to_spiffs_converter.py <input_image> <output.bin>")
        print("Supported formats: JPG, PNG")
        print("Output: 256x256 resolution, automatic color detection (RGB565/RGB565A8), no RLE")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    # Check if input file exists
    if not os.path.exists(input_file):
        print(f"Error: Input file '{input_file}' not found")
        sys.exit(1)
    
    try:
        print(f"Converting {input_file} to {output_file}...")
        
        # Step 1: Convert image to LVGL C array
        print("Step 1: Converting image to 256x256 LVGL C array...")
        lvgl_img = LVGLImage().from_image(input_file, target_size=(256, 256))
        
        # Create temporary C file
        temp_c_file = tempfile.NamedTemporaryFile(suffix=".c", delete=False)
        temp_c_path = temp_c_file.name
        temp_c_file.close()
        
        # Generate C array
        lvgl_img.to_c_array(temp_c_path)
        print(f"Generated temporary C file: {temp_c_path}")
        print(f"Image size: {lvgl_img.width}x{lvgl_img.height}")
        print(f"Color format: {lvgl_img.color_format.name}")
        print(f"Data size: {len(lvgl_img.data)} bytes")
        
        # Step 2: Convert C array to SPIFFS binary
        print("Step 2: Converting C array to SPIFFS binary...")
        animation_data = extract_animation_data(temp_c_path)
        create_spiffs_binary(animation_data, output_file)
        
        # Clean up temporary file
        os.unlink(temp_c_path)
        
        print("Conversion completed successfully!")
        
    except Exception as e:
        print(f"Error: {e}")
        # Clean up temporary file if it exists
        if 'temp_c_path' in locals() and os.path.exists(temp_c_path):
            os.unlink(temp_c_path)
        sys.exit(1)

if __name__ == "__main__":
    main()
