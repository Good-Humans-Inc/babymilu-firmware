#include "wifi_station.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_err.h>
#include <esp_idf_version.h>
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

void AppendFlag(std::string& flags, const char* flag) {
    if (!flags.empty()) {
        flags += "/";
    }
    flags += flag;
}

std::string BssidToString(const uint8_t bssid[6]) {
    char buffer[18];
    snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return buffer;
}

const char* AuthModeName(wifi_auth_mode_t authmode) {
    switch (authmode) {
        case WIFI_AUTH_OPEN:
            return "OPEN";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA_WPA2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2_ENTERPRISE";
        case WIFI_AUTH_WPA3_PSK:
            return "WPA3_PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2_WPA3_PSK";
        case WIFI_AUTH_WAPI_PSK:
            return "WAPI_PSK";
        case WIFI_AUTH_OWE:
            return "OWE";
        case WIFI_AUTH_WPA3_ENT_192:
            return "WPA3_ENT_192";
        case WIFI_AUTH_WPA3_EXT_PSK:
            return "WPA3_EXT_PSK";
        case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE:
            return "WPA3_EXT_PSK_MIXED";
        case WIFI_AUTH_DPP:
            return "DPP";
        case WIFI_AUTH_WPA3_ENTERPRISE:
            return "WPA3_ENTERPRISE";
        case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
            return "WPA2_WPA3_ENTERPRISE";
        default:
            return "UNKNOWN";
    }
}

const char* CipherName(wifi_cipher_type_t cipher) {
    switch (cipher) {
        case WIFI_CIPHER_TYPE_NONE:
            return "NONE";
        case WIFI_CIPHER_TYPE_WEP40:
            return "WEP40";
        case WIFI_CIPHER_TYPE_WEP104:
            return "WEP104";
        case WIFI_CIPHER_TYPE_TKIP:
            return "TKIP";
        case WIFI_CIPHER_TYPE_CCMP:
            return "CCMP";
        case WIFI_CIPHER_TYPE_TKIP_CCMP:
            return "TKIP_CCMP";
        case WIFI_CIPHER_TYPE_AES_CMAC128:
            return "AES_CMAC128";
        case WIFI_CIPHER_TYPE_SMS4:
            return "SMS4";
        case WIFI_CIPHER_TYPE_GCMP:
            return "GCMP";
        case WIFI_CIPHER_TYPE_GCMP256:
            return "GCMP256";
        case WIFI_CIPHER_TYPE_AES_GMAC128:
            return "AES_GMAC128";
        case WIFI_CIPHER_TYPE_AES_GMAC256:
            return "AES_GMAC256";
        case WIFI_CIPHER_TYPE_UNKNOWN:
            return "UNKNOWN";
        default:
            return "UNKNOWN";
    }
}

const char* SecondChannelName(wifi_second_chan_t second) {
    switch (second) {
        case WIFI_SECOND_CHAN_NONE:
            return "HT20";
        case WIFI_SECOND_CHAN_ABOVE:
            return "HT40+";
        case WIFI_SECOND_CHAN_BELOW:
            return "HT40-";
        default:
            return "UNKNOWN";
    }
}

const char* DisconnectReasonName(uint8_t reason) {
    switch (static_cast<wifi_err_reason_t>(reason)) {
        case WIFI_REASON_NO_AP_FOUND:
            return "NO_AP_FOUND";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
            return "NO_AP_FOUND_W_COMPATIBLE_SECURITY";
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD";
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return "NO_AP_FOUND_IN_RSSI_THRESHOLD";
        case WIFI_REASON_AUTH_FAIL:
            return "AUTH_FAIL";
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return "802_1X_AUTH_FAILED";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            return "4WAY_HANDSHAKE_TIMEOUT";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "HANDSHAKE_TIMEOUT";
        case WIFI_REASON_ASSOC_FAIL:
            return "ASSOC_FAIL";
        case WIFI_REASON_ASSOC_EXPIRE:
            return "ASSOC_EXPIRE";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "BEACON_TIMEOUT";
        case WIFI_REASON_CONNECTION_FAIL:
            return "CONNECTION_FAIL";
        case WIFI_REASON_TIMEOUT:
            return "TIMEOUT";
        case WIFI_REASON_ASSOC_LEAVE:
            return "ASSOC_LEAVE";
        case WIFI_REASON_AUTH_LEAVE:
            return "AUTH_LEAVE";
        case WIFI_REASON_ROAMING:
            return "ROAMING";
        default:
            return "UNKNOWN";
    }
}

