#include "wifi_manager.h"

#include "runtime_config.h"
#include "settings_store.h"
#include "system_info.h"

#include <sdkconfig.h>
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cstring>

#define TAG "WifiManager"
#define WIFI_CONNECTED_BIT BIT0

namespace {

constexpr int kMaxReconnectPerCandidate = 3;
constexpr int kScanRetryMs = 10000;
constexpr uint32_t kConnectedCallbackStackSize = 12288;

esp_err_t CollectHttpBody(esp_http_client_event_t* evt) {
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != nullptr && evt->data_len > 0) {
        auto* body = static_cast<std::string*>(evt->user_data);
        body->append(static_cast<const char*>(evt->data), evt->data_len);
    }
    return ESP_OK;
}

bool SameSsid(const wifi_ap_record_t& ap, const std::string& ssid) {
    const size_t scanned_len = strnlen(reinterpret_cast<const char*>(ap.ssid), sizeof(ap.ssid));
    return scanned_len == ssid.size() && memcmp(ap.ssid, ssid.data(), scanned_len) == 0;
}

}  // namespace

std::vector<WifiCredential> WifiCredentialStore::Load() const {
    SettingsStore settings("wifi_cred", false);
    int count = settings.GetInt("count", 0);
    if (count < 0) {
        count = 0;
    }
    if (count > 10) {
        count = 10;
    }
    std::vector<WifiCredential> credentials;
    credentials.reserve(count);
    for (int i = 0; i < count; ++i) {
        std::string ssid = settings.GetString(("ssid" + std::to_string(i)).c_str());
        std::string password = settings.GetString(("password" + std::to_string(i)).c_str());
        if (!ssid.empty()) {
            credentials.push_back({ssid, password});
        }
    }
    return credentials;
}

void WifiCredentialStore::Save(const std::vector<WifiCredential>& credentials) const {
    SettingsStore settings("wifi_cred", true);
    settings.EraseAll();
    const int count = std::min<int>(credentials.size(), 10);
    settings.SetInt("count", count);
    for (int i = 0; i < count; ++i) {
        settings.SetString(("ssid" + std::to_string(i)).c_str(), credentials[i].ssid);
        settings.SetString(("password" + std::to_string(i)).c_str(), credentials[i].password);
    }
}

void WifiCredentialStore::Clear() const {
    SettingsStore settings("wifi_cred", true);
    settings.EraseAll();
}

void WifiCredentialStore::AddLowestPriority(const std::string& ssid, const std::string& password) const {
    if (ssid.empty()) {
        return;
    }
    std::vector<WifiCredential> credentials = Load();
    credentials.erase(std::remove_if(credentials.begin(), credentials.end(),
                                     [&ssid](const WifiCredential& item) { return item.ssid == ssid; }),
                      credentials.end());
    credentials.push_back({ssid, password});
    Save(credentials);
}

bool WifiCredentialStore::ReorderByRankedSsids(const std::vector<std::string>& ranked_ssids) const {
    if (ranked_ssids.empty()) {
        return false;
    }
    std::vector<WifiCredential> current = Load();
    if (current.empty()) {
        return false;
    }

    std::vector<WifiCredential> reordered;
    for (const std::string& ssid : ranked_ssids) {
        auto it = std::find_if(current.begin(), current.end(),
                               [&ssid](const WifiCredential& item) { return item.ssid == ssid; });
        if (it != current.end()) {
            reordered.push_back(*it);
        }
    }
    for (const WifiCredential& item : current) {
        auto it = std::find_if(reordered.begin(), reordered.end(),
                               [&item](const WifiCredential& ranked) { return ranked.ssid == item.ssid; });
        if (it == reordered.end()) {
            reordered.push_back(item);
        }
    }

    bool changed = reordered.size() != current.size();
    for (size_t i = 0; !changed && i < current.size(); ++i) {
        changed = current[i].ssid != reordered[i].ssid;
    }
    if (changed) {
        Save(reordered);
    }
    return changed;
}

WifiManager::WifiManager() {
    event_group_ = xEventGroupCreate();
}

