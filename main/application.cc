#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "ml307_ssl_transport.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "iot/thing_manager.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "settings.h"
#include "audio_debugger.h"
#include "animation/animation_updater.h"
#include "animation/animation.h"
#include "ssid_manager.h"
#include "wifi_station.h"
#include "display/lcd_display.h"

#if CONFIG_USE_AUDIO_PROCESSOR
#include "afe_audio_processor.h"
#else
#include "no_audio_processor.h"
#endif

#if CONFIG_USE_AFE_WAKE_WORD
#include "afe_wake_word.h"
#elif CONFIG_USE_ESP_WAKE_WORD
#include "esp_wake_word.h"
#else
#include "no_wake_word.h"
#endif

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>

#define TAG "Application"

#ifndef DEFAULT_MQTT_ENDPOINT
#define DEFAULT_MQTT_ENDPOINT ""
#endif
#ifndef DEFAULT_MQTT_PUBLISH_TEMPLATE
#define DEFAULT_MQTT_PUBLISH_TEMPLATE "xiaozhi/%s/up"
#endif

// Minimal WAV-from-HTTP player for POC: expects mono 16-bit PCM at codec sample rate
static bool PlayWavFromUrl(const std::string &url, float gain)
{
    auto &board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (!codec)
    {
        ESP_LOGE(TAG, "Audio codec not available");
        return false;
    }

    std::unique_ptr<Http> http(board.CreateHttp());
    if (!http)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return false;
    }
    http->SetHeader("Accept", "audio/wav,application/octet-stream");
    http->SetHeader("Accept-Encoding", "identity");
    http->SetTimeout(10000);

    ESP_LOGI(TAG, "Opening URL: %s", url.c_str());
    if (!http->Open("GET", url))
    {
        ESP_LOGE(TAG, "HTTP open failed");
        return false;
    }
    int status = http->GetStatusCode();
    if (status != 200)
    {
        ESP_LOGE(TAG, "HTTP status %d", status);
        http->Close();
        return false;
    }

    // Naively skip 44-byte WAV header for PCM WAV
    int to_skip = 44;
    char hdr[64];
    while (to_skip > 0)
    {
        int n = http->Read(hdr, to_skip > (int)sizeof(hdr) ? (int)sizeof(hdr) : to_skip);
        if (n <= 0)
        {
            break;
        }
        to_skip -= n;
    }

    // Stream PCM frames -> codec
    const size_t BUF = 1024;
    std::unique_ptr<uint8_t[]> buf(new uint8_t[BUF]);
    bool have_leftover = false;
    uint8_t leftover = 0;
    gain = (gain <= 0.0f) ? 1.0f : gain;
    codec->EnableOutput(true);

    while (true)
    {
        int n = http->Read(reinterpret_cast<char *>(buf.get()), BUF);
        if (n <= 0)
        {
            break;
        }

        size_t offset = 0;
        std::vector<int16_t> samples;
        samples.reserve(n / 2 + 1);

        // Handle odd byte from previous chunk
        if (have_leftover)
        {
            int16_t s = (int16_t)((uint16_t)buf[offset] << 8 | leftover);
            int32_t v = (int32_t)(s * gain);
            if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
            samples.push_back((int16_t)v);
            offset += 1;
            have_leftover = false;
        }

        // Convert little-endian 16-bit to int16_t, apply gain
        for (; offset + 1 < (size_t)n; offset += 2)
        {
            int16_t s = (int16_t)((uint16_t)buf[offset + 1] << 8 | buf[offset]);
            int32_t v = (int32_t)(s * gain);
            if (v > 32767)
                v = 32767;
            else if (v < -32768)
                v = -32768;
            samples.push_back((int16_t)v);
        }

        // Save leftover byte if odd length
        if (offset < (size_t)n)
        {
            leftover = buf[offset];
            have_leftover = true;
        }

        if (!samples.empty())
        {
            codec->OutputData(samples);
        }
    }

    http->Close();
    return true;
}

static const char *const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"};

