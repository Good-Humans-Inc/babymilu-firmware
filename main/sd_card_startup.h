#ifndef SD_CARD_STARTUP_H
#define SD_CARD_STARTUP_H

#include <esp_err.h>
#include <string>

/**
 * @brief SD card startup (first-file probe); mount stays active at /sdcard for animations + SP32SD1114 MP3.
 */
class SdCardStartup {
public:
    /**
     * @brief Initialize SD card, read hello.txt, and eject the card
     * 
     * This function should be called during application startup.
     * It will:
     * 1. Initialize the SD card with custom pins
     * 2. Read the hello.txt file
     * 3. Log the content
     * 4. Eject the SD card properly
     * 
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t ProcessStartup();

    /**
     * @brief Get the content that was read from hello.txt during startup
     * 
     * @return The content of hello.txt, empty string if not read or failed
     */
    static const std::string& GetHelloContent();

private:
    static std::string s_hello_content;
    static constexpr const char* HELLO_FILENAME = "hello.txt";
};

#endif // SD_CARD_STARTUP_H

