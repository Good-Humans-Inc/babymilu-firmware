#include "mqtt_protocol.h"
#include "board.h"
#include "application.h"
#include "settings.h"
#include "config.h"
#include "ota.h"
#include "animation/animation_updater.h"
#include "ssid_manager.h"

#include <esp_log.h>
#include <esp_system.h>
#include <ml307_mqtt.h>
#include <ml307_udp.h>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <arpa/inet.h>
#include "assets/lang_config.h"

#define TAG "MQTT"

static bool IsValidWebSocketUrl(const std::string& url) {
    // Check for empty URL
    if (url.empty()) {
        return false;
    }
    
    // Check for localhost or loopback addresses
    if (url.find("127.0.0.1") != std::string::npos ||
        url.find("localhost") != std::string::npos ||
        url.find("::1") != std::string::npos ||
        url.find("0.0.0.0") != std::string::npos) {
        return false;
    }
    
    // Basic validation: should start with ws:// or wss://
    if (url.find("ws://") != 0 && url.find("wss://") != 0) {
        return false;
    }
    
    return true;
}

MqttProtocol::MqttProtocol() {
    event_group_handle_ = xEventGroupCreate();
    server_requested_websocket_ = false;
}

MqttProtocol::~MqttProtocol() {
    ESP_LOGI(TAG, "MqttProtocol deinit");
    if (udp_ != nullptr) {
        delete udp_;
    }
    if (mqtt_ != nullptr) {
        delete mqtt_;
    }
    vEventGroupDelete(event_group_handle_);
}

bool MqttProtocol::Start() {
    return StartMqttClient(false);
}

