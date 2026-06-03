#include "ble_wifi_provisioner.h"

#include "system_info.h"

#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <host/ble_hs.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <os/os_mbuf.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#include <algorithm>
#include <cstring>
#include <utility>

#define TAG "BleWifi"

namespace {

constexpr uint16_t kServiceUuid = 0x0180;
constexpr uint16_t kReadCharacteristicUuid = 0xFEF4;
constexpr uint16_t kWriteCharacteristicUuid = 0xDEAD;
constexpr size_t kMaxWriteLength = 192;

constexpr ble_uuid16_t MakeUuid16(uint16_t value) {
    return {{BLE_UUID_TYPE_16}, value};
}

constexpr ble_uuid16_t kServiceUuidDef = MakeUuid16(kServiceUuid);
constexpr ble_uuid16_t kReadCharacteristicUuidDef = MakeUuid16(kReadCharacteristicUuid);
constexpr ble_uuid16_t kWriteCharacteristicUuidDef = MakeUuid16(kWriteCharacteristicUuid);

BleWifiProvisioner* s_instance = nullptr;

std::string TrimBleString(std::string value) {
    while (!value.empty()) {
        char tail = value.back();
        if (tail != '\0' && tail != '\r' && tail != '\n') {
            break;
        }
        value.pop_back();
    }
    return value;
}

const ble_gatt_chr_def kCharacteristics[] = {
    {
        .uuid = &kReadCharacteristicUuidDef.u,
        .access_cb = BleWifiProvisioner::ReadAccess,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &kWriteCharacteristicUuidDef.u,
        .access_cb = BleWifiProvisioner::WriteAccess,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = nullptr,
    },
};

const ble_gatt_svc_def kGattServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kServiceUuidDef.u,
        .includes = nullptr,
        .characteristics = kCharacteristics,
    },
    {
        .type = 0,
    },
};

}  // namespace

bool BleWifiProvisioner::Start(const char* device_name) {
    if (initialized_) {
        ESP_LOGW(TAG, "BLE Wi-Fi provisioner already running");
        return true;
    }
    if (device_name == nullptr || device_name[0] == '\0') {
        ESP_LOGE(TAG, "BLE device name is required");
        return false;
    }
    if (s_instance != nullptr && s_instance != this) {
        ESP_LOGE(TAG, "another BLE Wi-Fi provisioner instance is already active");
        return false;
    }
    s_instance = this;

    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        s_instance = nullptr;
        return false;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(device_name);

    rc = ble_gatts_count_cfg(kGattServices);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        s_instance = nullptr;
        return false;
    }
    rc = ble_gatts_add_svcs(kGattServices);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        s_instance = nullptr;
        return false;
    }

    ble_hs_cfg.sync_cb = OnSync;
    SetStatus("Ready for WiFi configuration");
    initialized_ = true;
    nimble_port_freertos_init(HostTask);
    ESP_LOGI(TAG, "BLE Wi-Fi provisioner started as '%s'", device_name);
    return true;
}

void BleWifiProvisioner::Advertise() {
    if (!initialized_) {
        return;
    }
    if (advertising_) {
        ble_gap_adv_stop();
        advertising_ = false;
    }

    ble_hs_adv_fields fields = {};
    const char* device_name = ble_svc_gap_device_name();
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = reinterpret_cast<const uint8_t*>(device_name);
    fields.name_len = static_cast<uint8_t>(std::min<size_t>(strlen(device_name), UINT8_MAX));
    fields.name_is_complete = 1;
    fields.uuids16 = &kServiceUuidDef;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(addr_type_, nullptr, BLE_HS_FOREVER, &adv_params, GapEvent, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return;
    }
    advertising_ = true;
    ESP_LOGI(TAG, "BLE advertising for Wi-Fi provisioning service=0x%04x", kServiceUuid);
}

int BleWifiProvisioner::GapEvent(ble_gap_event* event, void* arg) {
    (void)arg;
    if (s_instance == nullptr || event == nullptr) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        s_instance->advertising_ = false;
        if (event->connect.status == 0) {
            s_instance->connected_ = true;
            s_instance->conn_handle_ = event->connect.conn_handle;
            s_instance->SetStatus(std::string("MAC:") + system_info::GetMacAddress());
            ESP_LOGI(TAG, "BLE client connected; MAC status ready");
        } else {
            ESP_LOGW(TAG, "BLE connect failed status=%d; restarting advertising", event->connect.status);
            s_instance->Advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE client disconnected");
        s_instance->connected_ = false;
        s_instance->conn_handle_ = 0;
        s_instance->advertising_ = false;
        if (!s_instance->restart_scheduled_) {
            s_instance->Advertise();
        }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_instance->advertising_ = false;
        if (!s_instance->connected_ && !s_instance->restart_scheduled_) {
            s_instance->Advertise();
        }
        break;
    default:
        break;
    }
    return 0;
}

