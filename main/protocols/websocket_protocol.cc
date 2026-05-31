#include "websocket_protocol.h"
#include "board.h"
#include "system_info.h"
#include "application.h"
#include "settings.h"
#include "config.h"
#include "ota.h"

#include <cstring>
#include <cJSON.h>
#include <esp_log.h>
#include <arpa/inet.h>
#include "assets/lang_config.h"

#define TAG "WS"

WebsocketProtocol::WebsocketProtocol() {
    event_group_handle_ = xEventGroupCreate();
}

WebsocketProtocol::~WebsocketProtocol() {
    if (websocket_ != nullptr) {
        delete websocket_;
    }
    vEventGroupDelete(event_group_handle_);
}

bool WebsocketProtocol::Start() {
    // Only connect to server when audio channel is needed
    return true;
}

bool WebsocketProtocol::SendAudio(const AudioStreamPacket& packet) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        ESP_LOGW(TAG, "Cannot send audio: websocket is null or not connected");
        return false;
    }

    if (version_ == 2) {
        std::string serialized;
        serialized.resize(sizeof(BinaryProtocol2) + packet.payload.size());
        auto bp2 = (BinaryProtocol2*)serialized.data();
        bp2->version = htons(version_);
        bp2->type = 0;
        bp2->reserved = 0;
        bp2->timestamp = htonl(packet.timestamp);
        bp2->payload_size = htonl(packet.payload.size());
        memcpy(bp2->payload, packet.payload.data(), packet.payload.size());

        return websocket_->Send(serialized.data(), serialized.size(), true);
    } else if (version_ == 3) {
        // Version 3: Send raw Opus frames as binary messages (no wrapper)
        // This matches the server's expectation for version 3
        frame_count_++;
        if (frame_count_ % 50 == 0 || frame_count_ <= 5) {
            // Log every 50th frame or first 5 frames for debugging
            ESP_LOGI(TAG, "Sending Opus frame %d, seq=%u, captured_ms=%u, dropped=%u, bytes=%zu",
                     frame_count_,
                     (unsigned)packet.sequence,
                     (unsigned)packet.capture_timestamp_ms,
                     (unsigned)packet.dropped_frames,
                     (size_t)packet.payload.size());
        } else {
            ESP_LOGD(TAG, "Sending Opus frame %d, seq=%u, bytes=%zu",
                     frame_count_, (unsigned)packet.sequence, (size_t)packet.payload.size());
        }
        bool ok = websocket_->Send(packet.payload.data(), packet.payload.size(), true);
        if (ok) {
            MaybeSendAudioMetadata(packet);
        }
        return ok;
    } else {
        return websocket_->Send(packet.payload.data(), packet.payload.size(), true);
    }
}

void WebsocketProtocol::MaybeSendAudioMetadata(const AudioStreamPacket& packet) {
    static constexpr int kAudioMetadataFramesPerChunk = 4;
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        return;
    }
    if (audio_metadata_frame_count_ == 0) {
        audio_metadata_start_sequence_ = packet.sequence;
        audio_metadata_start_timestamp_ms_ = packet.capture_timestamp_ms;
    }
    audio_metadata_frame_count_++;
    if (audio_metadata_frame_count_ < kAudioMetadataFramesPerChunk) {
        return;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
    cJSON_AddStringToObject(root, "type", "audio_meta");
    cJSON_AddStringToObject(root, "state", "chunk");
    cJSON_AddNumberToObject(root, "first_sequence", audio_metadata_start_sequence_);
    cJSON_AddNumberToObject(root, "last_sequence", packet.sequence);
    cJSON_AddNumberToObject(root, "first_timestamp_ms", audio_metadata_start_timestamp_ms_);
    cJSON_AddNumberToObject(root, "last_timestamp_ms", packet.capture_timestamp_ms);
    cJSON_AddNumberToObject(root, "frames", audio_metadata_frame_count_);
    cJSON_AddNumberToObject(root, "sample_rate", packet.sample_rate);
    cJSON_AddNumberToObject(root, "frame_duration", packet.frame_duration);
    cJSON_AddNumberToObject(root, "dropped_frames", packet.dropped_frames);
    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str != nullptr) {
        if (!websocket_->Send(json_str)) {
            ESP_LOGW(TAG, "Failed to send audio metadata chunk");
        }
        cJSON_free(json_str);
    }
    cJSON_Delete(root);
    audio_metadata_frame_count_ = 0;
}

