#include "settings_store.h"

#include <esp_log.h>

#define TAG "SettingsStore"

SettingsStore::SettingsStore(const char* ns, bool read_write)
    : ns_(ns ? ns : ""), read_write_(read_write) {
    esp_err_t err = nvs_open(ns_.c_str(), read_write_ ? NVS_READWRITE : NVS_READONLY, &handle_);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "open namespace %s failed: %s", ns_.c_str(), esp_err_to_name(err));
        }
        handle_ = 0;
    }
}

SettingsStore::~SettingsStore() {
    if (handle_ != 0) {
        if (read_write_ && dirty_) {
            esp_err_t err = nvs_commit(handle_);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "commit namespace %s failed: %s", ns_.c_str(), esp_err_to_name(err));
            }
        }
        nvs_close(handle_);
    }
}

std::string SettingsStore::GetString(const char* key, const std::string& fallback) const {
    if (handle_ == 0 || key == nullptr) {
        return fallback;
    }
    size_t len = 0;
    if (nvs_get_str(handle_, key, nullptr, &len) != ESP_OK || len == 0) {
        return fallback;
    }
    std::string value(len, '\0');
    if (nvs_get_str(handle_, key, value.data(), &len) != ESP_OK) {
        return fallback;
    }
    while (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

int32_t SettingsStore::GetInt(const char* key, int32_t fallback) const {
    if (handle_ == 0 || key == nullptr) {
        return fallback;
    }
    int32_t value = fallback;
    if (nvs_get_i32(handle_, key, &value) != ESP_OK) {
        return fallback;
    }
    return value;
}

bool SettingsStore::GetBool(const char* key, bool fallback) const {
    return GetInt(key, fallback ? 1 : 0) != 0;
}

void SettingsStore::SetString(const char* key, const std::string& value) {
    if (!read_write_ || handle_ == 0 || key == nullptr) {
        return;
    }
    if (nvs_set_str(handle_, key, value.c_str()) == ESP_OK) {
        dirty_ = true;
    }
}

void SettingsStore::SetInt(const char* key, int32_t value) {
    if (!read_write_ || handle_ == 0 || key == nullptr) {
        return;
    }
    if (nvs_set_i32(handle_, key, value) == ESP_OK) {
        dirty_ = true;
    }
}

void SettingsStore::SetBool(const char* key, bool value) {
    SetInt(key, value ? 1 : 0);
}

void SettingsStore::EraseKey(const char* key) {
    if (!read_write_ || handle_ == 0 || key == nullptr) {
        return;
    }
    esp_err_t err = nvs_erase_key(handle_, key);
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        dirty_ = true;
    }
}

void SettingsStore::EraseAll() {
    if (!read_write_ || handle_ == 0) {
        return;
    }
    if (nvs_erase_all(handle_) == ESP_OK) {
        dirty_ = true;
    }
}
