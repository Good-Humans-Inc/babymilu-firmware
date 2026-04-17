#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "backlight.h"
#include "board.h"
#include "animation/animation_updater.h"
#include "animation/animation.h"
#include "sd_card.h"
#include "sd_card_startup.h"
#include "power_save_timer.h"
#include "system_info.h"

#include <wifi_station.h>
#include <ssid_manager.h>
#include "settings.h"
#include <esp_log.h>
#include <esp_random.h>
#include <cJSON.h>

#include <driver/i2c_master.h>
#include "i2c_device.h"
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_st77916.h>
#include "touch.h"
#include <touch_sensor_lowlevel.h>
#include <touch_button_sensor.h>

#include "driver/temperature_sensor.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// BMI270 includes
#include "bmi270_api.h"
#include "i2c_bus.h"

#define TAG "EchoEar"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);
temperature_sensor_handle_t temp_sensor = NULL;

static bool WaitForAnimationUpdaterCheckToFinish() {
    AnimationUpdater& updater = AnimationUpdater::GetInstance();
    const TickType_t wait_step = pdMS_TO_TICKS(500);
    const int max_wait_for_start_steps = 6;    // 3 seconds max to observe updater start
    const int max_wait_for_finish_steps = 240; // 120 seconds max to observe updater finish

    bool saw_running = false;
    for (int i = 0; i < max_wait_for_start_steps; ++i) {
        if (updater.IsRunning()) {
            saw_running = true;
            ESP_LOGI(TAG, "[FIRESTORE] Animation updater has started; waiting for check to finish");
            break;
        }
        vTaskDelay(wait_step);
    }

    if (!saw_running) {
        ESP_LOGW(TAG, "[FIRESTORE] Timed out waiting 3 seconds for animation updater to start; continuing with Firestore request");
        return false;
    }

    for (int i = 0; i < max_wait_for_finish_steps; ++i) {
        if (!updater.IsRunning()) {
            ESP_LOGI(TAG, "[FIRESTORE] Animation updater check finished");
            return true;
        }
        vTaskDelay(wait_step);
    }

    ESP_LOGW(TAG, "[FIRESTORE] Timed out waiting for animation updater to finish");
    return false;
}

static void ApplyFirestoreWifiRanking(const std::string& response) {
    cJSON* root = cJSON_Parse(response.c_str());
    if (!root) {
        ESP_LOGE(TAG, "[FIRESTORE] Failed to parse Firestore JSON response");
        return;
    }

    cJSON* fields = cJSON_GetObjectItem(root, "fields");
    cJSON* wifi_setting = fields ? cJSON_GetObjectItem(fields, "wifiSetting") : nullptr;
    cJSON* wifi_map = wifi_setting ? cJSON_GetObjectItem(wifi_setting, "mapValue") : nullptr;
    cJSON* wifi_fields = wifi_map ? cJSON_GetObjectItem(wifi_map, "fields") : nullptr;
    cJSON* ranked_networks = wifi_fields ? cJSON_GetObjectItem(wifi_fields, "rankedNetworks") : nullptr;
    cJSON* array_value = ranked_networks ? cJSON_GetObjectItem(ranked_networks, "arrayValue") : nullptr;
    cJSON* values = array_value ? cJSON_GetObjectItem(array_value, "values") : nullptr;

    if (!cJSON_IsArray(values)) {
        ESP_LOGW(TAG, "[FIRESTORE] No wifiSetting.rankedNetworks array found in Firestore document");
        cJSON_Delete(root);
        return;
    }

    std::vector<std::string> ranked_ssids;
    cJSON* value = nullptr;
    cJSON_ArrayForEach(value, values) {
        cJSON* string_value = cJSON_GetObjectItem(value, "stringValue");
        if (cJSON_IsString(string_value) && string_value->valuestring != nullptr) {
            ranked_ssids.emplace_back(string_value->valuestring);
        }
    }

    cJSON_Delete(root);

    if (ranked_ssids.empty()) {
        ESP_LOGW(TAG, "[FIRESTORE] rankedNetworks exists but is empty");
        return;
    }

    auto& ssid_manager = SsidManager::GetInstance();
    const auto& current = ssid_manager.GetSsidList();
    if (current.empty()) {
        ESP_LOGW(TAG, "[FIRESTORE] No on-device WiFi credentials to reorder");
        return;
    }

    std::vector<SsidItem> reordered;
    reordered.reserve(current.size());

    for (const auto& ranked_ssid : ranked_ssids) {
        auto it = std::find_if(current.begin(), current.end(), [&ranked_ssid](const SsidItem& item) {
            return item.ssid == ranked_ssid;
        });
        if (it != current.end()) {
            reordered.push_back(*it);
        }
    }

    for (const auto& item : current) {
        auto it = std::find_if(reordered.begin(), reordered.end(), [&item](const SsidItem& ranked_item) {
            return ranked_item.ssid == item.ssid;
        });
        if (it == reordered.end()) {
            reordered.push_back(item);
        }
    }

    bool order_changed = reordered.size() != current.size();
    if (!order_changed) {
        for (size_t i = 0; i < current.size(); ++i) {
            if (current[i].ssid != reordered[i].ssid) {
                order_changed = true;
                break;
            }
        }
    }

    if (!order_changed) {
        ESP_LOGI(TAG, "[FIRESTORE] WiFi credential order already matches Firestore ranking");
        return;
    }

    ESP_LOGI(TAG, "[FIRESTORE] Reordering WiFi credentials to match Firestore ranking");
    ssid_manager.Clear();
    for (auto it = reordered.rbegin(); it != reordered.rend(); ++it) {
        ssid_manager.AddSsid(it->ssid, it->password);
    }

    if (!reordered.empty()) {
        ESP_LOGI(TAG, "[FIRESTORE] Highest priority WiFi is now: %s", reordered.front().ssid.c_str());
    }
}

static void FetchFirestoreDeviceDocumentAndApplyRanking() {
    std::string mac_address = SystemInfo::GetMacAddress();
    std::string firestore_url =
        "https://firestore.googleapis.com/v1/projects/composed-augury-469200-g6/"
        "databases/(default)/documents/devices/" + mac_address + "/";

    std::unique_ptr<Http> http(Board::GetInstance().CreateHttp());
    if (!http) {
        ESP_LOGE(TAG, "[FIRESTORE] Failed to create HTTP client");
        return;
    }

    http->SetHeader("Accept", "application/json");
    http->SetHeader("Accept-Encoding", "identity");
    http->SetTimeout(10000);

    ESP_LOGI(TAG, "[FIRESTORE] GET %s", firestore_url.c_str());
    if (!http->Open("GET", firestore_url)) {
        ESP_LOGE(TAG, "[FIRESTORE] Failed to open HTTPS connection");
        return;
    }

    int status_code = http->GetStatusCode();
    std::string response = http->ReadAll();
    if (response.empty()) {
        char buffer[256];
        int bytes_read = 0;
        while ((bytes_read = http->Read(buffer, sizeof(buffer))) > 0) {
            response.append(buffer, bytes_read);
        }
    }
    http->Close();

    ESP_LOGI(TAG, "[FIRESTORE] Status: %d", status_code);

    if (status_code == 200 && !response.empty()) {
        ApplyFirestoreWifiRanking(response);
    } else if (status_code == 200) {
        ESP_LOGW(TAG, "[FIRESTORE] Empty response body");
    }
}

