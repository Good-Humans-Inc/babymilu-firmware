#!/usr/bin/env python3
"""
Create a minimal assets.bin file containing a single GIF file and index.json.

This script creates a properly formatted assets.bin file that can be flashed
to the ESP32's assets partition and used with EmoteDisplay.

Usage:
    python create_single_gif_assets.py <gif_file> <output_assets.bin> [emotion_name] [fps] [loop]

Example:
    python create_single_gif_assets.py animation.gif assets.bin happy 20 true
"""

import struct
import os
import json
import sys

def compute_checksum(data):
    """Calculate checksum as sum of all bytes, masked to 16 bits."""
    return sum(data) & 0xFFFF

def pack_file(file_name, file_path, offset, max_name_len=32):
    """
    Pack a single file into the assets format.
    
    Args:
        file_name: Name of the file in assets (max 32 chars)
        file_path: Path to the actual file on disk
        offset: Offset in data section where file will be placed
        max_name_len: Maximum length for file name (default 32)
    
    Returns:
        tuple: (table_entry, data_entry, file_size)
    """
    if not os.path.exists(file_path):
        raise FileNotFoundError(f"File not found: {file_path}")
    
    with open(file_path, 'rb') as f:
        file_data = f.read()
    
    file_size = len(file_data)
    
    # File table entry (44 bytes: 32 name + 4 size + 4 offset + 2 width + 2 height)
    name_padded = file_name[:max_name_len].ljust(max_name_len, '\0')
    table_entry = bytearray()
    table_entry.extend(name_padded.encode('utf-8'))
    table_entry.extend(struct.pack('<I', file_size))  # File size (little-endian)
    table_entry.extend(struct.pack('<I', offset))      # Offset in data section
    table_entry.extend(struct.pack('<H', 0))          # Width (0 for non-images)
    table_entry.extend(struct.pack('<H', 0))          # Height (0 for non-images)
    
    # Data entry: magic bytes (0x5A5A) + actual file data
    data_entry = bytearray()
    data_entry.extend(b'\x5A\x5A')  # Magic bytes
    data_entry.extend(file_data)
    
    return table_entry, data_entry, file_size

def create_assets_bin(files_dict, output_path):
    """
    Create assets.bin from a dictionary of {name: path} files.
    
    Args:
        files_dict: Dictionary mapping asset names to file paths
                   Example: {"index.json": "path/to/index.json", "gif.gif": "path/to/gif.gif"}
        output_path: Output path for assets.bin
    
    Returns:
        str: Path to created assets.bin file
    """
    file_table = bytearray()
    data_section = bytearray()
    current_offset = 0
    file_info_list = []
    
    # Pack each file
    for file_name, file_path in files_dict.items():
        if not os.path.exists(file_path):
            raise FileNotFoundError(f"File not found: {file_path}")
        
        table_entry, data_entry, file_size = pack_file(
            file_name, file_path, current_offset
        )
        
        file_table.extend(table_entry)
        data_section.extend(data_entry)
        file_info_list.append((file_name, file_size, current_offset))
        current_offset += len(data_entry)
    
    # Combine file table and data section
    combined_data = file_table + data_section
    combined_length = len(combined_data)
    checksum = compute_checksum(combined_data)
    
    # Create header (12 bytes: 4 file_count + 4 checksum + 4 length)
    num_files = len(files_dict)
    header = struct.pack('<I', num_files)      # file count
    header += struct.pack('<I', checksum)       # checksum
    header += struct.pack('<I', combined_length)  # length
    
    # Write final file: header + combined_data
    final_data = header + combined_data
    
    # Ensure output directory exists
    output_dir = os.path.dirname(output_path)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir, exist_ok=True)
    
    with open(output_path, 'wb') as f:
        f.write(final_data)
    
    # Print summary
    print(f"✓ Created assets.bin: {output_path}")
    print(f"  - Files packed: {num_files}")
    print(f"  - Total size: {len(final_data):,} bytes")
    print(f"  - Header: 12 bytes")
    print(f"  - File table: {len(file_table)} bytes ({num_files} entries)")
    print(f"  - Data section: {len(data_section):,} bytes")
    print(f"  - Checksum: 0x{checksum:04X}")
    print()
    print("File details:")
    for name, size, offset in file_info_list:
        print(f"  - {name}: {size:,} bytes at offset {offset}")
    
    return output_path

