#!/usr/bin/env python3
"""
Generate C header file with overlay sparse pixel data from pixels_output_filtered.txt
"""

import re
import sys
import os

def parse_overlay_file(input_file, output_header):
    """Parse overlay file and generate C header"""
    
    # Pattern to match: (x, y, (r, g, b))
    pattern = r'\((\d+),\s*(\d+),\s*\((\d+),\s*(\d+),\s*(\d+)\)\)'
    
    pixels = []
    
    with open(input_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
                
            match = re.match(pattern, line)
            if match:
                x = int(match.group(1))
                y = int(match.group(2))
                r = int(match.group(3))
                g = int(match.group(4))
                b = int(match.group(5))
                
                # Convert RGB565: RRRRR GGGGGG BBBBB
                # r: 5 bits (0-31), g: 6 bits (0-63), b: 5 bits (0-31)
                rgb565 = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F)
                pixels.append((x, y, rgb565))
    
    print(f"Parsed {len(pixels)} pixels from {input_file}")
    
    # Generate C header file
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
        
        # Write pixels in chunks for readability
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
    if len(sys.argv) != 3:
        print("Usage: python generate_overlay_header.py <input.txt> <output.h>")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_header = sys.argv[2]
    
    if not os.path.exists(input_file):
        print(f"Error: Input file {input_file} not found")
        sys.exit(1)
    
    parse_overlay_file(input_file, output_header)

