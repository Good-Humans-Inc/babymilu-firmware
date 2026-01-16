#!/usr/bin/env python3
"""
Pack multiple GIF files into a single test.bin file for SD card loading.

This script creates a test.bin file that contains all animation GIFs in a format
that can be easily extracted and loaded by the firmware.

Usage:
    python pack_gifs_to_test_bin.py <gif_folder> <output_test.bin>

Example:
    python pack_gifs_to_test_bin.py animations/ test.bin

Expected GIF files in folder:
    Main animations (10):
    - normal.gif
    - embarrass.gif
    - fire.gif
    - inspiration.gif
    - shy.gif
    - sleep.gif
    - happy.gif
    - laugh.gif
    - sad.gif
    - talk.gif
    
    System GIFs (3):
    - wifi.gif (loaded but not used yet)
    - battery.gif (loaded but not used yet)
    - silence.gif (displayed when volume is 0)
    
Note: question.gif has been removed from the main animations.
"""

import struct
import os
import sys
from pathlib import Path

# Animation names in order - all 10 main animations to load from SD card
# Question has been removed, these are the main animations
ANIMATION_NAMES = [
    "normal",      # ANIMATION_STATIC_NORMAL / ANIMATION_NORMAL
    "embarrass",   # ANIMATION_EMBARRESSED
    "fire",        # ANIMATION_FIRE
    "inspiration", # ANIMATION_INSPIRATION
    "shy",         # ANIMATION_SHY
    "sleep",       # ANIMATION_SLEEP
    "happy",       # ANIMATION_HAPPY
    "laugh",       # Additional animation
    "sad",         # Additional animation
    "talk",        # Additional animation
]

# Additional system GIFs (loaded but not used as main animations yet)
SYSTEM_GIFS = [
    "wifi",     # System status GIF (for future use)
    "battery",  # System status GIF (for future use)
    "silence",  # System status GIF (displayed when volume is 0)
]

# Core GIF filenames (all 10 are main animations)
CORE_GIFS = [f"{name}.gif" for name in ANIMATION_NAMES]

# System GIF filenames
SYSTEM_GIF_FILES = [f"{name}.gif" for name in SYSTEM_GIFS]

# All supported GIF filenames (core + system)
ALL_GIFS = CORE_GIFS + SYSTEM_GIF_FILES

def compute_checksum(data):
    """Calculate checksum as sum of all bytes, masked to 32 bits."""
    return sum(data) & 0xFFFFFFFF

def verify_gif_format(file_path):
    """Verify that the file is a valid GIF."""
    try:
        with open(file_path, 'rb') as f:
            header = f.read(6)
            if header[:3] == b'GIF' and header[3:6] in [b'87a', b'89a']:
                return True
    except Exception:
        pass
    return False

def pack_gif_file(file_name, file_path, offset, max_name_len=32):
    """
    Pack a single GIF file into the test.bin format.
    
    Args:
        file_name: Name of the file (e.g., "normal.gif")
        file_path: Path to the actual GIF file
        offset: Offset in data section where file will be placed
        max_name_len: Maximum length for file name (default 32)
    
    Returns:
        tuple: (table_entry, data_entry, file_size)
    """
    if not os.path.exists(file_path):
        raise FileNotFoundError(f"GIF file not found: {file_path}")
    
    # Verify it's a valid GIF
    if not verify_gif_format(file_path):
        raise ValueError(f"File is not a valid GIF: {file_path}")
    
    with open(file_path, 'rb') as f:
        file_data = f.read()
    
    file_size = len(file_data)
    
    # File table entry (44 bytes: 32 name + 4 size + 4 offset + 2 width + 2 height)
    # For GIFs, width/height are 0 (not used, GIF has its own dimensions)
    name_padded = file_name[:max_name_len].ljust(max_name_len, '\0')
    table_entry = bytearray()
    table_entry.extend(name_padded.encode('utf-8'))
    table_entry.extend(struct.pack('<I', file_size))  # File size (little-endian)
    table_entry.extend(struct.pack('<I', offset))      # Offset in data section
    table_entry.extend(struct.pack('<H', 0))          # Width (0 for GIFs)
    table_entry.extend(struct.pack('<H', 0))          # Height (0 for GIFs)
    
    # Data entry: magic bytes (0x5A5A) + actual GIF data
    data_entry = bytearray()
    data_entry.extend(b'\x5A\x5A')  # Magic bytes (same as assets.bin format)
    data_entry.extend(file_data)
    
    return table_entry, data_entry, file_size