Application::Application()
{
    event_group_ = xEventGroupCreate();
    background_task_ = new BackgroundTask(4096 * 7);

#if CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

#if CONFIG_USE_AUDIO_PROCESSOR
    audio_processor_ = std::make_unique<AfeAudioProcessor>();
#else
    audio_processor_ = std::make_unique<NoAudioProcessor>();
#endif

#if CONFIG_USE_AFE_WAKE_WORD
    wake_word_ = std::make_unique<AfeWakeWord>();
#elif CONFIG_USE_ESP_WAKE_WORD
    wake_word_ = std::make_unique<EspWakeWord>();
#else
    wake_word_ = std::make_unique<NoWakeWord>();
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void *arg)
        {
            Application *app = (Application *)arg;
            app->OnClockTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true};
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application()
{
    if (clock_timer_handle_ != nullptr)
    {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    if (background_task_ != nullptr)
    {
        delete background_task_;
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckNewVersion()
{
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    while (true)
    {
        SetDeviceState(kDeviceStateActivating);
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota_.CheckVersion())
        {
            retry_count++;
            if (retry_count >= MAX_RETRY)
            {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[128];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota_.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "sad", Lang::Sounds::P3_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++)
            {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle)
                {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota_.HasNewVersion())
        {
            Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "happy", Lang::Sounds::P3_UPGRADE);

            vTaskDelay(pdMS_TO_TICKS(3000));

            SetDeviceState(kDeviceStateUpgrading);

            // display->SetIcon(FONT_AWESOME_DOWNLOAD);
            std::string message = std::string(Lang::Strings::NEW_VERSION) + ota_.GetFirmwareVersion();
            // DISABLED: Comment out transcript display to reduce memory usage
            // display->SetChatMessage("system", message.c_str());

            auto &board = Board::GetInstance();
            board.SetPowerSaveMode(false);
            wake_word_->StopDetection();
            // 预先关闭音频输出，避免升级过程有音频操作
            auto codec = board.GetAudioCodec();
            codec->EnableInput(false);
            codec->EnableOutput(false);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                audio_decode_queue_.clear();
            }
            background_task_->WaitForCompletion();
            delete background_task_;
            background_task_ = nullptr;
            vTaskDelay(pdMS_TO_TICKS(1000));

            ota_.StartUpgrade([display](int progress, size_t speed)
                              {
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                // DISABLED: Comment out transcript display to reduce memory usage
                // display->SetChatMessage("system", buffer); 
                });

            // If upgrade success, the device will reboot and never reach here
            display->SetStatus(Lang::Strings::UPGRADE_FAILED);
            ESP_LOGI(TAG, "Firmware upgrade failed...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            Reboot();
            return;
        }

        // No new version, mark the current version as valid
        ota_.MarkCurrentVersionValid();
        if (!ota_.HasActivationCode() && !ota_.HasActivationChallenge())
        {
            xEventGroupSetBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_.HasActivationCode())
        {
            ShowActivationCode();
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i)
        {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_.Activate();
            if (err == ESP_OK)
            {
                xEventGroupSetBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT);
                break;
            }
            else if (err == ESP_ERR_TIMEOUT)
            {
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle)
            {
                break;
            }
        }
    }
}

void Application::ShowActivationCode()
{
    auto &message = ota_.GetActivationMessage();
    auto &code = ota_.GetActivationCode();

    struct digit_sound
    {
        char digit;
        const std::string_view &sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{digit_sound{'0', Lang::Sounds::P3_0},
                                                           digit_sound{'1', Lang::Sounds::P3_1},
                                                           digit_sound{'2', Lang::Sounds::P3_2},
                                                           digit_sound{'3', Lang::Sounds::P3_3},
                                                           digit_sound{'4', Lang::Sounds::P3_4},
                                                           digit_sound{'5', Lang::Sounds::P3_5},
                                                           digit_sound{'6', Lang::Sounds::P3_6},
                                                           digit_sound{'7', Lang::Sounds::P3_7},
                                                           digit_sound{'8', Lang::Sounds::P3_8},
                                                           digit_sound{'9', Lang::Sounds::P3_9}}};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "happy", Lang::Sounds::P3_ACTIVATION);

    for (const auto &digit : code)
    {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
                               [digit](const digit_sound &ds)
                               { return ds.digit == digit; });
        if (it != digit_sounds.end())
        {
            PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char *status, const char *message, const char *emotion, const std::string_view &sound)
{
    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    // DISABLED: Comment out transcript display to reduce memory usage
    // display->SetChatMessage("system", message);
    if (!sound.empty())
    {
        ResetDecoder();
        PlaySound(sound);
    }
}

void Application::DismissAlert()
{
    if (device_state_ == kDeviceStateIdle)
    {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        // DISABLED: Comment out transcript display to reduce memory usage
        // display->SetChatMessage("system", "");
    }
}

void Application::PlaySound(const std::string_view &sound)
{
    // Wait for the previous sound to finish
    {
        std::unique_lock<std::mutex> lock(mutex_);
        audio_decode_cv_.wait(lock, [this]()
                              { return audio_decode_queue_.empty(); });
    }
    background_task_->WaitForCompletion();

    const char *data = sound.data();
    size_t size = sound.size();
    for (const char *p = data; p < data + size;)
    {
        auto p3 = (BinaryProtocol3 *)p;
        p += sizeof(BinaryProtocol3);

        auto payload_size = ntohs(p3->payload_size);
        AudioStreamPacket packet;
        packet.sample_rate = 16000;
        packet.frame_duration = 60;
        packet.payload.resize(payload_size);
        memcpy(packet.payload.data(), p3->payload, payload_size);
        p += payload_size;

        std::lock_guard<std::mutex> lock(mutex_);
        audio_decode_queue_.emplace_back(std::move(packet));
    }
}

void Application::EnterAudioTestingMode()
{
    ESP_LOGI(TAG, "Entering audio testing mode");
    ResetDecoder();
    SetDeviceState(kDeviceStateAudioTesting);
}

void Application::ExitAudioTestingMode()
{
    ESP_LOGI(TAG, "Exiting audio testing mode");
    SetDeviceState(kDeviceStateWifiConfiguring);
    // Copy audio_testing_queue_ to audio_decode_queue_
    std::lock_guard<std::mutex> lock(mutex_);
    audio_decode_queue_ = std::move(audio_testing_queue_);
    audio_decode_cv_.notify_all();
}

void Application::ToggleChatState()
{
    if (device_state_ == kDeviceStateActivating)
    {
        SetDeviceState(kDeviceStateIdle);
        return;
    }
    else if (device_state_ == kDeviceStateWifiConfiguring)
    {
        EnterAudioTestingMode();
        return;
    }
    else if (device_state_ == kDeviceStateAudioTesting)
    {
        ExitAudioTestingMode();
        return;
    }

    if (!protocol_)
    {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle)
    {
        Schedule([this]()
                 {
            auto* active_protocol = GetActiveProtocol();
            if (!active_protocol->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                
                // Always use WebSocket for manual interactions (with default fallback URL)
                // WebSocket protocol will use default URL if none is configured
                if (!websocket_protocol_ || !websocket_protocol_->IsAudioChannelOpened()) {
                    ESP_LOGI(TAG, "Opening WebSocket connection for manual interaction (will use default URL if needed)");
                    OpenWebSocketConnection();
                    active_protocol = GetActiveProtocol();
                }
                
                // If WebSocket failed, fall back to primary protocol (MQTT)
                if (!active_protocol->IsAudioChannelOpened()) {
                    ESP_LOGW(TAG, "WebSocket connection failed, falling back to primary protocol");
                    if (!active_protocol->OpenAudioChannel()) {
                        return;
                    }
                }
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime); });
    }
    else if (device_state_ == kDeviceStateSpeaking)
    {
        Schedule([this]()
                 { AbortSpeaking(kAbortReasonNone); });
    }
    else if (device_state_ == kDeviceStateListening)
    {
        Schedule([this]()
                 { 
                     auto* active_protocol = GetActiveProtocol();
                     if (active_protocol) {
                         active_protocol->CloseAudioChannel();
                     }
                 });
    }
}

void Application::StartListening()
{
    if (device_state_ == kDeviceStateActivating)
    {
        SetDeviceState(kDeviceStateIdle);
        return;
    }
    else if (device_state_ == kDeviceStateWifiConfiguring)
    {
        EnterAudioTestingMode();
        return;
    }

    if (!protocol_)
    {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle)
    {
        Schedule([this]()
                 {
            auto* active_protocol = GetActiveProtocol();
            if (!active_protocol->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                
                // Always use WebSocket for manual interactions (with default fallback URL)
                // WebSocket protocol will use default URL if none is configured
                if (!websocket_protocol_ || !websocket_protocol_->IsAudioChannelOpened()) {
                    ESP_LOGI(TAG, "Opening WebSocket connection for manual interaction (will use default URL if needed)");
                    OpenWebSocketConnection();
                    active_protocol = GetActiveProtocol();
                }
                
                // If WebSocket failed, fall back to primary protocol (MQTT)
                if (!active_protocol->IsAudioChannelOpened()) {
                    ESP_LOGW(TAG, "WebSocket connection failed, falling back to primary protocol");
                    if (!active_protocol->OpenAudioChannel()) {
                        return;
                    }
                }
            }

            SetListeningMode(kListeningModeManualStop); });
    }
    else if (device_state_ == kDeviceStateSpeaking)
    {
        Schedule([this]()
                 {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop); });
    }
}

void Application::StopListening()
{
    if (device_state_ == kDeviceStateAudioTesting)
    {
        ExitAudioTestingMode();
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end())
    {
        return;
    }

    Schedule([this]()
             {
        if (device_state_ == kDeviceStateListening) {
            auto* active_protocol = GetActiveProtocol();
            if (active_protocol) {
                ESP_LOGI(TAG, "Sending listen stop message");
                active_protocol->SendStopListening();
            }
            SetDeviceState(kDeviceStateIdle);
        } });
}

void Application::Start()
{
    auto &board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    /* Setup the audio codec */
    auto codec = board.GetAudioCodec();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(codec->output_sample_rate(), 1, OPUS_FRAME_DURATION_MS);
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    if (aec_mode_ != kAecOff)
    {
        ESP_LOGI(TAG, "AEC mode: %d, setting opus encoder complexity to 0", aec_mode_);
        opus_encoder_->SetComplexity(0);
    }
    else if (board.GetBoardType() == "ml307")
    {
        ESP_LOGI(TAG, "ML307 board detected, setting opus encoder complexity to 5");
        opus_encoder_->SetComplexity(5);
    }
    else
    {
        ESP_LOGI(TAG, "WiFi board detected, setting opus encoder complexity to 0");
        opus_encoder_->SetComplexity(0);
    }

    if (codec->input_sample_rate() != 16000)
    {
        input_resampler_.Configure(codec->input_sample_rate(), 16000);
        reference_resampler_.Configure(codec->input_sample_rate(), 16000);
    }
    codec->Start();

#if CONFIG_USE_AUDIO_PROCESSOR
    xTaskCreatePinnedToCore([](void *arg)
                            {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL); }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_, 1);
