#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

class Cst816sDevice;

class EchoEarTouchController {
public:
    ~EchoEarTouchController();

    struct Callbacks {
        std::function<void(const char* emotion, uint32_t duration_ms)> on_gpio7_emotion;
        std::function<void()> on_activity;
        std::function<void()> on_status_request;
        std::function<int(int delta)> adjust_volume;
        std::function<int(int delta)> adjust_brightness;
    };

    esp_err_t Init(i2c_master_bus_handle_t i2c_bus, Callbacks callbacks);
    void SetInteractionState(bool audio_busy, bool sleep_active);

private:
    struct GpioTouchEvent;

    esp_err_t InitGpio7Touch();
    esp_err_t InitCst816sTouch(i2c_master_bus_handle_t i2c_bus);

    static void Gpio7SensorTaskEntry(void* arg);
    static void Gpio7AppTaskEntry(void* arg);
    static void CstTouchTaskEntry(void* arg);
    static void CstIsrHandler(void* arg);
    static void Gpio7ButtonCallback(void* handle, uint32_t channel, int state, void* arg);

    void Gpio7SensorTask();
    void Gpio7AppTask();
    void CstTouchTask();
    void QueueGpio7Event(uint32_t channel, int state);

    Callbacks callbacks_;
    Cst816sDevice* cst_ = nullptr;
    void* gpio7_touch_handle_ = nullptr;
    QueueHandle_t gpio7_app_queue_ = nullptr;
    QueueHandle_t cst_event_queue_ = nullptr;
    TaskHandle_t gpio7_sensor_task_ = nullptr;
    TaskHandle_t gpio7_app_task_ = nullptr;
    TaskHandle_t cst_task_ = nullptr;
    std::atomic<bool> audio_busy_{false};
    std::atomic<bool> sleep_active_{false};
};