static void SdAnimInitTask(void* /*arg*/) {
    ESP_LOGI(TAG, "[SD/ANIM] Background init task started on core %d", xPortGetCoreID());
    
    if (!SdCard::IsMounted()) {
        esp_err_t ret = SdCardStartup::ProcessStartup();
        ESP_LOGI(TAG, "[SD/ANIM] SdCardStartup::ProcessStartup() returned: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "[SD/ANIM] SD card already mounted, skipping startup");
    }
    
    ESP_LOGI(TAG, "[SD/ANIM] === Initializing animations ===");
    animation_init();
    ESP_LOGI(TAG, "[SD/ANIM] === Animations initialization completed ===");

    auto& wifi_station = WifiStation::GetInstance();
    const TickType_t wait_step = pdMS_TO_TICKS(500);
    const int max_wait_steps = 40;  // 20 seconds max
    bool wifi_ready = false;
    for (int i = 0; i < max_wait_steps; ++i) {
        if (wifi_station.IsConnected()) {
            wifi_ready = true;
            break;
        }
        vTaskDelay(wait_step);
    }

    if (wifi_ready) {
        ESP_LOGI(TAG, "[FIRESTORE] WiFi connected");
        WaitForAnimationUpdaterCheckToFinish();
        ESP_LOGI(TAG, "[FIRESTORE] Requesting device document after animation updater check");
        FetchFirestoreDeviceDocumentAndApplyRanking();
    } else {
        ESP_LOGW(TAG, "[FIRESTORE] WiFi was not ready within timeout, skipping device document request");
    }
    
    vTaskDelete(NULL);
}
static const st77916_lcd_init_cmd_t vendor_specific_init_yysj[] = {
    {0xF0, (uint8_t []){0x28}, 1, 0},
    {0xF2, (uint8_t []){0x28}, 1, 0},
    {0x73, (uint8_t []){0xF0}, 1, 0},
    {0x7C, (uint8_t []){0xD1}, 1, 0},
    {0x83, (uint8_t []){0xE0}, 1, 0},
    {0x84, (uint8_t []){0x61}, 1, 0},
    {0xF2, (uint8_t []){0x82}, 1, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0},
    {0xF0, (uint8_t []){0x01}, 1, 0},
    {0xF1, (uint8_t []){0x01}, 1, 0},
    {0xB0, (uint8_t []){0x56}, 1, 0},
    {0xB1, (uint8_t []){0x4D}, 1, 0},
    {0xB2, (uint8_t []){0x24}, 1, 0},
    {0xB4, (uint8_t []){0x87}, 1, 0},
    {0xB5, (uint8_t []){0x44}, 1, 0},
    {0xB6, (uint8_t []){0x8B}, 1, 0},
    {0xB7, (uint8_t []){0x40}, 1, 0},
    {0xB8, (uint8_t []){0x86}, 1, 0},
    {0xBA, (uint8_t []){0x00}, 1, 0},
    {0xBB, (uint8_t []){0x08}, 1, 0},
    {0xBC, (uint8_t []){0x08}, 1, 0},
    {0xBD, (uint8_t []){0x00}, 1, 0},
    {0xC0, (uint8_t []){0x80}, 1, 0},
    {0xC1, (uint8_t []){0x10}, 1, 0},
    {0xC2, (uint8_t []){0x37}, 1, 0},
    {0xC3, (uint8_t []){0x80}, 1, 0},
    {0xC4, (uint8_t []){0x10}, 1, 0},
    {0xC5, (uint8_t []){0x37}, 1, 0},
    {0xC6, (uint8_t []){0xA9}, 1, 0},
    {0xC7, (uint8_t []){0x41}, 1, 0},
    {0xC8, (uint8_t []){0x01}, 1, 0},
    {0xC9, (uint8_t []){0xA9}, 1, 0},
    {0xCA, (uint8_t []){0x41}, 1, 0},
    {0xCB, (uint8_t []){0x01}, 1, 0},
    {0xD0, (uint8_t []){0x91}, 1, 0},
    {0xD1, (uint8_t []){0x68}, 1, 0},
    {0xD2, (uint8_t []){0x68}, 1, 0},
    {0xF5, (uint8_t []){0x00, 0xA5}, 2, 0},
    {0xDD, (uint8_t []){0x4F}, 1, 0},
    {0xDE, (uint8_t []){0x4F}, 1, 0},
    {0xF1, (uint8_t []){0x10}, 1, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0},
    {0xF0, (uint8_t []){0x02}, 1, 0},
    {0xE0, (uint8_t []){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
    {0xE1, (uint8_t []){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
    {0xF0, (uint8_t []){0x10}, 1, 0},
    {0xF3, (uint8_t []){0x10}, 1, 0},
    {0xE0, (uint8_t []){0x07}, 1, 0},
    {0xE1, (uint8_t []){0x00}, 1, 0},
    {0xE2, (uint8_t []){0x00}, 1, 0},
    {0xE3, (uint8_t []){0x00}, 1, 0},
    {0xE4, (uint8_t []){0xE0}, 1, 0},
    {0xE5, (uint8_t []){0x06}, 1, 0},
    {0xE6, (uint8_t []){0x21}, 1, 0},
    {0xE7, (uint8_t []){0x01}, 1, 0},
    {0xE8, (uint8_t []){0x05}, 1, 0},
    {0xE9, (uint8_t []){0x02}, 1, 0},
    {0xEA, (uint8_t []){0xDA}, 1, 0},
    {0xEB, (uint8_t []){0x00}, 1, 0},
    {0xEC, (uint8_t []){0x00}, 1, 0},
    {0xED, (uint8_t []){0x0F}, 1, 0},
    {0xEE, (uint8_t []){0x00}, 1, 0},
    {0xEF, (uint8_t []){0x00}, 1, 0},
    {0xF8, (uint8_t []){0x00}, 1, 0},
    {0xF9, (uint8_t []){0x00}, 1, 0},
    {0xFA, (uint8_t []){0x00}, 1, 0},
    {0xFB, (uint8_t []){0x00}, 1, 0},
    {0xFC, (uint8_t []){0x00}, 1, 0},
    {0xFD, (uint8_t []){0x00}, 1, 0},
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xFF, (uint8_t []){0x00}, 1, 0},
    {0x60, (uint8_t []){0x40}, 1, 0},
    {0x61, (uint8_t []){0x04}, 1, 0},
    {0x62, (uint8_t []){0x00}, 1, 0},
    {0x63, (uint8_t []){0x42}, 1, 0},
    {0x64, (uint8_t []){0xD9}, 1, 0},
    {0x65, (uint8_t []){0x00}, 1, 0},
    {0x66, (uint8_t []){0x00}, 1, 0},
    {0x67, (uint8_t []){0x00}, 1, 0},
    {0x68, (uint8_t []){0x00}, 1, 0},
    {0x69, (uint8_t []){0x00}, 1, 0},
    {0x6A, (uint8_t []){0x00}, 1, 0},
    {0x6B, (uint8_t []){0x00}, 1, 0},
    {0x70, (uint8_t []){0x40}, 1, 0},
    {0x71, (uint8_t []){0x03}, 1, 0},
    {0x72, (uint8_t []){0x00}, 1, 0},
    {0x73, (uint8_t []){0x42}, 1, 0},
    {0x74, (uint8_t []){0xD8}, 1, 0},
    {0x75, (uint8_t []){0x00}, 1, 0},
    {0x76, (uint8_t []){0x00}, 1, 0},
    {0x77, (uint8_t []){0x00}, 1, 0},
    {0x78, (uint8_t []){0x00}, 1, 0},
    {0x79, (uint8_t []){0x00}, 1, 0},
    {0x7A, (uint8_t []){0x00}, 1, 0},
    {0x7B, (uint8_t []){0x00}, 1, 0},
    {0x80, (uint8_t []){0x48}, 1, 0},
    {0x81, (uint8_t []){0x00}, 1, 0},
    {0x82, (uint8_t []){0x06}, 1, 0},
    {0x83, (uint8_t []){0x02}, 1, 0},
    {0x84, (uint8_t []){0xD6}, 1, 0},
    {0x85, (uint8_t []){0x04}, 1, 0},
    {0x86, (uint8_t []){0x00}, 1, 0},
    {0x87, (uint8_t []){0x00}, 1, 0},
    {0x88, (uint8_t []){0x48}, 1, 0},
    {0x89, (uint8_t []){0x00}, 1, 0},
    {0x8A, (uint8_t []){0x08}, 1, 0},
    {0x8B, (uint8_t []){0x02}, 1, 0},
    {0x8C, (uint8_t []){0xD8}, 1, 0},
    {0x8D, (uint8_t []){0x04}, 1, 0},
    {0x8E, (uint8_t []){0x00}, 1, 0},
    {0x8F, (uint8_t []){0x00}, 1, 0},
    {0x90, (uint8_t []){0x48}, 1, 0},
    {0x91, (uint8_t []){0x00}, 1, 0},
    {0x92, (uint8_t []){0x0A}, 1, 0},
    {0x93, (uint8_t []){0x02}, 1, 0},
    {0x94, (uint8_t []){0xDA}, 1, 0},
    {0x95, (uint8_t []){0x04}, 1, 0},
    {0x96, (uint8_t []){0x00}, 1, 0},
    {0x97, (uint8_t []){0x00}, 1, 0},
    {0x98, (uint8_t []){0x48}, 1, 0},
    {0x99, (uint8_t []){0x00}, 1, 0},
    {0x9A, (uint8_t []){0x0C}, 1, 0},
    {0x9B, (uint8_t []){0x02}, 1, 0},
    {0x9C, (uint8_t []){0xDC}, 1, 0},
    {0x9D, (uint8_t []){0x04}, 1, 0},
    {0x9E, (uint8_t []){0x00}, 1, 0},
    {0x9F, (uint8_t []){0x00}, 1, 0},
    {0xA0, (uint8_t []){0x48}, 1, 0},
    {0xA1, (uint8_t []){0x00}, 1, 0},
    {0xA2, (uint8_t []){0x05}, 1, 0},
    {0xA3, (uint8_t []){0x02}, 1, 0},
    {0xA4, (uint8_t []){0xD5}, 1, 0},
    {0xA5, (uint8_t []){0x04}, 1, 0},
    {0xA6, (uint8_t []){0x00}, 1, 0},
    {0xA7, (uint8_t []){0x00}, 1, 0},
    {0xA8, (uint8_t []){0x48}, 1, 0},
    {0xA9, (uint8_t []){0x00}, 1, 0},
    {0xAA, (uint8_t []){0x07}, 1, 0},
    {0xAB, (uint8_t []){0x02}, 1, 0},
    {0xAC, (uint8_t []){0xD7}, 1, 0},
    {0xAD, (uint8_t []){0x04}, 1, 0},
    {0xAE, (uint8_t []){0x00}, 1, 0},
    {0xAF, (uint8_t []){0x00}, 1, 0},
    {0xB0, (uint8_t []){0x48}, 1, 0},
    {0xB1, (uint8_t []){0x00}, 1, 0},
    {0xB2, (uint8_t []){0x09}, 1, 0},
    {0xB3, (uint8_t []){0x02}, 1, 0},
    {0xB4, (uint8_t []){0xD9}, 1, 0},
    {0xB5, (uint8_t []){0x04}, 1, 0},
    {0xB6, (uint8_t []){0x00}, 1, 0},
    {0xB7, (uint8_t []){0x00}, 1, 0},
    {0xB8, (uint8_t []){0x48}, 1, 0},
    {0xB9, (uint8_t []){0x00}, 1, 0},
    {0xBA, (uint8_t []){0x0B}, 1, 0},
    {0xBB, (uint8_t []){0x02}, 1, 0},
    {0xBC, (uint8_t []){0xDB}, 1, 0},
    {0xBD, (uint8_t []){0x04}, 1, 0},
    {0xBE, (uint8_t []){0x00}, 1, 0},
    {0xBF, (uint8_t []){0x00}, 1, 0},
    {0xC0, (uint8_t []){0x10}, 1, 0},
    {0xC1, (uint8_t []){0x47}, 1, 0},
    {0xC2, (uint8_t []){0x56}, 1, 0},
    {0xC3, (uint8_t []){0x65}, 1, 0},
    {0xC4, (uint8_t []){0x74}, 1, 0},
    {0xC5, (uint8_t []){0x88}, 1, 0},
    {0xC6, (uint8_t []){0x99}, 1, 0},
    {0xC7, (uint8_t []){0x01}, 1, 0},
    {0xC8, (uint8_t []){0xBB}, 1, 0},
    {0xC9, (uint8_t []){0xAA}, 1, 0},
    {0xD0, (uint8_t []){0x10}, 1, 0},
    {0xD1, (uint8_t []){0x47}, 1, 0},
    {0xD2, (uint8_t []){0x56}, 1, 0},
    {0xD3, (uint8_t []){0x65}, 1, 0},
    {0xD4, (uint8_t []){0x74}, 1, 0},
    {0xD5, (uint8_t []){0x88}, 1, 0},
    {0xD6, (uint8_t []){0x99}, 1, 0},
    {0xD7, (uint8_t []){0x01}, 1, 0},
    {0xD8, (uint8_t []){0xBB}, 1, 0},
    {0xD9, (uint8_t []){0xAA}, 1, 0},
    {0xF3, (uint8_t []){0x01}, 1, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0},
    {0x21, (uint8_t []){}, 0, 0},
    {0x11, (uint8_t []){}, 0, 0},
    {0x00, (uint8_t []){}, 0, 120},
};
float tsens_value;

// Application-level event generated from GPIO7 touch button events.
// This is used to move heavy work (display/emotion changes) out of the
// touch driver callback so it can't starve audio tasks.
typedef struct {
    touch_state_t state;
    uint32_t channel;
} TouchButtonAppEvent_t;

class Charge : public I2cDevice {
public:
    Charge(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        read_buffer_ = new uint8_t[8];
    }
    ~Charge() {
        delete[] read_buffer_;
    }
    void Printcharge() {
        if (!enabled_) {
            return;
        }
        if (TryReadRegs(0x08, read_buffer_, 2) != ESP_OK) {
            HandleReadFailure("voltage");
            return;
        }
        if (TryReadRegs(0x0c, read_buffer_ + 2, 2) != ESP_OK) {
            HandleReadFailure("current");
            return;
        }
        consecutive_read_failures_ = 0;
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &tsens_value));

        // Read voltage and current values (currently unused but available for future use)
        (void)((uint16_t)(read_buffer_[1] << 8 | read_buffer_[0]));  // voltage
        (void)((int16_t)(read_buffer_[3] << 8 | read_buffer_[2]));  // current
    }
    
    // Get battery voltage in millivolts
    uint16_t GetVoltage() {
        if (!enabled_) {
            return 0;
        }
        if (TryReadRegs(0x08, read_buffer_, 2) != ESP_OK) {
            HandleReadFailure("voltage");
            return 0;
        }
        consecutive_read_failures_ = 0;
        uint16_t voltage_raw = (read_buffer_[1] << 8) | read_buffer_[0];
        // Voltage register format: typically in units of 0.1mV or similar
        // Assuming raw value needs scaling - adjust based on actual chip specifications
        // For now, assuming it's already in mV or needs minimal conversion
        return voltage_raw;
    }
    
    // Get battery current in milliamps (positive = charging, negative = discharging)
    int16_t GetCurrent() {
        if (!enabled_) {
            return 0;
        }
        if (TryReadRegs(0x0c, read_buffer_ + 2, 2) != ESP_OK) {
            HandleReadFailure("current");
            return 0;
        }
        consecutive_read_failures_ = 0;
        int16_t current_raw = (int16_t)((read_buffer_[3] << 8) | read_buffer_[2]);
        // Current register format: typically in units of 0.1mA or similar
        // Adjust scaling based on actual chip specifications
        return current_raw;
    }
    
    static void TaskFunction(void *pvParameters) {
        Charge* charge = static_cast<Charge*>(pvParameters);
        while (true) {
            charge->Printcharge();
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }

    bool IsAvailable() const {
        return enabled_;
    }

private:
    void HandleReadFailure(const char* field) {
        consecutive_read_failures_++;
        if (consecutive_read_failures_ >= kMaxConsecutiveReadFailures) {
            enabled_ = false;
            ESP_LOGW(TAG, "[BATTERY] Charge IC unresponsive after repeated %s read failures, battery reporting disabled", field);
            return;
        }
        ESP_LOGW(TAG, "[BATTERY] Read %s failed", field);
    }

    static constexpr uint8_t kMaxConsecutiveReadFailures = 3;
    uint8_t* read_buffer_ = nullptr;
    uint8_t consecutive_read_failures_ = 0;
    bool enabled_ = true;
};


class Cst816s : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };
    
    enum GestureType {
        GESTURE_NONE = 0x00,
        GESTURE_SWIPE_UP = 0x01,
        GESTURE_SWIPE_DOWN = 0x02,
        GESTURE_SWIPE_LEFT = 0x03,
        GESTURE_SWIPE_RIGHT = 0x04,
        GESTURE_SINGLE_TAP = 0x05,
        GESTURE_DOUBLE_TAP = 0x0B,
        GESTURE_LONG_PRESS = 0x0C,
    };
    
    Cst816s(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        read_buffer_ = new uint8_t[6];
        gesture_buffer_ = new uint8_t[1];
        
        // Enable gesture detection mode
        // Register 0xFA: Gesture enable (0x01 = enable gestures)
        WriteReg(0xFA, 0x01);
        
        // Register 0xED: Gesture ID register (read-only, but we can verify it's working)
        // Gestures will be reported in register 0x01
    }

    ~Cst816s() {
        delete[] read_buffer_;
        delete[] gesture_buffer_;
    }

    void UpdateTouchPoint() {
        if (TryReadRegs(0x02, read_buffer_, 6) != ESP_OK) {
            tp_.num = 0;
            return;
        }
        tp_.num = read_buffer_[0] & 0x0F;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    GestureType ReadGesture() {
        // Read gesture register (0x01)
        if (TryReadRegs(0x01, gesture_buffer_, 1) != ESP_OK) {
            return GESTURE_NONE;
        }
        return static_cast<GestureType>(gesture_buffer_[0]);
    }

    const TouchPoint_t& GetTouchPoint() {
        return tp_;
    }

private:
    uint8_t* read_buffer_ = nullptr;
    uint8_t* gesture_buffer_ = nullptr;
    TouchPoint_t tp_;
};
class EchoEar : public WifiBoard {
private:
    // I2C bus handles
    i2c_bus_handle_t shared_i2c_bus_handle_ = nullptr;  // For BMI270 (i2c_bus wrapper)
    i2c_master_bus_handle_t i2c_bus_;                   // For other I2C devices (traditional API)
    
    // BMI270 sensor handle
    bmi270_handle_t bmi270_handle_ = nullptr;
    
    Cst816s* cst816s_;
    Charge* charge_;
    Button boot_button_;
    LcdDisplay* display_;
    PwmBacklight* backlight_ = nullptr;
    esp_timer_handle_t touchpad_timer_;
    esp_lcd_touch_handle_t tp;   // LCD touch handle
    touch_button_handle_t touch_button_handle_ = nullptr;  // Touch button sensor handle for GPIO7
    static volatile uint32_t touch_event_count_;  // Counter for touch events
    QueueHandle_t touch_event_queue_;  // Queue for touch interrupt events (used by CST816S)
    TaskHandle_t touch_event_task_handle_;  // Task handle for processing touch events (used by CST816S)
    QueueHandle_t touch_button_app_queue_ = nullptr;  // Queue for app-level touch button events
    PowerSaveTimer* power_save_timer_ = nullptr;
    esp_timer_handle_t emotion_reset_timer_ = nullptr;  // Timer to reset emotion to previous state after one animation cycle
    esp_timer_handle_t volume_message_timer_ = nullptr;  // Timer to clear volume message
    std::string previous_emotion_ = "normal";  // Store previous emotion string to restore
    int previous_volume_ = -1;  // Store volume before muting (for restore on unmute)

    void InitializeVolumeMessageTimer() {
        if (volume_message_timer_ != nullptr) {
            return;
        }
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                auto self = static_cast<EchoEar*>(arg);
                if (self == nullptr || self->display_ == nullptr) {
                    return;
                }
                self->display_->ClearOverlayMessage();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "volume_msg_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &volume_message_timer_));
    }

    void ShowVolumeMessage(int volume) {
        if (display_ == nullptr) {
            return;
        }
        InitializeVolumeMessageTimer();
        std::string message = "volume: " + std::to_string(volume) + "%";
        display_->CreateOverlayMessage(message.c_str());
        if (volume == 0) {
            esp_timer_stop(volume_message_timer_);
            return;
        }
        esp_timer_stop(volume_message_timer_);
        ESP_ERROR_CHECK(esp_timer_start_once(volume_message_timer_, 1000 * 1000));
    }

    void ShowBrightnessMessage(int brightness) {
        if (display_ == nullptr) {
            return;
        }
        InitializeVolumeMessageTimer();
        std::string message = "brightness: " + std::to_string(brightness) + "%";
        display_->CreateOverlayMessage(message.c_str());
        esp_timer_stop(volume_message_timer_);
        ESP_ERROR_CHECK(esp_timer_start_once(volume_message_timer_, 1000 * 1000));
    }

    // Toggle mute / unmute. Saves the pre-mute volume so unmute can restore it.
    // Shared by the physical BOOT button long-press and the touchscreen long-press.
    void ToggleMute() {
        auto codec = GetAudioCodec();
        if (!codec) return;

        int current_volume = codec->output_volume();
        if (current_volume == 0) {
            int restore_volume = (previous_volume_ > 0) ? previous_volume_ : 50;
            codec->SetOutputVolume(restore_volume);
            ESP_LOGI(TAG, "Volume restored from mute: %d", restore_volume);
            previous_volume_ = -1;
            ShowVolumeMessage(restore_volume);
        } else {
            previous_volume_ = current_volume;
            codec->SetOutputVolume(0);
            ESP_LOGI(TAG, "Volume muted (saved: %d)", previous_volume_);
            ShowVolumeMessage(0);
        }
    }

    void ShowBatteryMessage() {
        if (display_ == nullptr) {
            return;
        }
        InitializeVolumeMessageTimer();
        int level = 0;
        bool charging = false, discharging = false;
        std::string message;
        auto& wifi_station = WifiStation::GetInstance();
        if (GetBatteryLevel(level, charging, discharging)) {
            message = "battery: " + std::to_string(level) + "%";
            if (charging) message += " chrg";
        } else {
            message = "battery: N/A";
        }
        message += "\nnetwork: ";
        if (wifi_station.IsConnected()) {
            message += wifi_station.GetSsid();
        } else {
            message += "na";
        }
        display_->CreateOverlayMessage(message.c_str());
        esp_timer_stop(volume_message_timer_);
        ESP_ERROR_CHECK(esp_timer_start_once(volume_message_timer_, 2000 * 1000));
    }

    void InitializeI2c() {
        ESP_LOGI(TAG, "[BMI270] Initializing I2C bus for BMI270 compatibility");
        
        // Create shared I2C bus using i2c_bus wrapper (REQUIRED for BMI270)
        // All I2C devices (BMI270, and others) share the same physical bus
        i2c_config_t i2c_bus_cfg = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .sda_pullup_en = true,
            .scl_pullup_en = true,
            .master = {
                .clk_speed = 400000,  // 400kHz
            },
            .clk_flags = 0,
        };
        
        shared_i2c_bus_handle_ = i2c_bus_create(I2C_NUM_0, &i2c_bus_cfg);
        if (!shared_i2c_bus_handle_) {
            ESP_LOGE(TAG, "[BMI270] Failed to create shared I2C bus");
            ESP_ERROR_CHECK(ESP_FAIL);
        }
        ESP_LOGI(TAG, "[BMI270] Shared I2C bus created successfully");

        // Get the internal master bus handle for use with existing I2cDevice classes
        // This is required if you have other I2C devices using the traditional API
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0) && !CONFIG_I2C_BUS_BACKWARD_CONFIG
        i2c_bus_ = i2c_bus_get_internal_bus_handle(shared_i2c_bus_handle_);
#else
        ESP_LOGE(TAG, "[BMI270] ESP-IDF version does not support i2c_bus_get_internal_bus_handle()");
        ESP_ERROR_CHECK(ESP_FAIL);
#endif
        
        if (!i2c_bus_) {
            ESP_LOGE(TAG, "[BMI270] Failed to obtain master bus handle");
            ESP_ERROR_CHECK(ESP_FAIL);
        }
        ESP_LOGI(TAG, "[BMI270] Internal master bus handle obtained successfully");

        temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
        ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));
        ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));
        
        ESP_LOGI(TAG, "[BMI270] I2C bus initialization complete");
    }

    bool ProbeI2cDevice(uint8_t address, const char* name) {
        if (!i2c_bus_) {
            ESP_LOGE(TAG, "[I2C] Probe failed for %s: bus not ready", name);
            return false;
        }
        esp_err_t ret = i2c_master_probe(i2c_bus_, address, pdMS_TO_TICKS(200));
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "[I2C] %s not detected at 0x%02X: %s", name, address, esp_err_to_name(ret));
            return false;
        }
        ESP_LOGI(TAG, "[I2C] %s detected at 0x%02X", name, address);
        return true;
    }

    static void touchpad_timer_callback(void* arg) {
        auto& board = (EchoEar&)Board::GetInstance();
        auto touchpad = board.GetTouchpad();
        static bool was_touched = false;
        static int64_t touch_start_time = 0;
        const int64_t TOUCH_THRESHOLD_MS = 500;
        
        touchpad->UpdateTouchPoint();
        auto touch_point = touchpad->GetTouchPoint();
        
        if (touch_point.num > 0 && !was_touched) {
            was_touched = true;
            touch_start_time = esp_timer_get_time() / 1000;
        } 
        else if (touch_point.num == 0 && was_touched) {
            was_touched = false;
            int64_t touch_duration = (esp_timer_get_time() / 1000) - touch_start_time;
            
            if (touch_duration < TOUCH_THRESHOLD_MS) {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting && 
                    !WifiStation::GetInstance().IsConnected()) {
                    board.ResetWifiConfiguration();
                }
                if (board.power_save_timer_) {
                    // If in sleep mode, just wake up and return to idle state
                    if (board.power_save_timer_->IsInSleepMode()) {
                        ESP_LOGI(TAG, "Touch detected during sleep mode - waking up");
                        board.power_save_timer_->WakeUp();
                        // WakeUp() callback will set state to idle and show static_normal face
                    } else {
                        // Normal operation: wake up and toggle chat state
                        board.power_save_timer_->WakeUp();
                        app.ToggleChatState();
                    }
                } else {
                    app.ToggleChatState();
                }
            }
        }
    }
    static void touchpad_callback(Cst816s::TouchPoint_t touch_point) {
        auto& board = (EchoEar&)Board::GetInstance();
        static bool was_touched = false;
        static int64_t touch_start_time = 0;
        const int64_t TOUCH_THRESHOLD_MS = 500;
        
        if (touch_point.num > 0 && !was_touched) {
            was_touched = true;
            touch_start_time = esp_timer_get_time() / 1000;
        } 
        else if (touch_point.num == 0 && was_touched) {
            was_touched = false;
            int64_t touch_duration = (esp_timer_get_time() / 1000) - touch_start_time;
            
            if (touch_duration < TOUCH_THRESHOLD_MS) {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting && 
                    !WifiStation::GetInstance().IsConnected()) {
                    board.ResetWifiConfiguration();
                }
                if (board.power_save_timer_) {
                    // If in sleep mode, just wake up and return to idle state
                    if (board.power_save_timer_->IsInSleepMode()) {
                        ESP_LOGI(TAG, "Touch detected during sleep mode - waking up");
                        board.power_save_timer_->WakeUp();
                        // WakeUp() callback will set state to idle and show static_normal face
                    } else {
                        // Normal operation: wake up and toggle chat state
                        board.power_save_timer_->WakeUp();
                        app.ToggleChatState();
                    }
                } else {
                    app.ToggleChatState();
                }
            }
        }
    }

    static void lvgl_port_touch_isr_cb(void* arg)
    {
        // ISR context: only send notification to queue (non-blocking)
        // The actual work will be done in touch_event_task
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        uint32_t dummy = 1;  // Dummy data to send
        QueueHandle_t queue = static_cast<QueueHandle_t>(arg);
        
        if (queue != nullptr) {
            xQueueSendFromISR(queue, &dummy, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }

    // Software gesture classifier.
    // We ignore the CST816S chip's internal gesture register (too sensitive,
    // mid-stroke firing, easy left/up mix-ups) and classify ourselves from
    // raw (x, y) coordinates on finger-up. See proposal A+C+D+E.
    static void touch_event_task(void* arg)
    {
        EchoEar* board = static_cast<EchoEar*>(arg);
        if (board == nullptr) {
            ESP_LOGE(TAG, "touch_event_task: Invalid board pointer");
            vTaskDelete(NULL);
            return;
        }

        // Tunables - tweak here if gestures still feel off.
        constexpr int     MIN_SWIPE_PX     = 40;   // min finger travel to count as a swipe
        constexpr int     AXIS_RATIO_NUM   = 18;   // dominant axis must be >= 1.8x the other
        constexpr int     AXIS_RATIO_DEN   = 10;
        constexpr int64_t LONG_PRESS_MS    = 800;   // hold duration for battery/network overlay
        constexpr int64_t MUTE_LONG_PRESS_MS = 5000; // hold duration (>=) for mute/unmute toggle
        constexpr int     LONG_PRESS_DRIFT = 15;    // max finger wobble allowed during a hold (px)
        constexpr int64_t COOLDOWN_MS      = 350;  // ignore any new gesture within this window
        constexpr int64_t RAPID_REPEAT_MS  = 600;  // same-direction swipe within this = 2x step
        constexpr int     STEP_SLOW        = 10;   // always 10% per step (rapid repeat no longer accelerates)
        constexpr int     STEP_FAST        = 10;

        // Per-stroke state
        bool    in_stroke = false;
        int     x0 = 0, y0 = 0;
        int     last_x = 0, last_y = 0;
        int     max_drift = 0;
        int64_t t0_ms = 0;

        // Cross-stroke state (for rapid-repeat acceleration + cooldown)
        int64_t last_fire_ms = 0;
        int     last_axis = 0;   // 0=none, 1=horiz, 2=vert
        int     last_dir  = 0;   // -1 or +1

        uint32_t dummy;
        while (true) {
            if (xQueueReceive(board->touch_event_queue_, &dummy, portMAX_DELAY) != pdTRUE) {
                continue;
            }
            if (board->cst816s_ == nullptr) {
                continue;
            }

            board->cst816s_->UpdateTouchPoint();
            auto tp = board->cst816s_->GetTouchPoint();
            int64_t now_ms = esp_timer_get_time() / 1000;

            if (tp.num > 0 && tp.x >= 0 && tp.y >= 0) {
                int x = (tp.x < DISPLAY_WIDTH)  ? tp.x : (DISPLAY_WIDTH  - 1);
                int y = (tp.y < DISPLAY_HEIGHT) ? tp.y : (DISPLAY_HEIGHT - 1);

                if (!in_stroke) {
                    // Finger just went down - start a new stroke and wake the device.
                    in_stroke = true;
                    x0 = last_x = x;
                    y0 = last_y = y;
                    max_drift = 0;
                    t0_ms = now_ms;
                    ESP_LOGI(TAG, "[TOUCH] Touch pressed at (%d, %d)", x, y);
                    if (board->power_save_timer_) {
                        if (board->power_save_timer_->IsInSleepMode()) {
                            ESP_LOGI(TAG, "Touch detected during sleep mode - waking up");
                        }
                        board->power_save_timer_->WakeUp();
                    }
                } else {
                    // Still dragging - update drift using Chebyshev distance (cheap, no sqrt).
                    int adx = x - x0; if (adx < 0) adx = -adx;
                    int ady = y - y0; if (ady < 0) ady = -ady;
                    int drift = (adx > ady) ? adx : ady;
                    if (drift > max_drift) max_drift = drift;
                    last_x = x;
                    last_y = y;
                }
            } else if (in_stroke) {
                // Finger just lifted - classify the stroke exactly once.
                in_stroke = false;
                int dx = last_x - x0;
                int dy = last_y - y0;
                int adx = (dx < 0) ? -dx : dx;
                int ady = (dy < 0) ? -dy : dy;
                int64_t duration_ms = now_ms - t0_ms;
                ESP_LOGI(TAG, "[TOUCH] Touch released dx=%d dy=%d dur=%dms drift=%d",
                         dx, dy, (int)duration_ms, max_drift);

                // Global cooldown - any new gesture must be clearly separate in time.
                if (now_ms - last_fire_ms < COOLDOWN_MS) {
                    ESP_LOGI(TAG, "[TOUCH] Gesture suppressed by cooldown");
                    continue;
                }

                // LONG PRESS: held long, finger barely moved. Two tiers:
                //   >= 5000 ms -> toggle mute (volume-only feedback, no battery overlay)
                //   >= 800 ms  -> show battery/network overlay (no volume change)
                if (duration_ms >= LONG_PRESS_MS && max_drift < LONG_PRESS_DRIFT) {
                    if (duration_ms >= MUTE_LONG_PRESS_MS) {
                        ESP_LOGI(TAG, "[TOUCH] Very long press (>=5s) - toggling mute");
                        board->ToggleMute();
                    } else {
                        ESP_LOGI(TAG, "[TOUCH] Long press detected - showing battery");
                        board->ShowBatteryMessage();
                    }
                    last_fire_ms = now_ms;
                    last_axis = 0;
                    last_dir  = 0;
                    continue;
                }

                // SWIPE: needs minimum travel AND one axis must clearly dominate.
                if (adx < MIN_SWIPE_PX && ady < MIN_SWIPE_PX) {
                    // Tap or idle touch - nothing to do.
                    continue;
                }

                int axis = 0;
                int dir  = 0;
                if (adx * AXIS_RATIO_DEN >= ady * AXIS_RATIO_NUM) {
                    axis = 1;                    // horizontal (brightness)
                    dir  = (dx > 0) ? +1 : -1;
                } else if (ady * AXIS_RATIO_DEN >= adx * AXIS_RATIO_NUM) {
                    axis = 2;                    // vertical (volume)
                    dir  = (dy > 0) ? +1 : -1;
                } else {
                    ESP_LOGI(TAG, "[TOUCH] Ambiguous diagonal swipe - ignored");
                    continue;
                }

                // Rapid same-direction repeat -> bigger step.
                int step = STEP_SLOW;
                if (axis == last_axis && dir == last_dir &&
                    (now_ms - last_fire_ms) < RAPID_REPEAT_MS) {
                    step = STEP_FAST;
                }

                if (axis == 2) {
                    // Vertical: preserve original mapping (UP=decrease, DOWN=increase).
                    auto codec = board->GetAudioCodec();
                    if (codec != nullptr) {
                        int cur = codec->output_volume();
                        int delta = (dir < 0) ? -step : +step;
                        int nv = cur + delta;
                        if (nv < 0)   nv = 0;
                        if (nv > 100) nv = 100;
                        codec->SetOutputVolume(nv);
                        ESP_LOGI(TAG, "[TOUCH] Swipe %s - Volume: %d -> %d (step %d)",
                                 dir < 0 ? "UP" : "DOWN", cur, nv, step);
                        board->ShowVolumeMessage(nv);
                        last_fire_ms = now_ms;
                        last_axis = axis;
                        last_dir  = dir;
                    }
                } else {
                    // Horizontal: preserve original mapping (LEFT=increase, RIGHT=decrease).
                    auto bl = board->GetBacklight();
                    if (bl != nullptr) {
                        int cur = bl->brightness();
                        int delta = (dir < 0) ? +step : -step;
                        int nb = cur + delta;
                        if (nb < 10)  nb = 10;
                        if (nb > 100) nb = 100;
                        bl->SetBrightness(nb, true);
                        ESP_LOGI(TAG, "[TOUCH] Swipe %s - Brightness: %d -> %d (step %d)",
                                 dir < 0 ? "LEFT" : "RIGHT", cur, nb, step);
                        board->ShowBrightnessMessage(nb);
                        last_fire_ms = now_ms;
                        last_axis = axis;
                        last_dir  = dir;
                    }
                }
            }
        }
    }

    static void touch_log_task(void* arg)
    {
        ESP_LOGI(TAG, "[TOUCH] touch_log_task started");
        EchoEar* board = static_cast<EchoEar*>(arg);
        if (board == nullptr) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Invalid board pointer in touch_log_task");
            vTaskDelete(NULL);
            return;
        }

        uint32_t last_count = 0;
        ESP_LOGI(TAG, "[TOUCH] touch_log_task entering main loop");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));  // Wait 1 second
            
            // Check if device is in speaking/listening mode - skip logging if so
            auto& app = Application::GetInstance();
            DeviceState current_state = app.GetDeviceState();
            if (current_state == kDeviceStateSpeaking || current_state == kDeviceStateListening) {
                // Skip logging during audio activity to avoid interference
                continue;
            }
            
            uint32_t current_count = EchoEar::touch_event_count_;
            if (current_count > last_count) {
                ESP_LOGI(TAG, "[TOUCH] Touch sensor status: Events detected! Total events: %lu (GPIO7)", 
                         (unsigned long)current_count);
                last_count = current_count;
            } else {
                ESP_LOGI(TAG, "[TOUCH] Touch sensor status: No new events (GPIO7), total=%lu", 
                         (unsigned long)current_count);
            }
        }
    }
    static void touch_button_event_task(void* arg)
    {
        // Logging disabled to avoid I2C contention
        // ESP_LOGI(TAG, "[TOUCH] touch_button_event_task started");
        EchoEar* board = static_cast<EchoEar*>(arg);
        if (board == nullptr) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Invalid board pointer in touch_button_event_task");
            vTaskDelete(NULL);
            return;
        }

        // ESP_LOGI(TAG, "[TOUCH] touch_button_event_task handle=%p", (void*)board->touch_button_handle_);
        // ESP_LOGI(TAG, "[TOUCH] touch_button_event_task entering main loop");
        while (true) {
            // Check if device is in speaking/listening mode - skip event processing if so
            auto& app = Application::GetInstance();
            DeviceState current_state = app.GetDeviceState();
            if (current_state == kDeviceStateSpeaking || current_state == kDeviceStateListening) {
                // Skip touch event processing during audio activity
                vTaskDelay(pdMS_TO_TICKS(100));  // Longer delay when audio is active
                continue;
            }
            
            if (board->touch_button_handle_ != nullptr) {
                touch_button_sensor_handle_events(board->touch_button_handle_);
            }
            vTaskDelay(pdMS_TO_TICKS(10));  // Check every 10ms when not in audio mode
        }
    }

    // Handles "business logic" for GPIO7 touch button (emotion changes, timers, etc.)
    // This runs in normal task context at a modest priority so it cannot block
    // audio I/O even if the touch sensor generates many events.
    // IMPORTANT: This task skips all processing when device is in speaking/listening mode.
    static void touch_button_app_task(void* arg)
    {
        EchoEar* board = static_cast<EchoEar*>(arg);
        if (board == nullptr) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Invalid board pointer in touch_button_app_task");
            vTaskDelete(NULL);
            return;
        }

        // Logging disabled to avoid I2C contention
        // ESP_LOGI(TAG, "[TOUCH] touch_button_app_task started");

        TouchButtonAppEvent_t event;
        while (true) {
            if (xQueueReceive(board->touch_button_app_queue_, &event, portMAX_DELAY) == pdTRUE) {
                // CRITICAL: Skip all touch processing when device is speaking or listening
                auto& app = Application::GetInstance();
                DeviceState current_state = app.GetDeviceState();
                if (current_state == kDeviceStateSpeaking || current_state == kDeviceStateListening) {
                    // Logging disabled to avoid I2C contention
                    // ESP_LOGD(TAG, "[TOUCH] Skipping touch processing - device is in %s mode", 
                    //          current_state == kDeviceStateSpeaking ? "speaking" : "listening");
                    continue;  // Drop the event and continue
                }
                
                if (event.state == TOUCH_STATE_ACTIVE) {
                    // This is the heavy logic that used to live directly in the
                    // touch_button_sensor callback. Running it here keeps the
                    // driver callback lightweight and prevents it from blocking
                    // higher-priority tasks such as audio streaming.

                    // Wake up power save timer
                    if (board->power_save_timer_) {
                        board->power_save_timer_->WakeUp();
                    }

                    // Stop any existing emotion reset timer
                    if (board->emotion_reset_timer_ != nullptr) {
                        esp_timer_stop(board->emotion_reset_timer_);
                    }

                    // Store current emotion state - default to "normal" if unknown
                    // (We can't easily detect the current emotion, so default to normal)
                    board->previous_emotion_ = "normal";

                    // Randomly select one of three emotions
                    const char* emotions[] = {"angry", "happy", "embarressed"};
                    uint32_t random_index = esp_random() % 3;
                    const char* selected_emotion = emotions[random_index];

                    // Set the random emotion on display
                    auto display = board->GetDisplay();
                    if (display != nullptr) {
                        display->SetEmotion(selected_emotion);
                        ESP_LOGI(TAG, "[TOUCH] Random emotion selected: %s", selected_emotion);
                    }

                    // Calculate animation duration: frames * frame_delay (500ms per frame)
                    // Animation frame counts: Fire(4), Smirk/Happy(4), Embarrassed(3)
                    int frame_counts[] = {4, 4, 3};  // angry, happy, embarrassed
                    int frame_count = frame_counts[random_index];
                    int64_t animation_duration_us = frame_count * 500 * 1000;  // frames * 500ms in microseconds

                    // Start timer to restore previous emotion after one animation cycle
                    if (board->emotion_reset_timer_ != nullptr) {
                        esp_timer_start_once(board->emotion_reset_timer_, animation_duration_us);
                        ESP_LOGI(TAG, "[TOUCH] Timer set to restore emotion '%s' after %lld ms (one cycle)",
                                 board->previous_emotion_.c_str(), animation_duration_us / 1000);
                    }
                } else {
                    // Logging disabled to avoid I2C contention
                    // ESP_LOGI(TAG, "[TOUCH] App-level touch button release event (channel %lu)",
                    //          (unsigned long)event.channel);
                }
            }
        }
    }
    void InitializeTouchButton()
    {
        ESP_LOGI(TAG, "[TOUCH] ===== Starting touch button initialization =====");
        ESP_LOGI(TAG, "[TOUCH] GPIO7 -> Touch Channel 7");
        ESP_LOGI(TAG, "[TOUCH] Threshold: %.3f", LIGHT_TOUCH_THRESHOLD);
        
        // Step 1: Prepare channel configuration
        ESP_LOGI(TAG, "[TOUCH] Step 1: Preparing channel configuration");
        uint32_t touch_channel_list[] = {TOUCH_CHANNEL_1};  // GPIO7 = Channel 7
        ESP_LOGI(TAG, "[TOUCH] Channel list: [%lu]", (unsigned long)touch_channel_list[0]);
        
        touch_lowlevel_type_t *channel_type = (touch_lowlevel_type_t*)calloc(1, sizeof(touch_lowlevel_type_t));
        if (channel_type == NULL) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Memory allocation failed for channel_type");
            return;
        }
        channel_type[0] = TOUCH_LOWLEVEL_TYPE_TOUCH;
        ESP_LOGI(TAG, "[TOUCH] Channel type allocated and set to TOUCH");
        
        // Step 2: Create low-level touch sensor
        ESP_LOGI(TAG, "[TOUCH] Step 2: Creating low-level touch sensor");
        touch_lowlevel_config_t low_config = {
            .channel_num = 1,
            .channel_list = touch_channel_list,
            .channel_type = channel_type,
        };
        ESP_LOGI(TAG, "[TOUCH] Low-level config: channel_num=%d", low_config.channel_num);
        
        esp_err_t ret = touch_sensor_lowlevel_create(&low_config);
        ESP_LOGI(TAG, "[TOUCH] touch_sensor_lowlevel_create() returned: %d (%s)", ret, esp_err_to_name(ret));
        free(channel_type);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Touch sensor lowlevel create failed: %d (%s)", ret, esp_err_to_name(ret));
            return;
        }
        ESP_LOGI(TAG, "[TOUCH] ✓ Low-level touch sensor created successfully");
        
        // Step 3: Configure touch button sensor
        ESP_LOGI(TAG, "[TOUCH] Step 3: Configuring touch button sensor");
        float channel_threshold[] = {LIGHT_TOUCH_THRESHOLD};
        
        touch_button_config_t touch_cfg = {
            .channel_num = 1,
            .channel_list = touch_channel_list,
            .channel_threshold = channel_threshold,
            .channel_gold_value = NULL,
            .debounce_times = 1,
            .skip_lowlevel_init = true,
        };
        
        ESP_LOGI(TAG, "[TOUCH] Touch button config: channel_num=%d, threshold=%.3f, debounce=%d, skip_lowlevel_init=%d",
                 touch_cfg.channel_num, touch_cfg.channel_threshold[0], touch_cfg.debounce_times, touch_cfg.skip_lowlevel_init);
        
        // Step 4: Create touch button sensor with callback
        ESP_LOGI(TAG, "[TOUCH] Step 4: Creating touch button sensor with callback");
        touch_event_count_ = 0;  // Initialize counter
        ESP_LOGI(TAG, "[TOUCH] Touch event counter initialized to 0");

        // Create application-level event queue for heavy work triggered by touch events
        touch_button_app_queue_ = xQueueCreate(5, sizeof(TouchButtonAppEvent_t));
        if (touch_button_app_queue_ == nullptr) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Failed to create touch_button_app_queue_");
            return;
        }
        
        ret = touch_button_sensor_create(&touch_cfg, &touch_button_handle_, 
                                         [](touch_button_handle_t handle, uint32_t channel, touch_state_t state, void *cb_arg) {
                                             // Keep the callback extremely lightweight so it never
                                             // interferes with time-critical tasks like audio.
                                             // We only bump a counter and push a small event to a
                                             // queue that is handled by a separate, lower-priority task.

                                             // CRITICAL: Skip all touch processing when device is speaking or listening
                                             auto& app = Application::GetInstance();
                                             DeviceState current_state = app.GetDeviceState();
                                             if (current_state == kDeviceStateSpeaking || current_state == kDeviceStateListening) {
                                                 // Don't increment counter or queue events during audio activity
                                                 return;  // Exit callback immediately
                                             }

                                             EchoEar::touch_event_count_++;  // Increment on any touch event

                                             EchoEar* board = static_cast<EchoEar*>(cb_arg);
                                             if (board != nullptr && board->touch_button_app_queue_ != nullptr) {
                                                 TouchButtonAppEvent_t evt = {
                                                     .state = state,
                                                     .channel = channel,
                                                 };
                                                 // Non-blocking send; if the queue is full we just drop
                                                 // the event to avoid ever blocking the driver context.
                                                 xQueueSend(board->touch_button_app_queue_, &evt, 0);
                                             }

                                             // Logging disabled to avoid I2C contention during audio
                                             // if (state == TOUCH_STATE_ACTIVE) {
                                             //     ESP_LOGI(TAG, "[TOUCH] Touch button pressed (channel %lu)", (unsigned long)channel);
                                             // } else {
                                             //     ESP_LOGI(TAG, "[TOUCH] Touch button released (channel %lu)", (unsigned long)channel);
                                             // }
                                         }, this);
        
        ESP_LOGI(TAG, "[TOUCH] touch_button_sensor_create() returned: %d (%s)", ret, esp_err_to_name(ret));
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Create touch button sensor failed: %d (%s)", ret, esp_err_to_name(ret));
            return;
        }
        ESP_LOGI(TAG, "[TOUCH] ✓ Touch button sensor created successfully, handle=%p", (void*)touch_button_handle_);
        
        // Touch sensor logging task disabled to avoid I2C contention during audio
        // Periodic logging task creation commented out
        // ESP_LOGI(TAG, "[TOUCH] Creating periodic logging task");
        // BaseType_t task_ret1 = xTaskCreatePinnedToCore(touch_log_task, "touch_log_task", 6 * 1024, this, 1, NULL, 1);
        // ESP_LOGI(TAG, "[TOUCH] touch_log_task creation result: %d", task_ret1);
        // if (task_ret1 == pdPASS) {
        //     ESP_LOGI(TAG, "[TOUCH] ✓ touch_log_task created successfully");
        // } else {
        //     ESP_LOGE(TAG, "[TOUCH] ERROR: Failed to create touch_log_task");
        // }
        
        // Create task to handle touch button sensor events
        ESP_LOGI(TAG, "[TOUCH] Creating touch button event handling task");
        BaseType_t task_ret2 = xTaskCreatePinnedToCore(
            touch_button_event_task,
            "touch_btn_task",
            4 * 1024,
            this,
            4,  // Slightly lower priority to reduce contention with audio
            NULL,
            1
        );
        ESP_LOGI(TAG, "[TOUCH] touch_button_event_task creation result: %d", task_ret2);
        if (task_ret2 == pdPASS) {
            ESP_LOGI(TAG, "[TOUCH] ✓ touch_button_event_task created successfully");
        } else {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Failed to create touch_button_event_task");
        }

        // Create task to process app-level touch button events (very lightweight priority)
        ESP_LOGI(TAG, "[TOUCH] Creating touch button app handling task");
        BaseType_t task_ret3 = xTaskCreatePinnedToCore(
            touch_button_app_task,
            "touch_btn_app_task",
            4 * 1024,
            this,
            3,  // Lower priority than audio and core UI tasks
            NULL,
            1
        );
        if (task_ret3 == pdPASS) {
            ESP_LOGI(TAG, "[TOUCH] ✓ touch_button_app_task created successfully");
        } else {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Failed to create touch_button_app_task");
        }
        
        // Step 5: Start touch sensor
        ESP_LOGI(TAG, "[TOUCH] Step 5: Starting touch sensor");
        ret = touch_sensor_lowlevel_start();
        ESP_LOGI(TAG, "[TOUCH] touch_sensor_lowlevel_start() returned: %d (%s)", ret, esp_err_to_name(ret));
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Touch sensor start failed: %d (%s)", ret, esp_err_to_name(ret));
            return;
        }
        
        ESP_LOGI(TAG, "[TOUCH] ===== Touch button initialization complete =====");
        ESP_LOGI(TAG, "[TOUCH] Touch sensor is now active and monitoring GPIO7");
        ESP_LOGI(TAG, "[TOUCH] Touch events will be ignored during speaking/listening mode");
    }

    void InitializeBmi270() {
        ESP_LOGI(TAG, "[BMI270] ===== Starting BMI270 initialization =====");
        
        // Validate I2C bus handle
        if (!shared_i2c_bus_handle_) {
            ESP_LOGE(TAG, "[BMI270] Shared I2C bus not initialized");
            return;
        }

        // Create BMI270 sensor using the shared I2C bus wrapper
        // bmi270_config_file is provided by the bmi270_api.h header
        ESP_LOGI(TAG, "[BMI270] Creating BMI270 sensor...");
        esp_err_t ret = bmi270_sensor_create(
            shared_i2c_bus_handle_, 
            &bmi270_handle_, 
            bmi270_config_file,
            BMI2_GYRO_CROSS_SENS_ENABLE | BMI2_CRT_RTOSK_ENABLE
        );
        
        if (ret != ESP_OK || !bmi270_handle_) {
            ESP_LOGE(TAG, "[BMI270] BMI270 create failed: %s", esp_err_to_name(ret));
            return;
        }
        ESP_LOGI(TAG, "[BMI270] ✓ BMI270 sensor created successfully");

        // Enable accelerometer and gyroscope
        ESP_LOGI(TAG, "[BMI270] Enabling accelerometer and gyroscope...");
        const uint8_t sens_list[] = {BMI2_ACCEL, BMI2_GYRO};
        int8_t rslt = bmi270_sensor_enable(sens_list, 2, bmi270_handle_);
        if (rslt != BMI2_OK) {
            ESP_LOGE(TAG, "[BMI270] Failed to enable BMI270 sensors: %d", rslt);
            return;
        }
        ESP_LOGI(TAG, "[BMI270] ✓ BMI270 sensors enabled");

        // Configure accelerometer
        ESP_LOGI(TAG, "[BMI270] Configuring accelerometer...");
        struct bmi2_sens_config accel_config = {.type = BMI2_ACCEL};
        rslt = bmi270_get_sensor_config(&accel_config, 1, bmi270_handle_);
        if (rslt == BMI2_OK) {
            accel_config.cfg.acc.odr = BMI2_ACC_ODR_100HZ;           // 100Hz output data rate
            accel_config.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;         // Normal mode, average 4 samples
            accel_config.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;   // Performance optimized mode
            accel_config.cfg.acc.range = BMI2_ACC_RANGE_4G;          // ±4G range
            rslt = bmi270_set_sensor_config(&accel_config, 1, bmi270_handle_);
            if (rslt != BMI2_OK) {
                ESP_LOGW(TAG, "[BMI270] Failed to configure accelerometer: %d", rslt);
            } else {
                ESP_LOGI(TAG, "[BMI270] ✓ Accelerometer configured: ODR=100Hz, Range=±4G");
            }
        } else {
            ESP_LOGW(TAG, "[BMI270] Failed to get accelerometer config: %d", rslt);
        }

        // Configure gyroscope
        ESP_LOGI(TAG, "[BMI270] Configuring gyroscope...");
        struct bmi2_sens_config gyro_config = {.type = BMI2_GYRO};
        rslt = bmi270_get_sensor_config(&gyro_config, 1, bmi270_handle_);
        if (rslt == BMI2_OK) {
            gyro_config.cfg.gyr.odr = BMI2_GYR_ODR_100HZ;            // 100Hz output data rate
            gyro_config.cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;          // Normal bandwidth mode
            gyro_config.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;    // Performance optimized mode
            gyro_config.cfg.gyr.range = BMI2_GYR_RANGE_2000;         // ±2000°/s range
            rslt = bmi270_set_sensor_config(&gyro_config, 1, bmi270_handle_);
            if (rslt != BMI2_OK) {
                ESP_LOGW(TAG, "[BMI270] Failed to configure gyroscope: %d", rslt);
            } else {
                ESP_LOGI(TAG, "[BMI270] ✓ Gyroscope configured: ODR=100Hz, Range=±2000°/s");
            }
        } else {
            ESP_LOGW(TAG, "[BMI270] Failed to get gyroscope config: %d", rslt);
        }

        // Create task to read BMI270 data
        ESP_LOGI(TAG, "[BMI270] Creating BMI270 reading task...");
        BaseType_t task_ret = xTaskCreatePinnedToCore(
            Bmi270ReadTask,      // Task function
            "bmi270_task",       // Task name
            4 * 1024,            // Stack size (4KB)
            this,                // Task parameter (pass board instance)
            5,                   // Task priority
            NULL,                // Task handle (not stored)
            1                    // Core ID (core 1)
        );
        
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "[BMI270] Failed to create BMI270 reading task");
            return;
        }
        ESP_LOGI(TAG, "[BMI270] ✓ BMI270 reading task created successfully");
        ESP_LOGI(TAG, "[BMI270] ===== BMI270 initialization complete =====");
    }

    static void Bmi270ReadTask(void* arg) {
        EchoEar* board = static_cast<EchoEar*>(arg);
        if (!board || !board->bmi270_handle_) {
            ESP_LOGE(TAG, "[BMI270] Invalid BMI270 handle in read task");
            vTaskDelete(NULL);
            return;
        }

        ESP_LOGI(TAG, "[BMI270] Reading task started");
        struct bmi2_sens_data sensor_data = {0};
        uint32_t read_count = 0;
        uint32_t error_count = 0;

        while (true) {
            // Read sensor data
            int8_t rslt = bmi2_get_sensor_data(&sensor_data, board->bmi270_handle_);
            if (rslt == BMI2_OK) {
                read_count++;
                
                // Access accelerometer data
                int16_t accel_x = sensor_data.acc.x;
                int16_t accel_y = sensor_data.acc.y;
                int16_t accel_z = sensor_data.acc.z;
                
                // Access gyroscope data
                int16_t gyro_x = sensor_data.gyr.x;
                int16_t gyro_y = sensor_data.gyr.y;
                int16_t gyro_z = sensor_data.gyr.z;
                
                // Log sensor data every 10 readings (every 10 seconds)
                /*if (read_count % 10 == 0) {
                    ESP_LOGI(TAG, "[BMI270] Reading #%lu - Accel: X=%d, Y=%d, Z=%d | Gyro: X=%d, Y=%d, Z=%d",
                             (unsigned long)read_count, accel_x, accel_y, accel_z,
                             gyro_x, gyro_y, gyro_z);
                } else {
                    // Log at DEBUG level for other readings
                    ESP_LOGD(TAG, "[BMI270] Accel: X=%d, Y=%d, Z=%d | Gyro: X=%d, Y=%d, Z=%d",
                             accel_x, accel_y, accel_z, gyro_x, gyro_y, gyro_z);
                }*/
            } else {
                error_count++;
                ESP_LOGW(TAG, "[BMI270] Failed to read BMI270 data: %d (error count: %lu)", 
                         rslt, (unsigned long)error_count);
            }
            
            // Wait 1 second before next reading
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    void InitializeCharge() {
        if (!ProbeI2cDevice(0x55, "Charge IC")) {
            ESP_LOGW(TAG, "[BATTERY] Charge IC not detected, battery reporting disabled");
            charge_ = nullptr;
            return;
        }
        charge_ = new Charge(i2c_bus_, 0x55);
        // If the first read already fails, disable battery reporting to avoid
        // continuous I2C NACK spam from periodic battery polling.
        if (charge_->GetVoltage() == 0) {
            ESP_LOGW(TAG, "[BATTERY] Charge IC unresponsive, battery reporting disabled");
            delete charge_;
            charge_ = nullptr;
            return;
        }
        xTaskCreatePinnedToCore(Charge::TaskFunction, "batterydecTask", 3 * 1024, charge_, 6, NULL, 0);
    }

    void InitializeCst816sTouchPad() {
        if (!ProbeI2cDevice(0x15, "CST816S")) {
            ESP_LOGW(TAG, "[TOUCH] CST816S not detected, touch disabled");
            cst816s_ = nullptr;
            return;
        }
        cst816s_ = new Cst816s(i2c_bus_, 0x15);
        
        // Create queue for touch events (ISR -> task communication)
        touch_event_queue_ = xQueueCreate(5, sizeof(uint32_t));
        if (touch_event_queue_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create touch event queue");
            return;
        }

        // Create task to handle touch events (runs in task context, safe for GetInstance())
        BaseType_t task_ret = xTaskCreatePinnedToCore(
            touch_event_task,
            "touch_event_task",
            4 * 1024,  // 4KB stack
            this,
            5,  // Priority 5
            &touch_event_task_handle_,
            1   // Pin to core 1
        );
        if (task_ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create touch event task");
            vQueueDelete(touch_event_queue_);
            touch_event_queue_ = nullptr;
            return;
        }

        const gpio_config_t int_gpio_config = {
            .pin_bit_mask = (1ULL << TP_PIN_NUM_INT),
            .mode = GPIO_MODE_INPUT,
            .intr_type = GPIO_INTR_NEGEDGE
        };
        gpio_config(&int_gpio_config);
        gpio_install_isr_service(0);
        gpio_intr_enable(TP_PIN_NUM_INT);
        // Pass queue handle to ISR callback (will be used as arg parameter)
        gpio_isr_handler_add(TP_PIN_NUM_INT, EchoEar::lvgl_port_touch_isr_cb, touch_event_queue_);
        
        ESP_LOGI(TAG, "CST816S touch screen initialized - touch coordinates will be logged");
        ESP_LOGI(TAG, "Note: LVGL integration requires a custom touch driver wrapper (TODO)");
    }

    void InitializeSpi() {
        const spi_bus_config_t bus_config = TAIJIPI_ST77916_PANEL_BUS_QSPI_CONFIG(QSPI_PIN_NUM_LCD_PCLK,
                                                                        QSPI_PIN_NUM_LCD_DATA0,
                                                                        QSPI_PIN_NUM_LCD_DATA1,
                                                                        QSPI_PIN_NUM_LCD_DATA2,
                                                                        QSPI_PIN_NUM_LCD_DATA3,
                                                                        QSPI_LCD_H_RES * 80 * sizeof(uint16_t));
        ESP_ERROR_CHECK(spi_bus_initialize(QSPI_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void Initializest77916Display() {

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        const esp_lcd_panel_io_spi_config_t io_config = ST77916_PANEL_IO_QSPI_CONFIG(QSPI_PIN_NUM_LCD_CS, NULL, NULL);
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)QSPI_LCD_HOST, &io_config, &panel_io));
        st77916_vendor_config_t vendor_config = {
            .init_cmds = vendor_specific_init_yysj,
            .init_cmds_size = sizeof(vendor_specific_init_yysj) / sizeof(st77916_lcd_init_cmd_t),
            .flags = {
                .use_qspi_interface = 1,
            },
        };
        const esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = QSPI_PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = QSPI_LCD_BIT_PER_PIXEL,
            .vendor_config = &vendor_config,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_disp_on_off(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_20_4,
                                        .emoji_font = font_emoji_64_init(),
                                    });
        // Rotate display 180° (upside down)
        display_->SetDisplayRotation180(true);
        backlight_ = new PwmBacklight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        backlight_->RestoreBrightness();
    }

    void InitializePowerSaveTimer() {
        // Create power save timer: -1 (no CPU freq limit), 30 seconds to sleep, -1 (no shutdown)
        power_save_timer_ = new PowerSaveTimer(-1, 30, -1);
        power_save_timer_->OnEnterSleepMode([this]() {
            // Check battery level to determine sleep behavior
            int battery_level = 0;
            bool charging = false;
            bool discharging = false;

            if (GetBatteryLevel(battery_level, charging, discharging)) {
                if (battery_level < 25 && !charging) {
                    // < 25%, not charging: shutdown
                    ESP_LOGW(TAG, "Battery level %d%% < 25%% and not charging - shutting down", battery_level);
                    gpio_set_level(POWER_CTRL, 1);
                    return;
                } else if (battery_level < 25 && charging) {
                    // < 25%, charging: always powersaving mode + sleepy.gif
                    ESP_LOGI(TAG, "Always power saving mode - battery %d%% < 25%% and charging, setting brightness to 20 and switching to sleepy animation", battery_level);
                    GetBacklight()->SetBrightness(20, false);
                    auto display = GetDisplay();
                    if (display) {
                        display->SetEmotion("sleepy");
                    }
                } else if (battery_level >= 25 && battery_level <= 40 && !charging) {
                    // 25-40%, not charging: always powersaving mode + battery.gif
                    ESP_LOGI(TAG, "Always power saving mode - battery %d%% (25-40%% range) and not charging, setting brightness to 20 and switching to battery animation", battery_level);
                    GetBacklight()->SetBrightness(20, false);
                    auto display = GetDisplay();
                    if (display) {
                        display->SetEmotion("battery");
                    }
                } else if (battery_level >= 25 && battery_level <= 40 && charging) {
                    // 25-40%, charging: 30-sec powersaving mode + sleepy.gif
                    ESP_LOGI(TAG, "30-sec power saving mode - battery %d%% (25-40%% range) and charging, setting brightness to 20 and switching to sleepy animation", battery_level);
                    GetBacklight()->SetBrightness(20, false);
                    auto display = GetDisplay();
                    if (display) {
                        display->SetEmotion("sleepy");
                    }
                } else {
                    // > 40%: 30-sec powersaving mode + sleepy.gif (regardless of charging)
                    ESP_LOGI(TAG, "30-sec power saving mode - battery %d%% (>40%%), setting brightness to 20 and switching to sleepy animation", battery_level);
                    GetBacklight()->SetBrightness(20, false);
                    auto display = GetDisplay();
                    if (display) {
                        display->SetEmotion("sleepy");
                    }
                }
            } else {
                // Can't read battery - use default behavior (30-sec mode with sleepy)
                ESP_LOGI(TAG, "30-sec power saving mode - battery level unknown, setting brightness to 20 and switching to sleepy animation");
                GetBacklight()->SetBrightness(20, false);
                auto display = GetDisplay();
                if (display) {
                    display->SetEmotion("sleepy");
                }
            }
        });
        power_save_timer_->OnExitSleepMode([this]() {
            // Check if we're in "always powersaving" mode - if so, immediately put back to sleep
            int battery_level = 0;
            bool charging = false;
            bool discharging = false;

            if (GetBatteryLevel(battery_level, charging, discharging)) {
                bool always_powersaving = false;
                if (battery_level < 25 && charging) {
                    // < 25%, charging: always powersaving
                    always_powersaving = true;
                } else if (battery_level >= 25 && battery_level <= 40 && !charging) {
                    // 25-40%, not charging: always powersaving
                    always_powersaving = true;
                }

                if (always_powersaving) {
                    ESP_LOGI(TAG, "Always power saving mode active - immediately re-entering sleep mode");
                    // Immediately re-apply sleep settings
                    GetBacklight()->SetBrightness(20, false);
                    auto display = GetDisplay();
                    if (display) {
                        if (battery_level < 25 && charging) {
                            display->SetEmotion("sleepy");
                        } else if (battery_level >= 25 && battery_level <= 40 && !charging) {
                            display->SetEmotion("battery");
                        }
                    }
                    return;  // Don't restore brightness/animation for always powersaving mode
                }
            }

            ESP_LOGI(TAG, "Exiting sleep mode - restoring brightness and animation");
            GetBacklight()->RestoreBrightness();

            // Small delay to allow I2C bus and other peripherals to stabilize after wake
            vTaskDelay(pdMS_TO_TICKS(100));

            auto display = GetDisplay();
            if (display) {
                display->SetEmotion("normal");  // Restore to normal animation (maps to ANIMATION_NORMAL)
            }
            // Ensure application state is idle after waking from sleep
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() != kDeviceStateIdle) {
                ESP_LOGI(TAG, "Waking from sleep - setting device state to idle");
                app.SetDeviceState(kDeviceStateIdle);
            }
        });
        power_save_timer_->SetEnabled(true);

        // Start battery monitoring task for "always powersaving" mode
        InitializeBatteryMonitor();
    }

    void InitializeBatteryMonitor() {
        // Create battery monitoring task to check for "always powersaving" mode
        xTaskCreate([](void* arg) {
            EchoEar* board = static_cast<EchoEar*>(arg);
            bool was_always_powersaving = false;
            int log_counter = 0;  // Counter for 5-minute battery logging (60 iterations * 5 seconds = 5 minutes)
            
            while (true) {
                int battery_level = 0;
                bool charging = false;
                bool discharging = false;

                if (board->GetBatteryLevel(battery_level, charging, discharging)) {
                    bool always_powersaving = false;
                    if (battery_level < 25 && charging) {
                        // < 25%, charging: always powersaving
                        always_powersaving = true;
                    } else if (battery_level >= 25 && battery_level <= 40 && !charging) {
                        // 25-40%, not charging: always powersaving
                        always_powersaving = true;
                    }

                    if (always_powersaving) {
                        // If just entered always powersaving mode, immediately apply sleep settings
                        if (!was_always_powersaving) {
                            ESP_LOGI(TAG, "[BATTERY] Entered always power saving mode - applying sleep settings immediately");
                            board->GetBacklight()->SetBrightness(20, false);
                            auto display = board->GetDisplay();
                            if (display) {
                                if (battery_level < 25 && charging) {
                                    display->SetEmotion("sleepy");
                                } else if (battery_level >= 25 && battery_level <= 40 && !charging) {
                                    display->SetEmotion("battery");
                                }
                            }
                        }
                        // If in always powersaving mode and not in sleep mode, force it
                        if (board->power_save_timer_ && !board->power_save_timer_->IsInSleepMode()) {
                            ESP_LOGI(TAG, "[BATTERY] Always power saving mode - ensuring sleep settings applied");
                            board->GetBacklight()->SetBrightness(20, false);
                            auto display = board->GetDisplay();
                            if (display) {
                                if (battery_level < 25 && charging) {
                                    display->SetEmotion("sleepy");
                                } else if (battery_level >= 25 && battery_level <= 40 && !charging) {
                                    display->SetEmotion("battery");
                                }
                            }
                        }
                    } else if (was_always_powersaving) {
                        // Exited always powersaving mode - restore normal brightness if not in timer-based sleep mode
                        if (board->power_save_timer_ && !board->power_save_timer_->IsInSleepMode()) {
                            ESP_LOGI(TAG, "[BATTERY] Exited always power saving mode - restoring brightness");
                            board->GetBacklight()->RestoreBrightness();
                        }
                    }
                    
                    was_always_powersaving = always_powersaving;

                    // Log battery percentage every 30 seconds (6 iterations * 5 seconds)
                    log_counter++;
                    if (log_counter >= 6) {
                        log_counter = 0;
                        // Use ESP_LOGE so it goes through error logging system to /sdcard/err.txt
                        ESP_LOGE(TAG, "[BATTERY] %d%%, %s", 
                                battery_level, charging ? "charging" : (discharging ? "discharging" : "idle"));
                    }
                }

                // Check every 5 seconds
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
        }, "battery_monitor", 4096, this, 5, NULL);
        ESP_LOGI(TAG, "[BATTERY] Battery monitoring task started for always power saving mode");
    }

    void InitializeEmotionResetTimer() {
        // Create timer to reset emotion to previous state after one animation cycle
        const esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                EchoEar* board = static_cast<EchoEar*>(arg);
                if (board != nullptr) {
                    // Restore previous emotion
                    auto display = board->GetDisplay();
                    if (display != nullptr) {
                        display->SetEmotion(board->previous_emotion_.c_str());
                        ESP_LOGI(TAG, "[TOUCH] Emotion restored to previous state: %s", board->previous_emotion_.c_str());
                    }
                }
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "emotion_reset_timer",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &emotion_reset_timer_));
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                ESP_LOGI(TAG, "Ignoring BOOT click during startup");
                return;
            }
            if (power_save_timer_) {
                // If in sleep mode, wake up and directly start talking
                if (power_save_timer_->IsInSleepMode()) {
                    power_save_timer_->WakeUp();
                    app.ToggleChatState();
                } else {
                    // Normal operation: wake up and toggle chat state
                    power_save_timer_->WakeUp();
                    app.ToggleChatState();
                }
            } else {
                app.ToggleChatState();
            }
        });

        boot_button_.OnLongPress([this]() {
            ToggleMute();
        });

         gpio_config_t power_gpio_config = {
            .pin_bit_mask = (BIT64(POWER_CTRL) ),
            .mode = GPIO_MODE_OUTPUT,
            
      };
        ESP_ERROR_CHECK(gpio_config(&power_gpio_config));

        gpio_set_level(POWER_CTRL, 0);
    }

