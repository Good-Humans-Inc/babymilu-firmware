#pragma once

#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"

// Pin Configuration - Modify these for your hardware
#define CONFIG_EXAMPLE_PIN_CLK  14   // SD Card Clock
#define CONFIG_EXAMPLE_PIN_CMD  17   // SD Card Command
#define CONFIG_EXAMPLE_PIN_D0   16   // SD Card Data 0
#define CONFIG_EXAMPLE_PIN_D1   -1   // Not used in 1-wire mode
#define CONFIG_EXAMPLE_PIN_D2   -1   // Not used in 1-wire mode
#define CONFIG_EXAMPLE_PIN_D3   -1   // Not used in 1-wire mode

// Function to initialize SD card
void SD_Init(void);

