#pragma once

#include <string.h>

#include <esp_ota_ops.h>

// "Factory test" mode is enabled when running from the flash partition labeled "factory".
// This matches the project's partition table and the existing OTA behavior.
inline bool IsFactoryTestMode() {
    const esp_partition_t* partition = esp_ota_get_running_partition();
    return partition != nullptr && strcmp(partition->label, "factory") == 0;
}

