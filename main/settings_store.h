#pragma once

#include <cstdint>
#include <string>

#include <nvs.h>

class SettingsStore {
public:
    SettingsStore(const char* ns, bool read_write);
    ~SettingsStore();

    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;

    std::string GetString(const char* key, const std::string& fallback = "") const;
    int32_t GetInt(const char* key, int32_t fallback = 0) const;
    bool GetBool(const char* key, bool fallback = false) const;

    void SetString(const char* key, const std::string& value);
    void SetInt(const char* key, int32_t value);
    void SetBool(const char* key, bool value);
    void EraseKey(const char* key);
    void EraseAll();

private:
    std::string ns_;
    bool read_write_ = false;
    mutable nvs_handle_t handle_ = 0;
    bool dirty_ = false;
};
