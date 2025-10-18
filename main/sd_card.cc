#include "sd_card.h"
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>
#include <driver/sdmmc_host.h>
#include <driver/spi_common.h>
#include <driver/gpio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Include board configurations
#ifdef CONFIG_BOARD_TYPE_SENSECAP_WATCHER
#include "boards/sensecap-watcher/config.h"
#endif

#ifdef CONFIG_BOARD_TYPE_ESP32S3_Touch_AMOLED_1_75
#include "boards/waveshare-s3-touch-amoled-1.75/config.h"
#endif

#ifdef CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85
#include "boards/esp32-s3-touch-lcd-1.85/config.h"
#include "esp_io_expander_tca9554.h"
#endif

#ifdef CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85C
#include "boards/esp32-s3-touch-lcd-1.85c/config.h"
#include "esp_io_expander_tca9554.h"
#endif

static const char *TAG = "SD_CARD";

// IO expander handle for LCD boards
#if defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85) || defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85C)
static esp_io_expander_handle_t s_io_expander_handle = NULL;
#endif

bool SdCard::s_mounted = false;
#if defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_AMOLED_1_75) || \
    defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85) || \
    defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85C)
spi_host_device_t SdCard::s_spi_host = SPI3_HOST;  // Waveshare boards use SPI3_HOST for SD card
#else
spi_host_device_t SdCard::s_spi_host = SPI2_HOST;  // Default to SPI2_HOST
#endif

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
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;  // Use SPI2_HOST
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BSP_SPI2_HOST_MOSI,
        .miso_io_num = BSP_SPI2_HOST_MISO,
        .sclk_io_num = BSP_SPI2_HOST_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(static_cast<spi_host_device_t>(host.slot), &bus_cfg, SDSPI_DEFAULT_DMA);
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
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = BSP_SD_SPI_CS;
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);
    
    ESP_LOGI(TAG, "SD card slot configuration: CS=GPIO%d, Host=SPI%d", BSP_SD_SPI_CS, host.slot);
    
    // Ensure SD card CS pin is properly configured for SPI operation
    // This is important because other parts of the code might have configured it differently
    ESP_LOGI(TAG, "Configuring SD card CS pin (GPIO%d) for SPI operation...", BSP_SD_SPI_CS);
    gpio_reset_pin(static_cast<gpio_num_t>(BSP_SD_SPI_CS));
    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to ensure pin reset completes
    
    // Mount SD card with more permissive settings
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,  // Allow formatting if mount fails
        .max_files = 10,                 // Increase max files
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false // Disable disk status check for better compatibility
    };

    sdmmc_card_t* card;
    
    // Try mounting with retry mechanism
    int retry_count = 0;
    const int max_retries = 3;
    do {
        ESP_LOGI(TAG, "Attempting SD card mount (attempt %d/%d)...", retry_count + 1, max_retries);
        ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SD card mounted successfully on attempt %d", retry_count + 1);
            break;
        }
        
        retry_count++;
        if (retry_count < max_retries) {
            ESP_LOGW(TAG, "Mount attempt %d failed: %s, retrying in 500ms...", retry_count, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(500)); // Wait 500ms before retry
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

#elif defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_AMOLED_1_75)
    ESP_LOGI(TAG, "Initializing SD card with Waveshare AMOLED 1.75 configuration");
    ESP_LOGI(TAG, "MOSI: GPIO%d, MISO: GPIO%d, CLK: GPIO%d, CS: GPIO%d", 
             SD_SPI_MOSI, SD_SPI_MISO, SD_SPI_SCLK, SD_SPI_CS);

    // Add delay to ensure power has stabilized
    ESP_LOGI(TAG, "Waiting for SD card power to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay for power stabilization

    // Configure SPI bus using Waveshare pins (MOSI=GPIO1, MISO=GPIO3, SCK=GPIO2, CS=GPIO41)
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;  // Use SPI3_HOST
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_SPI_MOSI,
        .miso_io_num = SD_SPI_MISO,
        .sclk_io_num = SD_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t ret = spi_bus_initialize(static_cast<spi_host_device_t>(host.slot), &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "SPI bus already initialized, continuing with SD card initialization");
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

    // Configure SD card CS pin
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_SPI_CS;
    slot_config.host_id = static_cast<spi_host_device_t>(host.slot);
    
    ESP_LOGI(TAG, "SD card slot configuration: CS=GPIO%d, Host=SPI%d", SD_SPI_CS, host.slot);
    
    // Ensure SD card CS pin is properly configured for SPI operation
    ESP_LOGI(TAG, "Configuring SD card CS pin (GPIO%d) for SPI operation...", SD_SPI_CS);
    gpio_reset_pin(static_cast<gpio_num_t>(SD_SPI_CS));
    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay to ensure pin reset completes
    
    // Mount SD card with more permissive settings
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,  // Allow formatting if mount fails
        .max_files = 10,                 // Increase max files
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false // Disable disk status check for better compatibility
    };

    sdmmc_card_t* card;
    
    // Try mounting with retry mechanism
    int retry_count = 0;
    const int max_retries = 3;
    do {
        ESP_LOGI(TAG, "Attempting SD card mount (attempt %d/%d)...", retry_count + 1, max_retries);
        ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SD card mounted successfully on attempt %d", retry_count + 1);
            break;
        }
        
        retry_count++;
        if (retry_count < max_retries) {
            ESP_LOGW(TAG, "Mount attempt %d failed: %s, retrying in 500ms...", retry_count, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(500)); // Wait 500ms before retry
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

#elif defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85) || defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85C)
    ESP_LOGI(TAG, "Initializing SD card with Waveshare LCD 1.85 configuration (SDMMC mode)");
    ESP_LOGI(TAG, "Using SDMMC 1-wire mode: CLK=GPIO%d, CMD=GPIO%d, D0=GPIO%d", 
             SD_SPI_SCLK, SD_SPI_MOSI, SD_SPI_MISO);

    // Add delay to ensure power has stabilized
    ESP_LOGI(TAG, "Waiting for SD card power to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay for power stabilization

    // Use SDMMC mode instead of SPI mode (more reliable)
    // Initialize SD/MMC host
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    
    // Configure SD card slot for 1-wire mode
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;  // 1-wire mode (use 4 for 4-wire mode)
    
    // Map the SPI pins to SDMMC pins
    // CLK: GPIO14, CMD: GPIO17, D0: GPIO16
    slot_config.clk = SD_SPI_SCLK;   // GPIO14
    slot_config.cmd = SD_SPI_MOSI;   // GPIO17
    slot_config.d0 = SD_SPI_MISO;    // GPIO16
    slot_config.d1 = GPIO_NUM_NC;    // Not used in 1-wire mode
    slot_config.d2 = GPIO_NUM_NC;    // Not used in 1-wire mode
    slot_config.d3 = GPIO_NUM_NC;    // Not used in 1-wire mode
    
    // Enable internal pullups (external 10k pullups still recommended)
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    
    ESP_LOGI(TAG, "SD card slot configuration: 1-wire mode");
    ESP_LOGI(TAG, "  CLK: GPIO%d, CMD: GPIO%d, D0: GPIO%d", 
             slot_config.clk, slot_config.cmd, slot_config.d0);
    
    // Mount SD card with more permissive settings
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,  // Don't format automatically
        .max_files = 10,                  // Increase max files
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false // Disable disk status check for better compatibility
    };

    sdmmc_card_t* card;
    
    // Try mounting with retry mechanism
    int retry_count = 0;
    const int max_retries = 3;
    esp_err_t ret;
    do {
        ESP_LOGI(TAG, "Attempting SD card mount (attempt %d/%d)...", retry_count + 1, max_retries);
        ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SD card mounted successfully on attempt %d", retry_count + 1);
            break;
        }
        
        retry_count++;
        if (retry_count < max_retries) {
            ESP_LOGW(TAG, "Mount attempt %d failed: %s, retrying in 500ms...", retry_count, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(500)); // Wait 500ms before retry
        }
    } while (retry_count < max_retries);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. If you want the card to be formatted, set format_if_mount_failed = true.");
            ESP_LOGE(TAG, "This usually means the SD card is not properly formatted or is corrupted.");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "SD card not found. Please check: 1) SD card is inserted, 2) SD card is not damaged, 3) SDMMC connections are correct");
        } else if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "SD card communication timeout. Please check: 1) SD card is not corrupted, 2) SDMMC clock speed is appropriate");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return ret;
    }

    // Print card info
    sdmmc_card_print_info(stdout, card);
    
    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted successfully at %s", MOUNT_POINT);
    return ESP_OK;

