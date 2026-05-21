#include "wifi_station.h"
#include <cstring>
#include <algorithm>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs.h>
#include "nvs_flash.h"
#include <esp_netif.h>
#include <esp_system.h>
#include "ssid_manager.h"

#define TAG "wifi"
#define WIFI_EVENT_CONNECTED BIT0
#define MAX_RECONNECT_COUNT 3
#define RECONNECT_INTERVAL_MS 500

static std::string ToHexBytes(const char* input) {
    std::string out;
    if (input == nullptr) {
        return out;
    }
    while (*input) {
        if (!out.empty()) {
            out += " ";
        }
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X", static_cast<unsigned char>(*input));
        out += buf;
        ++input;
    }
    return out;
}

namespace {

std::string BytesToHex(const uint8_t* data, size_t len) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string hex;
    if (len == 0) {
        return hex;
    }
    hex.reserve(len * 3 - 1);
    for (size_t i = 0; i < len; ++i) {
        if (i > 0) {
            hex.push_back(' ');
        }
        hex.push_back(kHex[data[i] >> 4]);
        hex.push_back(kHex[data[i] & 0x0F]);
    }
    return hex;
}

void LogSsidBytes(const char* label, const uint8_t* ssid, size_t ssid_len) {
    ESP_LOGI(TAG, "%s len=%u hex=[%s]",
             label,
             static_cast<unsigned>(ssid_len),
             BytesToHex(ssid, ssid_len).c_str());
}

}  // namespace

WifiStation& WifiStation::GetInstance() {
    static WifiStation instance;
    return instance;
}

WifiStation::WifiStation() {
    // Create the event group
    event_group_ = xEventGroupCreate();

    // 读取配置
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("wifi", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
    }
    err = nvs_get_i8(nvs, "max_tx_power", &max_tx_power_);
    if (err != ESP_OK) {
        max_tx_power_ = 0;
    }
    err = nvs_get_u8(nvs, "remember_bssid", &remember_bssid_);
    if (err != ESP_OK) {
        remember_bssid_ = 0;
    }
    nvs_close(nvs);
}

WifiStation::~WifiStation() {
    vEventGroupDelete(event_group_);
}

void WifiStation::AddAuth(const std::string &&ssid, const std::string &&password) {
    auto& ssid_manager = SsidManager::GetInstance();
    ssid_manager.AddSsid(ssid, password);
}

void WifiStation::Stop() {
    if (timer_handle_ != nullptr) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
    }
    if (reconnect_timer_handle_ != nullptr) {
        esp_timer_stop(reconnect_timer_handle_);
        esp_timer_delete(reconnect_timer_handle_);
        reconnect_timer_handle_ = nullptr;
    }
    
    // 取消注册事件处理程序
    if (instance_any_id_ != nullptr) {
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_));
        instance_any_id_ = nullptr;
    }
    if (instance_got_ip_ != nullptr) {
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_));
        instance_got_ip_ = nullptr;
    }

    // Reset the WiFi stack
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());
}

void WifiStation::OnScanBegin(std::function<void()> on_scan_begin) {
    on_scan_begin_ = on_scan_begin;
}

void WifiStation::OnConnect(std::function<void(const std::string& ssid)> on_connect) {
    on_connect_ = on_connect;
}

void WifiStation::OnConnected(std::function<void(const std::string& ssid)> on_connected) {
    on_connected_ = on_connected;
}

void WifiStation::OnDisconnected(std::function<void(const std::string& ssid, wifi_err_reason_t reason, int8_t rssi)> on_disconnected) {
    on_disconnected_ = on_disconnected;
}

void WifiStation::Start() {
    // Initialize the TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiStation::WifiEventHandler,
                                                        this,
                                                        &instance_any_id_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WifiStation::IpEventHandler,
                                                        this,
                                                        &instance_got_ip_));

    // Create the default event loop
    esp_netif_create_default_wifi_sta();

    // Initialize the WiFi stack in station mode
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (max_tx_power_ != 0) {
        ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(max_tx_power_));
    }

    // Setup the timer to scan WiFi
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            esp_wifi_scan_start(nullptr, false);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "WiFiScanTimer",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));

    // Setup reconnect timer to delay retries for the same SSID.
    esp_timer_create_args_t reconnect_timer_args = {
        .callback = [](void* arg) {
            auto* self = static_cast<WifiStation*>(arg);
            ESP_LOGI(TAG, "Retrying connection to %s now", self->ssid_.c_str());
            esp_wifi_connect();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "WiFiReconnectTimer",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&reconnect_timer_args, &reconnect_timer_handle_));
}

bool WifiStation::WaitForConnected(int timeout_ms) {
    auto bits = xEventGroupWaitBits(event_group_, WIFI_EVENT_CONNECTED, pdFALSE, pdFALSE, timeout_ms / portTICK_PERIOD_MS);
    return (bits & WIFI_EVENT_CONNECTED) != 0;
}

