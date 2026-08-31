#include "wifi_board.h"

#include "display.h"
#include "application.h"
#include "system_info.h"
#include "font_awesome_symbols.h"
#include "settings.h"
#include "assets/lang_config.h"
#include <freertos/task.h>
#include <esp_http.h>
#include <esp_mqtt.h>
#include <esp_udp.h>
#include <tcp_transport.h>
#include <tls_transport.h>
#include <web_socket.h>
#include <esp_log.h>
#include <esp_app_desc.h>
#include <esp_system.h>
#include <cJSON.h>
#include <memory>
#include <utility>

#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <ssid_manager.h>
#include "animation/animation.h"
#include "display/lcd_display.h"
#include "error_log_uploader.h"

static const char *TAG = "WifiBoard";

namespace {

constexpr const char* kProvisioningNamespace = "wifi_prov";
constexpr int kProvisioningPendingCloud = 1;
constexpr int kProvisioningBleFailure = 2;
constexpr int kProvisioningBm1Active = 3;

std::string BuildProvisioningStatus(
    const std::string& attempt_id,
    const char* status,
    const char* reason) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "type", "wifi.status");
    cJSON_AddStringToObject(root, "attemptId", attempt_id.c_str());
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddStringToObject(root, "reason", reason);
    char* encoded = cJSON_PrintUnformatted(root);
    std::string result = encoded == nullptr ? "" : encoded;
    cJSON_free(encoded);
    cJSON_Delete(root);
    return result;
}

void PublishBleValue(const std::string& value) {
    if (!value.empty()) {
        ble_server_send_data(value.data(), static_cast<uint16_t>(value.size()));
    }
}

void ClearProvisioningState() {
    Settings settings(kProvisioningNamespace, true);
    settings.EraseAll();
}

void SaveProvisioningState(
    int state,
    const std::string& protocol,
    const std::string& attempt_id,
    const std::string& report_token = "",
    const std::string& status = "",
    const std::string& reason = "",
    const std::string& target_ssid = "") {
    Settings settings(kProvisioningNamespace, true);
    settings.SetInt("state", state);
    settings.SetString("protocol", protocol);
    settings.SetString("attempt", attempt_id);
    if (!report_token.empty()) settings.SetString("token", report_token);
    else settings.EraseKey("token");
    if (!status.empty()) settings.SetString("status", status);
    else settings.EraseKey("status");
    if (!reason.empty()) settings.SetString("reason", reason);
    else settings.EraseKey("reason");
    if (!target_ssid.empty()) settings.SetString("target_ssid", target_ssid);
    else settings.EraseKey("target_ssid");
}

bool HasForbiddenControl(const char* value) {
    if (value == nullptr) return true;
    for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(value);
         *cursor != 0; ++cursor) {
        if (*cursor < 0x20 || *cursor == 0x7f) return true;
    }
    return false;
}

bool IsValidReportToken(const std::string& token) {
    if (token.size() < 32 || token.size() > 128) return false;
    for (char value : token) {
        if (!((value >= 'A' && value <= 'Z') ||
              (value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '_' || value == '-')) {
            return false;
        }
    }
    return true;
}

std::pair<const char*, const char*> ClassifyWifiFailure(const WifiStation& station) {
    const uint8_t reason = station.GetLastDisconnectReason();
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_ASSOC_FAIL:
            return {"auth_failed", "AUTH_FAILED"};
        default:
            break;
    }
    if (station.WasAssociated()) return {"dhcp_failed", "DHCP_TIMEOUT"};
    return {"auth_failed", "AP_NOT_FOUND"};
}

bool ProbeRuntimeEndpoint(WifiBoard* board) {
    const std::string url = CONFIG_OTA_URL;
    if (url.empty()) return false;
    auto http = std::unique_ptr<Http>(board->CreateHttp());
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Content-Type", "application/json");
    http->SetTimeout(15000);
    if (!http->Open("GET", url)) return false;
    const int status = http->GetStatusCode();
    http->Close();
    return status == 200;
}

}  // namespace

// Keep newly added WiFi credentials at the lowest priority so existing
// networks are tried first on startup.
static void SaveCredentialAsLowestPriority(const std::string& ssid, const std::string& password) {
    auto& ssid_manager = SsidManager::GetInstance();
    auto current = ssid_manager.GetSsidList();

    // Build target order: all existing (except same SSID) + new SSID at tail.
    struct Cred {
        std::string ssid;
        std::string password;
    };
    std::vector<Cred> target;
    target.reserve(current.size() + 1);
    for (const auto& item : current) {
        if (item.ssid != ssid) {
            target.push_back({item.ssid, item.password});
        }
    }
    target.push_back({ssid, password});

    // Rebuild list in reverse order to place target head first because
    // SsidManager::AddSsid currently gives newly added entries higher priority.
    ssid_manager.Clear();
    for (auto it = target.rbegin(); it != target.rend(); ++it) {
        ssid_manager.AddSsid(it->ssid, it->password);
    }
}

static bool ConsumeNextBleCredentialLowestFlag() {
    Settings settings("wifi", true);
    if (settings.GetInt("ble_cred_low") == 1) {
        settings.SetInt("ble_cred_low", 0);
        return true;
    }
    return false;
}