public:
    EchoEar() : boot_button_(BOOT_BUTTON_GPIO, false, 5000, 0) {  // 5-second long press time
        InitializeI2c();
        InitializeBmi270();  // Initialize BMI270 after I2C bus is ready
        InitializeCharge();
        InitializeCst816sTouchPad();
        
        InitializeSpi();
        Initializest77916Display();
        InitializeButtons();
        InitializePowerSaveTimer();
        InitializeEmotionResetTimer();
        ESP_LOGI(TAG, "[TOUCH] About to call InitializeTouchButton()");
        InitializeTouchButton();
        ESP_LOGI(TAG, "[TOUCH] InitializeTouchButton() returned");
        
        // Start SD card + animation init in background to allow WiFi startup in parallel.
        // Pin to core 0 so WiFi (core 1 in your logs) can connect concurrently.
        ESP_LOGI(TAG, "[SD/ANIM] Starting background init task");
        xTaskCreatePinnedToCore(SdAnimInitTask, "sd_anim_init", 8192, nullptr, 1, nullptr, 0);
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }
    
    virtual Display* GetDisplay() override {
        return display_;
    }

    Cst816s* GetTouchpad() {
        return cst816s_;
    }

    virtual Backlight* GetBacklight() override {
        return backlight_;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        if (charge_ == nullptr || !charge_->IsAvailable()) {
            return false;
        }
        
        // Read voltage and current from charge IC
        uint16_t voltage_mv = charge_->GetVoltage();
        int16_t current_ma = charge_->GetCurrent();
        
        // Determine charging/discharging status based on current
        // Positive current typically means charging, negative means discharging
        charging = (current_ma > 50);  // Threshold: > 50mA considered charging
        discharging = (current_ma < -50);  // Threshold: < -50mA considered discharging
        
        // Convert voltage to battery level percentage
        // Typical Li-ion battery: 3.0V (0%) to 4.2V (100%)
        // Voltage register might be in different units - adjust based on actual chip
        // Assuming voltage is in millivolts, typical range: 3000mV (0%) to 4200mV (100%)
        const uint16_t MIN_VOLTAGE_MV = 3000;  // 3.0V = 0%
        const uint16_t MAX_VOLTAGE_MV = 4200;  // 4.2V = 100%
        
        // Clamp voltage to valid range
        if (voltage_mv < MIN_VOLTAGE_MV) {
            voltage_mv = MIN_VOLTAGE_MV;
        } else if (voltage_mv > MAX_VOLTAGE_MV) {
            voltage_mv = MAX_VOLTAGE_MV;
        }
        
        // Calculate percentage using linear interpolation
        level = ((voltage_mv - MIN_VOLTAGE_MV) * 100) / (MAX_VOLTAGE_MV - MIN_VOLTAGE_MV);
        
        // Ensure level is in valid range
        if (level < 0) level = 0;
        if (level > 100) level = 100;
        
        // Log battery status every 15 seconds
        static int64_t last_log_time = 0;
        int64_t current_time = esp_timer_get_time() / 1000; // Convert to milliseconds
        const int64_t LOG_INTERVAL_MS = 15000; // 15 seconds
        
        if (current_time - last_log_time >= LOG_INTERVAL_MS) {
            ESP_LOGI(TAG, "[BATTERY] Voltage: %d mV, Current: %d mA, Level: %d%%, Charging: %s, Discharging: %s",
                     voltage_mv, current_ma, level, charging ? "yes" : "no", discharging ? "yes" : "no");
            last_log_time = current_time;
        }
        
        return true;
    }

    virtual void StartNetwork() override {
        // Call parent's StartNetwork to proceed with normal WiFi initialization
        WifiBoard::StartNetwork();
    }
};

// Static member definition
volatile uint32_t EchoEar::touch_event_count_ = 0;

DECLARE_BOARD(EchoEar);

