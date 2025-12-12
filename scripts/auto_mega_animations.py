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
    """Find all emotion frames grouped by emotion name (supports jpg, png, jpeg)"""
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
                
                # Extract emotion name and frame number (e.g., "embarrass1" -> "embarrass", 1)
                if base_name and base_name[-1].isdigit():
                    if len(base_name) > 1 and base_name[-2].isdigit():
                        emotion = base_name[:-2]
                        frame_num = int(base_name[-2:])
                    else:
                        emotion = base_name[:-1]
                        frame_num = int(base_name[-1])
                    
                    if emotion not in emotions:
                        emotions[emotion] = {}
                    emotions[emotion][frame_num] = img_file
    
    return emotions

def generate_overlays(emotions, overlay_dir, diff_threshold=0, target_size=None):
    """Generate overlay .h files for all emotions (resizes images if target_size provided)"""
    from PIL import Image
    import tempfile
    import shutil
    
    overlay_dir = Path(overlay_dir)
    overlay_dir.mkdir(parents=True, exist_ok=True)
    
    generated = {}
    script_path = Path(__file__).parent.parent / "images" / "frame difference" / "generate_diff_overlay.py"
    temp_dir = None
    
    try:
        # Create temp directory for resized images if needed
        if target_size:
            temp_dir = Path(tempfile.mkdtemp())
        
        for emotion, frames in emotions.items():
            if 1 not in frames:
                continue
            
            overlays = {}
            sorted_frame_nums = sorted(frames.keys())
            
            # Generate sequential overlays: frame1→frame2, frame2→frame3, frame3→frame4
            for i in range(len(sorted_frame_nums) - 1):
                frame_num_from = sorted_frame_nums[i]
                frame_num_to = sorted_frame_nums[i + 1]
                
                frame_from = frames[frame_num_from]
                frame_to = frames[frame_num_to]
                overlay_file = overlay_dir / f"{emotion}_overlay{frame_num_to}.h"
                
                # Resize frames if needed
                if target_size:
                    with Image.open(frame_from) as img:
                        original_size = img.size
                        if img.size != target_size:
                            img_resized = img.resize(target_size, Image.Resampling.LANCZOS)
                            frame_from_resized = temp_dir / f"{emotion}_frame{frame_num_from}_resized.jpg"
                            img_resized.save(frame_from_resized, "JPEG", quality=95)
                            frame_from = str(frame_from_resized)
                            print(f"    Resized {emotion} frame {frame_num_from} from {original_size} to {target_size}")
                        else:
                            print(f"    {emotion} frame {frame_num_from} already {target_size}, no resize needed")
                    
                    with Image.open(frame_to) as img:
                        original_size = img.size
                        if img.size != target_size:
                            img_resized = img.resize(target_size, Image.Resampling.LANCZOS)
                            frame_to_resized = temp_dir / f"{emotion}_frame{frame_num_to}_resized.jpg"
                            img_resized.save(frame_to_resized, "JPEG", quality=95)
                            frame_to = str(frame_to_resized)
                            print(f"    Resized {emotion} frame {frame_num_to} from {original_size} to {target_size}")
                        else:
                            print(f"    {emotion} frame {frame_num_to} already {target_size}, no resize needed")
                
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
        
    finally:
        # Clean up temp directory
        if temp_dir and temp_dir.exists():
            shutil.rmtree(temp_dir)
    
    return generated

def create_mega_bin(input_dir, output_file, size=(256, 256), format=None):
    """Call create_mega_animations.py (overlays use default paths)"""
    script_path = Path(__file__).parent / "create_mega_animations.py"
    cmd = [sys.executable, str(script_path), input_dir, output_file, "--size", str(size[0]), str(size[1])]
    
    if format:
        cmd.extend(["--format", format])
    
    print(f"\nCreating mega animation file...")
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
    
    # Generate overlays (resize images to target size first)
    overlay_dir = Path(__file__).parent.parent / "images" / "frame difference"
    print(f"\nGenerating overlay files in {overlay_dir}...")
    print(f"Resizing images to {size[0]}x{size[1]} before generating overlays...")
    generated = generate_overlays(emotions, overlay_dir, threshold, target_size=size)
    
    # Create mega bin
    success = create_mega_bin(input_dir, output_file, size, format)
    
    if success:
        print(f"\n✓ Success! Mega animation file: {output_file}")
    else:
        print(f"\n✗ Failed to create mega animation file")
        sys.exit(1)

if __name__ == "__main__":
    main()

