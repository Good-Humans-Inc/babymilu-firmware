#include "animation_updater.h"
#include "board.h"
#include "system_info.h"
#include "animation.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <cstring>
#include <algorithm>
#include <unistd.h>

#define TAG "AnimationUpdater"

// Default server URL - change this to your test server
// #define DEFAULT_SERVER_URL "http://192.168.5.15:8081/api/animations"
// Updated for HTTPS testing
#define DEFAULT_SERVER_URL "https://github.com/Jackson-hangxuan/postman_test/raw/refs/heads/main"

AnimationUpdater& AnimationUpdater::GetInstance() {
    static AnimationUpdater instance;
    return instance;
}

AnimationUpdater::AnimationUpdater() {
    LoadConfiguration();
}

AnimationUpdater::~AnimationUpdater() {
    Stop();
}

void AnimationUpdater::Initialize() {
    ESP_LOGI(TAG, "Initializing Animation Updater");
    
    // Initialize SPIFFS for animations if not already done
    animation_init_spiffs();
    
    // Load configuration from NVS
    LoadConfiguration();
    
    ESP_LOGI(TAG, "Animation Updater initialized");
    ESP_LOGI(TAG, "  Server URL: %s", server_url_.c_str());
    ESP_LOGI(TAG, "  Check Interval: %u seconds", check_interval_seconds_);
    ESP_LOGI(TAG, "  Enabled: %s", enabled_.load() ? "true" : "false");
}

void AnimationUpdater::Start() {
    if (is_running_.load()) {
        ESP_LOGW(TAG, "Animation updater is already running");
        return;
    }
    
    if (!enabled_.load()) {
        ESP_LOGI(TAG, "Animation updater is disabled, not starting");
        return;
    }
    
    ESP_LOGI(TAG, "Starting animation updater");
    
    // Create background task
    BaseType_t ret = xTaskCreate(
        UpdateTask,
        "animation_updater",
        8192,  // 8KB stack - increased for HTTP operations and JSON parsing
        this,
        2,     // Low priority
        &update_task_handle_
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create animation updater task");
        return;
    }
    
    ESP_LOGI(TAG, "Animation updater task created successfully");
    
    is_running_.store(true);
    ESP_LOGI(TAG, "Animation updater started successfully");
}

void AnimationUpdater::Stop() {
    if (!is_running_.load()) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping animation updater");
    
    is_running_.store(false);
    
    if (update_task_handle_ != nullptr) {
        vTaskDelete(update_task_handle_);
        update_task_handle_ = nullptr;
    }
    
    if (update_timer_ != nullptr) {
        xTimerDelete(update_timer_, portMAX_DELAY);
        update_timer_ = nullptr;
    }
    
    ESP_LOGI(TAG, "Animation updater stopped");
}

void AnimationUpdater::SetServerUrl(const std::string& url) {
    server_url_ = url;
    SaveConfiguration();
    ESP_LOGI(TAG, "Server URL updated to: %s", url.c_str());
}

void AnimationUpdater::SetCheckInterval(uint32_t interval_seconds) {
    check_interval_seconds_ = interval_seconds;
    SaveConfiguration();
    ESP_LOGI(TAG, "Check interval updated to: %u seconds", interval_seconds);
}

void AnimationUpdater::SetEnabled(bool enabled) {
    enabled_.store(enabled);
    SaveConfiguration();
    ESP_LOGI(TAG, "Animation updater %s", enabled ? "enabled" : "disabled");
    
    if (enabled && !is_running_.load()) {
        Start();
    } else if (!enabled && is_running_.load()) {
        Stop();
    }
}

void AnimationUpdater::CheckForUpdates() {
    if (!enabled_.load()) {
        ESP_LOGD(TAG, "Animation updater is disabled, skipping check");
        return;
    }
    
    ESP_LOGI(TAG, "Manual check for animation updates");
    
    // Always attempt to download animations_mega.bin for updates
    ESP_LOGI(TAG, "Attempting to download animations_mega.bin...");
    TestHttpsDownload();
}

bool AnimationUpdater::DownloadMegaFileNow() {
    ESP_LOGI(TAG, "Manual download of animations_mega.bin requested");
    
    if (!enabled_.load()) {
        ESP_LOGW(TAG, "Animation updater is disabled, cannot download");
        return false;
    }
    
    // Force download even if already successful
    bool previous_success = first_download_success_.load();
    first_download_success_.store(false);
    
    ESP_LOGI(TAG, "Starting manual download of animations_mega.bin...");
    bool success = TestHttpsDownload();
    
    if (!success) {
        ESP_LOGE(TAG, "Manual download failed");
        // Restore previous state
        first_download_success_.store(previous_success);
        return false;
    }
    
    ESP_LOGI(TAG, "Manual download completed successfully");
    return true;
}

