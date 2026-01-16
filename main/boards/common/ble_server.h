#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// BLE Server callback function type for data received from client
typedef void (*ble_data_callback_t)(const char* data, uint16_t length);

// BLE Server callback function type for connection events
typedef void (*ble_connection_callback_t)(bool connected);

// BLE Server callback function type for device control commands
typedef void (*ble_device_control_callback_t)(const char* command);

/**
 * @brief Initialize BLE Server
 * @param device_name Name to advertise as BLE device
 * @param data_cb Callback function for received data
 * @param conn_cb Callback function for connection events (optional, can be NULL)
 * @param device_cb Callback function for device control commands (optional, can be NULL)
 * @return true if initialization successful, false otherwise
 */
bool ble_server_init(const char* device_name, ble_data_callback_t data_cb, ble_connection_callback_t conn_cb, ble_device_control_callback_t device_cb);

/**
 * @brief Start BLE advertising
 * @return true if started successfully, false otherwise
 */
bool ble_server_start_advertising(void);

/**
 * @brief Stop BLE advertising
 * @return true if stopped successfully, false otherwise
 */
bool ble_server_stop_advertising(void);

/**
 * @brief Send data to connected client
 * @param data Data to send
 * @param length Length of data
 * @return true if sent successfully, false otherwise
 */
bool ble_server_send_data(const char* data, uint16_t length);

/**
 * @brief Check if client is connected
 * @return true if connected, false otherwise
 */
bool ble_server_is_connected(void);

/**
 * @brief Deinitialize BLE Server
 */
void ble_server_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_SERVER_H