int BleWifiProvisioner::ReadAccess(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt* ctxt, void* arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (s_instance == nullptr || ctxt == nullptr) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    std::string status = s_instance->Status();
    int rc = os_mbuf_append(ctxt->om, status.data(), status.size());
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int BleWifiProvisioner::WriteAccess(uint16_t conn_handle, uint16_t attr_handle, ble_gatt_access_ctxt* ctxt, void* arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (s_instance == nullptr || ctxt == nullptr || ctxt->om == nullptr) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const uint16_t length = OS_MBUF_PKTLEN(ctxt->om);
    if (length == 0 || length > kMaxWriteLength) {
        s_instance->SetStatus("Error: Invalid payload length");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    std::string payload(length, '\0');
    int rc = os_mbuf_copydata(ctxt->om, 0, length, payload.data());
    if (rc != 0) {
        s_instance->SetStatus("Error: Could not read payload");
        return BLE_ATT_ERR_UNLIKELY;
    }

    s_instance->HandleWrite(TrimBleString(std::move(payload)));
    return 0;
}

void BleWifiProvisioner::HandleWrite(std::string payload) {
    if (payload.rfind("ssid:", 0) == 0) {
        pending_ssid_ = payload.substr(5);
        if (pending_ssid_.empty()) {
            SetStatus("Error: Empty SSID");
            return;
        }
        ESP_LOGI(TAG, "BLE SSID received: %s", pending_ssid_.c_str());
        SetStatus("SSID received, send password");
        return;
    }

    if (payload.rfind("pwd:", 0) == 0) {
        if (pending_ssid_.empty()) {
            SetStatus("Error: No SSID received first");
            return;
        }
        std::string password = payload.substr(4);
        ESP_LOGI(TAG, "BLE password received for SSID '%s' length=%u",
                 pending_ssid_.c_str(), static_cast<unsigned>(password.size()));
        HandleCredential(pending_ssid_, password);
        pending_ssid_.clear();
        return;
    }

    if (payload.rfind("wifi:", 0) == 0) {
        std::string credentials = payload.substr(5);
        size_t colon = credentials.find(':');
        if (colon == std::string::npos || colon == 0) {
            SetStatus("Error: Invalid format");
            return;
        }
        std::string ssid = credentials.substr(0, colon);
        std::string password = credentials.substr(colon + 1);
        ESP_LOGI(TAG, "BLE combined Wi-Fi credential received for SSID '%s' length=%u",
                 ssid.c_str(), static_cast<unsigned>(password.size()));
        HandleCredential(ssid, password);
        return;
    }

    ESP_LOGW(TAG, "unsupported BLE Wi-Fi command length=%u", static_cast<unsigned>(payload.size()));
    SetStatus("Error: Invalid format");
}

void BleWifiProvisioner::HandleCredential(const std::string& ssid, const std::string& password) {
    if (ssid.empty()) {
        SetStatus("Error: Empty SSID");
        return;
    }
    if (on_credentials_) {
        on_credentials_(ssid, password);
    }
    SetStatus("Restarting to connect...");
    RestartSoon();
}

void BleWifiProvisioner::SetStatus(std::string status) {
    status_ = std::move(status);
}

void BleWifiProvisioner::RestartSoon() {
    if (restart_scheduled_) {
        return;
    }
    restart_scheduled_ = true;
    BaseType_t ok = xTaskCreate(RestartTask, "ble_wifi_restart", 4096, nullptr, 5, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create BLE Wi-Fi restart task; restarting immediately");
        esp_restart();
    }
}

void BleWifiProvisioner::RestartTask(void* param) {
    (void)param;
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "BLE Wi-Fi credentials saved; restarting for clean Wi-Fi startup");
    esp_restart();
}

void BleWifiProvisioner::HostTask(void* param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BleWifiProvisioner::OnSync() {
    if (s_instance == nullptr) {
        return;
    }
    int rc = ble_hs_id_infer_auto(0, &s_instance->addr_type_);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }
    s_instance->Advertise();
}
