#ifndef WIFI_BOARD_H
#define WIFI_BOARD_H

#include "board.h"
#include "ble_server.h"  // BLE server enabled
#include <cstdint>

class WifiBoard : public Board {
protected:
    bool wifi_config_mode_ = false;
    bool ble_initialized_ = false;  // BLE server enabled
    std::string temp_ssid_;  // Temporary storage for SSID during BLE configuration
    bool has_last_wifi_failure_ = false;
    int last_wifi_failure_reason_ = 0;
    int8_t last_wifi_failure_rssi_ = 0;
    std::string last_wifi_failure_ssid_;
    std::string last_wifi_failure_password_;
    std::string last_wifi_failure_code_;
    std::string last_wifi_failure_reason_name_;
    std::string last_wifi_failure_screen_message_;
    void EnterWifiConfigMode();
    void EnterWifiConfigModeViaBLE();  // Enter BLE config mode after WiFi disconnect
    void InitializeBleServer();  // BLE server enabled
    void ParseWifiCredentials(const char* data);  // BLE server enabled
    virtual std::string GetBoardJson() override;

public:
    WifiBoard();
    virtual ~WifiBoard();
    virtual std::string GetBoardType() override;
    virtual void StartNetwork() override;
    
    // BLE handler methods (enabled)
    void HandleBleData(const char* data, uint16_t length);
    void HandleBleConnection(bool connected);
    virtual Http* CreateHttp() override;
    virtual WebSocket* CreateWebSocket() override;
    virtual Mqtt* CreateMqtt() override;
    virtual Udp* CreateUdp() override;
    virtual const char* GetNetworkStateIcon() override;
    virtual void SetPowerSaveMode(bool enabled) override;
    virtual void ResetWifiConfiguration();
    virtual void ClearWifiConfiguration();
    virtual void EnterBleWifiConfigMode() override;
    virtual AudioCodec* GetAudioCodec() override { return nullptr; }
    virtual std::string GetDeviceStatusJson() override;
};

#endif // WIFI_BOARD_H