bool AnimationUpdater::ForceUpdateCheck() {
    ESP_LOGI(TAG, "Force update check requested - bypassing success flag");
    
    if (!enabled_.load()) {
        ESP_LOGW(TAG, "Animation updater is disabled, cannot check for updates");
        return false;
    }
    
    ESP_LOGI(TAG, "Forcing immediate update check...");
    bool success = TestHttpsDownload();
    
    if (success) {
        ESP_LOGI(TAG, "Force update check completed successfully");
    } else {
        ESP_LOGE(TAG, "Force update check failed");
    }
    
    return success;
}

void AnimationUpdater::ResetFirstDownloadSuccess() {
    first_download_success_.store(false);
    ESP_LOGI(TAG, "First download success flag reset");
}

std::string AnimationUpdater::GetStatusJson() const {
    cJSON* json = cJSON_CreateObject();
    
    cJSON_AddBoolToObject(json, "enabled", enabled_.load());
    cJSON_AddBoolToObject(json, "running", is_running_.load());
    cJSON_AddStringToObject(json, "server_url", server_url_.c_str());
    cJSON_AddNumberToObject(json, "check_interval_seconds", check_interval_seconds_);
    cJSON_AddNumberToObject(json, "check_count", check_count_.load());
    cJSON_AddNumberToObject(json, "update_count", update_count_.load());
    cJSON_AddNumberToObject(json, "error_count", error_count_.load());
    cJSON_AddNumberToObject(json, "last_check_time", last_check_time_.load());
    cJSON_AddNumberToObject(json, "last_update_time", last_update_time_.load());
    cJSON_AddBoolToObject(json, "first_download_success", first_download_success_.load());
    
    char* json_string = cJSON_PrintUnformatted(json);
    std::string result(json_string);
    free(json_string);
    cJSON_Delete(json);
    
    return result;
}

void AnimationUpdater::UpdateTask(void* parameter) {
    AnimationUpdater* updater = static_cast<AnimationUpdater*>(parameter);
    updater->UpdateLoop();
}

void AnimationUpdater::UpdateLoop() {
    ESP_LOGI(TAG, "Animation updater task started");
    
    // Wait a bit before first check to let the system stabilize
    vTaskDelay(pdMS_TO_TICKS(5000)); // 5 seconds
    
    while (is_running_.load()) {
        // COMMENTED OUT: HTTP server checking for HTTPS testing
        // if (enabled_.load() && !first_download_success_.load()) {
        //     CheckServerForUpdates();
        // } else if (first_download_success_.load()) {
        //     ESP_LOGI(TAG, "First download successful, skipping update checks");
        //     // Continue the loop but skip the actual update check
        // }
        
        // HTTPS DOWNLOAD: Check for animations_mega.bin updates
        if (enabled_.load()) {
            if (!first_download_success_.load()) {
                ESP_LOGI(TAG, "Attempting to download animations_mega.bin from HTTPS server...");
                TestHttpsDownload();
            } else {
                // Check for updates periodically (every 10th iteration = ~100 seconds)
                static int update_check_counter = 0;
                update_check_counter++;
                
                if (update_check_counter >= 10) {
                    ESP_LOGI(TAG, "Periodic check for animations_mega.bin updates...");
                    TestHttpsDownload();
                    update_check_counter = 0;
                } else {
                    ESP_LOGD(TAG, "Skipping update check (counter: %d/10)", update_check_counter);
                }
            }
        }
        
        // Wait for the specified interval
        vTaskDelay(pdMS_TO_TICKS(check_interval_seconds_ * 1000));
    }
    
    ESP_LOGI(TAG, "Animation updater task ended");
    // Don't delete the task here - let the Stop() method handle it
}

// COMMENTED OUT: Original HTTP server checking logic for HTTPS testing
// Stub implementation to satisfy linker
bool AnimationUpdater::CheckServerForUpdates() {
    ESP_LOGI(TAG, "CheckServerForUpdates() called - using HTTPS testing instead");
    return TestHttpsDownload();
}

