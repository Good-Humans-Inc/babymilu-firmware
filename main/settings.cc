#include "settings.h"

#include <esp_log.h>
#include <nvs_flash.h>

#define TAG "Settings"

Settings::Settings(const std::string& ns, bool read_write) : ns_(ns), read_write_(read_write) {
    esp_err_t err = nvs_open(ns.c_str(), read_write_ ? NVS_READWRITE : NVS_READONLY, &nvs_handle_);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS namespace '%s' not found (will use defaults)", ns.c_str());
        } else {
            ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", ns.c_str(), esp_err_to_name(err));
        }
        nvs_handle_ = 0;
    }
}

Settings::~Settings() {
    if (nvs_handle_ != 0) {
        if (read_write_ && dirty_) {
            ESP_ERROR_CHECK(nvs_commit(nvs_handle_));
        }
        nvs_close(nvs_handle_);
    }
}

std::string Settings::GetString(const std::string& key, const std::string& default_value) {
    if (nvs_handle_ == 0) {
        return default_value;
    }

    size_t length = 0;
    if (nvs_get_str(nvs_handle_, key.c_str(), nullptr, &length) != ESP_OK) {
        return default_value;
    }

    std::string value;
    value.resize(length);
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle_, key.c_str(), value.data(), &length));
    while (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

void Settings::SetString(const std::string& key, const std::string& value) {
    if (nvs_handle_ == 0) {
        ESP_LOGE(TAG, "NVS handle is invalid, cannot set string for key '%s'", key.c_str());
        return;
    }
    
    if (read_write_) {
        esp_err_t err = nvs_set_str(nvs_handle_, key.c_str(), value.c_str());
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set string for key '%s': %s", key.c_str(), esp_err_to_name(err));
            return;
        }
        dirty_ = true;
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());
    }
}

int32_t Settings::GetInt(const std::string& key, int32_t default_value) {
    if (nvs_handle_ == 0) {
        return default_value;
    }

    int32_t value;
    if (nvs_get_i32(nvs_handle_, key.c_str(), &value) != ESP_OK) {
        return default_value;
    }
    return value;
}

void Settings::SetInt(const std::string& key, int32_t value) {
    if (nvs_handle_ == 0) {
        ESP_LOGE(TAG, "NVS handle is invalid, cannot set int for key '%s'", key.c_str());
        return;
    }
    
    if (read_write_) {
        esp_err_t err = nvs_set_i32(nvs_handle_, key.c_str(), value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set int for key '%s': %s", key.c_str(), esp_err_to_name(err));
            return;
        }
        dirty_ = true;
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());
    }
}

void Settings::EraseKey(const std::string& key) {
    if (nvs_handle_ == 0) {
        ESP_LOGE(TAG, "NVS handle is invalid, cannot erase key '%s'", key.c_str());
        return;
    }

    if (read_write_) {
        auto ret = nvs_erase_key(nvs_handle_, key.c_str());
        if (ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_ERROR_CHECK(ret);
        }
        dirty_ = true;
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());
    }
}

void Settings::EraseAll() {
    if (nvs_handle_ == 0) {
        ESP_LOGE(TAG, "NVS handle is invalid, cannot erase namespace '%s'", ns_.c_str());
        return;
    }

    if (read_write_) {
        ESP_ERROR_CHECK(nvs_erase_all(nvs_handle_));
        dirty_ = true;
    } else {
        ESP_LOGW(TAG, "Namespace %s is not open for writing", ns_.c_str());
    }
}
