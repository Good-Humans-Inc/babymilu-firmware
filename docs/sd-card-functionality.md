# SD Card Functionality for SenseCAP Watcher

This implementation adds SD card functionality to read a `hello.txt` file during startup and then properly eject the SD card.

## Features

- **SenseCAP Watcher Pin Configuration**: Uses the predefined SenseCAP Watcher board configuration:
  - MOSI (Master Out Slave In): GPIO 5 (`BSP_SPI2_HOST_MOSI`)
  - MISO (Master In Slave Out): GPIO 6 (`BSP_SPI2_HOST_MISO`)  
  - CLK (Clock): GPIO 4 (`BSP_SPI2_HOST_SCLK`)
  - CS (Chip Select): GPIO 46 (`BSP_SD_SPI_CS`)
  - SPI Host: SPI2_HOST (`BSP_SD_SPI_NUM`)

- **Startup Integration**: SD card operations are performed during application startup before the main application begins

- **Proper Ejection**: SD card is properly unmounted and SPI bus is freed after reading

- **Error Handling**: Graceful handling of SD card failures - application continues even if SD card is not available

- **Board-Specific**: Only works on SenseCAP Watcher board configuration

## Files Added

1. **`main/sd_card.h`** - Header file defining the SD card management class
2. **`main/sd_card.cc`** - Implementation of SD card operations (initialize, read, eject)
3. **`main/sd_card_startup.h`** - Header file for startup SD card manager
4. **`main/sd_card_startup.cc`** - Implementation of startup SD card processing

## Usage

The SD card functionality is automatically executed during application startup. To use it:

1. Insert an SD card with a `hello.txt` file into the SenseCAP Watcher
2. Power on the device
3. The application will:
   - Initialize the SD card with custom SPI pins
   - Read the `hello.txt` file
   - Log the content to the console
   - Eject the SD card properly
   - Continue with normal application startup

## Log Output

When the SD card is successfully processed, you'll see logs like:
```
I (xxxx) main: Processing SD card startup...
I (xxxx) SD_CARD: Initializing SD card with SenseCAP Watcher configuration
I (xxxx) SD_CARD: MOSI: GPIO5, MISO: GPIO6, CLK: GPIO4, CS: GPIO46
I (xxxx) SD_CARD: SD card mounted successfully at /sdcard
I (xxxx) SD_CARD: Reading file: /sdcard/hello.txt
I (xxxx) SD_CARD: Successfully read X bytes from hello.txt
I (xxxx) SD_CARD: File content: [content of hello.txt]
I (xxxx) SD_CARD: Ejecting SD card...
I (xxxx) SD_CARD: SD card ejected successfully
I (xxxx) main: SD card hello.txt content: [content of hello.txt]
```

## Error Handling

If the SD card is not available or `hello.txt` doesn't exist:
```
W (xxxx) SD_CARD_STARTUP: Failed to initialize SD card: [error]
W (xxxx) main: SD card startup failed: [error]
W (xxxx) main: Continuing without SD card functionality
```

## Technical Details

- **Mount Point**: `/sdcard`
- **SPI Host**: Uses SDSPI_DEFAULT_HOST
- **File System**: FAT filesystem
- **Max Files**: Limited to 5 concurrent files during mount
- **Allocation Unit**: 16KB

## Integration

The SD card functionality is integrated into `main/main.cc` in the `app_main()` function, right after SPIFFS initialization and before launching the main application. This ensures the SD card is processed early in the startup sequence and doesn't interfere with the main application functionality.