/*
bool AnimationUpdater::CheckServerForUpdates() {
    check_count_.fetch_add(1);
    last_check_time_.store(esp_timer_get_time() / 1000); // Convert to milliseconds
    
    ESP_LOGI(TAG, "Checking server for animation updates...");
    
    try {
        auto& board = Board::GetInstance();
        auto http = std::unique_ptr<Http>(board.CreateHttp());
        
        if (!http) {
            ESP_LOGE(TAG, "Failed to create HTTP client");
            error_count_.fetch_add(1);
            return false;
        }
        
        // Set headers similar to OTA requests
        http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
        http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
        http->SetHeader("User-Agent", "Xiaozhi-Animation/1.0");
        http->SetHeader("Accept", "application/json");
        http->SetHeader("Content-Type", "application/json");
        http->SetTimeout(30000); // 30 second timeout
        
        // Build check URL
        std::string check_url = server_url_ + "/check?device_id=" + SystemInfo::GetMacAddress();
        
        ESP_LOGI(TAG, "Checking URL: %s", check_url.c_str());
        
        if (!http->Open("GET", check_url)) {
            ESP_LOGE(TAG, "Failed to open HTTP connection to %s", check_url.c_str());
            error_count_.fetch_add(1);
            return false;
        }
        
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "Server returned status code: %d", status_code);
        if (status_code != 200) {
            ESP_LOGW(TAG, "Server returned status code: %d", status_code);
            if (status_code >= 400) {
                error_count_.fetch_add(1);
            }
            return false;
        }
        
        // Check content length
        size_t content_length = http->GetBodyLength();
        ESP_LOGI(TAG, "Content-Length: %d", content_length);
        
        // Try different reading approaches
        std::string response;
        
        // First try ReadAll()
        response = http->ReadAll();
        ESP_LOGI(TAG, "ReadAll() response length: %d", response.length());
        ESP_LOGI(TAG, "ReadAll() response: %s", response.c_str());
        
        // If ReadAll() fails, try manual reading
        if (response.empty() && content_length > 0) {
            ESP_LOGI(TAG, "ReadAll() failed, trying manual read with content_length: %d", content_length);
            
            response.clear();
            char buffer[512];
            int bytes_read;
            int total_read = 0;
            
            while (total_read < content_length) {
                bytes_read = http->Read(buffer, sizeof(buffer));
                if (bytes_read <= 0) {
                    ESP_LOGI(TAG, "Manual read finished, bytes_read: %d", bytes_read);
                    break;
                }
                
                response.append(buffer, bytes_read);
                total_read += bytes_read;
                ESP_LOGI(TAG, "Manual read %d bytes, total: %d/%d", bytes_read, total_read, content_length);
            }
            
            ESP_LOGI(TAG, "Manual read completed, total bytes: %d", total_read);
        }
        
        http->Close();
        
        ESP_LOGI(TAG, "Final response length: %d", response.length());
        ESP_LOGI(TAG, "Final response: %s", response.c_str());
        
        if (response.empty()) {
            ESP_LOGW(TAG, "Empty response from server");
            return false;
        }
        
        // Parse JSON response
        cJSON* json = cJSON_Parse(response.c_str());
        if (!json) {
            ESP_LOGE(TAG, "Failed to parse JSON response");
            error_count_.fetch_add(1);
            return false;
        }
        
        // Check if updates are available
        cJSON* has_updates = cJSON_GetObjectItem(json, "has_updates");
        if (!cJSON_IsBool(has_updates) || !cJSON_IsTrue(has_updates)) {
            ESP_LOGD(TAG, "No animation updates available");
            cJSON_Delete(json);
            return false;
        }
        
        // Get animation files to download
        cJSON* animations = cJSON_GetObjectItem(json, "animations");
        if (!cJSON_IsArray(animations)) {
            ESP_LOGE(TAG, "Invalid animations array in response");
            cJSON_Delete(json);
            error_count_.fetch_add(1);
            return false;
        }
        
        int array_size = cJSON_GetArraySize(animations);
        ESP_LOGI(TAG, "Found %d animation updates available", array_size);
        
        bool any_success = false;
        
        for (int i = 0; i < array_size; i++) {
            cJSON* animation = cJSON_GetArrayItem(animations, i);
            if (!cJSON_IsObject(animation)) {
                ESP_LOGW(TAG, "Invalid animation object at index %d", i);
                continue;
            }
            
            cJSON* filename = cJSON_GetObjectItem(animation, "filename");
            cJSON* download_url = cJSON_GetObjectItem(animation, "download_url");
            
            if (!cJSON_IsString(filename) || !cJSON_IsString(download_url)) {
                ESP_LOGW(TAG, "Invalid filename or download_url at index %d", i);
                continue;
            }
            
            std::string filename_str(filename->valuestring);
            std::string download_url_str(download_url->valuestring);
            
            ESP_LOGI(TAG, "Downloading animation: %s", filename_str.c_str());
            
            if (DownloadAnimationFile(download_url_str, filename_str)) {
                ESP_LOGI(TAG, "Successfully downloaded: %s", filename_str.c_str());
                any_success = true;
            } else {
                ESP_LOGE(TAG, "Failed to download: %s", filename_str.c_str());
            }
        }
        
        cJSON_Delete(json);
        
        if (any_success) {
            update_count_.fetch_add(1);
            last_update_time_.store(esp_timer_get_time() / 1000);
            
            // Set the first download success flag
            first_download_success_.store(true);
            
            // Reload animations from SPIFFS
            ReloadAnimations();
            
            ESP_LOGI(TAG, "Animation updates completed successfully");
        }
        
        return any_success;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in CheckServerForUpdates: %s", e.what());
        error_count_.fetch_add(1);
        return false;
    }
}
*/