bool MqttProtocol::StartMqttClient(bool report_error) {
    // Use single global MQTT client - don't recreate if already connected
    if (mqtt_ != nullptr && mqtt_->IsConnected()) {
        ESP_LOGI(TAG, "MQTT client already connected, reusing existing connection");
        return true;
    }
    
    // Only delete and recreate if not connected or doesn't exist
    if (mqtt_ != nullptr) {
        ESP_LOGW(TAG, "MQTT client exists but not connected, cleaning up and recreating");
        delete mqtt_;
        mqtt_ = nullptr;
    }

    Settings settings("mqtt", false);
    auto endpoint = settings.GetString("endpoint");
    auto client_id = settings.GetString("client_id");
    auto username = settings.GetString("username");
    auto password = settings.GetString("password");
    // Use 60s keepalive as recommended (was 120s default)
    int keepalive_interval = settings.GetInt("keepalive", 60);
    publish_topic_ = settings.GetString("publish_topic");
    
    // Derive subscribe topic from publish topic (replace /up with /down)
    subscribe_topic_ = settings.GetString("subscribe_topic");
    // If subscribe_topic is empty or invalid (e.g., "null"), derive it from publish_topic
    if ((subscribe_topic_.empty() || subscribe_topic_ == "null") && !publish_topic_.empty()) {
        subscribe_topic_ = publish_topic_;
        size_t pos = subscribe_topic_.rfind("/up");
        if (pos != std::string::npos) {
            subscribe_topic_.replace(pos, 3, "/down");
        } else {
            // If no /up pattern, append /down
            subscribe_topic_ += "/down";
        }
        // Save the derived subscribe_topic to settings for future use
        Settings write_settings("mqtt", true);
        write_settings.SetString("subscribe_topic", subscribe_topic_);
        ESP_LOGI(TAG, "Derived and saved subscribe_topic: %s", subscribe_topic_.c_str());
    }

    // MQTT endpoint must be set by application.cc from menuconfig OTA URL or OTA server response
    // No fallback/default here - fail if not configured
    if (endpoint.empty()) {
        ESP_LOGE(TAG, "MQTT endpoint is not configured. It should be derived from menuconfig OTA URL or set by OTA server.");
        return false;
    }

    // Verbose diagnostics for MQTT configuration
    ESP_LOGI(TAG,
             "MQTT config: endpoint=%s, client_id=%s, username=%s, keepalive=%d, up_topic=%s, down_topic=%s",
             endpoint.c_str(),
             client_id.empty() ? "<empty>" : client_id.c_str(),
             username.empty() ? "<empty>" : "<set>",
             keepalive_interval,
             publish_topic_.empty() ? "<empty>" : publish_topic_.c_str(),
             subscribe_topic_.empty() ? "<empty>" : subscribe_topic_.c_str());

    mqtt_ = Board::GetInstance().CreateMqtt();
    mqtt_->SetKeepAlive(keepalive_interval);

    mqtt_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Disconnected from endpoint - will automatically reconnect");
        
        // Prevent double reconnection race condition
        if (reconnecting_) {
            ESP_LOGW(TAG, "Reconnection already in progress, skipping duplicate disconnect handler");
            return;
        }
        
        reconnecting_ = true;
        
        // Start continuous reconnection attempts with exponential backoff
        // This will keep retrying until the server comes back online
        AttemptReconnection();
    });

    mqtt_->OnConnected([this, client_id]() {
        ESP_LOGI(TAG, "MQTT client fully connected (CONNACK received)");
        // Reset reconnection backoff on successful connection
        reconnect_backoff_ms_ = 200;
        // Subscribe in the OnConnected callback to ensure we're fully connected
        if (!subscribe_topic_.empty()) {
            ESP_LOGI(TAG, "Subscribing to topic (from OnConnected): %s", subscribe_topic_.c_str());
            bool is_connected = mqtt_->IsConnected();
            ESP_LOGI(TAG, "Connection status before subscribe: %s", is_connected ? "connected" : "not connected");
            
            if (!mqtt_->Subscribe(subscribe_topic_)) {
                ESP_LOGE(TAG, "Failed to subscribe to topic (from OnConnected): %s", subscribe_topic_.c_str());
                bool still_connected = mqtt_->IsConnected();
                ESP_LOGE(TAG, "Connection status after failed subscribe: %s", still_connected ? "connected" : "disconnected");
                
                // Diagnostic: Try to publish to see if it's ACL-specific to SUBSCRIBE
                if (!publish_topic_.empty()) {
                    ESP_LOGI(TAG, "Testing publish capability to diagnose ACL issue...");
                    std::string test_msg = "{\"type\":\"test\",\"diagnostic\":\"subscribe_failed\"}";
                    if (mqtt_->Publish(publish_topic_, test_msg)) {
                        ESP_LOGI(TAG, "Publish succeeded - connection is working, likely ACL denies SUBSCRIBE");
                        ESP_LOGE(TAG, "DIAGNOSIS: Broker ACL likely denies SUBSCRIBE for client_id=%s to topic=%s", 
                                 client_id.empty() ? "<empty>" : client_id.c_str(), subscribe_topic_.c_str());
                    } else {
                        ESP_LOGE(TAG, "Publish also failed - connection may be broken or ACL denies both PUBLISH and SUBSCRIBE");
                    }
                } else {
                    ESP_LOGE(TAG, "DIAGNOSIS: Subscribe failed, but cannot test publish (publish_topic empty). Likely ACL denies SUBSCRIBE for client_id=%s to topic=%s",
                             client_id.empty() ? "<empty>" : client_id.c_str(), subscribe_topic_.c_str());
                }
            } else {
                ESP_LOGI(TAG, "Successfully subscribed to topic (from OnConnected): %s", subscribe_topic_.c_str());
            }
        }
    });

    mqtt_->OnMessage([this](const std::string& topic, const std::string& payload) {
        // Log every inbound MQTT message (topic, size, and a short prefix of the payload)
        const int kPreviewLen = 200;  // Increased to see more of the message
        ESP_LOGI(TAG, "MQTT RX topic=%s len=%u prefix=%.*s",
                 topic.c_str(),
                 (unsigned)payload.size(),
                 (int)std::min((int)payload.size(), kPreviewLen),
                 payload.c_str());
        
        // Check if this is a listen message for immediate visibility
        if (payload.find("\"type\":\"listen\"") != std::string::npos || 
            payload.find("type\":\"listen\"") != std::string::npos) {
            ESP_LOGI(TAG, "*** LISTEN MESSAGE DETECTED IN MQTT PAYLOAD ***");
        }
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root == nullptr) {
            ESP_LOGE(TAG, "Failed to parse json message %s", payload.c_str());
            return;
        }
        cJSON* type = cJSON_GetObjectItem(root, "type");
        if (!cJSON_IsString(type)) {
            ESP_LOGE(TAG, "Message type is invalid");
            cJSON_Delete(root);
            return;
        }

        ESP_LOGI(TAG, "Processing message type: %s", type->valuestring);
        if (strcmp(type->valuestring, "hello") == 0) {
            ESP_LOGI(TAG, "Received server hello message");
            ParseServerHello(root);
        } else if (strcmp(type->valuestring, "rtc_alarm") == 0) {
            auto epoch = cJSON_GetObjectItem(root, "epoch");
            if (!cJSON_IsNumber(epoch)) {
                epoch = cJSON_GetObjectItem(root, "triggerAtEpoch");
            }
            if (!cJSON_IsNumber(epoch)) {
                ESP_LOGW(TAG, "rtc_alarm ignored: missing numeric epoch/triggerAtEpoch");
            } else {
                auto custom_mode_item = cJSON_GetObjectItem(root, "custom_mode");
                if (custom_mode_item == nullptr) {
                    custom_mode_item = cJSON_GetObjectItem(root, "customMode");
                }
                bool custom_mode = cJSON_IsTrue(custom_mode_item);

                auto replay_item = cJSON_GetObjectItem(root, "replay_if_no_mic");
                if (replay_item == nullptr) {
                    replay_item = cJSON_GetObjectItem(root, "replayIfNoMic");
                }
                bool replay_if_no_mic = replay_item == nullptr || !cJSON_IsFalse(replay_item);

                auto priority_item = cJSON_GetObjectItem(root, "priority");
                int priority = cJSON_IsNumber(priority_item) ? priority_item->valueint : 0;

                int fallback_delay_ms = -1;
                auto delay_ms_item = cJSON_GetObjectItem(root, "delay_ms");
                if (delay_ms_item == nullptr) {
                    delay_ms_item = cJSON_GetObjectItem(root, "delayMs");
                }
                if (cJSON_IsNumber(delay_ms_item)) {
                    fallback_delay_ms = std::max(1000, delay_ms_item->valueint);
                } else {
                    auto delay_seconds_item = cJSON_GetObjectItem(root, "delay_seconds");
                    if (delay_seconds_item == nullptr) {
                        delay_seconds_item = cJSON_GetObjectItem(root, "delaySeconds");
                    }
                    if (cJSON_IsNumber(delay_seconds_item)) {
                        fallback_delay_ms = std::max(1, delay_seconds_item->valueint) * 1000;
                    }
                }

                bool software_fallback_enabled = true;
                auto software_fallback_item = cJSON_GetObjectItem(root, "software_fallback");
                if (software_fallback_item == nullptr) {
                    software_fallback_item = cJSON_GetObjectItem(root, "softwareFallback");
                }
                if (software_fallback_item == nullptr) {
                    software_fallback_item = cJSON_GetObjectItem(root, "fallback");
                }
                if (cJSON_IsFalse(software_fallback_item)) {
                    software_fallback_enabled = false;
                } else if (cJSON_IsTrue(software_fallback_item)) {
                    software_fallback_enabled = true;
                }
                auto rtc_only_item = cJSON_GetObjectItem(root, "rtc_only");
                if (rtc_only_item == nullptr) {
                    rtc_only_item = cJSON_GetObjectItem(root, "rtcOnly");
                }
                if (cJSON_IsTrue(rtc_only_item)) {
                    software_fallback_enabled = false;
                }

                const char* wav_url = "";
                const char* url_keys[] = {"offline_wav_url", "offlineWavUrl", "wav_url", "wavUrl", "audioUrl", "url"};
                for (const char* key : url_keys) {
                    auto item = cJSON_GetObjectItem(root, key);
                    if (cJSON_IsString(item) && item->valuestring != nullptr && strlen(item->valuestring) > 0) {
                        wav_url = item->valuestring;
                        break;
                    }
                }

                const char* reminder_id = "";
                auto reminder_item = cJSON_GetObjectItem(root, "reminder_id");
                if (reminder_item == nullptr) {
                    reminder_item = cJSON_GetObjectItem(root, "reminderId");
                }
                if (cJSON_IsString(reminder_item) && reminder_item->valuestring != nullptr) {
                    reminder_id = reminder_item->valuestring;
                }

                const char* websocket_url = "";
                const char* websocket_url_keys[] = {"wss", "wsUrl", "ws_url", "websocketUrl", "websocket_url"};
                for (const char* key : websocket_url_keys) {
                    auto item = cJSON_GetObjectItem(root, key);
                    if (cJSON_IsString(item) && item->valuestring != nullptr && strlen(item->valuestring) > 0) {
                        websocket_url = item->valuestring;
                        break;
                    }
                }
                auto websocket_version = cJSON_GetObjectItem(root, "version");
                if (websocket_url[0] != '\0') {
                    std::string url(websocket_url);
                    if (IsValidWebSocketUrl(url)) {
                        Settings ws_settings("websocket", true);
                        ws_settings.SetString("url", url);
                        if (cJSON_IsNumber(websocket_version)) {
                            ws_settings.SetInt("version", websocket_version->valueint);
                            ESP_LOGI(TAG, "rtc_alarm saved WebSocket version: %d", websocket_version->valueint);
                        }
                        ESP_LOGI(TAG, "rtc_alarm saved WebSocket URL for self-start: %s", url.c_str());
                    } else {
                        ESP_LOGW(TAG, "rtc_alarm ignored invalid WebSocket URL: %s", url.c_str());
                    }
                }

                bool ok = Application::GetInstance().ArmRtcReminder(
                    static_cast<time_t>(epoch->valuedouble),
                    custom_mode,
                    wav_url,
                    priority,
                    reminder_id,
                    replay_if_no_mic,
                    fallback_delay_ms,
                    software_fallback_enabled);
                ESP_LOGI(TAG, "rtc_alarm processed: ok=%d epoch=%ld custom=%d sw_fb=%d url=%s",
                         ok ? 1 : 0,
                         static_cast<long>(epoch->valuedouble),
                         custom_mode ? 1 : 0,
                         software_fallback_enabled ? 1 : 0,
                         wav_url && wav_url[0] ? wav_url : "<none>");
            }
        } else if (strcmp(type->valuestring, "ws_start") == 0) {
            // Server is redirecting to WebSocket
            // ws_start indicates alarm mode (server-initiated conversation)
            server_requested_websocket_ = true;
            auto wss_url = cJSON_GetObjectItem(root, "wss");
            auto version = cJSON_GetObjectItem(root, "version");
            
            if (cJSON_IsString(wss_url)) {
                std::string url = wss_url->valuestring;
                ESP_LOGI(TAG, "Server requests WebSocket connection (alarm mode): %s", url.c_str());
                
                // Set alarm mode flag - in alarm mode, TTS plays first, then listening starts
                Application::GetInstance().SetAlarmMode(true);
                
                // Validate URL before saving
                if (!IsValidWebSocketUrl(url)) {
                    ESP_LOGW(TAG, "Invalid WebSocket URL received (localhost/invalid): %s, will use default URL instead", url.c_str());
                    // Don't save invalid URL, let WebSocket protocol use default
                } else {
                    // Save WebSocket URL and version to settings
                    Settings ws_settings("websocket", true);
                    ws_settings.SetString("url", url);
                    if (cJSON_IsNumber(version)) {
                        ws_settings.SetInt("version", version->valueint);
                        ESP_LOGI(TAG, "WebSocket version: %d", version->valueint);
                    }
                    ESP_LOGI(TAG, "WebSocket URL saved. Opening WebSocket connection for conversation...");
                }
                
                // Schedule opening WebSocket connection in the application context
                // (will use default URL if invalid URL was received)
                // Always open WebSocket connection when ws_start is received, even if one exists
                // This ensures a fresh session and proper callback setup for each new conversation
                Application::GetInstance().Schedule([this]() {
                    auto& app = Application::GetInstance();
                    // Always open WebSocket connection - if one exists, it will be closed and recreated
                    // This ensures clean state and proper callback setup for each new conversation
                    ESP_LOGI(TAG, "Opening WebSocket connection for ws_start (alarm mode - TTS first, then listening)");
                    app.OpenWebSocketConnection();
                });
            } else {
                ESP_LOGE(TAG, "ws_start message missing 'wss' field");
            }
            // Don't pass ws_start to on_incoming_json_ as it's a protocol-level message
        } else if (strcmp(type->valuestring, "goodbye") == 0) {
            auto session_id = cJSON_GetObjectItem(root, "session_id");
            ESP_LOGI(TAG, "Received goodbye message, session_id: %s", session_id ? session_id->valuestring : "null");
            if (session_id == nullptr || session_id_ == session_id->valuestring) {
                Application::GetInstance().Schedule([this]() {
                    CloseAudioChannel();
                });
            }
            // Don't forward goodbye to on_incoming_json_ as it's a protocol-level message
        } else if (strcmp(type->valuestring, "remote_anim_update") == 0) {
            // Remote animation update request - trigger animation updater's update loop
            ESP_LOGI(TAG, "Received remote_anim_update message, triggering animation update loop");
            Application::GetInstance().Schedule([]() {
                auto& anim_updater = AnimationUpdater::GetInstance();
                ESP_LOGI(TAG, "Calling AnimationUpdater::TriggerUpdateLoop()");
                anim_updater.TriggerUpdateLoop();
            });
            // Don't forward remote_anim_update to on_incoming_json_ as it's a protocol-level message
        } else if (strcmp(type->valuestring, "wifi_reconfig_nimble") == 0) {
            // Remote WiFi reconfiguration request:
            // enter NimBLE WiFi setup mode without clearing existing credentials.
            ESP_LOGI(TAG, "Received wifi_reconfig_nimble message, entering BLE WiFi config mode");
            Application::GetInstance().Schedule([]() {
                Board::GetInstance().EnterBleWifiConfigMode();
            });
            // Don't forward wifi_reconfig_nimble to on_incoming_json_ as it's a protocol-level message
        } else if (strcmp(type->valuestring, "wifi_clear_credential") == 0) {
            // Clear all persisted WiFi credentials, then reboot into BLE onboarding mode.
            ESP_LOGI(TAG, "Received wifi_clear_credential message, clearing saved WiFi credentials");
            Application::GetInstance().Schedule([]() {
                auto& board = Board::GetInstance();
                board.ClearWifiConfiguration();
                board.EnterBleWifiConfigMode();
            });
            // Don't forward wifi_clear_credential to on_incoming_json_ as it's a protocol-level message
        } else if (strcmp(type->valuestring, "switch_wifi_to") == 0) {
            auto message = cJSON_GetObjectItem(root, "message");
            if (!cJSON_IsString(message) || message->valuestring == nullptr || strlen(message->valuestring) == 0) {
                ESP_LOGW(TAG, "switch_wifi_to ignored: missing or invalid message field");
            } else {
                std::string target_ssid = message->valuestring;
                auto& ssid_manager = SsidManager::GetInstance();
                bool matched = false;
                for (const auto& item : ssid_manager.GetSsidList()) {
                    if (item.ssid == target_ssid) {
                        matched = true;
                        break;
                    }
                }

                if (!matched) {
                    ESP_LOGI(TAG, "switch_wifi_to ignored: target SSID '%s' not found in saved credentials", target_ssid.c_str());
                } else {
                    ESP_LOGI(TAG, "switch_wifi_to: scheduling one-shot preferred SSID '%s' for next reboot", target_ssid.c_str());
                    Settings wifi_settings("wifi", true);
                    wifi_settings.SetString("nxt_boot_ssid", target_ssid);
                    Application::GetInstance().Schedule([]() {
                        esp_restart();
                    });
                }
            }
            // Don't forward switch_wifi_to to on_incoming_json_ as it's a protocol-level message
        } else if (strcmp(type->valuestring, "set_ota_url") == 0) {
            auto message = cJSON_GetObjectItem(root, "message");
            if (!cJSON_IsString(message) || message->valuestring == nullptr || strlen(message->valuestring) == 0) {
                ESP_LOGW(TAG, "set_ota_url ignored: missing or invalid message field");
            } else {
                std::string custom_ota_url = message->valuestring;
                ESP_LOGI(TAG, "set_ota_url: saving custom OTA URL for next boot: %s", custom_ota_url.c_str());
                Settings ota_settings("ota", true);
                ota_settings.SetString("cus_ota_url", custom_ota_url);
                ota_settings.EraseKey("ota_retry");
                Application::GetInstance().Schedule([]() {
                    esp_restart();
                });
            }
            // Don't forward set_ota_url to on_incoming_json_ as it's a protocol-level message
        } else {
            // Forward all other message types (including "listen", "tts", "stt", etc.) to Application handler
            ESP_LOGI(TAG, "Forwarding MQTT message type '%s' to Application::OnIncomingJson", type->valuestring);
            if (on_incoming_json_ != nullptr) {
                on_incoming_json_(root);
            } else {
                ESP_LOGW(TAG, "on_incoming_json_ callback is null, cannot forward message type '%s'", type->valuestring);
            }
        }
        cJSON_Delete(root);
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    ESP_LOGI(TAG, "Connecting to endpoint %s", endpoint.c_str());
    std::string broker_address;
    int broker_port = 1883;  // Default to plain TCP port (1883), not TLS port (8883)
    bool use_tls = false;
    
    // Check endpoint scheme to determine transport type
    // mqtt:// = plain TCP, mqtts:// = TLS/SSL
    std::string endpoint_clean = endpoint;
    if (endpoint_clean.find("mqtts://") == 0) {
        use_tls = true;
        endpoint_clean = endpoint_clean.substr(8); // Remove "mqtts://" prefix
        broker_port = 8883;  // Default TLS port
    } else if (endpoint_clean.find("mqtt://") == 0) {
        use_tls = false;
        endpoint_clean = endpoint_clean.substr(7); // Remove "mqtt://" prefix
        broker_port = 1883;  // Default plain TCP port
    }
    
    size_t pos = endpoint_clean.find(':');
    if (pos != std::string::npos) {
        broker_address = endpoint_clean.substr(0, pos);
        broker_port = std::stoi(endpoint_clean.substr(pos + 1));
    } else {
        broker_address = endpoint_clean;
    }
    ESP_LOGI(TAG, "Broker parsed: %s:%d (transport: %s)", broker_address.c_str(), broker_port, use_tls ? "TLS" : "TCP");
    if (!mqtt_->Connect(broker_address, broker_port, client_id, username, password)) {
        ESP_LOGE(TAG, "Failed to connect to endpoint %s:%d", broker_address.c_str(), broker_port);
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    ESP_LOGI(TAG, "Connected to endpoint %s:%d", broker_address.c_str(), broker_port);
    
    // Check connection status
    bool is_connected = mqtt_->IsConnected();
    ESP_LOGI(TAG, "MQTT connection status after Connect(): %s", is_connected ? "connected" : "not connected");
    
    // Try immediate subscription (may fail if CONNACK not received yet)
    // OnConnected callback will also try to subscribe as a fallback
    if (!subscribe_topic_.empty()) {
        ESP_LOGI(TAG, "Attempting initial subscription to topic: %s", subscribe_topic_.c_str());
        if (!is_connected) {
            ESP_LOGW(TAG, "MQTT not fully connected yet, subscribe may fail - will retry in OnConnected callback");
        }
        if (!mqtt_->Subscribe(subscribe_topic_)) {
            ESP_LOGW(TAG, "Initial subscribe failed (may retry in OnConnected): %s", subscribe_topic_.c_str());
            ESP_LOGW(TAG, "Current connection status: %s", mqtt_->IsConnected() ? "connected" : "not connected");
            // Don't fail completely - OnConnected callback will retry and provide better diagnostics
        } else {
            ESP_LOGI(TAG, "Successfully subscribed to topic (initial): %s", subscribe_topic_.c_str());
        }
    } else {
        ESP_LOGW(TAG, "Subscribe topic is empty, cannot subscribe to receive messages");
    }
    
    return true;
}

void MqttProtocol::AttemptReconnection() {
    // Check if already connected (another thread might have connected)
    if (mqtt_ != nullptr && mqtt_->IsConnected()) {
        ESP_LOGI(TAG, "MQTT already connected, stopping reconnection attempts");
        reconnecting_ = false;
        reconnect_backoff_ms_ = 200;  // Reset backoff for next time
        return;
    }
    
    // Get current backoff delay
    int backoff_ms = reconnect_backoff_ms_;
    
    // Schedule reconnection attempt on application thread with backoff
    Application::GetInstance().Schedule([this, backoff_ms]() {
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));  // Backoff delay
        
        // Check again if connected (might have been connected by another thread)
        if (mqtt_ != nullptr && mqtt_->IsConnected()) {
            ESP_LOGI(TAG, "MQTT connected during backoff, stopping reconnection");
            reconnecting_ = false;
            reconnect_backoff_ms_ = 200;  // Reset backoff
            return;
        }
        
        ESP_LOGI(TAG, "Attempting MQTT reconnection (backoff: %dms)", backoff_ms);
        
        if (!StartMqttClient(false)) {  // Don't report error on reconnect
            // Reconnection failed - increase backoff exponentially (200ms -> 500ms -> 1s -> 2s -> 5s -> 10s max)
            if (reconnect_backoff_ms_ < 500) {
                reconnect_backoff_ms_ = std::min(reconnect_backoff_ms_ + 100, 500);
            } else if (reconnect_backoff_ms_ < 1000) {
                reconnect_backoff_ms_ = 1000;
            } else if (reconnect_backoff_ms_ < 2000) {
                reconnect_backoff_ms_ = 2000;
            } else if (reconnect_backoff_ms_ < 5000) {
                reconnect_backoff_ms_ = 5000;
            } else {
                reconnect_backoff_ms_ = 10000;  // Cap at 10 seconds
            }
            
            ESP_LOGW(TAG, "MQTT reconnection failed, will retry in %dms (server may be restarting)", reconnect_backoff_ms_);
            
            // Schedule next retry attempt (continuous retry loop)
            AttemptReconnection();
        } else {
            // Reconnection succeeded!
            ESP_LOGI(TAG, "MQTT reconnection successful after server restart");
            reconnecting_ = false;
            reconnect_backoff_ms_ = 200;  // Reset backoff to initial value for next time
        }
    });
}

