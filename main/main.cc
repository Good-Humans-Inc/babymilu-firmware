#include "afe_audio_processor.h"
#include "box_audio_codec.h"
#include "config.h"

#include <cJSON.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <opus_decoder.h>
#include <opus_encoder.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#define TAG "EchoEarGround"
#define WIFI_CONNECTED_BIT BIT0

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
        InitAudio();
        InitWifi();
        StartWebSocket();
        StartAudioTasks();
    }

private:
    EventGroupHandle_t wifi_events_ = xEventGroupCreate();
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    std::unique_ptr<BoxAudioCodec> codec_;
    std::unique_ptr<AfeAudioProcessor> processor_;
    std::unique_ptr<OpusEncoderWrapper> opus_encoder_;
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;
    esp_websocket_client_handle_t websocket_ = nullptr;
    std::string session_id_;
    std::atomic<bool> ws_connected_{false};
    std::atomic<bool> server_ready_{false};
    std::atomic<bool> streaming_{false};
    std::atomic<bool> playing_tts_{false};

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

        opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
        opus_encoder_->SetComplexity(0);
        opus_decoder_ = std::make_unique<OpusDecoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);

        processor_ = std::make_unique<AfeAudioProcessor>();
        processor_->Initialize(codec_.get());
        processor_->EnableDeviceAec(true);
        processor_->OnVadStateChange([this](bool speaking) { OnVadChange(speaking); });
        processor_->OnOutput([this](std::vector<int16_t>&& pcm) { OnProcessedAudio(std::move(pcm)); });
    }

    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
        auto* self = static_cast<EchoEarGroundApp*>(arg);
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
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

    void StartWebSocket() {
        if (std::string(CONFIG_ECHOEAR_WIFI_SSID).empty()) {
            return;
        }
        xEventGroupWaitBits(wifi_events_, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

        esp_websocket_client_config_t ws_cfg = {};
        ws_cfg.uri = CONFIG_ECHOEAR_WEBSOCKET_URL;
        ws_cfg.disable_auto_reconnect = false;
        websocket_ = esp_websocket_client_init(&ws_cfg);
        ESP_ERROR_CHECK(esp_websocket_register_events(websocket_, WEBSOCKET_EVENT_ANY, &WebSocketEventHandler, this));
        ESP_ERROR_CHECK(esp_websocket_client_start(websocket_));
    }

    static void WebSocketEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
        auto* self = static_cast<EchoEarGroundApp*>(handler_args);
        auto* data = static_cast<esp_websocket_event_data_t*>(event_data);

        switch (event_id) {
            case WEBSOCKET_EVENT_CONNECTED:
                self->ws_connected_ = true;
                ESP_LOGI(TAG, "WebSocket connected");
                self->SendHello();
                break;
            case WEBSOCKET_EVENT_DISCONNECTED:
                self->ws_connected_ = false;
                self->server_ready_ = false;
                self->streaming_ = false;
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
            processor_->Start();
            ESP_LOGI(TAG, "server hello ok session=%s", session_id_.c_str());
        } else if (cJSON_IsString(type) && strcmp(type->valuestring, "tts") == 0) {
            cJSON* state = cJSON_GetObjectItem(root, "state");
            if (cJSON_IsString(state)) {
                if (strcmp(state->valuestring, "start") == 0 || strcmp(state->valuestring, "sentence_start") == 0) {
                    playing_tts_ = true;
                    codec_->EnableOutput(true);
                } else if (strcmp(state->valuestring, "stop") == 0) {
                    playing_tts_ = false;
                }
            }
        }
        cJSON_Delete(root);
    }

    void HandleBinary(const esp_websocket_event_data_t* data) {
        if (!codec_ || !opus_decoder_) {
            return;
        }
        std::vector<uint8_t> opus(data->data_ptr, data->data_ptr + data->data_len);
        std::vector<int16_t> pcm;
        if (opus_decoder_->Decode(std::move(opus), pcm) && !pcm.empty()) {
            codec_->OutputData(pcm);
        }
    }

    void StartAudioTasks() {
        xTaskCreate(
            [](void* arg) {
                static_cast<EchoEarGroundApp*>(arg)->AudioFeedTask();
                vTaskDelete(nullptr);
            },
            "audio_feed", 4096, this, 5, nullptr);
    }

    void AudioFeedTask() {
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
            std::vector<int16_t> data(samples);
            if (codec_->InputData(data)) {
                processor_->Feed(data);
            }
        }
    }

    void OnVadChange(bool speaking) {
        ESP_LOGI(TAG, "VAD=%s", speaking ? "speech" : "silence");
        if (!server_ready_ || !ws_connected_) {
            return;
        }
        if (speaking) {
            if (playing_tts_) {
                playing_tts_ = false;
                SendAbort();
            }
            if (!streaming_) {
                streaming_ = true;
                opus_encoder_->ResetState();
                SendListen("start");
            }
        } else if (streaming_) {
            streaming_ = false;
            SendListen("stop");
        }
    }

    void OnProcessedAudio(std::vector<int16_t>&& pcm) {
        if (!server_ready_ || !ws_connected_ || !streaming_ || pcm.empty()) {
            return;
        }
        opus_encoder_->Encode(std::move(pcm), [this](std::vector<uint8_t>&& opus) {
            if (!opus.empty() && ws_connected_) {
                esp_websocket_client_send_bin(websocket_, reinterpret_cast<const char*>(opus.data()), static_cast<int>(opus.size()), portMAX_DELAY);
            }
        });
    }

    void SendListen(const char* state) {
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
        SendText(text);
        cJSON_free(text);
        cJSON_Delete(root);
    }

    void SendAbort() {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "abort");
        cJSON_AddStringToObject(root, "reason", "vad_interrupt");
        if (!session_id_.empty()) {
            cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
        }
        char* text = cJSON_PrintUnformatted(root);
        SendText(text);
        cJSON_free(text);
        cJSON_Delete(root);
    }

    void SendText(const char* text) {
        if (websocket_ && ws_connected_ && text) {
            esp_websocket_client_send_text(websocket_, text, strlen(text), portMAX_DELAY);
        }
    }

};

extern "C" void app_main(void) {
    EchoEarGroundApp::Instance().Start();
}