// NEW: HTTPS download method for animations_mega.bin
bool AnimationUpdater::TestHttpsDownload() {
    ESP_LOGI(TAG, "Downloading animations_mega.bin from HTTPS server...");
    
    // Test with the provided HTTPS endpoint
    std::string test_url = "https://1379890832-bqi413zoc2.ap-shanghai.tencentscf.com";
    
    // Add device_id to the URL for Flask program handling
    std::string device_id = SystemInfo::GetMacAddress();
    test_url += "?device_id=" + device_id;
    
    ESP_LOGI(TAG, "Attempting to connect to: %s", test_url.c_str());
    
    // Get the response and parse the download URL
    std::string download_url = GetDownloadUrlFromResponse(test_url);
    
    if (download_url.empty()) {
        ESP_LOGE(TAG, "Failed to get download URL from response");
        return false;
    }
    
    ESP_LOGI(TAG, "Got download URL: %s", download_url.c_str());
    
    // Download animations_mega.bin specifically
    bool success = DownloadMegaAnimationFile(download_url);
    
    if (success) {
        ESP_LOGI(TAG, "Successfully downloaded animations_mega.bin!");
        first_download_success_.store(true);
        ReloadAnimations();
    } else {
        ESP_LOGE(TAG, "Failed to download animations_mega.bin!");
    }
    
    return success;
}

// NEW: HTTPS connection testing method (no file download)
bool AnimationUpdater::TestHttpsConnection(const std::string& url) {
    try {
        auto& board = Board::GetInstance();
        auto http = std::unique_ptr<Http>(board.CreateHttp());
        
        if (!http) {
            ESP_LOGE(TAG, "Failed to create HTTP client for connection test");
            return false;
        }
        
        // Set headers for connection test
        http->SetHeader("User-Agent", "Xiaozhi-Animation-Test/1.0");
        http->SetHeader("Accept", "*/*");
        http->SetHeader("Accept-Encoding", "identity"); // Disable compression
        http->SetTimeout(30000); // 30 second timeout for connection test
        
        ESP_LOGI(TAG, "Testing connection to: %s", url.c_str());
        ESP_LOGI(TAG, "HTTP client created, attempting connection...");
        
        if (!http->Open("GET", url)) {
            ESP_LOGE(TAG, "Failed to open HTTPS connection");
            return false;
        }
        
        ESP_LOGI(TAG, "HTTPS connection opened successfully");
        
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "HTTP Status Code: %d", status_code);
        
        // Log response headers
        size_t content_length = http->GetBodyLength();
        ESP_LOGI(TAG, "Content-Length: %zu", content_length);
        
        // Try to read a small amount of response data to test the connection
        char buffer[512];
        int bytes_read = http->Read(buffer, sizeof(buffer) - 1);
        
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0'; // Null terminate for logging
            ESP_LOGI(TAG, "Response data (first %d bytes): %s", bytes_read, buffer);
        } else {
            ESP_LOGI(TAG, "No response data received (bytes_read: %d)", bytes_read);
        }
        
        // Try to read all response data for complete testing
        std::string full_response = http->ReadAll();
        ESP_LOGI(TAG, "Full response length: %zu bytes", full_response.length());
        
        if (!full_response.empty()) {
            // Log first 200 characters of response
            size_t preview_length = std::min(static_cast<size_t>(200), full_response.length());
            std::string preview = full_response.substr(0, preview_length);
            ESP_LOGI(TAG, "Response preview: %s", preview.c_str());
        }
        
        http->Close();
        
        // Consider connection successful if we got any response (even error codes)
        if (status_code > 0) {
            ESP_LOGI(TAG, "HTTPS connection test completed successfully");
            return true;
        } else {
            ESP_LOGE(TAG, "HTTPS connection test failed - no status code received");
            return false;
        }
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in TestHttpsConnection: %s", e.what());
        return false;
    }
}