std::string ProtocolMaskToString(uint8_t protocol) {
    std::string flags;
    if (protocol & WIFI_PROTOCOL_11B) {
        AppendFlag(flags, "11b");
    }
    if (protocol & WIFI_PROTOCOL_11G) {
        AppendFlag(flags, "11g");
    }
    if (protocol & WIFI_PROTOCOL_11N) {
        AppendFlag(flags, "11n");
    }
    if (protocol & WIFI_PROTOCOL_LR) {
        AppendFlag(flags, "lr");
    }
#ifdef WIFI_PROTOCOL_11A
    if (protocol & WIFI_PROTOCOL_11A) {
        AppendFlag(flags, "11a");
    }
#endif
#ifdef WIFI_PROTOCOL_11AC
    if (protocol & WIFI_PROTOCOL_11AC) {
        AppendFlag(flags, "11ac");
    }
#endif
#ifdef WIFI_PROTOCOL_11AX
    if (protocol & WIFI_PROTOCOL_11AX) {
        AppendFlag(flags, "11ax");
    }
#endif
    return flags.empty() ? "none" : flags;
}

std::string ApPhyFlags(const wifi_ap_record_t& ap) {
    std::string flags;
    if (ap.phy_11b) {
        AppendFlag(flags, "11b");
    }
    if (ap.phy_11g) {
        AppendFlag(flags, "11g");
    }
    if (ap.phy_11n) {
        AppendFlag(flags, "11n");
    }
    if (ap.phy_lr) {
        AppendFlag(flags, "lr");
    }
    if (ap.phy_11a) {
        AppendFlag(flags, "11a");
    }
    if (ap.phy_11ac) {
        AppendFlag(flags, "11ac");
    }
    if (ap.phy_11ax) {
        AppendFlag(flags, "11ax");
    }
    return flags.empty() ? "unknown" : flags;
}

std::string FormatApDiagnostics(const wifi_ap_record_t& ap) {
    char buffer[384];
    std::string bssid = BssidToString(ap.bssid);
    std::string phy = ApPhyFlags(ap);
    snprintf(buffer, sizeof(buffer),
             "bssid=%s channel=%u/%s rssi=%d auth=%s(%d) pairwise=%s group=%s phy=%s ax=%u wps=%u ftm_resp=%u",
             bssid.c_str(),
             ap.primary,
             SecondChannelName(ap.second),
             ap.rssi,
             AuthModeName(ap.authmode),
             static_cast<int>(ap.authmode),
             CipherName(ap.pairwise_cipher),
             CipherName(ap.group_cipher),
             phy.c_str(),
             static_cast<unsigned>(ap.phy_11ax),
             static_cast<unsigned>(ap.wps),
             static_cast<unsigned>(ap.ftm_responder));
    return buffer;
}

