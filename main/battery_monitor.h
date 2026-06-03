#pragma once

#include <cstdint>
#include <functional>

#include <driver/i2c_master.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

struct BatteryTelemetry {
    uint16_t voltage_mv = 0;
    int16_t current_ma = 0;
    int level_percent = 0;
    bool charging = false;
    bool discharging = false;
};

class ChargeGauge;

class BatteryMonitor {
public:
    ~BatteryMonitor();

    esp_err_t Init(i2c_master_bus_handle_t i2c_bus);
    void Start(std::function<void(const BatteryTelemetry&)> policy_callback);
    bool Read(BatteryTelemetry& telemetry);
    bool available() const { return gauge_ != nullptr; }

private:
    static void TaskEntry(void* arg);
    void TaskLoop();

    ChargeGauge* gauge_ = nullptr;
    std::function<void(const BatteryTelemetry&)> policy_callback_;
    TaskHandle_t task_ = nullptr;
};