#else
    xTaskCreate([](void *arg)
                {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL); }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_);
#endif

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Check if we should clear WiFi configuration to force nimBLE setup */
    // Only clear WiFi if no credentials exist (first boot or manual clearing)
    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    if (ssid_list.empty()) {
        ESP_LOGI(TAG, "No WiFi credentials found, nimBLE will start for configuration");
    } else {
        ESP_LOGI(TAG, "WiFi credentials found (%d networks), proceeding with normal startup", ssid_list.size());
    }

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);

    // Check WiFi connection status and show message if not connected
    // Note: If no WiFi credentials exist, StartNetwork() will block in WiFi config mode,
    // so this code only runs if WiFi credentials exist but connection failed or is in progress
    if (board.GetBoardType() != "ml307") {
        ESP_LOGI(TAG, "Checking WiFi connection status...");
        auto& wifi_station = WifiStation::GetInstance();
        bool is_connected = wifi_station.IsConnected();
        ESP_LOGI(TAG, "WiFi connection status: %s", is_connected ? "CONNECTED" : "NOT CONNECTED");
        ESP_LOGI(TAG, "Device state: %d (kDeviceStateWifiConfiguring=%d)", device_state_, kDeviceStateWifiConfiguring);
        
        if (!is_connected) {
            // Show message to guide user to connect WiFi (only if not already in config mode)
            // If in config mode, the message is already shown in wifi_board.cc
            if (device_state_ != kDeviceStateWifiConfiguring) {
                ESP_LOGI(TAG, "Not in WiFi config mode, attempting to show WiFi connection message");
                const char* wifi_message = "Connect me to wifi with BabyMilu App. Can't wait to meet you again.";
                
                // Try to use LcdDisplay::CreateSystemMessage if available
                LcdDisplay* lcd_display = static_cast<LcdDisplay*>(display);
                if (lcd_display != nullptr) {
                    ESP_LOGI(TAG, "Display is LcdDisplay, using CreateSystemMessage");
                    lcd_display->CreateSystemMessage(wifi_message);
                }
                
                // Also try standard methods as fallback
                display->SetChatMessage("system", wifi_message);
                ESP_LOGI(TAG, "Called SetChatMessage with WiFi message");
                
                // Also try ShowNotification as fallback
                vTaskDelay(pdMS_TO_TICKS(100));
                display->ShowNotification(wifi_message, 0);
                ESP_LOGI(TAG, "Called ShowNotification with WiFi message");
            } else {
                ESP_LOGI(TAG, "Already in WiFi config mode, message should be shown by wifi_board.cc");
            }
        } else {
            ESP_LOGI(TAG, "WiFi is connected, checking animation availability...");
            // WiFi is connected, check if animation is available
            Animation_t* current_anim = animation_get_normal_animation();
            ESP_LOGI(TAG, "Animation check: current_anim=%p", current_anim);
            if (current_anim != NULL) {
                ESP_LOGI(TAG, "Animation available: len=%d", current_anim->len);
            }
            
            if (current_anim == NULL || current_anim->len == 0) {
                ESP_LOGI(TAG, "No animation available, showing connected message");
                // No animation available, show connected message (display in center of screen)
                const char* connected_message = "Connected! I am traveling over :D";
                
                // Try to use LcdDisplay::CreateSystemMessage if available
                LcdDisplay* lcd_display = static_cast<LcdDisplay*>(display);
                if (lcd_display != nullptr) {
                    ESP_LOGI(TAG, "Display is LcdDisplay, using CreateSystemMessage for connected message");
                    lcd_display->CreateSystemMessage(connected_message);
                }
                
                // Also try standard methods as fallback
                display->SetChatMessage("system", connected_message);
                ESP_LOGI(TAG, "Called SetChatMessage with connected message");
                
                // Also try ShowNotification as fallback
                vTaskDelay(pdMS_TO_TICKS(100));
                display->ShowNotification(connected_message, 0);
                ESP_LOGI(TAG, "Called ShowNotification with connected message");
            } else {
                ESP_LOGI(TAG, "Animation is available, not showing connected message");
            }
        }
    } else {
        ESP_LOGI(TAG, "ML307 board detected, skipping WiFi message display");
    }

    // Initialize and start the animation updater
    AnimationUpdater::GetInstance().Initialize();
    AnimationUpdater::GetInstance().Start();

    // Check for new firmware version or get the MQTT broker address
    CheckNewVersion();

    // Seed MQTT config on first boot if unset (allows setting broker at build time)
    {
        Settings mqtt_settings("mqtt", true);
        auto endpoint = mqtt_settings.GetString("endpoint");
        if (endpoint.empty() && std::string(DEFAULT_MQTT_ENDPOINT).size() > 0)
        {
            auto mac = SystemInfo::GetMacAddress();
            char up_topic[128];
            snprintf(up_topic, sizeof(up_topic), DEFAULT_MQTT_PUBLISH_TEMPLATE, mac.c_str());
            mqtt_settings.SetString("endpoint", DEFAULT_MQTT_ENDPOINT);
            mqtt_settings.SetString("client_id", mac);
            mqtt_settings.SetString("publish_topic", up_topic);
            // Optional username/password can be added similarly if needed
            ESP_LOGI(TAG, "Seeded MQTT endpoint to %s", DEFAULT_MQTT_ENDPOINT);
        }
    }

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
#if CONFIG_IOT_PROTOCOL_MCP
    McpServer::GetInstance().AddCommonTools();
