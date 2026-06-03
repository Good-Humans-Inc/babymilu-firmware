#include "mqtt_control.h"

#include "url_utils.h"

#include <cJSON.h>
#include <esp_log.h>

#include <cstring>

#define TAG "MqttControl"

namespace {

std::string JsonString(cJSON* root, const char* key) {
    cJSON* item = cJSON_GetObjectItem(root, key);
    return cJSON_IsString(item) && item->valuestring ? std::string(item->valuestring) : "";
}

const char* Nullable(const std::string& value) {
    return value.empty() ? nullptr : value.c_str();
}

}  // namespace

bool MqttControl::Start() {
    const MqttRuntimeConfig& mqtt = config_.mqtt();
    if (mqtt.endpoint.empty() || mqtt.subscribe_topic.empty()) {
        ESP_LOGW(TAG, "MQTT not started: endpoint or subscribe topic missing");
        return false;
    }
    if (client_ != nullptr) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = mqtt.endpoint.c_str();
    mqtt_cfg.credentials.client_id = mqtt.client_id.empty() ? config_.device_id().c_str() : mqtt.client_id.c_str();
    mqtt_cfg.credentials.username = Nullable(mqtt.username);
    mqtt_cfg.credentials.authentication.password = Nullable(mqtt.password);
    mqtt_cfg.session.keepalive = mqtt.keepalive;
    mqtt_cfg.network.timeout_ms = 10000;

    client_ = esp_mqtt_client_init(&mqtt_cfg);
    if (client_ == nullptr) {
        ESP_LOGE(TAG, "failed to create MQTT client");
        return false;
    }
    esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MqttControl::EventHandler, this);
    esp_err_t err = esp_mqtt_client_start(client_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "MQTT started endpoint=%s subscribe=%s", mqtt.endpoint.c_str(), mqtt.subscribe_topic.c_str());
    return true;
}

void MqttControl::EventHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
    (void)base;
    auto* self = static_cast<MqttControl*>(arg);
    self->HandleEvent(static_cast<esp_mqtt_event_handle_t>(event_data));
}

void MqttControl::HandleEvent(esp_mqtt_event_handle_t event) {
    if (event == nullptr) {
        return;
    }
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            esp_mqtt_client_subscribe(client_, config_.mqtt().subscribe_topic.c_str(), 1);
            break;
        case MQTT_EVENT_DATA: {
            std::string payload(event->data, event->data + event->data_len);
            HandlePayload(payload);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            break;
        default:
            break;
    }
}

std::string MqttControl::ResolveConnectionType(cJSON* root, const std::string& url) const {
    std::string value = JsonString(root, "connectionType");
    if (value.empty()) {
        value = url_utils::QueryParam(url, "connectionType");
    }
    return url_utils::NormalizeConnectionType(value.empty() ? "normal" : value);
}

void MqttControl::HandlePayload(const std::string& payload) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        ESP_LOGW(TAG, "ignoring invalid MQTT JSON payload");
        return;
    }
    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == nullptr) {
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type->valuestring, "ws_start") == 0) {
        std::string url = JsonString(root, "wss");
        if (url.empty()) {
            url = config_.websocket().url;
        }
        std::string connectionType = ResolveConnectionType(root, url);
        url_utils::UpsertQueryParam(url, "connectionType", connectionType);
        ESP_LOGI(TAG, "ws_start connectionType=%s", connectionType.c_str());
        if (on_ws_start_) {
            on_ws_start_(url, connectionType);
        }
    } else if (strcmp(type->valuestring, "wifi_reconfig_nimble") == 0) {
        if (on_wifi_reconfigure_) {
            on_wifi_reconfigure_();
        }
    } else if (strcmp(type->valuestring, "wifi_clear_credential") == 0) {
        if (on_wifi_clear_) {
            on_wifi_clear_();
        }
    } else if (strcmp(type->valuestring, "switch_wifi_to") == 0) {
        std::string ssid = JsonString(root, "message");
        if (!ssid.empty() && on_switch_wifi_) {
            on_switch_wifi_(ssid);
        }
    } else if (strcmp(type->valuestring, "remote_anim_update") == 0) {
        if (on_remote_animation_update_) {
            std::string url = JsonString(root, "url");
            if (url.empty()) {
                url = JsonString(root, "message");
            }
            on_remote_animation_update_(url);
        }
    } else if (strcmp(type->valuestring, "set_ota_url") == 0) {
        std::string url = JsonString(root, "message");
        if (!url.empty() && on_set_ota_url_) {
            on_set_ota_url_(url);
        }
    }
    cJSON_Delete(root);
}