WifiBoard::WifiBoard() {
    Settings settings("wifi", true);
    // Comment out force_ap mode - use BLE instead
    // wifi_config_mode_ = settings.GetInt("force_ap") == 1;
    // if (wifi_config_mode_) {
    //     ESP_LOGI(TAG, "force_ap is set to 1, reset to 0");
    //     settings.SetInt("force_ap", 0);
    // }
    
    // Initialize BLE server only when needed (not in constructor)
    ble_initialized_ = false;
    wifi_config_mode_ = false;
}

WifiBoard::~WifiBoard() {
    if (ble_initialized_) {
        ble_server_deinit();
        ble_initialized_ = false;
    }
}

std::string WifiBoard::GetBoardType() {
    return "wifi";
}

void WifiBoard::EnterWifiConfigMode() {
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);

    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    wifi_ap.SetLanguage(Lang::CODE);
    wifi_ap.SetSsidPrefix("Xiaozhi");
    wifi_ap.Start();

    // 显示 WiFi 配置 AP 的 SSID 和 Web 服务器 URL
    std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
    hint += wifi_ap.GetSsid();
    hint += Lang::Strings::ACCESS_VIA_BROWSER;
    hint += wifi_ap.GetWebServerUrl();
    hint += "\n\n";
    
    // 播报配置 WiFi 的提示
    application.Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "", Lang::Sounds::P3_WIFICONFIG);
    
    // Wait forever until reset after configuration
    while (true) {
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void WifiBoard::EnterWifiConfigModeViaBLE() {
    ESP_LOGI(TAG, "WiFi disconnected, entering BLE configuration mode");
    
    // Stop WiFi station
    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.Stop();
    
    // Set config mode
    wifi_config_mode_ = true;
    
    // Initialize BLE server if not already done
    if (!ble_initialized_) {
        InitializeBleServer();
    }
    
    // Enable error logging to SD card during WiFi config mode
    ErrorLogUploader::EnableErrorLoggingToSD();
    
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);
    
    // Display BLE configuration instructions for reconnection
    std::string hint = "WiFi disconnected. Connect to BLE device 'BabyMilu' to reconfigure WiFi";
    application.Alert("WiFi Reconfiguration", hint.c_str(), "", Lang::Sounds::P3_WIFICONFIG);
    
    // Wait for BLE configuration
    while (wifi_config_mode_) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    
    // After BLE configuration, restart network with new credentials
    StartNetwork();
}