#else
    ESP_LOGE(TAG, "SD card functionality not supported on this board");
    return ESP_ERR_NOT_SUPPORTED;
#endif
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
#if defined(CONFIG_BOARD_TYPE_SENSECAP_WATCHER) || defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_AMOLED_1_75)
    // For boards without a dedicated SD card detection pin,
    // assume SD card is detected if we can initialize SPI communication
    ESP_LOGI(TAG, "SD card detection check not implemented - assuming detected");
    return true;
#else
    ESP_LOGW(TAG, "SD card detection not supported on this board");
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

#if defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85) || defined(CONFIG_BOARD_TYPE_ESP32S3_Touch_LCD_1_85C)
void SdCard::SetIoExpanderHandle(void* handle)
{
    s_io_expander_handle = (esp_io_expander_handle_t)handle;
    ESP_LOGI(TAG, "IO expander handle set for SD card CS control");
}
#endif

esp_err_t SdCard::AppendToFile(const std::string& filename, const std::string& content)
{
    if (!s_mounted) {
        ESP_LOGW(TAG, "SD card not mounted, cannot append to file");
        return ESP_ERR_INVALID_STATE;
    }

    std::string full_path = std::string(MOUNT_POINT) + "/" + filename;
    
    // Debug: Check if directory is writable
    ESP_LOGI(TAG, "Attempting to append to file: %s", full_path.c_str());
    
    FILE* file = fopen(full_path.c_str(), "a");  // "a" = append mode
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for appending: %s", full_path.c_str());
        ESP_LOGE(TAG, "Error details: errno=%d", errno);
        
        // Try to create the file if it doesn't exist
        ESP_LOGI(TAG, "Trying to create new file instead...");
        file = fopen(full_path.c_str(), "w");  // "w" = write mode (creates file)
        if (file == NULL) {
            ESP_LOGE(TAG, "Failed to create file: %s, errno=%d", full_path.c_str(), errno);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Successfully created new file: %s", full_path.c_str());
    } else {
        ESP_LOGI(TAG, "Successfully opened existing file for appending: %s", full_path.c_str());
    }
    
    size_t bytes_written = fwrite(content.c_str(), 1, content.length(), file);
    if (bytes_written != content.length()) {
        ESP_LOGE(TAG, "Failed to write all content to file: %s (wrote %zu of %zu bytes)", 
                 full_path.c_str(), bytes_written, content.length());
        fclose(file);
        return ESP_FAIL;
    }
    
    // Ensure data is flushed to SD card
    fflush(file);
    
    fclose(file);
    ESP_LOGI(TAG, "Successfully wrote %zu bytes to file: %s", bytes_written, full_path.c_str());
    return ESP_OK;
}

