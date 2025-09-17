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

#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <ssid_manager.h>
#include "animation/animation.h"

static const char *TAG = "WifiBoard";

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
    if (file_server_) {
        StopFileUploadServer();
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



void WifiBoard::StartNetwork() {
    // If no WiFi SSID is configured, use BLE for WiFi configuration
    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    
    // Debug: Log stored WiFi credentials
    ESP_LOGI(TAG, "Stored WiFi credentials count: %d", ssid_list.size());
    for (size_t i = 0; i < ssid_list.size(); i++) {
        ESP_LOGI(TAG, "WiFi %d: SSID='%s', Password='%s'", 
                 i, ssid_list[i].ssid.c_str(), 
                 ssid_list[i].password.c_str());
    }
    
    if (ssid_list.empty()) {
        // No WiFi credentials found, use BLE for configuration
        wifi_config_mode_ = true;
        ESP_LOGI(TAG, "No WiFi credentials found, using BLE for configuration");
        
        // Initialize BLE server only when needed
        if (!ble_initialized_) {
            InitializeBleServer();
        }
        
        auto& application = Application::GetInstance();
        application.SetDeviceState(kDeviceStateWifiConfiguring);
        
        // Display BLE configuration instructions
        std::string hint = "Connect to BLE device 'Xiaozhi-WiFi' to configure WiFi";
        application.Alert("WiFi Configuration", hint.c_str(), "", Lang::Sounds::P3_WIFICONFIG);
        
        // Wait for BLE configuration
        while (wifi_config_mode_) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        
        // After BLE configuration, restart network with new credentials
        StartNetwork();
        return;
    }

    // Start WiFi station with existing credentials
    auto& wifi_station = WifiStation::GetInstance();

    wifi_station.OnScanBegin([this]() {
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
    });
    wifi_station.OnConnect([this](const std::string& ssid) {
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
        
        // Start file upload server
        StartFileUploadServer();
    });
    
    wifi_station.Start();

    // Try to connect to WiFi, if failed, use BLE for configuration
    if (!wifi_station.WaitForConnected(60 * 1000)) {
        wifi_station.Stop();
        wifi_config_mode_ = true;
        ESP_LOGI(TAG, "WiFi connection failed, using BLE for configuration");
        
        // Initialize BLE server only when needed
        if (!ble_initialized_) {
            InitializeBleServer();
        }
        
        auto& application = Application::GetInstance();
        application.SetDeviceState(kDeviceStateWifiConfiguring);
        
        // Display BLE configuration instructions
        std::string hint = "WiFi connection failed. Connect to BLE device 'Xiaozhi-WiFi' to configure WiFi";
        application.Alert("WiFi Configuration", hint.c_str(), "", Lang::Sounds::P3_WIFICONFIG);
        
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

// HTTP Server Implementation for File Uploads
static esp_err_t upload_animation_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "File upload request received");
    
    // Get content length
    size_t content_length = req->content_len;
    if (content_length == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }
    
    // Validate file size (1MB limit)
    if (content_length > 1024 * 1024) {
        httpd_resp_send_err(req, HTTPD_413_REQUEST_ENTITY_TOO_LARGE, "File too large (max 1MB)");
        return ESP_FAIL;
    }
    
    // Get filename from query parameter
    char filename[64] = {0};
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char* buf = (char*)malloc(buf_len);
        if (buf && httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];
            if (httpd_query_key_value(buf, "filename", param, sizeof(param)) == ESP_OK) {
                strncpy(filename, param, sizeof(filename) - 1);
            }
        }
        if (buf) free(buf);
    }
    
    if (strlen(filename) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename parameter");
        return ESP_FAIL;
    }
    
    // Validate filename (no path traversal, reasonable length)
    if (strlen(filename) > 32 || strstr(filename, "..") || strstr(filename, "/") || strstr(filename, "\\")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Uploading file: %s (%d bytes)", filename, content_length);
    
    // Allocate buffer for file data
    uint8_t* file_data = (uint8_t*)malloc(content_length);
    if (!file_data) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }
    
    // Read file data
    size_t received = 0;
    while (received < content_length) {
        int ret = httpd_req_recv(req, (char*)(file_data + received), content_length - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Failed to receive file data: %d", ret);
            free(file_data);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive data");
            return ESP_FAIL;
        }
        received += ret;
    }
    
    // Write file atomically to SPIFFS
    bool success = animation_write_file_atomic(filename, file_data, content_length);
    
    if (success) {
        // Calculate simple hash for manifest
        uint32_t crc = esp_crc32_le(0, file_data, content_length);
        char hash_str[16];
        snprintf(hash_str, sizeof(hash_str), "%08x", crc);
        
        // Update manifest
        animation_update_manifest(filename, content_length, hash_str);
        
        // Reload animations
        animation_reload_animations_from_manifest();
        
        ESP_LOGI(TAG, "File uploaded successfully: %s", filename);
        httpd_resp_sendstr(req, "{\"success\": true, \"message\": \"File uploaded successfully\"}");
    } else {
        ESP_LOGE(TAG, "Failed to write file: %s", filename);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to write file");
    }
    
    free(file_data);
    return success ? ESP_OK : ESP_FAIL;
}