#endif

    // Protocol initialization strategy:
    // - MQTT (primary): Always connected for control messages (ws_start, wake word, etc.)
    // - WebSocket (on-demand): Created when needed for audio conversations
    // 
    // Both can be active simultaneously:
    // - MQTT: Handles server-initiated conversations (receives ws_start, can handle MQTT+UDP audio)
    // - WebSocket: Handles user-initiated conversations (button press) and server-initiated (via ws_start)
    // 
    // Audio routing: WebSocket takes priority when open, otherwise MQTT is used
    if (ota_.HasMqttConfig())
    {
        protocol_ = std::make_unique<MqttProtocol>();
    }
    else if (ota_.HasWebsocketConfig())
    {
        // If only WebSocket is configured, use it as primary
        protocol_ = std::make_unique<WebsocketProtocol>();
    }
    else
    {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }
    
    // WebSocket protocol will be created on-demand for audio conversations
    // MQTT stays connected for control messages even when WebSocket is active

    protocol_->OnNetworkError([this](const std::string &message)
                              {
        SetDeviceState(kDeviceStateIdle);
        Alert(Lang::Strings::ERROR, message.c_str(), "sad", Lang::Sounds::P3_EXCLAMATION); });
    protocol_->OnIncomingAudio([this](AudioStreamPacket &&packet)
                               {
        std::lock_guard<std::mutex> lock(mutex_);
        if (device_state_ == kDeviceStateSpeaking && audio_decode_queue_.size() < MAX_AUDIO_PACKETS_IN_QUEUE) {
            audio_decode_queue_.emplace_back(std::move(packet));
        } });
    protocol_->OnAudioChannelOpened([this, codec, &board]()
                                    {
                                        board.SetPowerSaveMode(false);
                                        if (protocol_->server_sample_rate() != codec->output_sample_rate())
                                        {
                                            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                                                     protocol_->server_sample_rate(), codec->output_sample_rate());
                                        }

#if CONFIG_IOT_PROTOCOL_XIAOZHI
                                        auto &thing_manager = iot::ThingManager::GetInstance();
                                        protocol_->SendIotDescriptors(thing_manager.GetDescriptorsJson());
                                        std::string states;
                                        if (thing_manager.GetStatesJson(states, false))
                                        {
                                            protocol_->SendIotStates(states);
                                        }
#endif
                                    });
    protocol_->OnAudioChannelClosed([this, &board]()
                                    {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            /* removed unused: display */
            // DISABLED: Comment out transcript display to reduce memory usage
            // display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        }); });
    protocol_->OnIncomingJson([this, display](const cJSON *root)
                              {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        // Save state before TTS to determine if we should resume listening after TTS
                        state_before_tts_ = device_state_;
                        
                        // If we're in LISTENING state, stop listening before TTS starts
                        // This ensures no microphone audio interference during TTS playback
                        if (device_state_ == kDeviceStateListening) {
                            auto* active_protocol = GetActiveProtocol();
                            if (active_protocol && active_protocol->IsAudioChannelOpened()) {
                                ESP_LOGI(TAG, "TTS starting while listening - stopping listening before TTS");
                                active_protocol->SendStopListening();
                                // Small delay to ensure server receives listen:stop before TTS starts
                                vTaskDelay(pdMS_TO_TICKS(50));
                            }
                        }
                        
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    ESP_LOGI(TAG, "TTS stop (primary): state=%d, listening_mode=%d", device_state_, (int)listening_mode_);
                    background_task_->WaitForCompletion();
                    if (device_state_ == kDeviceStateSpeaking) {
                        // Check if remote wakeup scenario (WebSocket still open)
                        auto* active_protocol = GetActiveProtocol();
                        bool is_remote_wakeup = (websocket_protocol_ != nullptr && 
                                                websocket_protocol_->IsAudioChannelOpened() &&
                                                active_protocol && active_protocol->IsAudioChannelOpened());
                        
                        if (is_remote_wakeup) {
                            // Automatically resume listening after TTS in remote wakeup scenario
                            if (is_alarm_mode_) {
                                // Alarm mode: Always start listening after TTS (even if wasn't listening before)
                                // This is the expected flow: ws_start → TTS → listening
                                ESP_LOGI(TAG, "TTS stopped, alarm mode - starting listening for user response");
                                SetDeviceState(kDeviceStateListening);
                                is_alarm_mode_ = false; // Reset alarm mode after first TTS completes
                            } else if (state_before_tts_ == kDeviceStateListening) {
                                // Normal remote wakeup: Only resume if we were listening before TTS started
                                ESP_LOGI(TAG, "TTS stopped, remote wakeup detected (WebSocket open) - automatically resuming listening");
                                SetDeviceState(kDeviceStateListening);
                            } else {
                                ESP_LOGI(TAG, "TTS stopped, remote wakeup detected but was not listening before TTS - going to idle");
                                SetDeviceState(kDeviceStateIdle);
                            }
                            state_before_tts_ = kDeviceStateUnknown; // Reset
                        } else if (listening_mode_ == kListeningModeManualStop) {
                            // Go to idle for manual interactions
                            ESP_LOGI(TAG, "TTS stopped, going to idle (manual stop mode)");
                            SetDeviceState(kDeviceStateIdle);
                            state_before_tts_ = kDeviceStateUnknown; // Reset
                        } else {
                            // Auto mode: resume listening
                            ESP_LOGI(TAG, "TTS stopped, automatically resuming listening (auto stop mode)");
                            SetDeviceState(kDeviceStateListening);
                            state_before_tts_ = kDeviceStateUnknown; // Reset
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    // DISABLED: Comment out transcript display to reduce memory usage
                    // Schedule([this, display, message = std::string(text->valuestring)]() {
                    //     display->SetChatMessage("assistant", message.c_str());
                    // });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                // DISABLED: Comment out transcript display to reduce memory usage
                // Schedule([this, display, message = std::string(text->valuestring)]() {
                //     display->SetChatMessage("user", message.c_str());
                // });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
#if CONFIG_IOT_PROTOCOL_MCP
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
#endif
#if CONFIG_IOT_PROTOCOL_XIAOZHI
        } else if (strcmp(type->valuestring, "iot") == 0) {
            auto commands = cJSON_GetObjectItem(root, "commands");
            if (cJSON_IsArray(commands)) {
                auto& thing_manager = iot::ThingManager::GetInstance();
                for (int i = 0; i < cJSON_GetArraySize(commands); ++i) {
                    auto command = cJSON_GetArrayItem(commands, i);
                    thing_manager.Invoke(command);
                }
            }
#endif
        } else if (strcmp(type->valuestring, "listen") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (cJSON_IsString(state)) {
                if (strcmp(state->valuestring, "start") == 0) {
                    ESP_LOGI(TAG, "Received listen:start from server, starting listening");
                    Schedule([this]() { 
                        ESP_LOGI(TAG, "Executing listen:start - setting listening mode (AutoStop)");
                        SetListeningMode(kListeningModeAutoStop); 
                    });
                } else if (strcmp(state->valuestring, "stop") == 0) {
                    ESP_LOGI(TAG, "Received listen:stop from server, stopping listening");
                    Schedule([this]() { StopListening(); });
                } else {
                    ESP_LOGW(TAG, "Received listen message with unknown state: %s", state->valuestring);
                }
            } else {
                ESP_LOGW(TAG, "Received listen message without valid state field");
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::P3_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
        } else if (strcmp(type->valuestring, "play_url") == 0) {
            auto url = cJSON_GetObjectItem(root, "url");
            auto gain = cJSON_GetObjectItem(root, "gain");
            if (cJSON_IsString(url)) {
                float g = cJSON_IsNumber(gain) ? (float)gain->valuedouble : 1.0f;
                // Run playback in background to avoid blocking main loop
                Schedule([this, url_str = std::string(url->valuestring), g]() {
                    ResetDecoder();
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                    background_task_->Schedule([this, url_str, g]() {
                        bool ok = PlayWavFromUrl(url_str, g);
                        Schedule([this, ok]() {
                            if (device_state_ == kDeviceStateSpeaking) {
                                SetDeviceState(kDeviceStateIdle);
                            }
                            ESP_LOGI(TAG, "PlayWavFromUrl %s", ok ? "done" : "failed");
                        });
                    });
                });
            } else {
                ESP_LOGW(TAG, "play_url missing 'url' field");
            }
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        } });
    bool protocol_started = protocol_->Start();

    audio_debugger_ = std::make_unique<AudioDebugger>();
    audio_processor_->Initialize(codec);
    audio_processor_->OnOutput([this](std::vector<int16_t> &&data)
                               {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (audio_send_queue_.size() >= MAX_AUDIO_PACKETS_IN_QUEUE) {
                ESP_LOGW(TAG, "Too many audio packets in queue, drop the newest packet");
                return;
            }
        }
        background_task_->Schedule([this, data = std::move(data)]() mutable {
            opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                AudioStreamPacket packet;
                packet.payload = std::move(opus);
#ifdef CONFIG_USE_SERVER_AEC
                {
                    std::lock_guard<std::mutex> lock(timestamp_mutex_);
                    if (!timestamp_queue_.empty()) {
                        packet.timestamp = timestamp_queue_.front();
                        timestamp_queue_.pop_front();
                    } else {
                        packet.timestamp = 0;
                    }

                    if (timestamp_queue_.size() > 3) { // 限制队列长度3
                        timestamp_queue_.pop_front(); // 该包发送前先出队保持队列长度
                        return;
                    }
                }
#endif
                std::lock_guard<std::mutex> lock(mutex_);
                if (audio_send_queue_.size() >= MAX_AUDIO_PACKETS_IN_QUEUE) {
                    ESP_LOGW(TAG, "Too many audio packets in queue, drop the oldest packet");
                    audio_send_queue_.pop_front();
                }
                audio_send_queue_.emplace_back(std::move(packet));
                xEventGroupSetBits(event_group_, SEND_AUDIO_EVENT);
            });
        }); });
    audio_processor_->OnVadStateChange([this](bool speaking)
                                       {
        if (device_state_ == kDeviceStateListening) {
            Schedule([this, speaking]() {
                if (speaking) {
                    voice_detected_ = true;
                } else {
                    voice_detected_ = false;
                }
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            });
        } });

    wake_word_->Initialize(codec);
    wake_word_->OnWakeWordDetected([this](const std::string &wake_word)
                                   { Schedule([this, &wake_word]()
                                              {
            if (!protocol_) {
                return;
            }

            if (device_state_ == kDeviceStateIdle) {
                wake_word_->EncodeWakeWordData();

                auto* active_protocol = GetActiveProtocol();
                if (!active_protocol->IsAudioChannelOpened()) {
                    SetDeviceState(kDeviceStateConnecting);
                    
                    // Always use WebSocket for wake word interactions (with default fallback URL)
                    // WebSocket protocol will use default URL if none is configured
                    if (!websocket_protocol_ || !websocket_protocol_->IsAudioChannelOpened()) {
                        ESP_LOGI(TAG, "Opening WebSocket connection for wake word (will use default URL if needed)");
                        OpenWebSocketConnection();
                        active_protocol = GetActiveProtocol();
                    }
                    
                    // If WebSocket failed, fall back to primary protocol (MQTT)
                    if (!active_protocol->IsAudioChannelOpened()) {
                        ESP_LOGW(TAG, "WebSocket connection failed, falling back to primary protocol");
                        if (!active_protocol->OpenAudioChannel()) {
                            wake_word_->StartDetection();
                            return;
                        }
                    }
                }

                ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD
                AudioStreamPacket packet;
                // Encode and send the wake word data to the server
                while (wake_word_->GetWakeWordOpus(packet.payload)) {
                    auto* active_protocol = GetActiveProtocol();
                    if (active_protocol) {
                        active_protocol->SendAudio(packet);
                    }
                }
                // Set the chat state to wake word detected
                // Use primary protocol for wake word detection (MQTT)
                if (protocol_) {
                    protocol_->SendWakeWordDetected(wake_word);
                }
#else
                // Play the pop up sound to indicate the wake word is detected
                // And wait 60ms to make sure the queue has been processed by audio task
                ResetDecoder();
                PlaySound(Lang::Sounds::P3_POPUP);
                vTaskDelay(pdMS_TO_TICKS(60));
#endif
                SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
            } else if (device_state_ == kDeviceStateSpeaking) {
                AbortSpeaking(kAbortReasonWakeWordDetected);
            } else if (device_state_ == kDeviceStateActivating) {
                SetDeviceState(kDeviceStateIdle);
            } }); });
    wake_word_->StartDetection();

    // Wait for the new version check to finish
    xEventGroupWaitBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);
    SetDeviceState(kDeviceStateIdle);

    if (protocol_started)
    {
        std::string message = std::string(Lang::Strings::VERSION) + ota_.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        // DISABLED: Comment out transcript display to reduce memory usage
        // display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        ResetDecoder();
        PlaySound(Lang::Sounds::P3_SUCCESS);
    }

    // Print heap stats
    SystemInfo::PrintHeapStats();

    // Enter the main event loop
    MainEventLoop();
}

