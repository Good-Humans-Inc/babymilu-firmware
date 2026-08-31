#include "wifi_provisioning_protocol.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

constexpr const char* kAttempt = "123e4567-e89b-42d3-a456-426614174000";

WifiProvisioningFrameResult Push(
    WifiProvisioningReassembler* parser,
    const std::string& frame,
    WifiProvisioningMessage* message) {
    return parser->Push(
        reinterpret_cast<const uint8_t*>(frame.data()), frame.size(), message);
}

void TestSingleFrame() {
    WifiProvisioningReassembler parser;
    WifiProvisioningMessage message;
    const std::string payload =
        R"({"v":2,"type":"wifi.provision","attemptId":"123e4567-e89b-42d3-a456-426614174000"})";
    assert(Push(&parser, "BM2|" + std::string(kAttempt) + "|1|1|" + payload, &message) ==
           WifiProvisioningFrameResult::kComplete);
    assert(message.protocol == "BM2");
    assert(message.attempt_id == kAttempt);
    assert(message.payload == payload);
}

void TestMultiFrameRawUtf8() {
    WifiProvisioningReassembler parser;
    WifiProvisioningMessage message;
    std::string first = "BM2|" + std::string(kAttempt) + "|1|2|{\"ssid\":\"\xE9";
    std::string second = "BM2|" + std::string(kAttempt) + "|2|2|\x9B\xAA\"}";
    assert(Push(&parser, first, &message) == WifiProvisioningFrameResult::kIncomplete);
    assert(Push(&parser, second, &message) == WifiProvisioningFrameResult::kComplete);
    assert(message.payload == std::string("{\"ssid\":\"\xE9\x9B\xAA\"}"));
}

void TestFrameOneResetsPartialAttempt() {
    WifiProvisioningReassembler parser;
    WifiProvisioningMessage message;
    assert(Push(&parser, "BM1|" + std::string(kAttempt) + "|1|2|old", &message) ==
           WifiProvisioningFrameResult::kIncomplete);
    assert(Push(&parser, "BM2|" + std::string(kAttempt) + "|1|1|new", &message) ==
           WifiProvisioningFrameResult::kComplete);
    assert(message.protocol == "BM2");
    assert(message.payload == "new");
}

void TestRejectsOutOfOrderAndOversize() {
    WifiProvisioningReassembler parser;
    WifiProvisioningMessage message;
    assert(Push(&parser, "BM2|" + std::string(kAttempt) + "|2|2|late", &message) ==
           WifiProvisioningFrameResult::kInvalid);
    std::string oversized(161, 'x');
    assert(parser.Push(reinterpret_cast<const uint8_t*>(oversized.data()), oversized.size(), &message) ==
           WifiProvisioningFrameResult::kInvalid);
}

void TestRejectsInvalidAttemptAndProtocol() {
    WifiProvisioningReassembler parser;
    WifiProvisioningMessage message;
    assert(Push(&parser, "BM3|" + std::string(kAttempt) + "|1|1|payload", &message) ==
           WifiProvisioningFrameResult::kInvalid);
    assert(Push(&parser, "BM2|123e4567-e89b-12d3-a456-426614174000|1|1|payload", &message) ==
           WifiProvisioningFrameResult::kInvalid);
}

void TestRejectsMixedSequenceAndOversizedLogicalPayload() {
    WifiProvisioningReassembler parser;
    WifiProvisioningMessage message;
    const std::string other_attempt = "223e4567-e89b-42d3-a456-426614174000";
    assert(Push(&parser, "BM2|" + std::string(kAttempt) + "|1|2|first", &message) ==
           WifiProvisioningFrameResult::kIncomplete);
    assert(Push(&parser, "BM2|" + other_attempt + "|2|2|second", &message) ==
           WifiProvisioningFrameResult::kInvalid);

    const std::string chunk(100, 'x');
    assert(Push(&parser, "BM2|" + std::string(kAttempt) + "|1|4|" + chunk, &message) ==
           WifiProvisioningFrameResult::kIncomplete);
    assert(Push(&parser, "BM2|" + std::string(kAttempt) + "|2|4|" + chunk, &message) ==
           WifiProvisioningFrameResult::kIncomplete);
    assert(Push(&parser, "BM2|" + std::string(kAttempt) + "|3|4|" + chunk, &message) ==
           WifiProvisioningFrameResult::kIncomplete);
    assert(Push(&parser, "BM2|" + std::string(kAttempt) + "|4|4|" + chunk, &message) ==
           WifiProvisioningFrameResult::kInvalid);
}

}  // namespace

int main() {
    TestSingleFrame();
    TestMultiFrameRawUtf8();
    TestFrameOneResetsPartialAttempt();
    TestRejectsOutOfOrderAndOversize();
    TestRejectsInvalidAttemptAndProtocol();
    TestRejectsMixedSequenceAndOversizedLogicalPayload();
    std::cout << "wifi_provisioning_protocol_test: PASS\n";
    return 0;
}
