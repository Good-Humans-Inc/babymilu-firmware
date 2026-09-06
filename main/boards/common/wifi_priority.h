#ifndef WIFI_PRIORITY_H
#define WIFI_PRIORITY_H

#include <cstddef>
#include <string>
#include <vector>

struct WifiPriorityCredential {
    std::string ssid;
    std::string password;
};

constexpr size_t kMaxSavedWifiNetworks = 10;

bool AppendCredentialAtLowestPriority(
    const std::vector<WifiPriorityCredential>& current,
    const std::string& ssid,
    const std::string& password,
    std::vector<WifiPriorityCredential>* result);

std::vector<WifiPriorityCredential> ReconcileWifiPriority(
    const std::vector<WifiPriorityCredential>& current,
    const std::vector<std::string>& ranked_ssids,
    const std::vector<std::string>& deleted_ssids);

#endif