void WifiBoard::StartNetwork() {
    // Allow remote-triggered BLE onboarding after reboot without wiping credentials.
    {
        Settings settings("wifi", true);
        if (settings.GetInt("force_ble_cfg") == 1) {
            ESP_LOGI(TAG, "force_ble_cfg is set, entering BLE WiFi configuration mode");
            settings.SetInt("force_ble_cfg", 0);
            wifi_config_mode_ = true;

            if (!ble_initialized_) {
                InitializeBleServer();
            }

            ErrorLogUploader::EnableErrorLoggingToSD();

            auto& application = Application::GetInstance();
            application.SetDeviceState(kDeviceStateWifiConfiguring);

            std::string hint = "Connect to BLE device 'BabyMilu' to add WiFi credentials";
            application.Alert("WiFi Configuration", hint.c_str(), "", Lang::Sounds::P3_WIFICONFIG);

            while (wifi_config_mode_) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }

            // Credentials were updated in BLE flow; restart cleanly.
            ESP_LOGI(TAG, "BLE configuration complete from force_ble_cfg mode, restarting system");
            esp_restart();
            return;
        }
    }

    // If no WiFi SSID is configured, use BLE for WiFi configuration
    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    
    ESP_LOGI(TAG, "Stored WiFi credentials count: %d", ssid_list.size());
    
    if (ssid_list.empty()) {
        // No WiFi credentials found, use BLE for configuration
        wifi_config_mode_ = true;
        ESP_LOGI(TAG, "No WiFi credentials found, using BLE for configuration");
        
        // Initialize BLE server only when needed
        if (!ble_initialized_) {
            InitializeBleServer();
        }
        
        // Enable error logging to SD card during WiFi config mode
        ErrorLogUploader::EnableErrorLoggingToSD();
        
        auto& application = Application::GetInstance();
        application.SetDeviceState(kDeviceStateWifiConfiguring);
        
        // Display BLE configuration instructions
        std::string hint = "Connect to BLE device 'BabyMilu' to configure WiFi";
        application.Alert("WiFi Configuration", hint.c_str(), "", Lang::Sounds::P3_WIFICONFIG);
        
        // Show message to guide user to connect WiFi (display in center of screen)
        ESP_LOGI(TAG, "Attempting to display WiFi connection message...");
        auto display = Board::GetInstance().GetDisplay();
        if (display == nullptr) {
            ESP_LOGE(TAG, "Display is null! Cannot show WiFi connection message");
        } else {
            ESP_LOGI(TAG, "Display pointer is valid, attempting to set message");
            
            // Wait for display to be fully initialized (LVGL needs time)
            ESP_LOGI(TAG, "Waiting for display to be fully initialized...");
            vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds for LVGL to initialize
            
            const char* wifi_message = "Connect me to wifi with BabyMilu App. Can't wait to meet you again.";
            
            // Try to cast to LcdDisplay to use CreateSystemMessage
            // Use static_cast since we know the display type for LCD boards
            LcdDisplay* lcd_display = static_cast<LcdDisplay*>(display);
            if (lcd_display != nullptr) {
                ESP_LOGI(TAG, "Display is LcdDisplay, using CreateSystemMessage method");
                lcd_display->CreateSystemMessage(wifi_message);
                ESP_LOGI(TAG, "Called CreateSystemMessage");
            } else {
                ESP_LOGI(TAG, "Display is not LcdDisplay, using standard methods");
                // Try SetChatMessage (works if CONFIG_USE_WECHAT_MESSAGE_STYLE is enabled)
                display->SetChatMessage("system", wifi_message);
                ESP_LOGI(TAG, "Called SetChatMessage");
            }
        }
        
        // Wait for BLE configuration
        while (wifi_config_mode_) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        // After BLE configuration, restart the system for clean WiFi startup
        ESP_LOGI(TAG, "BLE configuration complete, restarting system for clean WiFi startup");
        esp_restart();
    }

    // Start WiFi station with existing credentials
    auto& wifi_station = WifiStation::GetInstance();
    {
        Settings settings("wifi", true);
        std::string preferred_ssid = settings.GetString("nxt_boot_ssid");
        if (!preferred_ssid.empty()) {
            bool exists = false;
            for (const auto& item : ssid_list) {
                if (item.ssid == preferred_ssid) {
                    exists = true;
                    break;
                }
            }
            if (exists) {
                ESP_LOGI(TAG, "Applying one-shot preferred SSID for this boot: %s", preferred_ssid.c_str());
                wifi_station.SetPreferredSsidForNextConnect(preferred_ssid);
            } else {
                ESP_LOGW(TAG, "Ignoring one-shot preferred SSID not found in saved list: %s", preferred_ssid.c_str());
            }
            settings.SetString("nxt_boot_ssid", "");
        }
    }

    wifi_station.OnScanBegin([this]() {
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
    });
    wifi_station.OnConnect([this](const std::string& ssid) {
        ESP_LOGI(TAG, "Connecting with SSID='%s'", ssid.c_str());

        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECT_TO;
        notification += ssid;
        notification += "...";
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.OnConnected([this](const std::string& ssid) {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECTED_TO;
        notification += ssid;
        display->ShowNotification(notification.c_str(), 30000);
        
        // Stop BLE server when WiFi is connected
        if (ble_initialized_) {
            ESP_LOGI(TAG, "WiFi connected, stopping BLE server");
            ble_server_stop_advertising();
            ble_server_deinit();
            ble_initialized_ = false;
        }
        
        // Check if animation is available, if not show connected message
        ESP_LOGI(TAG, "WiFi connected, checking animation availability...");
        Animation_t* current_anim = animation_get_normal_animation();
        ESP_LOGI(TAG, "Animation check: current_anim=%p", current_anim);
        if (current_anim != NULL) {
            ESP_LOGI(TAG, "Animation available: len=%d", current_anim->len);
        }
        
        if (current_anim == NULL || current_anim->len == 0) {
            ESP_LOGI(TAG, "No animation available, showing connected message");
            // No animation available, show connected message (display in center of screen)
            const char* connected_message = "Connected! I am traveling over :D";
            
            // Try to use LcdDisplay::CreateSystemMessage if available
            LcdDisplay* lcd_display = static_cast<LcdDisplay*>(display);
            if (lcd_display != nullptr) {
                ESP_LOGI(TAG, "Display is LcdDisplay, using CreateSystemMessage for connected message");
                lcd_display->CreateSystemMessage(connected_message);
            }
            
            // Also try standard methods as fallback
            display->SetChatMessage("system", connected_message);
            ESP_LOGI(TAG, "Called SetChatMessage with connected message");
            
            // Also try ShowNotification as fallback
            vTaskDelay(pdMS_TO_TICKS(100));
            display->ShowNotification(connected_message, 0);
            ESP_LOGI(TAG, "Called ShowNotification with connected message");
        } else {
            ESP_LOGI(TAG, "Animation is available, not showing connected message");
        }
    });
    
    // Note: OnDisconnected callback is not available in WifiStation class
    // Disconnection handling is done internally by WifiStation with automatic reconnection
    
    wifi_station.Start();

    // Try to connect to WiFi, if failed, use BLE for configuration
    // Keep this longer than per-SSID retry window (3 retries x 15s) so
    // WifiStation can finish retries before BLE fallback.
    if (!wifi_station.WaitForConnected(60 * 1000)) {
        {
            Settings provisioning(kProvisioningNamespace, false);
            if (provisioning.GetInt("state") == kProvisioningPendingCloud) {
                const std::string attempt_id = provisioning.GetString("attempt");
                const auto failure = ClassifyWifiFailure(wifi_station);
                SaveProvisioningState(
                    kProvisioningBleFailure,
                    "BM2",
                    attempt_id,
                    "",
                    failure.first,
                    failure.second);
            }
        }
        wifi_station.Stop();
        wifi_config_mode_ = true;
        ESP_LOGI(TAG, "WiFi connection failed, using BLE for configuration");
        
        // Initialize BLE server only when needed
        if (!ble_initialized_) {
            InitializeBleServer();
        }
        
        // Enable error logging to SD card during WiFi config mode
        ErrorLogUploader::EnableErrorLoggingToSD();
        
        auto& application = Application::GetInstance();
        application.SetDeviceState(kDeviceStateWifiConfiguring);
        
        // Display BLE configuration instructions
        std::string hint = "WiFi connection failed. Connect to BLE device 'BabyMilu' to configure WiFi";
        application.Alert("WiFi Configuration", hint.c_str(), "", Lang::Sounds::P3_WIFICONFIG);

        // Credentials already exist on this device: do NOT show the
        // "Connect me to wifi with BabyMilu App..." onboarding message.
        // The wifi.gif animation remains on screen instead.

        // Wait for BLE configuration
        while (wifi_config_mode_) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        // After BLE configuration, restart network with new credentials
        StartNetwork();
        return;
    }
}

Http* WifiBoard::CreateHttp() {
    return new EspHttp();
}

WebSocket* WifiBoard::CreateWebSocket() {
    Settings settings("websocket", false);
    std::string url = settings.GetString("url");
    if (url.find("wss://") == 0) {
        return new WebSocket(new TlsTransport());
    } else {
        return new WebSocket(new TcpTransport());
    }
    return nullptr;
}

Mqtt* WifiBoard::CreateMqtt() {
    return new EspMqtt();
}

Udp* WifiBoard::CreateUdp() {
    return new EspUdp();
}

const char* WifiBoard::GetNetworkStateIcon() {
    if (wifi_config_mode_) {
        return FONT_AWESOME_WIFI;
    }
    auto& wifi_station = WifiStation::GetInstance();
    if (!wifi_station.IsConnected()) {
        return FONT_AWESOME_WIFI_OFF;
    }
    int8_t rssi = wifi_station.GetRssi();
    if (rssi >= -60) {
        return FONT_AWESOME_WIFI;
    } else if (rssi >= -70) {
        return FONT_AWESOME_WIFI_FAIR;
    } else {
        return FONT_AWESOME_WIFI_WEAK;
    }
}

std::string WifiBoard::GetBoardJson() {
    // Set the board type for OTA
    auto& wifi_station = WifiStation::GetInstance();
    std::string board_json = std::string("{\"type\":\"" BOARD_TYPE "\",");
    board_json += "\"name\":\"" BOARD_NAME "\",";
    if (!wifi_config_mode_) {
        board_json += "\"ssid\":\"" + wifi_station.GetSsid() + "\",";
        board_json += "\"rssi\":" + std::to_string(wifi_station.GetRssi()) + ",";
        board_json += "\"channel\":" + std::to_string(wifi_station.GetChannel()) + ",";
        board_json += "\"ip\":\"" + wifi_station.GetIpAddress() + "\",";
    }
    board_json += "\"mac\":\"" + SystemInfo::GetMacAddress() + "\"}";
    return board_json;
}

void WifiBoard::SetPowerSaveMode(bool enabled) {
    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.SetPowerSaveMode(enabled);
}

void WifiBoard::ResetWifiConfiguration() {
    // Set a flag and reboot the device to enter the network configuration mode
    {
        Settings settings("wifi", true);
        settings.SetInt("force_ap", 1);
    }
    GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    // Reboot the device
    esp_restart();
}


void WifiBoard::ClearWifiConfiguration() {
    ESP_LOGI(TAG, "Clearing all WiFi configuration from NVS storage");
    
    // Clear all WiFi credentials using SsidManager
    auto& ssid_manager = SsidManager::GetInstance();
    ssid_manager.Clear();
    
    // Also clear any additional WiFi settings
    {
        Settings settings("wifi", true);
        settings.EraseAll();
    }
    
    // Clear websocket settings as well
    {
        Settings settings("websocket", true);
        settings.EraseAll();
    }
    
    ESP_LOGI(TAG, "WiFi configuration cleared successfully");
}

void WifiBoard::EnterBleWifiConfigMode() {
    ESP_LOGI(TAG, "Entering BLE WiFi config mode (keep existing credentials, reboot first)");

    // Persist intent and reboot into a clean boot path to avoid NimBLE runtime init
    // failures while network stacks are active.
    {
        Settings settings("wifi", true);
        settings.SetInt("force_ble_cfg", 1);
        settings.SetInt("ble_cred_low", 1);
    }

    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

std::string WifiBoard::GetDeviceStatusJson() {
    /*
     * 返回设备状态JSON
     * 
     * 返回的JSON结构如下：
     * {
     *     "audio_speaker": {
     *         "volume": 70
     *     },
     *     "screen": {
     *         "brightness": 100,
     *         "theme": "light"
     *     },
     *     "battery": {
     *         "level": 50,
     *         "charging": true
     *     },
     *     "network": {
     *         "type": "wifi",
     *         "ssid": "Xiaozhi",
     *         "rssi": -60
     *     },
     *     "chip": {
     *         "temperature": 25
     *     }
     * }
     */
    auto& board = Board::GetInstance();
    auto root = cJSON_CreateObject();

    // Audio speaker
    auto audio_speaker = cJSON_CreateObject();
    auto audio_codec = board.GetAudioCodec();
    if (audio_codec) {
        cJSON_AddNumberToObject(audio_speaker, "volume", audio_codec->output_volume());
    }
    cJSON_AddItemToObject(root, "audio_speaker", audio_speaker);

    // Screen brightness
    auto backlight = board.GetBacklight();
    auto screen = cJSON_CreateObject();
    if (backlight) {
        cJSON_AddNumberToObject(screen, "brightness", backlight->brightness());
    }
    auto display = board.GetDisplay();
    if (display && display->height() > 64) { // For LCD display only
        cJSON_AddStringToObject(screen, "theme", display->GetTheme().c_str());
    }
    cJSON_AddItemToObject(root, "screen", screen);

    // Battery
    int battery_level = 0;
    bool charging = false;
    bool discharging = false;
    if (board.GetBatteryLevel(battery_level, charging, discharging)) {
        cJSON* battery = cJSON_CreateObject();
        cJSON_AddNumberToObject(battery, "level", battery_level);
        cJSON_AddBoolToObject(battery, "charging", charging);
        cJSON_AddItemToObject(root, "battery", battery);
    }

    // Network
    auto network = cJSON_CreateObject();
    auto& wifi_station = WifiStation::GetInstance();
    cJSON_AddStringToObject(network, "type", "wifi");
    cJSON_AddStringToObject(network, "ssid", wifi_station.GetSsid().c_str());
    int rssi = wifi_station.GetRssi();
    if (rssi >= -60) {
        cJSON_AddStringToObject(network, "signal", "strong");
    } else if (rssi >= -70) {
        cJSON_AddStringToObject(network, "signal", "medium");
    } else {
        cJSON_AddStringToObject(network, "signal", "weak");
    }
    cJSON_AddItemToObject(root, "network", network);

    // Chip
    float esp32temp = 0.0f;
    if (board.GetTemperature(esp32temp)) {
        auto chip = cJSON_CreateObject();
        cJSON_AddNumberToObject(chip, "temperature", esp32temp);
        cJSON_AddItemToObject(root, "chip", chip);
    }

    auto json_str = cJSON_PrintUnformatted(root);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    return json;
}

// Static callback functions for BLE server
static WifiBoard* g_wifi_board_instance = nullptr;

static void ble_data_callback(const char* data, uint16_t length) {
    if (g_wifi_board_instance) {
        g_wifi_board_instance->HandleBleData(data, length);
    }
}

static void ble_connection_callback(bool connected) {
    if (g_wifi_board_instance) {
        g_wifi_board_instance->HandleBleConnection(connected);
    }
}

static void ble_read_callback() {
    if (g_wifi_board_instance) {
        g_wifi_board_instance->HandleBleRead();
    }
}

void WifiBoard::InitializeBleServer() {
    ESP_LOGI(TAG, "Initializing BLE server for WiFi configuration");
    
    // Store instance pointer for static callbacks
    g_wifi_board_instance = this;
    
    // Initialize BLE server with callbacks
    if (ble_server_init("BabyMilu", 
                       ble_data_callback,
                       ble_connection_callback,
                       ble_read_callback,
                       nullptr)) { // No device control callback for now
        ble_initialized_ = true;
        ble_server_start_advertising();
        ESP_LOGI(TAG, "BLE server initialized and advertising for WiFi config");
    } else {
        ESP_LOGE(TAG, "Failed to initialize BLE server");
    }
}

void WifiBoard::HandleBleData(const char* data, uint16_t length) {
    if (data == nullptr || length == 0) return;
    if ((length >= 4 && memcmp(data, "BM1|", 4) == 0) ||
        (length >= 4 && memcmp(data, "BM2|", 4) == 0)) {
        WifiProvisioningMessage message;
        const auto result = provisioning_reassembler_.Push(
            reinterpret_cast<const uint8_t*>(data), length, &message);
        if (result == WifiProvisioningFrameResult::kInvalid) {
            ESP_LOGW(TAG, "Rejected invalid WiFi provisioning frame");
        } else if (result == WifiProvisioningFrameResult::kComplete) {
            ParseProvisioningMessage(message);
        }
        return;
    }
    ParseWifiCredentials(data);
}

void WifiBoard::HandleBleConnection(bool connected) {
    if (connected) {
        ESP_LOGI(TAG, "BLE client connected");
        const std::string pending_status = PendingProvisioningStatus();
        if (!pending_status.empty()) {
            provisioning_failure_exposed_ = true;
            PublishBleValue(pending_status);
        } else {
            PublishBleValue(ProvisioningCapabilities());
        }
    } else {
        ESP_LOGI(TAG, "BLE client disconnected");
    }
}

void WifiBoard::HandleBleRead() {
    if (!provisioning_failure_exposed_) return;

    ESP_LOGI(TAG, "Acknowledged saved provisioning failure over BLE");
    ClearProvisioningState();
    provisioning_failure_exposed_ = false;
    PublishBleValue(ProvisioningCapabilities());
}

std::string WifiBoard::ProvisioningCapabilities() const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "type", "wifi.capabilities");
    cJSON_AddStringToObject(root, "deviceId", SystemInfo::GetMacAddress().c_str());
    cJSON* protocols = cJSON_AddArrayToObject(root, "protocols");
    cJSON_AddItemToArray(protocols, cJSON_CreateString("BM1"));
    cJSON_AddNumberToObject(root, "maxFrameBytes", 160);
    cJSON_AddNumberToObject(root, "maxLogicalBytes", 384);
    cJSON_AddNumberToObject(root, "maxFrames", 4);
    cJSON* supported = cJSON_AddArrayToObject(root, "supportedProtocols");
    cJSON_AddItemToArray(supported, cJSON_CreateString("BM2"));
    cJSON_AddItemToArray(supported, cJSON_CreateString("BM1"));
    cJSON_AddItemToArray(supported, cJSON_CreateString("legacy"));
    cJSON_AddStringToObject(root, "preferredProtocol", "BM2");
    cJSON* channels = cJSON_AddArrayToObject(root, "resultChannels");
    cJSON_AddItemToArray(channels, cJSON_CreateString("cloud"));
    cJSON_AddItemToArray(channels, cJSON_CreateString("ble"));
    cJSON_AddStringToObject(root, "firmwareVersion", esp_app_get_description()->version);
    char* encoded = cJSON_PrintUnformatted(root);
    std::string result = encoded == nullptr ? "" : encoded;
    cJSON_free(encoded);
    cJSON_Delete(root);
    return result;
}

