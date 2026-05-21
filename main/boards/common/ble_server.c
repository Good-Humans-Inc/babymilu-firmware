#include "ble_server.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sdkconfig.h"

static const char *TAG = "BLE-Server";
static const esp_power_level_t kPortableSarBleTxPowerCap = ESP_PWR_LVL_N0;

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
} ble_server_state = {0};

// Buffer for current READ characteristic value
static char ble_read_value[128];
static uint16_t ble_read_len = 0;

// Forward declarations
static void ble_app_advertise(void);
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void ble_app_on_sync(void);
static void host_task(void *param);
static void ble_apply_tx_power_cap(void);
static int ble_device_read(uint16_t con_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int ble_device_write(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static void ble_apply_tx_power_cap(void)
{
    esp_err_t rc = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, kPortableSarBleTxPowerCap);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set BLE default TX power cap: %d", rc);
    }

    rc = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, kPortableSarBleTxPowerCap);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set BLE advertising TX power cap: %d", rc);
    }

    rc = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, kPortableSarBleTxPowerCap);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set BLE scan TX power cap: %d", rc);
    }

    ESP_LOGI(TAG, "Applied BLE TX power cap: 0 dBm");
}

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
    if (ble_read_len > 0) {
        os_mbuf_append(ctxt->om, ble_read_value, ble_read_len);
    } else {
        const char *response = "";
        os_mbuf_append(ctxt->om, response, strlen(response));
    }
    return 0;
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
        ble_server_state.advertising = false;  // Reset advertising state
        if (ble_server_state.connection_callback) {
            ble_server_state.connection_callback(false);
        }
        // Restart advertising after disconnect so device can be found again
        ESP_LOGI(TAG, "Restarting advertising after disconnect");
        ble_app_advertise();
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
        ESP_LOGW(TAG, "Cannot advertise: BLE server not initialized");
        return;
    }

    // Stop any existing advertising first
    if (ble_server_state.advertising) {
        ESP_LOGI(TAG, "Stopping existing advertising before restart");
        ble_gap_adv_stop();
        ble_server_state.advertising = false;
        vTaskDelay(pdMS_TO_TICKS(100));  // Small delay to ensure stop completes
    }

    // GAP - device name definition
    struct ble_hs_adv_fields fields;
    const char *device_name;
    memset(&fields, 0, sizeof(fields));
    device_name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising fields: %d", rc);
        return;
    }

    // GAP - device connectivity definition
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_apply_tx_power_cap();
    
    rc = ble_gap_adv_start(ble_server_state.addr_type, NULL, BLE_HS_FOREVER, 
                          &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        return;
    }
    
    ble_server_state.advertising = true;
    ESP_LOGI(TAG, "BLE advertising started successfully, device name: %s", device_name);
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
    ble_apply_tx_power_cap();
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

    // Store data as current READ characteristic value for polling clients
    if (length > sizeof(ble_read_value)) {
        length = sizeof(ble_read_value);
    }
    memcpy(ble_read_value, data, length);
    ble_read_len = length;
    ESP_LOGI(TAG, "Prepared READ value: %.*s", ble_read_len, ble_read_value);
    return true;
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

    ESP_LOGI(TAG, "Starting BLE server deinitialization");
    
    // Stop advertising first
    if (ble_server_state.advertising) {
        ble_gap_adv_stop();
        ble_server_state.advertising = false;
    }
    
    // Disconnect if connected
    if (ble_server_state.connected) {
        ble_gap_terminate(ble_server_state.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        ble_server_state.connected = false;
    }
    
    // Stop nimBLE port (this is asynchronous)
    nimble_port_stop();
    
    // Wait for nimBLE to fully stop - increased to 5+ seconds for proper cleanup
    ESP_LOGI(TAG, "Waiting for nimBLE to fully stop...");
    vTaskDelay(pdMS_TO_TICKS(2000));  // Wait 2 seconds first
    
    // Additional wait to ensure all BLE resources are released
    ESP_LOGI(TAG, "Additional cleanup wait...");
    vTaskDelay(pdMS_TO_TICKS(3000));  // Wait another 3 seconds (total 5+ seconds)
    
    // Deinitialize nimBLE port
    nimble_port_deinit();
    
    // Additional cleanup
    esp_nimble_hci_deinit();
    
    // Final wait to ensure all resources are released
    ESP_LOGI(TAG, "Final cleanup wait...");
    vTaskDelay(pdMS_TO_TICKS(1000));  // Additional 1 second (total 6+ seconds)
    
    memset(&ble_server_state, 0, sizeof(ble_server_state));
    ESP_LOGI(TAG, "BLE Server deinitialized");
}