bool MqttProtocol::SendText(const std::string& text) {
    if (publish_topic_.empty()) {
        return false;
    }
    if (!mqtt_->Publish(publish_topic_, text)) {
        ESP_LOGE(TAG, "Failed to publish message: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }
    return true;
}

bool MqttProtocol::SendAudio(const AudioStreamPacket& packet) {
    std::lock_guard<std::mutex> lock(channel_mutex_);
    if (udp_ == nullptr) {
        return false;
    }

    std::string nonce(aes_nonce_);
    *(uint16_t*)&nonce[2] = htons(packet.payload.size());
    *(uint32_t*)&nonce[8] = htonl(packet.timestamp);
    *(uint32_t*)&nonce[12] = htonl(++local_sequence_);

    std::string encrypted;
    encrypted.resize(aes_nonce_.size() + packet.payload.size());
    memcpy(encrypted.data(), nonce.data(), nonce.size());

    size_t nc_off = 0;
    uint8_t stream_block[16] = {0};
    if (mbedtls_aes_crypt_ctr(&aes_ctx_, packet.payload.size(), &nc_off, (uint8_t*)nonce.c_str(), stream_block,
        (uint8_t*)packet.payload.data(), (uint8_t*)&encrypted[nonce.size()]) != 0) {
        ESP_LOGE(TAG, "Failed to encrypt audio data");
        return false;
    }

    return udp_->Send(encrypted) > 0;
}

void MqttProtocol::CloseAudioChannel() {
    {
        std::lock_guard<std::mutex> lock(channel_mutex_);
        if (udp_ != nullptr) {
            delete udp_;
            udp_ = nullptr;
        }
    }

    std::string message = "{";
    message += "\"session_id\":\"" + session_id_ + "\",";
    message += "\"type\":\"goodbye\"";
    message += "}";
    SendText(message);

    if (on_audio_channel_closed_ != nullptr) {
        on_audio_channel_closed_();
    }
}

bool MqttProtocol::OpenAudioChannel() {
    if (mqtt_ == nullptr || !mqtt_->IsConnected()) {
        ESP_LOGI(TAG, "MQTT is not connected, try to connect now");
        if (!StartMqttClient(true)) {
            return false;
        }
    }

    error_occurred_ = false;
    session_id_ = "";
    server_requested_websocket_ = false;
    xEventGroupClearBits(event_group_handle_, MQTT_PROTOCOL_SERVER_HELLO_EVENT);

    auto message = GetHelloMessage();
    ESP_LOGI(TAG, "Sending hello message to topic: %s", publish_topic_.c_str());
    if (!SendText(message)) {
        return false;
    }

    // 等待服务器响应
    ESP_LOGI(TAG, "Waiting for server hello response...");
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, MQTT_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & MQTT_PROTOCOL_SERVER_HELLO_EVENT)) {
        if (server_requested_websocket_) {
            // Server requested WebSocket - this is expected for modern servers
            // Don't treat it as an error, just return false so caller can use WebSocket
            ESP_LOGI(TAG, "Server requested WebSocket instead of MQTT audio channel. This is normal - WebSocket will be used for audio streaming.");
            // The ws_start handler already saved the WebSocket URL and will trigger OpenWebSocketConnection()
            // Return false so the application can fall back to WebSocket
            return false;
        } else {
            ESP_LOGE(TAG, "Failed to receive server hello");
            SetError(Lang::Strings::SERVER_TIMEOUT);
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(channel_mutex_);
    if (udp_ != nullptr) {
        delete udp_;
    }
    udp_ = Board::GetInstance().CreateUdp();
    udp_->OnMessage([this](const std::string& data) {
        /*
         * UDP Encrypted OPUS Packet Format:
         * |type 1u|flags 1u|payload_len 2u|ssrc 4u|timestamp 4u|sequence 4u|
         * |payload payload_len|
         */
        if (data.size() < sizeof(aes_nonce_)) {
            ESP_LOGE(TAG, "Invalid audio packet size: %u", data.size());
            return;
        }
        if (data[0] != 0x01) {
            ESP_LOGE(TAG, "Invalid audio packet type: %x", data[0]);
            return;
        }
        uint32_t timestamp = ntohl(*(uint32_t*)&data[8]);
        uint32_t sequence = ntohl(*(uint32_t*)&data[12]);
        if (sequence < remote_sequence_) {
            ESP_LOGW(TAG, "Received audio packet with old sequence: %lu, expected: %lu", sequence, remote_sequence_);
            return;
        }
        if (sequence != remote_sequence_ + 1) {
            ESP_LOGW(TAG, "Received audio packet with wrong sequence: %lu, expected: %lu", sequence, remote_sequence_ + 1);
        }

        size_t decrypted_size = data.size() - aes_nonce_.size();
        size_t nc_off = 0;
        uint8_t stream_block[16] = {0};
        auto nonce = (uint8_t*)data.data();
        auto encrypted = (uint8_t*)data.data() + aes_nonce_.size();
        AudioStreamPacket packet;
        packet.sample_rate = server_sample_rate_;
        packet.frame_duration = server_frame_duration_;
        packet.timestamp = timestamp;
        packet.payload.resize(decrypted_size);
        int ret = mbedtls_aes_crypt_ctr(&aes_ctx_, decrypted_size, &nc_off, nonce, stream_block, encrypted, (uint8_t*)packet.payload.data());
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to decrypt audio data, ret: %d", ret);
            return;
        }
        if (on_incoming_audio_ != nullptr) {
            on_incoming_audio_(std::move(packet));
        }
        remote_sequence_ = sequence;
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    udp_->Connect(udp_server_, udp_port_);

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }
    return true;
}

std::string MqttProtocol::GetHelloMessage() {
    // 发送 hello 消息申请 UDP 通道
    // Get the device's actual output sample rate to avoid resampling
    auto codec = Board::GetInstance().GetAudioCodec();
    int requested_sample_rate = codec ? codec->output_sample_rate() : 16000;
    // Fallback to 16000 if codec not available or if it's 0
    if (requested_sample_rate == 0) {
        requested_sample_rate = 16000;
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", 3);
    cJSON_AddStringToObject(root, "transport", "udp");
    cJSON* features = cJSON_CreateObject();
#if CONFIG_USE_SERVER_AEC
    cJSON_AddBoolToObject(features, "aec", true);
#endif
#if CONFIG_IOT_PROTOCOL_MCP
    cJSON_AddBoolToObject(features, "mcp", true);
#endif
    cJSON_AddItemToObject(root, "features", features);
    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", requested_sample_rate);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", OPUS_FRAME_DURATION_MS);
    cJSON_AddItemToObject(root, "audio_params", audio_params);
    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return message;
}

void MqttProtocol::ParseServerHello(const cJSON* root) {
    auto transport = cJSON_GetObjectItem(root, "transport");
    if (transport == nullptr || strcmp(transport->valuestring, "udp") != 0) {
        ESP_LOGE(TAG, "Unsupported transport: %s", transport->valuestring);
        return;
    }

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        session_id_ = session_id->valuestring;
        ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());
    }

    // Get sample rate from hello message
    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            server_sample_rate_ = sample_rate->valueint;
        }
        auto frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            server_frame_duration_ = frame_duration->valueint;
        }
    }

    auto udp = cJSON_GetObjectItem(root, "udp");
    if (!cJSON_IsObject(udp)) {
        ESP_LOGE(TAG, "UDP is not specified");
        return;
    }
    udp_server_ = cJSON_GetObjectItem(udp, "server")->valuestring;
    udp_port_ = cJSON_GetObjectItem(udp, "port")->valueint;
    auto key = cJSON_GetObjectItem(udp, "key")->valuestring;
    auto nonce = cJSON_GetObjectItem(udp, "nonce")->valuestring;

    // auto encryption = cJSON_GetObjectItem(udp, "encryption")->valuestring;
    // ESP_LOGI(TAG, "UDP server: %s, port: %d, encryption: %s", udp_server_.c_str(), udp_port_, encryption);
    aes_nonce_ = DecodeHexString(nonce);
    mbedtls_aes_init(&aes_ctx_);
    mbedtls_aes_setkey_enc(&aes_ctx_, (const unsigned char*)DecodeHexString(key).c_str(), 128);
    local_sequence_ = 0;
    remote_sequence_ = 0;
    xEventGroupSetBits(event_group_handle_, MQTT_PROTOCOL_SERVER_HELLO_EVENT);
}

static const char hex_chars[] = "0123456789ABCDEF";
// 辅助函数，将单个十六进制字符转换为对应的数值
static inline uint8_t CharToHex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;  // 对于无效输入，返回0
}

std::string MqttProtocol::DecodeHexString(const std::string& hex_string) {
    std::string decoded;
    decoded.reserve(hex_string.size() / 2);
    for (size_t i = 0; i < hex_string.size(); i += 2) {
        char byte = (CharToHex(hex_string[i]) << 4) | CharToHex(hex_string[i + 1]);
        decoded.push_back(byte);
    }
    return decoded;
}

bool MqttProtocol::IsAudioChannelOpened() const {
    return udp_ != nullptr && !error_occurred_ && !IsTimeout();
}
