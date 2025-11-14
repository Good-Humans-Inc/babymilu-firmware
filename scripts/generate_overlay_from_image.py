#!/usr/bin/env python3
"""
Generate C header file with overlay sparse pixel data from an image.
Extracts all pixels that are NOT white (0xFFFFFF) and converts them to RGB565 format.
"""

import sys
import os
from PIL import Image

def rgb_to_rgb565(r, g, b):
    """Convert 8-bit RGB to RGB565 format"""
    # Standard RGB565 conversion: ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def is_white(r, g, b, threshold=5):
    """
    Check if a pixel is white (or very close to white).
    threshold: maximum difference from 255 for each channel to be considered white
    """
    return (r >= 255 - threshold and g >= 255 - threshold and b >= 255 - threshold)

def extract_non_white_pixels(image_path, white_threshold=5):
    """Extract all non-white pixels from an image"""
    pixels = []
    
    with Image.open(image_path) as img:
        # Convert to RGB if needed
        if img.mode != 'RGB':
            img = img.convert('RGB')
        
        width, height = img.size
        print(f"Processing image: {width}x{height}, mode: {img.mode}")
        
        # Iterate through all pixels
        for y in range(height):
            for x in range(width):
                r, g, b = img.getpixel((x, y))
                
                # Skip white pixels
                if not is_white(r, g, b, white_threshold):
                    rgb565 = rgb_to_rgb565(r, g, b)
                    pixels.append((x, y, rgb565))
    
    return pixels

def generate_overlay_header(pixels, output_header):
    """Generate C header file with overlay pixel data"""
    
    with open(output_header, 'w') as f:
        f.write("// Auto-generated overlay pixel data\n")
        f.write("// Format: {x, y, rgb565_color}\n")
        f.write(f"// Total pixels: {len(pixels)}\n\n")
        f.write("#ifndef OVERLAY_PIXELS_H\n")
        f.write("#define OVERLAY_PIXELS_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("// Overlay pixel structure\n")
        f.write("typedef struct {\n")
        f.write("    uint16_t x;\n")
        f.write("    uint16_t y;\n")
        f.write("    uint16_t color;  // RGB565 format\n")
        f.write("} overlay_pixel_t;\n\n")
        f.write(f"// Overlay pixel data ({len(pixels)} pixels)\n")
        f.write("static const overlay_pixel_t overlay_pixels[] = {\n")
        
        # Write pixels
        for i, (x, y, color) in enumerate(pixels):
            if i == len(pixels) - 1:
                f.write(f"    {{{x}, {y}, 0x{color:04X}}}\n")
            else:
                f.write(f"    {{{x}, {y}, 0x{color:04X}}},\n")
        
        f.write("};\n\n")
        f.write(f"#define OVERLAY_PIXEL_COUNT {len(pixels)}\n\n")
        f.write("#endif // OVERLAY_PIXELS_H\n")
    
    print(f"Generated {output_header} with {len(pixels)} pixels")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python generate_overlay_from_image.py <input_image> <output.h> [white_threshold]")
        print("  white_threshold: maximum difference from 255 for each channel to be considered white (default: 5)")
        sys.exit(1)
    
    input_image = sys.argv[1]
    output_header = sys.argv[2]
    white_threshold = int(sys.argv[3]) if len(sys.argv) > 3 else 5
    
    if not os.path.exists(input_image):
        print(f"Error: Input image {input_image} not found")
        sys.exit(1)
    
    print(f"Extracting non-white pixels from {input_image} (white threshold: {white_threshold})")
    pixels = extract_non_white_pixels(input_image, white_threshold)
    print(f"Found {len(pixels)} non-white pixels")
    
    generate_overlay_header(pixels, output_header)