void Application::OnClockTimer()
{
    clock_ticks_++;

    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar();

    // Print the debug info every 10 seconds
    if (clock_ticks_ % 10 == 0)
    {
        // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
        // SystemInfo::PrintTaskList();
        SystemInfo::PrintHeapStats();

        // If we have synchronized server time, set the status to clock "HH:MM" if the device is idle
        if (ota_.HasServerTime())
        {
            if (device_state_ == kDeviceStateIdle)
            {
                Schedule([this]()
                         {
                    // Set status to clock "HH:MM"
                    time_t now = time(NULL);
                    char time_str[64];
                    strftime(time_str, sizeof(time_str), "%H:%M  ", localtime(&now));
                    Board::GetInstance().GetDisplay()->SetStatus(time_str); });
            }
        }
    }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, SCHEDULE_EVENT);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop()
{
    // Raise the priority of the main event loop to avoid being interrupted by background tasks (which has priority 2)
    vTaskPrioritySet(NULL, 3);

    while (true)
    {
        auto bits = xEventGroupWaitBits(event_group_, SCHEDULE_EVENT | SEND_AUDIO_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & SEND_AUDIO_EVENT)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto packets = std::move(audio_send_queue_);
            lock.unlock();
            for (auto &packet : packets)
            {
                auto* active_protocol = GetActiveProtocol();
                if (!active_protocol || !active_protocol->SendAudio(packet))
                {
                    break;
                }
            }
        }

        if (bits & SCHEDULE_EVENT)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto &task : tasks)
            {
                task();
            }
        }
    }
}

// The Audio Loop is used to input and output audio data
void Application::AudioLoop()
{
    auto codec = Board::GetInstance().GetAudioCodec();
    while (true)
    {
        OnAudioInput();
        if (codec->output_enabled())
        {
            OnAudioOutput();
        }
    }
}

void Application::OnAudioOutput()
{
    if (busy_decoding_audio_)
    {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    const int max_silence_seconds = 10;

    std::unique_lock<std::mutex> lock(mutex_);
    if (audio_decode_queue_.empty())
    {
        // Disable the output if there is no audio data for a long time
        if (device_state_ == kDeviceStateIdle)
        {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_output_time_).count();
            if (duration > max_silence_seconds)
            {
                codec->EnableOutput(false);
            }
        }
        return;
    }

    auto packet = std::move(audio_decode_queue_.front());
    audio_decode_queue_.pop_front();
    lock.unlock();
    audio_decode_cv_.notify_all();

    // Synchronize the sample rate and frame duration
    SetDecodeSampleRate(packet.sample_rate, packet.frame_duration);

    busy_decoding_audio_ = true;
    background_task_->Schedule([this, codec, packet = std::move(packet)]() mutable
                               {
        busy_decoding_audio_ = false;
        if (aborted_) {
            return;
        }

        std::vector<int16_t> pcm;
        if (!opus_decoder_->Decode(std::move(packet.payload), pcm)) {
            return;
        }
        // Resample if the sample rate is different
        if (opus_decoder_->sample_rate() != codec->output_sample_rate()) {
            int target_size = output_resampler_.GetOutputSamples(pcm.size());
            std::vector<int16_t> resampled(target_size);
            output_resampler_.Process(pcm.data(), pcm.size(), resampled.data());
            pcm = std::move(resampled);
        }
        codec->OutputData(pcm);
#ifdef CONFIG_USE_SERVER_AEC
        std::lock_guard<std::mutex> lock(timestamp_mutex_);
        timestamp_queue_.push_back(packet.timestamp);
#endif
        last_output_time_ = std::chrono::steady_clock::now(); });
}