static esp_err_t delete_animation_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "File delete request received");
    
    // Get filename from query parameter
    char filename[64] = {0};
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
        char* buf = (char*)malloc(buf_len);
        if (buf && httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
            char param[32];
            if (httpd_query_key_value(buf, "filename", param, sizeof(param)) == ESP_OK) {
                strncpy(filename, param, sizeof(filename) - 1);
            }
        }
        if (buf) free(buf);
    }
    
    if (strlen(filename) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing filename parameter");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Deleting file: %s", filename);
    
    bool success = animation_delete_file(filename);
    
    if (success) {
        ESP_LOGI(TAG, "File deleted successfully: %s", filename);
        httpd_resp_sendstr(req, "{\"success\": true, \"message\": \"File deleted successfully\"}");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to delete file: %s", filename);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete file");
        return ESP_FAIL;
    }
}

static esp_err_t list_files_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "File list request received");
    
    // Get manifest JSON
    std::string manifest_json = animation_get_manifest_json();
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, manifest_json.c_str());
    return ESP_OK;
}

void WifiBoard::StartFileUploadServer()
{
    if (file_server_) {
        ESP_LOGW(TAG, "File upload server already running");
        return;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.max_uri_handlers = 8;
    config.max_resp_headers = 8;
    config.backlog_conn = 5;
    
    ESP_LOGI(TAG, "Starting file upload server on port %d", config.server_port);
    
    if (httpd_start(&file_server_, &config) == ESP_OK) {
        // Register upload endpoint
        httpd_uri_t upload_uri = {
            .uri = "/upload",
            .method = HTTP_POST,
            .handler = upload_animation_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(file_server_, &upload_uri);
        
        // Register delete endpoint
        httpd_uri_t delete_uri = {
            .uri = "/delete",
            .method = HTTP_DELETE,
            .handler = delete_animation_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(file_server_, &delete_uri);
        
        // Register list endpoint
        httpd_uri_t list_uri = {
            .uri = "/list",
            .method = HTTP_GET,
            .handler = list_files_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(file_server_, &list_uri);
        
        ESP_LOGI(TAG, "File upload server started successfully");
        ESP_LOGI(TAG, "Endpoints:");
        ESP_LOGI(TAG, "  POST /upload?filename=<name> - Upload animation file");
        ESP_LOGI(TAG, "  DELETE /delete?filename=<name> - Delete animation file");
        ESP_LOGI(TAG, "  GET /list - List available files");
    } else {
        ESP_LOGE(TAG, "Failed to start file upload server");
    }
}

void WifiBoard::StopFileUploadServer()
{
    if (file_server_) {
        httpd_stop(file_server_);
        file_server_ = nullptr;
        ESP_LOGI(TAG, "File upload server stopped");
    }
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

void WifiBoard::InitializeBleServer() {
    ESP_LOGI(TAG, "Initializing BLE server for WiFi configuration");
    
    // Store instance pointer for static callbacks
    g_wifi_board_instance = this;
    
    // Initialize BLE server with callbacks
    if (ble_server_init("Xiaozhi-WiFi", 
                       ble_data_callback,
                       ble_connection_callback,
                       nullptr)) { // No device control callback for now
        ble_initialized_ = true;
        ble_server_start_advertising();
        ESP_LOGI(TAG, "BLE server initialized and advertising for WiFi config");
    } else {
        ESP_LOGE(TAG, "Failed to initialize BLE server");
    }
}

void WifiBoard::HandleBleData(const char* data, uint16_t length) {
    ESP_LOGI(TAG, "BLE data received: %.*s", length, data);
    ParseWifiCredentials(data);
}

void WifiBoard::HandleBleConnection(bool connected) {
    if (connected) {
        ESP_LOGI(TAG, "BLE client connected");
        // Send status message to client
        ble_server_send_data("Ready for WiFi configuration", 30);
    } else {
        ESP_LOGI(TAG, "BLE client disconnected");
    }
}

void WifiBoard::ParseWifiCredentials(const char* data) {
    ESP_LOGI(TAG, "BLE data received: %s", data);
    
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
        
        ESP_LOGI(TAG, "WiFi password received via BLE: %s", password.c_str());
        
        // Check if we have a stored SSID
        if (!temp_ssid_.empty()) {
            ESP_LOGI(TAG, "WiFi credentials received via BLE: %s", temp_ssid_.c_str());
            
            // Save credentials
            auto& ssid_manager = SsidManager::GetInstance();
            ssid_manager.AddSsid(temp_ssid_, password);
            
            ble_server_send_data("WiFi credentials saved", 25);
            
            // Clear temporary SSID
            temp_ssid_.clear();
            
            // Exit config mode and restart
            wifi_config_mode_ = false;
            
            // Send confirmation to BLE client
            ble_server_send_data("Restarting to connect...", 25);
            // Small delay to ensure BLE message is sent
            vTaskDelay(pdMS_TO_TICKS(500));
            // Restart the entire application to use new credentials
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
            
            // Save credentials
            auto& ssid_manager = SsidManager::GetInstance();
            ssid_manager.AddSsid(ssid, password);
            
            ble_server_send_data("WiFi credentials saved", 25);
            
            // Exit config mode and restart
            wifi_config_mode_ = false;
            
            // Send confirmation to BLE client
            ble_server_send_data("Restarting to connect...", 25);
            // Small delay to ensure BLE message is sent
            vTaskDelay(pdMS_TO_TICKS(500));
            // Restart the entire application to use new credentials
            esp_restart();
        } else {
            ble_server_send_data("Error: Invalid format", 20);
        }
    }
}


