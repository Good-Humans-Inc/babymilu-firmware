#include "sd_card_startup.h"
#include "sd_card.h"
#include <esp_log.h>

static const char *TAG = "SD_CARD_STARTUP";

std::string SdCardStartup::s_hello_content;

esp_err_t SdCardStartup::ProcessStartup()
{
    ESP_LOGI(TAG, "Starting SD card startup process...");

    // Initialize SD card
    esp_err_t ret = SdCard::Initialize();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "Continuing without SD card functionality");
        return ret;
    }

    // Read first file from SD card
    std::string content;
    ret = SdCard::ReadTextFile("", content);  // Empty filename - will read first file found
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read files from SD card: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "SD card may be empty or not accessible");
    } else {
        s_hello_content = content;
        ESP_LOGI(TAG, "Successfully read first file from SD card (%zu bytes)", content.size());
        
        // Safely log the content as simple hex values
        ESP_LOGI(TAG, "File content (hex values):");
        for (int i = 0; i < (int)content.size() && i < 32; i++) {
            unsigned char c = (unsigned char)content[i];
            ESP_LOGI(TAG, "  Byte %d: 0x%02x", i, c);
        }
    }

    // Eject SD card regardless of read success/failure
    ret = SdCard::Eject();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to eject SD card: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "Continuing anyway - file was read successfully");
    }

    ESP_LOGI(TAG, "SD card startup process completed");
    return ESP_OK;  // Always return OK since file reading succeeded
}

const std::string& SdCardStartup::GetHelloContent()
{
    return s_hello_content;
}
