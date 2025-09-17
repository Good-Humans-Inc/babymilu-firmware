#!/usr/bin/env python3
"""
Convert actual image files to animation format for device upload
Supports PNG, JPG, and other common formats
"""

import struct
import sys
import os
from PIL import Image
import argparse

def convert_image_to_animation(input_file, output_file, width=None, height=None, frames=1):
    """
    Convert an image file to the custom LVGL animation format
    """
    
    try:
        # Open image
        img = Image.open(input_file)
        print(f"Original image: {img.size[0]}x{img.size[1]}, mode: {img.mode}")
        
        # Convert to RGB if needed
        if img.mode != 'RGB':
            img = img.convert('RGB')
        
        # Resize if specified
        if width and height:
            img = img.resize((width, height), Image.Resampling.LANCZOS)
            print(f"Resized to: {width}x{height}")
        
        # Convert to RGB565 format
        width, height = img.size
        
        # Create header
        magic = 0x4C56474C  # "LVGL" in little endian
        color_format = 0x0B  # RGB565
        flags = 0x00
        stride = width * 2  # 2 bytes per pixel for RGB565
        
        header = struct.pack('<IIIIII', magic, color_format, flags, width, height, stride)
        
        # Convert image data to RGB565
        frame_data = bytearray()
        
        for frame in range(frames):
            # For multiple frames, we'll just repeat the same image
            # In a real animation, you'd load different frames
            pixels = img.getdata()
            
            for pixel in pixels:
                r, g, b = pixel
                # Convert RGB888 to RGB565
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                frame_data.extend(struct.pack('<H', rgb565))
        
        # Write output file
        with open(output_file, 'wb') as f:
            f.write(header)
            f.write(frame_data)
        
        print(f"✅ Converted: {input_file} → {output_file}")
        print(f"   Size: {len(header) + len(frame_data)} bytes")
        print(f"   Dimensions: {width}x{height}")
        print(f"   Frames: {frames}")
        print(f"   Color format: RGB565")
        
        return True
        
    except Exception as e:
        print(f"❌ Conversion failed: {e}")
        return False

def create_animation_from_images(image_files, output_file, width=None, height=None):
    """
    Create animation from multiple image files (one per frame)
    """
    
    if not image_files:
        print("❌ No image files provided")
        return False
    
    try:
        # Load first image to get dimensions
        first_img = Image.open(image_files[0])
        if first_img.mode != 'RGB':
            first_img = first_img.convert('RGB')
        
        if width and height:
            first_img = first_img.resize((width, height), Image.Resampling.LANCZOS)
        else:
            width, height = first_img.size
        
        # Create header
        magic = 0x4C56474C
        color_format = 0x0B
        flags = 0x00
        stride = width * 2
        
        header = struct.pack('<IIIIII', magic, color_format, flags, width, height, stride)
        
        # Process all frames
        frame_data = bytearray()
        
        for i, image_file in enumerate(image_files):
            print(f"Processing frame {i+1}/{len(image_files)}: {image_file}")
            
            img = Image.open(image_file)
            if img.mode != 'RGB':
                img = img.convert('RGB')
            
            if width and height:
                img = img.resize((width, height), Image.Resampling.LANCZOS)
            
            pixels = img.getdata()
            for pixel in pixels:
                r, g, b = pixel
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                frame_data.extend(struct.pack('<H', rgb565))
        
        # Write output file
        with open(output_file, 'wb') as f:
            f.write(header)
            f.write(frame_data)
        
        print(f"✅ Created animation: {output_file}")
        print(f"   Size: {len(header) + len(frame_data)} bytes")
        print(f"   Dimensions: {width}x{height}")
        print(f"   Frames: {len(image_files)}")
        
        return True
        
    except Exception as e:
        print(f"❌ Animation creation failed: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Convert images to animation format')
    parser.add_argument('input', help='Input image file or directory')
    parser.add_argument('output', help='Output animation file')
    parser.add_argument('--width', type=int, help='Resize width')
    parser.add_argument('--height', type=int, help='Resize height')
    parser.add_argument('--frames', type=int, default=1, help='Number of frames (for single image)')
    parser.add_argument('--multi', action='store_true', help='Create animation from multiple images')
    
    args = parser.parse_args()
    
    if args.multi:
        # Multiple images mode
        if os.path.isdir(args.input):
            image_files = []
            for ext in ['*.png', '*.jpg', '*.jpeg', '*.bmp', '*.gif']:
                import glob
                image_files.extend(glob.glob(os.path.join(args.input, ext)))
            image_files.sort()
        else:
            # Assume comma-separated list of files
            image_files = [f.strip() for f in args.input.split(',')]
        
        if not image_files:
            print("❌ No image files found")
            return 1
        
        success = create_animation_from_images(image_files, args.output, args.width, args.height)
    else:
        # Single image mode
        if not os.path.exists(args.input):
            print(f"❌ Input file not found: {args.input}")
            return 1
        
        success = convert_image_to_animation(args.input, args.output, args.width, args.height, args.frames)
    
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
