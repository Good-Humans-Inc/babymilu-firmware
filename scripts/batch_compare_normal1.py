#!/usr/bin/env python3
"""
Batch compare normal1.png with all other PNG files in centralized_animation/ directory.
Calls generate_diff_overlay.py for each comparison to generate overlay header files.
"""

import sys
import os
import glob
import subprocess

def find_png_files(directory):
    """Find all PNG files in the specified directory"""
    png_files = []
    patterns = ['*.png', '*.PNG']
    
    for pattern in patterns:
        png_files.extend(glob.glob(os.path.join(directory, pattern)))
    
    # Sort files for consistent ordering
    return sorted(png_files)

def main():  
    # Get the script directory and project root
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    
    # Paths
    animation_dir = os.path.join(project_root, 'centralized_animation')
    diff_dir = os.path.join(project_root, 'images', 'frame difference')
    generate_script = os.path.join(diff_dir, 'generate_diff_overlay.py')
    normal1_path = os.path.join(animation_dir, 'normal1.png')
    
    # Check if generate_diff_overlay.py exists
    if not os.path.exists(generate_script):
        print(f"Error: generate_diff_overlay.py not found at {generate_script}")
        sys.exit(1)
    
    # Check if normal1.png exists
    if not os.path.exists(normal1_path):
        print(f"Error: normal1.png not found at {normal1_path}")
        sys.exit(1)
    
    # Find all PNG files in centralized_animation directory
    all_pngs = find_png_files(animation_dir)
    
    # Filter out normal1.png
    png_files = [f for f in all_pngs if os.path.basename(f).lower() != 'normal1.png']
    
    if not png_files:
        print(f"No PNG files found (except normal1.png) in {animation_dir}")
        sys.exit(0)
    
    print(f"Found {len(png_files)} PNG files to compare with normal1.png:")
    for png_file in png_files:
        print(f"  - {os.path.basename(png_file)}")
    print()
    
    # Process each PNG file
    success_count = 0
    error_count = 0
    
    for png_file in png_files:
        # Generate output header filename
        basename = os.path.basename(png_file)
        name_without_ext = os.path.splitext(basename)[0]
        output_header = os.path.join(diff_dir, f"{name_without_ext}_overlay.h")
        
        print(f"Processing: {basename}")
        print(f"  Comparing normal1.png -> {basename}")
        print(f"  Output: {os.path.basename(output_header)}")
        
        # Call generate_diff_overlay.py
        # Usage: python generate_diff_overlay.py <image1> <image2> <output.h>
        cmd = [
            sys.executable,
            generate_script,
            normal1_path,
            png_file,
            output_header
        ]
        
        try:
            result = subprocess.run(
                cmd,
                cwd=diff_dir,
                capture_output=True,
                text=True,
                check=True
            )
            print(f"  ✓ Success")
            if result.stdout:
                # Print the last line of output (usually the pixel count)
                lines = result.stdout.strip().split('\n')
                if lines:
                    print(f"  {lines[-1]}")
            success_count += 1
        except subprocess.CalledProcessError as e:
            print(f"  ✗ Error: {e}")
            if e.stdout:
                print(f"  stdout: {e.stdout}")
            if e.stderr:
                print(f"  stderr: {e.stderr}")
            error_count += 1
        
        print()
    
    # Summary
    print("=" * 60)
    print(f"Summary:")
    print(f"  Success: {success_count}")
    print(f"  Errors: {error_count}")
    print(f"  Total: {len(png_files)}")
    print("=" * 60)
    
    if error_count > 0:
        sys.exit(1)

if __name__ == "__main__":
    main()