bool WebsocketProtocol::SendText(const std::string& text) {
    if (websocket_ == nullptr) {
        ESP_LOGW(TAG, "SendText: WebSocket is null, cannot send");
        return false;
    }

    // Check if this is an MCP message for more detailed logging
    if (text.find("\"type\":\"mcp\"") != std::string::npos) {
        ESP_LOGI(TAG, "SendText: Sending MCP message, size=%zu bytes", text.length());
        if (text.length() < 500) {
            ESP_LOGI(TAG, "SendText: MCP message content: %s", text.c_str());
        } else {
            ESP_LOGI(TAG, "SendText: MCP message preview (first 200 chars): %.200s...", text.c_str());
        }
    }

    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "Failed to send text: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }

    if (text.find("\"type\":\"mcp\"") != std::string::npos) {
        ESP_LOGI(TAG, "SendText: MCP message sent successfully via WebSocket");
    }
    return true;
}

bool WebsocketProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

void WebsocketProtocol::CloseAudioChannel() {
    if (websocket_ != nullptr) {
        delete websocket_;
        websocket_ = nullptr;
    }
}

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

static bool UrlHasQueryParam(const std::string& url, const std::string& key) {
    size_t query_start = url.find('?');
    if (query_start == std::string::npos) {
        return false;
    }
    size_t pos = query_start + 1;
    while (pos < url.size()) {
        size_t end = url.find('&', pos);
        if (end == std::string::npos) {
            end = url.size();
        }
        size_t equals = url.find('=', pos);
        if (equals != std::string::npos && equals < end && url.substr(pos, equals - pos) == key) {
            return true;
        }
        pos = end + 1;
    }
    return false;
}

static void AppendQueryParamIfMissing(std::string& url, const std::string& key, const std::string& value) {
    if (value.empty() || UrlHasQueryParam(url, key)) {
        return;
    }
    url += (url.find('?') == std::string::npos) ? "?" : "&";
    url += key + "=" + value;
}

