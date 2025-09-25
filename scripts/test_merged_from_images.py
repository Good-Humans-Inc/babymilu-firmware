#!/usr/bin/env python3
"""
Test script to validate merged binary files created from images.

This script reads a merged binary file created by image_to_merged_spiffs.py
and validates its structure and contents.
"""

import sys
import os
import struct
import argparse

def test_merged_binary_file(filepath, expected_frames=None):
    """
    Test a merged binary file by reading and validating each frame.
    
    Args:
        filepath: Path to the merged binary file
        expected_frames: Expected number of frames (None for auto-detect)
    """
    print(f"=== Testing Merged Binary File: {filepath} ===")
    
    if not os.path.exists(filepath):
        print(f"❌ File not found: {filepath}")
        return False
    
    file_size = os.path.getsize(filepath)
    print(f"File size: {file_size} bytes")
    
    try:
        with open(filepath, 'rb') as f:
            current_offset = 0
            frame_count = 0
            
            while current_offset < file_size:
                frame_count += 1
                print(f"\n--- Frame {frame_count} ---")
                
                # Read header (6 uint32_t values)
                header_bytes = f.read(6 * 4)  # 24 bytes
                if len(header_bytes) != 24:
                    print(f"❌ Frame {frame_count}: Incomplete header (got {len(header_bytes)} bytes)")
                    if current_offset == 0:
                        return False  # First frame must be complete
                    else:
                        print("✅ End of file reached")
                        break
                
                # Unpack header data
                header_data = struct.unpack('<IIIIII', header_bytes)
                
                # Validate magic number
                if header_data[0] != 0x4C56474C:
                    print(f"❌ Frame {frame_count}: Invalid magic number 0x{header_data[0]:08x}")
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
                print(f"  Color format: {color_format} ({get_color_format_name(color_format)})")
                print(f"  Flags: {flags}")
                print(f"  Dimensions: {width}x{height}")
                print(f"  Stride: {stride}")
                print(f"  Expected data size: {expected_data_size} bytes")
                
                # Read pixel data
                pixel_data = f.read(expected_data_size)
                if len(pixel_data) != expected_data_size:
                    print(f"❌ Frame {frame_count}: Expected {expected_data_size} bytes, got {len(pixel_data)} bytes")
                    return False
                
                print(f"  Actual data size: {len(pixel_data)} bytes ✅")
                current_offset += 24 + expected_data_size
                print(f"  Frame offset: {current_offset - len(pixel_data) - 24} -> {current_offset}")
                
                # Check if we have enough data for another frame
                if current_offset >= file_size:
                    print("✅ End of file reached")
                    break
            
            # Check if we read the entire file
            remaining = f.read()
            if remaining:
                print(f"⚠️  Warning: {len(remaining)} extra bytes at end of file")
            
            print(f"\n✅ Successfully validated {frame_count} frames")
            
            # Check against expected frame count if provided
            if expected_frames is not None and frame_count != expected_frames:
                print(f"⚠️  Expected {expected_frames} frames, but found {frame_count}")
                return False
            
            return True
            
    except Exception as e:
        print(f"❌ Error reading file: {e}")
        return False

def get_color_format_name(cf_value):
    """Get human-readable color format name"""
    cf_names = {
        0x06: "L8",
        0x07: "I1", 
        0x08: "I2",
        0x09: "I4",
        0x0A: "I8",
        0x0B: "A1",
        0x0C: "A2",
        0x0D: "A4",
        0x0E: "A8",
        0x0F: "RGB888",
        0x10: "ARGB8888",
        0x11: "XRGB8888",
        0x12: "RGB565",
        0x13: "ARGB8565",
        0x14: "RGB565A8"
    }
    return cf_names.get(cf_value, f"Unknown({cf_value})")

def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(
        description="Test merged binary files created from images",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python test_merged_from_images.py merged_file.bin
  python test_merged_from_images.py merged_file.bin --frames 3
        """
    )
    
    parser.add_argument('filepath', help='Path to merged binary file')
    parser.add_argument('--frames', type=int, 
                       help='Expected number of frames (optional)')
    
    return parser.parse_args()

def main():
    """Main function"""
    args = parse_arguments()
    
    success = test_merged_binary_file(args.filepath, args.frames)
    
    if success:
        print("\n🎉 Merged binary file validation passed!")
    else:
        print("\n❌ Merged binary file validation failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()
