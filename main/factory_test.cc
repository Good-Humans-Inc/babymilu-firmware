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

#if defined(CONFIG_BOARD_TYPE_ECHOEAR)
LV_FONT_DECLARE(font_puhui_20_4);
#endif

namespace {
constexpr const char* TAG = "FactoryTest";
constexpr const char* NVS_NS = "factory";
constexpr const char* NVS_KEY_ENABLED = "ft_en";
constexpr const char* NVS_KEY_EXIT_PENDING = "ft_exit";
constexpr int64_t kRtcWaitMs = 2500;
constexpr int64_t kQrExitHoldMs = 3000;
constexpr int64_t kBmiContinuousMotionMs = 2000;
constexpr int64_t kBmiGapResetMs = 400;
constexpr int64_t kBmiTimeoutMs = 30000;
constexpr int kBmiMotionThresholdPx = 3;
constexpr int kFactoryEntryHoldMs = 3000;
constexpr int kTestsPerPage = 6;
constexpr size_t kMaxDetailChars = 48;

struct FactoryDisplayStep {
    const char* name;
    uint32_t bg_color;
    uint32_t text_color;
    uint8_t backlight;
};

constexpr FactoryDisplayStep kDisplaySteps[] = {
    {"红色", 0xFF0000, 0xFFFFFF, 100},
    {"绿色", 0x00FF00, 0x000000, 60},
    {"蓝色", 0x0000FF, 0xFFFFFF, 20},
    {"白色", 0xFFFFFF, 0x000000, 100},
    {"黑色", 0x000000, 0xFFFFFF, 100},
};

std::array<factory_test_state_t, FACTORY_TEST_COUNT> g_test_states;
std::array<std::string, FACTORY_TEST_COUNT> g_test_details;
factory_required_actions_t g_actions = {};
bool g_auto_started = false;

int64_t NowMs() {
    return esp_timer_get_time() / 1000;
}

const lv_font_t* FactoryTextFont() {
#if defined(CONFIG_BOARD_TYPE_ECHOEAR)
    return &font_puhui_20_4;
#else
    return &lv_font_montserrat_14;
#endif
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
    if (err == ESP_OK && !enabled) {
        err = nvs_set_u8(handle, NVS_KEY_EXIT_PENDING, 1);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ft_en write failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "ft_en=%d", enabled ? 1 : 0);
    return true;
}

bool ConsumeFactoryExitPendingFromNvs() {
    nvs_handle_t handle = 0;
    if (nvs_open(NVS_NS, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }

    uint8_t pending = 0;
    const esp_err_t get_err = nvs_get_u8(handle, NVS_KEY_EXIT_PENDING, &pending);
    if (get_err == ESP_OK) {
        esp_err_t err = nvs_erase_key(handle, NVS_KEY_EXIT_PENDING);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ft_exit clear failed: %s", esp_err_to_name(err));
        }
    }
    nvs_close(handle);

    if (get_err == ESP_OK && pending == 1) {
        ESP_LOGI(TAG, "Skipping factory entry latch once after factory exit");
        return true;
    }
    return false;
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
        return "等待";
    case FACTORY_TEST_STATE_RUNNING:
        return "测试中";
    case FACTORY_TEST_STATE_PASS:
        return "通过";
    case FACTORY_TEST_STATE_FAIL:
        return "失败";
    default:
        return "?";
    }
}

const char* ShortStateText(factory_test_state_t state) {
    switch (state) {
    case FACTORY_TEST_STATE_PENDING:
        return "等";
    case FACTORY_TEST_STATE_RUNNING:
        return "测";
    case FACTORY_TEST_STATE_PASS:
        return "过";
    case FACTORY_TEST_STATE_FAIL:
        return "败";
    default:
        return "?";
    }
}