// Helper method to get download URL from HTTPS response
std::string AnimationUpdater::GetDownloadUrlFromResponse(const std::string& url) {
    try {
        auto& board = Board::GetInstance();
        auto http = std::unique_ptr<Http>(board.CreateHttp());
        
        if (!http) {
            ESP_LOGE(TAG, "Failed to create HTTP client for URL parsing");
            return "";
        }
        
        // Set headers for connection test
        http->SetHeader("User-Agent", "Xiaozhi-Animation-Test/1.0");
        http->SetHeader("Accept", "*/*");
        http->SetHeader("Accept-Encoding", "identity"); // Disable compression
        http->SetTimeout(30000); // 30 second timeout for connection test
        
        ESP_LOGI(TAG, "Getting response from: %s", url.c_str());
        
        if (!http->Open("GET", url)) {
            ESP_LOGE(TAG, "Failed to open HTTPS connection for URL parsing");
            return "";
        }
        
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "HTTP Status Code: %d", status_code);
        
        if (status_code != 200) {
            ESP_LOGE(TAG, "Failed to get response, status code: %d", status_code);
            return "";
        }
        
        // Read the response data
        std::string response = http->ReadAll();
        http->Close();
        
        ESP_LOGI(TAG, "Response received: %s", response.c_str());
        
        // The response should contain the download URL
        // Trim any whitespace
        response.erase(0, response.find_first_not_of(" \t\n\r"));
        response.erase(response.find_last_not_of(" \t\n\r") + 1);
        
        return response;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in GetDownloadUrlFromResponse: %s", e.what());
        return "";
    }
}

// Helper method to extract filename from URL
std::string AnimationUpdater::ExtractFilenameFromUrl(const std::string& url) {
    // Find the last '/' in the URL
    size_t last_slash = url.find_last_of('/');
    if (last_slash == std::string::npos) {
        ESP_LOGW(TAG, "No slash found in URL, using default filename");
        return "downloaded_animation.bin";
    }
    
    // Extract everything after the last slash
    std::string filename = url.substr(last_slash + 1);
    
    // If filename is empty, use default
    if (filename.empty()) {
        ESP_LOGW(TAG, "Empty filename extracted, using default");
        return "downloaded_animation.bin";
    }
    
    return filename;
}

bool AnimationUpdater::DownloadAnimationFile(const std::string& url, const std::string& filename) {
    try {
        auto& board = Board::GetInstance();
        auto http = std::unique_ptr<Http>(board.CreateHttp());
        
        if (!http) {
            ESP_LOGE(TAG, "Failed to create HTTP client for download");
            return false;
        }
        
        // Set headers for download
        http->SetHeader("User-Agent", "Xiaozhi-Animation-Updater/1.0");
        http->SetHeader("Accept", "application/octet-stream");
        http->SetHeader("Accept-Encoding", "identity"); // Disable compression
        http->SetTimeout(60000); // 60 second timeout for downloads
        
        ESP_LOGI(TAG, "Downloading from: %s", url.c_str());
        ESP_LOGI(TAG, "HTTP client created, attempting connection...");
        
        if (!http->Open("GET", url)) {
            ESP_LOGE(TAG, "Failed to open download connection");
            return false;
        }
        
        ESP_LOGI(TAG, "HTTP connection opened successfully");
        
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "HTTP Status Code: %d", status_code);
        
        if (status_code != 200) {
            ESP_LOGE(TAG, "Download failed with status code: %d", status_code);
            return false;
        }
        
        size_t content_length = http->GetBodyLength();
        ESP_LOGI(TAG, "Content-Length: %zu", content_length);
        
        if (content_length == 0) {
            ESP_LOGE(TAG, "Empty file received - Content-Length is 0");
            
            // Try to read anyway in case content-length is not set
            ESP_LOGI(TAG, "Attempting to read despite zero content-length...");
            std::string test_data = http->ReadAll();
            ESP_LOGI(TAG, "ReadAll() returned %zu bytes", test_data.length());
            
            if (test_data.empty()) {
                ESP_LOGE(TAG, "No data received from server");
                return false;
            } else {
                ESP_LOGI(TAG, "Received data despite zero content-length, proceeding...");
                content_length = test_data.length();
            }
        }
        
        ESP_LOGI(TAG, "Downloading %s (%zu bytes)", filename.c_str(), content_length);
        
        // Read the file data
        std::string file_data;
        file_data.reserve(content_length);
        
        char buffer[1024];
        size_t total_read = 0;
        
        while (total_read < content_length) {
            int bytes_read = http->Read(buffer, sizeof(buffer));
            if (bytes_read <= 0) {
                ESP_LOGE(TAG, "Failed to read file data");
                return false;
            }
            
            file_data.append(buffer, bytes_read);
            total_read += bytes_read;
        }
        
        http->Close();
        
        // Validate the downloaded file
        if (!ValidateAnimationFile(file_data)) {
            ESP_LOGE(TAG, "Downloaded file failed validation: %s", filename.c_str());
            return false;
        }
        
        // Save to SPIFFS
        if (!SaveAnimationToSpiffs(filename, file_data)) {
            ESP_LOGE(TAG, "Failed to save file to SPIFFS: %s", filename.c_str());
            return false;
        }
        
        ESP_LOGI(TAG, "Successfully downloaded and saved: %s", filename.c_str());
        return true;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in DownloadAnimationFile: %s", e.what());
        return false;
    }
}

