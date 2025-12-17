#!/usr/bin/env python3
"""
Mega Animation Merger - Create One Huge Animation File
Merges ALL animation frames into a single mega .bin file for ultimate optimization.

This script creates a single binary file containing all animations:
- Normal (15 frames: 1 base + 14 overlays) - from normal_all.bin or individual files
- Embarrass (3 frames)
- Fire (4 frames) 
- Happy (4 frames)
- Inspiration (4 frames)
- Question (4 frames)
- Shy (2 frames)
- Sleep (4 frames)

Total: 40 frames in one file!

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

def get_default_embarrass_overlay_paths():
    """Return default paths to embarrass overlay headers relative to repo root."""
    script_dir = Path(__file__).resolve().parent
    frame_diff_dir = script_dir.parent / "images" / "frame difference"
    return {
        2: frame_diff_dir / "embarrass_overlay2.h",
        3: frame_diff_dir / "embarrass_overlay3.h"
    }

def get_default_fire_overlay_paths():
    """Return default paths to fire overlay headers relative to repo root."""
    script_dir = Path(__file__).resolve().parent
    frame_diff_dir = script_dir.parent / "images" / "frame difference"
    return {
        2: frame_diff_dir / "fire_overlay2.h",
        3: frame_diff_dir / "fire_overlay3.h",
        4: frame_diff_dir / "fire_overlay4.h"
    }

def get_default_happy_overlay_paths():
    """Return default paths to happy overlay headers relative to repo root."""
    script_dir = Path(__file__).resolve().parent
    frame_diff_dir = script_dir.parent / "images" / "frame difference"
    return {
        2: frame_diff_dir / "happy_overlay2.h",
        3: frame_diff_dir / "happy_overlay3.h",
        4: frame_diff_dir / "happy_overlay4.h"
    }

def get_default_inspiration_overlay_paths():
    """Return default paths to inspiration overlay headers relative to repo root."""
    script_dir = Path(__file__).resolve().parent
    frame_diff_dir = script_dir.parent / "images" / "frame difference"
    return {
        2: frame_diff_dir / "inspiration_overlay2.h",
        3: frame_diff_dir / "inspiration_overlay3.h",
        4: frame_diff_dir / "inspiration_overlay4.h"
    }

def get_default_question_overlay_paths():
    """Return default paths to question overlay headers relative to repo root."""
    script_dir = Path(__file__).resolve().parent
    frame_diff_dir = script_dir.parent / "images" / "frame difference"
    return {
        2: frame_diff_dir / "question_overlay2.h",
        3: frame_diff_dir / "question_overlay3.h",
        4: frame_diff_dir / "question_overlay4.h"
    }

def get_default_shy_overlay_paths():
    """Return default paths to shy overlay headers relative to repo root."""
    script_dir = Path(__file__).resolve().parent
    frame_diff_dir = script_dir.parent / "images" / "frame difference"
    return {
        2: frame_diff_dir / "shy_overlay2.h"
    }

def get_default_sleep_overlay_paths():
    """Return default paths to sleep overlay headers relative to repo root."""
    script_dir = Path(__file__).resolve().parent
    frame_diff_dir = script_dir.parent / "images" / "frame difference"
    return {
        2: frame_diff_dir / "sleep_overlay2.h",
        3: frame_diff_dir / "sleep_overlay3.h",
        4: frame_diff_dir / "sleep_overlay4.h"
    }

def get_default_normal_overlay_paths():
    """Return default paths to normal overlay headers relative to repo root.
    Supports 15 frames: 1 base frame + 14 overlay frames (overlay2 through overlay15).
    """
    script_dir = Path(__file__).resolve().parent
    frame_diff_dir = script_dir.parent / "images" / "frame difference"
    return {
        2: frame_diff_dir / "normal_overlay2.h",
        3: frame_diff_dir / "normal_overlay3.h",
        4: frame_diff_dir / "normal_overlay4.h",
        5: frame_diff_dir / "normal_overlay5.h",
        6: frame_diff_dir / "normal_overlay6.h",
        7: frame_diff_dir / "normal_overlay7.h",
        8: frame_diff_dir / "normal_overlay8.h",
        9: frame_diff_dir / "normal_overlay9.h",
        10: frame_diff_dir / "normal_overlay10.h",
        11: frame_diff_dir / "normal_overlay11.h",
        12: frame_diff_dir / "normal_overlay12.h",
        13: frame_diff_dir / "normal_overlay13.h",
        14: frame_diff_dir / "normal_overlay14.h",
        15: frame_diff_dir / "normal_overlay15.h"
    }

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

def load_embarrass_overlays(overlay_paths):
    """Load embarrass overlay pixels from multiple header files.
    
    Args:
        overlay_paths: Dict mapping frame index (2, 3) to header file path
        
    Returns:
        Dict mapping frame index to list of (x, y, color) tuples
    """
    overlays = {}
    for frame_idx, path in overlay_paths.items():
        pixels = load_overlay_pixels(path)
        if pixels:
            overlays[frame_idx] = pixels
    return overlays

def load_animation_overlays(overlay_paths):
    """Load animation overlay pixels from multiple header files (for fire, happy, inspiration).
    
    Args:
        overlay_paths: Dict mapping frame index (2, 3, 4) to header file path
        
    Returns:
        Dict mapping frame index to list of (x, y, color) tuples
    """
    overlays = {}
    for frame_idx, path in overlay_paths.items():
        pixels = load_overlay_pixels(path)
        if pixels:
            overlays[frame_idx] = pixels
    
    # Special case for happy: if overlay4 is missing/empty, reuse overlay3
    # This happens when happy3 and happy4 are the same image
    if 4 not in overlays and 3 in overlays:
        overlays[4] = overlays[3]
        print(f"Note: Reusing overlay3 for overlay4 (frames 3 and 4 are identical)")
    
    return overlays

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
            # Support both .jpg and .png extensions
            individual_files = []
            # Extract base pattern (e.g., "normal*" from "normal*.jpg")
            if '.' in self.individual_pattern:
                # Remove extension, keep the * wildcard
                base_pattern = self.individual_pattern.rsplit('.', 1)[0]
            else:
                base_pattern = self.individual_pattern
            
            # Try all image extensions
            for ext in ['.jpg', '.png', '.jpeg', '.JPG', '.PNG', '.JPEG']:
                pattern = base_pattern + ext
                found = glob.glob(os.path.join(input_dir, pattern))
                individual_files.extend(found)
            
            individual_files = sorted(set(individual_files))  # Remove duplicates and sort
            if individual_files:
                print(f"  Found {len(individual_files)} files: {[os.path.basename(f) for f in individual_files[:5]]}{'...' if len(individual_files) > 5 else ''}")
            
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
        """Use sparse overlay pixels to build additional frames sequentially"""
        pixels = self.overlay_config.get("pixels")
        target_frames = self.overlay_config.get("target_frames", [])
        base_index = self.overlay_config.get("base_frame_index", 0)
        
        # Support both single pixels list (for normal) and dict of pixels per frame (for embarrass)
        if isinstance(pixels, dict):
            # Dict format: {frame_idx: [(x, y, color), ...], ...}
            pixels_dict = pixels
        else:
            # List format: [(x, y, color), ...] - same pixels for all target frames
            pixels_dict = {frame_idx: pixels for frame_idx in target_frames}
        
        if not pixels_dict:
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
        
        # Apply overlays sequentially: each overlay builds on the previous frame
        # Frame 2 = Frame 1 + overlay2, Frame 3 = Frame 2 + overlay3, etc.
        sorted_target_frames = sorted(target_frames)
        current_base_index = base_index
        
        for frame_idx in sorted_target_frames:
            frame_pixels = pixels_dict.get(frame_idx, [])
            
            # Verify current base frame exists
            if current_base_index >= len(self.frames) or self.frames[current_base_index] is None:
                print(f"  ⚠️  Base frame {current_base_index} unavailable for frame {frame_idx} in {self.name}")
                continue
            
            # If no overlay pixels, reuse the previous overlay frame (or base frame if it's the first)
            # Previous behavior (commented out): skip frame and leave original full image

            '''if not frame_pixels:
                print(f"  ⚠️  No overlay pixels for frame {frame_idx} in {self.name}")
                continue'''
            if not frame_pixels:
                print(f"  ⚠️  No overlay pixels for frame {frame_idx} in {self.name}, reusing frame {current_base_index}")
                # Create an overlay frame with 0 pixels pointing to the previous frame (reuses it)
                entry_count = 0
                overlay_payload = bytearray()
            else:
                entry_count = 0
                overlay_payload = bytearray()
                
                for x, y, color in frame_pixels:
                    if x >= width or y >= height:
                        continue
                    overlay_payload.extend(struct.pack('<HHH', x, y, color & 0xFFFF))
                    entry_count += 1
            
            stride_bytes = entry_count * OVERLAY_ENTRY_SIZE_BYTES
            overlay_header = struct.pack(
                '<IIIIII',
                LVGL_MAGIC,
                ColorFormat.OVERLAY_PIXELS.value,
                current_base_index,      # flags: base frame index for sequential application
                entry_count,             # width field repurposed for entry count
                1,                       # height set to 1 (not used)
                stride_bytes             # stride stores total payload bytes
            )
            
            while len(self.frames) <= frame_idx:
                self.frames.append(None)
            
            self.frames[frame_idx] = overlay_header + bytes(overlay_payload)
            if entry_count > 0:
                print(f"    ✅ Stored {entry_count} sparse overlay pixels for frame {frame_idx} (applied on frame {current_base_index})")
            else:
                print(f"    ✅ Created frame {frame_idx} reusing frame {current_base_index} (no overlay pixels)")
            
            # Next overlay builds on this frame
            current_base_index = frame_idx
        
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
                           normal_overlays=None, embarrass_overlays=None, fire_overlays=None,
                           happy_overlays=None, inspiration_overlays=None, question_overlays=None,
                           shy_overlays=None, sleep_overlays=None):
    """Create mega animation file with all animations"""
    
    print("=== Creating Mega Animation File ===")
    print(f"Input directory: {input_dir}")
    print(f"Output file: {output_file}")
    print(f"Target size: {target_size[0]}x{target_size[1]}")
    
    # Configure normal animation overlay usage if overlays are provided
    normal_overlay_config = None
    normal_min_files = 15
    if normal_overlays:
        # Convert frame numbers (2-15) to frame indices (1-14)
        normal_overlays_indices = {idx - 1: pixels for idx, pixels in normal_overlays.items() if idx in range(2, 16)}
        normal_overlay_config = {
            "type": "overlay_pixels",
            "pixels": normal_overlays_indices,  # Dict format: {1: pixels, 2: pixels, ..., 14: pixels}
            "base_frame_index": 0,
            "target_frames": list(range(1, 15)),  # Frame indices 1-14 (normal2 through normal15)
        }
        normal_min_files = 1
        print(f"Normal animation will reuse base frame with overlay pixels for frames 2-15 ({len(normal_overlays_indices)} overlays)")
    
    # Configure embarrass animation overlay usage if overlays are provided
    embarrass_overlay_config = None
    embarrass_min_files = 3
    if embarrass_overlays:
        # Convert frame numbers (2, 3) to frame indices (1, 2)
        embarrass_overlays_indices = {idx - 1: pixels for idx, pixels in embarrass_overlays.items() if idx in [2, 3]}
        embarrass_overlay_config = {
            "type": "overlay_pixels",
            "pixels": embarrass_overlays_indices,  # Dict format: {1: pixels, 2: pixels}
            "base_frame_index": 0,
            "target_frames": [1, 2],  # Frame indices 1 and 2 (embarrass2 and embarrass3)
        }
        embarrass_min_files = 1
        print("Embarrass animation will reuse base frame with overlay pixels for frames 2 and 3")
    
    # Configure fire animation overlay usage if overlays are provided
    fire_overlay_config = None
    fire_min_files = 4
    if fire_overlays:
        # Convert frame indices 2,3,4 to animation frame indices 1,2,3
        fire_overlays_indices = {idx - 1: pixels for idx, pixels in fire_overlays.items() if idx in [2, 3, 4]}
        fire_overlay_config = {
            "type": "overlay_pixels",
            "pixels": fire_overlays_indices,  # Dict format: {1: pixels, 2: pixels, 3: pixels}
            "base_frame_index": 0,
            "target_frames": [1, 2, 3],  # Frame indices 1, 2, 3 (fire2, fire3, fire4)
        }
        fire_min_files = 1
        print("Fire animation will reuse base frame with overlay pixels for frames 2, 3, and 4")
    
    # Configure happy animation overlay usage if overlays are provided
    happy_overlay_config = None
    happy_min_files = 4
    if happy_overlays:
        # Convert frame indices 2,3,4 to animation frame indices 1,2,3
        happy_overlays_indices = {idx - 1: pixels for idx, pixels in happy_overlays.items() if idx in [2, 3, 4]}
        happy_overlay_config = {
            "type": "overlay_pixels",
            "pixels": happy_overlays_indices,  # Dict format: {1: pixels, 2: pixels, 3: pixels}
            "base_frame_index": 0,
            "target_frames": [1, 2, 3],  # Frame indices 1, 2, 3 (happy2, happy3, happy4)
        }
        happy_min_files = 1
        print("Happy animation will reuse base frame with overlay pixels for frames 2, 3, and 4")
    
    # Configure inspiration animation overlay usage if overlays are provided
    inspiration_overlay_config = None
    inspiration_min_files = 4
    if inspiration_overlays:
        # Convert frame indices 2,3,4 to animation frame indices 1,2,3
        inspiration_overlays_indices = {idx - 1: pixels for idx, pixels in inspiration_overlays.items() if idx in [2, 3, 4]}
        inspiration_overlay_config = {
            "type": "overlay_pixels",
            "pixels": inspiration_overlays_indices,  # Dict format: {1: pixels, 2: pixels, 3: pixels}
            "base_frame_index": 0,
            "target_frames": [1, 2, 3],  # Frame indices 1, 2, 3 (inspiration2, inspiration3, inspiration4)
        }
        inspiration_min_files = 1
        print("Inspiration animation will reuse base frame with overlay pixels for frames 2, 3, and 4")
    
    # Configure question animation overlay usage if overlays are provided
    question_overlay_config = None
    question_min_files = 4
    if question_overlays:
        question_overlays_indices = {idx - 1: pixels for idx, pixels in question_overlays.items() if idx in [2, 3, 4]}
        question_overlay_config = {
            "type": "overlay_pixels",
            "pixels": question_overlays_indices,
            "base_frame_index": 0,
            "target_frames": [1, 2, 3],
        }
        question_min_files = 1
        print("Question animation will reuse base frame with overlay pixels for frames 2, 3, and 4")
    
    # Configure shy animation overlay usage if overlays are provided
    shy_overlay_config = None
    shy_min_files = 2
    if shy_overlays:
        shy_overlays_indices = {idx - 1: pixels for idx, pixels in shy_overlays.items() if idx in [2]}
        shy_overlay_config = {
            "type": "overlay_pixels",
            "pixels": shy_overlays_indices,
            "base_frame_index": 0,
            "target_frames": [1],
        }
        shy_min_files = 1
        print("Shy animation will reuse base frame with overlay pixels for frame 2")
    
    # Configure sleep animation overlay usage if overlays are provided
    sleep_overlay_config = None
    sleep_min_files = 4
    if sleep_overlays:
        sleep_overlays_indices = {idx - 1: pixels for idx, pixels in sleep_overlays.items() if idx in [2, 3, 4]}
        sleep_overlay_config = {
            "type": "overlay_pixels",
            "pixels": sleep_overlays_indices,
            "base_frame_index": 0,
            "target_frames": [1, 2, 3],
        }
        sleep_min_files = 1
        print("Sleep animation will reuse base frame with overlay pixels for frames 2, 3, and 4")
    
    # Define all animation sets (supports both .jpg and .png)
    animation_sets = [
        AnimationSet(
            "Normal",
            15,  # 1 base frame + 14 overlay frames
            merged_file="normal_all.bin",
            individual_pattern="normal*.jpg",  # Pattern used for glob, but we search multiple extensions
            min_individual_files=normal_min_files,
            overlay_config=normal_overlay_config,
        ),
        AnimationSet(
            "Embarrass",
            3,
            individual_pattern="embarrass*.jpg",
            min_individual_files=embarrass_min_files,
            overlay_config=embarrass_overlay_config,
        ),
        AnimationSet("Fire", 4, individual_pattern="fire*.jpg", min_individual_files=fire_min_files, overlay_config=fire_overlay_config),
        AnimationSet("Happy", 4, individual_pattern="happy*.jpg", min_individual_files=happy_min_files, overlay_config=happy_overlay_config),
        AnimationSet("Inspiration", 4, individual_pattern="inspiration*.jpg", min_individual_files=inspiration_min_files, overlay_config=inspiration_overlay_config),
        AnimationSet("Question", 4, individual_pattern="question*.jpg", min_individual_files=question_min_files, overlay_config=question_overlay_config),
        AnimationSet("Shy", 2, individual_pattern="shy*.jpg", min_individual_files=shy_min_files, overlay_config=shy_overlay_config),
        AnimationSet("Sleep", 4, individual_pattern="sleep*.jpg", min_individual_files=sleep_min_files, overlay_config=sleep_overlay_config),
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
    parser.add_argument('--normal-overlay2',
                        default=None,
                        help='Path to normal_overlay2.h (default: images/frame difference/normal_overlay2.h). '
                             'Set to "none" to disable normal overlay frames.')
    parser.add_argument('--normal-overlay3',
                        default=None,
                        help='Path to normal_overlay3.h (default: images/frame difference/normal_overlay3.h). '
                             'Set to "none" to disable normal overlay frames.')
    parser.add_argument('--embarrass-overlay2',
                        default=None,
                        help='Path to embarrass_overlay2.h (default: images/frame difference/embarrass_overlay2.h). '
                             'Set to "none" to disable embarrass overlay frames.')
    parser.add_argument('--embarrass-overlay3',
                        default=None,
                        help='Path to embarrass_overlay3.h (default: images/frame difference/embarrass_overlay3.h). '
                             'Set to "none" to disable embarrass overlay frames.')
    
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
    
    # Load normal overlays if requested (supports frames 2-15)
    normal_overlays = None
    default_normal_paths = get_default_normal_overlay_paths()
    
    # Check if overlay2/overlay3 are explicitly disabled via command line
    overlay2_path = args.normal_overlay2 if args.normal_overlay2 is not None else str(default_normal_paths[2])
    overlay3_path = args.normal_overlay3 if args.normal_overlay3 is not None else str(default_normal_paths[3])
    
    # Load all normal overlays (2-15) from default paths
    # Only disable if overlay2 or overlay3 are explicitly set to "none"
    if overlay2_path.lower() != "none" and overlay3_path.lower() != "none":
        normal_overlays = {}
        # Load overlays 2-15 from default paths
        for frame_num in range(2, 16):
            overlay_path = default_normal_paths[frame_num]
            pixels = load_overlay_pixels(overlay_path)
            if pixels:
                normal_overlays[frame_num] = pixels
            else:
                print(f"Warning: normal_overlay{frame_num}.h not found or empty, skipping frame {frame_num}")
        
        if not normal_overlays:
            normal_overlays = None
            print("Warning: No normal overlay files found, will use individual image files instead")
        else:
            print(f"Loaded {len(normal_overlays)} normal overlay files (frames {min(normal_overlays.keys())}-{max(normal_overlays.keys())})")
    else:
        print("Normal overlay-based frame generation disabled by user request")
    
    # Load embarrass overlays if requested
    embarrass_overlays = None
    default_embarrass_paths = get_default_embarrass_overlay_paths()
    
    overlay2_path = args.embarrass_overlay2 if args.embarrass_overlay2 is not None else str(default_embarrass_paths[2])
    overlay3_path = args.embarrass_overlay3 if args.embarrass_overlay3 is not None else str(default_embarrass_paths[3])
    
    if overlay2_path.lower() != "none" and overlay3_path.lower() != "none":
        embarrass_overlays = {}
        if overlay2_path.lower() != "none":
            pixels2 = load_overlay_pixels(overlay2_path)
            if pixels2:
                embarrass_overlays[2] = pixels2  # Frame number 2 (embarrass2)
        if overlay3_path.lower() != "none":
            pixels3 = load_overlay_pixels(overlay3_path)
            if pixels3:
                embarrass_overlays[3] = pixels3  # Frame number 3 (embarrass3)
        
        if not embarrass_overlays:
            embarrass_overlays = None
    else:
        print("Embarrass overlay-based frame generation disabled by user request")
    
    # Load fire overlays (default paths)
    fire_overlays = load_animation_overlays(get_default_fire_overlay_paths())
    if not fire_overlays:
        fire_overlays = None
    
    # Load happy overlays (default paths)
    happy_overlays = load_animation_overlays(get_default_happy_overlay_paths())
    if not happy_overlays:
        happy_overlays = None
    
    # Load inspiration overlays (default paths)
    inspiration_overlays = load_animation_overlays(get_default_inspiration_overlay_paths())
    if not inspiration_overlays:
        inspiration_overlays = None
    
    # Load question overlays (default paths)
    question_overlays = load_animation_overlays(get_default_question_overlay_paths())
    if not question_overlays:
        question_overlays = None
    
    # Load shy overlays (default paths)
    shy_overlays = load_animation_overlays(get_default_shy_overlay_paths())
    if not shy_overlays:
        shy_overlays = None
    
    # Load sleep overlays (default paths)
    sleep_overlays = load_animation_overlays(get_default_sleep_overlay_paths())
    if not sleep_overlays:
        sleep_overlays = None
    
    # Create mega animations
    success = create_mega_animations(
        args.input_dir, 
        args.output_file, 
        target_size=tuple(args.size),
        force_format=force_format,
        normal_overlays=normal_overlays,
        embarrass_overlays=embarrass_overlays,
        fire_overlays=fire_overlays,
        happy_overlays=happy_overlays,
        inspiration_overlays=inspiration_overlays,
        question_overlays=question_overlays,
        shy_overlays=shy_overlays,
        sleep_overlays=sleep_overlays
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
