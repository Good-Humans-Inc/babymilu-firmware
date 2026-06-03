#include "afe_audio_processor.h"
#include "animation_store.h"
#include "battery_monitor.h"
#include "box_audio_codec.h"
#include "config.h"
#include "echoear_touch.h"
#include "error_log_uploader.h"
#include "mqtt_control.h"
#include "ota_client.h"
#include "power_save_timer.h"
#include "runtime_config.h"
#include "settings_store.h"
#include "url_utils.h"
#include "wifi_manager.h"

#include <cJSON.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
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
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#define TAG "EchoEarGround"
#define SERVER_READY_BIT BIT1
static constexpr int kAfeSampleRate = 16000;
static constexpr gpio_num_t kBootButtonGpio = BOOT_BUTTON_GPIO;
static constexpr uint32_t kAudioFeedTaskStackSize = 4096 * 3;
static constexpr uint32_t kAudioEncodeTaskStackSize = 4096 * 10;
static constexpr uint32_t kNetworkTaskStackSize = 4096 * 3;
static constexpr uint32_t kAudioPlaybackTaskStackSize = 4096 * 4;
static constexpr uint32_t kControlTaskStackSize = 4096 * 2;
static constexpr uint32_t kTtsPlaybackTailMs = 500;
static constexpr uint16_t kBatteryPowerOffVoltageMv = 3400;
static constexpr uint16_t kBatteryForceVisualVoltageMv = 3600;
static constexpr int kSleepBacklightBrightness = 20;
static constexpr uint32_t kGestureOverlayMs = 1000;
static constexpr uint32_t kStatusOverlayMs = 2000;
static constexpr UBaseType_t kPcmQueueDepth = 4;
static constexpr UBaseType_t kOpusQueueDepth = 8;
static constexpr UBaseType_t kIncomingOpusQueueDepth = 12;
static constexpr size_t kMaxPcmFrameSamples = 2048;
static constexpr size_t kMaxOpusFrameBytes = 1000;
static constexpr size_t kMaxIncomingOpusFrameBytes = 1000;
static constexpr UBaseType_t kInternalMemoryCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
static constexpr UBaseType_t kTaskStackMemoryCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

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
        esp_err_t nvs_err = nvs_flash_init();
        if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            nvs_err = nvs_flash_init();
        }
        ESP_ERROR_CHECK(nvs_err);
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        config_.Load();
        InitPowerHold();
        InitBootButton();
        animation_.InitDisplay();
        InitAudio();
        InitBattery();
        InitTouch();
        InitTouchEmotionResetTimer();
        InitPowerSaveTimer();
        animation_.InitStartupMedia();
        animation_.PlayStartupWav(codec_.get());
        animation_.InitBundle();
        ConfigureMqttCallbacks();
        wifi_.OnConnected([this]() { OnWifiConnected(); });
        wifi_.Start();
        StartAudioTasks();
        ESP_LOGI(TAG, "startup ready; OTA=%s WS=%s MQTT=%s",
                 config_.ota_url().c_str(),
                 config_.websocket().url.c_str(),
                 config_.mqtt().endpoint.c_str());
    }

