#ifndef STARTUP_MEDIA_H
#define STARTUP_MEDIA_H

#include <cstddef>
#include <cstdint>

#include <esp_err.h>
#include <freertos/FreeRTOS.h>

namespace StartupMedia {

struct Buffer {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

void Initialize();
esp_err_t PreloadFromSdCard();
esp_err_t GetSdHealthResult();
bool HasPreloadStarted();
bool WaitForPreloadFinished(TickType_t timeout);
Buffer GetStartupGif();
Buffer GetStartupWav();
void MarkAudioPlaybackStarted();
void MarkAudioPlaybackFinished();
bool WaitForAudioPlaybackFinished(TickType_t timeout);

}  // namespace StartupMedia

#endif  // STARTUP_MEDIA_H
