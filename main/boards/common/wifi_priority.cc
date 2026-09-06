#include "wifi_priority.h"

#include <algorithm>
#include <unordered_set>

bool AppendCredentialAtLowestPriority(
    const std::vector<WifiPriorityCredential>& current,
    const std::string& ssid,
    const std::string& password,
    std::vector<WifiPriorityCredential>* result) {
    if (result == nullptr || ssid.empty()) return false;
    const bool already_saved = std::any_of(
        current.begin(), current.end(), [&ssid](const auto& item) {
            return item.ssid == ssid;
        });
    if (!already_saved && current.size() >= kMaxSavedWifiNetworks) return false;

    result->clear();
    result->reserve(current.size() + (already_saved ? 0 : 1));
    for (const auto& item : current) {
        if (item.ssid != ssid) result->push_back(item);
    }
    result->push_back({ssid, password});
    return true;
}

std::vector<WifiPriorityCredential> ReconcileWifiPriority(
    const std::vector<WifiPriorityCredential>& current,
    const std::vector<std::string>& ranked_ssids,
    const std::vector<std::string>& deleted_ssids) {
    const std::unordered_set<std::string> deleted(
        deleted_ssids.begin(), deleted_ssids.end());
    std::vector<WifiPriorityCredential> result;
    result.reserve(current.size());

    for (const auto& ranked : ranked_ssids) {
        if (deleted.count(ranked) != 0) continue;
        auto found = std::find_if(
            current.begin(), current.end(), [&ranked](const auto& item) {
                return item.ssid == ranked;
            });
        if (found == current.end()) continue;
        const bool duplicate = std::any_of(
            result.begin(), result.end(), [&ranked](const auto& item) {
                return item.ssid == ranked;
            });
        if (!duplicate) result.push_back(*found);
    }

    for (const auto& item : current) {
        if (deleted.count(item.ssid) != 0) continue;
        const bool already_added = std::any_of(
            result.begin(), result.end(), [&item](const auto& candidate) {
                return candidate.ssid == item.ssid;
            });
        if (!already_added) result.push_back(item);
    }
    return result;
}