void WifiManager::Start() {
    LoadCredentialsForBoot();
    if (force_ble_config_) {
        StartBleProvisioning("forced by MQTT/app request");
        return;
    }
    if (credentials_.empty()) {
        StartBleProvisioning("no saved Wi-Fi credentials");
        return;
    }

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEventHandler, this, &wifi_event_instance_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &IpEventHandler, this, &ip_event_instance_));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    esp_timer_create_args_t scan_timer_args = {};
    scan_timer_args.callback = [](void* arg) {
        esp_wifi_scan_start(nullptr, false);
    };
    scan_timer_args.arg = this;
    scan_timer_args.dispatch_method = ESP_TIMER_TASK;
    scan_timer_args.name = "wifi_scan";
    ESP_ERROR_CHECK(esp_timer_create(&scan_timer_args, &scan_timer_));

    ESP_ERROR_CHECK(esp_wifi_start());
}

void WifiManager::LoadCredentialsForBoot() {
    WifiCredentialStore store;
    credentials_ = store.Load();
    SettingsStore settings("wifi", true);
    force_ble_config_ = settings.GetBool("force_ble_cfg", false);

#if CONFIG_ECHOEAR_BABYMILU_ALLOW_MENUCONFIG_WIFI_FALLBACK
    if (!force_ble_config_ && credentials_.empty() && strlen(CONFIG_ECHOEAR_BABYMILU_WIFI_FALLBACK_SSID) > 0) {
        ESP_LOGW(TAG, "using menuconfig Wi-Fi fallback because no saved credentials exist");
        credentials_.push_back({CONFIG_ECHOEAR_BABYMILU_WIFI_FALLBACK_SSID,
                                CONFIG_ECHOEAR_BABYMILU_WIFI_FALLBACK_PASSWORD});
    }
#endif
    ApplyPreferredSsidFromSettings();
}

void WifiManager::StartBleProvisioning(const char* reason) {
    ESP_LOGW(TAG, "starting BLE Wi-Fi provisioning: %s", reason ? reason : "unspecified");
    ble_provisioner_.OnCredentials([this](const std::string& ssid, const std::string& password) {
        SaveBleCredential(ssid, password);
    });
    if (!ble_provisioner_.Start("BabyMilu")) {
        ESP_LOGE(TAG, "BLE Wi-Fi provisioning failed to start");
    }
}

void WifiManager::SaveBleCredential(const std::string& ssid, const std::string& password) {
    WifiCredentialStore().AddLowestPriority(ssid, password);
    SettingsStore settings("wifi", true);
    settings.SetString("nxt_boot_ssid", ssid);
    settings.EraseKey("force_ble_cfg");
    force_ble_config_ = false;
    ESP_LOGI(TAG, "saved BLE Wi-Fi credential as lowest priority; next boot prefers SSID '%s' once", ssid.c_str());
}

void WifiManager::ApplyPreferredSsidFromSettings() {
    SettingsStore settings("wifi", true);
    std::string preferred = settings.GetString("nxt_boot_ssid");
    if (!preferred.empty()) {
        SetPreferredSsidForNextConnect(preferred);
        settings.EraseKey("nxt_boot_ssid");
    }
}

std::vector<WifiCredential> WifiManager::EffectiveCredentials() {
    std::vector<WifiCredential> effective = credentials_;
    if (!preferred_ssid_once_.empty()) {
        auto it = std::find_if(effective.begin(), effective.end(),
                               [this](const WifiCredential& item) { return item.ssid == preferred_ssid_once_; });
        if (it != effective.end() && it != effective.begin()) {
            WifiCredential preferred = *it;
            effective.erase(it);
            effective.insert(effective.begin(), preferred);
            ESP_LOGI(TAG, "one-shot preferred SSID applied: %s", preferred_ssid_once_.c_str());
        } else if (it == effective.end()) {
            ESP_LOGW(TAG, "one-shot preferred SSID not found in saved credentials: %s", preferred_ssid_once_.c_str());
        }
        preferred_ssid_once_.clear();
    }
    return effective;
}

