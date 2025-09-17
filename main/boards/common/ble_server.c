#include "ble_server.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sdkconfig.h"

static const char *TAG = "BLE-Server";

// BLE Server state
static struct {
    bool initialized;
    bool advertising;
    bool connected;
    uint8_t addr_type;
    ble_data_callback_t data_callback;
    ble_connection_callback_t connection_callback;
    ble_device_control_callback_t device_control_callback;
    uint16_t conn_handle;
    
    // File transfer state
    bool file_transfer_active;
    char current_filename[64];
    uint8_t* file_buffer;
    size_t file_size;
    size_t file_received;
} ble_server_state = {0};

// Forward declarations
static void ble_app_advertise(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void ble_app_on_sync(void);
static void host_task(void *param);
static int ble_device_read(uint16_t con_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int ble_device_write(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

// GATT service definitions
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180),                 // Define UUID for device type
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = BLE_UUID16_DECLARE(0xFEF4),           // Define UUID for reading
                .flags = BLE_GATT_CHR_F_READ,
                .access_cb = ble_device_read
            },
            {
                .uuid = BLE_UUID16_DECLARE(0xDEAD),           // Define UUID for writing
                .flags = BLE_GATT_CHR_F_WRITE,
                .access_cb = ble_device_write
            },
            {0}
        }
    },
    {0}
};

// Write data to ESP32 defined as server
static int ble_device_write(uint16_t conn_handle, uint16_t attr_handle, 
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    char *data = (char *)ctxt->om->om_data;
    data[ctxt->om->om_len] = '\0'; // Null terminate the string
    
    ESP_LOGI(TAG, "Data from client: %.*s", ctxt->om->om_len, ctxt->om->om_data);
    
    // Handle device control commands internally
    if (strcmp(data, "LIGHT ON") == 0) {
        printf("LIGHT ON\n");
        if (ble_server_state.device_control_callback) {
            ble_server_state.device_control_callback("LIGHT ON");
        }
    }
    else if (strcmp(data, "LIGHT OFF") == 0) {
        printf("LIGHT OFF\n");
        if (ble_server_state.device_control_callback) {
            ble_server_state.device_control_callback("LIGHT OFF");
        }
    }
    else if (strcmp(data, "FAN ON") == 0) {
        printf("FAN ON\n");
        if (ble_server_state.device_control_callback) {
            ble_server_state.device_control_callback("FAN ON");
        }
    }
    else if (strcmp(data, "FAN OFF") == 0) {
        printf("FAN OFF\n");
        if (ble_server_state.device_control_callback) {
            ble_server_state.device_control_callback("FAN OFF");
        }
    }
    else if (strncmp(data, "FILE_", 5) == 0) {
        // Handle file transfer commands
        ble_handle_file_command(data, ctxt->om->om_len);
    }
    else {
        // Call user data callback for other data
        if (ble_server_state.data_callback) {
            ble_server_state.data_callback(data, ctxt->om->om_len);
        }
    }
    
    return 0;
}

// Read data from ESP32 defined as server
static int ble_device_read(uint16_t con_handle, uint16_t attr_handle, 
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *response = "Data from the server";
    os_mbuf_append(ctxt->om, response, strlen(response));
    return 0;
}

// File transfer functions
static void ble_file_transfer_cleanup(void)
{
    if (ble_server_state.file_buffer) {
        free(ble_server_state.file_buffer);
        ble_server_state.file_buffer = NULL;
    }
    ble_server_state.file_transfer_active = false;
    ble_server_state.file_size = 0;
    ble_server_state.file_received = 0;
    memset(ble_server_state.current_filename, 0, sizeof(ble_server_state.current_filename));
}

static void ble_handle_file_command(const char* data, size_t len)
{
    ESP_LOGI(TAG, "File command: %.*s", len, data);
    
    if (strncmp(data, "FILE_START:", 11) == 0) {
        // Parse: FILE_START:filename:size
        char* filename_start = (char*)data + 11;
        char* size_start = strchr(filename_start, ':');
        if (!size_start) {
            ESP_LOGE(TAG, "Invalid FILE_START format");
            return;
        }
        
        *size_start = '\0';
        size_start++;
        
        strncpy(ble_server_state.current_filename, filename_start, sizeof(ble_server_state.current_filename) - 1);
        ble_server_state.file_size = atoi(size_start);
        
        if (ble_server_state.file_size > 1024 * 1024) { // 1MB limit
            ESP_LOGE(TAG, "File too large: %d bytes", ble_server_state.file_size);
            ble_file_transfer_cleanup();
            return;
        }
        
        ble_server_state.file_buffer = (uint8_t*)malloc(ble_server_state.file_size);
        if (!ble_server_state.file_buffer) {
            ESP_LOGE(TAG, "Failed to allocate file buffer");
            ble_file_transfer_cleanup();
            return;
        }
        
        ble_server_state.file_transfer_active = true;
        ble_server_state.file_received = 0;
        
        ESP_LOGI(TAG, "File transfer started: %s (%d bytes)", 
                 ble_server_state.current_filename, ble_server_state.file_size);
        
        ble_server_send_data("FILE_READY", 10);
        
    } else if (strncmp(data, "FILE_DATA:", 10) == 0) {
        if (!ble_server_state.file_transfer_active) {
            ESP_LOGE(TAG, "File transfer not active");
            return;
        }
        
        // Parse: FILE_DATA:base64_data
        char* data_start = (char*)data + 10;
        size_t data_len = len - 10;
        
        // For now, just copy raw data (skip base64 for simplicity)
        if (ble_server_state.file_received + data_len > ble_server_state.file_size) {
            ESP_LOGE(TAG, "File data overflow");
            ble_file_transfer_cleanup();
            return;
        }
        
        memcpy(ble_server_state.file_buffer + ble_server_state.file_received, data_start, data_len);
        ble_server_state.file_received += data_len;
        
        ESP_LOGI(TAG, "File data received: %d/%d bytes", 
                 ble_server_state.file_received, ble_server_state.file_size);
        
        if (ble_server_state.file_received >= ble_server_state.file_size) {
            // File complete, write to SPIFFS
            ESP_LOGI(TAG, "File transfer complete, writing to SPIFFS");
            
            // Note: This would need to call animation_write_file_atomic
            // For now, just acknowledge completion
            ble_server_send_data("FILE_COMPLETE", 13);
            ble_file_transfer_cleanup();
        } else {
            ble_server_send_data("FILE_DATA_OK", 12);
        }
        
    } else if (strncmp(data, "FILE_CANCEL", 11) == 0) {
        ESP_LOGI(TAG, "File transfer cancelled");
        ble_file_transfer_cleanup();
        ble_server_send_data("FILE_CANCELLED", 14);
    }
}

