#include "SD_MMC.h"

#define MOUNT_POINT "/sdcard"

static const char *SD_TAG = "SD";

void SD_Init(void)
{
    esp_err_t ret;

    // Options for mounting the filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,    // Set to true if you want to format on mount failure
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    
    ESP_LOGI(SD_TAG, "Initializing SD card");

    // Initialize SD/MMC host
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // Configure SD card slot
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;  // 1-wire mode (use 4 for 4-wire mode)
    
    slot_config.clk = CONFIG_EXAMPLE_PIN_CLK;
    slot_config.cmd = CONFIG_EXAMPLE_PIN_CMD;
    slot_config.d0 = CONFIG_EXAMPLE_PIN_D0;
    slot_config.d1 = CONFIG_EXAMPLE_PIN_D1;
    slot_config.d2 = CONFIG_EXAMPLE_PIN_D2;
    slot_config.d3 = CONFIG_EXAMPLE_PIN_D3;
    
    // Enable internal pullups (external 10k pullups still recommended)
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // Mount the filesystem
    ESP_LOGI(SD_TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(SD_TAG, "Failed to mount filesystem");
        } else {
            ESP_LOGE(SD_TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }
    
    ESP_LOGI(SD_TAG, "Filesystem mounted");
    
    // Print SD card information
    sdmmc_card_print_info(stdout, card);
}