void Application::OnAudioInput()
{
    if (device_state_ == kDeviceStateAudioTesting)
    {
        if (audio_testing_queue_.size() >= AUDIO_TESTING_MAX_DURATION_MS / OPUS_FRAME_DURATION_MS)
        {
            ExitAudioTestingMode();
            return;
        }
        std::vector<int16_t> data;
        int samples = OPUS_FRAME_DURATION_MS * 16000 / 1000;
        if (ReadAudio(data, 16000, samples))
        {
            background_task_->Schedule([this, data = std::move(data)]() mutable
                                       { opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t> &&opus)
                                                               {
                    AudioStreamPacket packet;
                    packet.payload = std::move(opus);
                    packet.frame_duration = OPUS_FRAME_DURATION_MS;
                    packet.sample_rate = 16000;
                    std::lock_guard<std::mutex> lock(mutex_);
                    audio_testing_queue_.push_back(std::move(packet)); }); });
            return;
        }
    }

    if (wake_word_->IsDetectionRunning())
    {
        std::vector<int16_t> data;
        int samples = wake_word_->GetFeedSize();
        if (samples > 0)
        {
            if (ReadAudio(data, 16000, samples))
            {
                wake_word_->Feed(data);
                return;
            }
        }
    }

    if (audio_processor_->IsRunning())
    {
        std::vector<int16_t> data;
        int samples = audio_processor_->GetFeedSize();
        if (samples > 0)
        {
            if (ReadAudio(data, 16000, samples))
            {
                audio_processor_->Feed(data);
                return;
            }
        }
    }

    vTaskDelay(pdMS_TO_TICKS(OPUS_FRAME_DURATION_MS / 2));
}

bool Application::ReadAudio(std::vector<int16_t> &data, int sample_rate, int samples)
{
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec->input_enabled())
    {
        return false;
    }

    if (codec->input_sample_rate() != sample_rate)
    {
        data.resize(samples * codec->input_sample_rate() / sample_rate);
        if (!codec->InputData(data))
        {
            return false;
        }
        if (codec->input_channels() == 2)
        {
            auto mic_channel = std::vector<int16_t>(data.size() / 2);
            auto reference_channel = std::vector<int16_t>(data.size() / 2);
            for (size_t i = 0, j = 0; i < mic_channel.size(); ++i, j += 2)
            {
                mic_channel[i] = data[j];
                reference_channel[i] = data[j + 1];
            }
            auto resampled_mic = std::vector<int16_t>(input_resampler_.GetOutputSamples(mic_channel.size()));
            auto resampled_reference = std::vector<int16_t>(reference_resampler_.GetOutputSamples(reference_channel.size()));
            input_resampler_.Process(mic_channel.data(), mic_channel.size(), resampled_mic.data());
            reference_resampler_.Process(reference_channel.data(), reference_channel.size(), resampled_reference.data());
            data.resize(resampled_mic.size() + resampled_reference.size());
            for (size_t i = 0, j = 0; i < resampled_mic.size(); ++i, j += 2)
            {
                data[j] = resampled_mic[i];
                data[j + 1] = resampled_reference[i];
            }
        }
        else
        {
            auto resampled = std::vector<int16_t>(input_resampler_.GetOutputSamples(data.size()));
            input_resampler_.Process(data.data(), data.size(), resampled.data());
            data = std::move(resampled);
        }
    }
    else
    {
        data.resize(samples);
        if (!codec->InputData(data))
        {
            return false;
        }
    }

    // 音频调试：发送原始音频数据
    if (audio_debugger_)
    {
        audio_debugger_->Feed(data);
    }

    return true;
}

void Application::AbortSpeaking(AbortReason reason)
{
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    protocol_->SendAbortSpeaking(reason);
}

void Application::SetListeningMode(ListeningMode mode)
{
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state)
{
    if (device_state_ == state)
    {
        return;
    }

    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);
    // The state is changed, wait for all background tasks to finish
    background_task_->WaitForCompletion();

    auto &board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state)
    {
    case kDeviceStateUnknown:
    case kDeviceStateIdle:
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        audio_processor_->Stop();
        wake_word_->StartDetection();
        break;
    case kDeviceStateConnecting:
        display->SetStatus(Lang::Strings::CONNECTING);
        display->SetEmotion("neutral");
        // DISABLED: Comment out transcript display to reduce memory usage
        // display->SetChatMessage("system", "");
        timestamp_queue_.clear();
        break;
    case kDeviceStateListening:
        display->SetStatus(Lang::Strings::LISTENING);
        display->SetEmotion("neutral");
        // Update the IoT states before sending the start listening command
#if CONFIG_IOT_PROTOCOL_XIAOZHI
        UpdateIotStates();
#endif

        // Make sure the audio processor is running
        if (!audio_processor_->IsRunning())
        {
            // Send the start listening command FIRST
            auto* active_protocol = GetActiveProtocol();
            if (active_protocol && active_protocol->IsAudioChannelOpened()) {
                // Only send listen start if the connection is actually open and ready
                // Detect if this is a remote wakeup scenario (WebSocket already open from ws_start)
                // vs manual connection (opening WebSocket now)
                bool is_remote_wakeup = (websocket_protocol_ && 
                                       websocket_protocol_->IsAudioChannelOpened() && 
                                       previous_state == kDeviceStateIdle);
                // Debug preflight to confirm active protocol, channel status, audio state, and mode
                {
                    bool ws_open = (websocket_protocol_ && websocket_protocol_->IsAudioChannelOpened());
                    const char* active_name = (active_protocol == websocket_protocol_.get() ? "ws"
                                                : (active_protocol ? "mqtt" : "null"));
                    ESP_LOGI(TAG, "DEBUG listen preflight: active=%s, ws_open=%d, audio_running=%d, mode=%d",
                             active_name,
                             ws_open ? 1 : 0,
                             audio_processor_->IsRunning() ? 1 : 0,
                             (int)listening_mode_);
                }
                // Hard-force AutoStop for remote wake so TTS stop resumes listening automatically
                if (is_remote_wakeup) {
                    listening_mode_ = kListeningModeAutoStop;
                }
                ESP_LOGI(TAG, "Sending listen start message, mode=%d (0=auto, 1=manual, 2=realtime)%s", 
                        listening_mode_, is_remote_wakeup ? " [remote wakeup]" : "");
                active_protocol->SendStartListening(listening_mode_);
                
                // For remote wakeup ONLY: Add small delay after listen:start to ensure server
                // has time to process the message before receiving audio frames.
                // Manual connections don't need this delay as they open the connection fresh.
                if (is_remote_wakeup) {
                    ESP_LOGI(TAG, "Remote wakeup: waiting 50ms after listen:start for server to process");
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            } else {
                ESP_LOGW(TAG, "Cannot send listen start: protocol not available or connection not open");
                ESP_LOGW(TAG, "Will send listen start when connection is ready (e.g., after ws_start opens WebSocket)");
            }
            
            if (previous_state == kDeviceStateSpeaking)
            {
                audio_decode_queue_.clear();
                audio_decode_cv_.notify_all();
                // FIXME: Wait for the speaker to empty the buffer
                vTaskDelay(pdMS_TO_TICKS(120));
            }
            
            opus_encoder_->ResetState();
            
            // Log Opus encoder configuration
            auto codec = Board::GetInstance().GetAudioCodec();
            ESP_LOGI(TAG, "Audio config: input_sample_rate=%d, Opus encoder=16000Hz mono, frame_duration=%dms (960 samples)", 
                     codec ? codec->input_sample_rate() : 0, OPUS_FRAME_DURATION_MS);
            
            ESP_LOGI(TAG, "Starting audio capture and streaming...");
            audio_processor_->Start();
            wake_word_->StopDetection();
        }
        break;
    case kDeviceStateSpeaking: {
        display->SetStatus(Lang::Strings::SPEAKING);
        
        // Set brightness to 100 when TTS starts
        auto backlight = board.GetBacklight();
        if (backlight) {
            backlight->SetBrightness(100, true);
            ESP_LOGI(TAG, "TTS started - brightness set to 100");
        }

        if (listening_mode_ != kListeningModeRealtime)
        {
            audio_processor_->Stop();
            // Only AFE wake word can be detected in speaking mode
#if CONFIG_USE_AFE_WAKE_WORD
            wake_word_->StartDetection();
#else
            wake_word_->StopDetection();
#endif
        }
        ResetDecoder();
        break;
    }
    case kDeviceStateStarting:
    case kDeviceStateWifiConfiguring:
    case kDeviceStateUpgrading:
    case kDeviceStateActivating:
    case kDeviceStateAudioTesting:
    case kDeviceStateFatalError:
        // These states are handled elsewhere or don't require special handling here
        break;
    default:
        // Do nothing
        break;
    }
}

