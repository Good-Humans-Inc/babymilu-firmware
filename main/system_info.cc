#include "system_info.h"

#include <esp_mac.h>

#include <cstdio>

namespace system_info {

std::string GetMacAddress() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buffer[18] = {};
    snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buffer;
}

std::string GetClientId() {
    std::string mac = GetMacAddress();
    for (char& ch : mac) {
        if (ch == ':') {
            ch = '-';
        }
    }
    return "echoear-" + mac;
}

}  // namespace system_info
