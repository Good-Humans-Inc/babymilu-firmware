#pragma once

#include "audio_processor.h"

#include <esp_afe_sr_models.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <functional>

class AfeAudioProcessor : public AudioProcessor {
public:
    AfeAudioProcessor();
    ~AfeAudioProcessor() override;

    void Initialize(AudioCodec* codec) override;
    void Feed(const std::vector<int16_t>& data) override;
    void Start() override;
    void Stop() override;
    bool IsRunning() override;
    void OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) override;
    void OnVadStateChange(std::function<void(bool speaking)> callback) override;
    size_t GetFeedSize() override;
    void EnableDeviceAec(bool enable) override;

private:
    EventGroupHandle_t event_group_ = nullptr;
    esp_afe_sr_iface_t* afe_iface_ = nullptr;
    esp_afe_sr_data_t* afe_data_ = nullptr;
    AudioCodec* codec_ = nullptr;
    bool is_speaking_ = false;
    std::function<void(std::vector<int16_t>&& data)> output_callback_;
    std::function<void(bool speaking)> vad_state_change_callback_;

    void AudioProcessorTask();
};