// NEW: Download animations_mega.bin specifically
bool AnimationUpdater::DownloadMegaAnimationFile(const std::string& url) {
    try {
        auto& board = Board::GetInstance();
        auto http = std::unique_ptr<Http>(board.CreateHttp());
        
        if (!http) {
            ESP_LOGE(TAG, "Failed to create HTTP client for mega file download");
            return false;
        }
        
        // Set headers for download
        http->SetHeader("User-Agent", "Xiaozhi-Animation-Updater/1.0");
        http->SetHeader("Accept", "application/octet-stream");
        http->SetHeader("Accept-Encoding", "identity"); // Disable compression
        http->SetTimeout(240000); // 4 minute timeout for large file downloads
        
        ESP_LOGI(TAG, "Downloading animations_mega.bin from: %s", url.c_str());
        ESP_LOGI(TAG, "HTTP client created, attempting connection...");
        
        if (!http->Open("GET", url)) {
            ESP_LOGE(TAG, "Failed to open download connection for mega file");
            return false;
        }
        
        ESP_LOGI(TAG, "HTTP connection opened successfully");
        
        int status_code = http->GetStatusCode();
        ESP_LOGI(TAG, "HTTP Status Code: %d", status_code);
        
        if (status_code != 200) {
            ESP_LOGE(TAG, "Download failed with status code: %d", status_code);
            return false;
        }
        
        size_t content_length = http->GetBodyLength();
        ESP_LOGI(TAG, "Content-Length: %u", (unsigned int)content_length);
        
        // Handle case where server doesn't provide Content-Length
        bool unknown_length = (content_length == 0);
        if (unknown_length) {
            ESP_LOGI(TAG, "Server did not provide Content-Length, will read until connection closes");
        }
        
        ESP_LOGI(TAG, "Downloading animations_mega.bin (%u bytes) - streaming to SPIFFS", (unsigned int)content_length);
        
        // Stream download directly to SPIFFS file (no RAM buffering)
        const char* filename = "animations_mega.bin";
        char full_path[128];
        snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);
        
        // Remove existing file if it exists
        if (access(full_path, F_OK) == 0) {
            ESP_LOGI(TAG, "Removing existing animations_mega.bin...");
            if (unlink(full_path) != 0) {
                ESP_LOGW(TAG, "Failed to remove existing file: %s", full_path);
            }
        }
        
        // Open file for writing
        FILE* file = fopen(full_path, "wb");
        if (!file) {
            ESP_LOGE(TAG, "Failed to open file for writing: %s", full_path);
            http->Close();
            return false;
        }
        
        char buffer[4096]; // 4KB buffer
        size_t total_read = 0;
        bool download_success = true;
        uint32_t timeout_start = esp_timer_get_time() / 1000; // Start time in ms
        uint32_t timeout_duration = 240000; // 4 minutes timeout
        
        while (download_success) {
            // Check for timeout
            uint32_t current_time = esp_timer_get_time() / 1000;
            if (current_time - timeout_start > timeout_duration) {
                ESP_LOGE(TAG, "Download timeout after %u ms", timeout_duration);
                download_success = false;
                break;
            }
            int bytes_read = http->Read(buffer, sizeof(buffer));
            if (bytes_read <= 0) {
                // End of data or connection closed
                ESP_LOGI(TAG, "Download completed, read %u bytes total", (unsigned int)total_read);
                break;
            }
            
            // Write directly to file
            size_t written = fwrite(buffer, 1, bytes_read, file);
            if (written != bytes_read) {
                ESP_LOGE(TAG, "Failed to write data to file");
                download_success = false;
                break;
            }
            
            total_read += bytes_read;
            
            // Log progress every 50KB
            if (total_read % 51200 == 0) {
                if (unknown_length) {
                    ESP_LOGI(TAG, "Download progress: %u bytes downloaded", (unsigned int)total_read);
                } else {
                    ESP_LOGI(TAG, "Download progress: %u/%u bytes (%.1f%%)", 
                             (unsigned int)total_read, (unsigned int)content_length, 
                             (float)total_read * 100.0f / content_length);
                }
            }
        }
        
        fclose(file);
        http->Close();
        
        if (!download_success) {
            ESP_LOGE(TAG, "Download failed, removing partial file");
            unlink(full_path);
            return false;
        }
        
        ESP_LOGI(TAG, "Download completed, validating animations_mega.bin...");
        
        // Validate the downloaded mega file by reading it back
        if (!ValidateMegaAnimationFileFromDisk(full_path)) {
            ESP_LOGE(TAG, "Downloaded animations_mega.bin failed validation");
            unlink(full_path);
            return false;
        }
        
        ESP_LOGI(TAG, "Successfully downloaded and saved animations_mega.bin (%u bytes)", (unsigned int)total_read);
        return true;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in DownloadMegaAnimationFile: %s", e.what());
        return false;
    }
}

