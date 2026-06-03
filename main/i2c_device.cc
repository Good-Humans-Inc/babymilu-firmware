#include "i2c_device.h"

#include <esp_check.h>

#define TAG "I2cDevice"

I2cDevice::I2cDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr) {
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = 400 * 1000;
    cfg.scl_wait_us = 0;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &cfg, &device_));
}

void I2cDevice::WriteReg(uint8_t reg, uint8_t value) {
    uint8_t buffer[2] = {reg, value};
    ESP_ERROR_CHECK(i2c_master_transmit(device_, buffer, sizeof(buffer), 100));
}

esp_err_t I2cDevice::TryReadRegs(uint8_t reg, uint8_t* buffer, size_t length) {
    if (buffer == nullptr || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(device_, &reg, 1, buffer, length, 100);
}
