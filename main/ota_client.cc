#include "ota_client.h"

#include "system_info.h"

#include <sdkconfig.h>
#include <esp_app_desc.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include <string>
#include <vector>

#define TAG "OtaClient"

namespace {

esp_err_t CollectHttpBody(esp_http_client_event_t* evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data == nullptr || evt->data_len <= 0) {
        return ESP_OK;
    }
    auto* body = static_cast<std::string*>(evt->user_data);
    body->append(static_cast<const char*>(evt->data), evt->data_len);
    return ESP_OK;
}

bool IsDifferentVersion(const std::string& target_version) {
    if (target_version.empty()) {
        return false;
    }
    const esp_app_desc_t* desc = esp_app_get_description();
    return desc == nullptr || target_version != desc->version;
}

}  // namespace

bool OtaClient::Hydrate() {
    std::string body;
    esp_http_client_config_t http_cfg = {};
    http_cfg.url = config_.ota_url().c_str();
    http_cfg.method = HTTP_METHOD_GET;
    http_cfg.timeout_ms = 15000;
    http_cfg.event_handler = CollectHttpBody;
    http_cfg.user_data = &body;
    http_cfg.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == nullptr) {
        ESP_LOGE(TAG, "failed to create OTA HTTP client");
        return false;
    }

    esp_http_client_set_header(client, "Device-Id", config_.device_id().c_str());
    esp_http_client_set_header(client, "X-Device-Id", config_.device_id().c_str());
    esp_http_client_set_header(client, "User-Agent", "EchoEarBabyMiluGround/0.1.0");

    ESP_LOGI(TAG, "hydrating runtime config from %s", config_.ota_url().c_str());
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || body.empty()) {
        ESP_LOGW(TAG, "OTA hydration skipped err=%s status=%d len=%u",
                 esp_err_to_name(err), status, static_cast<unsigned>(body.size()));
        return false;
    }

    config_.ApplyManifestJson(body.c_str());
    return true;
}

void OtaClient::MaybeStartFirmwareUpdate() {
#if CONFIG_ECHOEAR_BABYMILU_ENABLE_FIRMWARE_UPDATE
    const FirmwareUpdateMetadata& update = config_.firmware();
    if (!update.present) {
        return;
    }
    if (!update.force && !IsDifferentVersion(update.version)) {
        ESP_LOGI(TAG, "firmware metadata matches current version, skipping update");
        return;
    }

    ESP_LOGW(TAG, "starting firmware OTA update version=%s url=%s", update.version.c_str(), update.url.c_str());
    esp_http_client_config_t http_cfg = {};
    http_cfg.url = update.url.c_str();
    http_cfg.timeout_ms = 30000;
    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;
    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "firmware OTA complete, restarting");
        esp_restart();
    }
    ESP_LOGE(TAG, "firmware OTA failed: %s", esp_err_to_name(err));
#else
    if (config_.firmware().present) {
        ESP_LOGW(TAG, "firmware metadata present but update support is disabled in menuconfig");
    }
#endif
}
