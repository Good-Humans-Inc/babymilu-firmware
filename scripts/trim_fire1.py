#!/usr/bin/env python3
"""
Script to trim all PNG files in the centralized animation head folder
by keeping the rectangular area from (332, 340) to (692, 700)
"""

import os
import sys
import glob

try:
    from PIL import Image
except ImportError:
    print("Error: PIL (Pillow) is required but not installed.")
    print("Please install it with: pip install Pillow")
    sys.exit(1)

def trim_image(image_path, left=332, top=340, right=692, bottom=700):
    """Trim a single image to the specified rectangular area"""
    
    try:
        img = Image.open(image_path)
        original_size = img.size
        
        # Verify image size is 1024x1024
        if original_size != (1024, 1024):
            print(f"  Warning: Image size is {original_size}, expected (1024, 1024)")
        
        # Crop the image
        cropped_img = img.crop((left, top, right, bottom))
        cropped_size = cropped_img.size
        
        # Save the cropped image (overwrite original)
        cropped_img.save(image_path)
        print(f"  ✓ Trimmed {os.path.basename(image_path)}: {original_size} -> {cropped_size}")
        return True
        
    except Exception as e:
        print(f"  ✗ Error processing {os.path.basename(image_path)}: {e}")
        return False

def trim_all_pngs():
    """Trim all PNG files in the centralized animation head folder"""
    
    # Get the script directory and project root
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    
    # Path to the folder
    folder_path = os.path.join(project_root, "centralized animation head")
    
    # Check if the folder exists
    if not os.path.exists(folder_path):
        print(f"Error: Folder not found at {folder_path}")
        sys.exit(1)
    
    # Find all PNG files in the folder
    png_pattern = os.path.join(folder_path, "*.png")
    png_files = glob.glob(png_pattern)
    
    if not png_files:
        print(f"No PNG files found in {folder_path}")
        sys.exit(1)
    
    print(f"Found {len(png_files)} PNG file(s) to process:")
    print(f"Crop coordinates: (332, 340) to (692, 700)")
    print("-" * 60)
    
    # Crop coordinates: (left, top, right, bottom)
    # From (332, 340) to (692, 700)
    left = 332
    top = 340
    right = 692
    bottom = 700
    
    success_count = 0
    for png_file in sorted(png_files):
        if trim_image(png_file, left, top, right, bottom):
            success_count += 1
    
    print("-" * 60)
    print(f"Successfully processed {success_count}/{len(png_files)} file(s)")

if __name__ == "__main__":
    trim_all_pngs()

