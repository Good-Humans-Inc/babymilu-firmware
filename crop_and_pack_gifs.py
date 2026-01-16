#!/usr/bin/env python3
"""
Crop, resize, and pack GIF files into a test.bin file.

This script:
1. Crops and resizes all GIFs in a folder (optional step)
2. Packs the GIF files into a single test.bin file for SD card loading

Usage:
    python crop_and_pack_gifs.py <gif_folder> <output_test.bin> [--no-crop]
    
Arguments:
    gif_folder      Path to folder containing GIF files
    output_test.bin Path where test.bin will be written
    --no-crop       Skip the crop/resize step (only pack existing GIFs)

Example:
    python crop_and_pack_gifs.py gif_folder/ test.bin
    python crop_and_pack_gifs.py gif_folder/ test.bin --no-crop

Expected GIF files in folder (20 total):
    Main emotional animations (11):
    - normal.gif
    - smirk.gif, smirk_start.gif
    - heart.gif, heart_start.gif
    - blush.gif
    - sad.gif, sad_start.gif
    - laugh.gif, laugh_start.gif
    - sleep.gif
    - starry.gif, starry_start.gif
    - cry.gif
    - angry.gif, angry_start.gif
    - listening.gif
    
    System GIFs (3):
    - silence.gif
    - battery.gif
    - wifi.gif
"""

import struct
import os
import sys
from pathlib import Path
from PIL import Image, ImageSequence

# Crop/Resize configuration
CROP_BOX = (244, 219, 780, 755)  # (left, top, right, bottom)
TARGET_SIZE = (360, 360)  # Final size after resize

# Expected GIF files for 20 GIF system (all required)
EXPECTED_GIFS = [
    "normal.gif",
    "smirk.gif",
    "smirk_start.gif",
    "heart.gif",
    "heart_start.gif",
    "blush.gif",
    "battery.gif",
    "wifi.gif",
    "silence.gif",
    "sad.gif",
    "sad_start.gif",
    "laugh.gif",
    "laugh_start.gif",
    "sleep.gif",
    "starry.gif",
    "starry_start.gif",
    "cry.gif",
    "angry.gif",
    "angry_start.gif",
    "listening.gif",
]

# All supported GIF filenames (20 total)
ALL_GIFS = EXPECTED_GIFS

def crop_gif(input_path, output_path):
    """
    Crop an animated GIF to the specified region and resize.
    
    Args:
        input_path: Path to input GIF file
        output_path: Path to save cropped GIF
    """
    try:
        # Open the GIF
        gif = Image.open(input_path)
        
        # Get GIF info
        frames = []
        durations = []
        
        # Process each frame
        for frame in ImageSequence.Iterator(gif):
            # Crop the frame (preserve original color mode)
            cropped_frame = frame.crop(CROP_BOX)
            
            # Resize to target size (preserve original color mode)
            resized_frame = cropped_frame.resize(TARGET_SIZE, Image.Resampling.LANCZOS)
            
            # Keep original mode - don't convert color space
            frames.append(resized_frame.copy())
            
            # Preserve frame duration
            if 'duration' in frame.info:
                durations.append(frame.info['duration'])
            else:
                durations.append(100)  # Default 100ms if no duration info
        
        # Save the cropped GIF
        if len(frames) > 0:
            save_kwargs = {
                'save_all': True,
                'append_images': frames[1:],
                'duration': durations,
                'loop': gif.info.get('loop', 0)
            }
            
            # Preserve palette and transparency from original
            if 'palette' in gif.info:
                save_kwargs['palette'] = gif.info['palette']
            if 'transparency' in gif.info:
                save_kwargs['transparency'] = gif.info['transparency']
            
            frames[0].save(output_path, **save_kwargs)
            print(f"✓ Cropped and resized: {os.path.basename(input_path)}")
            return True
        else:
            print(f"✗ Error: No frames found in {input_path}")
            return False
            
    except Exception as e:
        print(f"✗ Error processing {input_path}: {str(e)}")
        return False

