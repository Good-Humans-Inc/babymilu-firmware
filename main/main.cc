#include "afe_audio_processor.h"
#include "box_audio_codec.h"
#include "config.h"

#include <cJSON.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/idf_additions.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <opus_decoder.h>
#include <opus_resampler.h>
#include <opus.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define TAG "EchoEarGround"
#define WIFI_CONNECTED_BIT BIT0
#define SERVER_READY_BIT BIT1
static constexpr int kAfeSampleRate = 16000;
static constexpr gpio_num_t kBootButtonGpio = GPIO_NUM_0;
static constexpr uint32_t kAudioFeedTaskStackSize = 4096 * 3;
static constexpr uint32_t kAudioEncodeTaskStackSize = 4096 * 10;
static constexpr uint32_t kNetworkTaskStackSize = 4096 * 3;
static constexpr uint32_t kAudioPlaybackTaskStackSize = 4096 * 4;
static constexpr uint32_t kControlTaskStackSize = 4096 * 2;
static constexpr uint32_t kVadSilenceStopMs = 1000;
static constexpr bool kEnableTtsVadInterrupt = false;
static constexpr uint32_t kTtsVadGraceMs = 1000;
static constexpr uint32_t kTtsVadDebounceMs = 400;
static constexpr uint32_t kTtsPlaybackTailMs = 500;
static constexpr UBaseType_t kPcmQueueDepth = 4;
static constexpr UBaseType_t kOpusQueueDepth = 8;
static constexpr UBaseType_t kIncomingOpusQueueDepth = 12;
static constexpr size_t kMaxPcmFrameSamples = 2048;
static constexpr size_t kMaxOpusFrameBytes = 1000;
static constexpr size_t kMaxIncomingOpusFrameBytes = 1000;
static constexpr UBaseType_t kInternalMemoryCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

struct PcmFrame {
    uint16_t samples = 0;
    int16_t data[kMaxPcmFrameSamples] = {};
};

struct OpusFrame {
    uint16_t bytes = 0;
    uint8_t data[kMaxOpusFrameBytes] = {};
};

struct IncomingOpusFrame {
    uint16_t bytes = 0;
    uint8_t data[kMaxIncomingOpusFrameBytes] = {};
};

class EchoEarGroundApp {
public:
    static EchoEarGroundApp& Instance() {
        static EchoEarGroundApp app;
        return app;
    }

    void Start() {
        ESP_ERROR_CHECK(nvs_flash_init());
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        InitPowerHold();
        InitBootButton();
        InitWifi();
        InitAudio();
        StartAudioTasks();
        ESP_LOGI(TAG, "startup ready; Wi-Fi driver owns buffers, WebSocket/session handshake is deferred until BOOT");
    }

private:
    EventGroupHandle_t wifi_events_ = xEventGroupCreate();
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    std::unique_ptr<BoxAudioCodec> codec_;
    std::unique_ptr<AfeAudioProcessor> processor_;
    OpusEncoder* opus_encoder_ = nullptr;
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;
    OpusResampler input_resampler_;
    OpusResampler reference_resampler_;
    OpusResampler output_resampler_;
    esp_websocket_client_handle_t websocket_ = nullptr;
    QueueHandle_t free_pcm_queue_ = nullptr;
    QueueHandle_t filled_pcm_queue_ = nullptr;
    QueueHandle_t free_opus_queue_ = nullptr;
    QueueHandle_t filled_opus_queue_ = nullptr;
    QueueHandle_t free_incoming_opus_queue_ = nullptr;
    QueueHandle_t filled_incoming_opus_queue_ = nullptr;
    PcmFrame pcm_pool_[kPcmQueueDepth];
    OpusFrame opus_pool_[kOpusQueueDepth];
    IncomingOpusFrame incoming_opus_pool_[kIncomingOpusQueueDepth];
    std::mutex websocket_send_mutex_;
    std::mutex opus_decoder_mutex_;
    std::string session_id_;
    std::atomic<bool> ws_connected_{false};
    std::atomic<bool> server_ready_{false};
    std::atomic<bool> streaming_{false};
    std::atomic<bool> conversation_active_{false};
    std::atomic<bool> playing_tts_{false};
    std::atomic<bool> vad_speaking_{false};
    std::atomic<bool> heard_speech_{false};
    std::atomic<bool> pending_abort_{false};
    std::atomic<bool> pending_listen_restart_{false};
    std::atomic<uint32_t> silence_since_ms_{0};
    std::atomic<uint32_t> tts_started_ms_{0};
    std::atomic<uint32_t> tts_stop_ms_{0};
    std::atomic<uint32_t> tts_vad_speech_since_ms_{0};
    std::atomic<int> incoming_tts_frames_{0};
    std::vector<int16_t> mic_buffer_;
    std::vector<int16_t> reference_buffer_;
    std::vector<int16_t> resampled_input_buffer_;
    std::vector<int16_t> resampled_mic_buffer_;
    std::vector<int16_t> resampled_reference_buffer_;
    std::vector<int16_t> resampled_output_buffer_;
    std::vector<int16_t> opus_input_buffer_;
    StackType_t* audio_encode_task_stack_ = nullptr;
    StaticTask_t audio_encode_task_buffer_ = {};
    TaskHandle_t audio_encode_task_handle_ = nullptr;
    int opus_frame_samples_ = 0;