void Application::ResetDecoder()
{
    std::lock_guard<std::mutex> lock(mutex_);
    opus_decoder_->ResetState();
    audio_decode_queue_.clear();
    audio_decode_cv_.notify_all();
    last_output_time_ = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableOutput(true);
}

void Application::SetDecodeSampleRate(int sample_rate, int frame_duration)
{
    if (opus_decoder_->sample_rate() == sample_rate && opus_decoder_->duration_ms() == frame_duration)
    {
        return;
    }

    opus_decoder_.reset();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(sample_rate, 1, frame_duration);

    auto codec = Board::GetInstance().GetAudioCodec();
    if (opus_decoder_->sample_rate() != codec->output_sample_rate())
    {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", opus_decoder_->sample_rate(), codec->output_sample_rate());
        output_resampler_.Configure(opus_decoder_->sample_rate(), codec->output_sample_rate());
    }
}

void Application::UpdateIotStates()
{
#if CONFIG_IOT_PROTOCOL_XIAOZHI
    auto &thing_manager = iot::ThingManager::GetInstance();
    std::string states;
    if (thing_manager.GetStatesJson(states, true))
    {
        protocol_->SendIotStates(states);
    }
#endif
}

void Application::ClearWifiConfiguration()
{
    ESP_LOGI(TAG, "Clearing WiFi configuration from application");
    auto& board = Board::GetInstance();
    board.ClearWifiConfiguration();
    
    // After clearing WiFi, restart to enter BLE configuration mode
    ESP_LOGI(TAG, "WiFi cleared, restarting to enter BLE configuration mode");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void Application::Reboot()
{
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

void Application::WakeWordInvoke(const std::string &wake_word)
{
    if (device_state_ == kDeviceStateIdle)
    {
        ToggleChatState();
        Schedule([this, wake_word]()
                 {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            } });
    }
    else if (device_state_ == kDeviceStateSpeaking)
    {
        Schedule([this]()
                 { AbortSpeaking(kAbortReasonNone); });
    }
    else if (device_state_ == kDeviceStateListening)
    {
        Schedule([this]()
                 {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            } });
    }
}

bool Application::CanEnterSleepMode()
{
    if (device_state_ != kDeviceStateIdle)
    {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened())
    {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string &payload)
{
    Schedule([this, payload]()
             {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        } });
}

void Application::SetAecMode(AecMode mode)
{
    aec_mode_ = mode;
    Schedule([this]()
             {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_processor_->EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_processor_->EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_processor_->EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        auto* active_protocol = GetActiveProtocol();
        if (active_protocol && active_protocol->IsAudioChannelOpened()) {
            active_protocol->CloseAudioChannel();
        } });
}

Protocol* Application::GetActiveProtocol() {
    // Protocol selection for audio streaming:
    // - WebSocket: Used for all audio conversations (both user-initiated and server-initiated via ws_start)
    // - MQTT: Used as fallback if WebSocket is not available, or for control messages only
    // 
    // Both protocols can be "live" simultaneously:
    // - MQTT: Always connected (if configured) for control messages (ws_start, wake word, etc.)
    // - WebSocket: Opened on-demand for audio streaming, takes priority when available
    if (websocket_protocol_ && websocket_protocol_->IsAudioChannelOpened()) {
        return websocket_protocol_.get();
    }
    return protocol_.get();
}

