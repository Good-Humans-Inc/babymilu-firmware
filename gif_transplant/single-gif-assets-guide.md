# Complete Guide: Creating and Displaying a Single GIF in assets.bin

This comprehensive guide walks through the entire process of creating a minimal `assets.bin` file containing a single GIF, and explains how it's stored, loaded, decoded, and displayed on the EchoEar device.

## Table of Contents

1. [Overview](#overview)
2. [assets.bin File Format](#assetsbin-file-format)
3. [Creating a Single-GIF assets.bin](#creating-a-single-gif-assetsbin)
4. [Complete Flow: From Creation to Display](#complete-flow-from-creation-to-display)
5. [Step-by-Step Implementation](#step-by-step-implementation)
6. [Code Examples](#code-examples)
7. [Troubleshooting](#troubleshooting)

---

## Overview

The `assets.bin` file is a custom binary format that packages multiple assets (GIFs, fonts, models, etc.) into a single file that can be flashed to a dedicated partition on the ESP32. For EchoEar devices using `EmoteDisplay`, GIFs are loaded from this partition and rendered using the ESP-IDF graphics library.

### Key Components

- **assets.bin**: Binary file containing all assets
- **assets partition**: Flash partition where assets.bin is stored
- **Assets class**: C++ class that manages asset loading
- **EmoteDisplay**: Display system that renders GIFs
- **gfx library**: ESP-IDF graphics library that decodes and renders GIFs

---

## assets.bin File Format

The `assets.bin` file has the following binary structure:

```
┌─────────────────────────────────────────────────────────────┐
│ Header (12 bytes)                                           │
├─────────────────────────────────────────────────────────────┤
│ Offset  │ Size │ Description                                │
│ 0x00    │ 4    │ Total number of files (uint32_t)          │
│ 0x04    │ 4    │ Checksum (uint32_t, sum of all bytes)     │
│ 0x08    │ 4    │ Combined data length (uint32_t)           │
├─────────────────────────────────────────────────────────────┤
│ File Table (N × 44 bytes per file)                          │
├─────────────────────────────────────────────────────────────┤
│ For each file:                                              │
│ 0x00    │ 32   │ File name (null-padded, max 32 bytes)    │
│ 0x20    │ 4    │ File size (uint32_t, little-endian)       │
│ 0x24    │ 4    │ File offset in data section (uint32_t)    │
│ 0x28    │ 2    │ Width (uint16_t, for images)               │
│ 0x2A    │ 2    │ Height (uint16_t, for images)              │
├─────────────────────────────────────────────────────────────┤
│ Data Section                                                │
├─────────────────────────────────────────────────────────────┤
│ For each file:                                              │
│ 0x00    │ 2    │ Magic bytes: 0x5A 0x5A                    │
│ 0x02    │ N    │ Actual file data                           │
└─────────────────────────────────────────────────────────────┘
```

### Checksum Calculation

The checksum is calculated as:
```python
checksum = sum(all_bytes_in_combined_data) & 0xFFFF
```

Where `combined_data` = `file_table + data_section` (everything after the 12-byte header).

---

## Creating a Single-GIF assets.bin

### Prerequisites

- Python 3.6+
- A GIF file (e.g., `my_animation.gif`)
- Basic understanding of binary file formats

### Method 1: Using Python Script (Recommended)

Create a script `create_single_gif_assets.py`:

```python
#!/usr/bin/env python3
"""
Create a minimal assets.bin file containing a single GIF file.
"""

import struct
import os
import json

def compute_checksum(data):
    """Calculate checksum as sum of all bytes, masked to 16 bits."""
    return sum(data) & 0xFFFF

def create_single_gif_assets(gif_path, output_path, gif_name="my_gif.gif"):
    """
    Create a minimal assets.bin file with a single GIF.
    
    Args:
        gif_path: Path to the input GIF file
        output_path: Path where assets.bin will be written
        gif_name: Name of the GIF file in the assets (max 32 chars)
    """
    
    # Read GIF file
    with open(gif_path, 'rb') as f:
        gif_data = f.read()
    
    gif_size = len(gif_data)
    
    # File table entry (44 bytes per file)
    # Format: name(32) + size(4) + offset(4) + width(2) + height(2)
    max_name_len = 32
    file_name = gif_name[:max_name_len].ljust(max_name_len, '\0')
    file_table = bytearray()
    file_table.extend(file_name.encode('utf-8'))
    file_table.extend(struct.pack('<I', gif_size))  # File size
    file_table.extend(struct.pack('<I', 0))          # Offset (0, since it's first file)
    file_table.extend(struct.pack('<H', 0))         # Width (0 for GIFs, not used)
    file_table.extend(struct.pack('<H', 0))          # Height (0 for GIFs, not used)
    
    # Data section: magic bytes (0x5A5A) + GIF data
    data_section = bytearray()
    data_section.extend(b'\x5A\x5A')  # Magic bytes
    data_section.extend(gif_data)
    
    # Combined data = file_table + data_section
    combined_data = file_table + data_section
    combined_length = len(combined_data)
    
    # Calculate checksum
    checksum = compute_checksum(combined_data)
    
    # Header: file_count(4) + checksum(4) + length(4)
    header = struct.pack('<I', 1)  # 1 file
    header += struct.pack('<I', checksum)
    header += struct.pack('<I', combined_length)
    
    # Final file = header + combined_data
    final_data = header + combined_data
    
    # Write to file
    with open(output_path, 'wb') as f:
        f.write(final_data)
    
    print(f"✓ Created assets.bin: {output_path}")
    print(f"  - File size: {len(final_data)} bytes")
    print(f"  - GIF size: {gif_size} bytes")
    print(f"  - Checksum: 0x{checksum:04X}")

def create_index_json(gif_name, output_dir):
    """
    Create index.json for EmoteDisplay configuration.
    
    Args:
        gif_name: Name of the GIF file
        output_dir: Directory where index.json will be created
    """
    index_data = {
        "version": 1,
        "emoji_collection": [
            {
                "name": "happy",
                "file": gif_name,
                "eaf": {
                    "fps": 20,
                    "loop": True,
                    "lack": False
                }
            }
        ]
    }
    
    json_path = os.path.join(output_dir, "index.json")
    with open(json_path, 'w') as f:
        json.dump(index_data, f, indent=2)
    
    print(f"✓ Created index.json: {json_path}")
    return json_path

if __name__ == "__main__":
    import sys
    
    if len(sys.argv) < 3:
        print("Usage: python create_single_gif_assets.py <gif_file> <output_assets.bin>")
        print("Example: python create_single_gif_assets.py animation.gif assets.bin")
        sys.exit(1)
    
    gif_path = sys.argv[1]
    output_path = sys.argv[2]
    
    if not os.path.exists(gif_path):
        print(f"Error: GIF file not found: {gif_path}")
        sys.exit(1)
    
    gif_name = os.path.basename(gif_path)
    create_single_gif_assets(gif_path, output_path, gif_name)
    
    # Also create index.json in the same directory
    output_dir = os.path.dirname(output_path) or "."
    create_index_json(gif_name, output_dir)
```

### Method 2: Manual Binary Creation

For educational purposes, here's how to create it manually:

1. **Prepare the GIF file**: Ensure your GIF is optimized and ready
2. **Calculate offsets**: 
   - Header: 12 bytes
   - File table: 44 bytes (for 1 file)
   - Data section starts at offset: 12 + 44 = 56 bytes
3. **Build the file**:
   - Write header (file count, checksum, length)
   - Write file table entry
   - Write data section (0x5A5A + GIF data)
   - Calculate and update checksum

---

## Complete Flow: From Creation to Display

### Phase 1: Asset Storage (Flash Partition)

```
┌─────────────────────────────────────────────────────────┐
│ 1. assets.bin created by script                        │
│    └─> Contains: Header + File Table + GIF Data        │
├─────────────────────────────────────────────────────────┤
│ 2. Flashed to "assets" partition via esptool          │
│    └─> Partition type: ESP_PARTITION_TYPE_ANY          │
│    └─> Partition subtype: ESP_PARTITION_SUBTYPE_ANY    │
│    └─> Partition label: "assets"                      │
└─────────────────────────────────────────────────────────┘
```

**Flash Command:**
```bash
esptool.py --chip esp32s3 --port COM3 write_flash 0xNNNNNN assets.bin
```
(Replace `0xNNNNNN` with your actual partition offset)

### Phase 2: Asset Loading (Runtime)

```
┌─────────────────────────────────────────────────────────┐
│ Assets::InitializePartition()                            │
│                                                          │
│ 1. Find "assets" partition                              │
│    esp_partition_find_first(..., "assets")              │
│                                                          │
│ 2. Memory-map partition (no copy!)                      │
│    esp_partition_mmap(partition, 0, size, ...)          │
│    └─> Returns: mmap_root_ pointer                      │
│                                                          │
│ 3. Parse file table                                      │
│    └─> Read header (file count, checksum, length)      │
│    └─> Validate checksum                                │
│    └─> Build assets_ map: name -> {offset, size}        │
└─────────────────────────────────────────────────────────┘
```

**Key Code Location:** `main/assets.cc:47-105`

### Phase 3: Asset Application (Configuration)

```
┌─────────────────────────────────────────────────────────┐
│ Assets::Apply()                                         │
│                                                          │
│ 1. Load index.json from assets.bin                      │
│    GetAssetData("index.json", ptr, size)                │
│                                                          │
│ 2. Parse JSON configuration                             │
│    └─> Extract emoji_collection array                   │
│    └─> For each emoji entry:                            │
│        ├─> name: "happy"                                │
│        ├─> file: "my_gif.gif"                           │
│        └─> eaf: {fps: 20, loop: true, lack: false}    │
│                                                          │
│ 3. Load GIF data (still memory-mapped, no copy!)        │
│    GetAssetData("my_gif.gif", ptr, size)                │
│    └─> Returns pointer to mmap_root_ + offset + 2       │
│        (skips 0x5A5A magic bytes)                        │
│                                                          │
│ 4. Register with EmoteDisplay                           │
│    emote_display->AddEmojiData(                          │
│        "happy", ptr, size, fps, loop, lack)            │
│    └─> Stores in emoji_data_map_                        │
└─────────────────────────────────────────────────────────┘
```

**Key Code Locations:**
- `main/assets.cc:107-383` - Assets::Apply()
- `main/assets.cc:289-327` - Emoji collection processing
- `main/display/emote_display.cc:530-541` - AddEmojiData()

### Phase 4: GIF Rendering (Display)

```
┌─────────────────────────────────────────────────────────┐
│ Application calls: display->SetEmotion("happy")        │
│                                                          │
│ 1. EmoteDisplay::SetEmotion("happy")                    │
│    └─> Retrieves AssetData from emoji_data_map_         │
│    └─> Extracts fps, loop settings                      │
│    └─> Calls: engine_->SetEyes("happy", loop, fps)     │
│                                                          │
│ 2. EmoteEngine::SetEyes()                               │
│    └─> Gets GIF data pointer (still from mmap!)        │
│    └─> Calls: gfx_anim_set_src(anim_obj, data, size)    │
│    └─> Sets animation parameters:                      │
│        ├─> Segment: 0 to 0xFFFF (all frames)           │
│        ├─> FPS: 20 (from config)                        │
│        └─> Loop: true (from config)                      │
│    └─> Starts animation: gfx_anim_start()              │
│                                                          │
│ 3. gfx Library (ESP-IDF Graphics)                       │
│    └─> Decodes GIF frames (LZW decompression)           │
│    └─> Renders frames at specified FPS                 │
│    └─> Calls flush callback for each frame              │
│                                                          │
│ 4. EmoteEngine::OnFlush()                               │
│    └─> Receives pixel data (x, y, width, height)       │
│    └─> Calls: esp_lcd_panel_draw_bitmap()              │
│        └─> Sends pixels to ST77916 LCD panel           │
│                                                          │
│ 5. Display Hardware                                     │
│    └─> ST77916 QSPI LCD receives pixel data            │
│    └─> Updates 360x360 circular display                 │
└─────────────────────────────────────────────────────────┘
```

**Key Code Locations:**
- `main/display/emote_display.cc:393-415` - SetEmotion()
- `main/display/emote_display.cc:310-332` - SetEyes()
- `main/display/emote_display.cc:372-379` - OnFlush()

---

## Step-by-Step Implementation

### Step 1: Prepare Your GIF

1. **Optimize your GIF:**
   ```bash
   # Using ImageMagick (optional)
   convert input.gif -resize 200x200 -colors 256 output.gif
   ```

2. **Verify GIF format:**
   - Ensure it's a valid GIF89a or GIF87a file
   - Check file size (keep it reasonable for embedded systems)

### Step 2: Create assets.bin

```bash
# Using the Python script
python create_single_gif_assets.py my_animation.gif assets.bin

# This creates:
# - assets.bin (binary file)
# - index.json (configuration file)
```

### Step 3: Create index.json

The `index.json` file tells the system how to use your GIF:

```json
{
  "version": 1,
  "emoji_collection": [
    {
      "name": "happy",
      "file": "my_animation.gif",
      "eaf": {
        "fps": 20,
        "loop": true,
        "lack": false
      }
    }
  ]
}
```

**Fields:**
- `name`: Emotion name (used in `SetEmotion("happy")`)
- `file`: GIF filename in assets.bin
- `eaf.fps`: Frames per second (0-63)
- `eaf.loop`: Whether to loop animation
- `eaf.lack`: Whether animation lacks frames (usually false)

### Step 4: Include index.json in assets.bin

Modify the script to include `index.json`:

```python
def create_assets_with_index(gif_path, index_json_path, output_path):
    """Create assets.bin with both GIF and index.json."""
    
    files_to_pack = [
        ("index.json", index_json_path),
        (os.path.basename(gif_path), gif_path)
    ]
    
    # Build file table and data section for all files
    # ... (extend the script to handle multiple files)
```

### Step 5: Flash assets.bin to Device

1. **Find partition offset:**
   ```bash
   # Check partition table
   idf.py partition-table
   ```

2. **Flash the file:**
   ```bash
   esptool.py --chip esp32s3 --port COM3 \
       write_flash <partition_offset> assets.bin
   ```

### Step 6: Verify in Code

Add logging to verify loading:

```cpp
// In your application initialization
auto& assets = Assets::GetInstance();
if (assets.partition_valid()) {
    ESP_LOGI("APP", "Assets partition found");
    if (assets.Apply()) {
        ESP_LOGI("APP", "Assets loaded successfully");
    }
}
```

### Step 7: Display the GIF

```cpp
// In your application code
auto display = Board::GetInstance().GetDisplay();
auto emote_display = dynamic_cast<emote::EmoteDisplay*>(display);

if (emote_display) {
    // Display the GIF
    emote_display->SetEmotion("happy");
}
```

---

## Code Examples

### Example 1: Complete Python Script

```python
#!/usr/bin/env python3
"""
Complete script to create assets.bin with a single GIF and index.json
"""

import struct
import os
import json
import sys

def compute_checksum(data):
    return sum(data) & 0xFFFF

def pack_file(file_name, file_path, offset, max_name_len=32):
    """Pack a single file into the assets format."""
    with open(file_path, 'rb') as f:
        file_data = f.read()
    
    file_size = len(file_data)
    
    # File table entry
    name_padded = file_name[:max_name_len].ljust(max_name_len, '\0')
    table_entry = bytearray()
    table_entry.extend(name_padded.encode('utf-8'))
    table_entry.extend(struct.pack('<I', file_size))
    table_entry.extend(struct.pack('<I', offset))
    table_entry.extend(struct.pack('<H', 0))  # width
    table_entry.extend(struct.pack('<H', 0))  # height
    
    # Data entry (with magic bytes)
    data_entry = bytearray()
    data_entry = b'\x5A\x5A' + file_data
    
    return table_entry, data_entry, file_size

def create_assets_bin(files_dict, output_path):
    """
    Create assets.bin from a dictionary of {name: path} files.
    
    Args:
        files_dict: Dictionary mapping asset names to file paths
        output_path: Output path for assets.bin
    """
    file_table = bytearray()
    data_section = bytearray()
    current_offset = 0
    
    # Pack each file
    for file_name, file_path in files_dict.items():
        if not os.path.exists(file_path):
            raise FileNotFoundError(f"File not found: {file_path}")
        
        table_entry, data_entry, file_size = pack_file(
            file_name, file_path, current_offset
        )
        
        file_table.extend(table_entry)
        data_section.extend(data_entry)
        current_offset += len(data_entry)
    
    # Combine file table and data section
    combined_data = file_table + data_section
    combined_length = len(combined_data)
    checksum = compute_checksum(combined_data)
    
    # Create header
    num_files = len(files_dict)
    header = struct.pack('<I', num_files)  # file count
    header += struct.pack('<I', checksum)   # checksum
    header += struct.pack('<I', combined_length)  # length
    
    # Write final file
    final_data = header + combined_data
    
    with open(output_path, 'wb') as f:
        f.write(final_data)
    
    print(f"✓ Created assets.bin: {output_path}")
    print(f"  - Files: {num_files}")
    print(f"  - Total size: {len(final_data)} bytes")
    print(f"  - Checksum: 0x{checksum:04X}")
    
    return output_path

def create_index_json(gif_name, emotion_name="happy", fps=20, loop=True, output_path="index.json"):
    """Create index.json for EmoteDisplay."""
    index_data = {
        "version": 1,
        "emoji_collection": [
            {
                "name": emotion_name,
                "file": gif_name,
                "eaf": {
                    "fps": fps,
                    "loop": loop,
                    "lack": False
                }
            }
        ]
    }
    
    with open(output_path, 'w') as f:
        json.dump(index_data, f, indent=2)
    
    print(f"✓ Created index.json: {output_path}")
    return output_path

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python create_assets.py <gif_file> <output_assets.bin> [emotion_name]")
        print("Example: python create_assets.py animation.gif assets.bin happy")
        sys.exit(1)
    
    gif_path = sys.argv[1]
    output_path = sys.argv[2]
    emotion_name = sys.argv[3] if len(sys.argv) > 3 else "happy"
    
    gif_name = os.path.basename(gif_path)
    
    # Create index.json
    index_path = os.path.join(os.path.dirname(output_path) or ".", "index.json")
    create_index_json(gif_name, emotion_name)
    
    # Create assets.bin with both index.json and GIF
    files_to_pack = {
        "index.json": index_path,
        gif_name: gif_path
    }
    
    create_assets_bin(files_to_pack, output_path)
    print("\n✓ Done! Flash assets.bin to your device's assets partition.")
```

### Example 2: C++ Code to Verify Asset Loading

```cpp
// Add this to your application initialization code

#include "assets.h"
#include "board.h"
#include "display.h"
#include "emote_display.h"

void VerifyAndDisplayGif() {
    auto& assets = Assets::GetInstance();
    
    // Check if partition is valid
    if (!assets.partition_valid()) {
        ESP_LOGE("APP", "Assets partition not found!");
        return;
    }
    
    ESP_LOGI("APP", "Assets partition found, applying...");
    
    // Apply assets (loads index.json and registers GIFs)
    if (!assets.Apply()) {
        ESP_LOGE("APP", "Failed to apply assets!");
        return;
    }
    
    ESP_LOGI("APP", "Assets applied successfully!");
    
    // Get display and show GIF
    auto display = Board::GetInstance().GetDisplay();
    auto emote_display = dynamic_cast<emote::EmoteDisplay*>(display);
    
    if (emote_display) {
        ESP_LOGI("APP", "Setting emotion to 'happy'...");
        emote_display->SetEmotion("happy");
    } else {
        ESP_LOGE("APP", "Display is not EmoteDisplay!");
    }
}
```

### Example 3: Reading assets.bin Structure (Debug)

```python
#!/usr/bin/env python3
"""
Debug script to read and display assets.bin structure
"""

import struct
import sys

def read_assets_bin(assets_path):
    """Read and display assets.bin structure."""
    with open(assets_path, 'rb') as f:
        data = f.read()
    
    print(f"Total file size: {len(data)} bytes\n")
    
    # Read header
    file_count = struct.unpack('<I', data[0:4])[0]
    checksum = struct.unpack('<I', data[4:8])[0]
    data_length = struct.unpack('<I', data[8:12])[0]
    
    print("Header:")
    print(f"  File count: {file_count}")
    print(f"  Checksum: 0x{checksum:04X}")
    print(f"  Data length: {data_length} bytes\n")
    
    # Read file table
    table_start = 12
    table_size = file_count * 44  # 44 bytes per entry
    data_start = table_start + table_size
    
    print("File Table:")
    for i in range(file_count):
        entry_start = table_start + i * 44
        entry = data[entry_start:entry_start + 44]
        
        name = entry[0:32].rstrip(b'\x00').decode('utf-8', errors='ignore')
        size = struct.unpack('<I', entry[32:36])[0]
        offset = struct.unpack('<I', entry[36:40])[0]
        width = struct.unpack('<H', entry[40:42])[0]
        height = struct.unpack('<H', entry[42:44])[0]
        
        print(f"  [{i}] {name}")
        print(f"      Size: {size} bytes")
        print(f"      Offset: {offset} (0x{offset:06X})")
        print(f"      Dimensions: {width}x{height}")
        
        # Verify magic bytes
        magic = data[data_start + offset:data_start + offset + 2]
        if magic == b'\x5A\x5A':
            print(f"      ✓ Magic bytes OK")
        else:
            print(f"      ✗ Magic bytes invalid: {magic.hex()}")
        print()
    
    # Verify checksum
    combined_data = data[12:]
    calculated_checksum = sum(combined_data) & 0xFFFF
    print(f"Checksum verification:")
    print(f"  Stored: 0x{checksum:04X}")
    print(f"  Calculated: 0x{calculated_checksum:04X}")
    if checksum == calculated_checksum:
        print(f"  ✓ Checksum valid")
    else:
        print(f"  ✗ Checksum mismatch!")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python read_assets.py <assets.bin>")
        sys.exit(1)
    
    read_assets_bin(sys.argv[1])
```

---

## Troubleshooting

### Issue: GIF Not Displaying

**Symptoms:** No animation appears on screen

**Solutions:**
1. **Check partition:**
   ```cpp
   if (!assets.partition_valid()) {
       ESP_LOGE("APP", "Partition not found!");
   }
   ```

2. **Verify asset loading:**
   ```cpp
   void* ptr = nullptr;
   size_t size = 0;
   if (assets.GetAssetData("my_gif.gif", ptr, size)) {
       ESP_LOGI("APP", "GIF found: %d bytes", size);
   } else {
       ESP_LOGE("APP", "GIF not found!");
   }
   ```

3. **Check index.json:**
   - Ensure `index.json` is in assets.bin
   - Verify JSON syntax is valid
   - Check emotion name matches `SetEmotion()` call

### Issue: Checksum Mismatch

**Symptoms:** `checksum_valid_` is false

**Solutions:**
1. Recreate assets.bin with correct checksum
2. Verify file wasn't corrupted during flash
3. Check partition size matches file size

### Issue: Wrong GIF Displayed

**Symptoms:** Different GIF appears than expected

**Solutions:**
1. Verify emotion name in `SetEmotion()` matches `index.json`
2. Check file name in `index.json` matches actual file in assets.bin
3. Clear and reload assets

### Issue: GIF Too Large

**Symptoms:** Out of memory errors

**Solutions:**
1. Optimize GIF (reduce colors, frames, size)
2. Use compression tools
3. Consider splitting into multiple smaller GIFs

### Debugging Tips

1. **Enable verbose logging:**
   ```cpp
   esp_log_level_set("Assets", ESP_LOG_DEBUG);
   esp_log_level_set("EmoteDisplay", ESP_LOG_DEBUG);
   ```

2. **Verify memory mapping:**
   ```cpp
   ESP_LOGI("APP", "Mmap root: %p", mmap_root_);
   ESP_LOGI("APP", "Partition size: %lu", partition_->size);
   ```

3. **Check GIF data:**
   ```cpp
   void* gif_ptr = nullptr;
   size_t gif_size = 0;
   if (assets.GetAssetData("my_gif.gif", gif_ptr, gif_size)) {
       // Check GIF header
       const char* header = (const char*)gif_ptr;
       if (header[0] == 'G' && header[1] == 'I' && header[2] == 'F') {
           ESP_LOGI("APP", "Valid GIF header");
       }
   }
   ```

---

## Summary

This guide covered the complete journey of a GIF from creation to display:

1. **Creation**: Python script creates `assets.bin` with proper binary format
2. **Storage**: File is flashed to "assets" partition in flash memory
3. **Loading**: `Assets::InitializePartition()` memory-maps the partition
4. **Configuration**: `Assets::Apply()` parses `index.json` and registers GIFs
5. **Rendering**: `EmoteDisplay::SetEmotion()` triggers GIF animation
6. **Display**: gfx library decodes and renders frames to LCD panel

The key insight is that **no data is copied** - everything works with pointers to the memory-mapped flash partition, making it very memory-efficient for embedded systems.

---

## Additional Resources

- **Assets Class**: `main/assets.cc`, `main/assets.h`
- **EmoteDisplay**: `main/display/emote_display.cc`, `main/display/emote_display.h`
- **Asset Packing Script**: `scripts/spiffs_assets/spiffs_assets_gen.py`
- **EchoEar Board**: `main/boards/echoear/EchoEar.cc`

---

*Last updated: 2025*

