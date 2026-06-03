#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/i2c_master.h>
#include <esp_err.h>

class I2cDevice {
public:
    I2cDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr);

protected:
    void WriteReg(uint8_t reg, uint8_t value);
    esp_err_t TryReadRegs(uint8_t reg, uint8_t* buffer, size_t length);

private:
    i2c_master_dev_handle_t device_ = nullptr;
};
