#pragma once

#include <cstdint>
#include <string>

#include <cJSON.h>

struct WebSocketRuntimeConfig {
    std::string url;
    int version = 3;
};

struct MqttRuntimeConfig {
    std::string endpoint;
    std::string client_id;
    std::string publish_topic;
    std::string subscribe_topic;
    std::string username;
    std::string password;
    int keepalive = 60;
};

struct ServerTimeConfig {
    bool valid = false;
    int64_t timestamp_ms = 0;
    int timezone_offset_minutes = 0;
};

struct FirmwareUpdateMetadata {
    bool present = false;
    bool force = false;
    std::string version;
    std::string url;
};

class RuntimeConfig {
public:
    void Load();
    void ApplyManifestJson(const char* json);
    void ApplyManifest(cJSON* root);

    const std::string& device_id() const { return device_id_; }
    const std::string& client_id() const { return client_id_; }
    const std::string& connectionType() const { return connectionType_; }
    void setConnectionType(const std::string& value);

    const std::string& ota_url() const { return ota_url_; }
    const WebSocketRuntimeConfig& websocket() const { return websocket_; }
    const MqttRuntimeConfig& mqtt() const { return mqtt_; }
    const ServerTimeConfig& server_time() const { return server_time_; }
    const FirmwareUpdateMetadata& firmware() const { return firmware_; }

    std::string BuildWebSocketUrl(const std::string& override_url = "") const;

private:
    void PersistWebSocket() const;
    void PersistMqtt() const;
    void PersistServerTime() const;
    void PersistFirmwareMetadata() const;
    void ReadPersisted();
    void ApplyServerTime();

    std::string device_id_;
    std::string client_id_;
    std::string connectionType_ = "normal";
    std::string ota_url_;
    WebSocketRuntimeConfig websocket_;
    MqttRuntimeConfig mqtt_;
    ServerTimeConfig server_time_;
    FirmwareUpdateMetadata firmware_;
};