const char* TestName(factory_test_id_t id) {
    switch (id) {
    case FACTORY_TEST_POWER:
        return "电源";
    case FACTORY_TEST_MEMORY:
        return "内存";
    case FACTORY_TEST_I2C_BUS:
        return "总线";
    case FACTORY_TEST_DISPLAY:
        return "屏幕";
    case FACTORY_TEST_VOLUME:
        return "音量";
    case FACTORY_TEST_BRIGHTNESS:
        return "亮度";
    case FACTORY_TEST_BMI270:
        return "姿态";
    case FACTORY_TEST_CODEC:
        return "音频";
    case FACTORY_TEST_BLUETOOTH:
        return "蓝牙";
    case FACTORY_TEST_WIFI:
        return "网络";
    case FACTORY_TEST_RTC:
        return "时钟";
    case FACTORY_TEST_TOUCH:
        return "触摸";
    case FACTORY_TEST_BATTERY:
        return "电池";
    case FACTORY_TEST_AUDIO:
        return "录放";
    case FACTORY_TEST_SD_CARD:
        return "SD卡";
    default:
        return "?";
    }
}

int ResultPageCount() {
    return (FACTORY_TEST_COUNT + kTestsPerPage - 1) / kTestsPerPage;
}

std::string CompactDetail(const std::string& detail) {
    if (detail.size() <= kMaxDetailChars) {
        return detail;
    }
    return detail.substr(0, kMaxDetailChars - 3) + "...";
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
    if (all_pass && g_actions.touch_triggered && g_actions.volume_changed &&
        g_actions.brightness_changed && g_actions.audio_executed) {
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
    enum class ManualStep {
        kNone,
        kGpioTouch,
        kVolume,
        kBrightness,
        kQrPrompt,
    };

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
        started_ms_ = NowMs();
        SetTestStateWithDetail(FACTORY_TEST_POWER, FACTORY_TEST_STATE_PASS, "已进入");
        SetTestStateWithDetail(FACTORY_TEST_TOUCH, FACTORY_TEST_STATE_PENDING, "触摸键");
        SetTestStateWithDetail(FACTORY_TEST_VOLUME, FACTORY_TEST_STATE_PENDING, "上下滑");
        SetTestStateWithDetail(FACTORY_TEST_BRIGHTNESS, FACTORY_TEST_STATE_PENDING, "左右滑");
        SetTestStateWithDetail(FACTORY_TEST_AUDIO, FACTORY_TEST_STATE_PENDING, "等待");
        StartRtcTest();
        BuildUi();
        StartDisplayTest();

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
        if (!display_) {
            return;
        }

        const int64_t now_ms = NowMs();
        if (rtc_running_) {
            if (now_ms - rtc_started_ms_ >= kRtcWaitMs) {
                rtc_running_ = false;
                struct timeval tv = {};
                gettimeofday(&tv, nullptr);
                const time_t elapsed = tv.tv_sec - rtc_reference_;
                if (elapsed >= 2 && elapsed <= 5) {
                    SetTestStateWithDetail(FACTORY_TEST_RTC, FACTORY_TEST_STATE_PASS, "时钟OK");
                } else {
                    SetTestStateWithDetail(FACTORY_TEST_RTC, FACTORY_TEST_STATE_FAIL, "时钟异常");
                }
                Render();
            }
        }

        if (started_ms_ > 0 &&
            GetTestState(FACTORY_TEST_BMI270) == FACTORY_TEST_STATE_RUNNING &&
            now_ms - started_ms_ >= kBmiTimeoutMs) {
            SetTestStateWithDetail(FACTORY_TEST_BMI270, FACTORY_TEST_STATE_FAIL, "未移动2秒");
            Render();
        }
    }

    void OnTouchDetected() {
        if (display_test_active_) {
            AdvanceDisplayTest();
        } else if (!IsManualPageActive() && !qr_page_visible_) {
            result_page_ = (result_page_ + 1) % ResultPageCount();
            Render();
        } else {
            Render();
        }
    }

    void OnTouchscreenLongPress() {
        if (!AllTestsPassed()) {
            ESP_LOGI(TAG, "Touchscreen long press ignored until all tests pass");
            return;
        }
        manual_step_ = ManualStep::kNone;
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

    void OnGpioTouchDetected() {
        if (manual_step_ != ManualStep::kGpioTouch &&
            GetTestState(FACTORY_TEST_TOUCH) != FACTORY_TEST_STATE_PASS) {
            return;
        }
        MarkTouchTriggered();
        SetTestStateWithDetail(FACTORY_TEST_TOUCH, FACTORY_TEST_STATE_PASS, "触摸OK");
        if (manual_step_ == ManualStep::kGpioTouch) {
            StartVolumeTest();
        }
        Render();
    }

    void OnVolumeSwipe(bool up, int volume) {
        if (manual_step_ != ManualStep::kVolume &&
            GetTestState(FACTORY_TEST_VOLUME) != FACTORY_TEST_STATE_PASS) {
            return;
        }
        if (up) {
            volume_up_seen_ = true;
        } else {
            volume_down_seen_ = true;
        }

        char detail[24];
        if (volume_up_seen_ && volume_down_seen_) {
            MarkVolumeChanged();
            snprintf(detail, sizeof(detail), "上下滑 %d%%", volume);
            SetTestStateWithDetail(FACTORY_TEST_VOLUME, FACTORY_TEST_STATE_PASS, detail);
            if (manual_step_ == ManualStep::kVolume) {
                StartBrightnessTest();
            }
        } else {
            snprintf(detail, sizeof(detail), "上%d 下%d %d%%",
                     volume_up_seen_ ? 1 : 0,
                     volume_down_seen_ ? 1 : 0,
                     volume);
            SetTestStateWithDetail(FACTORY_TEST_VOLUME, FACTORY_TEST_STATE_RUNNING, detail);
        }
        Render();
    }

    void OnBrightnessSwipe(bool left, int brightness) {
        if (manual_step_ != ManualStep::kBrightness &&
            GetTestState(FACTORY_TEST_BRIGHTNESS) != FACTORY_TEST_STATE_PASS) {
            return;
        }
        if (left) {
            brightness_left_seen_ = true;
        } else {
            brightness_right_seen_ = true;
        }

        char detail[24];
        if (brightness_left_seen_ && brightness_right_seen_) {
            MarkBrightnessChanged();
            snprintf(detail, sizeof(detail), "左右滑 %d%%", brightness);
            SetTestStateWithDetail(FACTORY_TEST_BRIGHTNESS, FACTORY_TEST_STATE_PASS, detail);
            if (manual_step_ == ManualStep::kBrightness) {
                StartQrPrompt();
            }
        } else {
            snprintf(detail, sizeof(detail), "左%d 右%d %d%%",
                     brightness_left_seen_ ? 1 : 0,
                     brightness_right_seen_ ? 1 : 0,
                     brightness);
            SetTestStateWithDetail(FACTORY_TEST_BRIGHTNESS, FACTORY_TEST_STATE_RUNNING, detail);
        }
        Render();
    }

    void OnAudioRecordingStarted() {
        SetTestStateWithDetail(FACTORY_TEST_AUDIO, FACTORY_TEST_STATE_RUNNING, "录音");
        Render();
    }

    void OnAudioPlaybackTriggered() {
        SetTestStateWithDetail(FACTORY_TEST_AUDIO, FACTORY_TEST_STATE_RUNNING, "回放");
        Render();
    }

    void OnAudioPlaybackFinished() {
        MarkAudioExecuted();
        SetTestStateWithDetail(FACTORY_TEST_AUDIO, FACTORY_TEST_STATE_PASS, "完成");
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
                    SetTestStateWithDetail(FACTORY_TEST_BMI270, FACTORY_TEST_STATE_PASS, "移动OK");
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
            if (qr_page_visible_ || GetTestState(FACTORY_TEST_BMI270) == FACTORY_TEST_STATE_PASS) {
                lv_obj_add_flag(bmi_dot_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(bmi_dot_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (GetTestState(FACTORY_TEST_BMI270) == FACTORY_TEST_STATE_PASS) {
            Render();
        }
    }

    void OnTestFinished(factory_test_id_t id, bool passed, const char* detail) {
        SetTestStateWithDetail(id,
                               passed ? FACTORY_TEST_STATE_PASS : FACTORY_TEST_STATE_FAIL,
                               detail != nullptr ? detail : (passed ? "通过" : "失败"));
        Render();
    }

    void OnWifiTestFinished(bool passed, const char* detail) {
        SetTestStateWithDetail(FACTORY_TEST_WIFI,
                               passed ? FACTORY_TEST_STATE_PASS : FACTORY_TEST_STATE_FAIL,
                               detail != nullptr ? detail : (passed ? "通过" : "失败"));
        Render();
    }

    void OnBluetoothTestFinished(bool passed, const char* detail) {
        SetTestStateWithDetail(FACTORY_TEST_BLUETOOTH,
                               passed ? FACTORY_TEST_STATE_PASS : FACTORY_TEST_STATE_FAIL,
                               detail != nullptr ? detail : (passed ? "广播" : "失败"));
        Render();
    }

    void OnBatterySample(bool passed, int level) {
        char detail[24];
        if (passed) {
            snprintf(detail, sizeof(detail), "%d%%", level);
        } else {
            snprintf(detail, sizeof(detail), "未检测");
        }
        SetTestStateWithDetail(FACTORY_TEST_BATTERY,
                               passed ? FACTORY_TEST_STATE_PASS : FACTORY_TEST_STATE_FAIL,
                               detail);
        Render();
    }

    void OnSdSample(bool present) {
        SetTestStateWithDetail(FACTORY_TEST_SD_CARD,
                               present ? FACTORY_TEST_STATE_PASS : FACTORY_TEST_STATE_FAIL,
                               present ? "SD通过" : "无SD卡");
        Render();
    }

    bool IsQrPageVisible() const {
        return qr_page_visible_;
    }

    bool IsDisplayTestActive() const {
        return display_test_active_;
    }

    uint8_t GetDisplayTestBacklight() const {
        if (!display_test_active_) {
            return 100;
        }
        const size_t step_count = sizeof(kDisplaySteps) / sizeof(kDisplaySteps[0]);
        const size_t step = std::min(display_test_step_, step_count - 1);
        return kDisplaySteps[step].backlight;
    }

    bool ShouldExitFactoryMode(int64_t held_ms) const {
        return held_ms >= kQrExitHoldMs;
    }

    void Render() {
        if (!display_ || !root_) {
            return;
        }

        DisplayLockGuard lock(display_);

        const bool show_display_test = display_test_active_ || IsManualPageActive();
        if (page_display_) {
            if (show_display_test) {
                lv_obj_clear_flag(page_display_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(page_display_);
                ApplyDisplayTestStep();
            } else {
                lv_obj_add_flag(page_display_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (page_test_) {
            if (qr_page_visible_ || show_display_test) {
                lv_obj_add_flag(page_test_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(page_test_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (page_qr_) {
            if (qr_page_visible_ && !show_display_test) {
                lv_obj_clear_flag(page_qr_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(page_qr_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (bmi_dot_) {
            if (qr_page_visible_ || show_display_test ||
                GetTestState(FACTORY_TEST_BMI270) == FACTORY_TEST_STATE_PASS) {
                lv_obj_add_flag(bmi_dot_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(bmi_dot_, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (identity_label_) {
            std::string identity = "W " + wifi_mac_display_ + "\n";
            identity += "B " + bt_mac_display_ + "  F " + fw_tag_;
            if (!hw_tag_.empty()) {
                identity += "  H " + hw_tag_;
            }
            lv_label_set_text(identity_label_, identity.c_str());
        }

        if (tests_label_) {
            if (result_page_ >= static_cast<size_t>(ResultPageCount())) {
                result_page_ = 0;
            }
            std::string tests;
            const int start = static_cast<int>(result_page_) * kTestsPerPage;
            const int end = std::min(start + kTestsPerPage, static_cast<int>(FACTORY_TEST_COUNT));
            for (int i = start; i < end; ++i) {
                tests += TestName(static_cast<factory_test_id_t>(i));
                tests += " ";
                tests += ShortStateText(g_test_states[i]);
                if (!g_test_details[i].empty()) {
                    tests += " ";
                    tests += CompactDetail(g_test_details[i]);
                }
                if (i + 1 < end) {
                    tests += "\n";
                }
            }
            lv_label_set_text(tests_label_, tests.c_str());
        }

        if (overall_label_) {
            const factory_test_state_t overall = OverallState();
            std::string text = "总结果 ";
            text += StateText(overall);
            text += "  第";
            text += std::to_string(result_page_ + 1);
            text += "/";
            text += std::to_string(ResultPageCount());
            text += "页";
            lv_label_set_text(overall_label_, text.c_str());
            lv_obj_set_style_text_color(
                overall_label_,
                overall == FACTORY_TEST_STATE_PASS ? lv_color_hex(0x24C06A)
                : (overall == FACTORY_TEST_STATE_FAIL ? lv_color_hex(0xFF5B5B) : lv_color_hex(0xF4C542)),
                0);
        }

        if (instruction_label_) {
            const factory_test_state_t overall = OverallState();
            const char* instruction = AllTestsPassed()
                ? "通过：长按屏幕出二维码"
                : (overall == FACTORY_TEST_STATE_FAIL
                    ? "失败：点屏查看页面"
                    : "点屏翻页，说话键录放");
            lv_label_set_text(instruction_label_, instruction);
        }

        if (qr_label_) {
            lv_label_set_text(qr_label_, "扫码注册");
        }
        if (qr_hint_label_) {
            lv_label_set_text(qr_hint_label_, "长按说话键3秒退出");
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
        lv_label_set_text(title, "工厂测试");
        lv_obj_set_style_text_font(title, FactoryTextFont(), 0);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

        identity_label_ = lv_label_create(page_test_);
        lv_obj_set_width(identity_label_, 288);
        lv_label_set_long_mode(identity_label_, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(identity_label_, FactoryTextFont(), 0);
        lv_obj_set_style_text_color(identity_label_, lv_color_hex(0xD0D0D0), 0);
        lv_obj_set_style_text_align(identity_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(identity_label_, LV_ALIGN_TOP_MID, 0, 24);

        tests_label_ = lv_label_create(page_test_);
        lv_obj_set_width(tests_label_, 292);
        lv_label_set_long_mode(tests_label_, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(tests_label_, FactoryTextFont(), 0);
        lv_obj_set_style_text_color(tests_label_, lv_color_white(), 0);
        lv_obj_align(tests_label_, LV_ALIGN_TOP_MID, 0, 72);

        overall_label_ = lv_label_create(page_test_);
        lv_obj_set_style_text_font(overall_label_, FactoryTextFont(), 0);
        lv_obj_align(overall_label_, LV_ALIGN_BOTTOM_MID, 0, -34);

        instruction_label_ = lv_label_create(page_test_);
        lv_obj_set_width(instruction_label_, 292);
        lv_label_set_long_mode(instruction_label_, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(instruction_label_, FactoryTextFont(), 0);
        lv_obj_set_style_text_color(instruction_label_, lv_color_hex(0xB8B8B8), 0);
        lv_obj_set_style_text_align(instruction_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(instruction_label_, LV_ALIGN_BOTTOM_MID, 0, -12);

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
        lv_obj_set_style_text_font(qr_label_, FactoryTextFont(), 0);
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
        lv_obj_set_style_text_font(qr_code_, FactoryTextFont(), 0);
        lv_label_set_text(qr_code_, "二维码未启用");
        lv_obj_set_style_text_color(qr_code_, lv_color_white(), 0);
        lv_obj_align(qr_code_, LV_ALIGN_CENTER, 0, 0);
#endif

        qr_hint_label_ = lv_label_create(page_qr_);
        lv_obj_set_style_text_font(qr_hint_label_, FactoryTextFont(), 0);
        lv_obj_set_style_text_color(qr_hint_label_, lv_color_hex(0xD0D0D0), 0);
        lv_obj_align(qr_hint_label_, LV_ALIGN_BOTTOM_MID, 0, -8);

        page_display_ = lv_obj_create(screen);
        lv_obj_set_size(page_display_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_pad_all(page_display_, 0, 0);
        lv_obj_set_style_border_width(page_display_, 0, 0);
        lv_obj_add_flag(page_display_, LV_OBJ_FLAG_FLOATING);
        lv_obj_add_flag(page_display_, LV_OBJ_FLAG_HIDDEN);

        display_test_label_ = lv_label_create(page_display_);
        lv_obj_set_width(display_test_label_, LV_PCT(90));
        lv_label_set_long_mode(display_test_label_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(display_test_label_, FactoryTextFont(), 0);
        lv_obj_set_style_text_align(display_test_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(display_test_label_, LV_ALIGN_CENTER, 0, 0);
    }

    void StartRtcTest() {
        struct timeval tv = {};
        tv.tv_sec = 1704067200; // 2024-01-01T00:00:00Z
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        rtc_reference_ = tv.tv_sec;
        rtc_started_ms_ = NowMs();
        rtc_running_ = true;
        SetTestStateWithDetail(FACTORY_TEST_RTC, FACTORY_TEST_STATE_RUNNING, "等2秒");
    }

    void StartDisplayTest() {
        display_test_step_ = 0;
        display_test_active_ = true;
        SetTestStateWithDetail(FACTORY_TEST_DISPLAY, FACTORY_TEST_STATE_RUNNING, "点红色");
    }

    void AdvanceDisplayTest() {
        if (!display_test_active_) {
            return;
        }
        const size_t step_count = sizeof(kDisplaySteps) / sizeof(kDisplaySteps[0]);
        if (display_test_step_ + 1 >= step_count) {
            display_test_active_ = false;
            SetTestStateWithDetail(FACTORY_TEST_DISPLAY, FACTORY_TEST_STATE_PASS, "五色OK");
            StartGpioTouchTest();
            return;
        }

        display_test_step_++;
        std::string detail = "点";
        detail += kDisplaySteps[display_test_step_].name;
        SetTestStateWithDetail(FACTORY_TEST_DISPLAY, FACTORY_TEST_STATE_RUNNING, detail);
        Render();
    }

    void ApplyDisplayTestStep() {
        if (!page_display_ || !display_test_label_) {
            return;
        }

        if (IsManualPageActive()) {
            ApplyManualStep();
            return;
        }

        if (!display_test_active_) {
            return;
        }

        const size_t step_count = sizeof(kDisplaySteps) / sizeof(kDisplaySteps[0]);
        const size_t step = std::min(display_test_step_, step_count - 1);
        const auto& cfg = kDisplaySteps[step];

        lv_obj_set_style_bg_color(page_display_, lv_color_hex(cfg.bg_color), 0);
        lv_obj_set_style_bg_opa(page_display_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(display_test_label_, lv_color_hex(cfg.text_color), 0);

        char text[96];
        snprintf(text, sizeof(text), "屏幕 %s\n亮度 %u%%\n纯色正常请点屏",
                 cfg.name, static_cast<unsigned>(cfg.backlight));
        lv_label_set_text(display_test_label_, text);
    }

    bool IsManualPageActive() const {
        return manual_step_ != ManualStep::kNone;
    }

    void SetManualPage(uint32_t bg_color, uint32_t text_color, const char* text) {
        lv_obj_set_style_bg_color(page_display_, lv_color_hex(bg_color), 0);
        lv_obj_set_style_bg_opa(page_display_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(display_test_label_, lv_color_hex(text_color), 0);
        lv_label_set_text(display_test_label_, text);
    }

    void BuildBlockingTestsText() {
        std::string text = "测试未通过";
        int shown = 0;

        for (int i = 0; i < FACTORY_TEST_COUNT && shown < 5; ++i) {
            if (g_test_states[i] != FACTORY_TEST_STATE_FAIL) {
                continue;
            }
            text += "\n";
            text += TestName(static_cast<factory_test_id_t>(i));
            text += " 失败";
            if (!g_test_details[i].empty()) {
                text += " ";
                text += CompactDetail(g_test_details[i]);
            }
            shown++;
        }

        for (int i = 0; i < FACTORY_TEST_COUNT && shown < 5; ++i) {
            const auto id = static_cast<factory_test_id_t>(i);
            if (g_test_states[i] == FACTORY_TEST_STATE_PASS ||
                g_test_states[i] == FACTORY_TEST_STATE_FAIL) {
                continue;
            }
            text += "\n";
            text += TestName(id);
            text += " ";
            text += ShortStateText(g_test_states[i]);
            if (!g_test_details[i].empty()) {
                text += " ";
                text += CompactDetail(g_test_details[i]);
            }
            shown++;
        }

        if (shown == 0) {
            if (!g_actions.touch_triggered) {
                text += "\n触摸 等待";
                shown++;
            }
            if (!g_actions.volume_changed && shown < 5) {
                text += "\n音量 等待";
                shown++;
            }
            if (!g_actions.brightness_changed && shown < 5) {
                text += "\n亮度 等待";
                shown++;
            }
            if (!g_actions.audio_executed && shown < 5) {
                text += "\n录放 等待";
                shown++;
            }
        }

        if (shown == 0) {
            text += "\n长按1秒";
        }

        snprintf(manual_text_, sizeof(manual_text_), "%s", text.c_str());
    }

    void ApplyManualStep() {
        switch (manual_step_) {
        case ManualStep::kGpioTouch:
            SetManualPage(0x101010, 0xFFFFFF, "GPIO7触摸\n按住触摸键\n直到通过");
            break;
        case ManualStep::kVolume:
            snprintf(manual_text_, sizeof(manual_text_),
                     "音量滑动\n上滑 %d/1\n下滑 %d/1",
                     volume_up_seen_ ? 1 : 0,
                     volume_down_seen_ ? 1 : 0);
            SetManualPage(0x001F3F, 0xFFFFFF, manual_text_);
            break;
        case ManualStep::kBrightness:
            snprintf(manual_text_, sizeof(manual_text_),
                     "亮度滑动\n左滑 %d/1\n右滑 %d/1",
                     brightness_left_seen_ ? 1 : 0,
                     brightness_right_seen_ ? 1 : 0);
            SetManualPage(0x3B2500, 0xFFFFFF, manual_text_);
            break;
        case ManualStep::kQrPrompt:
            if (AllTestsPassed()) {
                SetManualPage(0x001800, 0x24C06A, "准备扫码\n长按屏幕1秒\n显示二维码");
            } else {
                BuildBlockingTestsText();
                SetManualPage(0x180000, 0xFFFFFF, manual_text_);
            }
            break;
        case ManualStep::kNone:
        default:
            break;
        }
    }

    void StartGpioTouchTest() {
        manual_step_ = ManualStep::kGpioTouch;
        SetTestStateWithDetail(FACTORY_TEST_TOUCH, FACTORY_TEST_STATE_RUNNING, "按GPIO7");
        Render();
    }

    void StartVolumeTest() {
        manual_step_ = ManualStep::kVolume;
        SetTestStateWithDetail(FACTORY_TEST_VOLUME, FACTORY_TEST_STATE_RUNNING, "上下滑");
        Render();
    }

    void StartBrightnessTest() {
        manual_step_ = ManualStep::kBrightness;
        SetTestStateWithDetail(FACTORY_TEST_BRIGHTNESS, FACTORY_TEST_STATE_RUNNING, "左右滑");
        Render();
    }

    void StartQrPrompt() {
        manual_step_ = ManualStep::kQrPrompt;
        Render();
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
    lv_obj_t* page_display_ = nullptr;
    lv_obj_t* identity_label_ = nullptr;
    lv_obj_t* tests_label_ = nullptr;
    lv_obj_t* overall_label_ = nullptr;
    lv_obj_t* instruction_label_ = nullptr;
    lv_obj_t* bmi_dot_ = nullptr;
    lv_obj_t* qr_label_ = nullptr;
    lv_obj_t* qr_code_ = nullptr;
    lv_obj_t* qr_hint_label_ = nullptr;
    lv_obj_t* display_test_label_ = nullptr;
    bool qr_page_visible_ = false;
    bool display_test_active_ = false;
    ManualStep manual_step_ = ManualStep::kNone;
    size_t display_test_step_ = 0;
    size_t result_page_ = 0;
    bool volume_up_seen_ = false;
    bool volume_down_seen_ = false;
    bool brightness_left_seen_ = false;
    bool brightness_right_seen_ = false;
    char manual_text_[192] = {};
    bool poll_task_started_ = false;
    bool rtc_running_ = false;
    int64_t started_ms_ = 0;
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
    if (gpio == GPIO_NUM_NC) {
        return false;
    }
    if (ConsumeFactoryExitPendingFromNvs()) {
        return false;
    }
    if (ShouldEnterFactoryMode()) {
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
    SetTestStateWithDetail(FACTORY_TEST_POWER, FACTORY_TEST_STATE_PASS, "开机");
    SetTestStateWithDetail(FACTORY_TEST_MEMORY, FACTORY_TEST_STATE_RUNNING, "检查");
    SetTestStateWithDetail(FACTORY_TEST_I2C_BUS, FACTORY_TEST_STATE_RUNNING, "扫描");
    SetTestStateWithDetail(FACTORY_TEST_DISPLAY, FACTORY_TEST_STATE_RUNNING, "点屏");
    SetTestStateWithDetail(FACTORY_TEST_BMI270, FACTORY_TEST_STATE_RUNNING, "移动2秒");
    SetTestStateWithDetail(FACTORY_TEST_BRIGHTNESS, FACTORY_TEST_STATE_PENDING, "左右滑");
    SetTestStateWithDetail(FACTORY_TEST_CODEC, FACTORY_TEST_STATE_RUNNING, "探测");
    SetTestStateWithDetail(FACTORY_TEST_BLUETOOTH, FACTORY_TEST_STATE_RUNNING, "初始化");
    SetTestStateWithDetail(FACTORY_TEST_WIFI, FACTORY_TEST_STATE_RUNNING, "扫描");
    SetTestStateWithDetail(FACTORY_TEST_RTC, FACTORY_TEST_STATE_RUNNING, "等2秒");
    SetTestStateWithDetail(FACTORY_TEST_BATTERY, FACTORY_TEST_STATE_RUNNING, "读取");
    SetTestStateWithDetail(FACTORY_TEST_SD_CARD, FACTORY_TEST_STATE_RUNNING, "检查");
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
    return g_actions.touch_triggered && g_actions.volume_changed &&
           g_actions.brightness_changed && g_actions.audio_executed;
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

void MarkBrightnessChanged() {
    g_actions.brightness_changed = true;
    SetTestState(FACTORY_TEST_BRIGHTNESS, FACTORY_TEST_STATE_PASS);
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

void Controller::OnGpioTouchDetected() {
    Impl().OnGpioTouchDetected();
}

void Controller::OnVolumeSwipe(bool up, int volume) {
    Impl().OnVolumeSwipe(up, volume);
}

void Controller::OnBrightnessSwipe(bool left, int brightness) {
    Impl().OnBrightnessSwipe(left, brightness);
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

void Controller::OnTestFinished(factory_test_id_t id, bool passed, const char* detail) {
    Impl().OnTestFinished(id, passed, detail);
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

bool Controller::IsDisplayTestActive() const {
    return Impl().IsDisplayTestActive();
}

uint8_t Controller::GetDisplayTestBacklight() const {
    return Impl().GetDisplayTestBacklight();
}

bool Controller::ShouldExitFactoryMode(int64_t held_ms) const {
    return Impl().ShouldExitFactoryMode(held_ms);
}

void Controller::Render() {
    Impl().Render();
}
} // namespace FactoryTest
