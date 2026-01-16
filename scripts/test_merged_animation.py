#!/usr/bin/env python3
"""
Test script to verify the format of merged animation files.

This script reads a merged animation file and validates its structure.
"""

import sys
import os
import struct

def test_merged_animation_file(filepath, expected_frames=3):
    """
    Test a merged animation file by reading and validating each frame.
    
    Args:
        filepath: Path to the merged animation file
        expected_frames: Number of frames expected in the file
    """
    print(f"=== Testing Merged Animation File: {filepath} ===")
    
    if not os.path.exists(filepath):
        print(f"❌ File not found: {filepath}")
        return False
    
    file_size = os.path.getsize(filepath)
    print(f"File size: {file_size} bytes")
    
    try:
        with open(filepath, 'rb') as f:
            current_offset = 0
            
            for frame_num in range(expected_frames):
                print(f"\n--- Frame {frame_num + 1} ---")
                
                # Read header (6 uint32_t values)
                header_bytes = f.read(6 * 4)  # 24 bytes
                if len(header_bytes) != 24:
                    print(f"❌ Frame {frame_num + 1}: Incomplete header (got {len(header_bytes)} bytes)")
                    return False
                
                # Unpack header data
                header_data = struct.unpack('<IIIIII', header_bytes)
                
                # Validate magic number
                if header_data[0] != 0x4C56474C:
                    print(f"❌ Frame {frame_num + 1}: Invalid magic number 0x{header_data[0]:08x}")
                    return False
                
                # Extract image properties
                magic = header_data[0]
                color_format = header_data[1]
                flags = header_data[2]
                width = header_data[3]
                height = header_data[4]
                stride = header_data[5]
                expected_data_size = height * stride
                
                print(f"  Magic: 0x{magic:08x} ✅")
                print(f"  Color format: {color_format}")
                print(f"  Flags: {flags}")
                print(f"  Dimensions: {width}x{height}")
                print(f"  Stride: {stride}")
                print(f"  Expected data size: {expected_data_size} bytes")
                
                # Read pixel data
                pixel_data = f.read(expected_data_size)
                if len(pixel_data) != expected_data_size:
                    print(f"❌ Frame {frame_num + 1}: Expected {expected_data_size} bytes, got {len(pixel_data)} bytes")
                    return False
                
                print(f"  Actual data size: {len(pixel_data)} bytes ✅")
                current_offset += 24 + expected_data_size
                print(f"  Frame offset: {current_offset - len(pixel_data) - 24} -> {current_offset}")
            
            # Check if we've read the entire file
            remaining = f.read()
            if remaining:
                print(f"⚠️  Warning: {len(remaining)} extra bytes at end of file")
            else:
                print(f"✅ File read completely, no extra bytes")
            
            print(f"\n✅ Successfully validated {expected_frames} frames")
            return True
            
    except Exception as e:
        print(f"❌ Error reading file: {e}")
        return False

def main():
    """Main function"""
    if len(sys.argv) != 2:
        print("Usage: python test_merged_animation.py <merged_animation_file>")
        print("Example: python test_merged_animation.py animations/normal_all.bin")
        sys.exit(1)
    
    filepath = sys.argv[1]
    
    success = test_merged_animation_file(filepath)
    
    if success:
        print("\n🎉 Merged animation file validation passed!")
    else:
        print("\n❌ Merged animation file validation failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()
