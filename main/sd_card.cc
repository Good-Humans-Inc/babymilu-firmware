#include "sd_card.h"
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <driver/gpio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Include SenseCAP Watcher board configuration
    esp_err_t ret = ESP_OK;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();

#ifdef CONFIG_BOARD_TYPE_SENSECAP_WATCHER
#include "boards/sensecap-watcher/config.h"
#endif

static const char *TAG = "SD_CARD";

bool SdCard::s_mounted = false;
spi_host_device_t SdCard::s_spi_host = SPI2_HOST;

esp_err_t SdCard::Initialize()
{
    if (s_mounted) {
        ESP_LOGW(TAG, "SD card already mounted");
        return ESP_OK;
    }

#ifdef CONFIG_BOARD_TYPE_SENSECAP_WATCHER
    ESP_LOGI(TAG, "Initializing SD card with SenseCAP Watcher configuration");
    ESP_LOGI(TAG, "MOSI: GPIO%d, MISO: GPIO%d, CLK: GPIO%d, CS: GPIO%d", 
             BSP_SPI2_HOST_MOSI, BSP_SPI2_HOST_MISO, BSP_SPI2_HOST_SCLK, BSP_SD_SPI_CS);

    // Add delay to ensure power has stabilized after IO expander initialization
    ESP_LOGI(TAG, "Waiting for SD card power to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay for power stabilization

    // Configure SPI bus using SenseCAP Watcher pins
    host.slot = SPI2_HOST;  // Use SPI2_HOST
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BSP_SPI2_HOST_MOSI,
        .miso_io_num = BSP_SPI2_HOST_MISO,
        .sclk_io_num = BSP_SPI2_HOST_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 2048,
    };

    ret = spi_bus_initialize(static_cast<spi_host_device_t>(host.slot), &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "SPI bus already initialized (likely by board), continuing with SD card initialization");
            // SPI bus is already initialized, we can still proceed with SD card mounting
            // Don't store the SPI host for cleanup since we didn't initialize it
            s_spi_host = SPI_HOST_MAX; // Mark as not initialized by us
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
            return ret;
        }
    } else {
        // Store the SPI host for later cleanup only if we initialized it
        s_spi_host = static_cast<spi_host_device_t>(host.slot);
    }

    // Configure SD card using SenseCAP Watcher CS pin
    slot_config.gpio_cs = BSP_SD_SPI_CS;
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);
    
    ESP_LOGI(TAG, "SD card slot configuration: CS=GPIO%d, Host=SPI%d", BSP_SD_SPI_CS, host.slot);
    
    // Ensure SD card CS pin is properly configured for SPI operation
    // This is important because other parts of the code might have configured it differently
    ESP_LOGI(TAG, "Configuring SD card CS pin (GPIO%d) for SPI operation...", BSP_SD_SPI_CS);
    gpio_reset_pin(static_cast<gpio_num_t>(BSP_SD_SPI_CS));
    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to ensure pin reset completes
    
#else
    ESP_LOGE(TAG, "SD card functionality only supported on SenseCAP Watcher board");
    return ESP_ERR_NOT_SUPPORTED;
#endif

    // Mount SD card with memory-conservative settings
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, // Do not format to avoid large work buffers
        .max_files = 4,                  // Reduce FATFS file descriptors
        .allocation_unit_size = 0,       // Default AU size
        .disk_status_check_enable = false // Disable disk status check for better compatibility
    };

    sdmmc_card_t* card;
    
    // Try mounting with retry mechanism
    int retry_count = 0;
    const int max_retries = 5;
    do {
        ESP_LOGI(TAG, "Attempting SD card mount (attempt %d/%d)...", retry_count + 1, max_retries);
        ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SD card mounted successfully on attempt %d", retry_count + 1);
            break;
        }
        
        retry_count++;
        if (retry_count < max_retries) {
            ESP_LOGW(TAG, "Mount attempt %d failed: %s, retrying in 1000ms...", retry_count, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1s before retry to allow power settle
        }
    } while (retry_count < max_retries);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. If you want the card to be formatted, set format_if_mount_failed = true.");
            ESP_LOGE(TAG, "This usually means the SD card is not properly formatted or is corrupted.");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "SD card not found. Please check: 1) SD card is inserted, 2) SD card is not damaged, 3) SPI connections are correct");
        } else if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "SD card communication timeout. Please check: 1) SD card is not corrupted, 2) SPI clock speed is appropriate");
        } else if (ret == ESP_ERR_NO_MEM) {
            ESP_LOGE(TAG, "Failed to initialize the card (ESP_ERR_NO_MEM). Reducing buffers and retrying later may help.");
            ESP_LOGE(TAG, "Tips: reduce FATFS max files, lower SPI transfer size, ensure PSRAM configured, free heap");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        // Only free SPI bus if we initialized it
        if (s_spi_host != SPI_HOST_MAX) {
            spi_bus_free(s_spi_host);
        }
        return ret;
    }

    // Print card info
    sdmmc_card_print_info(stdout, card);
    
    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted successfully at %s", MOUNT_POINT);
    return ESP_OK;
}

