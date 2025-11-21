#!/usr/bin/env python3
"""
Generate C header file with overlay sparse pixel data from the difference between two images.
Finds pixels that differ between image1 and image2, and uses the color from image2.
"""

import sys
import os
from PIL import Image

def rgb_to_rgb565(r, g, b):
    """Convert 8-bit RGB to RGB565 format"""
    # Standard RGB565 conversion: ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def pixels_differ(r1, g1, b1, r2, g2, b2, threshold=0):
    """
    Check if two pixels are different.
    threshold: maximum difference per channel to be considered the same (default: 0 = exact match)
    """
    return (abs(r1 - r2) > threshold or 
            abs(g1 - g2) > threshold or 
            abs(b1 - b2) > threshold)

def extract_difference_pixels(image1_path, image2_path, diff_threshold=0):
    """
    Extract pixels that differ between image1 and image2.
    Returns pixels from image2 that are different from image1.
    """
    pixels = []
    
    with Image.open(image1_path) as img1, Image.open(image2_path) as img2:
        # Convert to RGB if needed
        if img1.mode != 'RGB':
            img1 = img1.convert('RGB')
        if img2.mode != 'RGB':
            img2 = img2.convert('RGB')
        
        width1, height1 = img1.size
        width2, height2 = img2.size
        
        # Use the smaller dimensions if images differ in size
        width = min(width1, width2)
        height = min(height1, height2)
        
        print(f"Processing images:")
        print(f"  Image1: {width1}x{height1}, mode: {img1.mode}")
        print(f"  Image2: {width2}x{height2}, mode: {img2.mode}")
        print(f"  Comparing: {width}x{height} pixels (diff threshold: {diff_threshold})")
        
        # Iterate through all pixels
        for y in range(height):
            for x in range(width):
                r1, g1, b1 = img1.getpixel((x, y))
                r2, g2, b2 = img2.getpixel((x, y))
                
                # If pixels differ, include the pixel from image2
                if pixels_differ(r1, g1, b1, r2, g2, b2, diff_threshold):
                    rgb565 = rgb_to_rgb565(r2, g2, b2)
                    pixels.append((x, y, rgb565))
    
    return pixels

def generate_overlay_header(pixels, output_header, header_guard_name="EMBARRASS_OVERLAY_H"):
    """Generate C header file with overlay pixel data"""
    
    with open(output_header, 'w') as f:
        f.write("// Auto-generated overlay pixel data from frame difference\n")
        f.write("// Format: {x, y, rgb565_color}\n")
        f.write(f"// Total pixels: {len(pixels)}\n\n")
        f.write(f"#ifndef {header_guard_name}\n")
        f.write(f"#define {header_guard_name}\n\n")
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
        f.write(f"#endif // {header_guard_name}\n")
    
    print(f"Generated {output_header} with {len(pixels)} pixels")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python generate_diff_overlay.py <image1> <image2> <output.h> [diff_threshold]")
        print("  diff_threshold: maximum difference per channel to be considered the same (default: 0 = exact match)")
        sys.exit(1)
    
    image1_path = sys.argv[1]
    image2_path = sys.argv[2]
    output_header = sys.argv[3]
    diff_threshold = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    
    if not os.path.exists(image1_path):
        print(f"Error: Input image1 {image1_path} not found")
        sys.exit(1)
    
    if not os.path.exists(image2_path):
        print(f"Error: Input image2 {image2_path} not found")
        sys.exit(1)
    
    print(f"Finding pixel differences from {image1_path} to {image2_path} (diff threshold: {diff_threshold})")
    pixels = extract_difference_pixels(image1_path, image2_path, diff_threshold)
    print(f"Found {len(pixels)} different pixels")
    
    # Generate header guard name from output filename
    header_guard = output_header.upper().replace('/', '_').replace('.', '_').replace('-', '_')
    if not header_guard.endswith('_H'):
        header_guard += '_H'
    
    generate_overlay_header(pixels, output_header, header_guard)

