#include "no_audio_processor.h"
#include <esp_log.h>
#include <utility>

#define TAG "NoAudioProcessor"

void NoAudioProcessor::Initialize(AudioCodec* codec) {
    codec_ = codec;
}

void NoAudioProcessor::Feed(const std::vector<int16_t>& data) {
    if (!is_running_ || !output_callback_) {
        return;
    }

    // EchoEar's codec supplies interleaved microphone + playback-reference
    // samples. Without the AFE, send only the microphone channel to the mono
    // Opus encoder; forwarding both channels doubles the frame length and
    // treats the playback reference as microphone audio.
    if (codec_ != nullptr && codec_->input_channels() == 2) {
        std::vector<int16_t> microphone(data.size() / 2);
        for (size_t i = 0, j = 0; i < microphone.size(); ++i, j += 2) {
            microphone[i] = data[j];
        }
        output_callback_(std::move(microphone));
        return;
    }

    output_callback_(std::vector<int16_t>(data));
}

void NoAudioProcessor::Start() {
    is_running_ = true;
}

void NoAudioProcessor::Stop() {
    is_running_ = false;
}

bool NoAudioProcessor::IsRunning() {
    return is_running_;
}

void NoAudioProcessor::OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) {
    output_callback_ = callback;
}

void NoAudioProcessor::OnVadStateChange(std::function<void(bool speaking)> callback) {
    vad_state_change_callback_ = callback;
}

size_t NoAudioProcessor::GetFeedSize() {
    if (!codec_) {
        return 0;
    }
    // ReadAudio() is asked for 16 kHz target samples. Account for interleaved
    // input channels so the mono output contains one complete 60 ms Opus frame.
    constexpr size_t kTargetSampleRate = 16000;
    constexpr size_t kFrameDurationMs = 60;
    return kFrameDurationMs * kTargetSampleRate / 1000 * codec_->input_channels();
}

void NoAudioProcessor::EnableDeviceAec(bool enable) {
    if (enable) {
        ESP_LOGE(TAG, "Device AEC is not supported");
    }
}