bool WebsocketProtocol::OpenAudioChannel() {
    static constexpr size_t kWebSocketReceiveBufferSize = 4096;

    if (websocket_ != nullptr) {
        delete websocket_;
    }

    Settings settings("websocket", false);
    std::string url = settings.GetString("url");
    
    // Validate URL and derive from OTA URL if invalid or not configured
    if (!IsValidWebSocketUrl(url)) {
        if (!url.empty()) {
            ESP_LOGW(TAG, "Invalid WebSocket URL in settings (localhost/invalid): %s, clearing invalid URL", url.c_str());
            // Clear invalid URL from settings
            settings.SetString("url", "");
        }
        
        // Derive WebSocket URL from OTA URL (same host, port 8000)
        Ota ota;
        std::string ota_url = ota.GetCheckVersionUrl();
        if (!ota_url.empty()) {
            // Extract hostname from OTA URL and construct WebSocket URL
            // Format: http://host:port/path or https://host:port/path
            size_t protocol_end = ota_url.find("://");
            if (protocol_end != std::string::npos) {
                std::string protocol = ota_url.substr(0, protocol_end);
                // Use wss:// if OTA uses https://, otherwise ws://
                std::string ws_protocol = (protocol == "https") ? "wss" : "ws";
                
                size_t host_start = protocol_end + 3;
                size_t host_end = ota_url.find_first_of(":/", host_start);
                if (host_end == std::string::npos) {
                    host_end = ota_url.length();
                }
                std::string hostname = ota_url.substr(host_start, host_end - host_start);
                
                std::string ws_path = "/babymilu/v1/";
                if (ota_url.find("/xiaozhi/") != std::string::npos &&
                    ota_url.find("/babymilu/") == std::string::npos) {
                    ws_path = "/xiaozhi/v1/";
                }
                url = ws_protocol + "://" + hostname + ":8000" + ws_path;
                
                if (IsValidWebSocketUrl(url)) {
                    ESP_LOGI(TAG, "Using WebSocket URL derived from OTA URL: %s", url.c_str());
                } else {
                    ESP_LOGE(TAG, "Derived WebSocket URL is invalid: %s", url.c_str());
                    url = "";
                }
            }
        }
        
        if (url.empty()) {
            ESP_LOGE(TAG, "No valid WebSocket URL configured and could not derive from OTA URL");
            return false;
        }
    } else {
        ESP_LOGI(TAG, "Using WebSocket URL from settings: %s", url.c_str());
    }
    
    std::string mac_address = SystemInfo::GetMacAddress();
    std::string client_id = Board::GetInstance().GetUuid();
    std::string connection_mode = Application::GetInstance().GetWebSocketConnectionMode();
    AppendQueryParamIfMissing(url, "device-id", mac_address);
    AppendQueryParamIfMissing(url, "client-id", client_id);
    AppendQueryParamIfMissing(url, "mode", connection_mode);
    AppendQueryParamIfMissing(url, "connectionType", connection_mode);
    ESP_LOGI(TAG, "WebSocket URL with connection params: %s", url.c_str());
    
    std::string token = settings.GetString("token");
    int version = settings.GetInt("version");
    if (version != 0) {
        version_ = version;
    }

    error_occurred_ = false;
    frame_count_ = 0;  // Reset frame counter for new session
    audio_metadata_frame_count_ = 0;
    audio_metadata_start_sequence_ = 0;
    audio_metadata_start_timestamp_ms_ = 0;
    last_incoming_time_ = std::chrono::steady_clock::now();  // Initialize timestamp for inactivity checking
    websocket_ = Board::GetInstance().CreateWebSocket();
    websocket_->SetReceiveBufferSize(kWebSocketReceiveBufferSize);
    ESP_LOGI(TAG, "Configured WebSocket receive buffer: %u bytes", (unsigned)kWebSocketReceiveBufferSize);
    
    // Set headers before connecting - the WebSocket library will automatically
    // handle the HTTP GET + Upgrade handshake when Connect() is called with ws:// or wss:// URL
    if (!token.empty()) {
        // If token not has a space, add "Bearer " prefix
        if (token.find(" ") == std::string::npos) {
            token = "Bearer " + token;
        }
        websocket_->SetHeader("Authorization", token.c_str());
    }
    websocket_->SetHeader("Protocol-Version", std::to_string(version_).c_str());
    // Note: Device-Id and Client-Id are now in URL query params, but keep headers for backward compatibility
    websocket_->SetHeader("Device-Id", mac_address.c_str());
    websocket_->SetHeader("Client-Id", client_id.c_str());
    websocket_->SetHeader("Connection-Mode", connection_mode.c_str());

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (binary) {
            if (on_incoming_audio_ != nullptr) {
                if (version_ == 2) {
                    BinaryProtocol2* bp2 = (BinaryProtocol2*)data;
                    bp2->version = ntohs(bp2->version);
                    bp2->type = ntohs(bp2->type);
                    bp2->timestamp = ntohl(bp2->timestamp);
                    bp2->payload_size = ntohl(bp2->payload_size);
                    auto payload = (uint8_t*)bp2->payload;
                    on_incoming_audio_(AudioStreamPacket{
                        .sample_rate = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp = bp2->timestamp,
                        .payload = std::vector<uint8_t>(payload, payload + bp2->payload_size)
                    });
                } else if (version_ == 3) {
                    // Version 3: Server sends raw Opus frames over WebSocket.
                    // No wrapper detection — previous heuristic could false-positive
                    // on random Opus byte patterns, corrupting ~0.1-0.5% of frames.
                    on_incoming_audio_(AudioStreamPacket{
                        .sample_rate = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp = 0,
                        .payload = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                    });
                } else {
                    on_incoming_audio_(AudioStreamPacket{
                        .sample_rate = server_sample_rate_,
                        .frame_duration = server_frame_duration_,
                        .timestamp = 0,
                        .payload = std::vector<uint8_t>((uint8_t*)data, (uint8_t*)data + len)
                    });
                }
            }
        } else {
            // Parse JSON data
            ESP_LOGI(TAG, "WS RX text message len=%u data=%.*s", (unsigned)len, (int)std::min((int)len, 200), data);
            auto root = cJSON_Parse(data);
            if (root == nullptr) {
                ESP_LOGE(TAG, "Failed to parse WebSocket JSON message: %.*s", (int)std::min((int)len, 200), data);
                return;
            }
            auto type = cJSON_GetObjectItem(root, "type");
            if (cJSON_IsString(type)) {
                ESP_LOGI(TAG, "WS RX message type: %s", type->valuestring);
                // Check if this is a listen message for immediate visibility
                if (strcmp(type->valuestring, "listen") == 0) {
                    ESP_LOGI(TAG, "*** LISTEN MESSAGE DETECTED IN WEBSOCKET ***");
                }
                if (strcmp(type->valuestring, "hello") == 0) {
                    ParseServerHello(root);
                } else {
                    ESP_LOGI(TAG, "Forwarding WebSocket message type '%s' to Application::OnIncomingJson", type->valuestring);
                    if (on_incoming_json_ != nullptr) {
                        on_incoming_json_(root);
                    } else {
                        ESP_LOGW(TAG, "on_incoming_json_ callback is null, cannot forward WebSocket message type '%s'", type->valuestring);
                    }
                }
            } else {
                ESP_LOGE(TAG, "Missing message type in WebSocket message, data: %.*s", (int)std::min((int)len, 200), data);
            }
            cJSON_Delete(root);
        }
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Websocket disconnected");
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    ESP_LOGI(TAG, "Connecting to websocket server: %s with version: %d", url.c_str(), version_);
    if (!websocket_->Connect(url.c_str())) {
        ESP_LOGE(TAG, "Failed to connect to websocket server");
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    // Send hello message to describe the client
    auto message = GetHelloMessage();
    ESP_LOGI(TAG, "Sending hello message: %s", message.c_str());
    if (!SendText(message)) {
        return false;
    }

    // Wait for server hello
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        SetError(Lang::Strings::SERVER_TIMEOUT);
        return false;
    }

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }

    return true;
}

