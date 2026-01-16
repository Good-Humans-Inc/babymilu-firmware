#include "error_log_uploader.h"
#include "sd_card.h"
#include "system_info.h"
#include "board.h"

#include <esp_log.h>
#include <sys/stat.h>
#include <time.h>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define TAG "ErrorLogUpload"

// Static variables for error logging hook
static bool s_error_logging_enabled = false;
static vprintf_like_t s_original_vprintf = nullptr;
static SemaphoreHandle_t s_log_mutex = nullptr;
static bool s_in_hook = false; // Flag to prevent recursion

std::string ErrorLogUploader::GetCurrentTimestamp() {
    time_t now = time(NULL);
    struct tm timeinfo;
    
    // Use UTC time for ISO 8601 format
    gmtime_r(&now, &timeinfo);
    
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    
    return std::string(timestamp);
}

esp_err_t ErrorLogUploader::ReadErrorLogFile(std::string& content) {
    FILE* file = fopen(ERROR_LOG_FILE, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open error log file: %s", ERROR_LOG_FILE);
        return ESP_FAIL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size < 0) {
        ESP_LOGE(TAG, "Failed to get file size");
        fclose(file);
        return ESP_FAIL;
    }
    
    // Limit file size to prevent memory issues (100KB max)
    const long MAX_FILE_SIZE = 100 * 1024;
    if (file_size > MAX_FILE_SIZE) {
        ESP_LOGW(TAG, "File too large (%ld bytes), limiting to %ld bytes", file_size, MAX_FILE_SIZE);
        file_size = MAX_FILE_SIZE;
    }
    
    ESP_LOGI(TAG, "Reading %ld bytes from error log file", file_size);
    
    // Read file content
    content.resize(file_size);
    size_t bytes_read = fread(content.data(), 1, file_size, file);
    fclose(file);
    
    // Resize to actual bytes read
    content.resize(bytes_read);
    
    ESP_LOGI(TAG, "Successfully read %zu bytes from error log file", bytes_read);
    return ESP_OK;
}

esp_err_t ErrorLogUploader::UploadErrorLog() {
    ESP_LOGI(TAG, "Starting error log upload");
    
    // Read error log file
    std::string file_content;
    esp_err_t ret = ReadErrorLogFile(file_content);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read error log file");
        return ret;
    }
    
    if (file_content.empty()) {
        ESP_LOGW(TAG, "Error log file is empty, skipping upload");
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Get device ID (MAC address)
    std::string device_id = SystemInfo::GetMacAddress();
    
    // Get timestamp in ISO 8601 format
    std::string timestamp = GetCurrentTimestamp();
    
    ESP_LOGI(TAG, "Uploading error log: device_id=%s, timestamp=%s, file_size=%zu", 
             device_id.c_str(), timestamp.c_str(), file_content.size());
    
    // Create HTTP client
    auto& board = Board::GetInstance();
    auto http = board.CreateHttp();
    if (!http) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return ESP_FAIL;
    }
    
    // Construct multipart/form-data request body
    std::string boundary = "----ESP32_ERROR_LOG_BOUNDARY";
    
    // Construct device_id field
    std::string device_id_field;
    device_id_field += "--" + boundary + "\r\n";
    device_id_field += "Content-Disposition: form-data; name=\"device_id\"\r\n";
    device_id_field += "\r\n";
    device_id_field += device_id + "\r\n";
    
    // Construct timestamp field
    std::string timestamp_field;
    timestamp_field += "--" + boundary + "\r\n";
    timestamp_field += "Content-Disposition: form-data; name=\"timestamp\"\r\n";
    timestamp_field += "\r\n";
    timestamp_field += timestamp + "\r\n";
    
    // Construct file field header
    std::string file_header;
    file_header += "--" + boundary + "\r\n";
    file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"err.txt\"\r\n";
    file_header += "Content-Type: text/plain\r\n";
    file_header += "\r\n";
    
    // Construct footer
    std::string multipart_footer;
    multipart_footer += "\r\n--" + boundary + "--\r\n";
    
    // Set headers
    http->SetHeader("X-API-Key", API_KEY);
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked");
    
    // Open connection
    if (!http->Open("POST", UPLOAD_URL)) {
        ESP_LOGE(TAG, "Failed to connect to upload URL: %s", UPLOAD_URL);
        http->Close();
        return ESP_FAIL;
    }
    
    // Write multipart data
    // First: device_id field
    http->Write(device_id_field.c_str(), device_id_field.size());
    
    // Second: timestamp field
    http->Write(timestamp_field.c_str(), timestamp_field.size());
    
    // Third: file field header
    http->Write(file_header.c_str(), file_header.size());
    
    // Fourth: file content
    http->Write(file_content.c_str(), file_content.size());
    
    // Fifth: multipart footer
    http->Write(multipart_footer.c_str(), multipart_footer.size());
    
    // End chunked transfer
    http->Write("", 0);
    
    // Check status code
    int status_code = http->GetStatusCode();
    std::string response = http->ReadAll();
    http->Close();
    
    if (status_code != 200) {
        ESP_LOGE(TAG, "Upload failed with status code %d, response: %s", status_code, response.c_str());
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Error log uploaded successfully, status: %d, response: %s", status_code, response.c_str());
    
    // After successful upload, delete the error log file to start fresh
    // Note: This will be done after enabling error logging, so new errors can be captured
    remove(ERROR_LOG_FILE);
    
    return ESP_OK;
}