std::string WifiBoard::PendingProvisioningStatus() const {
    Settings settings(kProvisioningNamespace, false);
    if (settings.GetInt("state") != kProvisioningBleFailure) return "";
    const std::string attempt_id = settings.GetString("attempt");
    const std::string status = settings.GetString("status");
    const std::string reason = settings.GetString("reason");
    if (!IsCanonicalProvisioningAttemptId(attempt_id) || status.empty() || reason.empty()) {
        return "";
    }
    return BuildProvisioningStatus(attempt_id, status.c_str(), reason.c_str());
}

bool WifiBoard::ParseProvisioningMessage(const WifiProvisioningMessage& message) {
    cJSON* root = cJSON_ParseWithLength(message.payload.data(), message.payload.size());
    const int expected_version = message.protocol == "BM2" ? 2 : 1;
    cJSON* version = root == nullptr ? nullptr : cJSON_GetObjectItem(root, "v");
    cJSON* type = root == nullptr ? nullptr : cJSON_GetObjectItem(root, "type");
    cJSON* attempt = root == nullptr ? nullptr : cJSON_GetObjectItem(root, "attemptId");
    cJSON* ssid_item = root == nullptr ? nullptr : cJSON_GetObjectItem(root, "ssid");
    cJSON* password_item = root == nullptr ? nullptr : cJSON_GetObjectItem(root, "password");
    cJSON* token_item = root == nullptr ? nullptr : cJSON_GetObjectItem(root, "reportToken");

    const bool base_valid =
        cJSON_IsNumber(version) && version->valueint == expected_version &&
        cJSON_IsString(type) && strcmp(type->valuestring, "wifi.provision") == 0 &&
        cJSON_IsString(attempt) && message.attempt_id == attempt->valuestring &&
        cJSON_IsString(ssid_item) && cJSON_IsString(password_item);
    const std::string ssid = base_valid ? ssid_item->valuestring : "";
    const std::string password = base_valid ? password_item->valuestring : "";
    const bool credentials_valid =
        base_valid && !ssid.empty() && ssid.size() <= 32 &&
        (password.empty() || (password.size() >= 8 && password.size() <= 63)) &&
        !HasForbiddenControl(ssid.c_str()) && !HasForbiddenControl(password.c_str());
    const std::string report_token =
        cJSON_IsString(token_item) ? token_item->valuestring : "";
    const bool token_valid =
        message.protocol == "BM1" || IsValidReportToken(report_token);

    if (!credentials_valid || !token_valid) {
        PublishBleValue(BuildProvisioningStatus(
            message.attempt_id, "invalid", "INVALID_REQUEST"));
        cJSON_Delete(root);
        return false;
    }

    if (ConsumeNextBleCredentialLowestFlag()) {
        SaveCredentialAsLowestPriority(ssid, password);
    } else {
        SsidManager::GetInstance().AddSsid(ssid, password);
    }
    {
        Settings wifi("wifi", true);
        wifi.SetString("nxt_boot_ssid", ssid);
    }

    if (message.protocol == "BM2") {
        SaveProvisioningState(
            kProvisioningPendingCloud,
            "BM2",
            message.attempt_id,
            report_token,
            "",
            "",
            ssid);
        PublishBleValue(BuildProvisioningStatus(
            message.attempt_id, "accepted", "CREDENTIALS_STAGED"));
        wifi_config_mode_ = false;
        cJSON_Delete(root);
        vTaskDelay(pdMS_TO_TICKS(750));
        esp_restart();
        return true;
    }

    SaveProvisioningState(
        kProvisioningBm1Active,
        "BM1",
        message.attempt_id,
        "",
        "",
        "",
        ssid);
    PublishBleValue(BuildProvisioningStatus(
        message.attempt_id, "accepted", "CREDENTIALS_STAGED"));
    cJSON_Delete(root);
    StartBm1NetworkCheck();
    return true;
}

