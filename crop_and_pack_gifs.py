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

Expected GIF files in folder (exactly 21 GIFs):
    - smirk.gif
    - smirk_start.gif
    - heart.gif
    - heart_start.gif
    - blush.gif
    - battery.gif
    - wifi.gif
    - silence.gif
    - sad.gif
    - sad_start.gif
    - laugh.gif
    - laugh_start.gif
    - sleep.gif
    - starry.gif
    - starry_start.gif
    - cry.gif
    - normal.gif
    - angry.gif
    - angry_start.gif
    - listening.gif
    - startup.gif        # original (preserved); a *resize-only* (no crop)
                         # optimized copy is written to startup_resized.gif
                         # and packed into test.bin as 'startup.gif'
"""

import struct
import os
import sys
from pathlib import Path
from PIL import Image, ImageSequence

# Crop/Resize configuration
CROP_BOX = (244, 219, 780, 755)  # (left, top, right, bottom)
TARGET_SIZE = (360, 360)  # Final size after resize

# Exact list of GIF files that will be packed into test.bin (and nothing else)
EXPECTED_GIFS = [
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
    "normal.gif",
    "angry.gif",
    "angry_start.gif",
    "listening.gif",
    "startup.gif",
]

# startup.gif is treated specially: the original is preserved (it can be a
# very large source asset) and a resized copy is written to this filename,
# which is what gets packed into test.bin (under the logical name
# "startup.gif"). The resized file is also kept on disk so callers can use
# it independently.
STARTUP_GIF_NAME = "startup.gif"
STARTUP_RESIZED_NAME = "startup_resized.gif"
EXPECTED_GIF_SET = {name.lower() for name in EXPECTED_GIFS}
STARTUP_PALETTE_COLORS = 32
STARTUP_PALETTE_SAMPLE_EVERY = 15
STARTUP_DISPOSAL = 1

def get_gif_size(file_path):
    """Return the logical canvas size for a GIF, or None if it cannot be read."""
    try:
        with Image.open(file_path) as gif:
            return gif.size
    except Exception:
        return None

def build_startup_palette(input_path):
    """Build one shared palette for startup.gif after resize."""
    gif = Image.open(input_path)
    samples = []
    try:
        for index, frame in enumerate(ImageSequence.Iterator(gif)):
            if index % STARTUP_PALETTE_SAMPLE_EVERY != 0:
                continue
            samples.append(
                frame.resize(TARGET_SIZE, Image.Resampling.LANCZOS).convert('RGB')
            )

        if not samples:
            return None

        sample_sheet = Image.new(
            'RGB',
            (TARGET_SIZE[0] * len(samples), TARGET_SIZE[1])
        )
        for index, sample in enumerate(samples):
            sample_sheet.paste(sample, (TARGET_SIZE[0] * index, 0))

        return sample_sheet.quantize(
            colors=STARTUP_PALETTE_COLORS,
            method=Image.Quantize.MEDIANCUT,
            dither=Image.Dither.NONE,
        )
    finally:
        gif.close()

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

        if gif.size == TARGET_SIZE:
            print(f"[OK] Already {TARGET_SIZE[0]}x{TARGET_SIZE[1]}, keeping: "
                  f"{os.path.basename(input_path)}")
            gif.close()
            return True

        if gif.size[0] < CROP_BOX[2] or gif.size[1] < CROP_BOX[3]:
            print(f"[ERROR] {os.path.basename(input_path)} is {gif.size}, "
                  f"too small for crop box {CROP_BOX}")
            gif.close()
            return False
        
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
            gif.close()
            print(f"[OK] Cropped and resized: {os.path.basename(input_path)}")
            return True
        else:
            gif.close()
            print(f"[ERROR] No frames found in {input_path}")
            return False
            
    except Exception as e:
        print(f"[ERROR] Error processing {input_path}: {str(e)}")
        return False

def resize_gif(input_path, output_path):
    """
    Resize-only path used for startup.gif: no crop is applied, frames are
    just scaled to TARGET_SIZE and the output is re-encoded with GIF
    optimization so the result is actually smaller than the source.

    The input is often a heavily delta-compressed GIF. To avoid turning the
    resized startup into hundreds of full-size frames, this uses one shared
    palette and preserves do-not-dispose frame semantics so Pillow can emit
    compact deltas.
    """
    try:
        palette = build_startup_palette(input_path)
        gif = Image.open(input_path)
        frames = []
        durations = []

        for frame in ImageSequence.Iterator(gif):
            resized_frame = frame.resize(TARGET_SIZE, Image.Resampling.LANCZOS)
            if palette is not None:
                resized_frame = resized_frame.convert('RGB').quantize(
                    palette=palette,
                    dither=Image.Dither.NONE,
                )
            elif resized_frame.mode != 'P':
                resized_frame = resized_frame.convert(
                    'P',
                    palette=Image.ADAPTIVE,
                    colors=STARTUP_PALETTE_COLORS,
                    dither=Image.Dither.NONE,
                )
            frames.append(resized_frame.copy())

            if 'duration' in frame.info:
                durations.append(frame.info['duration'])
            else:
                durations.append(100)

        if not frames:
            print(f"[ERROR] No frames found in {input_path}")
            return False

        save_kwargs = {
            'save_all': True,
            'append_images': frames[1:],
            'duration': durations,
            'loop': gif.info.get('loop', 0),
            'optimize': True,
            'disposal': STARTUP_DISPOSAL,
        }
        if 'transparency' in gif.info:
            save_kwargs['transparency'] = gif.info['transparency']

        frames[0].save(output_path, **save_kwargs)

        try:
            in_size = os.path.getsize(input_path)
            out_size = os.path.getsize(output_path)
            arrow = "<=" if out_size <= in_size else ">"
            print(f"[OK] Resized (no crop) + optimized: "
                  f"{os.path.basename(input_path)} -> {os.path.basename(output_path)} "
                  f"({in_size:,} {arrow} {out_size:,} bytes)")
        except OSError:
            print(f"[OK] Resized (no crop) + optimized: "
                  f"{os.path.basename(input_path)} -> {os.path.basename(output_path)}")
        return True

    except Exception as e:
        print(f"[ERROR] Error processing {input_path}: {str(e)}")
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
    
    # Process only the logical bundle inputs. Generated helper files such as
    # startup_resized.gif must never be cropped on a later run.
    all_gif_files = [f for f in os.listdir(gif_folder) if f.lower().endswith('.gif')]
    gif_files = [name for name in EXPECTED_GIFS if (gif_folder / name).exists()]
    ignored_gifs = sorted(
        f for f in all_gif_files if f.lower() not in EXPECTED_GIF_SET
    )
    
    if not gif_files:
        print(f"No GIF files found in '{gif_folder}'")
        return False
    
    print(f"Found {len(gif_files)} expected GIF file(s) to crop/resize...")
    if ignored_gifs:
        print("Ignoring generated/extra GIF file(s): "
              f"{', '.join(ignored_gifs)}")
    print(f"Crop region: {CROP_BOX}, then resize to {TARGET_SIZE}\n")
    
    success_count = 0
    # Process each GIF
    for gif_file in sorted(gif_files):
        input_path = gif_folder / gif_file
        # startup.gif is special: only resize (no crop), write to a
        # separate file so the (possibly very large) original is preserved,
        # and re-encode with GIF optimization so the output is smaller.
        if gif_file.lower() == STARTUP_GIF_NAME:
            output_path = gif_folder / STARTUP_RESIZED_NAME
            if resize_gif(input_path, output_path):
                success_count += 1
            continue

        # All other GIFs: crop + resize, overwriting the source in place.
        output_path = gif_folder / gif_file
        if crop_gif(input_path, output_path):
            success_count += 1
    
    print(f"\n[OK] Completed processing {success_count}/{len(gif_files)} GIF file(s)\n")
    return success_count == len(gif_files)

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
    
    # Find all GIF files (must match EXPECTED_GIFS exactly)
    found_gifs = {}
    missing_gifs = []

    # Check for all expected GIFs.
    # startup.gif is always packed from startup_resized.gif so the bundle gets
    # a 360x360 resize-only startup asset, never the multi-MB original.
    for gif_name in EXPECTED_GIFS:
        if gif_name == STARTUP_GIF_NAME:
            resized = gif_folder / STARTUP_RESIZED_NAME
            original = gif_folder / STARTUP_GIF_NAME
            if not original.exists():
                missing_gifs.append(gif_name)
                continue

            resized_size = get_gif_size(resized) if resized.exists() else None
            original_mtime = original.stat().st_mtime
            resized_mtime = resized.stat().st_mtime if resized.exists() else 0
            if resized_size != TARGET_SIZE or resized_mtime < original_mtime:
                if resized.exists() and resized_size != TARGET_SIZE:
                    print(f"Regenerating {STARTUP_RESIZED_NAME}: "
                          f"current size is {resized_size}, target is {TARGET_SIZE}")
                elif resized.exists():
                    print(f"Regenerating {STARTUP_RESIZED_NAME}: startup.gif is newer")
                else:
                    print(f"Creating {STARTUP_RESIZED_NAME} from startup.gif")

                if not resize_gif(original, resized):
                    print(f"Error: Failed to create {STARTUP_RESIZED_NAME}")
                    return False

            found_gifs[gif_name] = str(resized)
            continue

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
        print(f"Error: Missing required GIF files: {', '.join(missing_gifs)}")
        print("Aborting test.bin creation because the set must be complete.")
        return False
    
    print(f"Found {len(found_gifs)} GIF file(s) to pack:")
    for name in found_gifs.keys():
        size = os.path.getsize(found_gifs[name])
        print(f"  - {name}: {size:,} bytes")
    
    # Pack files strictly in EXPECTED_GIFS order
    file_table = bytearray()
    data_section = bytearray()
    current_offset = 0
    file_info_list = []
    
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

                print(f"[OK] Packed {gif_name} ({file_size:,} bytes)")
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
    print(f"[OK] Created test.bin: {output_path}")
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
        print(f"Expected GIF files ({len(EXPECTED_GIFS)} total, all required):")
        for name in EXPECTED_GIFS:
            if name == STARTUP_GIF_NAME:
                print(f"  - {name}  (resized to {STARTUP_RESIZED_NAME}; original preserved)")
            else:
                print(f"  - {name}")
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