std::string WebsocketProtocol::GetHelloMessage() {
    // keys: message type, version, audio_params (format, sample_rate, channels)
    // Always request 16 kHz for WebSocket as per server requirements
    // The Opus encoder is configured for 16 kHz, and input is resampled to 16 kHz if needed
    int requested_sample_rate = 16000;
    std::string connection_mode = Application::GetInstance().GetWebSocketConnectionMode();
    std::string mac_address = SystemInfo::GetMacAddress();
    std::string client_id = Board::GetInstance().GetUuid();
    
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", version_);
    cJSON_AddStringToObject(root, "device_id", mac_address.c_str());
    cJSON_AddStringToObject(root, "client_id", client_id.c_str());
    cJSON_AddStringToObject(root, "mode", connection_mode.c_str());
    cJSON_AddStringToObject(root, "connectionType", connection_mode.c_str());
    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "mode", connection_mode.c_str());
    cJSON_AddStringToObject(request, "connectionType", connection_mode.c_str());
    cJSON_AddItemToObject(root, "request", request);
    cJSON* features = cJSON_CreateObject();
#if CONFIG_USE_SERVER_AEC
    cJSON_AddBoolToObject(features, "aec", true);
#endif
#if CONFIG_IOT_PROTOCOL_MCP
    cJSON_AddBoolToObject(features, "mcp", true);
#endif
    cJSON_AddBoolToObject(features, "audio_sequence", true);
    cJSON_AddBoolToObject(features, "audio_timestamp_ms", true);
    cJSON_AddStringToObject(features, "audio_metadata", "chunk");
    cJSON_AddItemToObject(root, "features", features);
    cJSON_AddStringToObject(root, "transport", "websocket");
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

void WebsocketProtocol::ParseServerHello(const cJSON* root) {
    auto transport = cJSON_GetObjectItem(root, "transport");
    if (!cJSON_IsString(transport) || strcmp(transport->valuestring, "websocket") != 0) {
        ESP_LOGE(TAG, "Unsupported or missing transport in server hello: %s",
                 cJSON_IsString(transport) ? transport->valuestring : "null");
        return;
    }

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        session_id_ = session_id->valuestring;
        ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());
    }

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

    xEventGroupSetBits(event_group_handle_, WEBSOCKET_PROTOCOL_SERVER_HELLO_EVENT);
}