void WifiManager::SetPreferredSsidForNextConnect(const std::string& ssid) {
    preferred_ssid_once_ = ssid;
}

bool WifiManager::WaitForConnected(int timeout_ms) {
    EventBits_t bits = xEventGroupWaitBits(event_group_, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool WifiManager::IsConnected() const {
    return (xEventGroupGetBits(event_group_) & WIFI_CONNECTED_BIT) != 0;
}

void WifiManager::ScheduleScan(int delay_ms) {
    if (scan_timer_ != nullptr) {
        esp_timer_stop(scan_timer_);
        esp_timer_start_once(scan_timer_, static_cast<uint64_t>(delay_ms) * 1000);
    }
}

void WifiManager::HandleScanDone() {
    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num == 0) {
        ScheduleScan(kScanRetryMs);
        return;
    }
    std::vector<wifi_ap_record_t> ap_records(ap_num);
    esp_wifi_scan_get_ap_records(&ap_num, ap_records.data());

    queue_.clear();
    for (const WifiCredential& cred : EffectiveCredentials()) {
        auto it = std::find_if(ap_records.begin(), ap_records.end(),
                               [&cred](const wifi_ap_record_t& ap) { return SameSsid(ap, cred.ssid); });
        if (it == ap_records.end()) {
            continue;
        }
        Candidate candidate;
        candidate.ssid = cred.ssid;
        candidate.password = cred.password;
        candidate.channel = it->primary;
        candidate.authmode = it->authmode;
        memcpy(candidate.bssid, it->bssid, sizeof(candidate.bssid));
        queue_.push_back(candidate);
    }

    if (queue_.empty()) {
        ESP_LOGI(TAG, "no saved SSID was visible; retrying scan");
        ScheduleScan(kScanRetryMs);
        return;
    }
    ConnectNext();
}

void WifiManager::ConnectNext() {
    if (queue_.empty()) {
        ScheduleScan(kScanRetryMs);
        return;
    }
    Candidate candidate = queue_.front();
    queue_.erase(queue_.begin());
    connected_ssid_ = candidate.ssid;
    reconnect_count_ = 0;

    wifi_config_t wifi_config = {};
    snprintf(reinterpret_cast<char*>(wifi_config.sta.ssid), sizeof(wifi_config.sta.ssid), "%s", candidate.ssid.c_str());
    snprintf(reinterpret_cast<char*>(wifi_config.sta.password), sizeof(wifi_config.sta.password), "%s", candidate.password.c_str());
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.channel = candidate.channel;
    memcpy(wifi_config.sta.bssid, candidate.bssid, sizeof(wifi_config.sta.bssid));
    wifi_config.sta.bssid_set = true;

    ESP_LOGI(TAG, "connecting to saved SSID '%s'", candidate.ssid.c_str());
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();
}

void WifiManager::WifiEventHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
    auto* self = static_cast<WifiManager*>(arg);
    if (base != WIFI_EVENT) {
        return;
    }
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_scan_start(nullptr, false);
    } else if (event_id == WIFI_EVENT_SCAN_DONE) {
        self->HandleScanDone();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(self->event_group_, WIFI_CONNECTED_BIT);
        auto* disc = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        ESP_LOGW(TAG, "disconnected from '%s' reason=%d",
                 disc ? reinterpret_cast<const char*>(disc->ssid) : self->connected_ssid_.c_str(),
                 disc ? disc->reason : -1);
        if (self->reconnect_count_ < kMaxReconnectPerCandidate) {
            ++self->reconnect_count_;
            esp_wifi_connect();
            return;
        }
        self->ConnectNext();
    }
}

void WifiManager::IpEventHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
    auto* self = static_cast<WifiManager*>(arg);
    if (base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "connected to Wi-Fi SSID '%s'", self->connected_ssid_.c_str());
    self->NotifyConnectedAsync();
}

