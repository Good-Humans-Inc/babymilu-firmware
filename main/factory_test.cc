#include "factory_test.h"

#include "config.h"
#include "display.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/time.h>

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <nvs.h>

#if defined(LV_USE_QRCODE) && LV_USE_QRCODE
#include <libs/qrcode/lv_qrcode.h>
#endif

namespace {
constexpr const char* TAG = "FactoryTest";
constexpr const char* NVS_NS = "factory";
constexpr const char* NVS_KEY_ENABLED = "factory_mode_enabled";
constexpr int64_t kRtcWaitMs = 2500;
constexpr int64_t kQrExitHoldMs = 10000;
constexpr int64_t kBmiContinuousMotionMs = 2000;
constexpr int64_t kBmiGapResetMs = 400;
constexpr int kBmiMotionThresholdPx = 3;
constexpr int kFactoryEntryHoldMs = 3000;

std::array<factory_test_state_t, FACTORY_TEST_COUNT> g_test_states;
std::array<std::string, FACTORY_TEST_COUNT> g_test_details;
factory_required_actions_t g_actions = {};
bool g_auto_started = false;

int64_t NowMs() {
    return esp_timer_get_time() / 1000;
}

bool IsFactoryPartition() {
    const esp_partition_t* partition = esp_ota_get_running_partition();
    return partition != nullptr && strcmp(partition->label, "factory") == 0;
}

bool ReadFactoryModeFlagFromNvs() {
    nvs_handle_t handle = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    uint8_t enabled = 0;
    esp_err_t err = nvs_get_u8(handle, NVS_KEY_ENABLED, &enabled);
    nvs_close(handle);
    return err == ESP_OK && enabled == 1;
}

bool WriteFactoryModeFlagToNvs(bool enabled) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_u8(handle, NVS_KEY_ENABLED, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "factory_mode_enabled write failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "factory_mode_enabled=%d", enabled ? 1 : 0);
    return true;
}

void NormalizeMac(const uint8_t mac[6], char* out, size_t out_size) {
    snprintf(out, out_size, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

std::string DisplayMac(const char* compact_mac) {
    if (compact_mac == nullptr || strlen(compact_mac) != 12) {
        return "--:--:--:--:--:--";
    }
    char formatted[18];
    snprintf(formatted, sizeof(formatted), "%.2s:%.2s:%.2s:%.2s:%.2s:%.2s",
             compact_mac, compact_mac + 2, compact_mac + 4,
             compact_mac + 6, compact_mac + 8, compact_mac + 10);
    return std::string(formatted);
}

void SetTestStateWithDetail(factory_test_id_t id, factory_test_state_t state, const std::string& detail) {
    if (id < 0 || id >= FACTORY_TEST_COUNT) {
        return;
    }
    g_test_states[id] = state;
    g_test_details[id] = detail;
    ESP_LOGI(TAG, "test[%d]=%d detail=%s", static_cast<int>(id), static_cast<int>(state), detail.c_str());
}

const char* StateText(factory_test_state_t state) {
    switch (state) {
    case FACTORY_TEST_STATE_PENDING:
        return "PENDING";
    case FACTORY_TEST_STATE_RUNNING:
        return "RUN";
    case FACTORY_TEST_STATE_PASS:
        return "PASS";
    case FACTORY_TEST_STATE_FAIL:
        return "FAIL";
    default:
        return "?";
    }
}

const char* TestName(factory_test_id_t id) {
    switch (id) {
    case FACTORY_TEST_POWER:
        return "PWR";
    case FACTORY_TEST_VOLUME:
        return "VOL";
    case FACTORY_TEST_BMI270:
        return "BMI";
    case FACTORY_TEST_BLUETOOTH:
        return "BT";
    case FACTORY_TEST_WIFI:
        return "WIFI";
    case FACTORY_TEST_RTC:
        return "RTC";
    case FACTORY_TEST_TOUCH:
        return "TOUCH";
    case FACTORY_TEST_BATTERY:
        return "BAT";
    case FACTORY_TEST_AUDIO:
        return "AUDIO";
    case FACTORY_TEST_SD_CARD:
        return "SD";
    default:
        return "?";
    }
}

factory_test_state_t OverallState() {
    bool any_fail = false;
    bool all_pass = true;
    for (int i = 0; i < FACTORY_TEST_COUNT; ++i) {
        if (g_test_states[i] == FACTORY_TEST_STATE_FAIL) {
            any_fail = true;
        }
        if (g_test_states[i] != FACTORY_TEST_STATE_PASS) {
            all_pass = false;
        }
    }
    if (all_pass && g_actions.touch_triggered && g_actions.volume_changed && g_actions.audio_executed) {
        return FACTORY_TEST_STATE_PASS;
    }
    return any_fail ? FACTORY_TEST_STATE_FAIL : FACTORY_TEST_STATE_RUNNING;
}

gpio_num_t FactoryEntryGpio() {
#if defined(PWR_BUTTON_GPIO) && (PWR_BUTTON_GPIO != GPIO_NUM_NC)
    return PWR_BUTTON_GPIO;
#elif defined(BOOT_BUTTON_GPIO) && (BOOT_BUTTON_GPIO != GPIO_NUM_NC)
    return BOOT_BUTTON_GPIO;
#else
    return GPIO_NUM_NC;
#endif
}

bool CheckButtonHeldMs(gpio_num_t gpio, int hold_ms) {
    if (gpio == GPIO_NUM_NC) {
        return false;
    }
    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (gpio_get_level(gpio) != 0) {
        return false;
    }
    const int64_t start_ms = NowMs();
    while ((NowMs() - start_ms) < hold_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (gpio_get_level(gpio) != 0) {
            return false;
        }
    }
    return true;
}
} // namespace

namespace FactoryTest {
class ControllerImpl {
public:
    void Start(Display* display, const char* fw_tag, const char* hw_tag) {
        display_ = display;
        fw_tag_ = (fw_tag != nullptr && *fw_tag != '\0') ? fw_tag : "FT1";
        hw_tag_ = (hw_tag != nullptr) ? hw_tag : "";

        char wifi_mac[13] = {0};
        char bt_mac[13] = {0};
        GetWifiMac(wifi_mac, sizeof(wifi_mac));
        GetBtMac(bt_mac, sizeof(bt_mac));
        wifi_mac_display_ = DisplayMac(wifi_mac);
        bt_mac_display_ = DisplayMac(bt_mac);

        char payload[96] = {0};
        if (BuildQrPayload(payload, sizeof(payload), fw_tag_.c_str(), hw_tag_.c_str())) {
            qr_payload_ = payload;
        }

        TestsInit();
        TestsStartAuto();
        SetTestStateWithDetail(FACTORY_TEST_POWER, FACTORY_TEST_STATE_PASS, "ENTERED");
        SetTestStateWithDetail(FACTORY_TEST_TOUCH, FACTORY_TEST_STATE_PENDING, "WAIT");
        SetTestStateWithDetail(FACTORY_TEST_VOLUME, FACTORY_TEST_STATE_PENDING, "WAIT");
        SetTestStateWithDetail(FACTORY_TEST_AUDIO, FACTORY_TEST_STATE_PENDING, "WAIT");
        StartRtcTest();
        BuildUi();

        if (!poll_task_started_) {
            poll_task_started_ = true;
            xTaskCreate([](void* arg) {
                auto* self = static_cast<ControllerImpl*>(arg);
                while (true) {
                    self->Poll();
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }, "factory_poll", 4096, this, 1, nullptr);
        }

        Render();
    }

    void Poll() {
        if (!display_ || !rtc_running_) {
            return;
        }

        if (NowMs() - rtc_started_ms_ < kRtcWaitMs) {
            return;
        }

        rtc_running_ = false;
        struct timeval tv = {};
        gettimeofday(&tv, nullptr);
        const time_t elapsed = tv.tv_sec - rtc_reference_;
        if (elapsed >= 2 && elapsed <= 5) {
            SetTestStateWithDetail(FACTORY_TEST_RTC, FACTORY_TEST_STATE_PASS, "TICK");
        } else {
            SetTestStateWithDetail(FACTORY_TEST_RTC, FACTORY_TEST_STATE_FAIL, "NO TICK");
        }
        Render();
    }

    void OnTouchDetected() {
        MarkTouchTriggered();
        SetTestStateWithDetail(FACTORY_TEST_TOUCH, FACTORY_TEST_STATE_PASS, "TOUCHED");
        Render();
    }

    void OnTouchscreenLongPress() {
        if (!AllTestsPassed()) {
            ESP_LOGI(TAG, "Touchscreen long press ignored until all tests pass");
            return;
        }
        qr_page_visible_ = true;
        Render();
    }

    void OnVolumeChanged(int volume) {
        MarkVolumeChanged();
        char detail[16];
        snprintf(detail, sizeof(detail), "%d%%", volume);
        SetTestStateWithDetail(FACTORY_TEST_VOLUME, FACTORY_TEST_STATE_PASS, detail);
        Render();
    }

    void OnAudioRecordingStarted() {
        SetTestStateWithDetail(FACTORY_TEST_AUDIO, FACTORY_TEST_STATE_RUNNING, "RECORD");
        Render();
    }

    void OnAudioPlaybackTriggered() {
        SetTestStateWithDetail(FACTORY_TEST_AUDIO, FACTORY_TEST_STATE_RUNNING, "PLAY");
        Render();
    }

    void OnAudioPlaybackFinished() {
        MarkAudioExecuted();
        SetTestStateWithDetail(FACTORY_TEST_AUDIO, FACTORY_TEST_STATE_PASS, "DONE");
        Render();
    }

    void OnBmiDotMoved(int x, int y) {
        if (!display_) {
            return;
        }

        const int64_t now_ms = NowMs();
        const bool has_previous = last_bmi_x_ != INT_MIN && last_bmi_y_ != INT_MIN;
        const bool moved = !has_previous ||
            (std::abs(x - last_bmi_x_) >= kBmiMotionThresholdPx) ||
            (std::abs(y - last_bmi_y_) >= kBmiMotionThresholdPx);

        last_bmi_x_ = x;
        last_bmi_y_ = y;

        if (GetTestState(FACTORY_TEST_BMI270) != FACTORY_TEST_STATE_PASS) {
            SetTestState(FACTORY_TEST_BMI270, FACTORY_TEST_STATE_RUNNING);
            if (moved) {
                if (bmi_last_motion_ms_ == 0 || (now_ms - bmi_last_motion_ms_) > kBmiGapResetMs) {
                    bmi_motion_start_ms_ = now_ms;
                }
                bmi_last_motion_ms_ = now_ms;
                if ((now_ms - bmi_motion_start_ms_) >= kBmiContinuousMotionMs) {
                    SetTestStateWithDetail(FACTORY_TEST_BMI270, FACTORY_TEST_STATE_PASS, "2S MOVE");
                }
            }
        }

        if (!page_test_ || !bmi_dot_) {
            return;
        }

        DisplayLockGuard lock(display_);
        const int max_x = display_->width() - 24;
        const int max_y = display_->height() - 24;
        if (bmi_dot_) {
            lv_obj_set_pos(bmi_dot_, std::max(12, std::min(x, max_x)), std::max(12, std::min(y, max_y)));
            if (qr_page_visible_) {
                lv_obj_add_flag(bmi_dot_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(bmi_dot_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (GetTestState(FACTORY_TEST_BMI270) == FACTORY_TEST_STATE_PASS) {
            Render();
        }
    }

    void OnWifiTestFinished(bool passed, const char* detail) {
        SetTestStateWithDetail(FACTORY_TEST_WIFI,
                               passed ? FACTORY_TEST_STATE_PASS : FACTORY_TEST_STATE_FAIL,
                               detail != nullptr ? detail : (passed ? "OK" : "FAIL"));
        Render();
    }

    void OnBluetoothTestFinished(bool passed, const char* detail) {
        SetTestStateWithDetail(FACTORY_TEST_BLUETOOTH,
                               passed ? FACTORY_TEST_STATE_PASS : FACTORY_TEST_STATE_FAIL,
                               detail != nullptr ? detail : (passed ? "ADV" : "FAIL"));
        Render();
    }

    void OnBatterySample(bool passed, int level) {
        char detail[24];
        if (passed) {
            snprintf(detail, sizeof(detail), "%d%%", level);
        } else {
            snprintf(detail, sizeof(detail), "NOT DETECTED");
        }
        SetTestStateWithDetail(FACTORY_TEST_BATTERY,
                               passed ? FACTORY_TEST_STATE_PASS : FACTORY_TEST_STATE_FAIL,
                               detail);
        Render();
    }

    void OnSdSample(bool present) {
        SetTestStateWithDetail(FACTORY_TEST_SD_CARD,
                               present ? FACTORY_TEST_STATE_PASS : FACTORY_TEST_STATE_FAIL,
                               present ? "SD OK" : "SD MISSING");
        Render();
    }

    bool IsQrPageVisible() const {
        return qr_page_visible_;
    }

    bool ShouldExitFactoryMode(int64_t held_ms) const {
        return held_ms >= kQrExitHoldMs;
    }

    void Render() {
        if (!display_ || !root_) {
            return;
        }

        DisplayLockGuard lock(display_);

        if (page_test_) {
            if (qr_page_visible_) {
                lv_obj_add_flag(page_test_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(page_test_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (page_qr_) {
            if (qr_page_visible_) {
                lv_obj_clear_flag(page_qr_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(page_qr_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (bmi_dot_) {
            if (qr_page_visible_) {
                lv_obj_add_flag(bmi_dot_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(bmi_dot_, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (identity_label_) {
            std::string identity = "W " + wifi_mac_display_ + "\n";
            identity += "B " + bt_mac_display_ + "\n";
            identity += "F " + fw_tag_;
            if (!hw_tag_.empty()) {
                identity += "  H " + hw_tag_;
            }
            lv_label_set_text(identity_label_, identity.c_str());
        }

        if (tests_label_) {
            std::string tests;
            for (int i = 0; i < FACTORY_TEST_COUNT; ++i) {
                tests += TestName(static_cast<factory_test_id_t>(i));
                tests += " ";
                tests += StateText(g_test_states[i]);
                if (!g_test_details[i].empty()) {
                    tests += " ";
                    tests += g_test_details[i];
                }
                if (i + 1 < FACTORY_TEST_COUNT) {
                    tests += "\n";
                }
            }
            lv_label_set_text(tests_label_, tests.c_str());
        }

        if (overall_label_) {
            const factory_test_state_t overall = OverallState();
            std::string text = "OVERALL ";
            text += StateText(overall);
            lv_label_set_text(overall_label_, text.c_str());
            lv_obj_set_style_text_color(
                overall_label_,
                overall == FACTORY_TEST_STATE_PASS ? lv_color_hex(0x24C06A)
                : (overall == FACTORY_TEST_STATE_FAIL ? lv_color_hex(0xFF5B5B) : lv_color_hex(0xF4C542)),
                0);
        }

        if (instruction_label_) {
            const char* instruction = AllTestsPassed()
                ? "Hold screen for QR. Hold TALK 10s to exit."
                : "Swipe screen for volume. TALK: record, then playback.";
            lv_label_set_text(instruction_label_, instruction);
        }

        if (qr_label_) {
            lv_label_set_text(qr_label_, "Scan to Register");
        }
        if (qr_hint_label_) {
            lv_label_set_text(qr_hint_label_, "Hold TALK 10s to exit");
        }

#if defined(LV_USE_QRCODE) && LV_USE_QRCODE
        if (qr_code_) {
            lv_qrcode_update(qr_code_, qr_payload_.c_str(), qr_payload_.size());
        }
#endif
    }

private:
    void BuildUi() {
        if (!display_) {
            return;
        }

        DisplayLockGuard lock(display_);
        auto* screen = lv_screen_active();

        root_ = lv_obj_create(screen);
        lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_radius(root_, 0, 0);
        lv_obj_set_style_pad_all(root_, 12, 0);
        lv_obj_set_style_border_width(root_, 0, 0);
        lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
        lv_obj_add_flag(root_, LV_OBJ_FLAG_FLOATING);
        lv_obj_move_foreground(root_);

        page_test_ = lv_obj_create(root_);
        lv_obj_set_size(page_test_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(page_test_, 0, 0);
        lv_obj_set_style_border_width(page_test_, 0, 0);
        lv_obj_set_style_bg_opa(page_test_, LV_OPA_TRANSP, 0);

        auto* title = lv_label_create(page_test_);
        lv_label_set_text(title, "FACTORY TEST");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

        identity_label_ = lv_label_create(page_test_);
        lv_obj_set_width(identity_label_, LV_PCT(100));
        lv_obj_set_style_text_color(identity_label_, lv_color_hex(0xD0D0D0), 0);
        lv_obj_align(identity_label_, LV_ALIGN_TOP_LEFT, 0, 28);

        tests_label_ = lv_label_create(page_test_);
        lv_obj_set_width(tests_label_, LV_PCT(100));
        lv_obj_set_style_text_color(tests_label_, lv_color_white(), 0);
        lv_obj_align(tests_label_, LV_ALIGN_TOP_LEFT, 0, 106);

        overall_label_ = lv_label_create(page_test_);
        lv_obj_align(overall_label_, LV_ALIGN_BOTTOM_LEFT, 0, -32);

        instruction_label_ = lv_label_create(page_test_);
        lv_obj_set_width(instruction_label_, LV_PCT(100));
        lv_obj_set_style_text_color(instruction_label_, lv_color_hex(0xB8B8B8), 0);
        lv_obj_align(instruction_label_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        bmi_dot_ = lv_obj_create(page_test_);
        lv_obj_set_size(bmi_dot_, 16, 16);
        lv_obj_set_style_radius(bmi_dot_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(bmi_dot_, lv_color_hex(0x24C06A), 0);
        lv_obj_set_style_border_width(bmi_dot_, 0, 0);
        lv_obj_set_pos(bmi_dot_, display_->width() / 2, display_->height() / 2);

        page_qr_ = lv_obj_create(root_);
        lv_obj_set_size(page_qr_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(page_qr_, 0, 0);
        lv_obj_set_style_border_width(page_qr_, 0, 0);
        lv_obj_set_style_bg_opa(page_qr_, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(page_qr_, LV_OBJ_FLAG_HIDDEN);

        qr_label_ = lv_label_create(page_qr_);
        lv_obj_set_style_text_color(qr_label_, lv_color_white(), 0);
        lv_obj_align(qr_label_, LV_ALIGN_TOP_MID, 0, 8);

#if defined(LV_USE_QRCODE) && LV_USE_QRCODE
        qr_code_ = lv_qrcode_create(page_qr_);
        lv_qrcode_set_size(qr_code_, 220);
        lv_qrcode_set_dark_color(qr_code_, lv_color_black());
        lv_qrcode_set_light_color(qr_code_, lv_color_white());
        lv_obj_set_style_border_width(qr_code_, 8, 0);
        lv_obj_set_style_border_color(qr_code_, lv_color_white(), 0);
        lv_obj_align(qr_code_, LV_ALIGN_CENTER, 0, 6);
#else
        qr_code_ = lv_label_create(page_qr_);
        lv_label_set_text(qr_code_, "QR disabled");
        lv_obj_set_style_text_color(qr_code_, lv_color_white(), 0);
        lv_obj_align(qr_code_, LV_ALIGN_CENTER, 0, 0);
#endif

        qr_hint_label_ = lv_label_create(page_qr_);
        lv_obj_set_style_text_color(qr_hint_label_, lv_color_hex(0xD0D0D0), 0);
        lv_obj_align(qr_hint_label_, LV_ALIGN_BOTTOM_MID, 0, -8);
    }

    void StartRtcTest() {
        struct timeval tv = {};
        tv.tv_sec = 1704067200; // 2024-01-01T00:00:00Z
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        rtc_reference_ = tv.tv_sec;
        rtc_started_ms_ = NowMs();
        rtc_running_ = true;
        SetTestStateWithDetail(FACTORY_TEST_RTC, FACTORY_TEST_STATE_RUNNING, "WAIT 2S");
    }

    Display* display_ = nullptr;
    std::string fw_tag_ = "FT1";
    std::string hw_tag_ = "";
    std::string wifi_mac_display_;
    std::string bt_mac_display_;
    std::string qr_payload_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* page_test_ = nullptr;
    lv_obj_t* page_qr_ = nullptr;
    lv_obj_t* identity_label_ = nullptr;
    lv_obj_t* tests_label_ = nullptr;
    lv_obj_t* overall_label_ = nullptr;
    lv_obj_t* instruction_label_ = nullptr;
    lv_obj_t* bmi_dot_ = nullptr;
    lv_obj_t* qr_label_ = nullptr;
    lv_obj_t* qr_code_ = nullptr;
    lv_obj_t* qr_hint_label_ = nullptr;
    bool qr_page_visible_ = false;
    bool poll_task_started_ = false;
    bool rtc_running_ = false;
    time_t rtc_reference_ = 0;
    int64_t rtc_started_ms_ = 0;
    int last_bmi_x_ = INT_MIN;
    int last_bmi_y_ = INT_MIN;
    int64_t bmi_motion_start_ms_ = 0;
    int64_t bmi_last_motion_ms_ = 0;
};

ControllerImpl& Impl() {
    static ControllerImpl instance;
    return instance;
}

bool ShouldEnterFactoryMode() {
    return IsFactoryPartition() || ReadFactoryModeFlagFromNvs();
}

bool IsFactoryTestMode() {
    return ShouldEnterFactoryMode();
}

void SetFactoryMode(bool enabled) {
    WriteFactoryModeFlagToNvs(enabled);
}

bool MaybeLatchFactoryModeFromBootButton() {
    const gpio_num_t gpio = FactoryEntryGpio();
    if (gpio == GPIO_NUM_NC || ShouldEnterFactoryMode()) {
        return false;
    }
    if (!CheckButtonHeldMs(gpio, kFactoryEntryHoldMs)) {
        return false;
    }
    ESP_LOGI(TAG, "Factory entry button held for %d ms", kFactoryEntryHoldMs);
    return WriteFactoryModeFlagToNvs(true);
}

bool GetWifiMac(char* out, size_t out_size) {
    if (out == nullptr || out_size < 13) {
        return false;
    }
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return false;
    }
    NormalizeMac(mac, out, out_size);
    return true;
}

bool GetBtMac(char* out, size_t out_size) {
    if (out == nullptr || out_size < 13) {
        return false;
    }
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_BT) != ESP_OK) {
        return false;
    }
    NormalizeMac(mac, out, out_size);
    return true;
}

bool BuildQrPayload(char* out, size_t out_size, const char* fw_tag, const char* hw_tag) {
    if (out == nullptr || out_size == 0 || fw_tag == nullptr || hw_tag == nullptr) {
        return false;
    }
    char wifi_mac[13] = {0};
    char bt_mac[13] = {0};
    if (!GetWifiMac(wifi_mac, sizeof(wifi_mac)) || !GetBtMac(bt_mac, sizeof(bt_mac))) {
        return false;
    }
    int n = snprintf(out, out_size, "W=%s;B=%s;F=%s;H=%s", wifi_mac, bt_mac, fw_tag, hw_tag);
    const bool ok = n > 0 && static_cast<size_t>(n) < out_size;
    if (ok) {
        ESP_LOGI(TAG, "QR payload: %s", out);
    }
    return ok;
}

void TestsInit() {
    g_test_states.fill(FACTORY_TEST_STATE_PENDING);
    g_test_details.fill("");
    g_actions = {};
    g_auto_started = false;
}

void TestsStartAuto() {
    g_auto_started = true;
    SetTestStateWithDetail(FACTORY_TEST_POWER, FACTORY_TEST_STATE_PASS, "BOOT");
    SetTestStateWithDetail(FACTORY_TEST_BMI270, FACTORY_TEST_STATE_RUNNING, "MOVE 2S");
    SetTestStateWithDetail(FACTORY_TEST_BLUETOOTH, FACTORY_TEST_STATE_RUNNING, "INIT");
    SetTestStateWithDetail(FACTORY_TEST_WIFI, FACTORY_TEST_STATE_RUNNING, "SCAN");
    SetTestStateWithDetail(FACTORY_TEST_RTC, FACTORY_TEST_STATE_RUNNING, "WAIT 2S");
    SetTestStateWithDetail(FACTORY_TEST_BATTERY, FACTORY_TEST_STATE_RUNNING, "READ");
    SetTestStateWithDetail(FACTORY_TEST_SD_CARD, FACTORY_TEST_STATE_RUNNING, "CHECK");
}

void TestsPoll() {
    if (g_auto_started) {
        Impl().Poll();
    }
}

void SetTestState(factory_test_id_t id, factory_test_state_t state) {
    if (id < 0 || id >= FACTORY_TEST_COUNT) {
        return;
    }
    g_test_states[id] = state;
}

factory_test_state_t GetTestState(factory_test_id_t id) {
    if (id < 0 || id >= FACTORY_TEST_COUNT) {
        return FACTORY_TEST_STATE_FAIL;
    }
    return g_test_states[id];
}

bool CanEnterQrPage() {
    return g_actions.touch_triggered && g_actions.volume_changed && g_actions.audio_executed;
}

bool AllTestsPassed() {
    for (int i = 0; i < FACTORY_TEST_COUNT; ++i) {
        if (g_test_states[i] != FACTORY_TEST_STATE_PASS) {
            return false;
        }
    }
    return CanEnterQrPage();
}

void MarkTouchTriggered() {
    g_actions.touch_triggered = true;
    SetTestState(FACTORY_TEST_TOUCH, FACTORY_TEST_STATE_PASS);
}

void MarkVolumeChanged() {
    g_actions.volume_changed = true;
    SetTestState(FACTORY_TEST_VOLUME, FACTORY_TEST_STATE_PASS);
}

void MarkAudioExecuted() {
    g_actions.audio_executed = true;
    SetTestState(FACTORY_TEST_AUDIO, FACTORY_TEST_STATE_PASS);
}

factory_required_actions_t GetRequiredActions() {
    return g_actions;
}

Controller& Controller::GetInstance() {
    static Controller controller;
    return controller;
}

void Controller::Start(Display* display, const char* fw_tag, const char* hw_tag) {
    Impl().Start(display, fw_tag, hw_tag);
}

void Controller::Poll() {
    Impl().Poll();
}

void Controller::OnTouchDetected() {
    Impl().OnTouchDetected();
}

void Controller::OnTouchscreenLongPress() {
    Impl().OnTouchscreenLongPress();
}

void Controller::OnVolumeChanged(int volume) {
    Impl().OnVolumeChanged(volume);
}

void Controller::OnAudioRecordingStarted() {
    Impl().OnAudioRecordingStarted();
}

void Controller::OnAudioPlaybackTriggered() {
    Impl().OnAudioPlaybackTriggered();
}

void Controller::OnAudioPlaybackFinished() {
    Impl().OnAudioPlaybackFinished();
}

void Controller::OnBmiDotMoved(int x, int y) {
    Impl().OnBmiDotMoved(x, y);
}

void Controller::OnWifiTestFinished(bool passed, const char* detail) {
    Impl().OnWifiTestFinished(passed, detail);
}

void Controller::OnBluetoothTestFinished(bool passed, const char* detail) {
    Impl().OnBluetoothTestFinished(passed, detail);
}

void Controller::OnBatterySample(bool passed, int level) {
    Impl().OnBatterySample(passed, level);
}

void Controller::OnSdSample(bool present) {
    Impl().OnSdSample(present);
}

bool Controller::IsQrPageVisible() const {
    return Impl().IsQrPageVisible();
}

bool Controller::ShouldExitFactoryMode(int64_t held_ms) const {
    return Impl().ShouldExitFactoryMode(held_ms);
}

void Controller::Render() {
    Impl().Render();
}
} // namespace FactoryTest
