#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>

#include "application.h"
#include "system_info.h"
#include "animation.h"
#include "sd_card.h"
#include "sd_card_startup.h"
#include "custom_logging.h"

#define TAG "main"

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== app_main() STARTED ===");
    
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "Event loop created");

    // Initialize NVS flash for WiFi configuration
    ESP_LOGI(TAG, "Initializing NVS flash...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS flash initialized successfully");

    ESP_LOGI(TAG, "=== Starting SD card initialization process ===");
    
    // Process SD card startup (initialize, read test.bin, keep mounted for animations)
    ESP_LOGI(TAG, "Processing SD card startup...");
    ret = SdCardStartup::ProcessStartup();
    ESP_LOGI(TAG, "SdCardStartup::ProcessStartup() returned: %s", esp_err_to_name(ret));
    
    if (ret == ESP_OK) {
        const std::string& hello_content = SdCardStartup::GetHelloContent();
        if (!hello_content.empty()) {
            ESP_LOGI(TAG, "SD card file read successfully (%zu bytes)", hello_content.size());
            ESP_LOGI(TAG, "Content length: %zu", hello_content.length());
        } else {
            ESP_LOGI(TAG, "SD card processed but content is empty");
        }
        ESP_LOGI(TAG, "SD card initialized and ready for animation loading");
    } else {
        ESP_LOGW(TAG, "SD card startup failed: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "Animations will not be available without SD card");
    }
    
    ESP_LOGI(TAG, "=== SD card initialization process completed ===");

    // Setup custom logging to write ERROR logs to SD card
    ESP_LOGI(TAG, "Setting up custom logging for ERROR logs...");
    ret = setup_custom_logging();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Custom logging setup successful - ERROR logs will be written to err.txt on SD card");
        
        // Test the custom logging system
        ESP_LOGI(TAG, "Running custom logging test...");
        test_custom_logging();
    } else {
        ESP_LOGW(TAG, "Custom logging setup failed: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "ERROR logs will only be available via UART monitor");
    }

    // NOTE: animation_init() is now called from SensecapWatcher constructor
    // after SD card initialization to ensure proper timing

    // Launch the application
    ESP_LOGI(TAG, "Launching Application::GetInstance().Start()...");
    Application::GetInstance().Start();
    ESP_LOGI(TAG, "=== app_main() COMPLETED ===");
}