def crop_all_gifs(gif_folder):
    """
    Crop and resize all GIFs in the folder.
    
    Args:
        gif_folder: Path to folder containing GIF files
        
    Returns:
        bool: True if successful, False otherwise
    """
    gif_folder = Path(gif_folder)
    
    if not gif_folder.exists() or not gif_folder.is_dir():
        print(f"Error: Folder '{gif_folder}' not found!")
        return False
    
    # Get all GIF files
    gif_files = [f for f in os.listdir(gif_folder) if f.lower().endswith('.gif')]
    
    if not gif_files:
        print(f"No GIF files found in '{gif_folder}'")
        return False
    
    print(f"Found {len(gif_files)} GIF file(s) to crop and resize...")
    print(f"Crop region: {CROP_BOX}, then resize to {TARGET_SIZE}\n")
    
    success_count = 0
    # Process each GIF
    for gif_file in sorted(gif_files):
        input_path = gif_folder / gif_file
        output_path = gif_folder / gif_file  # Overwrite original
        if crop_gif(input_path, output_path):
            success_count += 1
    
    print(f"\n✓ Completed processing {success_count}/{len(gif_files)} GIF file(s)\n")
    return success_count > 0

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
    
    # Find all GIF files (all 20 expected GIFs)
    found_gifs = {}
    missing_gifs = []
    
    # Check for all expected GIFs
    for gif_name in EXPECTED_GIFS:
        gif_path = gif_folder / gif_name
        if gif_path.exists():
            found_gifs[gif_name] = str(gif_path)
        else:
            missing_gifs.append(gif_name)
    
    if not found_gifs:
        print("Error: No GIF files found in folder!")
        print(f"Expected files: {', '.join(EXPECTED_GIFS)}")
        return False
    
    if missing_gifs:
        print(f"Error: Missing required GIF files ({len(missing_gifs)} missing):")
        for gif_name in missing_gifs:
            print(f"  - {gif_name}")
        print(f"\nFound {len(found_gifs)}/{len(EXPECTED_GIFS)} required GIFs.")
        print("All 20 GIFs are required for the 20 GIF system.")
        return False
    
    print(f"Found {len(found_gifs)} GIF file(s) to pack:")
    for name in found_gifs.keys():
        size = os.path.getsize(found_gifs[name])
        print(f"  - {name}: {size:,} bytes")
    
    # Pack files in order (as specified in EXPECTED_GIFS)
    file_table = bytearray()
    data_section = bytearray()
    current_offset = 0
    file_info_list = []
    
    # Pack all GIFs in the order specified in EXPECTED_GIFS
    for gif_name in EXPECTED_GIFS:
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
    # Parse arguments
    skip_crop = '--no-crop' in sys.argv
    args = [arg for arg in sys.argv[1:] if arg != '--no-crop']
    
    if len(args) < 2:
        print("Usage: python crop_and_pack_gifs.py <gif_folder> <output_test.bin> [--no-crop]")
        print()
        print("Arguments:")
        print("  gif_folder      Path to folder containing GIF files")
        print("  output_test.bin Path where test.bin will be written")
        print("  --no-crop       Skip the crop/resize step (only pack existing GIFs)")
        print()
        print("Expected GIF files (20 total, all required):")
        print("  Main emotional animations:")
        print("    - normal.gif")
        print("    - smirk.gif, smirk_start.gif")
        print("    - heart.gif, heart_start.gif")
        print("    - blush.gif")
        print("    - sad.gif, sad_start.gif")
        print("    - laugh.gif, laugh_start.gif")
        print("    - sleep.gif")
        print("    - starry.gif, starry_start.gif")
        print("    - cry.gif")
        print("    - angry.gif, angry_start.gif")
        print("    - listening.gif")
        print("  System GIFs:")
        print("    - silence.gif")
        print("    - battery.gif")
        print("    - wifi.gif")
        print()
        print("Examples:")
        print("  python crop_and_pack_gifs.py gif_folder/ test.bin")
        print("  python crop_and_pack_gifs.py gif_folder/ test.bin --no-crop")
        sys.exit(1)
    
    gif_folder = args[0]
    output_path = args[1]
    
    # Step 1: Crop and resize GIFs (unless --no-crop is specified)
    if not skip_crop:
        print("=" * 60)
        print("Step 1: Cropping and resizing GIFs")
        print("=" * 60)
        if not crop_all_gifs(gif_folder):
            print("Warning: Crop/resize step had errors, continuing with packing...")
        print()
    else:
        print("Skipping crop/resize step (--no-crop flag specified)\n")
    
    # Step 2: Pack GIFs into test.bin
    print("=" * 60)
    print("Step 2: Packing GIFs into test.bin")
    print("=" * 60)
    if not create_test_bin(gif_folder, output_path):
        sys.exit(1)

if __name__ == "__main__":
    main()

