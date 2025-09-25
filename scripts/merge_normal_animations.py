#!/usr/bin/env python3
"""
Script to merge normal1.bin, normal2.bin, and normal3.bin into a single normal_all.bin file.

This script reads individual animation files and concatenates them into a single file
that can be loaded more efficiently by the firmware.

Usage:
    python merge_normal_animations.py [input_directory] [output_file]

Example:
    python merge_normal_animations.py animations/ animations/normal_all.bin
"""

import sys
import os
import struct
from pathlib import Path

def read_animation_file(filepath):
    """
    Read an animation file and return its header and data.
    
    Returns:
        tuple: (header_data, pixel_data) where header_data is 6 uint32_t values
    """
    print(f"Reading {filepath}...")
    
    if not os.path.exists(filepath):
        raise FileNotFoundError(f"File not found: {filepath}")
    
    with open(filepath, 'rb') as f:
        # Read header (6 uint32_t values)
        header_bytes = f.read(6 * 4)  # 6 * 4 bytes = 24 bytes
        if len(header_bytes) != 24:
            raise ValueError(f"Invalid header size in {filepath}: {len(header_bytes)} bytes")
        
        # Unpack header data
        header_data = struct.unpack('<IIIIII', header_bytes)  # Little endian, 6 uint32_t
        
        # Validate magic number (0x4C56474C = "LVGL" in little endian)
        if header_data[0] != 0x4C56474C:
            raise ValueError(f"Invalid magic number in {filepath}: 0x{header_data[0]:08x} (expected 0x4C56474C)")
        
        # Read remaining pixel data
        pixel_data = f.read()
        
        # Calculate expected data size
        width = header_data[3]
        height = header_data[4]
        stride = header_data[5]
        expected_size = height * stride
        
        if len(pixel_data) != expected_size:
            print(f"Warning: {filepath} - Expected {expected_size} bytes, got {len(pixel_data)} bytes")
        
        print(f"  Magic: 0x{header_data[0]:08x}")
        print(f"  Color format: {header_data[1]}")
        print(f"  Flags: {header_data[2]}")
        print(f"  Dimensions: {width}x{height}")
        print(f"  Stride: {stride}")
        print(f"  Data size: {len(pixel_data)} bytes")
        
        return header_data, pixel_data

def merge_normal_animations(input_dir, output_file):
    """
    Merge normal1.bin, normal2.bin, and normal3.bin into normal_all.bin
    """
    print("=== Merging Normal Animation Files ===")
    
    # Define input files
    input_files = [
        os.path.join(input_dir, "normal1.bin"),
        os.path.join(input_dir, "normal2.bin"),
        os.path.join(input_dir, "normal3.bin")
    ]
    
    # Check if all input files exist
    missing_files = []
    for filepath in input_files:
        if not os.path.exists(filepath):
            missing_files.append(filepath)
    
    if missing_files:
        print("Error: Missing input files:")
        for filepath in missing_files:
            print(f"  - {filepath}")
        return False
    
    # Read all animation files
    frames = []
    total_size = 0
    
    for filepath in input_files:
        try:
            header_data, pixel_data = read_animation_file(filepath)
            frames.append((header_data, pixel_data))
            total_size += len(header_data) * 4 + len(pixel_data)  # 4 bytes per uint32_t
            print(f"  ✅ Successfully read {filepath}")
        except Exception as e:
            print(f"  ❌ Error reading {filepath}: {e}")
            return False
    
    print(f"\nTotal frames to merge: {len(frames)}")
    print(f"Estimated output size: {total_size} bytes")
    
    # Create output directory if it doesn't exist
    output_dir = os.path.dirname(output_file)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # Write merged file
    print(f"\nWriting merged file to: {output_file}")
    
    try:
        with open(output_file, 'wb') as out_f:
            for i, (header_data, pixel_data) in enumerate(frames):
                print(f"Writing frame {i+1}...")
                
                # Write header (6 uint32_t values)
                header_bytes = struct.pack('<IIIIII', *header_data)
                out_f.write(header_bytes)
                
                # Write pixel data
                out_f.write(pixel_data)
        
        # Verify output file
        output_size = os.path.getsize(output_file)
        print(f"✅ Successfully created merged file: {output_file}")
        print(f"Output file size: {output_size} bytes")
        
        if output_size == total_size:
            print("✅ File size matches expected size")
        else:
            print(f"⚠️  File size mismatch: expected {total_size}, got {output_size}")
        
        return True
        
    except Exception as e:
        print(f"❌ Error writing output file: {e}")
        return False

def main():
    """Main function"""
    if len(sys.argv) != 3:
        print("Usage: python merge_normal_animations.py <input_directory> <output_file>")
        print("Example: python merge_normal_animations.py animations/ animations/normal_all.bin")
        sys.exit(1)
    
    input_dir = sys.argv[1]
    output_file = sys.argv[2]
    
    if not os.path.isdir(input_dir):
        print(f"Error: Input directory does not exist: {input_dir}")
        sys.exit(1)
    
    success = merge_normal_animations(input_dir, output_file)
    
    if success:
        print("\n🎉 Animation merging completed successfully!")
        print(f"You can now use '{output_file}' instead of individual normal1-3.bin files.")
    else:
        print("\n❌ Animation merging failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()
