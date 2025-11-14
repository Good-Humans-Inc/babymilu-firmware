#!/usr/bin/env python3
"""
Mega Animation Merger - Create One Huge Animation File
Merges ALL animation frames into a single mega .bin file for ultimate optimization.

This script creates a single binary file containing all animations:
- Normal (3 frames) - from normal_all.bin or individual files
- Embarrass (3 frames)
- Fire (4 frames) 
- Happy (4 frames)
- Inspiration (4 frames)
- Question (4 frames)
- Shy (2 frames)
- Sleep (4 frames)

Total: 28 frames in one file!

Usage:
    python create_mega_animations.py input_dir/ output_mega.bin
    python create_mega_animations.py input_dir/ output_mega.bin --size 256 256
"""

import sys
import os
import glob
import struct
import argparse
import re
from pathlib import Path
from enum import Enum

try:
    from PIL import Image
except ImportError:
    print("Error: PIL (Pillow) is required but not installed.")
    print("Please install it with: pip install Pillow")
    sys.exit(1)

class ColorFormat(Enum):
    RGB565 = 0x12
    RGB565A8 = 0x14
    RGB888 = 0x0F
    ARGB8888 = 0x10
    XRGB8888 = 0x11
    ARGB8565 = 0x13
    L8 = 0x06
    I1 = 0x07
    I2 = 0x08
    I4 = 0x09
    I8 = 0x0A
    A1 = 0x0B
    A2 = 0x0C
    A4 = 0x0D
    A8 = 0x0E
    OVERLAY_PIXELS = 0x4F50584C  # "OPXL" - sparse overlay pixel payload

LVGL_MAGIC = 0x4C56474C
OVERLAY_ENTRY_SIZE_BYTES = 6  # uint16 x, uint16 y, uint16 color

def get_default_overlay_header_path():
    """Return default path to overlay_pixels.h relative to repo root."""
    script_dir = Path(__file__).resolve().parent
    return script_dir.parent / "main" / "display" / "overlay_pixels.h"

def load_overlay_pixels(header_path):
    """Parse overlay pixel data from overlay_pixels.h"""
    path = Path(header_path)
    if not path.exists():
        print(f"Warning: Overlay header not found at {path}")
        return []
    
    overlay_pixels = []
    pattern = re.compile(r"\{(\d+)\s*,\s*(\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\}")
    
    try:
        with path.open("r", encoding="utf-8") as f:
            for line in f:
                match = pattern.search(line)
                if not match:
                    continue
                x = int(match.group(1))
                y = int(match.group(2))
                color_str = match.group(3)
                color = int(color_str, 16) if color_str.lower().startswith("0x") else int(color_str)
                overlay_pixels.append((x, y, color))
    except Exception as exc:
        print(f"Warning: Failed to parse overlay header {path}: {exc}")
        return []
    
    if overlay_pixels:
        print(f"Loaded {len(overlay_pixels)} overlay pixels from {path}")
    else:
        print(f"Warning: No overlay pixels found in {path}")
    return overlay_pixels

class LVGLImage:
    def __init__(self):
        self.width = 0
        self.height = 0
        self.color_format = ColorFormat.RGB565
        self.data = b''
        self.stride = 0
        
    def from_image(self, image_path, target_size=(256, 256), force_format=None):
        """Load image and convert to LVGL format"""
        with Image.open(image_path) as img:
            # Resize to target size
            img = img.resize(target_size, Image.Resampling.LANCZOS)
            self.width, self.height = img.size
            
            # Determine color format
            if force_format:
                self.color_format = force_format
            else:
                # Auto-detect color format based on image mode
                has_alpha = img.mode in ('RGBA', 'LA') or 'transparency' in img.info
                self.color_format = ColorFormat.RGB565A8 if has_alpha else ColorFormat.RGB565
            
            # Convert image based on color format
            if self.color_format == ColorFormat.RGB565A8:
                img = img.convert('RGBA')
                self.stride = self.width * 3  # 2 bytes RGB + 1 byte alpha
                self.data = self._convert_to_rgb565a8(img)
            else:
                img = img.convert('RGB')
                self.stride = self.width * 2  # 2 bytes per pixel
                self.data = self._convert_to_rgb565(img)
            
        return self
    
    def _convert_to_rgb565(self, img):
        """Convert PIL image to RGB565 format"""
        data = []
        for y in range(self.height):
            for x in range(self.width):
                r, g, b = img.getpixel((x, y))
                # Convert to RGB565
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                data.extend([rgb565 & 0xFF, (rgb565 >> 8) & 0xFF])
        return bytes(data)
    
    def _convert_to_rgb565a8(self, img):
        """Convert PIL image to RGB565A8 format"""
        data = []
        for y in range(self.height):
            for x in range(self.width):
                r, g, b, a = img.getpixel((x, y))
                # Convert to RGB565
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                data.extend([rgb565 & 0xFF, (rgb565 >> 8) & 0xFF, a])
        return bytes(data)
    
    def to_binary_frame(self):
        """Convert to binary frame data (header + pixel data)"""
        # Pack header
        header = struct.pack('<IIIIII', 
            LVGL_MAGIC,               # magic
            self.color_format.value,  # color_format
            0,                        # flags
            self.width,               # w
            self.height,              # h
            self.stride               # stride
        )
        
        return header + self.data

