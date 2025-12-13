#!/usr/bin/env python3
"""
Resize all PNG images in baby_asset_package directory to a target size.
Usage: python resize_png_assets.py --size Width Height
"""

import os
import sys
import argparse
from PIL import Image

def resize_png_images(directory, target_width, target_height):
    """
    Resize all PNG images in the given directory to the target size.
    
    Args:
        directory: Path to the directory containing PNG files
        target_width: Target width in pixels
        target_height: Target height in pixels
    """
    if not os.path.exists(directory):
        print(f"Error: Directory '{directory}' does not exist")
        sys.exit(1)
    
    if not os.path.isdir(directory):
        print(f"Error: '{directory}' is not a directory")
        sys.exit(1)
    
    # Find all PNG files in the directory
    png_files = []
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.lower().endswith('.png'):
                png_files.append(os.path.join(root, file))
    
    if not png_files:
        print(f"No PNG files found in '{directory}'")
        return
    
    print(f"Found {len(png_files)} PNG file(s) in '{directory}'")
    print(f"Resizing to {target_width}x{target_height}...")
    print()
    
    success_count = 0
    error_count = 0
    
    for png_file in png_files:
        try:
            # Open the image
            with Image.open(png_file) as img:
                original_size = img.size
                original_mode = img.mode
                
                # Resize the image using LANCZOS resampling for high quality
                resized_img = img.resize((target_width, target_height), Image.Resampling.LANCZOS)
                
                # Preserve the original mode (RGBA, RGB, etc.)
                if resized_img.mode != original_mode:
                    resized_img = resized_img.convert(original_mode)
                
                # Save the resized image, overwriting the original
                resized_img.save(png_file, 'PNG')
                
                print(f"✓ {os.path.basename(png_file)}: {original_size[0]}x{original_size[1]} → {target_width}x{target_height}")
                success_count += 1
                
        except Exception as e:
            print(f"✗ Error processing {os.path.basename(png_file)}: {str(e)}")
            error_count += 1
    
    print()
    print(f"Completed: {success_count} successful, {error_count} errors")

def main():
    parser = argparse.ArgumentParser(
        description='Resize all PNG images in baby_asset_package directory to a target size',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='Example: python resize_png_assets.py --size 256 256'
    )
    
    parser.add_argument(
        '--size',
        nargs=2,
        type=int,
        metavar=('WIDTH', 'HEIGHT'),
        required=True,
        help='Target size as width and height in pixels'
    )
    
    parser.add_argument(
        '--dir',
        type=str,
        default='baby_asset_package',
        help='Directory containing PNG files (default: baby_asset_package)'
    )
    
    args = parser.parse_args()
    
    width, height = args.size
    
    if width <= 0 or height <= 0:
        print("Error: Width and height must be positive integers")
        sys.exit(1)
    
    resize_png_images(args.dir, width, height)

if __name__ == "__main__":
    main()