void WifiStation::HandleScanResult() {
    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(ap_num * sizeof(wifi_ap_record_t));
    esp_wifi_scan_get_ap_records(&ap_num, ap_records);

    // Debug: print visible AP SSIDs in raw hex-byte format.
    std::string visible_aps = "[";
    for (int i = 0; i < ap_num; i++) {
        if (i > 0) {
            visible_aps += ", ";
        }
        visible_aps += "[";
        visible_aps += ToHexBytes((const char*)ap_records[i].ssid);
        visible_aps += "]";
    }
    visible_aps += "]";
    ESP_LOGI(TAG, "Scan visible AP SSIDs (hex): %s", visible_aps.c_str());

    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    const auto persisted_ssid_list = ssid_list;

    // One-shot override used by switch_wifi_to: only affects this boot's
    // connection attempt order and does not modify persisted credential order.
    if (!preferred_ssid_once_.empty()) {
        auto preferred_it = std::find_if(ssid_list.begin(), ssid_list.end(),
            [this](const SsidItem& item) {
                return item.ssid == preferred_ssid_once_;
            });
        if (preferred_it != ssid_list.end() && preferred_it != ssid_list.begin()) {
            SsidItem preferred = *preferred_it;
            ssid_list.erase(preferred_it);
            ssid_list.insert(ssid_list.begin(), preferred);
            ESP_LOGI(TAG, "Applying one-shot preferred SSID first: %s", preferred_ssid_once_.c_str());
        } else if (preferred_it == ssid_list.end()) {
            ESP_LOGW(TAG, "One-shot preferred SSID not found in saved list: %s", preferred_ssid_once_.c_str());
        }
        preferred_ssid_once_.clear();
    }

    // Debug: print persisted credentials from NVS (actual stored priority).
    std::string persisted_creds = "[";
    for (size_t i = 0; i < persisted_ssid_list.size(); ++i) {
        if (i > 0) {
            persisted_creds += ", ";
        }
        persisted_creds += "{\"ssid_hex\":[";
        persisted_creds += ToHexBytes(persisted_ssid_list[i].ssid.c_str());
        persisted_creds += "],\"pwd_hex\":[";
        persisted_creds += ToHexBytes(persisted_ssid_list[i].password.c_str());
        persisted_creds += "]}";
    }
    persisted_creds += "]";
    ESP_LOGI(TAG, "Stored credentials (NVS persisted priority, hex): %s", persisted_creds.c_str());

    // Debug: print effective credentials used for this boot's connection queue.
    std::string effective_creds = "[";
    for (size_t i = 0; i < ssid_list.size(); ++i) {
        if (i > 0) {
            effective_creds += ", ";
        }
        effective_creds += "{\"ssid_hex\":[";
        effective_creds += ToHexBytes(ssid_list[i].ssid.c_str());
        effective_creds += "],\"pwd_hex\":[";
        effective_creds += ToHexBytes(ssid_list[i].password.c_str());
        effective_creds += "]}";
    }
    effective_creds += "]";
    ESP_LOGI(TAG, "Effective credentials for this boot (connection order, hex): %s", effective_creds.c_str());

    // Build connection queue by stored credential order (priority list),
    // not by AP RSSI. Use exact SSID byte match only.
    for (const auto& item : ssid_list) {
        wifi_ap_record_t* it = ap_records + ap_num;
        for (auto ap_it = ap_records; ap_it != ap_records + ap_num; ++ap_it) {
            size_t scanned_ssid_len = strnlen(reinterpret_cast<const char*>(ap_it->ssid), sizeof(ap_it->ssid));
            if (scanned_ssid_len == item.ssid.size() &&
                memcmp(ap_it->ssid, item.ssid.data(), scanned_ssid_len) == 0) {
                it = ap_it;
                break;
            }
        }
        if (it != ap_records + ap_num) {
            auto ap_record = *it;
            ESP_LOGI(TAG, "Found AP: %s, BSSID: %02x:%02x:%02x:%02x:%02x:%02x, RSSI: %d, Channel: %d, Authmode: %d",
                (char *)ap_record.ssid, 
                ap_record.bssid[0], ap_record.bssid[1], ap_record.bssid[2],
                ap_record.bssid[3], ap_record.bssid[4], ap_record.bssid[5],
                ap_record.rssi, ap_record.primary, ap_record.authmode);
            size_t scanned_ssid_len = strnlen(reinterpret_cast<const char*>(ap_record.ssid), sizeof(ap_record.ssid));
            LogSsidBytes("Found AP SSID bytes",
                         reinterpret_cast<const uint8_t*>(ap_record.ssid),
                         scanned_ssid_len);
            LogSsidBytes("Stored SSID bytes",
                         reinterpret_cast<const uint8_t*>(item.ssid.data()),
                         item.ssid.size());
            WifiApRecord record = {
                .ssid = std::string(reinterpret_cast<const char*>(ap_record.ssid), scanned_ssid_len),
                .password = item.password,
                .channel = ap_record.primary,
                .authmode = ap_record.authmode
            };
            memcpy(record.bssid, ap_record.bssid, 6);
            connect_queue_.push_back(record);
        }
    }
    free(ap_records);

    if (connect_queue_.empty()) {
        ESP_LOGI(TAG, "Wait for next scan");
        esp_timer_start_once(timer_handle_, 10 * 1000);
        return;
    }

    StartConnect();
}

