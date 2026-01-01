#!/usr/bin/env python3
"""
Debug script to read and display assets.bin structure.

This script helps verify that assets.bin was created correctly by
displaying its internal structure, file table, and verifying checksums.

Usage:
    python read_assets_bin.py <assets.bin>
"""

import struct
import sys
import os

def read_assets_bin(assets_path):
    """Read and display assets.bin structure."""
    if not os.path.exists(assets_path):
        print(f"Error: File not found: {assets_path}")
        sys.exit(1)
    
    with open(assets_path, 'rb') as f:
        data = f.read()
    
    print("=" * 60)
    print(f"Reading assets.bin: {assets_path}")
    print("=" * 60)
    print(f"Total file size: {len(data):,} bytes\n")
    
    if len(data) < 12:
        print("Error: File too small to be a valid assets.bin")
        sys.exit(1)
    
    # Read header (12 bytes)
    file_count = struct.unpack('<I', data[0:4])[0]
    checksum = struct.unpack('<I', data[4:8])[0]
    data_length = struct.unpack('<I', data[8:12])[0]
    
    print("Header (12 bytes):")
    print(f"  File count: {file_count}")
    print(f"  Checksum: 0x{checksum:04X} ({checksum})")
    print(f"  Data length: {data_length:,} bytes")
    print()
    
    # Calculate expected sizes
    table_start = 12
    table_size = file_count * 44  # 44 bytes per entry
    data_start = table_start + table_size
    expected_total = 12 + data_length
    
    print("Structure:")
    print(f"  Header: 0x0000 - 0x000B (12 bytes)")
    print(f"  File table: 0x000C - 0x{data_start-1:04X} ({table_size} bytes)")
    print(f"  Data section: 0x{data_start:04X} - 0x{len(data)-1:04X} ({len(data)-data_start:,} bytes)")
    print()
    
    if len(data) != expected_total:
        print(f"  ⚠ Warning: File size mismatch!")
        print(f"    Expected: {expected_total:,} bytes")
        print(f"    Actual: {len(data):,} bytes")
        print()
    
    # Read file table
    print("File Table:")
    print("-" * 60)
    file_info_list = []
    
    for i in range(file_count):
        entry_start = table_start + i * 44
        if entry_start + 44 > len(data):
            print(f"  Error: File table entry {i} extends beyond file!")
            break
        
        entry = data[entry_start:entry_start + 44]
        
        # Parse entry
        name_bytes = entry[0:32]
        name = name_bytes.rstrip(b'\x00').decode('utf-8', errors='ignore')
        size = struct.unpack('<I', entry[32:36])[0]
        offset = struct.unpack('<I', entry[36:40])[0]
        width = struct.unpack('<H', entry[40:42])[0]
        height = struct.unpack('<H', entry[42:44])[0]
        
        file_info_list.append((name, size, offset, width, height))
        
        print(f"  [{i}] {name}")
        print(f"      Size: {size:,} bytes")
        print(f"      Offset: {offset} (0x{offset:06X})")
        if width > 0 or height > 0:
            print(f"      Dimensions: {width}x{height}")
        
        # Verify magic bytes
        if data_start + offset + 2 <= len(data):
            magic = data[data_start + offset:data_start + offset + 2]
            if magic == b'\x5A\x5A':
                print(f"      ✓ Magic bytes: 0x5A5A")
            else:
                print(f"      ✗ Magic bytes invalid: {magic.hex()}")
        else:
            print(f"      ✗ Offset out of bounds!")
        print()
    
    # Verify checksum
    print("Checksum Verification:")
    print("-" * 60)
    calculated_checksum = 0
    if len(data) > 12:
        combined_data = data[12:12+data_length]
        calculated_checksum = sum(combined_data) & 0xFFFF
        print(f"  Stored checksum: 0x{checksum:04X} ({checksum})")
        print(f"  Calculated checksum: 0x{calculated_checksum:04X} ({calculated_checksum})")
        if checksum == calculated_checksum:
            print(f"  ✓ Checksum valid!")
        else:
            print(f"  ✗ Checksum mismatch!")
            print(f"    Difference: {abs(checksum - calculated_checksum)}")
    else:
        print("  ⚠ Cannot verify checksum (file too small)")
    print()
    
    # File summary
    print("File Summary:")
    print("-" * 60)
    total_data_size = sum(size for _, size, _, _, _ in file_info_list)
    print(f"  Total files: {file_count}")
    print(f"  Total data size: {total_data_size:,} bytes")
    print(f"  Data section size: {len(data) - data_start:,} bytes")
    print(f"  Overhead (magic bytes): {file_count * 2:,} bytes")
    print()
    
    # Check for common issues
    print("Validation:")
    print("-" * 60)
    issues = []
    
    if file_count == 0:
        issues.append("No files in assets.bin")
    
    if len(data) > 12:
        combined_data = data[12:12+data_length]
        calculated_checksum = sum(combined_data) & 0xFFFF
        if checksum != calculated_checksum:
            issues.append("Checksum mismatch")
    
    if len(data) != expected_total:
        issues.append("File size doesn't match header")
    
    for name, size, offset, _, _ in file_info_list:
        if data_start + offset + 2 + size > len(data):
            issues.append(f"File '{name}' extends beyond file bounds")
    
    if not issues:
        print("  ✓ All checks passed!")
    else:
        print("  ✗ Issues found:")
        for issue in issues:
            print(f"    - {issue}")
    print()
    
    # Detect file types
    print("File Type Detection:")
    print("-" * 60)
    for name, size, offset, _, _ in file_info_list:
        if data_start + offset + 2 <= len(data):
            file_start = data_start + offset + 2
            if file_start + min(10, size) <= len(data):
                header = data[file_start:file_start + min(10, size)]
                
                file_type = "Unknown"
                if name.endswith('.json'):
                    file_type = "JSON"
                elif name.endswith('.gif'):
                    if header[:3] == b'GIF':
                        file_type = f"GIF ({header[3:6].decode('ascii', errors='ignore')})"
                    else:
                        file_type = "GIF (invalid header)"
                elif name.endswith('.bin'):
                    file_type = "Binary"
                elif name.endswith(('.png', '.jpg', '.jpeg')):
                    file_type = "Image"
                
                print(f"  {name}: {file_type}")
                if file_type == "JSON" and size < 1000:
                    try:
                        json_data = data[file_start:file_start + size].decode('utf-8')
                        print(f"    Preview: {json_data[:50]}...")
                    except:
                        pass

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python read_assets_bin.py <assets.bin>")
        print()
        print("This script reads and displays the structure of an assets.bin file,")
        print("including file table, checksums, and validation checks.")
        sys.exit(1)
    
    read_assets_bin(sys.argv[1])

