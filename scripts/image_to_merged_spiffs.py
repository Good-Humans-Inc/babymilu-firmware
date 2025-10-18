#!/usr/bin/env python3
"""
Enhanced Image to SPIFFS Converter - Direct to Merged Binary
Converts multiple JPG/PNG images directly to a single merged SPIFFS .bin file

This script eliminates the need for intermediate individual .bin files and creates
the merged file directly from source images.

Usage:
    python image_to_merged_spiffs.py input_dir/ output_merged.bin
    python image_to_merged_spiffs.py input_dir/ output_merged.bin --size 256 256
    python image_to_merged_spiffs.py input_dir/ output_merged.bin --format RGB565
"""

import sys
import os
import glob
import struct
import argparse
from pathlib import Path
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
        
    def from_image(self, image_path, target_size=(256, 256), force_format=None):
        """Load image and convert to LVGL format"""
        with Image.open(image_path) as img:
            # Resize to target size
            img = img.resize(target_size, Image.Resampling.LANCZOS)
            self.width, self.height = img.size
            
            # Determine color format
            if force_format:
                self.color_format = force_format
            else:
                # Auto-detect color format based on image mode
                has_alpha = img.mode in ('RGBA', 'LA') or 'transparency' in img.info
                self.color_format = ColorFormat.RGB565A8 if has_alpha else ColorFormat.RGB565
            
            # Convert image based on color format
            if self.color_format == ColorFormat.RGB565A8:
                img = img.convert('RGBA')
                self.stride = self.width * 3  # 2 bytes RGB + 1 byte alpha
                self.data = self._convert_to_rgb565a8(img)
            else:
                img = img.convert('RGB')
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
    
    def to_binary_frame(self):
        """Convert to binary frame data (header + pixel data)"""
        # LVGL magic number
        magic = 0x4C56474C  # "LVGL" in little endian
        
        # Pack header
        header = struct.pack('<IIIIII', 
            magic,                    # magic
            self.color_format.value,  # color_format
            0,                        # flags
            self.width,               # w
            self.height,              # h
            self.stride               # stride
        )
        
        return header + self.data

def find_image_files(input_dir):
    """Find all supported image files in directory, sorted by name"""
    supported_extensions = ['*.jpg', '*.jpeg', '*.png', '*.bmp', '*.gif']
    image_files = []
    
    for ext in supported_extensions:
        # Search for both lowercase and uppercase extensions
        lowercase_files = glob.glob(os.path.join(input_dir, ext))
        uppercase_files = glob.glob(os.path.join(input_dir, ext.upper()))
        
        # Add files and avoid duplicates
        for file_path in lowercase_files + uppercase_files:
            if file_path not in image_files:
                image_files.append(file_path)
    
    # Sort files by name to ensure consistent order
    image_files.sort()
    
    return image_files

def convert_images_to_merged_binary(input_dir, output_file, target_size=(256, 256), force_format=None):
    """Convert multiple images to a single merged binary file"""
    
    print(f"=== Converting Images from {input_dir} to {output_file} ===")
    
    # Find image files
    image_files = find_image_files(input_dir)
    
    if not image_files:
        print(f"Error: No supported image files found in {input_dir}")
        print("Supported formats: JPG, JPEG, PNG, BMP, GIF")
        return False
    
    print(f"Found {len(image_files)} image files:")
    for i, file_path in enumerate(image_files, 1):
        print(f"  {i}. {os.path.basename(file_path)}")
    
    # Create output directory if needed
    output_dir = os.path.dirname(output_file)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # Process each image
    frames = []
    total_size = 0
    
    for i, image_file in enumerate(image_files, 1):
        print(f"\nProcessing frame {i}: {os.path.basename(image_file)}")
        
        try:
            # Convert image to LVGL format
            lvgl_img = LVGLImage().from_image(image_file, target_size, force_format)
            
            # Convert to binary frame
            frame_data = lvgl_img.to_binary_frame()
            frames.append(frame_data)
            
            print(f"  ✅ Size: {lvgl_img.width}x{lvgl_img.height}")
            print(f"  ✅ Color format: {lvgl_img.color_format.name}")
            print(f"  ✅ Frame size: {len(frame_data)} bytes")
            
            total_size += len(frame_data)
            
        except Exception as e:
            print(f"  ❌ Error processing {image_file}: {e}")
            return False
    
    # Write merged binary file
    print(f"\nWriting merged binary file...")
    
    try:
        with open(output_file, 'wb') as f:
            for i, frame_data in enumerate(frames, 1):
                print(f"Writing frame {i} ({len(frame_data)} bytes)...")
                f.write(frame_data)
        
        # Verify output
        output_size = os.path.getsize(output_file)
        print(f"\n✅ Successfully created merged file: {output_file}")
        print(f"✅ Output file size: {output_size} bytes")
        print(f"✅ Total frames: {len(frames)}")
        print(f"✅ Average frame size: {output_size // len(frames)} bytes")
        
        if output_size == total_size:
            print("✅ File size matches expected total")
        else:
            print(f"⚠️  File size mismatch: expected {total_size}, got {output_size}")
        
        return True
        
    except Exception as e:
        print(f"❌ Error writing output file: {e}")
        return False

def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(
        description="Convert multiple images to a single merged SPIFFS binary file",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python image_to_merged_spiffs.py images/ output.bin
  python image_to_merged_spiffs.py images/ output.bin --size 128 128
  python image_to_merged_spiffs.py images/ output.bin --format RGB565A8
  python image_to_merged_spiffs.py images/ output.bin --size 256 256 --format RGB565
        """
    )
    
    parser.add_argument('input_dir', help='Input directory containing image files')
    parser.add_argument('output_file', help='Output merged binary file path')
    parser.add_argument('--size', nargs=2, type=int, default=[256, 256], 
                       metavar=('WIDTH', 'HEIGHT'),
                       help='Target image size (default: 256 256)')
    parser.add_argument('--format', choices=['RGB565', 'RGB565A8', 'RGB888', 'ARGB8888'], 
                       help='Force color format (auto-detect if not specified)')
    
    return parser.parse_args()

def main():
    """Main function"""
    args = parse_arguments()
    
    # Validate input directory
    if not os.path.isdir(args.input_dir):
        print(f"Error: Input directory does not exist: {args.input_dir}")
        sys.exit(1)
    
    # Validate output file extension
    if not args.output_file.endswith('.bin'):
        print("Warning: Output file should have .bin extension")
    
    # Parse color format if specified
    force_format = None
    if args.format:
        force_format = ColorFormat[args.format]
        print(f"Using forced color format: {args.format}")
    else:
        print("Using automatic color format detection")
    
    # Convert images
    success = convert_images_to_merged_binary(
        args.input_dir, 
        args.output_file, 
        target_size=tuple(args.size),
        force_format=force_format
    )
    
    if success:
        print("\n🎉 Image conversion completed successfully!")
        print(f"Ready to upload {args.output_file} to your device's SPIFFS partition.")
    else:
        print("\n❌ Image conversion failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()