void WifiStation::StartConnect() {
    auto ap_record = connect_queue_.front();
    connect_queue_.erase(connect_queue_.begin());
    ssid_ = ap_record.ssid;
    password_ = ap_record.password;

    if (on_connect_) {
        on_connect_(ssid_);
    }

    wifi_config_t wifi_config;
    bzero(&wifi_config, sizeof(wifi_config));
    strcpy((char *)wifi_config.sta.ssid, ap_record.ssid.c_str());
    strcpy((char *)wifi_config.sta.password, ap_record.password.c_str());
    // Compatibility profile for consumer hotspots (including Windows hotspot):
    // - allow WPA/WPA2 and above
    // - support PMF when AP offers it, but do not require PMF
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    LogSsidBytes("Target SSID bytes before connect",
                 reinterpret_cast<const uint8_t*>(wifi_config.sta.ssid),
                 strnlen(reinterpret_cast<const char*>(wifi_config.sta.ssid), sizeof(wifi_config.sta.ssid)));
    if (remember_bssid_) {
        wifi_config.sta.channel = ap_record.channel;
        memcpy(wifi_config.sta.bssid, ap_record.bssid, 6);
        wifi_config.sta.bssid_set = true;
    }
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    reconnect_count_ = 0;
    ESP_ERROR_CHECK(esp_wifi_connect());
}

int8_t WifiStation::GetRssi() {
    // Get station info
    wifi_ap_record_t ap_info;
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));
    return ap_info.rssi;
}

uint8_t WifiStation::GetChannel() {
    // Get station info
    wifi_ap_record_t ap_info;
    ESP_ERROR_CHECK(esp_wifi_sta_get_ap_info(&ap_info));
    return ap_info.primary;
}

bool WifiStation::IsConnected() {
    return xEventGroupGetBits(event_group_) & WIFI_EVENT_CONNECTED;
}

void WifiStation::SetPreferredSsidForNextConnect(const std::string& ssid) {
    preferred_ssid_once_ = ssid;
    ESP_LOGI(TAG, "Set one-shot preferred SSID: %s",
             preferred_ssid_once_.empty() ? "<none>" : preferred_ssid_once_.c_str());
}

void WifiStation::SetPowerSaveMode(bool enabled) {
    ESP_ERROR_CHECK(esp_wifi_set_ps(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE));
}

// Static event handler functions
void WifiStation::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_scan_start(nullptr, false);
        if (this_->on_scan_begin_) {
            this_->on_scan_begin_();
        }
    } else if (event_id == WIFI_EVENT_SCAN_DONE) {
        this_->HandleScanResult();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        auto* disc = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        if (disc != nullptr) {
            ESP_LOGW(TAG,
                     "Disconnected from %s, reason=%d, rssi=%d",
                     reinterpret_cast<const char*>(disc->ssid),
                     disc->reason,
                     disc->rssi);
            LogSsidBytes("Disconnected SSID bytes",
                         reinterpret_cast<const uint8_t*>(disc->ssid),
                         disc->ssid_len);
            if (this_->on_disconnected_) {
                this_->on_disconnected_(this_->ssid_, static_cast<wifi_err_reason_t>(disc->reason), disc->rssi);
            }
        }
        xEventGroupClearBits(this_->event_group_, WIFI_EVENT_CONNECTED);
        if (this_->reconnect_count_ < MAX_RECONNECT_COUNT) {
            this_->reconnect_count_++;
            ESP_LOGI(TAG, "Reconnecting %s (attempt %d / %d) in %d seconds",
                     this_->ssid_.c_str(),
                     this_->reconnect_count_,
                     MAX_RECONNECT_COUNT,
                     RECONNECT_INTERVAL_MS / 1000);
            esp_timer_stop(this_->reconnect_timer_handle_);
            esp_timer_start_once(this_->reconnect_timer_handle_, (uint64_t)RECONNECT_INTERVAL_MS * 1000);
            return;
        }

        if (!this_->connect_queue_.empty()) {
            this_->StartConnect();
            return;
        }
        
        ESP_LOGI(TAG, "No more AP to connect, wait for next scan");
        esp_timer_start_once(this_->timer_handle_, 10 * 1000);
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
    }
}

void WifiStation::IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    auto* event = static_cast<ip_event_got_ip_t*>(event_data);

    char ip_address[16];
    esp_ip4addr_ntoa(&event->ip_info.ip, ip_address, sizeof(ip_address));
    this_->ip_address_ = ip_address;
    ESP_LOGI(TAG, "Got IP: %s", this_->ip_address_.c_str());
    
    xEventGroupSetBits(this_->event_group_, WIFI_EVENT_CONNECTED);
    if (this_->on_connected_) {
        this_->on_connected_(this_->ssid_);
    }
    this_->connect_queue_.clear();
    this_->reconnect_count_ = 0;
}
