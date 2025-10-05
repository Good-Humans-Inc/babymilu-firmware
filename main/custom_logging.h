#ifndef CUSTOM_LOGGING_H
#define CUSTOM_LOGGING_H

#include <esp_err.h>

/**
 * @brief Setup custom logging to write ERROR logs to SD card
 * 
 * This function sets up a custom log hook that intercepts ESP_LOGE calls
 * and writes them to an "err.txt" file on the SD card while still
 * outputting them to the normal UART console.
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t setup_custom_logging();

/**
 * @brief Restore original logging function
 * 
 * This function restores the original ESP-IDF logging function,
 * disabling the custom SD card logging hook.
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t restore_original_logging();

/**
 * @brief Test function to generate sample ERROR logs
 * 
 * This function generates some test ERROR logs to verify that
 * the custom logging system is working correctly.
 */
void test_custom_logging();

/**
 * @brief Check SD card status and test logging
 * 
 * This function checks if SD card is mounted and tests the logging
 * system with a sample ERROR log.
 */
void debug_sd_card_logging();

/**
 * @brief Test ERROR logging after system is fully initialized
 * 
 * This function generates ERROR logs after the SD card and animations
 * are fully loaded to ensure the logging system works properly.
 */
void test_error_logging_after_init();

/**
 * @brief Simple error test to verify recursion fix
 * 
 * This function generates simple ERROR logs to test that the recursion
 * guard is working and preventing crashes.
 */
void simple_error_test();

/**
 * @brief Show error logging status summary
 * 
 * This function displays a comprehensive status of the error logging
 * system including SD card mount status and file sizes.
 */
void show_error_logging_status();

#endif // CUSTOM_LOGGING_H