esp_err_t SdCard::ReadTextFile(const std::string& filename, std::string& content)
{
    if (!s_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Listing files in SD card...");

    // List files in the SD card directory
    DIR* dir = opendir(MOUNT_POINT);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", MOUNT_POINT);
        return ESP_FAIL;
    }

    struct dirent* entry;
    int file_count = 0;
    std::string first_file = "";

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // Regular file
            file_count++;
            if (first_file.empty()) {
                first_file = entry->d_name;
            }
        }
    }
    closedir(dir);

    ESP_LOGI(TAG, "Total files found: %d", file_count);

    if (file_count == 0) {
        ESP_LOGW(TAG, "No files found in SD card");
        return ESP_FAIL;
    }

    // Read the first file found
    std::string file_to_read = first_file;
    ESP_LOGI(TAG, "Reading first file found");

    std::string full_path = std::string(MOUNT_POINT) + "/" + file_to_read;
    FILE* file = fopen(full_path.c_str(), "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file");
        return ESP_FAIL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size < 0) {
        ESP_LOGE(TAG, "Failed to get file size");
        fclose(file);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "File size: %ld bytes", file_size);

    // Limit file size for safety
    if (file_size > 512) {
        ESP_LOGW(TAG, "File too large (%ld bytes), limiting to 512 bytes", file_size);
        file_size = 512;
    }

    ESP_LOGI(TAG, "Reading %ld bytes from file", file_size);

    // Read file content
    content.resize(file_size);
    size_t bytes_read = fread(content.data(), 1, file_size, file);
    fclose(file);

    // Resize to actual bytes read
    content.resize(bytes_read);

    ESP_LOGI(TAG, "Successfully read %zu bytes from file", bytes_read);
    ESP_LOGI(TAG, "File read completed successfully");

    return ESP_OK;
}

esp_err_t SdCard::Eject()
{
    if (!s_mounted) {
        ESP_LOGW(TAG, "SD card not mounted, nothing to eject");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Ejecting SD card...");

    // Unmount SD card
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, NULL);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to unmount SD card: %s", esp_err_to_name(ret));
        // Continue with SPI bus cleanup even if unmount fails
    }

    // Free SPI bus (only if we initialized it)
    if (s_spi_host != SPI_HOST_MAX) {
        ret = spi_bus_free(s_spi_host);
        if (ret != ESP_OK) {
            if (ret == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "SPI bus was not initialized by SD card, skipping cleanup");
            } else {
                ESP_LOGE(TAG, "Failed to free SPI bus: %s", esp_err_to_name(ret));
                return ret;
            }
        }
    } else {
        ESP_LOGI(TAG, "SPI bus was not initialized by SD card, skipping cleanup");
    }

    s_mounted = false;
    ESP_LOGI(TAG, "SD card ejected successfully");
    return ESP_OK;
}

bool SdCard::IsMounted()
{
    return s_mounted;
}

bool SdCard::IsDetected()
{
#ifdef CONFIG_BOARD_TYPE_SENSECAP_WATCHER
    // Check SD card detection pin through IO expander
    // Note: This requires access to the IO expander handle, which is not available here
    // For now, we'll assume SD card is detected if we can initialize SPI communication
    ESP_LOGI(TAG, "SD card detection check not implemented - assuming detected");
    return true;
#else
    ESP_LOGW(TAG, "SD card detection only supported on SenseCAP Watcher board");
    return false;
#endif
}

esp_err_t SdCard::DebugStatus()
{
    ESP_LOGI(TAG, "=== SD Card Debug Status ===");
    ESP_LOGI(TAG, "Mount status: %s", s_mounted ? "MOUNTED" : "NOT MOUNTED");
    ESP_LOGI(TAG, "Mount point: %s", MOUNT_POINT);
    ESP_LOGI(TAG, "SPI host: %d", s_spi_host);
    
    if (!s_mounted) {
        ESP_LOGW(TAG, "SD card is not mounted - cannot list files");
        return ESP_ERR_INVALID_STATE;
    }
    
    // List files in the SD card directory
    DIR* dir = opendir(MOUNT_POINT);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", MOUNT_POINT);
        return ESP_FAIL;
    }

    struct dirent* entry;
    int file_count = 0;
    ESP_LOGI(TAG, "Files on SD card:");
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // Regular file
            file_count++;
            char full_path[512];  // Increased buffer size to handle long filenames
            int ret = snprintf(full_path, sizeof(full_path), "%s/%s", MOUNT_POINT, entry->d_name);
            if (ret >= sizeof(full_path)) {
                ESP_LOGW(TAG, "  %s (filename too long, truncated)", entry->d_name);
                continue;
            }
            
            struct stat st;
            if (stat(full_path, &st) == 0) {
                ESP_LOGI(TAG, "  %s (%ld bytes)", entry->d_name, st.st_size);
            } else {
                ESP_LOGI(TAG, "  %s (size unknown)", entry->d_name);
            }
        }
    }
    closedir(dir);
    
    ESP_LOGI(TAG, "Total files found: %d", file_count);
    ESP_LOGI(TAG, "=== End SD Card Debug Status ===");
    
    return ESP_OK;
}