void WifiBoard::StartBm1NetworkCheck() {
    xTaskCreate([](void* argument) {
        auto* board = static_cast<WifiBoard*>(argument);
        Settings provisioning(kProvisioningNamespace, false);
        const std::string attempt_id = provisioning.GetString("attempt");
        const std::string target_ssid = provisioning.GetString("target_ssid");
        auto& station = WifiStation::GetInstance();
        station.Start();
        if (!station.WaitForConnected(40 * 1000)) {
            const auto failure = ClassifyWifiFailure(station);
            station.Stop();
            PublishBleValue(BuildProvisioningStatus(
                attempt_id, failure.first, failure.second));
            vTaskDelete(nullptr);
            return;
        }
        if (station.GetSsid() != target_ssid) {
            station.Stop();
            PublishBleValue(BuildProvisioningStatus(
                attempt_id, "auth_failed", "TARGET_NETWORK_NOT_CONNECTED"));
            vTaskDelete(nullptr);
            return;
        }
        if (!ProbeRuntimeEndpoint(board)) {
            station.Stop();
            PublishBleValue(BuildProvisioningStatus(
                attempt_id, "internet_failed", "RUNTIME_UNREACHABLE"));
            vTaskDelete(nullptr);
            return;
        }
        PublishBleValue(BuildProvisioningStatus(
            attempt_id, "connected", "RUNTIME_READY"));
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }, "bm1_wifi_check", 6144, this, 5, nullptr);
}

