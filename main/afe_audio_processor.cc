#include "afe_audio_processor.h"

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mn_speech_commands.h>
#include <freertos/idf_additions.h>

#include <string>

#define TAG "AfeAudioProcessor"
#define PROCESSOR_RUNNING 0x01
static constexpr uint32_t kAfeFetchTaskStackSize = 4096 * 2;
static constexpr UBaseType_t kTaskStackMemoryCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

AfeAudioProcessor::AfeAudioProcessor() {
    event_group_ = xEventGroupCreate();
}

AfeAudioProcessor::~AfeAudioProcessor() {
    if (afe_data_ != nullptr) {
        afe_iface_->destroy(afe_data_);
    }
#if CONFIG_USE_CUSTOM_WAKE_WORD
    if (multinet_data_ != nullptr && multinet_iface_ != nullptr) {
        multinet_iface_->destroy(multinet_data_);
    }
#endif
    if (models_ != nullptr) {
        esp_srmodel_deinit(models_);
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

    models_ = esp_srmodel_init("model");
    char* ns_model_name = esp_srmodel_filter(models_, ESP_NSNET_PREFIX, nullptr);

    afe_config_t* afe_config = afe_config_init(input_format.c_str(), nullptr, AFE_TYPE_VC, AFE_MODE_HIGH_PERF);
    afe_config->aec_mode = AEC_MODE_VOIP_HIGH_PERF;
    afe_config->agc_init = true;
    afe_config->afe_perferred_core = 1;
    afe_config->afe_perferred_priority = 1;
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

    if (ns_model_name != nullptr) {
        afe_config->ns_init = true;
        afe_config->ns_model_name = ns_model_name;
        afe_config->afe_ns_mode = AFE_NS_MODE_NET;
    }

#if CONFIG_USE_DEVICE_AEC
    afe_config->aec_init = true;
#else
    afe_config->aec_init = false;
#endif
    afe_config->vad_init = false;

    afe_iface_ = esp_afe_handle_from_config(afe_config);
    afe_data_ = afe_iface_->create_from_config(afe_config);
    ESP_LOGI(TAG, "AFE initialized input_format=%s aec=%d vad=%d; server-side ASR owns VAD",
             input_format.c_str(),
             afe_config->aec_init, afe_config->vad_init);
    InitializeCustomWakeWord();

    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        [](void* arg) {
            static_cast<AfeAudioProcessor*>(arg)->AudioProcessorTask();
        },
        "afe_fetch", kAfeFetchTaskStackSize, this, 3, nullptr, 1, kTaskStackMemoryCaps);
    ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_FAIL);
}

size_t AfeAudioProcessor::GetFeedSize() {
    if (afe_data_ == nullptr) {
        return 0;
    }
#if CONFIG_USE_CUSTOM_WAKE_WORD
    if (!streaming_active_.load() && wake_word_detection_enabled_.load() &&
        multinet_data_ != nullptr && multinet_iface_ != nullptr) {
        const int chunksize = multinet_iface_->get_samp_chunksize(multinet_data_);
        if (chunksize > 0) {
            return static_cast<size_t>(chunksize) * codec_->input_channels();
        }
    }
#endif
    return afe_iface_->get_feed_chunksize(afe_data_) * codec_->input_channels();
}

