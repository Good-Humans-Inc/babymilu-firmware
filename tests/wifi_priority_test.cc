#include "wifi_priority.h"

#include <cassert>
#include <iostream>

void TestAppendAtBottom() {
    std::vector<WifiPriorityCredential> out;
    assert(AppendCredentialAtLowestPriority(
        {{"home", "one"}, {"office", "two"}}, "hotspot", "three", &out));
    assert(out.size() == 3);
    assert(out[0].ssid == "home");
    assert(out[2].ssid == "hotspot");
}

void TestFullListRejectsNewNetworkWithoutEviction() {
    std::vector<WifiPriorityCredential> current;
    for (size_t i = 0; i < kMaxSavedWifiNetworks; ++i) {
        current.push_back({"wifi-" + std::to_string(i), "password"});
    }
    std::vector<WifiPriorityCredential> out;
    assert(!AppendCredentialAtLowestPriority(current, "wifi-new", "password", &out));
    assert(out.empty());
}

void TestExistingCredentialMovesToBottom() {
    std::vector<WifiPriorityCredential> out;
    assert(AppendCredentialAtLowestPriority(
        {{"home", "old"}, {"office", "two"}}, "home", "new", &out));
    assert(out.size() == 2);
    assert(out[0].ssid == "office");
    assert(out[1].ssid == "home");
    assert(out[1].password == "new");
}

void TestReconcileReordersDeletesAndPreservesUnknown() {
    auto out = ReconcileWifiPriority(
        {{"home", "one"}, {"office", "two"}, {"legacy", "three"}},
        {"office", "home"},
        {"legacy"});
    assert(out.size() == 2);
    assert(out[0].ssid == "office");
    assert(out[1].ssid == "home");
}

int main() {
    TestAppendAtBottom();
    TestFullListRejectsNewNetworkWithoutEviction();
    TestExistingCredentialMovesToBottom();
    TestReconcileReordersDeletesAndPreservesUnknown();
    std::cout << "wifi_priority_test: PASS\n";
    return 0;
}