void WifiBoard::ParseWifiCredentials(const char* data) {
    if (strncmp(data, "ssid:", 5) == 0) {
        std::string ssid = data + 5;
        ESP_LOGI(TAG, "WiFi SSID received via BLE: %s", ssid.c_str());
        ble_server_send_data("SSID received, send password", 28);
        
        // Store SSID temporarily for later use
        temp_ssid_ = ssid;
    }
    else if (strncmp(data, "pwd:", 4) == 0) {
        std::string password = data + 4;
        
        // Clean up password - remove any duplicate "pwd:" if present
        size_t pwd_pos = password.find("pwd:");
        if (pwd_pos != std::string::npos) {
            password = password.substr(0, pwd_pos);
        }
        
        // Check if we have a stored SSID
        if (!temp_ssid_.empty()) {
            ESP_LOGI(TAG, "WiFi credentials received via BLE: %s", temp_ssid_.c_str());
            
            if (ConsumeNextBleCredentialLowestFlag()) {
                ESP_LOGI(TAG, "Saving BLE credential as lowest priority (one-shot rule)");
                SaveCredentialAsLowestPriority(temp_ssid_, password);
                Settings settings("wifi", true);
                settings.SetString("nxt_boot_ssid", temp_ssid_);
            } else {
                SsidManager::GetInstance().AddSsid(temp_ssid_, password);
            }
            
            ble_server_send_data("WiFi credentials saved", 25);
            
            // Clear temporary SSID
            temp_ssid_.clear();
            
            // Exit config mode and restart
            wifi_config_mode_ = false;
            
            // Send confirmation to BLE client
            ble_server_send_data("Restarting to connect...", 25);
            // Small delay to ensure BLE message is sent
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            // Instead of trying to switch from BLE to WiFi (which causes memory issues),
            // restart the system for a clean state
            ESP_LOGI(TAG, "BLE configuration complete, restarting system for clean WiFi startup");
            esp_restart();
        } else {
            ble_server_send_data("Error: No SSID received first", 32);
        }
    }
    else if (strncmp(data, "wifi:", 5) == 0) {
        // Combined SSID and password format: "wifi:SSID:PASSWORD"
        std::string credentials = data + 5;
        size_t colon_pos = credentials.find(':');
        if (colon_pos != std::string::npos) {
            std::string ssid = credentials.substr(0, colon_pos);
            std::string password = credentials.substr(colon_pos + 1);
            ESP_LOGI(TAG, "WiFi credentials received via BLE: %s", ssid.c_str());
            
            if (ConsumeNextBleCredentialLowestFlag()) {
                ESP_LOGI(TAG, "Saving BLE credential as lowest priority (one-shot rule)");
                SaveCredentialAsLowestPriority(ssid, password);
                Settings settings("wifi", true);
                settings.SetString("nxt_boot_ssid", ssid);
            } else {
                SsidManager::GetInstance().AddSsid(ssid, password);
            }
            
            ble_server_send_data("WiFi credentials saved", 25);
            
            // Exit config mode and restart
            wifi_config_mode_ = false;
            
            // Send confirmation to BLE client
            ble_server_send_data("Restarting to connect...", 25);
            // Small delay to ensure BLE message is sent
            vTaskDelay(pdMS_TO_TICKS(500));
            
            // Stop BLE server before restarting network
            if (ble_initialized_) {
                ESP_LOGI(TAG, "Stopping BLE server before network restart");
                ble_server_stop_advertising();
                ble_server_deinit();
                ble_initialized_ = false;
                ESP_LOGI(TAG, "Waiting for BLE cleanup to complete before WiFi start...");
                vTaskDelay(pdMS_TO_TICKS(7000)); // Wait 7 seconds for BLE to fully deinitialize
            }
            
            // Stop WiFi station to ensure clean restart
            auto& wifi_station = WifiStation::GetInstance();
            wifi_station.Stop();
            vTaskDelay(pdMS_TO_TICKS(1000)); // Give WiFi time to fully stop
            
            // Exit config mode and restart network with new credentials
            wifi_config_mode_ = false;
            
            // Restart network with new credentials (no full system restart)
            StartNetwork();
        } else {
            ble_server_send_data("Error: Invalid format", 20);
        }
    }
}

