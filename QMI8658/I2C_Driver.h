#pragma once

#include <stdint.h>
#include <esp_err.h>
#include <driver/i2c_master.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2C driver with external bus handle
 * @param bus_handle External I2C bus handle (can be NULL to create new bus)
 * @return ESP_OK on success
 */
esp_err_t I2C_Init(i2c_master_bus_handle_t bus_handle);

/**
 * @brief Write data to I2C device
 * @param device_addr I2C device address
 * @param reg_addr Register address to write to
 * @param data Data to write
 * @param len Length of data
 * @return ESP_OK on success
 */
esp_err_t I2C_Write(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, uint8_t len);

/**
 * @brief Read data from I2C device
 * @param device_addr I2C device address
 * @param reg_addr Register address to read from
 * @param data Buffer to store read data
 * @param len Length of data to read
 * @return ESP_OK on success
 */
esp_err_t I2C_Read(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif
