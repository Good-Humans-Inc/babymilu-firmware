#!/usr/bin/env python3
"""
Script to crop and resize all GIFs in gif_folder/.
Crop region: (285, 266) to (739, 720), then resize to 360x360
"""

import os
from PIL import Image, ImageSequence

# Configuration
GIF_FOLDER = "gif_folder"
CROP_BOX = (285, 266, 739, 720)  # (left, top, right, bottom)
TARGET_SIZE = (360, 360)  # Final size after resize

def crop_gif(input_path, output_path):
    """
    Crop an animated GIF to the specified region.
    
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
            print(f"✓ Cropped and resized: {os.path.basename(input_path)} -> {os.path.basename(output_path)}")
        else:
            print(f"✗ Error: No frames found in {input_path}")
            
    except Exception as e:
        print(f"✗ Error processing {input_path}: {str(e)}")

def main():
    """Main function to process all GIFs in the folder."""
    if not os.path.exists(GIF_FOLDER):
        print(f"Error: Folder '{GIF_FOLDER}' not found!")
        return
    
    # Get all GIF files
    gif_files = [f for f in os.listdir(GIF_FOLDER) if f.lower().endswith('.gif')]
    
    if not gif_files:
        print(f"No GIF files found in '{GIF_FOLDER}'")
        return
    
    print(f"Found {len(gif_files)} GIF file(s) to process...")
    print(f"Crop region: {CROP_BOX}, then resize to {TARGET_SIZE}\n")
    
    # Process each GIF
    for gif_file in sorted(gif_files):
        input_path = os.path.join(GIF_FOLDER, gif_file)
        output_path = os.path.join(GIF_FOLDER, gif_file)  # Overwrite original
        crop_gif(input_path, output_path)
    
    print(f"\n✓ Completed processing {len(gif_files)} GIF file(s)")

if __name__ == "__main__":
    main()

