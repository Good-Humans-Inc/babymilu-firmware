#pragma once

#include <functional>
#include <string>

class BleWifiProvisioner {
public:
    using CredentialCallback = std::function<void(const std::string& ssid, const std::string& password)>;

    void OnCredentials(CredentialCallback callback) { on_credentials_ = std::move(callback); }
    bool Start(const char* device_name);
    bool IsRunning() const { return initialized_; }

    static int GapEvent(struct ble_gap_event* event, void* arg);
    static int ReadAccess(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg);
    static int WriteAccess(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg);

private:
    static void HostTask(void* param);
    static void OnSync();
    static void RestartTask(void* param);

    void Advertise();
    void HandleWrite(std::string payload);
    void HandleCredential(const std::string& ssid, const std::string& password);
    void SetStatus(std::string status);
    std::string Status() const { return status_; }
    void RestartSoon();

    CredentialCallback on_credentials_;
    std::string pending_ssid_;
    std::string status_ = "Ready for WiFi configuration";
    bool initialized_ = false;
    bool advertising_ = false;
    bool connected_ = false;
    bool restart_scheduled_ = false;
    uint8_t addr_type_ = 0;
    uint16_t conn_handle_ = 0;
};
