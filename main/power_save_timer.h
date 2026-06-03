#pragma once

#include <functional>

#include <esp_timer.h>

class PowerSaveTimer {
public:
    explicit PowerSaveTimer(int seconds_to_sleep = 30);
    ~PowerSaveTimer();

    void SetEnabled(bool enabled);
    void SetCanSleepCallback(std::function<bool()> callback);
    void OnEnterSleepMode(std::function<void()> callback);
    void OnExitSleepMode(std::function<void()> callback);
    bool WakeUp();
    bool IsInSleepMode() const { return in_sleep_mode_; }

private:
    static void TimerCallback(void* arg);
    void Check();

    esp_timer_handle_t timer_ = nullptr;
    bool enabled_ = false;
    bool in_sleep_mode_ = false;
    int ticks_ = 0;
    int seconds_to_sleep_ = 30;
    std::function<bool()> can_sleep_callback_;
    std::function<void()> on_enter_sleep_mode_;
    std::function<void()> on_exit_sleep_mode_;
};