bool AnimationUpdater::SaveAnimationToSpiffs(const std::string& filename, const std::string& data) {
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename.c_str());
    
    FILE* file = fopen(full_path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", full_path);
        return false;
    }
    
    size_t written = fwrite(data.data(), 1, data.size(), file);
    fclose(file);
    
    if (written != data.size()) {
        ESP_LOGE(TAG, "Failed to write complete file: %s (written: %zu, expected: %zu)", 
                 filename.c_str(), written, data.size());
        return false;
    }
    
    ESP_LOGI(TAG, "Saved animation file: %s (%zu bytes)", filename.c_str(), written);
    return true;
}

// NEW: Save animations_mega.bin to SPIFFS
bool AnimationUpdater::SaveMegaAnimationToSpiffs(const std::string& data) {
    const char* filename = "animations_mega.bin";
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);
    
    ESP_LOGI(TAG, "Saving animations_mega.bin to SPIFFS (%zu bytes)...", data.size());
    
    // Remove existing file if it exists
    if (access(full_path, F_OK) == 0) {
        ESP_LOGI(TAG, "Removing existing animations_mega.bin...");
        if (unlink(full_path) != 0) {
            ESP_LOGW(TAG, "Failed to remove existing file: %s", full_path);
        }
    }
    
    FILE* file = fopen(full_path, "wb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", full_path);
        return false;
    }
    
    size_t written = fwrite(data.data(), 1, data.size(), file);
    fclose(file);
    
    if (written != data.size()) {
        ESP_LOGE(TAG, "Failed to write complete animations_mega.bin (written: %zu, expected: %zu)", 
                 written, data.size());
        return false;
    }
    
    ESP_LOGI(TAG, "✅ Successfully saved animations_mega.bin (%zu bytes)", written);
    
    // Verify the file was written correctly
    FILE* verify_file = fopen(full_path, "rb");
    if (verify_file) {
        fseek(verify_file, 0, SEEK_END);
        size_t file_size = ftell(verify_file);
        fclose(verify_file);
        
        if (file_size == data.size()) {
            ESP_LOGI(TAG, "✅ File verification successful: %zu bytes", file_size);
            return true;
        } else {
            ESP_LOGE(TAG, "❌ File verification failed: expected %zu, got %zu", data.size(), file_size);
            return false;
        }
    } else {
        ESP_LOGE(TAG, "❌ Failed to verify written file");
        return false;
    }
}

bool AnimationUpdater::ValidateAnimationFile(const std::string& data) {
    if (data.size() < 24) { // Minimum size for our custom format
        ESP_LOGE(TAG, "File too small: %zu bytes", data.size());
        return false;
    }
    
    // Check magic number (0x4C56474C = "LVGL" in little endian)
    const uint32_t* header = reinterpret_cast<const uint32_t*>(data.data());
    if (header[0] != 0x4C56474C) {
        ESP_LOGE(TAG, "Invalid magic number: 0x%x", header[0]);
        return false;
    }
    
    // Basic validation passed
    return true;
}

// NEW: Validate animations_mega.bin file
bool AnimationUpdater::ValidateMegaAnimationFile(const std::string& data) {
    if (data.size() < 24) { // Minimum size for one frame
        ESP_LOGE(TAG, "Mega file too small: %zu bytes", data.size());
        return false;
    }
    
    ESP_LOGI(TAG, "Validating animations_mega.bin (%zu bytes)...", data.size());
    
    // Animation frame counts: Normal(3), Embarrass(3), Fire(4), Happy(4), Inspiration(4), Question(4), Shy(2), Sleep(4)
    int animation_frame_counts[] = {3, 3, 4, 4, 4, 4, 2, 4};
    int total_expected_frames = 0;
    for (int i = 0; i < 8; i++) {
        total_expected_frames += animation_frame_counts[i];
    }
    
    ESP_LOGI(TAG, "Expected total frames: %d", total_expected_frames);
    
    // Validate each frame in the mega file
    size_t offset = 0;
    int frame_count = 0;
    
    for (int anim_idx = 0; anim_idx < 8; anim_idx++) {
        int frame_count_for_anim = animation_frame_counts[anim_idx];
        
        for (int frame_idx = 0; frame_idx < frame_count_for_anim; frame_idx++) {
            if (offset + 24 > data.size()) {
                ESP_LOGE(TAG, "Mega file truncated at frame %d (offset %zu)", frame_count, offset);
                return false;
            }
            
            // Read header (6 uint32_t values)
            const uint32_t* header = reinterpret_cast<const uint32_t*>(data.data() + offset);
            
            // Validate magic number
            if (header[0] != 0x4C56474C) {
                ESP_LOGE(TAG, "Invalid magic number for frame %d: 0x%x", frame_count, header[0]);
                return false;
            }
            
            // Calculate expected data size
            uint32_t width = header[3];
            uint32_t height = header[4];
            uint32_t stride = header[5];
            size_t frame_data_size = height * stride;
            size_t total_frame_size = 24 + frame_data_size; // 24 bytes header + data
            
            if (offset + total_frame_size > data.size()) {
                ESP_LOGE(TAG, "Frame %d data truncated (offset %zu, size %zu, total %zu)", 
                         frame_count, offset, total_frame_size, data.size());
                return false;
            }
            
            ESP_LOGD(TAG, "Frame %d: %dx%d, %zu bytes", frame_count, width, height, frame_data_size);
            
            offset += total_frame_size;
            frame_count++;
        }
    }
    
    ESP_LOGI(TAG, "✅ Successfully validated animations_mega.bin with %d frames", frame_count);
    return true;
}

