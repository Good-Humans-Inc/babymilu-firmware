#ifndef SCRIPTED_PLAYBACK_H
#define SCRIPTED_PLAYBACK_H

#include <esp_err.h>
#include <string>
#include <vector>

/**
 * @brief Scripted playback system for video shooting
 * 
 * This system allows you to pre-record interaction animations and conversations
 * by creating a JSON script file on the SD card. When the boot button is pressed,
 * the script is played automatically.
 * 
 * Script Format (JSON):
 * {
 *   "sequence": [
 *     {
 *       "type": "animation",
 *       "animation": "happy",
 *       "duration_ms": 2000
 *     },
 *     {
 *       "type": "animation",
 *       "animation": "normal",
 *       "duration_ms": 1000
 *     },
 *     {
 *       "type": "audio",
 *       "file": "audio1.wav",
 *       "duration_ms": 3000
 *     }
 *   ]
 * }
 * 
 * Supported animation types: normal, happy, fire, question, shy, sleep, embarrass, inspiration
 */

class ScriptedPlayback {
public:
    /**
     * @brief Initialize the scripted playback system
     * @return ESP_OK on success
     */
    static esp_err_t Initialize();

    /**
     * @brief Check if a script file exists on SD card
     * @param script_filename Name of the script file (default: "playback.json")
     * @return true if script exists, false otherwise
     */
    static bool HasScript(const std::string& script_filename = "playback.json");

    /**
     * @brief Play the scripted sequence from SD card
     * @param script_filename Name of the script file (default: "playback.json")
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t PlayScript(const std::string& script_filename = "playback.json");

    /**
     * @brief Play a video sequence (frames + audio) from SD card
     * @param video_config JSON configuration for video playback
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t PlayVideo(const std::string& frame_directory, 
                               const std::string& frame_prefix,
                               const std::string& frame_format,
                               int frame_count,
                               int fps,
                               const std::string& audio_file = "");

    /**
     * @brief Stop the current playback
     */
    static void Stop();

    /**
     * @brief Check if playback is currently active
     * @return true if playing, false otherwise
     */
    static bool IsPlaying();

private:
    struct ScriptItem {
        std::string type;           // "animation" or "audio"
        std::string animation;      // Animation name (for type="animation")
        std::string file;           // File name (for type="audio")
        int duration_ms;            // Duration in milliseconds
    };

    static bool s_is_playing;
    static void PlaybackTask(void* arg);
    static int GetAnimationIndex(const std::string& animation_name);
};

#endif // SCRIPTED_PLAYBACK_H

