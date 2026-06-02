#include "startup_media.h"

#include "sd_card.h"

#include <cstdio>
#include <cstdlib>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/event_groups.h>

namespace {

constexpr EventBits_t kPreloadStartedBit = BIT0;
constexpr EventBits_t kPreloadFinishedBit = BIT1;
constexpr EventBits_t kAudioFinishedBit = BIT2;

const char* TAG = "StartupMedia";

EventGroupHandle_t s_events = nullptr;
StartupMedia::Buffer s_startup_gif;
StartupMedia::Buffer s_startup_wav;

EventGroupHandle_t Events()
{
    StartupMedia::Initialize();
    return s_events;
}

uint8_t* AllocateMediaBuffer(size_t size)
{
    uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data == nullptr) {
        data = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
    }
    if (data == nullptr) {
        data = static_cast<uint8_t*>(malloc(size));
    }
    return data;
}

bool LoadFileToBuffer(const char* path, StartupMedia::Buffer& buffer)
{
    if (buffer.data != nullptr && buffer.size > 0) {
        return true;
    }

    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        ESP_LOGW(TAG, "%s not found", path);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        ESP_LOGW(TAG, "Failed to seek %s", path);
        fclose(file);
        return false;
    }

    long file_size = ftell(file);
    if (file_size <= 0) {
        ESP_LOGW(TAG, "Invalid size %ld for %s", file_size, path);
        fclose(file);
        return false;
    }

    rewind(file);
    uint8_t* data = AllocateMediaBuffer(static_cast<size_t>(file_size));
    if (data == nullptr) {
        ESP_LOGW(TAG, "Failed to allocate %ld bytes for %s", file_size, path);
        fclose(file);
        return false;
    }

    const size_t bytes_read = fread(data, 1, static_cast<size_t>(file_size), file);
    fclose(file);
    if (bytes_read != static_cast<size_t>(file_size)) {
        ESP_LOGW(TAG, "Failed to read entire %s (%u/%ld bytes)",
                 path, static_cast<unsigned>(bytes_read), file_size);
        free(data);
        return false;
    }

    buffer.data = data;
    buffer.size = static_cast<size_t>(file_size);
    ESP_LOGI(TAG, "Preloaded %s (%u bytes)", path, static_cast<unsigned>(buffer.size));
    return true;
}

}  // namespace

namespace StartupMedia {

void Initialize()
{
    if (s_events == nullptr) {
        s_events = xEventGroupCreate();
    }
}

esp_err_t PreloadFromSdCard()
{
    EventGroupHandle_t events = Events();
    xEventGroupSetBits(events, kPreloadStartedBit);

    esp_err_t ret = ESP_OK;
    if (!SdCard::IsMounted()) {
        ESP_LOGI(TAG, "Mounting SD card for startup media preload");
        ret = SdCard::Initialize();
        ESP_LOGI(TAG, "SdCard::Initialize() returned: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SD card already mounted for startup media preload");
    }

    if (ret == ESP_OK) {
        LoadFileToBuffer("/sdcard/startup.gif", s_startup_gif);
        LoadFileToBuffer("/sdcard/startup.wav", s_startup_wav);
    }

    xEventGroupSetBits(events, kPreloadFinishedBit);
    return ret;
}

bool HasPreloadStarted()
{
    EventBits_t bits = xEventGroupGetBits(Events());
    return (bits & kPreloadStartedBit) != 0;
}

bool WaitForPreloadFinished(TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(Events(), kPreloadFinishedBit, pdFALSE, pdTRUE, timeout);
    return (bits & kPreloadFinishedBit) != 0;
}

Buffer GetStartupGif()
{
    return s_startup_gif;
}

Buffer GetStartupWav()
{
    return s_startup_wav;
}

void MarkAudioPlaybackStarted()
{
    xEventGroupClearBits(Events(), kAudioFinishedBit);
}

void MarkAudioPlaybackFinished()
{
    xEventGroupSetBits(Events(), kAudioFinishedBit);
}

bool WaitForAudioPlaybackFinished(TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(Events(), kAudioFinishedBit, pdFALSE, pdTRUE, timeout);
    return (bits & kAudioFinishedBit) != 0;
}

}  // namespace StartupMedia
