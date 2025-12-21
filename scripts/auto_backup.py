#!/usr/bin/env python3
"""
Automated mega animation generator.
Automatically generates overlay files and creates mega .bin in one step.
"""

import sys
import os
import glob
import subprocess
from pathlib import Path

def find_emotion_frames(input_dir):
    """Find all emotion frames grouped by emotion name (supports jpg, png, jpeg)
    Supports frame numbers up to 99 (e.g., normal1, normal15, normal99)
    """
    emotions = {}
    # Supported image extensions
    extensions = ['*.jpg', '*.jpeg', '*.png', '*.JPG', '*.JPEG', '*.PNG']
    
    # Try flat directory first, then subdirectories
    for ext in extensions:
        patterns = [
            os.path.join(input_dir, f"*[0-9]{ext[1:]}"),  # Remove * from extension
            os.path.join(input_dir, "*", f"*[0-9]{ext[1:]}")
        ]
        
        for pattern in patterns:
            for img_file in glob.glob(pattern):
                file_name = os.path.basename(img_file)
                base_name = file_name.rsplit('.', 1)[0]
                
                # Extract emotion name and frame number (e.g., "normal1" -> "normal", 1)
                # Supports up to 2-digit frame numbers (1-99)
                if base_name and base_name[-1].isdigit():
                    # Check if last 2 characters are digits (for frames 10-99)
                    if len(base_name) > 1 and base_name[-2].isdigit():
                        emotion = base_name[:-2]
                        frame_num = int(base_name[-2:])
                    else:
                        # Single digit frame (1-9)
                        emotion = base_name[:-1]
                        frame_num = int(base_name[-1])
                    
                    if emotion not in emotions:
                        emotions[emotion] = {}
                    emotions[emotion][frame_num] = img_file
    
    return emotions

def resize_and_convert_to_rgb(emotions, target_size):
    """Resize all PNG images and convert them to RGB mode (for RGB565 processing)"""
    from PIL import Image
    import tempfile
    import shutil
    
    temp_dir = None
    resized_files = {}
    
    try:
        if target_size:
            temp_dir = Path(tempfile.mkdtemp())
            print(f"\nStep 1: Resizing and converting all images to RGB mode...")
            
            for emotion, frames in emotions.items():
                resized_files[emotion] = {}
                
                for frame_num, img_path in frames.items():
                    try:
                        with Image.open(img_path) as img:
                            original_size = img.size
                            original_mode = img.mode
                            
                            # Resize if needed
                            if img.size != target_size:
                                img_resized = img.resize(target_size, Image.Resampling.LANCZOS)
                            else:
                                img_resized = img.copy()
                            
                            # Convert to RGB mode (removes alpha, ensures RGB565 compatibility)
                            if img_resized.mode != 'RGB':
                                if img_resized.mode == 'RGBA':
                                    # Convert RGBA to RGB (alpha channel removed)
                                    img_resized = img_resized.convert('RGB')
                                elif img_resized.mode in ('LA', 'P'):
                                    # Convert other modes with potential alpha to RGB
                                    img_resized = img_resized.convert('RGB')
                                else:
                                    # Convert any other mode to RGB
                                    img_resized = img_resized.convert('RGB')
                            
                            # Save resized RGB image with original base name (for create_mega_animations.py to find)
                            # e.g., normal1.png, normal2.png, etc.
                            original_name = Path(img_path).name
                            base_name = original_name.rsplit('.', 1)[0]  # Remove extension
                            resized_path = temp_dir / f"{base_name}.png"
                            img_resized.save(resized_path, "PNG")
                            resized_files[emotion][frame_num] = str(resized_path)
                            
                            if original_size != target_size or original_mode != 'RGB':
                                print(f"  {emotion}{frame_num}: {original_size} ({original_mode}) → {target_size} (RGB)")
                            else:
                                print(f"  {emotion}{frame_num}: Already {target_size} (RGB)")
                    
                    except Exception as e:
                        print(f"  ✗ Error processing {emotion}{frame_num}: {e}")
                        # Keep original path if resize fails
                        resized_files[emotion][frame_num] = img_path
            
            print(f"✓ All images resized and converted to RGB mode")
            return resized_files, temp_dir
        else:
            # No resizing needed, return original paths
            return {emotion: frames.copy() for emotion, frames in emotions.items()}, None
    
    except Exception as e:
        print(f"✗ Error in resize_and_convert_to_rgb: {e}")
        if temp_dir and temp_dir.exists():
            shutil.rmtree(temp_dir)
        return {emotion: frames.copy() for emotion, frames in emotions.items()}, None

