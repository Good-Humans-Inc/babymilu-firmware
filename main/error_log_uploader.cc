#include "error_log_uploader.h"

#include "system_info.h"

#include <sdkconfig.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <time.h>

#define TAG "ErrorLogUpload"

namespace {

constexpr size_t kMaxFileSize = 100 * 1024;
static bool s_error_logging_enabled = false;
static bool s_in_hook = false;
static vprintf_like_t s_original_vprintf = nullptr;
static SemaphoreHandle_t s_log_mutex = nullptr;

std::string CurrentTimestamp() {
    time_t now = time(nullptr);
    struct tm timeinfo = {};
    gmtime_r(&now, &timeinfo);
    char timestamp[32] = {};
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
    return std::string(timestamp);
}

esp_err_t ReadErrorLogFile(std::string& content) {
    FILE* file = fopen(ErrorLogUploader::kErrorLogFile, "rb");
    if (file == nullptr) {
        ESP_LOGI(TAG, "no error log file to upload: %s", ErrorLogUploader::kErrorLogFile);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (file_size <= 0) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    if (file_size > static_cast<long>(kMaxFileSize)) {
        ESP_LOGW(TAG, "err.txt too large (%ld bytes), uploading first %u bytes",
                 file_size, static_cast<unsigned>(kMaxFileSize));
        file_size = static_cast<long>(kMaxFileSize);
    }

    content.resize(static_cast<size_t>(file_size));
    size_t bytes_read = fread(content.data(), 1, content.size(), file);
    fclose(file);
    if (bytes_read != content.size()) {
        content.resize(bytes_read);
    }
    return content.empty() ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

std::string BuildMultipartBody(const std::string& boundary, const std::string& file_content) {
    std::string body;
    const std::string device_id = system_info::GetMacAddress();
    const std::string timestamp = CurrentTimestamp();
    body.reserve(file_content.size() + 512);
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"device_id\"\r\n\r\n";
    body += device_id + "\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"timestamp\"\r\n\r\n";
    body += timestamp + "\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"err.txt\"\r\n";
    body += "Content-Type: text/plain\r\n\r\n";
    body += file_content;
    body += "\r\n--" + boundary + "--\r\n";
    ESP_LOGI(TAG, "prepared error log upload device_id=%s timestamp=%s bytes=%u",
             device_id.c_str(), timestamp.c_str(), static_cast<unsigned>(file_content.size()));
    return body;
}

int ErrorLogVprintfHook(const char* format, va_list args) {
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
    int result = 0;
    if (s_original_vprintf) {
        va_list args_copy;
        va_copy(args_copy, args);
        result = s_original_vprintf(format, args_copy);
        va_end(args_copy);
    }

    if (s_error_logging_enabled && s_log_mutex != nullptr &&
        xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        char log_buffer[512] = {};
        va_list args_copy;
        va_copy(args_copy, args);
        int len = vsnprintf(log_buffer, sizeof(log_buffer), format, args_copy);
        va_end(args_copy);

        const bool warning_or_error = len >= 2 && len < static_cast<int>(sizeof(log_buffer)) &&
                                      ((log_buffer[0] == 'E' && log_buffer[1] == ' ') ||
                                       (log_buffer[0] == 'W' && log_buffer[1] == ' '));
        if (warning_or_error) {
            FILE* file = fopen(ErrorLogUploader::kErrorLogFile, "a");
            if (file != nullptr) {
                fwrite(log_buffer, 1, static_cast<size_t>(len), file);
                fflush(file);
                fclose(file);
            }
        }
        xSemaphoreGive(s_log_mutex);
    }

    s_in_hook = false;
    return result;
}

}  // namespace

esp_err_t ErrorLogUploader::UploadErrorLog() {
    std::string file_content;
    esp_err_t read_result = ReadErrorLogFile(file_content);
    if (read_result != ESP_OK) {
        return read_result;
    }

    const std::string boundary = "----ESP32_ERROR_LOG_BOUNDARY";
    const std::string body = BuildMultipartBody(boundary, file_content);
    esp_http_client_config_t cfg = {};
    cfg.url = CONFIG_ECHOEAR_BABYMILU_ERROR_UPLOAD_URL;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        ESP_LOGE(TAG, "failed to create upload HTTP client");
        return ESP_FAIL;
    }

    std::string content_type = "multipart/form-data; boundary=" + boundary;
    esp_http_client_set_header(client, "Content-Type", content_type.c_str());
    if (strlen(CONFIG_ECHOEAR_BABYMILU_ERROR_UPLOAD_API_KEY) > 0) {
        esp_http_client_set_header(client, "X-API-Key", CONFIG_ECHOEAR_BABYMILU_ERROR_UPLOAD_API_KEY);
    }
    esp_http_client_set_post_field(client, body.data(), static_cast<int>(body.size()));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "error log upload failed err=%s status=%d", esp_err_to_name(err), status);
        return err == ESP_OK ? ESP_FAIL : err;
    }

    ESP_LOGI(TAG, "error log uploaded successfully; deleting %s", kErrorLogFile);
    remove(kErrorLogFile);
    return ESP_OK;
}

void ErrorLogUploader::EnableErrorLoggingToSD() {
    if (s_error_logging_enabled) {
        return;
    }
    if (s_log_mutex == nullptr) {
        s_log_mutex = xSemaphoreCreateMutex();
        if (s_log_mutex == nullptr) {
            ESP_LOGE(TAG, "failed to create error log mutex");
            return;
        }
    }
    s_original_vprintf = esp_log_set_vprintf(ErrorLogVprintfHook);
    if (s_original_vprintf == nullptr) {
        s_original_vprintf = vprintf;
    }
    s_error_logging_enabled = true;
    ESP_LOGI(TAG, "SD error-log hook enabled for warning/error logs at %s", kErrorLogFile);
}

void ErrorLogUploader::DisableErrorLoggingToSD() {
    if (!s_error_logging_enabled) {
        return;
    }
    if (s_original_vprintf != nullptr) {
        esp_log_set_vprintf(s_original_vprintf);
        s_original_vprintf = nullptr;
    }
    s_error_logging_enabled = false;
}
