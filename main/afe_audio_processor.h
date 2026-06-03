#pragma once

#include "audio_processor.h"

#include <esp_afe_sr_models.h>
#include <esp_mn_iface.h>
#include <esp_mn_models.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <atomic>
#include <functional>
#include <mutex>

class AfeAudioProcessor : public AudioProcessor {
public:
    struct WakeWordStats {
        uint32_t feed_calls = 0;
        uint32_t detect_calls = 0;
        int32_t last_samples = 0;
        int32_t last_peak = 0;
        int32_t last_avg_abs = 0;
        int32_t last_left_peak = 0;
        int32_t last_right_peak = 0;
        int32_t last_selected_channel = 0;
    };

    AfeAudioProcessor();
    ~AfeAudioProcessor() override;

    void Initialize(AudioCodec* codec) override;
    void Feed(const std::vector<int16_t>& data) override;
    void Start() override;
    void Stop() override;
    bool IsRunning() override;
    void OnOutput(std::function<void(const int16_t* data, size_t samples)> callback) override;
    void OnVadStateChange(std::function<void(bool speaking)> callback) override;
    void OnWakeWordDetected(std::function<void(const char* wake_word)> callback);
    void SetWakeWordDetectionEnabled(bool enable);
    bool IsWakeWordDetectionEnabled() const;
    bool IsWakeWordAvailable() const;
    WakeWordStats GetWakeWordStats() const;
    size_t GetFeedSize() override;
    void EnableDeviceAec(bool enable) override;

private:
    EventGroupHandle_t event_group_ = nullptr;
    const esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    srmodel_list_t* models_ = nullptr;
    AudioCodec* codec_ = nullptr;
    std::atomic<bool> streaming_active_{false};
    std::atomic<bool> wake_word_detection_enabled_{false};
    bool is_speaking_ = false;
    std::mutex feed_mutex_;
    std::function<void(const int16_t* data, size_t samples)> output_callback_;
    std::function<void(bool speaking)> vad_state_change_callback_;
    std::function<void(const char* wake_word)> wake_word_detected_callback_;

#if CONFIG_USE_CUSTOM_WAKE_WORD
    esp_mn_iface_t* multinet_iface_ = nullptr;
    model_iface_data_t* multinet_data_ = nullptr;
    std::vector<int16_t> wake_word_input_buffer_;
    std::atomic<bool> wake_word_commands_ready_{false};
#endif
    std::atomic<uint32_t> wake_word_feed_calls_{0};
    std::atomic<uint32_t> wake_word_detect_calls_{0};
    std::atomic<int32_t> wake_word_last_samples_{0};
    std::atomic<int32_t> wake_word_last_peak_{0};
    std::atomic<int32_t> wake_word_last_avg_abs_{0};
    std::atomic<int32_t> wake_word_last_left_peak_{0};
    std::atomic<int32_t> wake_word_last_right_peak_{0};
    std::atomic<int32_t> wake_word_last_selected_channel_{0};

    void AudioProcessorTask();
    void InitializeCustomWakeWord();
    void ProcessCustomWakeWord(const int16_t* data, size_t samples);
};
