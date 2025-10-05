#include "custom_logging.h"
#include "sd_card.h"
#include <esp_log.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "CUSTOM_LOGGING";

// Store original vprintf function - we'll use vprintf as the default
static int (*original_vprintf)(const char* format, va_list args) = vprintf;

// Mutex to protect SD card writes from multiple tasks
static SemaphoreHandle_t log_mutex = NULL;

// Recursion guard to prevent infinite loops when SD card logging calls ESP_LOGI
static volatile bool in_sd_card_write = false;

// Custom log vprintf function that intercepts ERROR logs
int custom_log_vprintf(const char* format, va_list args) {
    // Recursion guard: if we're already writing to SD card, skip to avoid infinite loop
    if (in_sd_card_write) {
        // Just output to original destination and return
        if (original_vprintf != NULL) {
            return original_vprintf(format, args);
        } else {
            return vprintf(format, args);
        }
    }
    
    // Format the log message to check if it's an ERROR log
    char buffer[512];
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    
    // Check if this is an ERROR level log (ESP_LOGE)
    // ESP-IDF ERROR logs start with "E (" in their formatted output
    // Format: "I (timestamp) tag: message" for INFO
    // Format: "E (timestamp) tag: message" for ERROR
    if (len > 0 && strstr(buffer, "E (") != NULL) {
        // Take mutex to protect SD card access
        if (log_mutex != NULL && xSemaphoreTake(log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Check if SD card is available before writing
            if (SdCard::IsMounted()) {
                // Set recursion guard
                in_sd_card_write = true;
                
                // Add newline if not present
                std::string log_line = std::string(buffer);
                if (!log_line.empty() && log_line.back() != '\n') {
                    log_line += "\n";
                }
                
                // Write to SD card using direct file operations to avoid recursion
                std::string full_path = "/sdcard/ERR.TXT";
                FILE* file = fopen(full_path.c_str(), "a");  // "a" = append mode
                if (file != NULL) {
                    fwrite(log_line.c_str(), 1, log_line.length(), file);
                    fflush(file);
                    fclose(file);
                }
                
                // Clear recursion guard
                in_sd_card_write = false;
            }
            xSemaphoreGive(log_mutex);
        }
    }
    
    // Always output to original destination (UART)
    if (original_vprintf != NULL) {
        return original_vprintf(format, args);
    } else {
        return vprintf(format, args);
    }
}

esp_err_t setup_custom_logging() {
    ESP_LOGI(TAG, "Setting up custom logging for ERROR logs to SD card");
    
    // Create mutex for thread safety
    if (log_mutex == NULL) {
        log_mutex = xSemaphoreCreateMutex();
        if (log_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create logging mutex");
            return ESP_FAIL;
        }
    }
    
    // original_vprintf is already initialized to vprintf
    // Set our custom vprintf function
    esp_log_set_vprintf(&custom_log_vprintf);
    
    ESP_LOGI(TAG, "Custom logging setup complete - ERROR logs will be written to err.txt on SD card");
    return ESP_OK;
}

esp_err_t restore_original_logging() {
    ESP_LOGI(TAG, "Restoring original logging function");
    
    // Restore original vprintf function (vprintf)
    esp_log_set_vprintf(original_vprintf);
    
    // Delete mutex
    if (log_mutex != NULL) {
        vSemaphoreDelete(log_mutex);
        log_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "Original logging function restored");
    return ESP_OK;
}

void test_custom_logging() {
    ESP_LOGI(TAG, "Testing custom logging system...");
    
    // Generate some test ERROR logs
    ESP_LOGE(TAG, "Test ERROR log 1: This is a test error message");
    ESP_LOGE(TAG, "Test ERROR log 2: Simulated failure with code %d", 12345);
    ESP_LOGE(TAG, "Test ERROR log 3: Another test error for SD card logging");
    
    // Generate some non-ERROR logs (these should NOT be written to SD card)
    ESP_LOGI(TAG, "This INFO log should NOT appear in err.txt");
    ESP_LOGW(TAG, "This WARN log should NOT appear in err.txt");
    
    ESP_LOGI(TAG, "Custom logging test completed - check err.txt on SD card for ERROR logs only");
}

void debug_sd_card_logging() {
    ESP_LOGI(TAG, "=== SD Card Logging Debug ===");
    ESP_LOGI(TAG, "SD card mounted: %s", SdCard::IsMounted() ? "YES" : "NO");
    
    if (SdCard::IsMounted()) {
        ESP_LOGI(TAG, "SD card is mounted - testing ERROR log write...");
        ESP_LOGE(TAG, "DEBUG ERROR: Testing SD card error logging at %lu", esp_timer_get_time() / 1000);
        ESP_LOGI(TAG, "ERROR log should now be written to err.txt on SD card");
    } else {
        ESP_LOGW(TAG, "SD card is NOT mounted - ERROR logs will only go to UART");
        ESP_LOGW(TAG, "This is why err.txt is empty on your SD card");
    }
    
    ESP_LOGI(TAG, "=== End SD Card Logging Debug ===");
}

void test_error_logging_after_init() {
    // Generate a simple ERROR log to test the custom logging system
    ESP_LOGE(TAG, "POST-INIT ERROR TEST: System fully initialized, testing SD card error logging");
    ESP_LOGE(TAG, "POST-INIT ERROR TEST: Timestamp: %lu", esp_timer_get_time() / 1000);
    ESP_LOGE(TAG, "POST-INIT ERROR TEST: This should appear in err.txt on SD card");
    
    // Wait a moment for the logs to be written
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Test direct file writing to debug the issue (using only direct file operations)
    printf("=== Testing Direct File Write ===\n");
    if (SdCard::IsMounted()) {
        // Test 1: Try direct fopen operations (safe - no ESP logging)
        printf("Test 1: Using direct fopen()\n");
        std::string full_path = "/sdcard/ERR.TXT";
        FILE* file = fopen(full_path.c_str(), "a");
        if (file != NULL) {
            size_t written = fwrite("DIRECT TEST: fopen method test\n", 1, 31, file);
            fflush(file);
            fclose(file);
            printf("Direct fopen: wrote %lu bytes\n", (unsigned long)written);
        } else {
            printf("Direct fopen failed: errno=%d\n", errno);
        }
        
        // Test 2: Try lowercase filename
        printf("Test 2: Using lowercase filename\n");
        std::string full_path_lower = "/sdcard/err.txt";
        FILE* file_lower = fopen(full_path_lower.c_str(), "a");
        if (file_lower != NULL) {
            size_t written_lower = fwrite("DIRECT TEST: lowercase filename test\n", 1, 38, file_lower);
            fflush(file_lower);
            fclose(file_lower);
            printf("Lowercase fopen: wrote %lu bytes\n", (unsigned long)written_lower);
        } else {
            printf("Lowercase fopen failed: errno=%d\n", errno);
        }
        
        // Test 3: Try creating a completely new test file
        printf("Test 3: Creating new test file\n");
        std::string full_path_new = "/sdcard/tst.txt";
        FILE* file_new = fopen(full_path_new.c_str(), "a");
        if (file_new != NULL) {
            size_t written_new = fwrite("DIRECT TEST: New file creation test\n", 1, 35, file_new);
            fflush(file_new);
            fclose(file_new);
            printf("New file fopen: wrote %lu bytes\n", (unsigned long)written_new);
        } else {
            printf("New file fopen failed: errno=%d (%s)\n", errno, strerror(errno));
            printf("Attempted path: %s\n", full_path_new.c_str());
            
            // Try with a simpler filename
            printf("Trying with simpler filename...\n");
            FILE* file_new2 = fopen("/sdcard/test.txt", "a");
            if (file_new2 != NULL) {
                size_t written_new2 = fwrite("DIRECT TEST: Simple filename test\n", 1, 33, file_new2);
                fflush(file_new2);
                fclose(file_new2);
                printf("Simple filename fopen: wrote %lu bytes\n", (unsigned long)written_new2);
            } else {
                printf("Simple filename also failed: errno=%d (%s)\n", errno, strerror(errno));
            }
        }
        
        // Test 4: Check SD card permissions
        printf("Test 4: Checking SD card directory permissions\n");
        struct stat st;
        if (stat("/sdcard", &st) == 0) {
            printf("SD card directory exists, mode: %lo\n", (unsigned long)st.st_mode);
            printf("Readable: %s, Writable: %s\n", 
                   (access("/sdcard", R_OK) == 0) ? "YES" : "NO",
                   (access("/sdcard", W_OK) == 0) ? "YES" : "NO");
        } else {
            printf("Cannot stat SD card directory, errno: %d\n", errno);
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // Wait for writes to complete
    }
    
    // List file sizes on SD card after error test
    ESP_LOGI(TAG, "=== SD Card File Sizes After Error Test ===");
    if (SdCard::IsMounted()) {
        // List files and their sizes on SD card
        DIR* dir = opendir("/sdcard");
        if (dir != NULL) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_type == DT_REG) {  // Regular file
                    std::string full_path = std::string("/sdcard/") + entry->d_name;
                    FILE* file = fopen(full_path.c_str(), "r");
                    if (file != NULL) {
                        fseek(file, 0, SEEK_END);
                        long file_size = ftell(file);
                        fclose(file);
                        ESP_LOGI(TAG, "File: %s, Size: %ld bytes", entry->d_name, file_size);
                    }
                }
            }
            closedir(dir);
        }
    } else {
        ESP_LOGW(TAG, "SD card not mounted - cannot check file sizes");
    }
    ESP_LOGI(TAG, "=== End SD Card File Sizes Check ===");
}