void WifiManager::NotifyConnectedAsync() {
    if (!on_connected_) {
        return;
    }
    if (connected_callback_task_ != nullptr) {
        ESP_LOGW(TAG, "Wi-Fi connected callback already running; skipping duplicate IP event");
        return;
    }

    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        ConnectedCallbackTask,
        "wifi_conn_cb",
        kConnectedCallbackStackSize,
        this,
        4,
        &connected_callback_task_,
        0,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        connected_callback_task_ = nullptr;
        ESP_LOGE(TAG, "failed to start Wi-Fi connected callback task");
    }
}

void WifiManager::ConnectedCallbackTask(void* arg) {
    auto* self = static_cast<WifiManager*>(arg);
    ESP_LOGI(TAG, "Wi-Fi connected callback task running stack=%lu caps=INTERNAL",
             static_cast<unsigned long>(kConnectedCallbackStackSize));
    if (self != nullptr && self->on_connected_) {
        self->on_connected_();
        self->connected_callback_task_ = nullptr;
    }
    vTaskDelete(nullptr);
}

void WifiManager::ClearCredentialsAndRequestOnboarding() {
    WifiCredentialStore().Clear();
    SettingsStore settings("wifi", true);
    settings.SetBool("force_ble_cfg", true);
    ESP_LOGI(TAG, "Wi-Fi credentials cleared; rebooting into onboarding request");
    esp_restart();
}

void WifiManager::RequestBleReconfigure() {
    SettingsStore settings("wifi", true);
    settings.SetBool("force_ble_cfg", true);
    ESP_LOGI(TAG, "BLE Wi-Fi reconfiguration requested; rebooting into clean network path");
    esp_restart();
}

std::vector<std::string> WifiManager::ParseRankedNetworks(const std::string& firestore_json) const {
    std::vector<std::string> ranked;
    cJSON* root = cJSON_Parse(firestore_json.c_str());
    if (!root) {
        return ranked;
    }
    cJSON* fields = cJSON_GetObjectItem(root, "fields");
    cJSON* wifi_setting = fields ? cJSON_GetObjectItem(fields, "wifiSetting") : nullptr;
    cJSON* wifi_map = wifi_setting ? cJSON_GetObjectItem(wifi_setting, "mapValue") : nullptr;
    cJSON* wifi_fields = wifi_map ? cJSON_GetObjectItem(wifi_map, "fields") : nullptr;
    cJSON* ranked_networks = wifi_fields ? cJSON_GetObjectItem(wifi_fields, "rankedNetworks") : nullptr;
    cJSON* array_value = ranked_networks ? cJSON_GetObjectItem(ranked_networks, "arrayValue") : nullptr;
    cJSON* values = array_value ? cJSON_GetObjectItem(array_value, "values") : nullptr;
    if (cJSON_IsArray(values)) {
        cJSON* value = nullptr;
        cJSON_ArrayForEach(value, values) {
            cJSON* string_value = cJSON_GetObjectItem(value, "stringValue");
            if (cJSON_IsString(string_value) && string_value->valuestring != nullptr) {
                ranked.emplace_back(string_value->valuestring);
            }
        }
    }
    cJSON_Delete(root);
    return ranked;
}

void WifiManager::FetchFirestoreRankingAndApply() {
    if (!IsConnected()) {
        return;
    }
    std::string url =
        std::string("https://firestore.googleapis.com/v1/projects/") +
        CONFIG_ECHOEAR_BABYMILU_FIRESTORE_PROJECT +
        "/databases/(default)/documents/devices/" +
        system_info::GetMacAddress() + "/";
    std::string body;
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = 10000;
    cfg.event_handler = CollectHttpBody;
    cfg.user_data = &body;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        return;
    }
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200 || body.empty()) {
        ESP_LOGW(TAG, "Firestore Wi-Fi ranking skipped err=%s status=%d", esp_err_to_name(err), status);
        return;
    }
    std::vector<std::string> ranked = ParseRankedNetworks(body);
    bool changed = WifiCredentialStore().ReorderByRankedSsids(ranked);
    ESP_LOGI(TAG, "Firestore rankedNetworks parsed=%u changed=%d",
             static_cast<unsigned>(ranked.size()), changed);
}
