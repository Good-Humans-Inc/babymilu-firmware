#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "display_animator.h"

class AudioCodec;

struct GifEntry {
    std::string name;
    uint32_t size = 0;
    uint32_t offset = 0;
};

struct LoadedGif {
    std::string name;
    uint8_t* data = nullptr;
    size_t size = 0;
};

class AnimationStore {
public:
    ~AnimationStore();

    esp_err_t Init();
    esp_err_t InitDisplay();
    esp_err_t InitStartupMedia();
    esp_err_t InitBundle();
    bool PlayStartupWav(AudioCodec* codec);
    void ShowStartup();
    void ShowEmotion(const std::string& emotion);
    void ShowUtility(const std::string& name);
    void SetBacklightBrightness(int percent);
    int AdjustBacklightBrightness(int delta);
    void RestoreBacklightBrightness();
    int backlight_brightness() const { return display_.backlight_brightness(); }
    void ShowStatusCanvas(const char* network_label, int network_percent, int battery_percent,
                          bool charging, uint32_t duration_ms = 2000);
    void ShowValueOverlay(const char* label, int percent, uint32_t duration_ms = 1000);
    void ClearOverlay();
    void TriggerUpdateCheck(const std::string& test_bin_url = "");
    void ClearCache();

private:
    static void UpdateTask(void* arg);

    bool MountSdCard();
    bool ValidateBundle(const char* path, std::vector<GifEntry>* entries = nullptr);
    bool ValidateBundleFile(const char* path);
    bool ValidateGifFile(const char* path);
    bool ValidateWavFile(const char* path);
    LoadedGif* LoadGifByName(const std::string& name, bool log_missing = true);
    LoadedGif* LoadFirstAvailable(const char* const* candidates, bool log_missing = true);
    bool LoadRawFile(const char* path, LoadedGif& out);
    bool ExtractGif(const GifEntry& entry, LoadedGif& out);
    void RunUpdateCheck();
    bool DownloadBundleToTempAndSwap(const std::string& url);
    bool DownloadStartupGifToTempAndSwap(const std::string& url);
    bool DownloadStartupWavToTempAndSwap(const std::string& url);
    bool DownloadToTempAndSwap(const std::string& url, const char* temp_path, const char* final_path,
                               bool (AnimationStore::*validator)(const char*), bool skip_same_size);
    bool FetchRemoteContentLength(const std::string& url, size_t* out_length);
    bool mounted_ = false;
    bool bundle_ready_ = false;
    bool startup_ready_ = false;
    DisplayAnimator display_;
    LoadedGif startup_gif_;
    std::vector<GifEntry> entries_;
    std::map<std::string, LoadedGif> cache_;
    std::mutex mutex_;
    std::atomic<bool> update_running_{false};
    TaskHandle_t update_task_ = nullptr;
};
