#!/usr/bin/env python3
"""
Debug script to help troubleshoot file detection issues.

This script shows exactly what files are being found in a directory
and helps identify why the image converter might be detecting more files than expected.
"""

import sys
import os
import glob

def debug_file_detection(input_dir):
    """Debug file detection in a directory"""
    print(f"=== Debugging File Detection in: {input_dir} ===")
    
    if not os.path.isdir(input_dir):
        print(f"❌ Directory does not exist: {input_dir}")
        return
    
    # List all files in directory
    print(f"\nAll files in directory:")
    all_files = os.listdir(input_dir)
    for i, filename in enumerate(all_files, 1):
        file_path = os.path.join(input_dir, filename)
        file_size = os.path.getsize(file_path) if os.path.isfile(file_path) else "N/A"
        print(f"  {i}. {filename} ({file_size} bytes)")
    
    # Test glob patterns
    supported_extensions = ['*.jpg', '*.jpeg', '*.png', '*.bmp', '*.gif']
    
    print(f"\nTesting glob patterns:")
    all_detected_files = []
    
    for ext in supported_extensions:
        print(f"\n--- Extension: {ext} ---")
        
        # Lowercase
        lowercase_files = glob.glob(os.path.join(input_dir, ext))
        print(f"  Lowercase ({ext}): {len(lowercase_files)} files")
        for file_path in lowercase_files:
            print(f"    - {os.path.basename(file_path)}")
        
        # Uppercase
        uppercase_files = glob.glob(os.path.join(input_dir, ext.upper()))
        print(f"  Uppercase ({ext.upper()}): {len(uppercase_files)} files")
        for file_path in uppercase_files:
            print(f"    - {os.path.basename(file_path)}")
        
        # Combine and deduplicate
        combined_files = lowercase_files + uppercase_files
        unique_files = []
        for file_path in combined_files:
            if file_path not in unique_files:
                unique_files.append(file_path)
        
        print(f"  Unique files for {ext}: {len(unique_files)}")
        for file_path in unique_files:
            print(f"    - {os.path.basename(file_path)}")
        
        all_detected_files.extend(unique_files)
    
    # Final deduplication
    print(f"\n--- Final Results ---")
    final_files = []
    for file_path in all_detected_files:
        if file_path not in final_files:
            final_files.append(file_path)
    
    print(f"Total unique image files detected: {len(final_files)}")
    for i, file_path in enumerate(final_files, 1):
        print(f"  {i}. {os.path.basename(file_path)}")
    
    # Show what the script would actually process
    final_files.sort()
    print(f"\nFiles that would be processed (sorted):")
    for i, file_path in enumerate(final_files, 1):
        print(f"  {i}. {os.path.basename(file_path)}")

def main():
    """Main function"""
    if len(sys.argv) != 2:
        print("Usage: python debug_file_detection.py <directory>")
        print("Example: python debug_file_detection.py images/normal/")
        sys.exit(1)
    
    input_dir = sys.argv[1]
    debug_file_detection(input_dir)

if __name__ == "__main__":
    main()
