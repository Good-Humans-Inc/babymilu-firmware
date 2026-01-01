#include "error_log_uploader.h"
#include "sd_card.h"
#include "system_info.h"
#include "board.h"

#include <esp_log.h>
#include <sys/stat.h>
#include <time.h>
#include <cstring>

#define TAG "ErrorLogUpload"

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
    return ESP_OK;
}

