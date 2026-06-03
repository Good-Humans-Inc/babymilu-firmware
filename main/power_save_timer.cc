#include "power_save_timer.h"

#include <esp_log.h>

#define TAG "PowerSaveTimer"

PowerSaveTimer::PowerSaveTimer(int seconds_to_sleep) : seconds_to_sleep_(seconds_to_sleep) {
    esp_timer_create_args_t args = {};
    args.callback = TimerCallback;
    args.arg = this;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "power_save_timer";
    args.skip_unhandled_events = true;
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer_));
}

PowerSaveTimer::~PowerSaveTimer() {
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
}

void PowerSaveTimer::SetEnabled(bool enabled) {
    if (enabled && !enabled_) {
        ticks_ = 0;
        enabled_ = true;
        ESP_ERROR_CHECK(esp_timer_start_periodic(timer_, 1000000));
        ESP_LOGI(TAG, "Power save timer enabled");
    } else if (!enabled && enabled_) {
        ESP_ERROR_CHECK(esp_timer_stop(timer_));
        enabled_ = false;
        WakeUp();
        ESP_LOGI(TAG, "Power save timer disabled");
    }
}

void PowerSaveTimer::SetCanSleepCallback(std::function<bool()> callback) {
    can_sleep_callback_ = std::move(callback);
}

void PowerSaveTimer::OnEnterSleepMode(std::function<void()> callback) {
    on_enter_sleep_mode_ = std::move(callback);
}

void PowerSaveTimer::OnExitSleepMode(std::function<void()> callback) {
    on_exit_sleep_mode_ = std::move(callback);
}

bool PowerSaveTimer::WakeUp() {
    ticks_ = 0;
    if (!in_sleep_mode_) {
        return false;
    }
    in_sleep_mode_ = false;
    if (on_exit_sleep_mode_) {
        on_exit_sleep_mode_();
    }
    return true;
}

void PowerSaveTimer::TimerCallback(void* arg) {
    static_cast<PowerSaveTimer*>(arg)->Check();
}

void PowerSaveTimer::Check() {
    if (!in_sleep_mode_ && can_sleep_callback_ && !can_sleep_callback_()) {
        ticks_ = 0;
        return;
    }

    ++ticks_;
    if (seconds_to_sleep_ != -1 && ticks_ >= seconds_to_sleep_ && !in_sleep_mode_) {
        in_sleep_mode_ = true;
        if (on_enter_sleep_mode_) {
            on_enter_sleep_mode_();
        }
    }
}