void Application::OpenWebSocketConnection() {
    // If WebSocket protocol already exists and is opened, do nothing
    if (websocket_protocol_ && websocket_protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "WebSocket connection already open");
        return;
    }
    
    // Create WebSocket protocol if it doesn't exist
    if (!websocket_protocol_) {
        ESP_LOGI(TAG, "Creating WebSocket protocol instance");
        websocket_protocol_ = std::make_unique<WebsocketProtocol>();
        
        // Set up callbacks same as primary protocol
        websocket_protocol_->OnNetworkError([this](const std::string &message) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, message.c_str(), "sad", Lang::Sounds::P3_EXCLAMATION);
        });
        
        websocket_protocol_->OnIncomingAudio([this](AudioStreamPacket &&packet) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (device_state_ == kDeviceStateSpeaking && audio_decode_queue_.size() < MAX_AUDIO_PACKETS_IN_QUEUE) {
                audio_decode_queue_.emplace_back(std::move(packet));
            }
        });
        
        websocket_protocol_->OnAudioChannelOpened([this, codec = Board::GetInstance().GetAudioCodec(), &board = Board::GetInstance()]() {
            board.SetPowerSaveMode(false);
            if (websocket_protocol_->server_sample_rate() != codec->output_sample_rate()) {
                ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                         websocket_protocol_->server_sample_rate(), codec->output_sample_rate());
            }
        });
        
        websocket_protocol_->OnAudioChannelClosed([this, &board = Board::GetInstance()]() {
            board.SetPowerSaveMode(true);
            Schedule([this]() {
                is_alarm_mode_ = false; // Reset alarm mode when WebSocket closes
                SetDeviceState(kDeviceStateIdle);
            });
        });
        
        // Set up JSON handler - use the same handler as primary protocol
        auto display = Board::GetInstance().GetDisplay();
        websocket_protocol_->OnIncomingJson([this, display](const cJSON *root) {
            // Use the same JSON parsing logic as primary protocol
            auto type = cJSON_GetObjectItem(root, "type");
            if (!cJSON_IsString(type)) {
                return;
            }
            
            if (strcmp(type->valuestring, "tts") == 0) {
                auto state = cJSON_GetObjectItem(root, "state");
                if (strcmp(state->valuestring, "start") == 0) {
                    Schedule([this]() {
                        aborted_ = false;
                        if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                            // Save state before TTS to determine if we should resume listening after TTS
                            state_before_tts_ = device_state_;
                            
                            // If we're in LISTENING state, stop listening before TTS starts
                            // This ensures no microphone audio interference during TTS playback
                            if (device_state_ == kDeviceStateListening) {
                                auto* active_protocol = GetActiveProtocol();
                                if (active_protocol && active_protocol->IsAudioChannelOpened()) {
                                    ESP_LOGI(TAG, "TTS starting while listening (WebSocket) - stopping listening before TTS");
                                    active_protocol->SendStopListening();
                                    // Small delay to ensure server receives listen:stop before TTS starts
                                    vTaskDelay(pdMS_TO_TICKS(50));
                                }
                            }
                            
                            SetDeviceState(kDeviceStateSpeaking);
                        }
                    });
                } else if (strcmp(state->valuestring, "stop") == 0) {
                    Schedule([this]() {
                        ESP_LOGI(TAG, "TTS stop (WebSocket): state=%d, listening_mode=%d", device_state_, (int)listening_mode_);
                        background_task_->WaitForCompletion();
                        if (device_state_ == kDeviceStateSpeaking) {
                            // Check if remote wakeup scenario (WebSocket still open)
                            auto* active_protocol = GetActiveProtocol();
                            bool is_remote_wakeup = (websocket_protocol_ != nullptr && 
                                                    websocket_protocol_->IsAudioChannelOpened() &&
                                                    active_protocol && active_protocol->IsAudioChannelOpened());
                            
                            if (is_remote_wakeup) {
                                // Automatically resume listening after TTS in remote wakeup scenario
                                if (is_alarm_mode_) {
                                    // Alarm mode: Always start listening after TTS (even if wasn't listening before)
                                    // This is the expected flow: ws_start → TTS → listening
                                    ESP_LOGI(TAG, "TTS stopped (WebSocket), alarm mode - starting listening for user response");
                                    SetDeviceState(kDeviceStateListening);
                                    is_alarm_mode_ = false; // Reset alarm mode after first TTS completes
                                } else if (state_before_tts_ == kDeviceStateListening) {
                                    // Normal remote wakeup: Only resume if we were listening before TTS started
                                    ESP_LOGI(TAG, "TTS stopped (WebSocket), remote wakeup detected - automatically resuming listening");
                                    SetDeviceState(kDeviceStateListening);
                                } else {
                                    ESP_LOGI(TAG, "TTS stopped (WebSocket), remote wakeup detected but was not listening before TTS - going to idle");
                                    SetDeviceState(kDeviceStateIdle);
                                }
                                state_before_tts_ = kDeviceStateUnknown; // Reset
                            } else if (listening_mode_ == kListeningModeManualStop) {
                                // Go to idle for manual interactions
                                ESP_LOGI(TAG, "TTS stopped (WebSocket), going to idle (manual stop mode)");
                                SetDeviceState(kDeviceStateIdle);
                                state_before_tts_ = kDeviceStateUnknown; // Reset
                            } else {
                                // Auto mode: resume listening
                                ESP_LOGI(TAG, "TTS stopped (WebSocket), automatically resuming listening (auto stop mode)");
                                SetDeviceState(kDeviceStateListening);
                                state_before_tts_ = kDeviceStateUnknown; // Reset
                            }
                        }
                    });
                } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                    auto text = cJSON_GetObjectItem(root, "text");
                    if (cJSON_IsString(text)) {
                        ESP_LOGI(TAG, "<< %s", text->valuestring);
                    }
                }
            } else if (strcmp(type->valuestring, "stt") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, ">> %s", text->valuestring);
                }
            } else if (strcmp(type->valuestring, "llm") == 0) {
                auto emotion = cJSON_GetObjectItem(root, "emotion");
                if (cJSON_IsString(emotion)) {
                    Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                        display->SetEmotion(emotion_str.c_str());
                    });
                }
            } else if (strcmp(type->valuestring, "listen") == 0) {
                auto state = cJSON_GetObjectItem(root, "state");
                if (cJSON_IsString(state)) {
                    if (strcmp(state->valuestring, "start") == 0) {
                        ESP_LOGI(TAG, "Received listen:start from server (WebSocket), starting listening");
                        Schedule([this]() { 
                            ESP_LOGI(TAG, "Executing listen:start - setting listening mode (AutoStop)");
                            SetListeningMode(kListeningModeAutoStop); 
                        });
                    } else if (strcmp(state->valuestring, "stop") == 0) {
                        ESP_LOGI(TAG, "Received listen:stop from server (WebSocket), stopping listening");
                        Schedule([this]() { StopListening(); });
                    } else {
                        ESP_LOGW(TAG, "Received listen message with unknown state: %s", state->valuestring);
                    }
                } else {
                    ESP_LOGW(TAG, "Received listen message without valid state field");
                }
            }
            // Add other message type handlers as needed
        });
        
        // Start the WebSocket protocol
        if (!websocket_protocol_->Start()) {
            ESP_LOGE(TAG, "Failed to start WebSocket protocol");
            websocket_protocol_.reset();
            return;
        }
    }
    
    // Open the audio channel
    ESP_LOGI(TAG, "Opening WebSocket audio channel");
    if (!websocket_protocol_->OpenAudioChannel()) {
        ESP_LOGE(TAG, "Failed to open WebSocket audio channel");
        return;
    }
    
    ESP_LOGI(TAG, "WebSocket connection opened successfully");
    
    // For remote wakeup (ws_start): Behavior depends on alarm mode
    // - Normal mode: Automatically enter listening state (for normal conversations)
    // - Alarm mode: Skip listening, let TTS play first, then listening starts after TTS
    if (device_state_ == kDeviceStateIdle) {
        if (is_alarm_mode_) {
            // Alarm mode: Don't start listening automatically
            // Server will send TTS first, then listening will start after TTS stops
            ESP_LOGI(TAG, "Alarm mode: WebSocket opened via ws_start, skipping automatic listening (TTS will play first)");
        } else {
            // Normal mode: Automatically enter listening state
            ESP_LOGI(TAG, "Remote wakeup: WebSocket opened via ws_start, automatically entering listening state");
            // Small delay to ensure WebSocket connection is fully ready before starting listening
            // This prevents race conditions where connection might not be ready immediately
            vTaskDelay(pdMS_TO_TICKS(100));
            // Verify connection is still open before proceeding
            if (websocket_protocol_ && websocket_protocol_->IsAudioChannelOpened()) {
                // Use AutoStop for remote wake so TTS stop resumes listening automatically
                SetListeningMode(kListeningModeAutoStop);
                // SetListeningMode() calls SetDeviceState(kDeviceStateListening) which will:
                // - Turn on red light (via led->OnStateChanged())
                // - Send listen start message
                // - Start audio capture
            } else {
                ESP_LOGW(TAG, "WebSocket connection not ready after delay, cannot start listening automatically");
            }
        }
    }
    else if (device_state_ == kDeviceStateSpeaking) {
        // If speaking, abort speaking and enter listening mode (same as button press behavior)
        ESP_LOGI(TAG, "Remote wakeup: Device is speaking, aborting and entering listening state");
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeAutoStop);
        });
    }
    // If we're already in listening state (e.g., user pressed button before ws_start),
    // send the listen start message now that the connection is ready
    else if (device_state_ == kDeviceStateListening && !audio_processor_->IsRunning()) {
        ESP_LOGI(TAG, "Already in listening state, sending listen start to new WebSocket connection");
        websocket_protocol_->SendStartListening(listening_mode_);
        // Small delay to ensure server processes the message
        vTaskDelay(pdMS_TO_TICKS(50));
        // Start audio capture if not already running
        if (!audio_processor_->IsRunning()) {
            opus_encoder_->ResetState();
            auto codec = Board::GetInstance().GetAudioCodec();
            ESP_LOGI(TAG, "Starting audio capture: input_sample_rate=%d, Opus encoder=16000Hz mono, frame_duration=%dms", 
                     codec ? codec->input_sample_rate() : 0, OPUS_FRAME_DURATION_MS);
            audio_processor_->Start();
            wake_word_->StopDetection();
        }
    }
    else if (device_state_ == kDeviceStateConnecting) {
        // If we're in connecting state (e.g., connection just opened), enter listening mode
        // This handles the case where ws_start arrives while device is connecting
        ESP_LOGI(TAG, "Remote wakeup: Device is connecting, entering listening state");
        SetListeningMode(kListeningModeAutoStop);
    }
}
