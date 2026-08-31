#include "wifi_provisioning_protocol.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

bool IsHex(char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

bool ParseSmallUnsigned(const std::string& value, size_t* output) {
    if (value.size() != 1 || value[0] < '1' || value[0] > '4') {
        return false;
    }
    *output = static_cast<size_t>(value[0] - '0');
    return true;
}

}  // namespace

bool IsCanonicalProvisioningAttemptId(const std::string& value) {
    if (value.size() != 36) return false;
    for (size_t index = 0; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') return false;
        } else if (!IsHex(value[index])) {
            return false;
        }
    }
    if (value[14] != '4') return false;
    return value[19] == '8' || value[19] == '9' ||
           value[19] == 'a' || value[19] == 'b';
}

void WifiProvisioningReassembler::Reset() {
    protocol_.clear();
    attempt_id_.clear();
    expected_count_ = 0;
    next_index_ = 1;
    payload_.clear();
}

WifiProvisioningFrameResult WifiProvisioningReassembler::Push(
    const uint8_t* data,
    size_t length,
    WifiProvisioningMessage* message) {
    if (data == nullptr || message == nullptr || length == 0 ||
        length > kMaxFrameBytes) {
        Reset();
        return WifiProvisioningFrameResult::kInvalid;
    }

    size_t separators[4] = {};
    size_t separator_count = 0;
    for (size_t index = 0; index < length && separator_count < 4; ++index) {
        if (data[index] == '|') separators[separator_count++] = index;
    }
    if (separator_count != 4) {
        Reset();
        return WifiProvisioningFrameResult::kInvalid;
    }

    const std::string protocol(reinterpret_cast<const char*>(data), separators[0]);
    const std::string attempt_id(
        reinterpret_cast<const char*>(data + separators[0] + 1),
        separators[1] - separators[0] - 1);
    const std::string index_text(
        reinterpret_cast<const char*>(data + separators[1] + 1),
        separators[2] - separators[1] - 1);
    const std::string count_text(
        reinterpret_cast<const char*>(data + separators[2] + 1),
        separators[3] - separators[2] - 1);
    size_t frame_index = 0;
    size_t frame_count = 0;
    if ((protocol != "BM1" && protocol != "BM2") ||
        !IsCanonicalProvisioningAttemptId(attempt_id) ||
        !ParseSmallUnsigned(index_text, &frame_index) ||
        !ParseSmallUnsigned(count_text, &frame_count) ||
        frame_index > frame_count) {
        Reset();
        return WifiProvisioningFrameResult::kInvalid;
    }

    if (frame_index == 1) {
        Reset();
        protocol_ = protocol;
        attempt_id_ = attempt_id;
        expected_count_ = frame_count;
    } else if (protocol != protocol_ || attempt_id != attempt_id_ ||
               frame_count != expected_count_ || frame_index != next_index_) {
        Reset();
        return WifiProvisioningFrameResult::kInvalid;
    }

    const size_t payload_offset = separators[3] + 1;
    const size_t payload_length = length - payload_offset;
    if (payload_length == 0 || payload_.size() + payload_length > kMaxLogicalBytes) {
        Reset();
        return WifiProvisioningFrameResult::kInvalid;
    }
    payload_.insert(payload_.end(), data + payload_offset, data + length);

    if (frame_index < expected_count_) {
        next_index_ = frame_index + 1;
        return WifiProvisioningFrameResult::kIncomplete;
    }

    message->protocol = protocol_;
    message->attempt_id = attempt_id_;
    message->payload.assign(payload_.begin(), payload_.end());
    Reset();
    return WifiProvisioningFrameResult::kComplete;
}