bool WifiBoard::HasPendingWifiProvisioning() {
    Settings provisioning(kProvisioningNamespace, false);
    return provisioning.GetInt("state") == kProvisioningPendingCloud &&
        provisioning.GetString("protocol") == "BM2";
}

void WifiBoard::OnWifiProvisioningRuntimeFailure() {
    Settings provisioning(kProvisioningNamespace, false);
    const std::string attempt_id = provisioning.GetString("attempt");
    if (!IsCanonicalProvisioningAttemptId(attempt_id)) {
        ClearProvisioningState();
        return;
    }
    SaveProvisioningState(
        kProvisioningBleFailure,
        "BM2",
        attempt_id,
        "",
        "internet_failed",
        "RUNTIME_UNREACHABLE");
    {
        Settings wifi("wifi", true);
        wifi.SetInt("force_ble_cfg", 1);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
}

void WifiBoard::OnRuntimeReady() {
    Settings provisioning(kProvisioningNamespace, false);
    if (provisioning.GetInt("state") != kProvisioningPendingCloud ||
        provisioning.GetString("protocol") != "BM2") {
        return;
    }
    const std::string attempt_id = provisioning.GetString("attempt");
    const std::string report_token = provisioning.GetString("token");
    const std::string target_ssid = provisioning.GetString("target_ssid");
    if (!IsCanonicalProvisioningAttemptId(attempt_id) ||
        !IsValidReportToken(report_token)) {
        ESP_LOGW(TAG, "Discarding invalid pending provisioning state");
        ClearProvisioningState();
        return;
    }
    if (WifiStation::GetInstance().GetSsid() != target_ssid) {
        SaveProvisioningState(
            kProvisioningBleFailure,
            "BM2",
            attempt_id,
            "",
            "auth_failed",
            "TARGET_NETWORK_NOT_CONNECTED");
        {
            Settings wifi("wifi", true);
            wifi.SetInt("force_ble_cfg", 1);
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
        return;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "operation", "report");
    cJSON_AddStringToObject(root, "deviceId", SystemInfo::GetMacAddress().c_str());
    cJSON_AddStringToObject(root, "attemptId", attempt_id.c_str());
    cJSON_AddStringToObject(root, "status", "connected");
    cJSON_AddStringToObject(root, "reason", "RUNTIME_READY");
    cJSON_AddStringToObject(
        root, "firmwareVersion", esp_app_get_description()->version);
    char* encoded = cJSON_PrintUnformatted(root);
    std::string body = encoded == nullptr ? "" : encoded;
    cJSON_free(encoded);
    cJSON_Delete(root);
    if (body.empty()) return;

    const std::string url = CONFIG_PROVISIONING_RESULT_URL;
    if (url.empty()) {
        ESP_LOGW(TAG, "Provisioning result URL is not configured");
        return;
    }
    for (int attempt = 1; attempt <= 3; ++attempt) {
        auto http = std::unique_ptr<Http>(CreateHttp());
        http->SetHeader("Authorization", ("Device " + report_token).c_str());
        http->SetHeader("Content-Type", "application/json");
        http->SetTimeout(15000);
        http->SetContent(std::string(body));
        if (http->Open("POST", url)) {
            const int status_code = http->GetStatusCode();
            http->ReadAll();
            http->Close();
            if (status_code == 200) {
                ESP_LOGI(TAG, "Provisioning success reported");
                ClearProvisioningState();
                return;
            }
            ESP_LOGW(TAG, "Provisioning report rejected with status %d", status_code);
            if (status_code >= 400 && status_code < 500 &&
                status_code != 408 && status_code != 429) {
                ESP_LOGE(TAG, "Discarding provisioning report after terminal response");
                ClearProvisioningState();
                return;
            }
        } else {
            ESP_LOGW(TAG, "Provisioning report attempt %d failed", attempt);
        }
        vTaskDelay(pdMS_TO_TICKS(attempt * 1000));
    }
}
