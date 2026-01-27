#include "wav_player.h"
#include "sd_card.h"
#include "board.h"
#include "application.h"
#include "audio_codecs/audio_codec.h"
#include "animation/animation.h"
#include <esp_log.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <vector>
#include <memory>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdlib>

#define TAG "WavPlayer"

// Static state for sequential playback
int WavPlayer::current_sequential_index_ = 0;
std::vector<std::string> WavPlayer::numbered_wav_files_;
bool WavPlayer::is_playing_ = false;

// Based on PlayWavFromUrl from application.cc
// Adapted to read from SD card file instead of HTTP stream

bool WavPlayer::IsWavFile(const std::string& filename) {
    if (filename.length() < 4) {
        return false;
    }
    
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    return lower.substr(lower.length() - 4) == ".wav";
}

esp_err_t WavPlayer::FindAllWavFiles(std::vector<std::string>& filenames) {
    filenames.clear();
    
    if (!SdCard::IsMounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Searching for WAV files on SD card...");

    DIR* dir = opendir("/sdcard");
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open SD card directory");
        return ESP_FAIL;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // Regular file
            std::string file_name = entry->d_name;
            if (IsWavFile(file_name)) {
                filenames.push_back(file_name);
            }
        }
    }
    closedir(dir);

    ESP_LOGI(TAG, "Found %u WAV file(s)", (unsigned int)filenames.size());
    return filenames.empty() ? ESP_ERR_NOT_FOUND : ESP_OK;
}

esp_err_t WavPlayer::FindFirstWavFile(std::string& filename) {
    std::vector<std::string> filenames;
    esp_err_t ret = FindAllWavFiles(filenames);
    if (ret != ESP_OK || filenames.empty()) {
        ESP_LOGW(TAG, "No WAV files found on SD card");
        return ESP_ERR_NOT_FOUND;
    }
    filename = filenames[0];
    ESP_LOGI(TAG, "Found WAV file: %s", filename.c_str());
    return ESP_OK;
}