def create_test_bin(gif_folder, output_path):
    """
    Create test.bin from GIF files in the specified folder.
    
    Args:
        gif_folder: Path to folder containing GIF files
        output_path: Output path for test.bin
    
    Returns:
        bool: True if successful, False otherwise
    """
    gif_folder = Path(gif_folder)
    
    if not gif_folder.exists() or not gif_folder.is_dir():
        print(f"Error: GIF folder not found: {gif_folder}")
        return False
    
    # Find all GIF files (main animations + system GIFs)
    found_gifs = {}
    missing_gifs = []
    
    # Check for all main animations
    for gif_name in CORE_GIFS:
        gif_path = gif_folder / gif_name
        if gif_path.exists():
            found_gifs[gif_name] = str(gif_path)
        else:
            missing_gifs.append(gif_name)
    
    # Check for system GIFs
    for gif_name in SYSTEM_GIF_FILES:
        gif_path = gif_folder / gif_name
        if gif_path.exists():
            found_gifs[gif_name] = str(gif_path)
            print(f"Info: Found system GIF: {gif_name}")
        else:
            print(f"Warning: System GIF not found: {gif_name}")
    
    if not found_gifs:
        print("Error: No GIF files found in folder!")
        print(f"Expected main files: {', '.join(CORE_GIFS)}")
        print(f"Expected system files: {', '.join(SYSTEM_GIF_FILES)}")
        return False
    
    if missing_gifs:
        print(f"Warning: Missing main GIF files: {', '.join(missing_gifs)}")
        print("These animations will not be available.")
    
    print(f"Found {len(found_gifs)} GIF file(s) to pack:")
    for name in found_gifs.keys():
        size = os.path.getsize(found_gifs[name])
        print(f"  - {name}: {size:,} bytes")
    
    # Pack files in order (main animations first, then system GIFs)
    file_table = bytearray()
    data_section = bytearray()
    current_offset = 0
    file_info_list = []
    
    # Pack all main animations in the order of ANIMATION_NAMES
    for anim_name in ANIMATION_NAMES:
        gif_name = f"{anim_name}.gif"
        if gif_name in found_gifs:
            try:
                table_entry, data_entry, file_size = pack_gif_file(
                    gif_name, found_gifs[gif_name], current_offset
                )
                
                file_table.extend(table_entry)
                data_section.extend(data_entry)
                file_info_list.append((gif_name, file_size, current_offset))
                current_offset += len(data_entry)
                
                print(f"✓ Packed {gif_name} ({file_size:,} bytes)")
            except Exception as e:
                print(f"Error packing {gif_name}: {e}")
                return False
    
    # Pack system GIFs after main animations
    for system_name in SYSTEM_GIFS:
        gif_name = f"{system_name}.gif"
        if gif_name in found_gifs:
            try:
                table_entry, data_entry, file_size = pack_gif_file(
                    gif_name, found_gifs[gif_name], current_offset
                )
                
                file_table.extend(table_entry)
                data_section.extend(data_entry)
                file_info_list.append((gif_name, file_size, current_offset))
                current_offset += len(data_entry)
                
                print(f"✓ Packed system GIF {gif_name} ({file_size:,} bytes)")
            except Exception as e:
                print(f"Error packing {gif_name}: {e}")
                return False
    
    # Combine file table and data section
    combined_data = file_table + data_section
    combined_length = len(combined_data)
    checksum = compute_checksum(combined_data)
    
    # Create header (12 bytes: 4 file_count + 4 checksum + 4 length)
    num_files = len(file_info_list)
    header = struct.pack('<I', num_files)      # file count
    header += struct.pack('<I', checksum)      # checksum
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
    print()
    print("=" * 60)
    print(f"✓ Created test.bin: {output_path}")
    print("=" * 60)
    print(f"  Files packed: {num_files}")
    print(f"  Total size: {len(final_data):,} bytes")
    print(f"  Header: 12 bytes")
    print(f"  File table: {len(file_table)} bytes ({num_files} entries)")
    print(f"  Data section: {len(data_section):,} bytes")
    print(f"  Checksum: 0x{checksum:08X}")
    print()
    print("File details:")
    print("-" * 60)
    for name, size, offset in file_info_list:
        print(f"  {name:20s} {size:8,} bytes  @ offset {offset:8,}")
    print()
    print("Next steps:")
    print(f"  1. Copy {output_path} to the root of your SD card")
    print(f"  2. Ensure the SD card is mounted on the device")
    print(f"  3. The firmware will automatically load GIFs from test.bin")
    print()
    
    return True

def main():
    """Main function."""
    if len(sys.argv) < 3:
        print("Usage: python pack_gifs_to_test_bin.py <gif_folder> <output_test.bin>")
        print()
        print("Arguments:")
        print("  gif_folder      Path to folder containing GIF files")
        print("  output_test.bin Path where test.bin will be written")
        print()
        print("Main GIF files (10 animations):")
        for name in CORE_GIFS:
            print(f"  - {name}")
        print()
        print("System GIF files (3 system status):")
        for name in SYSTEM_GIF_FILES:
            print(f"  - {name}")
        print()
        print("Example:")
        print("  python pack_gifs_to_test_bin.py animations/ test.bin")
        sys.exit(1)
    
    gif_folder = sys.argv[1]
    output_path = sys.argv[2]
    
    if not create_test_bin(gif_folder, output_path):
        sys.exit(1)

if __name__ == "__main__":
    main()

