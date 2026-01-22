#!/usr/bin/env python3
"""
Convert PNG to 50x50 LVGL C array for echoear
"""

import sys
import os
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

class LVGLImage:
    def __init__(self):
        self.width = 0
        self.height = 0
        self.color_format = ColorFormat.RGB565
        self.data = b''
        self.stride = 0
        
    def from_image(self, image_path, target_size=(50, 50)):
        """Load image and convert to LVGL format with automatic color detection"""
        with Image.open(image_path) as img:
            # Resize to target size (50x50)
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
            f.write(f"#ifndef LV_ATTRIBUTE_MEM_ALIGN\n")
            f.write(f"#define LV_ATTRIBUTE_MEM_ALIGN\n")
            f.write(f"#endif\n\n")
            f.write(f"#ifndef LV_ATTRIBUTE_IMAGE_{array_name.upper()}\n")
            f.write(f"#define LV_ATTRIBUTE_IMAGE_{array_name.upper()}\n")
            f.write(f"#endif\n\n")
            f.write(f"static const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMAGE_{array_name.upper()}\n")
            f.write(f"uint8_t {array_name}[] = {{\n")
            
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
            descriptor_name = os.path.splitext(os.path.basename(output_path))[0]
            f.write(f"const lv_image_dsc_t {descriptor_name} = {{\n")
            f.write(f"    .header.magic = LV_IMAGE_HEADER_MAGIC,\n")
            f.write(f"    .header.cf = LV_COLOR_FORMAT_{self.color_format.name},\n")
            f.write(f"    .header.flags = 0,\n")
            f.write(f"    .header.w = {self.width},\n")
            f.write(f"    .header.h = {self.height},\n")
            f.write(f"    .header.stride = {self.stride},\n")
            f.write(f"    .data_size = sizeof({array_name}),\n")
            f.write(f"    .data = {array_name}\n")
            f.write("};\n")

def main():
    if len(sys.argv) != 3:
        print("Usage: python convert_image_50x50.py <input_image> <output.c>")
        print("Supported formats: JPG, PNG")
        print("Output: 50x50 resolution, automatic color detection (RGB565/RGB565A8)")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    # Check if input file exists
    if not os.path.exists(input_file):
        print(f"Error: Input file '{input_file}' not found")
        sys.exit(1)
    
    try:
        print(f"Converting {input_file} to {output_file}...")
        print("Target size: 50x50 pixels")
        
        # Convert image to LVGL C array
        lvgl_img = LVGLImage().from_image(input_file, target_size=(50, 50))
        lvgl_img.to_c_array(output_file)
        
        print(f"Generated C file: {output_file}")
        print(f"Image size: {lvgl_img.width}x{lvgl_img.height}")
        print(f"Color format: {lvgl_img.color_format.name}")
        print(f"Data size: {len(lvgl_img.data)} bytes")
        print("Conversion completed successfully!")
        
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()
