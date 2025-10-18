#!/usr/bin/env python3
"""
Test script to validate mega animation files.

This script reads a mega animation file and validates its structure,
ensuring all 28 frames are properly formatted and accessible.
"""

import sys
import os
import struct
import argparse

def test_mega_animation_file(filepath):
    """
    Test a mega animation file by reading and validating all 28 frames.
    
    Args:
        filepath: Path to the mega animation file
    """
    print(f"=== Testing Mega Animation File: {filepath} ===")
    
    if not os.path.exists(filepath):
        print(f"❌ File not found: {filepath}")
        return False
    
    file_size = os.path.getsize(filepath)
    print(f"File size: {file_size} bytes ({file_size / 1024:.1f} KB)")
    
    # Expected frame counts for each animation
    animation_names = ["Normal", "Embarrass", "Fire", "Happy", "Inspiration", "Question", "Shy", "Sleep"]
    animation_frame_counts = [3, 3, 4, 4, 4, 4, 2, 4]
    total_expected_frames = sum(animation_frame_counts)
    
    print(f"Expected total frames: {total_expected_frames}")
    print(f"Animation breakdown:")
    for i, (name, count) in enumerate(zip(animation_names, animation_frame_counts)):
        print(f"  {i+1}. {name}: {count} frames")
    
    try:
        with open(filepath, 'rb') as f:
            current_offset = 0
            total_frames_read = 0
            
            # Test each animation
            for anim_idx, (anim_name, frame_count) in enumerate(zip(animation_names, animation_frame_counts)):
                print(f"\n--- {anim_name} Animation ({frame_count} frames) ---")
                
                for frame_idx in range(frame_count):
                    total_frames_read += 1
                    print(f"  Frame {frame_idx + 1}/{frame_count} (Global frame {total_frames_read})")
                    
                    # Read header (6 uint32_t values)
                    header_bytes = f.read(6 * 4)  # 24 bytes
                    if len(header_bytes) != 24:
                        print(f"    ❌ Incomplete header (got {len(header_bytes)} bytes)")
                        return False
                    
                    # Unpack header data
                    header_data = struct.unpack('<IIIIII', header_bytes)
                    
                    # Validate magic number
                    if header_data[0] != 0x4C56474C:
                        print(f"    ❌ Invalid magic number 0x{header_data[0]:08x}")
                        return False
                    
                    # Extract image properties
                    magic = header_data[0]
                    color_format = header_data[1]
                    flags = header_data[2]
                    width = header_data[3]
                    height = header_data[4]
                    stride = header_data[5]
                    expected_data_size = height * stride
                    
                    print(f"    Magic: 0x{magic:08x} ✅")
                    print(f"    Color format: {color_format} ({get_color_format_name(color_format)})")
                    print(f"    Flags: {flags}")
                    print(f"    Dimensions: {width}x{height}")
                    print(f"    Stride: {stride}")
                    print(f"    Expected data size: {expected_data_size} bytes")
                    
                    # Read pixel data
                    pixel_data = f.read(expected_data_size)
                    if len(pixel_data) != expected_data_size:
                        print(f"    ❌ Expected {expected_data_size} bytes, got {len(pixel_data)} bytes")
                        return False
                    
                    print(f"    Actual data size: {len(pixel_data)} bytes ✅")
                    current_offset += 24 + expected_data_size
                    print(f"    Frame offset: {current_offset - len(pixel_data) - 24} -> {current_offset}")
                
                print(f"  ✅ {anim_name} animation validated successfully")
            
            # Check if we've read the entire file
            remaining = f.read()
            if remaining:
                print(f"⚠️  Warning: {len(remaining)} extra bytes at end of file")
            else:
                print(f"✅ File read completely, no extra bytes")
            
            print(f"\n✅ Successfully validated all {total_frames_read} frames")
            
            # Verify frame count
            if total_frames_read == total_expected_frames:
                print(f"✅ Frame count matches expected ({total_expected_frames})")
            else:
                print(f"❌ Frame count mismatch: expected {total_expected_frames}, got {total_frames_read}")
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
        description="Test mega animation files containing all animations",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python test_mega_animations.py animations_mega.bin
        """
    )
    
    parser.add_argument('filepath', help='Path to mega animation binary file')
    
    return parser.parse_args()

def main():
    """Main function"""
    args = parse_arguments()
    
    success = test_mega_animation_file(args.filepath)
    
    if success:
        print("\n🎉 Mega animation file validation passed!")
        print("Ready to flash to device SPIFFS partition.")
    else:
        print("\n❌ Mega animation file validation failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()