class AnimationSet:
    def __init__(self, name, frame_count, merged_file=None, individual_pattern=None,
                 min_individual_files=None, overlay_config=None):
        self.name = name
        self.frame_count = frame_count
        self.merged_file = merged_file
        self.individual_pattern = individual_pattern
        self.frames = []
        self.min_individual_files = min_individual_files if min_individual_files is not None else frame_count
        self.overlay_config = overlay_config
    
    def load_from_directory(self, input_dir, target_size=(256, 256), force_format=None):
        """Load animation frames from directory"""
        print(f"\n--- Loading {self.name} Animation ({self.frame_count} frames) ---")
        self.frames = []
        
        # Try merged file first
        if self.merged_file:
            merged_path = os.path.join(input_dir, self.merged_file)
            if os.path.exists(merged_path):
                print(f"  Found merged file: {self.merged_file}")
                if self._load_from_merged_file(merged_path):
                    return self._finalize_load()
                else:
                    print(f"  Failed to load merged file, trying individual files...")
        
        # Try individual files
        if self.individual_pattern:
            print(f"  Looking for individual files: {self.individual_pattern}")
            individual_files = glob.glob(os.path.join(input_dir, self.individual_pattern))
            individual_files.sort()
            
            if len(individual_files) >= self.frame_count:
                print(f"  Found {len(individual_files)} individual files")
                if self._load_from_individual_files(individual_files[:self.frame_count], target_size, force_format):
                    return self._finalize_load()
            elif len(individual_files) >= self.min_individual_files:
                print(f"  Found {len(individual_files)} files (minimum required: {self.min_individual_files})")
                if self._load_from_individual_files(individual_files[:self.min_individual_files], target_size, force_format):
                    return self._finalize_load()
            else:
                print(f"  Found only {len(individual_files)} files, need {self.frame_count}")
        
        print(f"  ❌ Failed to load {self.name} animation")
        return False
    
    def _load_from_merged_file(self, merged_path):
        """Load frames from an existing merged file"""
        try:
            with open(merged_path, 'rb') as f:
                for frame_idx in range(self.frame_count):
                    # Read header
                    header_bytes = f.read(24)  # 6 * 4 bytes
                    if len(header_bytes) != 24:
                        print(f"    ❌ Frame {frame_idx}: Incomplete header")
                        return False
                    
                    # Unpack header
                    magic, cf, flags, width, height, stride = struct.unpack('<IIIIII', header_bytes)
                    
                    # Validate magic
                    if magic != 0x4C56474C:
                        print(f"    ❌ Frame {frame_idx}: Invalid magic number")
                        return False
                    
                    # Read pixel data
                    data_size = height * stride
                    pixel_data = f.read(data_size)
                    if len(pixel_data) != data_size:
                        print(f"    ❌ Frame {frame_idx}: Incomplete pixel data")
                        return False
                    
                    # Create frame data
                    frame_data = header_bytes + pixel_data
                    self.frames.append(frame_data)
                    
                    print(f"    ✅ Frame {frame_idx}: {width}x{height}, {data_size} bytes")
                
                return True
                
        except Exception as e:
            print(f"    ❌ Error loading merged file: {e}")
            return False
    
    def _finalize_load(self):
        """Apply any post-processing (like overlays) and validate frame count"""
        if not self._apply_overlay_config():
            return False
        
        if len(self.frames) != self.frame_count:
            print(f"  ❌ {self.name}: Expected {self.frame_count} frames, got {len(self.frames)}")
            return False
        
        return True
    
    def _apply_overlay_config(self):
        """Apply overlay configuration if provided"""
        if not self.overlay_config:
            return True
        
        overlay_type = self.overlay_config.get("type")
        if overlay_type == "overlay_pixels":
            return self._generate_overlay_frames_from_pixels()
        
        print(f"  ⚠️  Unknown overlay type '{overlay_type}' for {self.name}")
        return False
    
    def _generate_overlay_frames_from_pixels(self):
        """Use sparse overlay pixels to build additional frames"""
        pixels = self.overlay_config.get("pixels")
        target_frames = self.overlay_config.get("target_frames", [])
        base_index = self.overlay_config.get("base_frame_index", 0)
        
        if not pixels:
            print(f"  ⚠️  No overlay pixels provided for {self.name}")
            return False
        
        if not target_frames:
            print(f"  ⚠️  No target frames specified for {self.name} overlay")
            return False
        
        if base_index >= len(self.frames):
            print(f"  ⚠️  Base frame index {base_index} unavailable for {self.name}")
            return False
        
        base_frame = self.frames[base_index]
        if len(base_frame) < 24:
            print(f"  ⚠️  Base frame data too small for {self.name}")
            return False
        
        _, cf, _, width, height, stride = struct.unpack('<IIIIII', base_frame[:24])
        
        if cf != ColorFormat.RGB565.value:
            print(f"  ⚠️  Unsupported color format {cf} for overlay in {self.name}")
            return False
        
        if width == 0 or height == 0 or stride == 0:
            print(f"  ⚠️  Invalid dimensions ({width}x{height}) for {self.name}")
            return False
        
        for frame_idx in target_frames:
            entry_count = 0
            overlay_payload = bytearray()
            
            for x, y, color in pixels:
                if x >= width or y >= height:
                    continue
                overlay_payload.extend(struct.pack('<HHH', x, y, color & 0xFFFF))
                entry_count += 1
            
            stride_bytes = entry_count * OVERLAY_ENTRY_SIZE_BYTES
            overlay_header = struct.pack(
                '<IIIIII',
                LVGL_MAGIC,
                ColorFormat.OVERLAY_PIXELS.value,
                frame_idx,               # flags: target frame index for reference
                entry_count,             # width field repurposed for entry count
                1,                       # height set to 1 (not used)
                stride_bytes             # stride stores total payload bytes
            )
            
            while len(self.frames) <= frame_idx:
                self.frames.append(None)
            
            self.frames[frame_idx] = overlay_header + bytes(overlay_payload)
            print(f"    ✅ Stored {entry_count} sparse overlay pixels for frame {frame_idx}")
        
        return True
    
    def _load_from_individual_files(self, file_paths, target_size, force_format):
        """Load frames from individual image files"""
        initial_count = len(self.frames)
        for i, file_path in enumerate(file_paths):
            print(f"  Processing frame {i}: {os.path.basename(file_path)}")
            
            try:
                # Convert image to LVGL format
                lvgl_img = LVGLImage().from_image(file_path, target_size, force_format)
                
                # Convert to binary frame
                frame_data = lvgl_img.to_binary_frame()
                self.frames.append(frame_data)
                
                print(f"    ✅ Size: {lvgl_img.width}x{lvgl_img.height}")
                print(f"    ✅ Color format: {lvgl_img.color_format.name}")
                print(f"    ✅ Frame size: {len(frame_data)} bytes")
                
            except Exception as e:
                print(f"    ❌ Error processing {file_path}: {e}")
                return False
        
        expected_total = initial_count + len(file_paths)
        return len(self.frames) == expected_total
    
    def get_total_size(self):
        """Get total size of all frames"""
        return sum(len(frame) for frame in self.frames)

