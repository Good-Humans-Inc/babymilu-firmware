#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <esp_event.h>
#include <esp_timer.h>
#include <esp_wifi_types_generic.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include "ble_wifi_provisioner.h"

struct WifiCredential {
    std::string ssid;
    std::string password;
};

class WifiCredentialStore {
public:
    std::vector<WifiCredential> Load() const;
    void Save(const std::vector<WifiCredential>& credentials) const;
    void Clear() const;
    void AddLowestPriority(const std::string& ssid, const std::string& password) const;
    bool ReorderByRankedSsids(const std::vector<std::string>& ranked_ssids) const;
};

class WifiManager {
public:
    using ConnectedCallback = std::function<void()>;

    WifiManager();

    void Start();
    bool WaitForConnected(int timeout_ms);
    bool IsConnected() const;
    std::string ConnectedSsid() const { return connected_ssid_; }

    void OnConnected(ConnectedCallback callback) { on_connected_ = std::move(callback); }
    void SetPreferredSsidForNextConnect(const std::string& ssid);
    void ApplyPreferredSsidFromSettings();
    void ClearCredentialsAndRequestOnboarding();
    void RequestBleReconfigure();
    void FetchFirestoreRankingAndApply();

private:
    struct Candidate {
        std::string ssid;
        std::string password;
        int channel = 0;
        wifi_auth_mode_t authmode = WIFI_AUTH_OPEN;
        uint8_t bssid[6] = {};
    };

    static void WifiEventHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data);
    static void IpEventHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data);
    static void ConnectedCallbackTask(void* arg);

    void HandleScanDone();
    void ConnectNext();
    void ScheduleScan(int delay_ms);
    void NotifyConnectedAsync();
    void LoadCredentialsForBoot();
    void StartBleProvisioning(const char* reason);
    void SaveBleCredential(const std::string& ssid, const std::string& password);
    std::vector<WifiCredential> EffectiveCredentials();
    std::vector<std::string> ParseRankedNetworks(const std::string& firestore_json) const;

    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t scan_timer_ = nullptr;
    esp_event_handler_instance_t wifi_event_instance_ = nullptr;
    esp_event_handler_instance_t ip_event_instance_ = nullptr;
    BleWifiProvisioner ble_provisioner_;
    std::vector<WifiCredential> credentials_;
    std::vector<Candidate> queue_;
    std::string preferred_ssid_once_;
    std::string connected_ssid_;
    bool force_ble_config_ = false;
    TaskHandle_t connected_callback_task_ = nullptr;
    int reconnect_count_ = 0;
    ConnectedCallback on_connected_;
};