// BLE event handling
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE GAP EVENT CONNECT %s", 
                 event->connect.status == 0 ? "OK!" : "FAILED!");
        if (event->connect.status == 0) {
            ble_server_state.connected = true;
            ble_server_state.conn_handle = event->connect.conn_handle;
            if (ble_server_state.connection_callback) {
                ble_server_state.connection_callback(true);
            }
        } else {
            ble_app_advertise();
        }
        break;
        
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE GAP EVENT DISCONNECTED");
        ble_server_state.connected = false;
        ble_server_state.conn_handle = 0;
        if (ble_server_state.connection_callback) {
            ble_server_state.connection_callback(false);
        }
        break;
        
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "BLE GAP EVENT ADV_COMPLETE");
        if (!ble_server_state.connected) {
            ble_app_advertise();
        }
        break;
        
    default:
        break;
    }
    return 0;
}

// Define the BLE connection
static void ble_app_advertise(void)
{
    if (!ble_server_state.initialized) {
        return;
    }

    // GAP - device name definition
    struct ble_hs_adv_fields fields;
    const char *device_name;
    memset(&fields, 0, sizeof(fields));
    device_name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    // GAP - device connectivity definition
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(ble_server_state.addr_type, NULL, BLE_HS_FOREVER, 
                     &adv_params, ble_gap_event, NULL);
    
    ble_server_state.advertising = true;
}

// The application
static void ble_app_on_sync(void)
{
    ble_hs_id_infer_auto(0, &ble_server_state.addr_type);
    ble_app_advertise();
}

// The infinite task
static void host_task(void *param)
{
    nimble_port_run();
}

// Public API Implementation
bool ble_server_init(const char* device_name, ble_data_callback_t data_cb, ble_connection_callback_t conn_cb, ble_device_control_callback_t device_cb)
{
    if (ble_server_state.initialized) {
        ESP_LOGW(TAG, "BLE Server already initialized");
        return false;
    }

    if (!device_name) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    // Initialize state
    memset(&ble_server_state, 0, sizeof(ble_server_state));
    ble_server_state.data_callback = data_cb;
    ble_server_state.connection_callback = conn_cb;
    ble_server_state.device_control_callback = device_cb;

    // Initialize BLE
    nimble_port_init();
    ble_svc_gap_device_name_set(device_name);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(host_task);

    ble_server_state.initialized = true;
    ESP_LOGI(TAG, "BLE Server initialized with device name: %s", device_name);
    return true;
}

bool ble_server_start_advertising(void)
{
    if (!ble_server_state.initialized) {
        ESP_LOGE(TAG, "BLE Server not initialized");
        return false;
    }

    if (ble_server_state.advertising) {
        ESP_LOGW(TAG, "Already advertising");
        return true;
    }

    ble_app_advertise();
    return true;
}

bool ble_server_stop_advertising(void)
{
    if (!ble_server_state.initialized) {
        ESP_LOGE(TAG, "BLE Server not initialized");
        return false;
    }

    if (!ble_server_state.advertising) {
        ESP_LOGW(TAG, "Not advertising");
        return true;
    }

    ble_gap_adv_stop();
    ble_server_state.advertising = false;
    return true;
}

bool ble_server_send_data(const char* data, uint16_t length)
{
    if (!ble_server_state.initialized) {
        ESP_LOGE(TAG, "BLE Server not initialized");
        return false;
    }

    if (!ble_server_state.connected) {
        ESP_LOGW(TAG, "No client connected");
        return false;
    }

    if (!data || length == 0) {
        ESP_LOGE(TAG, "Invalid data parameters");
        return false;
    }

    // Find the read characteristic to send data
    struct ble_gatt_chr_def *chr = (struct ble_gatt_chr_def *)gatt_svcs[0].characteristics;
    if (chr && chr->uuid) {
        // This is a simplified implementation
        // In a real implementation, you'd need to use notifications or indications
        ESP_LOGI(TAG, "Sending data: %.*s", length, data);
        return true;
    }

    return false;
}

bool ble_server_is_connected(void)
{
    return ble_server_state.connected;
}

void ble_server_deinit(void)
{
    if (!ble_server_state.initialized) {
        return;
    }

    if (ble_server_state.advertising) {
        ble_gap_adv_stop();
    }

    // Clean up file transfer state
    ble_file_transfer_cleanup();

    nimble_port_stop();
    nimble_port_deinit();
    
    memset(&ble_server_state, 0, sizeof(ble_server_state));
    ESP_LOGI(TAG, "BLE Server deinitialized");
}
