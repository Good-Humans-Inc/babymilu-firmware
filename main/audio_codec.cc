#include "audio_codec.h"

#include <driver/i2s_common.h>
#include <esp_log.h>

#define TAG "AudioCodec"

void AudioCodec::Start() {
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    EnableInput(true);
    EnableOutput(true);
    ESP_LOGI(TAG, "codec started");
}

void AudioCodec::SetOutputVolume(int volume) {
    output_volume_ = volume;
    ESP_LOGI(TAG, "output volume=%d", output_volume_);
}

void AudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) {
        return;
    }
    input_enabled_ = enable;
    ESP_LOGI(TAG, "input=%s", enable ? "on" : "off");
}

void AudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) {
        return;
    }
    output_enabled_ = enable;
    ESP_LOGI(TAG, "output=%s", enable ? "on" : "off");
}

void AudioCodec::OutputData(std::vector<int16_t>& data) {
    Write(data.data(), static_cast<int>(data.size()));
}

bool AudioCodec::InputData(std::vector<int16_t>& data) {
    return Read(data.data(), static_cast<int>(data.size())) > 0;
}

