#include "system_info.h"

#include <freertos/task.h>
#include <esp_log.h>
#include <esp_flash.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_partition.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <cstdlib>
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_wifi_remote.h"
#endif

#define TAG "SystemInfo"

namespace {

char TaskStateToChar(eTaskState state) {
    switch (state) {
    case eRunning:
        return 'R';
    case eReady:
        return 'Y';
    case eBlocked:
        return 'B';
    case eSuspended:
        return 'S';
    case eDeleted:
        return 'D';
    default:
        return '?';
    }
}

const char* StackMemoryType(const void* stack_base) {
    if (stack_base == nullptr) {
        return "null";
    }
    if (esp_ptr_external_ram(stack_base)) {
        return "psram";
    }
    if (esp_ptr_internal(stack_base)) {
        return "sram";
    }
    return "other";
}

} // namespace

size_t SystemInfo::GetFlashSize() {
    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get flash size");
        return 0;
    }
    return (size_t)flash_size;
}

size_t SystemInfo::GetMinimumFreeHeapSize() {
    return esp_get_minimum_free_heap_size();
}

size_t SystemInfo::GetFreeHeapSize() {
    return esp_get_free_heap_size();
}

std::string SystemInfo::GetMacAddress() {
    uint8_t mac[6];
#if CONFIG_IDF_TARGET_ESP32P4
    esp_wifi_get_mac(WIFI_IF_STA, mac);
#else
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
#endif
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(mac_str);
}

std::string SystemInfo::GetChipModelName() {
    return std::string(CONFIG_IDF_TARGET);
}

esp_err_t SystemInfo::PrintTaskCpuUsage(TickType_t xTicksToWait) {
    #define ARRAY_SIZE_OFFSET 5
    TaskStatus_t *start_array = NULL, *end_array = NULL;
    UBaseType_t start_array_size, end_array_size;
    configRUN_TIME_COUNTER_TYPE start_run_time, end_run_time;
    esp_err_t ret;
    uint32_t total_elapsed_time;

    //Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    start_array = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * start_array_size);
    if (start_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    vTaskDelay(xTicksToWait);

    //Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    end_array = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * end_array_size);
    if (end_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    //Calculate total_elapsed_time in units of run time stats clock period.
    total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        ret = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    printf("| Task | Run Time | Percentage\n");
    //Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {
        int k = -1;
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {
                k = j;
                //Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
        //Check if matching task found
        if (k >= 0) {
            uint32_t task_elapsed_time = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
            uint32_t percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * CONFIG_FREERTOS_NUMBER_OF_CORES);
            printf("| %-16s | %8lu | %4lu%%\n", start_array[i].pcTaskName, task_elapsed_time, percentage_time);
        }
    }

    //Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            printf("| %s | Deleted\n", start_array[i].pcTaskName);
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            printf("| %s | Created\n", end_array[i].pcTaskName);
        }
    }
    ret = ESP_OK;

exit:    //Common return path
    free(start_array);
    free(end_array);
    return ret;
}

void SystemInfo::PrintTaskList() {
    char buffer[500];
    vTaskList(buffer);
    ESP_LOGI(TAG, "Task list: \n%s", buffer);
}

void SystemInfo::PrintHeapStats() {
    int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "free sram: %u minimal sram: %u", free_sram, min_free_sram);
}

void SystemInfo::PrintMemorySnapshot(const char* label) {
    const char* snapshot = label != nullptr ? label : "snapshot";

    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    const size_t internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    const size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t psram_min = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    const size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    ESP_LOGI(TAG,
             "[MEM:%s] internal total=%u free=%u min=%u largest=%u; psram total=%u free=%u min=%u largest=%u",
             snapshot,
             (unsigned)internal_total,
             (unsigned)internal_free,
             (unsigned)internal_min,
             (unsigned)internal_largest,
             (unsigned)psram_total,
             (unsigned)psram_free,
             (unsigned)psram_min,
             (unsigned)psram_largest);

    ESP_LOGI(TAG, "[MEM:%s] heap map: MALLOC_CAP_INTERNAL", snapshot);
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    if (psram_total > 0) {
        ESP_LOGI(TAG, "[MEM:%s] heap map: MALLOC_CAP_SPIRAM", snapshot);
        heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
    }

#if configUSE_TRACE_FACILITY
    UBaseType_t task_count = uxTaskGetNumberOfTasks() + 5;
    TaskStatus_t* tasks = static_cast<TaskStatus_t*>(
        heap_caps_malloc(sizeof(TaskStatus_t) * task_count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (tasks == nullptr) {
        tasks = static_cast<TaskStatus_t*>(malloc(sizeof(TaskStatus_t) * task_count));
    }
    if (tasks == nullptr) {
        ESP_LOGW(TAG, "[MEM:%s] task snapshot skipped: no memory for %u TaskStatus_t entries",
                 snapshot, (unsigned)task_count);
        return;
    }

    configRUN_TIME_COUNTER_TYPE total_run_time = 0;
    UBaseType_t actual_task_count = uxTaskGetSystemState(tasks, task_count, &total_run_time);
    if (actual_task_count == 0) {
        ESP_LOGW(TAG, "[MEM:%s] task snapshot skipped: task array too small (%u entries)",
                 snapshot, (unsigned)task_count);
        free(tasks);
        return;
    }

    ESP_LOGI(TAG,
             "[MEM:%s] task stacks: count=%u; hwm is minimum unused stack since task creation",
             snapshot,
             (unsigned)actual_task_count);
    for (UBaseType_t i = 0; i < actual_task_count; ++i) {
        const uint32_t hwm_bytes = static_cast<uint32_t>(tasks[i].usStackHighWaterMark);
#if configTASKLIST_INCLUDE_COREID
        const int core = tasks[i].xCoreID == tskNO_AFFINITY ? -1 : static_cast<int>(tasks[i].xCoreID);
#else
        const int core = -2;
#endif
        ESP_LOGI(TAG,
                 "[MEM:%s] task %-16s state=%c prio=%u core=%d stack_base=%p stack_mem=%s hwm=%u bytes",
                 snapshot,
                 tasks[i].pcTaskName,
                 TaskStateToChar(tasks[i].eCurrentState),
                 (unsigned)tasks[i].uxCurrentPriority,
                 core,
                 tasks[i].pxStackBase,
                 StackMemoryType(tasks[i].pxStackBase),
                 (unsigned)hwm_bytes);
    }
    free(tasks);
#else
    ESP_LOGW(TAG, "[MEM:%s] task snapshot skipped: configUSE_TRACE_FACILITY is disabled", snapshot);
#endif
}