def create_index_json(gif_name, emotion_name="happy", fps=20, loop=True, output_path="index.json"):
    """
    Create index.json for EmoteDisplay configuration.
    
    Args:
        gif_name: Name of the GIF file in assets.bin
        emotion_name: Emotion name to use (default: "happy")
        fps: Frames per second (default: 20)
        loop: Whether to loop animation (default: True)
        output_path: Path where index.json will be written
    
    Returns:
        str: Path to created index.json file
    """
    index_data = {
        "version": 1,
        "emoji_collection": [
            {
                "name": emotion_name,
                "file": gif_name,
                "eaf": {
                    "fps": fps,
                    "loop": loop,
                    "lack": False
                }
            }
        ]
    }
    
    # Ensure output directory exists
    output_dir = os.path.dirname(output_path)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir, exist_ok=True)
    
    with open(output_path, 'w') as f:
        json.dump(index_data, f, indent=2)
    
    print(f"✓ Created index.json: {output_path}")
    print(f"  - Emotion name: {emotion_name}")
    print(f"  - GIF file: {gif_name}")
    print(f"  - FPS: {fps}")
    print(f"  - Loop: {loop}")
    print()
    
    return output_path

def verify_gif_format(gif_path):
    """
    Verify that the file is a valid GIF.
    
    Args:
        gif_path: Path to GIF file
    
    Returns:
        bool: True if valid GIF, False otherwise
    """
    try:
        with open(gif_path, 'rb') as f:
            header = f.read(6)
            if header[:3] == b'GIF' and header[3:6] in [b'87a', b'89a']:
                return True
    except Exception:
        pass
    return False

def main():
    """Main function."""
    if len(sys.argv) < 3:
        print("Usage: python create_single_gif_assets.py <gif_file> <output_assets.bin> [emotion_name] [fps] [loop]")
        print()
        print("Arguments:")
        print("  gif_file          Path to input GIF file")
        print("  output_assets.bin Path where assets.bin will be written")
        print("  emotion_name      Emotion name (default: 'happy')")
        print("  fps               Frames per second (default: 20)")
        print("  loop              Loop animation (default: true)")
        print()
        print("Example:")
        print("  python create_single_gif_assets.py animation.gif assets.bin happy 20 true")
        sys.exit(1)
    
    gif_path = sys.argv[1]
    output_path = sys.argv[2]
    emotion_name = sys.argv[3] if len(sys.argv) > 3 else "happy"
    fps = int(sys.argv[4]) if len(sys.argv) > 4 else 20
    loop_str = sys.argv[5] if len(sys.argv) > 5 else "true"
    loop = loop_str.lower() in ['true', '1', 'yes', 'y']
    
    # Validate inputs
    if not os.path.exists(gif_path):
        print(f"Error: GIF file not found: {gif_path}")
        sys.exit(1)
    
    if not verify_gif_format(gif_path):
        print(f"Warning: {gif_path} may not be a valid GIF file")
        response = input("Continue anyway? (y/n): ")
        if response.lower() != 'y':
            sys.exit(1)
    
    if fps < 1 or fps > 63:
        print(f"Error: FPS must be between 1 and 63 (got {fps})")
        sys.exit(1)
    
    print("Creating assets.bin with single GIF...")
    print(f"  Input GIF: {gif_path}")
    print(f"  Output: {output_path}")
    print()
    
    # Get base directory for index.json
    output_dir = os.path.dirname(output_path) or "."
    gif_name = os.path.basename(gif_path)
    
    # Create index.json
    index_path = os.path.join(output_dir, "index.json")
    create_index_json(gif_name, emotion_name, fps, loop, index_path)
    
    # Create assets.bin with both index.json and GIF
    files_to_pack = {
        "index.json": index_path,
        gif_name: gif_path
    }
    
    try:
        create_assets_bin(files_to_pack, output_path)
        print()
        print("✓ Success! Next steps:")
        print(f"  1. Flash assets.bin to your device's assets partition:")
        print(f"     esptool.py --chip esp32s3 --port COM3 write_flash <offset> {output_path}")
        print(f"  2. In your code, call: display->SetEmotion(\"{emotion_name}\")")
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()

