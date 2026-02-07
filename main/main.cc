#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "application.h"
#include "system_info.h"
#include "animation.h"
#include "sd_card.h"
#include "sd_card_startup.h"

#define TAG "main"

// Power button GPIO for wake-up check (matches esp32-s3-touch-lcd-1.85 board)
#define PWR_BUTTON_GPIO GPIO_NUM_6

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== app_main() STARTED ===");
    
    // Check if we woke from deep sleep
    esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
    if (wake_cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "Woke from deep sleep via GPIO (ext1)");
        
        // Configure power button GPIO to check if it's still pressed
        gpio_reset_pin(PWR_BUTTON_GPIO);
        gpio_set_direction(PWR_BUTTON_GPIO, GPIO_MODE_INPUT);
        gpio_set_pull_mode(PWR_BUTTON_GPIO, GPIO_PULLUP_ONLY);
        
        // Wait a bit for GPIO to stabilize after wake
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // Check if button is still pressed (active low)
        bool button_pressed = (gpio_get_level(PWR_BUTTON_GPIO) == 0);
        
        if (button_pressed) {
            ESP_LOGI(TAG, "Power button is pressed, waiting for 5s long press to power on...");
            
            // Wait for 5 seconds, checking if button is still pressed
            int64_t start_time = esp_timer_get_time() / 1000; // milliseconds
            bool still_pressed = true;
            
            while ((esp_timer_get_time() / 1000 - start_time) < 5000) {
                vTaskDelay(pdMS_TO_TICKS(100)); // Check every 100ms
                bool current_state = (gpio_get_level(PWR_BUTTON_GPIO) == 0);
                
                if (!current_state) {
                    // Button released before 5s - go back to sleep
                    ESP_LOGI(TAG, "Button released before 5s, going back to deep sleep");
                    still_pressed = false;
                    break;
                }
            }
            
            if (still_pressed) {
                ESP_LOGI(TAG, "5s long press detected - powering on device");
                // Continue with normal boot
            } else {
                // Go back to deep sleep
                ESP_LOGI(TAG, "Re-entering deep sleep");
                uint64_t gpio_mask = (1ULL << PWR_BUTTON_GPIO);
                esp_sleep_enable_ext1_wakeup(gpio_mask, ESP_EXT1_WAKEUP_ANY_LOW);
                esp_deep_sleep_start();
                return; // Should never reach here
            }
        } else {
            // Button not pressed - might be a brief press, go back to sleep
            ESP_LOGI(TAG, "Power button not pressed, going back to deep sleep");
            uint64_t gpio_mask = (1ULL << PWR_BUTTON_GPIO);
            esp_sleep_enable_ext1_wakeup(gpio_mask, ESP_EXT1_WAKEUP_ANY_LOW);
            esp_deep_sleep_start();
            return; // Should never reach here
        }
    } else if (wake_cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "Woke from sleep, cause: %d", wake_cause);
    }
    
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

#if !defined(CONFIG_BOARD_TYPE_ECHOEAR)
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
        ESP_LOGW(TAG, "Will fall back to SPIFFS animations");
    }
    
    ESP_LOGI(TAG, "=== SD card initialization process completed ===");
#else
    ESP_LOGI(TAG, "Skipping SD card startup in app_main for EchoEar (handled in background task)");
#endif

    // NOTE: animation_init_spiffs() is now called from SensecapWatcher constructor
    // after SD card initialization to ensure proper timing

    // Launch the application
    ESP_LOGI(TAG, "Launching Application::GetInstance().Start()...");
    Application::GetInstance().Start();
    ESP_LOGI(TAG, "=== app_main() COMPLETED ===");
}
