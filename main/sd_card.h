#ifndef SD_CARD_H
#define SD_CARD_H

#include <esp_err.h>
#include <string>
#include <driver/spi_common.h>

/**
 * @brief SD Card management class for reading files and ejecting the card
 * 
 * This class handles SD card initialization, file reading, and proper ejection.
 * It uses the SenseCAP Watcher board configuration for SPI communication.
 */
class SdCard {
public:
    /**
     * @brief Initialize the SD card with SenseCAP Watcher SPI pins
     * 
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t Initialize();

    /**
     * @brief Read a text file from the SD card
     * 
     * @param filename The name of the file to read (e.g., "hello.txt")
     * @param content Output parameter to store the file content
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t ReadTextFile(const std::string& filename, std::string& content);

    /**
     * @brief Eject/abandon the SD card properly
     * 
     * This function unmounts the SD card and releases all resources.
     * After calling this, the SD card should not be used for the rest of the program.
     * 
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t Eject();

    /**
     * @brief Check if SD card is currently mounted
     * 
     * @return true if SD card is mounted, false otherwise
     */
    static bool IsMounted();

private:
    static bool s_mounted;
    static constexpr const char* MOUNT_POINT = "/sdcard";
    static spi_host_device_t s_spi_host;
};

#endif // SD_CARD_H