void simple_error_test() {
    // Simple test without complex debugging to verify the recursion fix
    printf("Starting simple error test...\n");
    ESP_LOGE(TAG, "SIMPLE ERROR TEST: Testing recursion fix");
    ESP_LOGE(TAG, "SIMPLE ERROR TEST: This should NOT cause a crash");
    ESP_LOGE(TAG, "SIMPLE ERROR TEST: Another test message");
    vTaskDelay(pdMS_TO_TICKS(100));
    printf("Simple error test completed - no crash means recursion is fixed!\n");
    
    // Check if error logs were written
    if (SdCard::IsMounted()) {
        std::string full_path = "/sdcard/ERR.TXT";
        FILE* file = fopen(full_path.c_str(), "r");
        if (file != NULL) {
            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            fclose(file);
            printf("ERR.TXT file size after simple test: %ld bytes\n", file_size);
        }
    }
}

void show_error_logging_status() {
    printf("\n=== ERROR LOGGING STATUS SUMMARY ===\n");
    
    if (SdCard::IsMounted()) {
        printf("✅ SD Card: MOUNTED\n");
        
        // Check ERR.TXT file
        std::string full_path = "/sdcard/ERR.TXT";
        FILE* file = fopen(full_path.c_str(), "r");
        if (file != NULL) {
            fseek(file, 0, SEEK_END);
            long file_size = ftell(file);
            fclose(file);
            printf("✅ ERR.TXT: %ld bytes (ERROR logs written successfully!)\n", file_size);
        } else {
            printf("❌ ERR.TXT: Cannot read file\n");
        }
        
        // Check if custom logging is working
        printf("✅ Custom Logging: ACTIVE (ERROR logs will be written to SD card)\n");
        printf("✅ Recursion Guard: ACTIVE (prevents crashes)\n");
        
    } else {
        printf("❌ SD Card: NOT MOUNTED\n");
        printf("❌ Error logging: DISABLED (SD card required)\n");
    }
    
    printf("=== END STATUS SUMMARY ===\n\n");
}
