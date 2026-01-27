#ifndef WAV_PLAYER_H
#define WAV_PLAYER_H

#include <esp_err.h>
#include <string>
#include <vector>

/**
 * @brief WAV Player class for playing WAV files from SD card
 */
class WavPlayer {
public:
    /**
     * @brief Find the first WAV file on SD card
     * @param filename Output parameter to store the found WAV filename
     * @return ESP_OK if found, error code otherwise
     */
    static esp_err_t FindFirstWavFile(std::string& filename);

    /**
     * @brief Play a WAV file from SD card
     * @param filename The WAV file to play (relative to SD card mount point)
     * @param gain Gain multiplier (default 1.0)
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t PlayWavFile(const std::string& filename, float gain = 1.0f);

    /**
     * @brief Play the first WAV file found on SD card
     * @param gain Gain multiplier (default 1.0)
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t PlayFirstWav(float gain = 1.0f);

    /**
     * @brief Play a random WAV file from SD card
     * @param gain Gain multiplier (default 1.0)
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t PlayRandomWav(float gain = 1.0f);

    /**
     * @brief Play WAV files sequentially (1.wav, 2.wav, 3.wav, etc.) with matching animations
     * @param gain Gain multiplier (default 1.0)
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t PlaySequentialWav(float gain = 1.0f);

    /**
     * @brief Find all WAV files on SD card
     * @param filenames Output vector to store found WAV filenames
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t FindAllWavFiles(std::vector<std::string>& filenames);

    /**
     * @brief Find all numbered WAV files (1.wav, 2.wav, etc.) and return them sorted
     * @param filenames Output vector to store found numbered WAV filenames, sorted by number
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t FindNumberedWavFiles(std::vector<std::string>& filenames);

    /**
     * @brief Reset the sequential playback index to start from the beginning
     */
    static void ResetSequentialIndex();

private:
    /**
     * @brief Check if a file has WAV extension
     * @param filename The filename to check
     * @return true if WAV, false otherwise
     */
    static bool IsWavFile(const std::string& filename);

    /**
     * @brief Extract number from a numbered WAV filename (e.g., "1.wav" -> 1)
     * @param filename The filename to extract number from
     * @return The number if valid, -1 if not a numbered file
     */
    static int ExtractNumberFromFilename(const std::string& filename);

    /**
     * @brief Map wav file number to animation index
     * @param wav_number The wav file number (1, 2, 3, etc.)
     * @return The animation index to use
     */
    static int GetAnimationForWavNumber(int wav_number);

    // Static state for sequential playback
    static int current_sequential_index_;
    static std::vector<std::string> numbered_wav_files_;
    static bool is_playing_;
};

#endif // WAV_PLAYER_H