esp_err_t WavPlayer::PlayWavFile(const std::string& filename, float gain) {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (!codec) {
        ESP_LOGE(TAG, "Audio codec not available");
        return ESP_ERR_INVALID_STATE;
    }

    if (!SdCard::IsMounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    std::string full_path = std::string("/sdcard/") + filename;
    ESP_LOGI(TAG, "Playing WAV file: %s", full_path.c_str());

    FILE* file = fopen(full_path.c_str(), "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open WAV file: %s", full_path.c_str());
        return ESP_FAIL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        ESP_LOGE(TAG, "Invalid file size: %ld", file_size);
        fclose(file);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WAV file size: %ld bytes", file_size);

    // Naively skip 44-byte WAV header for PCM WAV (same as PlayWavFromUrl)
    int to_skip = 44;
    char hdr[64];
    while (to_skip > 0) {
        int n = fread(hdr, 1, to_skip > (int)sizeof(hdr) ? (int)sizeof(hdr) : to_skip, file);
        if (n <= 0) {
            break;
        }
        to_skip -= n;
    }

    // Stream PCM frames -> codec (same logic as PlayWavFromUrl)
    const size_t BUF = 1024;
    std::unique_ptr<uint8_t[]> buf(new uint8_t[BUF]);
    bool have_leftover = false;
    uint8_t leftover = 0;
    gain = (gain <= 0.0f) ? 1.0f : gain;

    // Prevent Application::OnAudioOutput() from disabling output during playback
    Application::GetInstance().StartExternalAudioPlayback();
    
    // Disable input if enabled (to avoid duplex mode conflicts and save latency)
    // Input is for ASR which we don't need for WAV playback
    if (codec->input_enabled()) {
        ESP_LOGI(TAG, "Disabling input for WAV playback (saves latency)");
        codec->EnableInput(false);
        vTaskDelay(pdMS_TO_TICKS(50)); // Brief delay for I2S to settle
    }
    
    // Enable output
    codec->EnableOutput(true);
    
    // Ensure output volume is set
    int current_volume = codec->output_volume();
    if (current_volume <= 0) {
        ESP_LOGW(TAG, "Output volume is 0, setting to default 70");
        codec->SetOutputVolume(70);
    }
    
    // Small delay to ensure output device is ready
    // This also gives Application::OnAudioOutput() time to see output is enabled
    vTaskDelay(pdMS_TO_TICKS(50));

    // Track consecutive failures to detect if output keeps getting disabled
    int consecutive_failures = 0;
    const int MAX_CONSECUTIVE_FAILURES = 5;

    while (true) {
        int n = fread(reinterpret_cast<char *>(buf.get()), 1, BUF, file);
        if (n <= 0) {
            break;
        }

        size_t offset = 0;
        std::vector<int16_t> samples;
        samples.reserve(n / 2 + 1);

        // Handle odd byte from previous chunk
        if (have_leftover) {
            int16_t s = (int16_t)((uint16_t)buf[offset] << 8 | leftover);
            int32_t v = (int32_t)(s * gain);
            if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
            samples.push_back((int16_t)v);
            offset += 1;
            have_leftover = false;
        }

        // Convert little-endian 16-bit to int16_t, apply gain
        for (; offset + 1 < (size_t)n; offset += 2) {
            int16_t s = (int16_t)((uint16_t)buf[offset + 1] << 8 | buf[offset]);
            int32_t v = (int32_t)(s * gain);
            if (v > 32767)
                v = 32767;
            else if (v < -32768)
                v = -32768;
            samples.push_back((int16_t)v);
        }

        // Save leftover byte if odd length
        if (offset < (size_t)n) {
            leftover = buf[offset];
            have_leftover = true;
        }

        if (!samples.empty()) {
            // Application::OnAudioOutput() may disable output if decode queue is empty
            // Keep re-enabling it during WAV playback
            if (!codec->output_enabled()) {
                ESP_LOGW(TAG, "Output was disabled, re-enabling for WAV playback");
                codec->EnableOutput(true);
                consecutive_failures++;
                if (consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
                    ESP_LOGE(TAG, "Output keeps getting disabled, stopping playback");
                    break;
                }
            } else {
                consecutive_failures = 0; // Reset on success
            }
            
            // Write audio data
            codec->OutputData(samples);
            
            // Small delay to allow audio to play (prevents overwhelming the I2S buffer)
            // For 16-bit mono at typical sample rates, ~512 samples is ~10-30ms
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    // Wait a bit longer for final samples to play
    vTaskDelay(pdMS_TO_TICKS(100));

    // Note: We don't re-enable input here to save latency
    // Input will be re-enabled by the system when needed (e.g., for ASR)

    Application::GetInstance().StopExternalAudioPlayback();

    fclose(file);
    ESP_LOGI(TAG, "WAV playback completed");
    return ESP_OK;
}

esp_err_t WavPlayer::PlayFirstWav(float gain) {
    std::string filename;
    esp_err_t ret = FindFirstWavFile(filename);
    if (ret != ESP_OK) {
        return ret;
    }
    return PlayWavFile(filename, gain);
}

esp_err_t WavPlayer::PlayRandomWav(float gain) {
    std::vector<std::string> filenames;
    esp_err_t ret = FindAllWavFiles(filenames);
    if (ret != ESP_OK || filenames.empty()) {
        ESP_LOGW(TAG, "No WAV files found for random playback");
        return ESP_ERR_NOT_FOUND;
    }

    // Select random file - ensure we have valid files
    size_t file_count = filenames.size();
    if (file_count == 0) {
        ESP_LOGE(TAG, "No WAV files available");
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t random_index = esp_random() % file_count;
    if (random_index >= file_count) {
        ESP_LOGE(TAG, "Invalid random index: %u >= %u", random_index, (unsigned int)file_count);
        random_index = 0; // Fallback to first file
    }

    // Make a copy of the filename to ensure it's valid
    std::string selected_file = filenames[random_index];
    ESP_LOGI(TAG, "Randomly selected WAV file %u of %u: %s", 
             (unsigned int)(random_index + 1), 
             (unsigned int)file_count, 
             selected_file.c_str());
    
    return PlayWavFile(selected_file, gain);
}

int WavPlayer::ExtractNumberFromFilename(const std::string& filename) {
    // Extract number from filename like "1.wav", "2.wav", etc.
    // Remove .wav extension (case insensitive)
    std::string name = filename;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    
    if (name.length() < 5 || name.substr(name.length() - 4) != ".wav") {
        return -1; // Not a .wav file
    }
    
    // Get the base name without extension
    std::string base = name.substr(0, name.length() - 4);
    
    // Check if it's a pure number
    if (base.empty()) {
        return -1;
    }
    
    // Check if all characters are digits
    bool is_number = true;
    for (char c : base) {
        if (!std::isdigit(c)) {
            is_number = false;
            break;
        }
    }
    
    if (!is_number) {
        return -1;
    }
    
    // Convert to integer
    int number = std::atoi(base.c_str());
    return number;
}

esp_err_t WavPlayer::FindNumberedWavFiles(std::vector<std::string>& filenames) {
    filenames.clear();
    
    if (!SdCard::IsMounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Searching for numbered WAV files on SD card...");

    DIR* dir = opendir("/sdcard");
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open SD card directory");
        return ESP_FAIL;
    }

    // Map to store number -> filename pairs for sorting
    std::vector<std::pair<int, std::string>> numbered_files;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // Regular file
            std::string file_name = entry->d_name;
            if (IsWavFile(file_name)) {
                int number = ExtractNumberFromFilename(file_name);
                if (number > 0) {
                    numbered_files.push_back(std::make_pair(number, file_name));
                }
            }
        }
    }
    closedir(dir);

    if (numbered_files.empty()) {
        ESP_LOGW(TAG, "No numbered WAV files found on SD card");
        return ESP_ERR_NOT_FOUND;
    }

    // Sort by number
    std::sort(numbered_files.begin(), numbered_files.end(), 
              [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
                  return a.first < b.first;
              });

    // Extract sorted filenames
    for (const auto& pair : numbered_files) {
        filenames.push_back(pair.second);
    }

    ESP_LOGI(TAG, "Found %u numbered WAV file(s):", (unsigned int)filenames.size());
    for (size_t i = 0; i < filenames.size(); i++) {
        ESP_LOGI(TAG, "  %s", filenames[i].c_str());
    }

    return ESP_OK;
}

int WavPlayer::GetAnimationForWavNumber(int wav_number) {
    // Lookup table mapping wav file numbers to animation indices
    // 1 -> starry (ANIMATION_INSPIRATION)
    // 2 -> heart (ANIMATION_HAPPY)
    // 3 -> laugh (ANIMATION_LAUGH)
    // 4 -> smirk (ANIMATION_SMIRK)
    // 5 -> cry (ANIMATION_TALK)
    
    static const int wav_to_animation_map[] = {
        ANIMATION_INSPIRATION,  // 1.wav -> starry
        ANIMATION_HAPPY,         // 2.wav -> heart
        ANIMATION_LAUGH,         // 3.wav -> laugh
        ANIMATION_SMIRK,         // 4.wav -> smirk
        ANIMATION_TALK,          // 5.wav -> cry
    };
    
    const int map_size = sizeof(wav_to_animation_map) / sizeof(wav_to_animation_map[0]);
    
    // Check if wav_number is within the mapped range
    if (wav_number >= 1 && wav_number <= map_size) {
        return wav_to_animation_map[wav_number - 1];
    }
    
    // Fallback: use modulo for wav numbers beyond the lookup table
    // This allows additional wav files to cycle through animations
    int anim_index = (wav_number - 1) % ANIMATION_NUM;
    if (anim_index < 0) {
        anim_index = ANIMATION_STATIC_NORMAL;
    }
    
    return anim_index;
}

void WavPlayer::ResetSequentialIndex() {
    current_sequential_index_ = 0;
    numbered_wav_files_.clear();
    ESP_LOGI(TAG, "Reset sequential playback index");
}

esp_err_t WavPlayer::PlaySequentialWav(float gain) {
    // Check if already playing - if so, skip this call to prevent animation freezing
    if (is_playing_) {
        ESP_LOGW(TAG, "Playback already in progress, skipping duplicate call");
        return ESP_ERR_INVALID_STATE;
    }

    // If we don't have the numbered files list yet, or it's empty, find them
    if (numbered_wav_files_.empty()) {
        esp_err_t ret = FindNumberedWavFiles(numbered_wav_files_);
        if (ret != ESP_OK || numbered_wav_files_.empty()) {
            ESP_LOGW(TAG, "No numbered WAV files found for sequential playback");
            return ESP_ERR_NOT_FOUND;
        }
        // Reset index when we first load the files
        current_sequential_index_ = 0;
    }

    // Check if we've reached the end, loop back to start
    if (current_sequential_index_ >= (int)numbered_wav_files_.size()) {
        ESP_LOGI(TAG, "Reached end of sequence, looping back to start");
        current_sequential_index_ = 0;
    }

    // Get the current file to play
    std::string filename = numbered_wav_files_[current_sequential_index_];
    int wav_number = ExtractNumberFromFilename(filename);
    
    ESP_LOGI(TAG, "Playing sequential WAV %d/%d: %s", 
             current_sequential_index_ + 1, 
             (int)numbered_wav_files_.size(), 
             filename.c_str());

    // Get and set the matching animation
    int animation_index = GetAnimationForWavNumber(wav_number);
    ESP_LOGI(TAG, "Setting animation %d for wav number %d", animation_index, wav_number);
    animation_set_now_animation(animation_index);

    // Mark as playing
    is_playing_ = true;

    // Play the wav file
    esp_err_t ret = PlayWavFile(filename, gain);
    
    // Mark as not playing
    is_playing_ = false;
    
    if (ret == ESP_OK) {
        // Move to next file for next call
        current_sequential_index_++;
        ESP_LOGI(TAG, "Sequential playback completed, next index: %d", current_sequential_index_);
    } else {
        ESP_LOGE(TAG, "Failed to play sequential WAV file: %s", filename.c_str());
    }

    return ret;
}

