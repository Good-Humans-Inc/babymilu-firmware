#pragma once

#include "audio_codec.h"

#include <functional>
#include <vector>

class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    virtual void Initialize(AudioCodec* codec) = 0;
    virtual void Feed(const std::vector<int16_t>& data) = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsRunning() = 0;
    virtual void OnOutput(std::function<void(const int16_t* data, size_t samples)> callback) = 0;
    virtual void OnVadStateChange(std::function<void(bool speaking)> callback) = 0;
    virtual size_t GetFeedSize() = 0;
    virtual void EnableDeviceAec(bool enable) = 0;
};
