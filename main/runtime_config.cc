#include "runtime_config.h"

#include "settings_store.h"
#include "system_info.h"
#include "url_utils.h"

#include <sdkconfig.h>
#include <esp_log.h>

#include <sys/time.h>

#define TAG "RuntimeConfig"

namespace {

std::string JsonString(cJSON* obj, const char* key) {
    cJSON* item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsString(item) && item->valuestring ? std::string(item->valuestring) : "";
}

int JsonInt(cJSON* obj, const char* key, int fallback) {
    cJSON* item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

bool JsonBoolOrForce(cJSON* obj) {
    cJSON* force = cJSON_GetObjectItem(obj, "force");
    if (cJSON_IsBool(force)) {
        return cJSON_IsTrue(force);
    }
    if (cJSON_IsNumber(force)) {
        return force->valueint != 0;
    }
    return false;
}

void PersistStringIfPresent(SettingsStore& settings, const char* key, const std::string& value) {
    if (!value.empty()) {
        settings.SetString(key, value);
    }
}

}  // namespace

void RuntimeConfig::Load() {
    device_id_ = system_info::GetMacAddress();
    client_id_ = system_info::GetClientId();
    ota_url_ = CONFIG_ECHOEAR_BABYMILU_OTA_URL;
    websocket_.url = CONFIG_ECHOEAR_BABYMILU_WEBSOCKET_URL;
    websocket_.version = 3;
    mqtt_.endpoint = CONFIG_ECHOEAR_BABYMILU_MQTT_URL;
    mqtt_.keepalive = 60;
    mqtt_.client_id = device_id_;
    mqtt_.publish_topic = "xiaozhi/" + device_id_ + "/up";
    mqtt_.subscribe_topic = "xiaozhi/" + device_id_ + "/down";
    ReadPersisted();
}

void RuntimeConfig::ReadPersisted() {
    SettingsStore ota("ota", false);
    std::string custom_ota = ota.GetString("url");
    if (!custom_ota.empty()) {
        ota_url_ = custom_ota;
    }

    SettingsStore ws("websocket", false);
    websocket_.url = ws.GetString("url", websocket_.url);
    websocket_.version = ws.GetInt("version", websocket_.version);
    connectionType_ = url_utils::NormalizeConnectionType(ws.GetString("connectionType", connectionType_));

    SettingsStore mqtt("mqtt", false);
    mqtt_.endpoint = mqtt.GetString("endpoint", mqtt_.endpoint);
    mqtt_.client_id = mqtt.GetString("client_id", mqtt_.client_id);
    mqtt_.publish_topic = mqtt.GetString("publish_topic", mqtt_.publish_topic);
    mqtt_.subscribe_topic = mqtt.GetString("subscribe_topic", mqtt_.subscribe_topic);
    mqtt_.username = mqtt.GetString("username");
    mqtt_.password = mqtt.GetString("password");
    mqtt_.keepalive = mqtt.GetInt("keepalive", mqtt_.keepalive);
}

void RuntimeConfig::setConnectionType(const std::string& value) {
    connectionType_ = url_utils::NormalizeConnectionType(value);
    SettingsStore ws("websocket", true);
    ws.SetString("connectionType", connectionType_);
}

std::string RuntimeConfig::BuildWebSocketUrl(const std::string& override_url) const {
    const std::string& base = override_url.empty() ? websocket_.url : override_url;
    return url_utils::BuildWebSocketUrl(base, device_id_, client_id_, connectionType_);
}

void RuntimeConfig::ApplyManifestJson(const char* json) {
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGE(TAG, "OTA manifest parse failed");
        return;
    }
    ApplyManifest(root);
    cJSON_Delete(root);
}