    static void InitPowerHold() {
        gpio_config_t config = {
            .pin_bit_mask = BIT64(POWER_CTRL),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&config));
        gpio_set_level(POWER_CTRL, 0);
    }

    static uint32_t NowMs() {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000);
    }

    static void LogHeap(const char* label) {
        ESP_LOGI(TAG, "%s heap internal=%u psram=%u",
                 label,
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    }

    static void InitBootButton() {
        gpio_config_t config = {
            .pin_bit_mask = BIT64(kBootButtonGpio),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&config));
    }

    void InitAudio() {
        i2c_master_bus_config_t i2c_bus_config = {};
        i2c_bus_config.i2c_port = I2C_NUM_1;
        i2c_bus_config.sda_io_num = AUDIO_CODEC_I2C_SDA_PIN;
        i2c_bus_config.scl_io_num = AUDIO_CODEC_I2C_SCL_PIN;
        i2c_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        i2c_bus_config.glitch_ignore_cnt = 7;
        i2c_bus_config.flags.enable_internal_pullup = 1;
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus_));

        codec_ = std::make_unique<BoxAudioCodec>(
            i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);

        codec_->SetOutputVolume(70);
        codec_->Start();

        int opus_error = OPUS_OK;
        opus_encoder_ = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &opus_error);
        ESP_ERROR_CHECK(opus_encoder_ != nullptr && opus_error == OPUS_OK ? ESP_OK : ESP_FAIL);
        opus_encoder_ctl(opus_encoder_, OPUS_SET_DTX(1));
        opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(0));
        opus_frame_samples_ = 16000 / 1000 * OPUS_FRAME_DURATION_MS;
        opus_input_buffer_.reserve(kMaxPcmFrameSamples * 4);
        opus_decoder_ = std::make_unique<OpusDecoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
        if (opus_decoder_->sample_rate() != codec_->output_sample_rate()) {
            output_resampler_.Configure(opus_decoder_->sample_rate(), codec_->output_sample_rate());
        }
        if (codec_->input_sample_rate() != kAfeSampleRate) {
            input_resampler_.Configure(codec_->input_sample_rate(), kAfeSampleRate);
            reference_resampler_.Configure(codec_->input_sample_rate(), kAfeSampleRate);
        }

        processor_ = std::make_unique<AfeAudioProcessor>();
        processor_->Initialize(codec_.get());
        processor_->EnableDeviceAec(true);
        processor_->OnVadStateChange([this](bool speaking) { OnVadChange(speaking); });
        processor_->OnOutput([this](const int16_t* pcm, size_t samples) { OnProcessedAudio(pcm, samples); });
    }

    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
        auto* self = static_cast<EchoEarGroundApp*>(arg);
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            xEventGroupClearBits(self->wifi_events_, WIFI_CONNECTED_BIT | SERVER_READY_BIT);
            ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting");
            esp_wifi_connect();
        } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            xEventGroupSetBits(self->wifi_events_, WIFI_CONNECTED_BIT);
            ESP_LOGI(TAG, "Wi-Fi connected");
        }
    }

    void InitWifi() {
        if (std::string(CONFIG_ECHOEAR_WIFI_SSID).empty()) {
            ESP_LOGW(TAG, "CONFIG_ECHOEAR_WIFI_SSID is empty; audio starts but WebSocket will not connect");
            return;
        }

        esp_netif_create_default_wifi_sta();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEventHandler, this, nullptr));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiEventHandler, this, nullptr));

        wifi_config_t wifi_config = {};
        snprintf(reinterpret_cast<char*>(wifi_config.sta.ssid), sizeof(wifi_config.sta.ssid), "%s", CONFIG_ECHOEAR_WIFI_SSID);
        snprintf(reinterpret_cast<char*>(wifi_config.sta.password), sizeof(wifi_config.sta.password), "%s", CONFIG_ECHOEAR_WIFI_PASSWORD);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    bool EnsureWebSocketReady() {
        if (std::string(CONFIG_ECHOEAR_WIFI_SSID).empty()) {
            ESP_LOGW(TAG, "BOOT listen ignored: Wi-Fi SSID is empty");
            return false;
        }

        if (server_ready_ && ws_connected_) {
            return true;
        }

        ESP_LOGI(TAG, "BOOT waiting for Wi-Fi before WebSocket open");
        EventBits_t bits = xEventGroupWaitBits(
            wifi_events_, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
        if ((bits & WIFI_CONNECTED_BIT) == 0) {
            ESP_LOGW(TAG, "BOOT listen ignored: Wi-Fi not connected");
            return false;
        }

        if (websocket_ == nullptr) {
            ESP_LOGI(TAG, "BOOT opening WebSocket: %s", CONFIG_ECHOEAR_WEBSOCKET_URL);
            server_ready_ = false;
            session_id_.clear();
            xEventGroupClearBits(wifi_events_, SERVER_READY_BIT);

            esp_websocket_client_config_t ws_cfg = {};
            ws_cfg.uri = CONFIG_ECHOEAR_WEBSOCKET_URL;
            ws_cfg.disable_auto_reconnect = false;
            ws_cfg.reconnect_timeout_ms = 10000;
            ws_cfg.network_timeout_ms = 10000;
            websocket_ = esp_websocket_client_init(&ws_cfg);
            if (websocket_ == nullptr) {
                ESP_LOGE(TAG, "failed to create WebSocket client");
                return false;
            }

            esp_err_t err = esp_websocket_register_events(websocket_, WEBSOCKET_EVENT_ANY, &WebSocketEventHandler, this);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "failed to register WebSocket events: %s", esp_err_to_name(err));
                esp_websocket_client_destroy(websocket_);
                websocket_ = nullptr;
                return false;
            }

            err = esp_websocket_client_start(websocket_);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "failed to start WebSocket client: %s", esp_err_to_name(err));
                esp_websocket_client_destroy(websocket_);
                websocket_ = nullptr;
                return false;
            }
        } else {
            ESP_LOGI(TAG, "BOOT waiting for existing WebSocket/server hello");
        }

        bits = xEventGroupWaitBits(wifi_events_, SERVER_READY_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
        if ((bits & SERVER_READY_BIT) == 0 || !server_ready_ || !ws_connected_) {
            ESP_LOGW(TAG, "BOOT listen ignored: server hello timeout");
            return false;
        }
        return true;
    }

    static void WebSocketEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
        auto* self = static_cast<EchoEarGroundApp*>(handler_args);
        auto* data = static_cast<esp_websocket_event_data_t*>(event_data);

        switch (event_id) {
            case WEBSOCKET_EVENT_CONNECTED:
                self->ws_connected_ = true;
                self->server_ready_ = false;
                self->session_id_.clear();
                xEventGroupClearBits(self->wifi_events_, SERVER_READY_BIT);
                ESP_LOGI(TAG, "WebSocket connected");
                self->SendHello();
                break;
            case WEBSOCKET_EVENT_DISCONNECTED:
                self->ws_connected_ = false;
                self->server_ready_ = false;
                self->streaming_ = false;
                self->conversation_active_ = false;
                self->playing_tts_ = false;
                self->heard_speech_ = false;
                self->vad_speaking_ = false;
                self->pending_abort_ = false;
                self->pending_listen_restart_ = false;
                self->silence_since_ms_ = 0;
                if (self->processor_) {
                    self->processor_->Stop();
                }
                xEventGroupClearBits(self->wifi_events_, SERVER_READY_BIT);
                ESP_LOGW(TAG, "WebSocket disconnected");
                break;
            case WEBSOCKET_EVENT_DATA:
                if (data->op_code == 0x2) {
                    self->HandleBinary(data);
                } else if (data->op_code == 0x1) {
                    self->HandleText(data);
                }
                break;
            case WEBSOCKET_EVENT_ERROR:
                ESP_LOGE(TAG, "WebSocket error");
                break;
            default:
                break;
        }
    }

    void SendHello() {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "hello");
        cJSON_AddNumberToObject(root, "version", 3);
        cJSON_AddStringToObject(root, "transport", "websocket");
        cJSON* audio = cJSON_CreateObject();
        cJSON_AddStringToObject(audio, "format", "opus");
        cJSON_AddNumberToObject(audio, "sample_rate", 16000);
        cJSON_AddNumberToObject(audio, "channels", 1);
        cJSON_AddNumberToObject(audio, "frame_duration", OPUS_FRAME_DURATION_MS);
        cJSON_AddItemToObject(root, "audio_params", audio);
        char* text = cJSON_PrintUnformatted(root);
        SendText(text);
        cJSON_free(text);
        cJSON_Delete(root);
    }

    void HandleText(const esp_websocket_event_data_t* data) {
        std::string text(data->data_ptr, data->data_len);
        ESP_LOGI(TAG, "RX text: %.*s", data->data_len > 180 ? 180 : data->data_len, data->data_ptr);
        cJSON* root = cJSON_ParseWithLength(data->data_ptr, data->data_len);
        if (!root) {
            return;
        }
        cJSON* type = cJSON_GetObjectItem(root, "type");
        if (cJSON_IsString(type) && strcmp(type->valuestring, "hello") == 0) {
            cJSON* session = cJSON_GetObjectItem(root, "session_id");
            if (cJSON_IsString(session)) {
                session_id_ = session->valuestring;
            }
            server_ready_ = true;
            xEventGroupSetBits(wifi_events_, SERVER_READY_BIT);
            ESP_LOGI(TAG, "server hello ok session=%s", session_id_.c_str());
        } else if (cJSON_IsString(type) && strcmp(type->valuestring, "tts") == 0) {
            cJSON* state = cJSON_GetObjectItem(root, "state");
            if (cJSON_IsString(state)) {
                if (strcmp(state->valuestring, "start") == 0 || strcmp(state->valuestring, "sentence_start") == 0) {
                    streaming_ = false;
                    ResetVadTracking();
                    tts_started_ms_ = NowMs();
                    tts_stop_ms_ = 0;
                    tts_vad_speech_since_ms_ = 0;
                    playing_tts_ = true;
                    pending_listen_restart_ = false;
                    if (processor_) {
                        processor_->Start();
                    }
                    codec_->EnableOutput(true);
                } else if (strcmp(state->valuestring, "stop") == 0) {
                    playing_tts_ = false;
                    tts_stop_ms_ = NowMs();
                    tts_vad_speech_since_ms_ = 0;
                    if (conversation_active_) {
                        pending_listen_restart_ = true;
                    }
                }
            }
        }
        cJSON_Delete(root);
    }

    void HandleBinary(const esp_websocket_event_data_t* data) {
        if (data == nullptr || data->data_ptr == nullptr || data->data_len <= 0) {
            return;
        }
        QueueIncomingAudio(reinterpret_cast<const uint8_t*>(data->data_ptr), static_cast<size_t>(data->data_len));
    }

    void StartAudioTasks() {
        LogHeap("before audio tasks");
        free_pcm_queue_ = xQueueCreateWithCaps(kPcmQueueDepth, sizeof(uint8_t), kInternalMemoryCaps);
        filled_pcm_queue_ = xQueueCreateWithCaps(kPcmQueueDepth, sizeof(uint8_t), kInternalMemoryCaps);
        free_opus_queue_ = xQueueCreateWithCaps(kOpusQueueDepth, sizeof(uint8_t), kInternalMemoryCaps);
        filled_opus_queue_ = xQueueCreateWithCaps(kOpusQueueDepth, sizeof(uint8_t), kInternalMemoryCaps);
        free_incoming_opus_queue_ = xQueueCreateWithCaps(kIncomingOpusQueueDepth, sizeof(uint8_t), kInternalMemoryCaps);
        filled_incoming_opus_queue_ = xQueueCreateWithCaps(kIncomingOpusQueueDepth, sizeof(uint8_t), kInternalMemoryCaps);
        ESP_ERROR_CHECK(free_pcm_queue_ != nullptr && filled_pcm_queue_ != nullptr &&
                            free_opus_queue_ != nullptr && filled_opus_queue_ != nullptr &&
                            free_incoming_opus_queue_ != nullptr && filled_incoming_opus_queue_ != nullptr
                        ? ESP_OK
                        : ESP_FAIL);
        for (uint8_t i = 0; i < kPcmQueueDepth; ++i) {
            ESP_ERROR_CHECK(xQueueSend(free_pcm_queue_, &i, 0) == pdTRUE ? ESP_OK : ESP_FAIL);
        }
        for (uint8_t i = 0; i < kOpusQueueDepth; ++i) {
            ESP_ERROR_CHECK(xQueueSend(free_opus_queue_, &i, 0) == pdTRUE ? ESP_OK : ESP_FAIL);
        }
        for (uint8_t i = 0; i < kIncomingOpusQueueDepth; ++i) {
            ESP_ERROR_CHECK(xQueueSend(free_incoming_opus_queue_, &i, 0) == pdTRUE ? ESP_OK : ESP_FAIL);
        }

        BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
            [](void* arg) {
                static_cast<EchoEarGroundApp*>(arg)->AudioFeedTask();
            },
            "audio_feed", kAudioFeedTaskStackSize, this, 5, nullptr, 1, kInternalMemoryCaps);
        ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_FAIL);

        audio_encode_task_stack_ = static_cast<StackType_t*>(
            heap_caps_malloc(kAudioEncodeTaskStackSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        ESP_ERROR_CHECK(audio_encode_task_stack_ != nullptr ? ESP_OK : ESP_ERR_NO_MEM);
        audio_encode_task_handle_ = xTaskCreateStaticPinnedToCore(
            [](void* arg) {
                static_cast<EchoEarGroundApp*>(arg)->AudioEncodeTask();
            },
            "audio_encode",
            kAudioEncodeTaskStackSize,
            this,
            4,
            audio_encode_task_stack_,
            &audio_encode_task_buffer_,
            tskNO_AFFINITY);
        ESP_ERROR_CHECK(audio_encode_task_handle_ != nullptr ? ESP_OK : ESP_FAIL);

        ok = xTaskCreatePinnedToCoreWithCaps(
            [](void* arg) {
                static_cast<EchoEarGroundApp*>(arg)->NetworkSendTask();
            },
            "net_send", kNetworkTaskStackSize, this, 3, nullptr, 0, kInternalMemoryCaps);
        ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_FAIL);

        ok = xTaskCreatePinnedToCoreWithCaps(
            [](void* arg) {
                static_cast<EchoEarGroundApp*>(arg)->AudioPlaybackTask();
            },
            "tts_play", kAudioPlaybackTaskStackSize, this, 3, nullptr, tskNO_AFFINITY, kInternalMemoryCaps);
        ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_FAIL);

        ok = xTaskCreatePinnedToCoreWithCaps(
            [](void* arg) {
                static_cast<EchoEarGroundApp*>(arg)->ControlTask();
            },
            "control", kControlTaskStackSize, this, 4, nullptr, 0, kInternalMemoryCaps);
        ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_FAIL);
        LogHeap("after audio tasks");
    }

    void AudioFeedTask() {
        ESP_LOGI(TAG, "audio_feed task running stack=%lu", static_cast<unsigned long>(kAudioFeedTaskStackSize));
        std::vector<int16_t> data;
        while (true) {
            if (!processor_ || !processor_->IsRunning()) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            size_t samples = processor_->GetFeedSize();
            if (samples == 0) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (data.size() != samples) {
                data.resize(samples);
            }
            if (ReadAudioForAfe(data, samples)) {
                processor_->Feed(data);
            }
        }
    }

    bool ReadAudioForAfe(std::vector<int16_t>& data, size_t samples) {
        if (!codec_ || !codec_->input_enabled()) {
            return false;
        }

        if (codec_->input_sample_rate() == kAfeSampleRate) {
            data.resize(samples);
            return codec_->InputData(data);
        }

        const size_t codec_samples = samples * codec_->input_sample_rate() / kAfeSampleRate;
        data.resize(codec_samples);
        if (!codec_->InputData(data)) {
            return false;
        }

        if (codec_->input_channels() == 2) {
            const size_t channel_samples = data.size() / 2;
            mic_buffer_.resize(channel_samples);
            reference_buffer_.resize(channel_samples);
            for (size_t i = 0, j = 0; i < channel_samples; ++i, j += 2) {
                mic_buffer_[i] = data[j];
                reference_buffer_[i] = data[j + 1];
            }

            resampled_mic_buffer_.resize(input_resampler_.GetOutputSamples(static_cast<int>(mic_buffer_.size())));
            resampled_reference_buffer_.resize(reference_resampler_.GetOutputSamples(static_cast<int>(reference_buffer_.size())));
            input_resampler_.Process(mic_buffer_.data(), static_cast<int>(mic_buffer_.size()), resampled_mic_buffer_.data());
            reference_resampler_.Process(reference_buffer_.data(), static_cast<int>(reference_buffer_.size()), resampled_reference_buffer_.data());

            data.resize(resampled_mic_buffer_.size() + resampled_reference_buffer_.size());
            for (size_t i = 0, j = 0; i < resampled_mic_buffer_.size(); ++i, j += 2) {
                data[j] = resampled_mic_buffer_[i];
                data[j + 1] = resampled_reference_buffer_[i];
            }
        } else {
            resampled_input_buffer_.resize(input_resampler_.GetOutputSamples(static_cast<int>(data.size())));
            input_resampler_.Process(data.data(), static_cast<int>(data.size()), resampled_input_buffer_.data());
            data.assign(resampled_input_buffer_.begin(), resampled_input_buffer_.end());
        }

        return data.size() == samples;
    }

    void EncodePcmFrame(const int16_t* pcm, size_t samples) {
        if (opus_encoder_ == nullptr || pcm == nullptr || samples == 0) {
            return;
        }

        opus_input_buffer_.insert(opus_input_buffer_.end(), pcm, pcm + samples);
        while (opus_input_buffer_.size() >= static_cast<size_t>(opus_frame_samples_)) {
            uint8_t opus[kMaxOpusFrameBytes];
            int encoded = opus_encode(
                opus_encoder_,
                opus_input_buffer_.data(),
                opus_frame_samples_,
                opus,
                static_cast<opus_int32>(kMaxOpusFrameBytes));
            if (encoded < 0) {
                ESP_LOGE(TAG, "opus encode failed: %d", encoded);
                opus_input_buffer_.clear();
                return;
            }
            if (ws_connected_ && streaming_) {
                QueueOpusFrame(opus, static_cast<size_t>(encoded));
            }
            opus_input_buffer_.erase(opus_input_buffer_.begin(), opus_input_buffer_.begin() + opus_frame_samples_);
        }
    }

    void ResetOpusEncoder() {
        if (opus_encoder_ != nullptr) {
            opus_encoder_ctl(opus_encoder_, OPUS_RESET_STATE);
        }
        opus_input_buffer_.clear();
    }

    void AudioEncodeTask() {
        ESP_LOGI(TAG, "audio_encode task running stack=%lu caps=SPIRAM",
                 static_cast<unsigned long>(kAudioEncodeTaskStackSize));
        uint32_t encoded_frames = 0;
        while (true) {
            uint8_t frame_index = 0;
            if (xQueueReceive(filled_pcm_queue_, &frame_index, portMAX_DELAY) != pdTRUE || frame_index >= kPcmQueueDepth) {
                continue;
            }
            PcmFrame& frame = pcm_pool_[frame_index];
            if (!server_ready_ || !ws_connected_ || !streaming_ || frame.samples == 0) {
                frame.samples = 0;
                xQueueSend(free_pcm_queue_, &frame_index, 0);
                continue;
            }
            const uint16_t samples = frame.samples;
            const int16_t* pcm = frame.data;
            frame.samples = 0;
            EncodePcmFrame(pcm, samples);
            ++encoded_frames;
            if (encoded_frames == 1 || encoded_frames % 50 == 0) {
                ESP_LOGI(TAG, "audio_encode stack free watermark=%u",
                         static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
            }
            xQueueSend(free_pcm_queue_, &frame_index, 0);
        }
    }

    void NetworkSendTask() {
        ESP_LOGI(TAG, "net_send task running stack=%lu", static_cast<unsigned long>(kNetworkTaskStackSize));
        while (true) {
            uint8_t frame_index = 0;
            if (xQueueReceive(filled_opus_queue_, &frame_index, portMAX_DELAY) != pdTRUE || frame_index >= kOpusQueueDepth) {
                continue;
            }
            OpusFrame& frame = opus_pool_[frame_index];
            if (websocket_ && ws_connected_ && frame.bytes > 0) {
                std::lock_guard<std::mutex> lock(websocket_send_mutex_);
                esp_websocket_client_send_bin(
                    websocket_,
                    reinterpret_cast<const char*>(frame.data),
                    static_cast<int>(frame.bytes),
                    pdMS_TO_TICKS(250));
            }
            frame.bytes = 0;
            xQueueSend(free_opus_queue_, &frame_index, 0);
        }
    }

    void AudioPlaybackTask() {
        ESP_LOGI(TAG, "tts_play task running stack=%lu", static_cast<unsigned long>(kAudioPlaybackTaskStackSize));
        while (true) {
            uint8_t frame_index = 0;
            if (xQueueReceive(filled_incoming_opus_queue_, &frame_index, portMAX_DELAY) != pdTRUE ||
                frame_index >= kIncomingOpusQueueDepth) {
                continue;
            }

            IncomingOpusFrame& frame = incoming_opus_pool_[frame_index];
            if (codec_ && opus_decoder_ && frame.bytes > 0) {
                std::vector<uint8_t> opus(frame.data, frame.data + frame.bytes);
                std::vector<int16_t> pcm;
                bool decoded = false;
                {
                    std::lock_guard<std::mutex> lock(opus_decoder_mutex_);
                    decoded = opus_decoder_->Decode(std::move(opus), pcm);
                }
                if (decoded && !pcm.empty()) {
                    if (!codec_->output_enabled()) {
                        codec_->EnableOutput(true);
                    }
                    if (opus_decoder_->sample_rate() != codec_->output_sample_rate()) {
                        resampled_output_buffer_.resize(output_resampler_.GetOutputSamples(static_cast<int>(pcm.size())));
                        output_resampler_.Process(
                            pcm.data(),
                            static_cast<int>(pcm.size()),
                            resampled_output_buffer_.data());
                        pcm.assign(resampled_output_buffer_.begin(), resampled_output_buffer_.end());
                    }
                    codec_->OutputData(pcm);
                }
            }

            frame.bytes = 0;
            incoming_tts_frames_.fetch_sub(1);
            xQueueSend(free_incoming_opus_queue_, &frame_index, 0);
        }
    }

    void ControlTask() {
        ESP_LOGI(TAG, "control task running stack=%lu boot_gpio=%d", static_cast<unsigned long>(kControlTaskStackSize), kBootButtonGpio);
        bool last_raw_pressed = gpio_get_level(kBootButtonGpio) == 0;
        bool stable_pressed = last_raw_pressed;
        uint32_t last_change_ms = NowMs();

        while (true) {
            const uint32_t now_ms = NowMs();
            const bool raw_pressed = gpio_get_level(kBootButtonGpio) == 0;
            if (raw_pressed != last_raw_pressed) {
                last_raw_pressed = raw_pressed;
                last_change_ms = now_ms;
            }
            if (raw_pressed != stable_pressed && now_ms - last_change_ms >= 50) {
                stable_pressed = raw_pressed;
                if (stable_pressed) {
                    ToggleConversationFromBootButton();
                }
            }

            if (pending_abort_.exchange(false)) {
                SendAbort();
            }

            CheckTtsVadInterrupt(now_ms);

            if (pending_listen_restart_.exchange(false)) {
                ContinueConversationAfterTts();
            }

            const uint32_t silence_since = silence_since_ms_.load();
            if (streaming_ && heard_speech_ && !vad_speaking_ && silence_since != 0 &&
                now_ms - silence_since >= kVadSilenceStopMs) {
                StopListeningForSilence();
            }

            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void OnVadChange(bool speaking) {
        ESP_LOGI(TAG, "VAD=%s", speaking ? "speech" : "silence");
        if (playing_tts_) {
            vad_speaking_ = speaking;
            if (speaking) {
                if (tts_vad_speech_since_ms_.load() == 0) {
                    tts_vad_speech_since_ms_ = NowMs();
                }
            } else {
                tts_vad_speech_since_ms_ = 0;
            }
            return;
        }

        if (speaking) {
            vad_speaking_ = true;
            heard_speech_ = true;
            silence_since_ms_ = 0;
        } else {
            vad_speaking_ = false;
            silence_since_ms_ = NowMs();
        }
    }

    void OnProcessedAudio(const int16_t* pcm, size_t samples) {
        if (!free_pcm_queue_ || !filled_pcm_queue_ || !server_ready_ || !ws_connected_ || !streaming_ || pcm == nullptr || samples == 0) {
            return;
        }
        if (samples > kMaxPcmFrameSamples) {
            ESP_LOGW(TAG, "PCM frame too large: %u samples", static_cast<unsigned int>(samples));
            return;
        }

        uint8_t frame_index = 0;
        if (xQueueReceive(free_pcm_queue_, &frame_index, 0) != pdTRUE || frame_index >= kPcmQueueDepth) {
            return;
        }

        PcmFrame& frame = pcm_pool_[frame_index];
        frame.samples = static_cast<uint16_t>(samples);
        std::memcpy(frame.data, pcm, samples * sizeof(int16_t));
        if (xQueueSend(filled_pcm_queue_, &frame_index, 0) != pdTRUE) {
            frame.samples = 0;
            xQueueSend(free_pcm_queue_, &frame_index, 0);
        }
    }

    void QueueOpusFrame(const uint8_t* opus, size_t bytes) {
        if (!free_opus_queue_ || !filled_opus_queue_ || opus == nullptr || bytes == 0) {
            return;
        }
        if (bytes > kMaxOpusFrameBytes) {
            ESP_LOGW(TAG, "Opus frame too large: %u bytes", static_cast<unsigned int>(bytes));
            return;
        }

        uint8_t frame_index = 0;
        if (xQueueReceive(free_opus_queue_, &frame_index, 0) != pdTRUE || frame_index >= kOpusQueueDepth) {
            return;
        }

        OpusFrame& frame = opus_pool_[frame_index];
        frame.bytes = static_cast<uint16_t>(bytes);
        std::memcpy(frame.data, opus, bytes);
        if (xQueueSend(filled_opus_queue_, &frame_index, 0) != pdTRUE) {
            frame.bytes = 0;
            xQueueSend(free_opus_queue_, &frame_index, 0);
        }
    }

    void QueueIncomingAudio(const uint8_t* opus, size_t bytes) {
        if (!free_incoming_opus_queue_ || !filled_incoming_opus_queue_ || opus == nullptr || bytes == 0) {
            return;
        }
        if (bytes > kMaxIncomingOpusFrameBytes) {
            ESP_LOGW(TAG, "incoming Opus frame too large: %u bytes", static_cast<unsigned int>(bytes));
            return;
        }

        uint8_t frame_index = 0;
        if (xQueueReceive(free_incoming_opus_queue_, &frame_index, 0) != pdTRUE ||
            frame_index >= kIncomingOpusQueueDepth) {
            return;
        }

        IncomingOpusFrame& frame = incoming_opus_pool_[frame_index];
        frame.bytes = static_cast<uint16_t>(bytes);
        std::memcpy(frame.data, opus, bytes);
        if (xQueueSend(filled_incoming_opus_queue_, &frame_index, 0) != pdTRUE) {
            frame.bytes = 0;
            xQueueSend(free_incoming_opus_queue_, &frame_index, 0);
            return;
        }
        incoming_tts_frames_.fetch_add(1);
    }

    void ResetVadTracking() {
        vad_speaking_ = false;
        heard_speech_ = false;
        silence_since_ms_ = 0;
        tts_vad_speech_since_ms_ = 0;
    }

    void ToggleConversationFromBootButton() {
        if (conversation_active_) {
            StopConversationFromBootButton();
            return;
        }

        conversation_active_ = true;
        pending_abort_ = false;
        pending_listen_restart_ = false;
        if (!StartListeningCycle("BOOT conversation started")) {
            conversation_active_ = false;
        }
    }

    void StopConversationFromBootButton() {
        conversation_active_ = false;
        pending_abort_ = false;
        pending_listen_restart_ = false;
        const bool was_streaming = streaming_.exchange(false);
        const bool was_playing_tts = playing_tts_.exchange(false);
        ResetVadTracking();
        DrainIncomingAudio();
        if (processor_) {
            processor_->Stop();
        }

        if (was_streaming || was_playing_tts) {
            SendAbort();
        }
        ESP_LOGI(TAG, "BOOT conversation stopped");
    }

    void ContinueConversationAfterTts() {
        if (!conversation_active_ || streaming_ || playing_tts_) {
            return;
        }
        if (incoming_tts_frames_.load() > 0) {
            pending_listen_restart_ = true;
            return;
        }
        const uint32_t tts_stop_ms = tts_stop_ms_.load();
        if (tts_stop_ms != 0 && NowMs() - tts_stop_ms < kTtsPlaybackTailMs) {
            pending_listen_restart_ = true;
            return;
        }
        if (!ws_connected_ || !server_ready_) {
            ESP_LOGW(TAG, "conversation restart skipped: server not ready");
            conversation_active_ = false;
            return;
        }
        if (!StartListeningCycle("TTS stopped; continuing conversation")) {
            conversation_active_ = false;
        }
    }

    bool StartListeningCycle(const char* reason) {
        if (!EnsureWebSocketReady()) {
            return false;
        }
        if (streaming_) {
            ESP_LOGI(TAG, "listen start ignored: already listening");
            return true;
        }

        processor_->Stop();
        playing_tts_ = false;
        ResetVadTracking();
        ResetOpusEncoder();
        if (!SendListen("start")) {
            ESP_LOGW(TAG, "listen start send failed");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        streaming_ = true;
        processor_->Start();
        ESP_LOGI(TAG, "%s; waiting for %lums VAD silence after speech",
                 reason ? reason : "listen started",
                 static_cast<unsigned long>(kVadSilenceStopMs));
        return true;
    }

    void StopListeningForSilence() {
        if (!streaming_.exchange(false)) {
            return;
        }
        ResetVadTracking();
        SendListen("stop");
        if (processor_) {
            processor_->Stop();
        }
        ESP_LOGI(TAG, "listen stopped after %lums VAD silence; waiting for TTS",
                 static_cast<unsigned long>(kVadSilenceStopMs));
    }

    void CheckTtsVadInterrupt(uint32_t now_ms) {
        if (!kEnableTtsVadInterrupt) {
            return;
        }
        if (!playing_tts_ || !vad_speaking_) {
            return;
        }

        const uint32_t tts_started_ms = tts_started_ms_.load();
        const uint32_t vad_since_ms = tts_vad_speech_since_ms_.load();
        if (tts_started_ms == 0 || vad_since_ms == 0) {
            return;
        }
        if (now_ms - tts_started_ms < kTtsVadGraceMs) {
            return;
        }
        if (now_ms - vad_since_ms < kTtsVadDebounceMs) {
            return;
        }

        ESP_LOGI(TAG, "VAD barge-in confirmed during TTS after %lums; aborting playback",
                 static_cast<unsigned long>(now_ms - vad_since_ms));
        playing_tts_ = false;
        tts_vad_speech_since_ms_ = 0;
        pending_abort_ = true;
        DrainIncomingAudio();
    }

    void DrainIncomingAudio() {
        if (!filled_incoming_opus_queue_ || !free_incoming_opus_queue_) {
            incoming_tts_frames_ = 0;
            return;
        }

        uint8_t frame_index = 0;
        int drained = 0;
        while (xQueueReceive(filled_incoming_opus_queue_, &frame_index, 0) == pdTRUE) {
            if (frame_index < kIncomingOpusQueueDepth) {
                incoming_opus_pool_[frame_index].bytes = 0;
                xQueueSend(free_incoming_opus_queue_, &frame_index, 0);
                ++drained;
            }
        }
        if (drained > 0) {
            incoming_tts_frames_.fetch_sub(drained);
        }
        if (opus_decoder_) {
            std::lock_guard<std::mutex> lock(opus_decoder_mutex_);
            opus_decoder_->ResetState();
        }
    }

    bool SendListen(const char* state) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "listen");
        cJSON_AddStringToObject(root, "state", state);
        if (!session_id_.empty()) {
            cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
        }
        if (strcmp(state, "start") == 0) {
            cJSON_AddStringToObject(root, "mode", "auto");
        }
        char* text = cJSON_PrintUnformatted(root);
        ESP_LOGI(TAG, "TX listen:%s", state);
        bool ok = SendText(text);
        cJSON_free(text);
        cJSON_Delete(root);
        return ok;
    }

    void SendAbort() {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "abort");
        cJSON_AddStringToObject(root, "reason", "vad_interrupt");
        if (!session_id_.empty()) {
            cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
        }
        char* text = cJSON_PrintUnformatted(root);
        ESP_LOGI(TAG, "TX abort:vad_interrupt");
        SendText(text);
        cJSON_free(text);
        cJSON_Delete(root);
    }

    bool SendText(const char* text) {
        if (websocket_ && ws_connected_ && text) {
            std::lock_guard<std::mutex> lock(websocket_send_mutex_);
            return esp_websocket_client_send_text(websocket_, text, strlen(text), pdMS_TO_TICKS(1000)) >= 0;
        }
        return false;
    }

};

extern "C" void app_main(void) {
    EchoEarGroundApp::Instance().Start();
}