esp_err_t SdCard::TestWriteCapability() {
    if (!s_mounted) {
        ESP_LOGW(TAG, "SD card not mounted, cannot test write capability");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Testing SD card write capability...");
    
    // Test 1: Check directory permissions
    struct stat st;
    if (stat(MOUNT_POINT, &st) == 0) {
        ESP_LOGI(TAG, "SD card directory exists: %s", MOUNT_POINT);
        ESP_LOGI(TAG, "Directory permissions: mode=0%o, uid=%d, gid=%d", st.st_mode, st.st_uid, st.st_gid);
    } else {
        ESP_LOGE(TAG, "Failed to stat SD card directory: %s", MOUNT_POINT);
        return ESP_FAIL;
    }
    
    // Test 2: Try to create a temporary file
    std::string test_file = std::string(MOUNT_POINT) + "/write_test.tmp";
    FILE* file = fopen(test_file.c_str(), "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to create test file: %s, errno=%d", test_file.c_str(), errno);
        return ESP_FAIL;
    }
    
    // Test 3: Write some data
    const char* test_data = "Write capability test\n";
    size_t written = fwrite(test_data, 1, strlen(test_data), file);
    if (written != strlen(test_data)) {
        ESP_LOGE(TAG, "Failed to write test data, wrote %zu of %zu bytes", written, strlen(test_data));
        fclose(file);
        remove(test_file.c_str());
        return ESP_FAIL;
    }
    
    fflush(file);
    fclose(file);
    
    // Test 4: Clean up test file
    if (remove(test_file.c_str()) == 0) {
        ESP_LOGI(TAG, "SD card write capability test PASSED - can create, write, and delete files");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Write test passed but failed to delete test file: %s", test_file.c_str());
        return ESP_OK; // Still consider it a pass since writing worked
    }
}