void RuntimeConfig::ApplyManifest(cJSON* root) {
    if (!cJSON_IsObject(root)) {
        return;
    }

    cJSON* activation = cJSON_GetObjectItem(root, "activation");
    if (activation != nullptr) {
        ESP_LOGW(TAG, "Ignoring unexpected activation field in BabyMilu OTA manifest");
    }

    cJSON* websocket = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        std::string url = JsonString(websocket, "url");
        if (!url.empty()) {
            websocket_.url = url;
        }
        websocket_.version = JsonInt(websocket, "version", websocket_.version);
        PersistWebSocket();
        ESP_LOGI(TAG, "hydrated websocket url=%s version=%d", websocket_.url.c_str(), websocket_.version);
    }

    cJSON* mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        mqtt_.endpoint = JsonString(mqtt, "endpoint").empty() ? mqtt_.endpoint : JsonString(mqtt, "endpoint");
        mqtt_.client_id = JsonString(mqtt, "client_id").empty() ? mqtt_.client_id : JsonString(mqtt, "client_id");
        mqtt_.publish_topic = JsonString(mqtt, "publish_topic").empty() ? mqtt_.publish_topic : JsonString(mqtt, "publish_topic");
        mqtt_.subscribe_topic = JsonString(mqtt, "subscribe_topic").empty() ? mqtt_.subscribe_topic : JsonString(mqtt, "subscribe_topic");
        mqtt_.username = JsonString(mqtt, "username");
        mqtt_.password = JsonString(mqtt, "password");
        mqtt_.keepalive = JsonInt(mqtt, "keepalive", mqtt_.keepalive);
        PersistMqtt();
        ESP_LOGI(TAG, "hydrated mqtt endpoint=%s keepalive=%d", mqtt_.endpoint.c_str(), mqtt_.keepalive);
    }

    cJSON* server_time = cJSON_GetObjectItem(root, "server_time");
    if (cJSON_IsObject(server_time)) {
        cJSON* timestamp = cJSON_GetObjectItem(server_time, "timestamp");
        if (cJSON_IsNumber(timestamp)) {
            server_time_.valid = true;
            server_time_.timestamp_ms = static_cast<int64_t>(timestamp->valuedouble);
            server_time_.timezone_offset_minutes = JsonInt(server_time, "timezone_offset", 0);
            PersistServerTime();
            ApplyServerTime();
        }
    }

    cJSON* firmware = cJSON_GetObjectItem(root, "firmware");
    if (cJSON_IsObject(firmware)) {
        firmware_.version = JsonString(firmware, "version");
        firmware_.url = JsonString(firmware, "url");
        firmware_.force = JsonBoolOrForce(firmware);
        firmware_.present = !firmware_.version.empty() && !firmware_.url.empty();
        if (firmware_.present) {
            PersistFirmwareMetadata();
            ESP_LOGI(TAG, "firmware update metadata version=%s url=%s force=%d",
                     firmware_.version.c_str(), firmware_.url.c_str(), firmware_.force);
        }
    }
}

void RuntimeConfig::PersistWebSocket() const {
    SettingsStore settings("websocket", true);
    settings.SetString("url", websocket_.url);
    settings.SetInt("version", websocket_.version);
    settings.SetString("connectionType", connectionType_);
}

void RuntimeConfig::PersistMqtt() const {
    SettingsStore settings("mqtt", true);
    PersistStringIfPresent(settings, "endpoint", mqtt_.endpoint);
    PersistStringIfPresent(settings, "client_id", mqtt_.client_id);
    PersistStringIfPresent(settings, "publish_topic", mqtt_.publish_topic);
    PersistStringIfPresent(settings, "subscribe_topic", mqtt_.subscribe_topic);
    PersistStringIfPresent(settings, "username", mqtt_.username);
    PersistStringIfPresent(settings, "password", mqtt_.password);
    settings.SetInt("keepalive", mqtt_.keepalive);
}

void RuntimeConfig::PersistServerTime() const {
    SettingsStore settings("server_time", true);
    settings.SetString("timestamp_ms", std::to_string(server_time_.timestamp_ms));
    settings.SetInt("timezone_offset", server_time_.timezone_offset_minutes);
}

void RuntimeConfig::PersistFirmwareMetadata() const {
    SettingsStore settings("firmware", true);
    settings.SetString("version", firmware_.version);
    settings.SetString("url", firmware_.url);
    settings.SetBool("force", firmware_.force);
}

void RuntimeConfig::ApplyServerTime() {
    if (!server_time_.valid || server_time_.timestamp_ms <= 0) {
        return;
    }
    int64_t adjusted_ms = server_time_.timestamp_ms +
                          static_cast<int64_t>(server_time_.timezone_offset_minutes) * 60 * 1000;
    timeval tv = {
        .tv_sec = static_cast<time_t>(adjusted_ms / 1000),
        .tv_usec = static_cast<suseconds_t>((adjusted_ms % 1000) * 1000),
    };
    settimeofday(&tv, nullptr);
    ESP_LOGI(TAG, "applied server_time timestamp_ms=%lld tz_offset=%d",
             static_cast<long long>(server_time_.timestamp_ms),
             server_time_.timezone_offset_minutes);
}