WifiApRecord BuildWifiApRecord(const wifi_ap_record_t& ap,
                               const std::string& password,
                               size_t ssid_len) {
    WifiApRecord record;
    record.ssid = std::string(reinterpret_cast<const char*>(ap.ssid), ssid_len);
    record.password = password;
    record.channel = ap.primary;
    record.rssi = ap.rssi;
    record.authmode = ap.authmode;
    record.second = ap.second;
    record.pairwise_cipher = ap.pairwise_cipher;
    record.group_cipher = ap.group_cipher;
    record.phy_11b = ap.phy_11b != 0;
    record.phy_11g = ap.phy_11g != 0;
    record.phy_11n = ap.phy_11n != 0;
    record.phy_lr = ap.phy_lr != 0;
    record.phy_11ax = ap.phy_11ax != 0;
    record.diagnostics = FormatApDiagnostics(ap);
    memcpy(record.bssid, ap.bssid, sizeof(record.bssid));
    return record;
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

    uint8_t protocol = 0;
    esp_err_t protocol_err = esp_wifi_get_protocol(WIFI_IF_STA, &protocol);
    if (protocol_err == ESP_OK) {
        ESP_LOGI(TAG,
                 "[WIFI_DIAG] station_started idf=%s local_protocols=%s protocol_mask=0x%02x remember_bssid=%u max_tx_power=%d",
                 esp_get_idf_version(),
                 ProtocolMaskToString(protocol).c_str(),
                 protocol,
                 remember_bssid_,
                 max_tx_power_);
    } else {
        ESP_LOGW(TAG,
                 "[WIFI_DIAG] station_started idf=%s protocol_read_failed=%s remember_bssid=%u max_tx_power=%d",
                 esp_get_idf_version(),
                 esp_err_to_name(protocol_err),
                 remember_bssid_,
                 max_tx_power_);
    }

    if (max_tx_power_ != 0) {
        ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(max_tx_power_));
    }

    // Setup the timer to scan WiFi
    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            esp_err_t err = esp_wifi_scan_start(nullptr, false);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "[WIFI_DIAG] periodic_scan_start_failed err=%s", esp_err_to_name(err));
            }
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
            ESP_LOGI(TAG,
                     "[WIFI_DIAG] reconnect_attempt=%lu retry=%d/%d ssid=%s current_attempt=\"%s\"",
                     static_cast<unsigned long>(self->current_attempt_id_),
                     self->reconnect_count_,
                     MAX_RECONNECT_COUNT,
                     self->ssid_.c_str(),
                     self->current_attempt_diagnostics_.c_str());
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
    const uint32_t scan_id = ++scan_count_;
    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)calloc(ap_num == 0 ? 1 : ap_num, sizeof(wifi_ap_record_t));
    if (ap_records == nullptr) {
        ESP_LOGE(TAG, "[WIFI_DIAG] scan=%lu failed to allocate records for %u APs",
                 static_cast<unsigned long>(scan_id),
                 ap_num);
        esp_timer_start_once(timer_handle_, 10 * 1000);
        return;
    }
    esp_err_t records_err = esp_wifi_scan_get_ap_records(&ap_num, ap_records);
    if (records_err != ESP_OK) {
        ESP_LOGW(TAG, "[WIFI_DIAG] scan=%lu read_records_failed err=%s",
                 static_cast<unsigned long>(scan_id),
                 esp_err_to_name(records_err));
        free(ap_records);
        esp_timer_start_once(timer_handle_, 10 * 1000);
        return;
    }
    ESP_LOGI(TAG, "[WIFI_DIAG] scan=%lu done total_aps=%u",
             static_cast<unsigned long>(scan_id),
             ap_num);

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
        persisted_creds += "],\"pwd_len\":";
        persisted_creds += std::to_string(persisted_ssid_list[i].password.size());
        persisted_creds += "}";
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
        effective_creds += "],\"pwd_len\":";
        effective_creds += std::to_string(ssid_list[i].password.size());
        effective_creds += "}";
    }
    effective_creds += "]";
    ESP_LOGI(TAG, "Effective credentials for this boot (connection order, hex): %s", effective_creds.c_str());

    // Build connection queue by stored credential order (priority list),
    // not by AP RSSI. Use exact SSID byte match only.
    uint16_t matched_candidate_count = 0;
    uint16_t ax_candidate_count = 0;
    for (const auto& item : ssid_list) {
        wifi_ap_record_t* it = ap_records + ap_num;
        size_t saved_index = &item - ssid_list.data();
        int candidate_index = 0;
        for (auto ap_it = ap_records; ap_it != ap_records + ap_num; ++ap_it) {
            size_t scanned_ssid_len = strnlen(reinterpret_cast<const char*>(ap_it->ssid), sizeof(ap_it->ssid));
            if (scanned_ssid_len == item.ssid.size() &&
                memcmp(ap_it->ssid, item.ssid.data(), scanned_ssid_len) == 0) {
                matched_candidate_count++;
                if (ap_it->phy_11ax) {
                    ax_candidate_count++;
                }
                ESP_LOGI(TAG,
                         "[WIFI_DIAG] scan=%lu candidate saved_index=%u candidate_index=%d ssid=%s %s",
                         static_cast<unsigned long>(scan_id),
                         static_cast<unsigned>(saved_index),
                         candidate_index,
                         item.ssid.c_str(),
                         FormatApDiagnostics(*ap_it).c_str());
                if (it == ap_records + ap_num) {
                    it = ap_it;
                }
                candidate_index++;
            }
        }
        if (it != ap_records + ap_num) {
            auto ap_record = *it;
            ESP_LOGI(TAG, "Selected AP for credential: %s %s",
                     item.ssid.c_str(),
                     FormatApDiagnostics(ap_record).c_str());
            size_t scanned_ssid_len = strnlen(reinterpret_cast<const char*>(ap_record.ssid), sizeof(ap_record.ssid));
            LogSsidBytes("Found AP SSID bytes",
                         reinterpret_cast<const uint8_t*>(ap_record.ssid),
                         scanned_ssid_len);
            LogSsidBytes("Stored SSID bytes",
                         reinterpret_cast<const uint8_t*>(item.ssid.data()),
                         item.ssid.size());
            WifiApRecord record = BuildWifiApRecord(ap_record, item.password, scanned_ssid_len);
            connect_queue_.push_back(record);
        }
    }
    ESP_LOGI(TAG,
             "[WIFI_DIAG] scan=%lu summary total_aps=%u saved_credentials=%u matched_candidates=%u ax_candidates=%u connect_queue=%u",
             static_cast<unsigned long>(scan_id),
             ap_num,
             static_cast<unsigned>(ssid_list.size()),
             matched_candidate_count,
             ax_candidate_count,
             static_cast<unsigned>(connect_queue_.size()));
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
    current_attempt_id_ = ++connect_attempt_count_;
    current_attempt_diagnostics_ = ap_record.diagnostics;

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
    ESP_LOGI(TAG,
             "[WIFI_DIAG] connect_attempt=%lu ssid=%s password_len=%u %s remember_bssid=%u pmf_capable=%u pmf_required=%u threshold_auth=%s(%d)",
             static_cast<unsigned long>(current_attempt_id_),
             ssid_.c_str(),
             static_cast<unsigned>(password_.size()),
             current_attempt_diagnostics_.c_str(),
             remember_bssid_,
             static_cast<unsigned>(wifi_config.sta.pmf_cfg.capable),
             static_cast<unsigned>(wifi_config.sta.pmf_cfg.required),
             AuthModeName(wifi_config.sta.threshold.authmode),
             static_cast<int>(wifi_config.sta.threshold.authmode));
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
        esp_err_t err = esp_wifi_scan_start(nullptr, false);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "[WIFI_DIAG] initial_scan_start_failed err=%s", esp_err_to_name(err));
        }
        if (this_->on_scan_begin_) {
            this_->on_scan_begin_();
        }
    } else if (event_id == WIFI_EVENT_SCAN_DONE) {
        this_->HandleScanResult();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        auto* disc = static_cast<wifi_event_sta_disconnected_t*>(event_data);
        if (disc != nullptr) {
            std::string event_ssid(reinterpret_cast<const char*>(disc->ssid), disc->ssid_len);
            ESP_LOGW(TAG,
                     "[WIFI_DIAG] disconnect attempt=%lu ssid=%s event_bssid=%s reason=%u reason_name=%s rssi=%d current_attempt=\"%s\"",
                     static_cast<unsigned long>(this_->current_attempt_id_),
                     event_ssid.c_str(),
                     BssidToString(disc->bssid).c_str(),
                     disc->reason,
                     DisconnectReasonName(disc->reason),
                     disc->rssi,
                     this_->current_attempt_diagnostics_.c_str());
            ESP_LOGI(TAG,
                     "Disconnected from %s during connection retry, reason=%d, rssi=%d",
                     event_ssid.c_str(),
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
        auto* conn = static_cast<wifi_event_sta_connected_t*>(event_data);
        if (conn != nullptr) {
            std::string event_ssid(reinterpret_cast<const char*>(conn->ssid), conn->ssid_len);
            ESP_LOGI(TAG,
                     "[WIFI_DIAG] sta_connected attempt=%lu ssid=%s bssid=%s channel=%u auth=%s(%d) aid=%u current_attempt=\"%s\"",
                     static_cast<unsigned long>(this_->current_attempt_id_),
                     event_ssid.c_str(),
                     BssidToString(conn->bssid).c_str(),
                     conn->channel,
                     AuthModeName(conn->authmode),
                     static_cast<int>(conn->authmode),
                     conn->aid,
                     this_->current_attempt_diagnostics_.c_str());
        }
    }
}

void WifiStation::IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    auto* this_ = static_cast<WifiStation*>(arg);
    auto* event = static_cast<ip_event_got_ip_t*>(event_data);

    char ip_address[16];
    esp_ip4addr_ntoa(&event->ip_info.ip, ip_address, sizeof(ip_address));
    this_->ip_address_ = ip_address;
    ESP_LOGI(TAG, "Got IP: %s", this_->ip_address_.c_str());

    wifi_ap_record_t ap_info;
    esp_err_t ap_info_err = esp_wifi_sta_get_ap_info(&ap_info);
    if (ap_info_err == ESP_OK) {
        ESP_LOGI(TAG,
                 "[WIFI_DIAG] got_ip attempt=%lu ip=%s connected_ap %s",
                 static_cast<unsigned long>(this_->current_attempt_id_),
                 this_->ip_address_.c_str(),
                 FormatApDiagnostics(ap_info).c_str());
    } else {
        ESP_LOGW(TAG,
                 "[WIFI_DIAG] got_ip attempt=%lu ip=%s ap_info_failed=%s",
                 static_cast<unsigned long>(this_->current_attempt_id_),
                 this_->ip_address_.c_str(),
                 esp_err_to_name(ap_info_err));
    }
    
    xEventGroupSetBits(this_->event_group_, WIFI_EVENT_CONNECTED);
    if (this_->on_connected_) {
        this_->on_connected_(this_->ssid_);
    }
    this_->connect_queue_.clear();
    this_->reconnect_count_ = 0;
}
