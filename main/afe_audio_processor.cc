#include "afe_audio_processor.h"

#include <esp_log.h>

#include <string>

#define TAG "AfeAudioProcessor"
#define PROCESSOR_RUNNING 0x01

AfeAudioProcessor::AfeAudioProcessor() {
    event_group_ = xEventGroupCreate();
}

AfeAudioProcessor::~AfeAudioProcessor() {
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
    }
}

void AfeAudioProcessor::Initialize(AudioCodec* codec) {
    codec_ = codec;
    const int ref_num = codec_->input_reference() ? 1 : 0;

    std::string input_format;
    for (int i = 0; i < codec_->input_channels() - ref_num; ++i) {
        input_format.push_back('M');
    }
    for (int i = 0; i < ref_num; ++i) {
        input_format.push_back('R');
    }

    srmodel_list_t* models = esp_srmodel_init("model");
    char* ns_model_name = esp_srmodel_filter(models, ESP_NSNET_PREFIX, nullptr);
    char* vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, nullptr);

    afe_config_t* afe_config = afe_config_init(input_format.c_str(), nullptr, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
    afe_config->aec_mode = AEC_MODE_VOIP_HIGH_PERF;
    afe_config->vad_mode = VAD_MODE_0;
    afe_config->vad_min_noise_ms = 100;
    afe_config->agc_init = true;
    afe_config->afe_perferred_core = 1;
    afe_config->afe_perferred_priority = 1;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

    if (vad_model_name != nullptr) {
        afe_config->vad_model_name = vad_model_name;
    }
    if (ns_model_name != nullptr) {
        afe_config->ns_init = true;
        afe_config->ns_model_name = ns_model_name;
        afe_config->afe_ns_mode = AFE_NS_MODE_NET;
    }

#if CONFIG_USE_DEVICE_AEC
    afe_config->aec_init = true;
    afe_config->vad_init = true;
#else
    afe_config->aec_init = false;
    afe_config->vad_init = true;
#endif

    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_->create_from_config(afe_config);
    ESP_LOGI(TAG, "AFE initialized input_format=%s aec=%d vad=%d", input_format.c_str(),
             afe_config->aec_init, afe_config->vad_init);

    xTaskCreate(
        [](void* arg) {
            static_cast<AfeAudioProcessor*>(arg)->AudioProcessorTask();
            vTaskDelete(nullptr);
        },
        "afe_fetch", 4096, this, 3, nullptr);
}

size_t AfeAudioProcessor::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
    return afe_iface_->get_feed_chunksize(afe_data_) * codec_->input_channels();
}

void AfeAudioProcessor::Feed(const std::vector<int16_t>& data) {
    if (afe_data_ != nullptr && !data.empty()) {
        afe_iface_->feed(afe_data_, data.data());
    }
}

void AfeAudioProcessor::Start() {
    xEventGroupSetBits(event_group_, PROCESSOR_RUNNING);
}

void AfeAudioProcessor::Stop() {
    xEventGroupClearBits(event_group_, PROCESSOR_RUNNING);
    if (afe_data_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
}

bool AfeAudioProcessor::IsRunning() {
    return (xEventGroupGetBits(event_group_) & PROCESSOR_RUNNING) != 0;
}

void AfeAudioProcessor::OnOutput(std::function<void(std::vector<int16_t>&& data)> callback) {
    output_callback_ = std::move(callback);
}

void AfeAudioProcessor::OnVadStateChange(std::function<void(bool speaking)> callback) {
    vad_state_change_callback_ = std::move(callback);
}

void AfeAudioProcessor::AudioProcessorTask() {
    ESP_LOGI(TAG, "AFE fetch task running feed=%d fetch=%d",
             afe_iface_->get_feed_chunksize(afe_data_),
             afe_iface_->get_fetch_chunksize(afe_data_));

    while (true) {
        xEventGroupWaitBits(event_group_, PROCESSOR_RUNNING, pdFALSE, pdTRUE, portMAX_DELAY);
        auto res = afe_iface_->fetch_with_delay(afe_data_, portMAX_DELAY);
        if (!IsRunning()) {
            continue;
        }
        if (res == nullptr || res->ret_value == ESP_FAIL) {
            continue;
        }

        if (vad_state_change_callback_) {
            if (res->vad_state == VAD_SPEECH && !is_speaking_) {
                is_speaking_ = true;
                vad_state_change_callback_(true);
            } else if (res->vad_state == VAD_SILENCE && is_speaking_) {
                is_speaking_ = false;
                vad_state_change_callback_(false);
            }
        }

        if (output_callback_) {
            output_callback_(std::vector<int16_t>(res->data, res->data + res->data_size / sizeof(int16_t)));
        }
    }
}

void AfeAudioProcessor::EnableDeviceAec(bool enable) {
    if (afe_data_ == nullptr) {
        return;
    }
    if (enable) {
#if CONFIG_USE_DEVICE_AEC
        afe_iface_->enable_aec(afe_data_);
        afe_iface_->enable_vad(afe_data_);
        ESP_LOGI(TAG, "device AEC and VAD enabled");
#else
        ESP_LOGE(TAG, "device AEC is not built in");
#endif
    } else {
        afe_iface_->disable_aec(afe_data_);
        afe_iface_->enable_vad(afe_data_);
    }
}