// NEW: Validate animations_mega.bin file from disk (memory-efficient)
bool AnimationUpdater::ValidateMegaAnimationFileFromDisk(const char* file_path) {
    ESP_LOGI(TAG, "Validating animations_mega.bin from disk: %s", file_path);
    
    FILE* f = fopen(file_path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for validation: %s", file_path);
        return false;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size < 24) { // Minimum size for one frame
        ESP_LOGE(TAG, "Mega file too small: %zu bytes", file_size);
        fclose(f);
        return false;
    }
    
    ESP_LOGI(TAG, "Validating animations_mega.bin (%zu bytes)...", file_size);
    
    // Animation frame counts: Normal(3), Embarrass(3), Fire(4), Happy(4), Inspiration(4), Question(4), Shy(2), Sleep(4)
    int animation_frame_counts[] = {3, 3, 4, 4, 4, 4, 2, 4};
    int total_expected_frames = 0;
    for (int i = 0; i < 8; i++) {
        total_expected_frames += animation_frame_counts[i];
    }
    
    ESP_LOGI(TAG, "Expected total frames: %d", total_expected_frames);
    
    // Validate each frame in the mega file
    int frame_count = 0;
    
    for (int anim_idx = 0; anim_idx < 8; anim_idx++) {
        int frame_count_for_anim = animation_frame_counts[anim_idx];
        
        for (int frame_idx = 0; frame_idx < frame_count_for_anim; frame_idx++) {
            // Read header (6 uint32_t values)
            uint32_t header_data[6];
            if (fread(header_data, sizeof(uint32_t), 6, f) != 6) {
                ESP_LOGE(TAG, "Failed to read header for frame %d", frame_count);
                fclose(f);
                return false;
            }
            
            // Validate magic number
            if (header_data[0] != 0x4C56474C) {
                ESP_LOGE(TAG, "Invalid magic number for frame %d: 0x%x", frame_count, header_data[0]);
                fclose(f);
                return false;
            }
            
            // Calculate expected data size
            uint32_t width = header_data[3];
            uint32_t height = header_data[4];
            uint32_t stride = header_data[5];
            size_t frame_data_size = height * stride;
            
            // Skip the pixel data
            if (fseek(f, frame_data_size, SEEK_CUR) != 0) {
                ESP_LOGE(TAG, "Failed to skip frame %d data", frame_count);
                fclose(f);
                return false;
            }
            
            ESP_LOGD(TAG, "Frame %d: %dx%d, %zu bytes", frame_count, width, height, frame_data_size);
            frame_count++;
        }
    }
    
    fclose(f);
    
    ESP_LOGI(TAG, "✅ Successfully validated animations_mega.bin with %d frames", frame_count);
    return true;
}

void AnimationUpdater::ReloadAnimations() {
    ESP_LOGI(TAG, "Reloading animations from SPIFFS");
    
    // Reload SPIFFS animations
    animation_load_spiffs_animations();
    
    ESP_LOGI(TAG, "Animations reloaded successfully");
    
    // Show current animation sources for debugging
    animation_show_current_sources();
}

void AnimationUpdater::LoadConfiguration() {
    // Use hardcoded configuration for testing
    server_url_ = DEFAULT_SERVER_URL;
    check_interval_seconds_ = 10;  // 10 seconds
    enabled_.store(true);  // Always enabled for testing
    
    ESP_LOGI(TAG, "Configuration loaded:");
    ESP_LOGI(TAG, "  Server URL: %s", server_url_.c_str());
    ESP_LOGI(TAG, "  Check Interval: %u seconds", check_interval_seconds_);
    ESP_LOGI(TAG, "  Enabled: %s", enabled_.load() ? "true" : "false");
}

void AnimationUpdater::SaveConfiguration() {
    // Configuration is hardcoded, no need to save
    ESP_LOGD(TAG, "Configuration is hardcoded, no save needed");
}