def generate_overlays(emotions, overlay_dir, diff_threshold=0, target_size=None, resized_files=None):
    """Generate overlay .h files for all emotions (uses pre-resized RGB images)"""
    overlay_dir = Path(overlay_dir)
    overlay_dir.mkdir(parents=True, exist_ok=True)
    
    generated = {}
    script_path = Path(__file__).parent.parent / "images" / "frame difference" / "generate_diff_overlay.py"
    
    # Use resized files if provided, otherwise use original files
    files_to_use = resized_files if resized_files else emotions
    
    print(f"\nStep 2: Generating overlay files...")
    for emotion, frames in files_to_use.items():
        if 1 not in frames:
            continue
        
        overlays = {}
        sorted_frame_nums = sorted(frames.keys())
        
        # Generate overlays: frame1→frame2, frame1→frame3, ..., frame1→frameN
        # Supports variable number of frames: 1 base frame + N overlay frames
        frame_num_from = sorted_frame_nums[0]  # Always use frame 1 as the base
        for i in range(1, len(sorted_frame_nums)):
            frame_num_to = sorted_frame_nums[i]
            
            frame_from = frames[frame_num_from]
            frame_to = frames[frame_num_to]
            overlay_file = overlay_dir / f"{emotion}_overlay{frame_num_to}.h"
            
            print(f"Generating {emotion} overlay{frame_num_to}.h (frame {frame_num_from}→{frame_num_to})...")
            result = subprocess.run([
                sys.executable, str(script_path),
                frame_from, frame_to, str(overlay_file), str(diff_threshold)
            ], capture_output=True, text=True)
            
            if result.returncode == 0:
                overlays[frame_num_to] = str(overlay_file)
                print(f"  ✓ Generated {overlay_file.name}")
            else:
                print(f"  ✗ Failed: {result.stderr}")
        
        if overlays:
            generated[emotion] = overlays
    
    return generated

def create_mega_bin(input_dir, output_file, size=(256, 256), format=None):
    """Call create_mega_animations.py (overlays use default paths)"""
    script_path = Path(__file__).parent / "create_mega_animations.py"
    cmd = [sys.executable, str(script_path), input_dir, output_file, "--size", str(size[0]), str(size[1])]
    
    if format:
        cmd.extend(["--format", format])
    
    print(f"\nStep 3: Creating mega animation file from RGB images...")
    result = subprocess.run(cmd)
    return result.returncode == 0

def main():
    if len(sys.argv) < 3:
        print("Usage: python auto_mega_animations.py <input_dir> <output.bin> [--size W H] [--format FORMAT] [--threshold N]")
        sys.exit(1)
    
    input_dir = sys.argv[1]
    output_file = sys.argv[2]
    size = (256, 256)
    format = None
    threshold = 0
    
    # Parse optional args
    i = 3
    while i < len(sys.argv):
        if sys.argv[i] == "--size" and i + 2 < len(sys.argv):
            size = (int(sys.argv[i+1]), int(sys.argv[i+2]))
            i += 3
        elif sys.argv[i] == "--format" and i + 1 < len(sys.argv):
            format = sys.argv[i+1]
            i += 2
        elif sys.argv[i] == "--threshold" and i + 1 < len(sys.argv):
            threshold = int(sys.argv[i+1])
            i += 2
        else:
            i += 1
    
    if not os.path.isdir(input_dir):
        print(f"Error: Input directory does not exist: {input_dir}")
        sys.exit(1)
    
    # Find all emotion frames
    print("Scanning for emotion frames...")
    emotions = find_emotion_frames(input_dir)
    print(f"Found {len(emotions)} emotions: {', '.join(emotions.keys())}")
    
    # Step 1: Resize all PNGs and convert to RGB mode (for RGB565)
    resized_files, temp_dir = resize_and_convert_to_rgb(emotions, size)
    
    # Step 2: Generate overlays using resized RGB images
    overlay_dir = Path(__file__).parent.parent / "images" / "frame difference"
    generated = generate_overlays(emotions, overlay_dir, threshold, target_size=size, resized_files=resized_files)
    
    # Step 3: Create mega bin using resized RGB images
    # Use temp_dir if we resized images, otherwise use original input_dir
    input_dir_for_bin = str(temp_dir) if temp_dir else input_dir
    
    try:
        success = create_mega_bin(input_dir_for_bin, output_file, size, format)
        
        if success:
            print(f"\n✓ Success! Mega animation file: {output_file}")
        else:
            print(f"\n✗ Failed to create mega animation file")
            sys.exit(1)
    finally:
        # Clean up temp directory
        if temp_dir and temp_dir.exists():
            import shutil
            shutil.rmtree(temp_dir)
            print(f"Cleaned up temporary directory")

if __name__ == "__main__":
    main()

