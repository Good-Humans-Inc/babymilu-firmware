#ifndef WIFI_PROVISIONING_PROTOCOL_H
#define WIFI_PROVISIONING_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class WifiProvisioningFrameResult {
    kIncomplete,
    kComplete,
    kInvalid,
};

struct WifiProvisioningMessage {
    std::string protocol;
    std::string attempt_id;
    std::string payload;
};

class WifiProvisioningReassembler {
public:
    static constexpr size_t kMaxFrameBytes = 160;
    static constexpr size_t kMaxLogicalBytes = 384;
    static constexpr size_t kMaxFrames = 4;

    WifiProvisioningFrameResult Push(
        const uint8_t* data,
        size_t length,
        WifiProvisioningMessage* message);
    void Reset();

private:
    std::string protocol_;
    std::string attempt_id_;
    size_t expected_count_ = 0;
    size_t next_index_ = 1;
    std::vector<uint8_t> payload_;
};

bool IsCanonicalProvisioningAttemptId(const std::string& value);

#endif
