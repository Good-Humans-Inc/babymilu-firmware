#pragma once

#include "runtime_config.h"

class OtaClient {
public:
    explicit OtaClient(RuntimeConfig& config) : config_(config) {}

    bool Hydrate();
    void MaybeStartFirmwareUpdate();

private:
    RuntimeConfig& config_;
};