// vprintf hook function that captures ESP log messages and writes to SD card
int ErrorLogVprintfHook(const char* format, va_list args) {
    // Prevent recursion - if we're already in the hook, just call original and return
    if (s_in_hook) {
        if (s_original_vprintf) {
            va_list args_copy;
            va_copy(args_copy, args);
            int result = s_original_vprintf(format, args_copy);
            va_end(args_copy);
            return result;
        }
        return 0;
    }
    
    s_in_hook = true;
    
    // First call the original vprintf to maintain normal logging
    int result = 0;
    if (s_original_vprintf) {
        va_list args_copy;
        va_copy(args_copy, args);
        result = s_original_vprintf(format, args_copy);
        va_end(args_copy);
    }
    
    // If error logging is enabled and SD card is mounted, write to file
    if (s_error_logging_enabled && SdCard::IsMounted()) {
        // Take mutex to prevent concurrent writes
        if (s_log_mutex && xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Format the log message
            char log_buffer[512];
            va_list args_copy2;
            va_copy(args_copy2, args);
            int len = vsnprintf(log_buffer, sizeof(log_buffer), format, args_copy2);
            va_end(args_copy2);
            
            if (len > 0 && len < (int)sizeof(log_buffer)) {
                // Filter: Only write ERROR (E) and WARNING (W) messages to err.txt
                // ESP-IDF log format: "E (timestamp) TAG: message" or "W (timestamp) TAG: message"
                // Skip INFO (I) and DEBUG (D) messages to keep the error log focused
                bool should_log = false;
                if (len >= 2) {
                    // Check if message starts with "E (" for ERROR or "W (" for WARNING
                    if ((log_buffer[0] == 'E' && log_buffer[1] == ' ') ||
                        (log_buffer[0] == 'W' && log_buffer[1] == ' ')) {
                        should_log = true;
                    }
                }
                
                if (should_log) {
                    // Write directly to file to avoid recursion from AppendToFile's logging
                    // This bypasses SdCard::AppendToFile which would log and cause recursion
                    // Use the same path as ERROR_LOG_FILE constant: "/sdcard/err.txt"
                    const char* full_path = "/sdcard/err.txt";
                    FILE* file = fopen(full_path, "a");  // "a" = append mode
                    if (file != NULL) {
                        size_t bytes_written = fwrite(log_buffer, 1, len, file);
                        fflush(file);  // Ensure data is flushed
                        fclose(file);
                        // Ignore write errors silently to avoid recursion
                        (void)bytes_written;
                    }
                    // If file open fails, silently ignore to avoid recursion
                }
            }
            
            xSemaphoreGive(s_log_mutex);
        }
    }
    
    s_in_hook = false;
    return result;
}

void ErrorLogUploader::EnableErrorLoggingToSD() {
    if (s_error_logging_enabled) {
        ESP_LOGW(TAG, "Error logging to SD card is already enabled");
        return;
    }
    
    // Check if SD card is mounted
    if (!SdCard::IsMounted()) {
        ESP_LOGW(TAG, "SD card is not mounted, cannot enable error logging to SD card");
        return;
    }
    
    // Create mutex if it doesn't exist
    if (s_log_mutex == nullptr) {
        s_log_mutex = xSemaphoreCreateMutex();
        if (s_log_mutex == nullptr) {
            ESP_LOGE(TAG, "Failed to create mutex for error logging");
            return;
        }
    }
    
    // Store the original vprintf function
    s_original_vprintf = esp_log_set_vprintf(ErrorLogVprintfHook);
    
    if (s_original_vprintf == nullptr) {
        ESP_LOGW(TAG, "esp_log_set_vprintf returned nullptr, logging hook may not work correctly");
        // Still set a default if needed
        s_original_vprintf = vprintf;
    }
    
    s_error_logging_enabled = true;
    ESP_LOGI(TAG, "Error logging to SD card enabled - ESP errors will be written to /sdcard/err.txt");
}

void ErrorLogUploader::DisableErrorLoggingToSD() {
    if (!s_error_logging_enabled) {
        ESP_LOGW(TAG, "Error logging to SD card is not enabled");
        return;
    }
    
    // Restore the original vprintf function
    if (s_original_vprintf) {
        esp_log_set_vprintf(s_original_vprintf);
        s_original_vprintf = nullptr;
    }
    
    s_error_logging_enabled = false;
    ESP_LOGI(TAG, "Error logging to SD card disabled");
}