void AfeAudioProcessor::Feed(const std::vector<int16_t>& data) {
    if (afe_data_ == nullptr || data.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(feed_mutex_);
    if (streaming_active_.load()) {
        afe_iface_->feed(afe_data_, data.data());
        return;
    }

    if (wake_word_detection_enabled_.load()) {
        if (codec_->input_channels() == 2) {
            std::vector<int16_t> mono;
            mono.reserve(data.size() / 2);
            int64_t left_sum_abs = 0;
            int64_t right_sum_abs = 0;
            int32_t left_peak = 0;
            int32_t right_peak = 0;
            for (size_t i = 0; i + 1 < data.size(); i += 2) {
                const int32_t left = data[i];
                const int32_t right = data[i + 1];
                const int32_t left_abs = left < 0 ? -left : left;
                const int32_t right_abs = right < 0 ? -right : right;
                left_sum_abs += left_abs;
                right_sum_abs += right_abs;
                if (left_abs > left_peak) {
                    left_peak = left_abs;
                }
                if (right_abs > right_peak) {
                    right_peak = right_abs;
                }
            }
            const bool use_right = right_sum_abs > left_sum_abs;
            wake_word_last_left_peak_ = left_peak;
            wake_word_last_right_peak_ = right_peak;
            wake_word_last_selected_channel_ = use_right ? 1 : 0;
            for (size_t i = use_right ? 1 : 0; i < data.size(); i += 2) {
                mono.push_back(data[i]);
            }
            ProcessCustomWakeWord(mono.data(), mono.size());
        } else {
            wake_word_last_selected_channel_ = 0;
            ProcessCustomWakeWord(data.data(), data.size());
        }
    }
}

void AfeAudioProcessor::Start() {
    streaming_active_ = true;
    xEventGroupSetBits(event_group_, PROCESSOR_RUNNING);
}

void AfeAudioProcessor::Stop() {
    streaming_active_ = false;
    xEventGroupClearBits(event_group_, PROCESSOR_RUNNING);
    std::lock_guard<std::mutex> lock(feed_mutex_);
    if (afe_data_ != nullptr) {
        afe_iface_->reset_buffer(afe_data_);
    }
    is_speaking_ = false;
}

bool AfeAudioProcessor::IsRunning() {
    return streaming_active_.load() || wake_word_detection_enabled_.load();
}

void AfeAudioProcessor::OnOutput(std::function<void(const int16_t* data, size_t samples)> callback) {
    output_callback_ = std::move(callback);
}

void AfeAudioProcessor::OnVadStateChange(std::function<void(bool speaking)> callback) {
    vad_state_change_callback_ = std::move(callback);
}

void AfeAudioProcessor::OnWakeWordDetected(std::function<void(const char* wake_word)> callback) {
    wake_word_detected_callback_ = std::move(callback);
}

void AfeAudioProcessor::SetWakeWordDetectionEnabled(bool enable) {
#if CONFIG_USE_CUSTOM_WAKE_WORD
    const bool available = IsWakeWordAvailable();
    const bool should_enable = enable && available;
    const bool was_enabled = wake_word_detection_enabled_.exchange(should_enable);
    if (was_enabled == should_enable) {
        return;
    }
    if (should_enable) {
        ESP_LOGI(TAG, "custom wake word listening enabled");
    } else {
        std::lock_guard<std::mutex> lock(feed_mutex_);
        wake_word_input_buffer_.clear();
        if (multinet_data_ != nullptr) {
            multinet_iface_->clean(multinet_data_);
        }
        ESP_LOGI(TAG, "custom wake word listening disabled");
    }
#else
    (void)enable;
#endif
}

bool AfeAudioProcessor::IsWakeWordDetectionEnabled() const {
#if CONFIG_USE_CUSTOM_WAKE_WORD
    return wake_word_detection_enabled_.load();
#else
    return false;
#endif
}

bool AfeAudioProcessor::IsWakeWordAvailable() const {
#if CONFIG_USE_CUSTOM_WAKE_WORD
    return multinet_data_ != nullptr && multinet_iface_ != nullptr && wake_word_commands_ready_.load();
#else
    return false;
#endif
}

AfeAudioProcessor::WakeWordStats AfeAudioProcessor::GetWakeWordStats() const {
    WakeWordStats stats = {};
#if CONFIG_USE_CUSTOM_WAKE_WORD
    stats.feed_calls = wake_word_feed_calls_.load();
    stats.detect_calls = wake_word_detect_calls_.load();
    stats.last_samples = wake_word_last_samples_.load();
    stats.last_peak = wake_word_last_peak_.load();
    stats.last_avg_abs = wake_word_last_avg_abs_.load();
    stats.last_left_peak = wake_word_last_left_peak_.load();
    stats.last_right_peak = wake_word_last_right_peak_.load();
    stats.last_selected_channel = wake_word_last_selected_channel_.load();
#endif
    return stats;
}

void AfeAudioProcessor::AudioProcessorTask() {
    ESP_LOGI(TAG, "AFE fetch task running stack=%lu feed=%d fetch=%d",
             static_cast<unsigned long>(kAfeFetchTaskStackSize),
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
            output_callback_(res->data, res->data_size / sizeof(int16_t));
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void AfeAudioProcessor::InitializeCustomWakeWord() {
#if CONFIG_USE_CUSTOM_WAKE_WORD
    if (models_ == nullptr || models_->num == -1) {
        ESP_LOGW(TAG, "custom wake word unavailable: model list not loaded");
        return;
    }

    const char* preferred_language = nullptr;
#if CONFIG_SR_MN_EN_MULTINET5_SINGLE_RECOGNITION_QUANT8 || CONFIG_SR_MN_EN_MULTINET6_QUANT || CONFIG_SR_MN_EN_MULTINET7_QUANT
    preferred_language = "en";
#elif CONFIG_SR_MN_CN_MULTINET5_RECOGNITION_QUANT8 || CONFIG_SR_MN_CN_MULTINET6_QUANT || CONFIG_SR_MN_CN_MULTINET6_AC_QUANT || CONFIG_SR_MN_CN_MULTINET7_QUANT || CONFIG_SR_MN_CN_MULTINET7_AC_QUANT
    preferred_language = "cn";
#endif
    char* model_name = esp_srmodel_filter(models_, ESP_MN_PREFIX, preferred_language);
    if (model_name == nullptr && preferred_language != nullptr) {
        ESP_LOGE(TAG, "custom wake word unavailable: preferred language '%s' MultiNet model missing from model partition",
                 preferred_language);
        return;
    }
    if (model_name == nullptr) {
        model_name = esp_srmodel_filter(models_, ESP_MN_PREFIX, nullptr);
    }
    if (model_name == nullptr) {
        ESP_LOGW(TAG, "custom wake word unavailable: no MultiNet model in model partition");
        return;
    }

    multinet_iface_ = esp_mn_handle_from_name(model_name);
    if (multinet_iface_ == nullptr) {
        ESP_LOGW(TAG, "custom wake word unavailable: failed to get MultiNet handle for %s", model_name);
        return;
    }

    multinet_data_ = multinet_iface_->create(model_name, 3000);
    if (multinet_data_ == nullptr) {
        ESP_LOGW(TAG, "custom wake word unavailable: failed to create MultiNet model %s", model_name);
        multinet_iface_ = nullptr;
        return;
    }

    const float threshold = CONFIG_CUSTOM_WAKE_WORD_THRESHOLD / 100.0f;
    multinet_iface_->set_det_threshold(multinet_data_, threshold);
    esp_mn_commands_clear();
    esp_err_t add_err = esp_mn_commands_add(1, CONFIG_CUSTOM_WAKE_WORD);
    if (add_err != ESP_OK) {
        ESP_LOGE(TAG, "custom wake word command rejected phrase=\"%s\" model=%s err=%s",
                 CONFIG_CUSTOM_WAKE_WORD, model_name, esp_err_to_name(add_err));
        return;
    }
    esp_mn_error_t* update_error = esp_mn_commands_update();
    if (update_error != nullptr && update_error->num > 0) {
        ESP_LOGE(TAG, "custom wake word command update failed count=%d phrase=\"%s\" model=%s",
                 update_error->num, CONFIG_CUSTOM_WAKE_WORD, model_name);
        for (int i = 0; i < update_error->num; ++i) {
            esp_mn_phrase_t* phrase = update_error->phrases[i];
            ESP_LOGE(TAG, "custom wake word rejected active phrase id=%d text=\"%s\" phonemes=\"%s\"",
                     phrase != nullptr ? phrase->command_id : -1,
                     phrase != nullptr && phrase->string != nullptr ? phrase->string : "<null>",
                     phrase != nullptr && phrase->phonemes != nullptr ? phrase->phonemes : "<null>");
        }
        return;
    }
    wake_word_commands_ready_ = true;
    multinet_iface_->print_active_speech_commands(multinet_data_);
    ESP_LOGI(TAG, "custom wake word enabled model=%s language=%s phrase=\"%s\" threshold=%.2f chunk=%d",
             model_name,
             preferred_language != nullptr ? preferred_language : "<any>",
             CONFIG_CUSTOM_WAKE_WORD,
             threshold,
             multinet_iface_->get_samp_chunksize(multinet_data_));
#endif
}

void AfeAudioProcessor::ProcessCustomWakeWord(const int16_t* data, size_t samples) {
#if CONFIG_USE_CUSTOM_WAKE_WORD
    if (multinet_data_ == nullptr || multinet_iface_ == nullptr || !wake_word_commands_ready_.load() ||
        data == nullptr || samples == 0) {
        return;
    }

    int32_t peak = 0;
    int64_t sum_abs = 0;
    for (size_t i = 0; i < samples; ++i) {
        int32_t value = data[i];
        int32_t abs_value = value < 0 ? -value : value;
        if (abs_value > peak) {
            peak = abs_value;
        }
        sum_abs += abs_value;
    }
    wake_word_feed_calls_.fetch_add(1);
    wake_word_last_samples_ = static_cast<int32_t>(samples);
    wake_word_last_peak_ = peak;
    wake_word_last_avg_abs_ = static_cast<int32_t>(sum_abs / static_cast<int64_t>(samples));

    wake_word_input_buffer_.insert(wake_word_input_buffer_.end(), data, data + samples);
    const int chunksize = multinet_iface_->get_samp_chunksize(multinet_data_);
    while (chunksize > 0 && wake_word_input_buffer_.size() >= static_cast<size_t>(chunksize)) {
        wake_word_detect_calls_.fetch_add(1);
        esp_mn_state_t state = multinet_iface_->detect(multinet_data_, wake_word_input_buffer_.data());
        wake_word_input_buffer_.erase(wake_word_input_buffer_.begin(), wake_word_input_buffer_.begin() + chunksize);

        if (state == ESP_MN_STATE_DETECTED) {
            esp_mn_results_t* results = multinet_iface_->get_results(multinet_data_);
            if (results != nullptr && results->num > 0) {
                ESP_LOGI(TAG, "custom wake word detected phrase=\"%s\" prob=%.3f",
                         CONFIG_CUSTOM_WAKE_WORD_DISPLAY, results->prob[0]);
            } else {
                ESP_LOGI(TAG, "custom wake word detected phrase=\"%s\"", CONFIG_CUSTOM_WAKE_WORD_DISPLAY);
            }
            wake_word_detection_enabled_ = false;
            wake_word_input_buffer_.clear();
            multinet_iface_->clean(multinet_data_);
            if (!streaming_active_.load()) {
                xEventGroupClearBits(event_group_, PROCESSOR_RUNNING);
            }
            if (wake_word_detected_callback_) {
                wake_word_detected_callback_(CONFIG_CUSTOM_WAKE_WORD_DISPLAY);
            }
            break;
        }
        if (state == ESP_MN_STATE_TIMEOUT) {
            multinet_iface_->clean(multinet_data_);
        }
    }
#else
    (void)data;
    (void)samples;
#endif
}

void AfeAudioProcessor::EnableDeviceAec(bool enable) {
    if (afe_data_ == nullptr) {
        return;
    }
    if (enable) {
#if CONFIG_USE_DEVICE_AEC
        afe_iface_->enable_aec(afe_data_);
        ESP_LOGI(TAG, "device AEC enabled; local AFE VAD remains disabled");
#else
        ESP_LOGE(TAG, "device AEC is not built in");
#endif
    } else {
        afe_iface_->disable_aec(afe_data_);
        ESP_LOGI(TAG, "device AEC disabled; local AFE VAD remains disabled");
    }
}