def create_mega_animations(input_dir, output_file, target_size=(256, 256), force_format=None,
                           overlay_pixels=None):
    """Create mega animation file with all animations"""
    
    print("=== Creating Mega Animation File ===")
    print(f"Input directory: {input_dir}")
    print(f"Output file: {output_file}")
    print(f"Target size: {target_size[0]}x{target_size[1]}")
    
    # Configure normal animation overlay usage if pixels are provided
    normal_overlay_config = None
    normal_min_files = 3
    if overlay_pixels:
        normal_overlay_config = {
            "type": "overlay_pixels",
            "pixels": overlay_pixels,
            "base_frame_index": 0,
            "target_frames": [1, 2],
        }
        normal_min_files = 1
        print("Normal animation will reuse base frame with overlay pixels for frames 2 and 3")
    
    # Define all animation sets
    animation_sets = [
        AnimationSet(
            "Normal",
            3,
            merged_file="normal_all.bin",
            individual_pattern="normal*.jpg",
            min_individual_files=normal_min_files,
            overlay_config=normal_overlay_config,
        ),
        AnimationSet("Embarrass", 3, individual_pattern="embarrass*.jpg"),
        AnimationSet("Fire", 4, individual_pattern="fire*.jpg"),
        AnimationSet("Happy", 4, individual_pattern="happy*.jpg"),
        AnimationSet("Inspiration", 4, individual_pattern="inspiration*.jpg"),
        AnimationSet("Question", 4, individual_pattern="question*.jpg"),
        AnimationSet("Shy", 2, individual_pattern="shy*.jpg"),
        AnimationSet("Sleep", 4, individual_pattern="sleep*.jpg"),
    ]
    
    # Load all animations
    all_frames = []
    total_size = 0
    successful_animations = 0
    
    for anim_set in animation_sets:
        if anim_set.load_from_directory(input_dir, target_size, force_format):
            all_frames.extend(anim_set.frames)
            anim_size = anim_set.get_total_size()
            total_size += anim_size
            successful_animations += 1
            print(f"✅ {anim_set.name}: {len(anim_set.frames)} frames, {anim_size} bytes")
        else:
            print(f"❌ {anim_set.name}: Failed to load")
    
    if successful_animations == 0:
        print("\n❌ No animations loaded successfully!")
        return False
    
    print(f"\n=== Summary ===")
    print(f"Successfully loaded: {successful_animations}/{len(animation_sets)} animations")
    print(f"Total frames: {len(all_frames)}")
    print(f"Total size: {total_size} bytes ({total_size / 1024:.1f} KB)")
    
    # Create output directory if needed
    output_dir = os.path.dirname(output_file)
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # Write mega binary file
    print(f"\nWriting mega animation file...")
    
    try:
        with open(output_file, 'wb') as f:
            for i, frame_data in enumerate(all_frames, 1):
                print(f"Writing frame {i}/{len(all_frames)} ({len(frame_data)} bytes)...")
                f.write(frame_data)
        
        # Verify output
        output_size = os.path.getsize(output_file)
        print(f"\n✅ Successfully created mega animation file: {output_file}")
        print(f"✅ Output file size: {output_size} bytes ({output_size / 1024:.1f} KB)")
        print(f"✅ Total frames: {len(all_frames)}")
        print(f"✅ Average frame size: {output_size // len(all_frames)} bytes")
        
        if output_size == total_size:
            print("✅ File size matches expected total")
        else:
            print(f"⚠️  File size mismatch: expected {total_size}, got {output_size}")
        
        return True
        
    except Exception as e:
        print(f"❌ Error writing output file: {e}")
        return False