private:
    EventGroupHandle_t server_events_ = xEventGroupCreate();
    RuntimeConfig config_;
    WifiManager wifi_;
    MqttControl mqtt_{config_};
    AnimationStore animation_;
    BatteryMonitor battery_;
    EchoEarTouchController touch_;
    PowerSaveTimer power_save_{30};
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
    std::mutex opus_encoder_mutex_;
    std::mutex opus_decoder_mutex_;
    std::string session_id_;
    std::string active_ws_url_;
    std::atomic<bool> ws_connected_{false};
    std::atomic<bool> server_ready_{false};
    std::atomic<bool> streaming_{false};
    std::atomic<bool> listen_stop_sent_{false};
    std::atomic<bool> conversation_active_{false};
    std::atomic<bool> playing_tts_{false};
    std::atomic<bool> pending_listen_restart_{false};
    std::atomic<bool> sleep_visual_active_{false};
    std::atomic<bool> low_battery_visual_forced_{false};
    std::atomic<bool> error_log_hook_started_{false};
    std::atomic<bool> custom_wake_word_pending_{false};
    std::atomic<uint32_t> custom_wake_word_detections_{0};
    std::atomic<uint32_t> tts_started_ms_{0};
    std::atomic<uint32_t> tts_stop_ms_{0};
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
    esp_timer_handle_t touch_emotion_reset_timer_ = nullptr;
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
        processor_->OnOutput([this](const int16_t* pcm, size_t samples) { OnProcessedAudio(pcm, samples); });
        processor_->OnWakeWordDetected([this](const char* wake_word) { OnCustomWakeWordDetected(wake_word); });
        UpdateWakeWordDetectionState();
    }

    void InitBattery() {
        ESP_ERROR_CHECK(battery_.Init(i2c_bus_));
        battery_.Start([this](const BatteryTelemetry& telemetry) {
            ApplyBatteryPolicy(telemetry);
        });
    }

    void InitTouch() {
        EchoEarTouchController::Callbacks callbacks;
        callbacks.on_gpio7_emotion = [this](const char* emotion, uint32_t duration_ms) {
            OnGpio7TouchEmotion(emotion, duration_ms);
        };
        callbacks.on_activity = [this]() {
            power_save_.WakeUp();
            UpdateTouchInteractionState();
        };
        callbacks.on_status_request = [this]() {
            ShowStatusCanvas();
        };
        callbacks.adjust_volume = [this](int delta) {
            return AdjustOutputVolume(delta);
        };
        callbacks.adjust_brightness = [this](int delta) {
            return AdjustBacklightBrightness(delta);
        };
        ESP_ERROR_CHECK(touch_.Init(i2c_bus_, std::move(callbacks)));
        UpdateTouchInteractionState();
    }

    void InitTouchEmotionResetTimer() {
        esp_timer_create_args_t args = {};
        args.callback = [](void* arg) {
            static_cast<EchoEarGroundApp*>(arg)->RestoreTouchEmotion();
        };
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "gpio7_emotion_reset";
        args.skip_unhandled_events = true;
        ESP_ERROR_CHECK(esp_timer_create(&args, &touch_emotion_reset_timer_));
    }

    void InitPowerSaveTimer() {
        power_save_.SetCanSleepCallback([this]() {
            return CanEnterSleepMode();
        });
        power_save_.OnEnterSleepMode([this]() {
            EnterSleepMode();
        });
        power_save_.OnExitSleepMode([this]() {
            ExitSleepVisualToIdle();
        });
        power_save_.SetEnabled(true);
    }

    void ConfigureMqttCallbacks() {
        mqtt_.OnWsStart([this](const std::string& url, const std::string& connectionType) {
            WakeFromSleep("MQTT ws_start");
            config_.setConnectionType(connectionType);
            conversation_active_ = true;
            ShowListeningAnimation();
            if (OpenWebSocketConnection(config_.BuildWebSocketUrl(url))) {
                StartListeningCycle("MQTT ws_start conversation started");
            } else {
                conversation_active_ = false;
                ShowIdleAnimation();
            }
        });
        mqtt_.OnWifiReconfigure([this]() {
            wifi_.RequestBleReconfigure();
        });
        mqtt_.OnWifiClear([this]() {
            wifi_.ClearCredentialsAndRequestOnboarding();
        });
        mqtt_.OnSwitchWifi([](const std::string& ssid) {
            SettingsStore settings("wifi", true);
            settings.SetString("nxt_boot_ssid", ssid);
            ESP_LOGI(TAG, "switch_wifi_to saved one-shot SSID '%s'; restarting", ssid.c_str());
            esp_restart();
        });
        mqtt_.OnRemoteAnimationUpdate([this](const std::string& url) {
            animation_.TriggerUpdateCheck(url);
        });
        mqtt_.OnSetOtaUrl([](const std::string& url) {
            SettingsStore settings("ota", true);
            settings.SetString("url", url);
            ESP_LOGI(TAG, "custom OTA URL saved for next boot: %s", url.c_str());
            esp_restart();
        });
    }

    void OnWifiConnected() {
        MaybeUploadErrorLogAndEnableHook();
        ShowIdleAnimation();
        OtaClient ota(config_);
        ota.Hydrate();
        wifi_.FetchFirestoreRankingAndApply();
        mqtt_.Start();
        animation_.TriggerUpdateCheck();
        ota.MaybeStartFirmwareUpdate();
    }

    bool EnsureWebSocketReady() {
        if (server_ready_ && ws_connected_) {
            return true;
        }

        ESP_LOGI(TAG, "BOOT waiting for Wi-Fi before WebSocket open");
        if (!wifi_.WaitForConnected(15000)) {
            ESP_LOGW(TAG, "BOOT listen ignored: Wi-Fi not connected");
            return false;
        }

        return OpenWebSocketConnection(config_.BuildWebSocketUrl());
    }

    bool OpenWebSocketConnection(const std::string& url) {
        if (!url_utils::IsUsableWebSocketUrl(url)) {
            ESP_LOGE(TAG, "refusing unusable WebSocket URL: %s", url.c_str());
            return false;
        }
        if (websocket_ != nullptr) {
            esp_websocket_client_stop(websocket_);
            esp_websocket_client_destroy(websocket_);
            websocket_ = nullptr;
        }

        active_ws_url_ = url;
        server_ready_ = false;
        ws_connected_ = false;
        session_id_.clear();
        listen_stop_sent_ = false;
        xEventGroupClearBits(server_events_, SERVER_READY_BIT);

        ESP_LOGI(TAG, "opening WebSocket: %s", active_ws_url_.c_str());
        esp_websocket_client_config_t ws_cfg = {};
        ws_cfg.uri = active_ws_url_.c_str();
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

        EventBits_t bits = xEventGroupWaitBits(server_events_, SERVER_READY_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
        if ((bits & SERVER_READY_BIT) == 0 || !server_ready_ || !ws_connected_) {
            ESP_LOGW(TAG, "WebSocket opened but server hello timed out");
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
                xEventGroupClearBits(self->server_events_, SERVER_READY_BIT);
                ESP_LOGI(TAG, "WebSocket connected");
                self->SendHello();
                break;
            case WEBSOCKET_EVENT_DISCONNECTED:
                self->ws_connected_ = false;
                self->server_ready_ = false;
                self->streaming_ = false;
                self->conversation_active_ = false;
                self->playing_tts_ = false;
                self->pending_listen_restart_ = false;
                if (self->processor_) {
                    self->processor_->Stop();
                }
                self->ResetOpusEncoder();
                xEventGroupClearBits(self->server_events_, SERVER_READY_BIT);
                self->ShowIdleAnimation();
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
        cJSON_AddNumberToObject(root, "version", config_.websocket().version);
        cJSON_AddStringToObject(root, "transport", "websocket");
        cJSON_AddStringToObject(root, "device_id", config_.device_id().c_str());
        cJSON_AddStringToObject(root, "client_id", config_.client_id().c_str());
        cJSON_AddStringToObject(root, "connectionType", config_.connectionType().c_str());
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
            xEventGroupSetBits(server_events_, SERVER_READY_BIT);
            ESP_LOGI(TAG, "server hello ok session=%s", session_id_.c_str());
        } else if (cJSON_IsString(type) && strcmp(type->valuestring, "tts") == 0) {
            cJSON* state = cJSON_GetObjectItem(root, "state");
            if (cJSON_IsString(state)) {
                if (strcmp(state->valuestring, "start") == 0 || strcmp(state->valuestring, "sentence_start") == 0) {
                    if (!conversation_active_) {
                        ESP_LOGI(TAG, "tts:%s ignored: conversation stopped", state->valuestring);
                        cJSON_Delete(root);
                        return;
                    }
                    if (streaming_) {
                        StopListeningForServerTtsStart();
                    }
                    tts_started_ms_ = NowMs();
                    tts_stop_ms_ = 0;
                    playing_tts_ = true;
                    pending_listen_restart_ = false;
                    codec_->EnableOutput(true);
                    ShowSpeakingAnimation();
                } else if (strcmp(state->valuestring, "stop") == 0) {
                    playing_tts_ = false;
                    tts_stop_ms_ = NowMs();
                    if (conversation_active_) {
                        pending_listen_restart_ = true;
                    } else {
                        ShowIdleAnimation();
                    }
                }
            }
        } else if (cJSON_IsString(type) && strcmp(type->valuestring, "listen") == 0) {
            cJSON* state = cJSON_GetObjectItem(root, "state");
            if (cJSON_IsString(state)) {
                HandleServerListenState(state->valuestring);
            }
        } else if (cJSON_IsString(type) && strcmp(type->valuestring, "llm") == 0) {
            auto get_string = [](cJSON* item) -> const char* {
                return cJSON_IsString(item) && item->valuestring != nullptr ? item->valuestring : nullptr;
            };
            const char* animation_token = get_string(cJSON_GetObjectItem(root, "emotion"));
            if (animation_token == nullptr) {
                animation_token = get_string(cJSON_GetObjectItem(root, "animation"));
            }
            if (animation_token == nullptr) {
                animation_token = get_string(cJSON_GetObjectItem(root, "emoji"));
            }
            if (animation_token == nullptr) {
                animation_token = get_string(cJSON_GetObjectItem(root, "text"));
            }
            if (animation_token != nullptr) {
                animation_.ShowEmotion(animation_token);
            }
        }
        cJSON_Delete(root);
    }

    void HandleBinary(const esp_websocket_event_data_t* data) {
        if (data == nullptr || data->data_ptr == nullptr || data->data_len <= 0) {
            return;
        }
        if (!conversation_active_ || !playing_tts_) {
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
        if (free_pcm_queue_ == nullptr || filled_pcm_queue_ == nullptr ||
            free_opus_queue_ == nullptr || filled_opus_queue_ == nullptr ||
            free_incoming_opus_queue_ == nullptr || filled_incoming_opus_queue_ == nullptr) {
            ESP_LOGE(TAG, "audio queue allocation failed free_pcm=%p filled_pcm=%p free_opus=%p filled_opus=%p free_in=%p filled_in=%p",
                     free_pcm_queue_, filled_pcm_queue_, free_opus_queue_, filled_opus_queue_,
                     free_incoming_opus_queue_, filled_incoming_opus_queue_);
            LogHeap("audio queue allocation failed");
            return;
        }
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
            "audio_feed", kAudioFeedTaskStackSize, this, 5, nullptr, 1, kTaskStackMemoryCaps);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "failed to start audio_feed task");
            LogHeap("audio_feed task allocation failed");
            return;
        }

        audio_encode_task_stack_ = static_cast<StackType_t*>(
            heap_caps_malloc(kAudioEncodeTaskStackSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (audio_encode_task_stack_ == nullptr) {
            ESP_LOGE(TAG, "failed to allocate audio_encode task stack");
            LogHeap("audio_encode stack allocation failed");
            return;
        }
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
        if (audio_encode_task_handle_ == nullptr) {
            ESP_LOGE(TAG, "failed to start audio_encode task");
            LogHeap("audio_encode task allocation failed");
            return;
        }

        ok = xTaskCreatePinnedToCoreWithCaps(
            [](void* arg) {
                static_cast<EchoEarGroundApp*>(arg)->NetworkSendTask();
            },
            "net_send", kNetworkTaskStackSize, this, 3, nullptr, 0, kTaskStackMemoryCaps);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "failed to start net_send task");
            LogHeap("net_send task allocation failed");
            return;
        }

        ok = xTaskCreatePinnedToCoreWithCaps(
            [](void* arg) {
                static_cast<EchoEarGroundApp*>(arg)->AudioPlaybackTask();
            },
            "tts_play", kAudioPlaybackTaskStackSize, this, 3, nullptr, tskNO_AFFINITY, kTaskStackMemoryCaps);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "failed to start tts_play task");
            LogHeap("tts_play task allocation failed");
            return;
        }

        ok = xTaskCreatePinnedToCoreWithCaps(
            [](void* arg) {
                static_cast<EchoEarGroundApp*>(arg)->ControlTask();
            },
            "control", kControlTaskStackSize, this, 4, nullptr, 0, kTaskStackMemoryCaps);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "failed to start control task");
            LogHeap("control task allocation failed");
            return;
        }
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
                vTaskDelay(pdMS_TO_TICKS(1));
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

        std::lock_guard<std::mutex> lock(opus_encoder_mutex_);
        if (!streaming_) {
            opus_input_buffer_.clear();
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
            if (!streaming_) {
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
        std::lock_guard<std::mutex> lock(opus_encoder_mutex_);
        if (opus_encoder_ != nullptr) {
            opus_encoder_ctl(opus_encoder_, OPUS_RESET_STATE);
        }
        opus_input_buffer_.clear();
        DrainOutgoingAudio();
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
            if (websocket_ && ws_connected_ && streaming_ && frame.bytes > 0) {
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
            if (codec_ && opus_decoder_ && frame.bytes > 0 && conversation_active_ && playing_tts_) {
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
                    if (conversation_active_ && playing_tts_) {
                        codec_->OutputData(pcm);
                    }
                }
            }

            frame.bytes = 0;
            DecrementIncomingTtsFrames();
            xQueueSend(free_incoming_opus_queue_, &frame_index, 0);
        }
    }

    void ControlTask() {
        ESP_LOGI(TAG, "control task running stack=%lu boot_gpio=%d", static_cast<unsigned long>(kControlTaskStackSize), kBootButtonGpio);
        bool last_raw_pressed = gpio_get_level(kBootButtonGpio) == 0;
        bool stable_pressed = last_raw_pressed;
        uint32_t last_change_ms = NowMs();
        uint32_t last_wake_word_status_log_ms = 0;

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

            if (pending_listen_restart_.exchange(false)) {
                ContinueConversationAfterTts();
            }
            if (custom_wake_word_pending_.exchange(false)) {
                ToggleConversationFromWakeWord();
            }

            UpdateTouchInteractionState();
            UpdateWakeWordDetectionState();
            MaybeLogWakeWordStatus(now_ms, last_wake_word_status_log_ms);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void OnCustomWakeWordDetected(const char* wake_word) {
        uint32_t count = custom_wake_word_detections_.fetch_add(1) + 1;
        ESP_LOGI(TAG, "[WAKEWORD] detected event queued phrase=\"%s\" count=%lu",
                 wake_word != nullptr ? wake_word : "<unknown>",
                 static_cast<unsigned long>(count));
        custom_wake_word_pending_ = true;
    }

    void MaybeLogWakeWordStatus(uint32_t now_ms, uint32_t& last_log_ms) {
#if CONFIG_USE_CUSTOM_WAKE_WORD
        if (now_ms - last_log_ms < 1000) {
            return;
        }
        last_log_ms = now_ms;
        const bool available = processor_ != nullptr && processor_->IsWakeWordAvailable();
        const bool listening = processor_ != nullptr && processor_->IsWakeWordDetectionEnabled();
        AfeAudioProcessor::WakeWordStats stats = {};
        if (processor_ != nullptr) {
            stats = processor_->GetWakeWordStats();
        }
        ESP_LOGI(TAG,
                 "[WAKEWORD] available=%d listening=%d pending=%d detected_count=%lu feed=%lu detect_calls=%lu samples=%ld peak=%ld avg_abs=%ld ch=%ld left_peak=%ld right_peak=%ld conversation=%d streaming=%d playing_tts=%d incoming_tts=%d phrase=\"%s\"",
                 available ? 1 : 0,
                 listening ? 1 : 0,
                 custom_wake_word_pending_.load() ? 1 : 0,
                 static_cast<unsigned long>(custom_wake_word_detections_.load()),
                 static_cast<unsigned long>(stats.feed_calls),
                 static_cast<unsigned long>(stats.detect_calls),
                 static_cast<long>(stats.last_samples),
                 static_cast<long>(stats.last_peak),
                 static_cast<long>(stats.last_avg_abs),
                 static_cast<long>(stats.last_selected_channel),
                 static_cast<long>(stats.last_left_peak),
                 static_cast<long>(stats.last_right_peak),
                 conversation_active_.load() ? 1 : 0,
                 streaming_.load() ? 1 : 0,
                 playing_tts_.load() ? 1 : 0,
                 incoming_tts_frames_.load(),
                 CONFIG_CUSTOM_WAKE_WORD_DISPLAY);
#else
        (void)now_ms;
        (void)last_log_ms;
#endif
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

    void DecrementIncomingTtsFrames(int count = 1) {
        int current = incoming_tts_frames_.load();
        while (current > 0) {
            const int next = std::max(0, current - count);
            if (incoming_tts_frames_.compare_exchange_weak(current, next)) {
                return;
            }
        }
    }

    void ShowIdleAnimation() {
        if (sleep_visual_active_.load()) {
            return;
        }
        animation_.ShowEmotion("normal");
        UpdateTouchInteractionState();
    }

    void ShowListeningAnimation() {
        animation_.ShowUtility("listening");
        UpdateTouchInteractionState();
    }

    void ShowSpeakingAnimation() {
        animation_.ShowEmotion("speaking");
        UpdateTouchInteractionState();
    }

    bool IsAudioBusy() const {
        return streaming_.load() || playing_tts_.load() || incoming_tts_frames_.load() > 0;
    }

    bool CanEnterSleepMode() const {
        return !conversation_active_.load() &&
               !streaming_.load() &&
               !playing_tts_.load() &&
               !pending_listen_restart_.load() &&
               incoming_tts_frames_.load() == 0;
    }

    void UpdateTouchInteractionState() {
        touch_.SetInteractionState(IsAudioBusy(), sleep_visual_active_.load());
    }

    void UpdateWakeWordDetectionState() {
#if CONFIG_USE_CUSTOM_WAKE_WORD
        if (!processor_) {
            return;
        }
        const bool enable = processor_->IsWakeWordAvailable() &&
                            !conversation_active_.load() &&
                            !IsAudioBusy();
        processor_->SetWakeWordDetectionEnabled(enable);
#endif
    }

    void ApplySleepVisual(const char* visual, bool low_battery_forced) {
        sleep_visual_active_ = true;
        low_battery_visual_forced_ = low_battery_forced;
        animation_.SetBacklightBrightness(kSleepBacklightBrightness);
        if (visual != nullptr && strcmp(visual, "battery") == 0) {
            animation_.ShowUtility("battery");
        } else {
            animation_.ShowEmotion(visual != nullptr ? visual : "sleep");
        }
        UpdateTouchInteractionState();
    }

    void ExitSleepVisualToIdle() {
        if (!sleep_visual_active_.exchange(false) && !low_battery_visual_forced_.exchange(false)) {
            UpdateTouchInteractionState();
            return;
        }
        low_battery_visual_forced_ = false;
        animation_.RestoreBacklightBrightness();
        animation_.ShowEmotion("normal");
        UpdateTouchInteractionState();
        ESP_LOGI(TAG, "exited sleep visual; normal animation restored");
    }

    void EnterSleepMode() {
        BatteryTelemetry telemetry;
        if (battery_.Read(telemetry)) {
            if (telemetry.voltage_mv < kBatteryPowerOffVoltageMv) {
                ESP_LOGW(TAG, "[BATTERY] Voltage %u mV < %u mV; powering off",
                         static_cast<unsigned>(telemetry.voltage_mv),
                         static_cast<unsigned>(kBatteryPowerOffVoltageMv));
                PowerOff();
                return;
            }
            if (telemetry.voltage_mv < kBatteryForceVisualVoltageMv) {
                ESP_LOGI(TAG, "entering sleep with low battery visual voltage=%u mV",
                         static_cast<unsigned>(telemetry.voltage_mv));
                ApplySleepVisual("battery", true);
                return;
            }
        }

        ESP_LOGI(TAG, "entering sleep visual");
        ApplySleepVisual("sleep", false);
    }

    void WakeFromSleep(const char* reason) {
        bool woke_timer = power_save_.WakeUp();
        if (!woke_timer && sleep_visual_active_.load()) {
            ExitSleepVisualToIdle();
        }
        if (woke_timer || reason != nullptr) {
            ESP_LOGI(TAG, "wake path: %s", reason ? reason : "activity");
        }
        UpdateTouchInteractionState();
    }

    void ApplyBatteryPolicy(const BatteryTelemetry& telemetry) {
        if (telemetry.voltage_mv < kBatteryPowerOffVoltageMv) {
            ESP_LOGW(TAG, "[BATTERY] Voltage %u mV < %u mV; powering off",
                     static_cast<unsigned>(telemetry.voltage_mv),
                     static_cast<unsigned>(kBatteryPowerOffVoltageMv));
            PowerOff();
            return;
        }
        if (telemetry.voltage_mv < kBatteryForceVisualVoltageMv) {
            ApplySleepVisual("battery", true);
            return;
        }
        if (low_battery_visual_forced_.load() && !power_save_.IsInSleepMode()) {
            ExitSleepVisualToIdle();
        }
    }

    static void PowerOff() {
        gpio_set_level(POWER_CTRL, 1);
    }

    void MaybeUploadErrorLogAndEnableHook() {
        if (error_log_hook_started_.exchange(true)) {
            return;
        }
        ErrorLogUploader::UploadErrorLog();
        ErrorLogUploader::EnableErrorLoggingToSD();
    }

    int ReadNetworkSignalPercent() const {
        if (!wifi_.IsConnected()) {
            return 0;
        }

        wifi_ap_record_t ap_info = {};
        if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
            return 100;
        }

        const int rssi = static_cast<int>(ap_info.rssi);
        if (rssi <= -90) {
            return 10;
        }
        if (rssi >= -50) {
            return 100;
        }
        return std::clamp((rssi + 90) * 90 / 40 + 10, 10, 100);
    }

    void ShowStatusCanvas() {
        if (sleep_visual_active_.load()) {
            return;
        }

        BatteryTelemetry telemetry = {};
        int battery_percent = -1;
        bool charging = false;
        if (battery_.Read(telemetry)) {
            battery_percent = telemetry.level_percent;
            charging = telemetry.charging;
        }

        std::string network_label = wifi_.IsConnected() ? wifi_.ConnectedSsid() : "offline";
        if (network_label.empty()) {
            network_label = wifi_.IsConnected() ? "connected" : "offline";
        }
        animation_.ShowStatusCanvas(network_label.c_str(), ReadNetworkSignalPercent(),
                                    battery_percent, charging, kStatusOverlayMs);
    }

    void OnGpio7TouchEmotion(const char* emotion, uint32_t duration_ms) {
        if (emotion == nullptr || IsAudioBusy() || sleep_visual_active_.load()) {
            return;
        }
        animation_.ShowEmotion(emotion);
        if (touch_emotion_reset_timer_ != nullptr) {
            esp_timer_stop(touch_emotion_reset_timer_);
            esp_timer_start_once(touch_emotion_reset_timer_, static_cast<uint64_t>(duration_ms) * 1000);
        }
    }

    void RestoreTouchEmotion() {
        if (!IsAudioBusy() && !sleep_visual_active_.load()) {
            ShowIdleAnimation();
        }
    }

    int AdjustOutputVolume(int delta) {
        if (!codec_) {
            return 0;
        }
        const int next = std::clamp(codec_->output_volume() + delta, 0, 100);
        codec_->SetOutputVolume(next);
        animation_.ShowValueOverlay("volume", next, kGestureOverlayMs);
        return next;
    }

    int AdjustBacklightBrightness(int delta) {
        const int next = animation_.AdjustBacklightBrightness(delta);
        animation_.ShowValueOverlay("brightness", next, kGestureOverlayMs);
        return next;
    }

    void DrainOutgoingAudio() {
        if (!filled_opus_queue_ || !free_opus_queue_) {
            return;
        }

        uint8_t frame_index = 0;
        int drained = 0;
        while (xQueueReceive(filled_opus_queue_, &frame_index, 0) == pdTRUE) {
            if (frame_index < kOpusQueueDepth) {
                opus_pool_[frame_index].bytes = 0;
                xQueueSend(free_opus_queue_, &frame_index, 0);
                ++drained;
            }
        }
        if (drained > 0) {
            ESP_LOGI(TAG, "drained %d queued outbound opus frames", drained);
        }
    }

    void ToggleConversationFromBootButton() {
        ToggleConversationFromUserTrigger("BOOT button", "BOOT conversation started");
    }

    void ToggleConversationFromWakeWord() {
        ToggleConversationFromUserTrigger("custom wake word", "custom wake word conversation started");
    }

    void ToggleConversationFromUserTrigger(const char* wake_reason, const char* start_reason) {
        WakeFromSleep(wake_reason);
        if (conversation_active_) {
            StopConversationFromBootButton();
            return;
        }

        conversation_active_ = true;
        pending_listen_restart_ = false;
        tts_stop_ms_ = 0;
        if (!StartListeningCycle(start_reason)) {
            conversation_active_ = false;
            ShowIdleAnimation();
        }
        UpdateTouchInteractionState();
    }

    void StopConversationFromBootButton() {
        conversation_active_ = false;
        pending_listen_restart_ = false;
        tts_stop_ms_ = 0;
        tts_started_ms_ = 0;
        listen_stop_sent_ = true;
        const bool was_streaming = streaming_.exchange(false);
        const bool was_playing_tts = playing_tts_.exchange(false);
        DrainIncomingAudio();
        if (processor_) {
            processor_->Stop();
        }
        ResetOpusEncoder();

        if (ws_connected_ && server_ready_) {
            if (was_streaming) {
                SendListen("stop");
            }
            SendAbort();
        } else if (was_streaming || was_playing_tts) {
            ESP_LOGW(TAG, "BOOT stop could not notify server: websocket not ready");
        }
        ShowIdleAnimation();
        ESP_LOGI(TAG, "BOOT conversation stopped");
        UpdateTouchInteractionState();
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
            ShowIdleAnimation();
            return;
        }
        if (!StartListeningCycle("TTS stopped; continuing conversation")) {
            conversation_active_ = false;
            ShowIdleAnimation();
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
        DrainIncomingAudio();
        ResetOpusEncoder();
        listen_stop_sent_ = false;
        if (!SendListen("start")) {
            ESP_LOGW(TAG, "listen start send failed");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        streaming_ = true;
        ShowListeningAnimation();
        processor_->Start();
        ESP_LOGI(TAG, "%s; streaming until server finalizes or user stops", reason ? reason : "listen started");
        UpdateTouchInteractionState();
        return true;
    }

    void HandleServerListenState(const char* state) {
        if (state == nullptr) {
            return;
        }

        if (strcmp(state, "stop") == 0) {
            const bool was_streaming = streaming_.exchange(false);
            if (was_streaming && processor_) {
                processor_->Stop();
                ResetOpusEncoder();
            }
            listen_stop_sent_ = true;
            if (!playing_tts_) {
                ShowIdleAnimation();
            }
            ESP_LOGI(TAG, "server listen:stop; %s", was_streaming ? "paused audio upload" : "already paused");
            return;
        }

        if (strcmp(state, "start") == 0) {
            if (!conversation_active_) {
                ESP_LOGI(TAG, "server listen:start ignored: conversation is stopped");
                return;
            }
            if (playing_tts_ || incoming_tts_frames_.load() > 0) {
                pending_listen_restart_ = true;
                ESP_LOGI(TAG, "server listen:start deferred until playback drains");
                return;
            }
            const uint32_t tts_stop_ms = tts_stop_ms_.load();
            if (tts_stop_ms != 0 && NowMs() - tts_stop_ms < kTtsPlaybackTailMs) {
                pending_listen_restart_ = true;
                ESP_LOGI(TAG, "server listen:start deferred for TTS tail");
                return;
            }
            pending_listen_restart_ = false;
            if (!StartListeningCycle("server listen:start")) {
                conversation_active_ = false;
            }
            return;
        }

        ESP_LOGW(TAG, "server listen:%s ignored", state);
    }

    void StopListeningForServerTtsStart() {
        if (!streaming_.exchange(false)) {
            return;
        }
        ESP_LOGI(TAG, "server tts:start while listening; sending one listen:stop and switching to playback");
        SendListenStopOnce("server_tts_start");
        if (processor_) {
            processor_->Stop();
        }
        ResetOpusEncoder();
    }

    bool SendListenStopOnce(const char* reason) {
        bool already_sent = listen_stop_sent_.exchange(true);
        if (already_sent) {
            ESP_LOGI(TAG, "listen:stop already sent for this cycle; reason=%s ignored", reason ? reason : "<none>");
            return true;
        }
        return SendListen("stop");
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
            DecrementIncomingTtsFrames(drained);
            ESP_LOGI(TAG, "drained %d queued incoming TTS frames", drained);
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
        cJSON_AddStringToObject(root, "connectionType", config_.connectionType().c_str());
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
        cJSON_AddStringToObject(root, "reason", "user_stop");
        if (!session_id_.empty()) {
            cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
        }
        char* text = cJSON_PrintUnformatted(root);
        ESP_LOGI(TAG, "TX abort:user_stop");
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
