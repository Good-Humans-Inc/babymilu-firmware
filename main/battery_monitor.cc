#include "battery_monitor.h"

#include "i2c_device.h"

#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>

#define TAG "BatteryMonitor"

namespace {

constexpr uint8_t kChargeGaugeAddr = 0x55;
constexpr uint16_t kMinVoltageMv = 3000;
constexpr uint16_t kMaxVoltageMv = 4200;
constexpr int64_t kDetailedLogIntervalMs = 60 * 1000;
constexpr TickType_t kBatteryPollDelay = pdMS_TO_TICKS(5000);

}  // namespace

class ChargeGauge : public I2cDevice {
public:
    ChargeGauge(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {}

    bool ReadVoltage(uint16_t& voltage_mv) {
        uint8_t data[2] = {};
        if (TryReadRegs(0x08, data, sizeof(data)) != ESP_OK) {
            HandleReadFailure("voltage");
            return false;
        }
        consecutive_read_failures_ = 0;
        voltage_mv = static_cast<uint16_t>((data[1] << 8) | data[0]);
        return voltage_mv != 0;
    }

    bool ReadCurrent(int16_t& current_ma) {
        uint8_t data[2] = {};
        if (TryReadRegs(0x0c, data, sizeof(data)) != ESP_OK) {
            HandleReadFailure("current");
            return false;
        }
        consecutive_read_failures_ = 0;
        current_ma = static_cast<int16_t>((data[1] << 8) | data[0]);
        return true;
    }

    bool available() const { return enabled_; }

private:
    void HandleReadFailure(const char* field) {
        ++consecutive_read_failures_;
        if (consecutive_read_failures_ >= kMaxConsecutiveReadFailures) {
            enabled_ = false;
            ESP_LOGW(TAG, "[BATTERY] Charge IC unresponsive after repeated %s read failures, battery reporting disabled", field);
            return;
        }
        ESP_LOGW(TAG, "[BATTERY] Read %s failed", field);
    }

    static constexpr uint8_t kMaxConsecutiveReadFailures = 3;
    uint8_t consecutive_read_failures_ = 0;
    bool enabled_ = true;
};

namespace {

int VoltageToLevel(uint16_t voltage_mv) {
    const uint16_t clamped = std::min<uint16_t>(std::max<uint16_t>(voltage_mv, kMinVoltageMv), kMaxVoltageMv);
    return ((clamped - kMinVoltageMv) * 100) / (kMaxVoltageMv - kMinVoltageMv);
}

}  // namespace

BatteryMonitor::~BatteryMonitor() {
    delete gauge_;
}

esp_err_t BatteryMonitor::Init(i2c_master_bus_handle_t i2c_bus) {
    if (i2c_bus == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t probe = i2c_master_probe(i2c_bus, kChargeGaugeAddr, 100);
    if (probe != ESP_OK) {
        ESP_LOGW(TAG, "[BATTERY] Charge IC not detected at 0x%02x: %s", kChargeGaugeAddr, esp_err_to_name(probe));
        return ESP_OK;
    }

    gauge_ = new ChargeGauge(i2c_bus, kChargeGaugeAddr);
    BatteryTelemetry telemetry;
    if (!Read(telemetry)) {
        ESP_LOGW(TAG, "[BATTERY] Charge IC probe passed but telemetry read failed; battery reporting disabled");
        delete gauge_;
        gauge_ = nullptr;
        return ESP_OK;
    }

    ESP_LOGI(TAG, "[BATTERY] Charge IC ready voltage=%u mV current=%d mA",
             static_cast<unsigned>(telemetry.voltage_mv), telemetry.current_ma);
    return ESP_OK;
}

void BatteryMonitor::Start(std::function<void(const BatteryTelemetry&)> policy_callback) {
    policy_callback_ = std::move(policy_callback);
    if (task_ != nullptr || gauge_ == nullptr) {
        return;
    }
    BaseType_t ok = xTaskCreatePinnedToCore(TaskEntry, "battery_monitor", 4096, this, 4, &task_, 0);
    if (ok != pdPASS) {
        task_ = nullptr;
        ESP_LOGW(TAG, "[BATTERY] Battery monitor task create failed");
    }
}

bool BatteryMonitor::Read(BatteryTelemetry& telemetry) {
    if (gauge_ == nullptr || !gauge_->available()) {
        return false;
    }

    uint16_t voltage_mv = 0;
    int16_t current_ma = 0;
    if (!gauge_->ReadVoltage(voltage_mv) || !gauge_->ReadCurrent(current_ma)) {
        return false;
    }

    telemetry.voltage_mv = voltage_mv;
    telemetry.current_ma = current_ma;
    telemetry.charging = current_ma > 50;
    telemetry.discharging = current_ma < -50;
    telemetry.level_percent = VoltageToLevel(voltage_mv);
    return true;
}

void BatteryMonitor::TaskEntry(void* arg) {
    static_cast<BatteryMonitor*>(arg)->TaskLoop();
}

void BatteryMonitor::TaskLoop() {
    int64_t last_log_ms = 0;
    while (true) {
        BatteryTelemetry telemetry;
        if (Read(telemetry)) {
            if (policy_callback_) {
                policy_callback_(telemetry);
            }

            const int64_t now_ms = esp_timer_get_time() / 1000;
            if (last_log_ms == 0 || now_ms - last_log_ms >= kDetailedLogIntervalMs) {
                ESP_LOGE(TAG, "[BATTERY] Voltage: %u mV, Current: %d mA, Level: %d%%, Charging: %s, Discharging: %s",
                         static_cast<unsigned>(telemetry.voltage_mv),
                         telemetry.current_ma,
                         telemetry.level_percent,
                         telemetry.charging ? "yes" : "no",
                         telemetry.discharging ? "yes" : "no");
                last_log_ms = now_ms;
            }
        }
        vTaskDelay(kBatteryPollDelay);
    }
}