def parse_arguments():
    """Parse command line arguments"""
    parser = argparse.ArgumentParser(
        description="Create mega animation file containing all animations",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python create_mega_animations.py images/ animations_mega.bin
  python create_mega_animations.py images/ animations_mega.bin --size 128 128
  python create_mega_animations.py images/ animations_mega.bin --format RGB565A8
        """
    )
    
    parser.add_argument('input_dir', help='Input directory containing animation files')
    parser.add_argument('output_file', help='Output mega animation binary file path')
    parser.add_argument('--size', nargs=2, type=int, default=[256, 256], 
                       metavar=('WIDTH', 'HEIGHT'),
                       help='Target image size (default: 256 256)')
    parser.add_argument('--format', choices=['RGB565', 'RGB565A8', 'RGB888', 'ARGB8888'], 
                       help='Force color format (auto-detect if not specified)')
    parser.add_argument('--overlay-header',
                        default=str(get_default_overlay_header_path()),
                        help='Path to overlay_pixels.h for deriving Normal frame overlays (default: %(default)s). '
                             'Set to "none" to disable overlay-based frames.')
    
    return parser.parse_args()

def main():
    """Main function"""
    args = parse_arguments()
    
    # Validate input directory
    if not os.path.isdir(args.input_dir):
        print(f"Error: Input directory does not exist: {args.input_dir}")
        sys.exit(1)
    
    # Validate output file extension
    if not args.output_file.endswith('.bin'):
        print("Warning: Output file should have .bin extension")
    
    # Parse color format if specified
    force_format = None
    if args.format:
        force_format = ColorFormat[args.format]
        print(f"Using forced color format: {args.format}")
    else:
        print("Using automatic color format detection")
    
    # Load overlay pixels if requested
    overlay_pixels = None
    if args.overlay_header and args.overlay_header.lower() != "none":
        overlay_pixels = load_overlay_pixels(args.overlay_header)
        if not overlay_pixels:
            overlay_pixels = None
    else:
        print("Overlay-based frame generation disabled by user request")
    
    # Create mega animations
    success = create_mega_animations(
        args.input_dir, 
        args.output_file, 
        target_size=tuple(args.size),
        force_format=force_format,
        overlay_pixels=overlay_pixels
    )
    
    if success:
        print("\n🎉 Mega animation file created successfully!")
        print(f"Ready to flash {args.output_file} to your device's SPIFFS partition.")
        print("\nNext steps:")
        print("1. Flash the mega file to SPIFFS partition")
        print("2. Update firmware to support mega file loading")
        print("3. Test the mega animation system")
    else:
        print("\n❌ Mega animation file creation failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()
