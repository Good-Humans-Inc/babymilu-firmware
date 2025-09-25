#!/usr/bin/env python3
"""
Example usage script for the enhanced image to SPIFFS converter.

This script demonstrates how to use the new image_to_merged_spiffs.py script
with example commands and directory structure.
"""

import os
import sys
from pathlib import Path

def print_example_structure():
    """Print example directory structure"""
    print("=== Example Directory Structure ===")
    print("""
project_root/
├── images/
│   ├── normal/
│   │   ├── normal1.jpg    # Animation frame 1
│   │   ├── normal2.jpg    # Animation frame 2
│   │   └── normal3.jpg    # Animation frame 3
│   ├── happy/
│   │   ├── happy1.png     # Happy animation frames
│   │   ├── happy2.png
│   │   ├── happy3.png
│   │   └── happy4.png
│   └── fire/
│       ├── fire1.jpg      # Fire animation frames
│       ├── fire2.jpg
│       ├── fire3.jpg
│       └── fire4.jpg
├── animations/             # Output directory
│   ├── normal_all.bin     # Generated merged files
│   ├── happy_all.bin
│   └── fire_all.bin
└── scripts/
    ├── image_to_merged_spiffs.py
    └── test_merged_from_images.py
""")

def print_example_commands():
    """Print example commands"""
    print("=== Example Commands ===")
    print("""
# Convert normal animation (3 frames)
python scripts/image_to_merged_spiffs.py images/normal/ animations/normal_all.bin

# Convert happy animation (4 frames) with custom size
python scripts/image_to_merged_spiffs.py images/happy/ animations/happy_all.bin --size 128 128

# Convert fire animation with forced RGB565A8 format
python scripts/image_to_merged_spiffs.py images/fire/ animations/fire_all.bin --format RGB565A8

# Test the generated files
python scripts/test_merged_from_images.py animations/normal_all.bin --frames 3
python scripts/test_merged_from_images.py animations/happy_all.bin --frames 4
python scripts/test_merged_from_images.py animations/fire_all.bin --frames 4
""")

def print_workflow_steps():
    """Print workflow steps"""
    print("=== Complete Workflow ===")
    print("""
1. Prepare source images:
   - Place animation frames in dedicated directories
   - Use consistent naming (frame1.jpg, frame2.jpg, etc.)
   - Ensure all frames have the same dimensions

2. Convert to merged binary:
   python scripts/image_to_merged_spiffs.py images/normal/ animations/normal_all.bin

3. Test the output:
   python scripts/test_merged_from_images.py animations/normal_all.bin

4. Deploy to device:
   - Upload animations/normal_all.bin to SPIFFS partition
   - Device will automatically use merged file when available

5. Monitor device logs:
   - Look for "✅ Successfully loaded normal animation from merged file"
   - Or "Merged file not found, trying individual files..." (fallback)
""")

def print_benefits():
    """Print benefits of the new approach"""
    print("=== Benefits ===")
    print("""
✅ Simplified Workflow:
   - Single command converts multiple images to merged binary
   - No need for intermediate individual .bin files
   - No need for separate merge script

✅ Better Performance:
   - Single file operation instead of multiple
   - Reduced flash metadata overhead
   - Improved wear leveling

✅ Easier Management:
   - Upload one file instead of multiple
   - Atomic updates for entire animation set
   - Simpler deployment process

✅ Backward Compatible:
   - Falls back to individual files if merged file not found
   - No breaking changes to existing functionality
   - Gradual migration possible
""")

def main():
    """Main function"""
    print("Enhanced Image to SPIFFS Converter - Example Usage")
    print("=" * 60)
    
    print_example_structure()
    print_example_commands()
    print_workflow_steps()
    print_benefits()
    
    print("\n" + "=" * 60)
    print("Ready to start converting your animations!")
    print("Run the commands above with your actual image directories.")

if __name__ == "__main__":
    main()
