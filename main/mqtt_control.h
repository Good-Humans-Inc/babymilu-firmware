#pragma once

#include "runtime_config.h"

#include <functional>
#include <string>
#include <utility>

#include <mqtt_client.h>

class MqttControl {
public:
    using WsStartCallback = std::function<void(const std::string& url, const std::string& connectionType)>;
    using StringCallback = std::function<void(const std::string& value)>;
    using VoidCallback = std::function<void()>;

    explicit MqttControl(RuntimeConfig& config) : config_(config) {}

    bool Start();

    void OnWsStart(WsStartCallback callback) { on_ws_start_ = std::move(callback); }
    void OnWifiReconfigure(VoidCallback callback) { on_wifi_reconfigure_ = std::move(callback); }
    void OnWifiClear(VoidCallback callback) { on_wifi_clear_ = std::move(callback); }
    void OnSwitchWifi(StringCallback callback) { on_switch_wifi_ = std::move(callback); }
    void OnRemoteAnimationUpdate(StringCallback callback) { on_remote_animation_update_ = std::move(callback); }
    void OnSetOtaUrl(StringCallback callback) { on_set_ota_url_ = std::move(callback); }

private:
    static void EventHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data);
    void HandleEvent(esp_mqtt_event_handle_t event);
    void HandlePayload(const std::string& payload);
    std::string ResolveConnectionType(cJSON* root, const std::string& url) const;

    RuntimeConfig& config_;
    esp_mqtt_client_handle_t client_ = nullptr;
    WsStartCallback on_ws_start_;
    VoidCallback on_wifi_reconfigure_;
    VoidCallback on_wifi_clear_;
    StringCallback on_switch_wifi_;
    StringCallback on_remote_animation_update_;
    StringCallback on_set_ota_url_;
};
