#ifndef ERROR_LOG_UPLOADER_H
#define ERROR_LOG_UPLOADER_H

#include <esp_err.h>
#include <string>

/**
 * @brief Error log uploader for uploading err.txt from SD card
 */
class ErrorLogUploader {
public:
    /**
     * @brief Upload error log file from /sdcard/err.txt to the server
     * 
     * Uploads the error log file using multipart/form-data POST request
     * with device_id, timestamp, and file fields, matching the curl format.
     * 
     * @return ESP_OK on success, error code on failure
     */
    static esp_err_t UploadErrorLog();

private:
    static constexpr const char* ERROR_LOG_FILE = "/sdcard/err.txt";
    static constexpr const char* UPLOAD_URL = "https://upload-error-log-tmdtlnu7zq-uc.a.run.app";
    static constexpr const char* API_KEY = "test-key-123";
    
    static std::string GetCurrentTimestamp();
    static esp_err_t ReadErrorLogFile(std::string& content);
};

#endif // ERROR_LOG_UPLOADER_H

