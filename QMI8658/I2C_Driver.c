#include "I2C_Driver.h"
#include <driver/i2c_master.h>
#include <esp_log.h>
#include "../main/boards/esp32-s3-touch-lcd-1.85/config.h"

static const char *TAG = "I2C_Driver";
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t qmi8658_device_handle = NULL;

esp_err_t I2C_Init(i2c_master_bus_handle_t external_bus_handle)
{
    esp_err_t ret = ESP_OK;
    
    if (external_bus_handle != NULL) {
        // Use the external I2C bus handle
        i2c_bus_handle = external_bus_handle;
        ESP_LOGI(TAG, "Using external I2C bus handle");
    } else {
        // Initialize our own I2C master bus
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = I2C_SDA_IO,
            .scl_io_num = I2C_SCL_IO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        
        ret = i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize I2C master bus: %s", esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "I2C master bus initialized successfully");
    }
    
    return ESP_OK;
}

esp_err_t I2C_Write(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, uint8_t len)
{
    esp_err_t ret = ESP_OK;
    
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Create device handle if not exists
    if (qmi8658_device_handle == NULL) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = device_addr,
            .scl_speed_hz = 100000,
        };
        
        ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &qmi8658_device_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    // Prepare write buffer with register address and data
    uint8_t write_buffer[len + 1];
    write_buffer[0] = reg_addr;
    for (int i = 0; i < len; i++) {
        write_buffer[i + 1] = data[i];
    }
    
    ret = i2c_master_transmit(qmi8658_device_handle, write_buffer, len + 1, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t I2C_Read(uint8_t device_addr, uint8_t reg_addr, uint8_t *data, uint8_t len)
{
    esp_err_t ret = ESP_OK;
    
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Create device handle if not exists
    if (qmi8658_device_handle == NULL) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = device_addr,
            .scl_speed_hz = 100000,
        };
        
        ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &qmi8658_device_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    // Write register address first
    ret = i2c_master_transmit(qmi8658_device_handle, &reg_addr, 1, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C register address write failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Read data
    ret = i2c_master_receive(qmi8658_device_handle, data, len, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
    }
    
    return ret;
}
