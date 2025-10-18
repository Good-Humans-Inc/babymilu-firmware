# Minimal SD Card Driver for ESP32

This is a minimal SD card driver that only initializes the SD card and mounts the FAT filesystem.

## Files Included:
- `SD_MMC.h` - Header file with pin definitions
- `SD_MMC.c` - SD card initialization implementation
- `CMakeLists.txt` - Component configuration

## Installation:

1. Copy the entire `SD_Card_Minimal` folder to your project's `main` directory or as a component in `components/` directory

2. Add to your `main/CMakeLists.txt`:
```cmake
idf_component_register(SRCS 
                       "your_file.c"
                       INCLUDE_DIRS 
                       "."
                       REQUIRES SD_Card_Minimal)  # Add this if in components folder
```

3. Configure pins in `SD_MMC.h`:
```c
#define CONFIG_EXAMPLE_PIN_CLK  14   // Your SD Card Clock GPIO
#define CONFIG_EXAMPLE_PIN_CMD  17   // Your SD Card Command GPIO
#define CONFIG_EXAMPLE_PIN_D0   16   // Your SD Card Data GPIO
```

## Usage:

```c
#include "SD_MMC.h"

void app_main(void) {
    SD_Init();  // Initialize and mount SD card at /sdcard
    
    // Your SD card is now accessible at /sdcard
    // Use standard C file operations:
    FILE* fp = fopen("/sdcard/your_file.txt", "r");
    // ...
}
```

## Hardware Requirements:
- External 10kΩ pull-up resistors on CLK, CMD, and D0 lines
- SD card socket
- 3.3V power supply

## Configuration Options:
- Change `slot_config.width` to 4 for 4-wire mode (faster)
- Set `format_if_mount_failed = true` to format SD card if mount fails
- Adjust `max_files` and `allocation_unit_size` as needed

