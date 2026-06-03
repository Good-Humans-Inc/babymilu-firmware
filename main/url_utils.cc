#include "url_utils.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace url_utils {

bool IsUsableWebSocketUrl(const std::string& url) {
    if (url.rfind("ws://", 0) != 0 && url.rfind("wss://", 0) != 0) {
        return false;
    }
    return url.find("localhost") == std::string::npos &&
           url.find("127.0.0.1") == std::string::npos &&
           url.find("0.0.0.0") == std::string::npos &&
           url.find("::1") == std::string::npos;
}

std::string QueryParam(const std::string& url, const std::string& key) {
    size_t query_start = url.find('?');
    if (query_start == std::string::npos) {
        return "";
    }
    size_t fragment_start = url.find('#', query_start);
    std::string query = url.substr(
        query_start + 1,
        fragment_start == std::string::npos ? std::string::npos : fragment_start - query_start - 1);
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        std::string part = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t equals = part.find('=');
        std::string name = part.substr(0, equals);
        if (name == key) {
            return equals == std::string::npos ? "" : part.substr(equals + 1);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return "";
}

void UpsertQueryParam(std::string& url, const std::string& key, const std::string& value) {
    if (url.empty() || key.empty() || value.empty()) {
        return;
    }

    size_t fragment_start = url.find('#');
    std::string fragment = fragment_start == std::string::npos ? "" : url.substr(fragment_start);
    std::string without_fragment = fragment_start == std::string::npos ? url : url.substr(0, fragment_start);
    size_t query_start = without_fragment.find('?');
    std::string base = query_start == std::string::npos ? without_fragment : without_fragment.substr(0, query_start);
    std::string query = query_start == std::string::npos ? "" : without_fragment.substr(query_start + 1);

    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= query.size() && !query.empty()) {
        size_t end = query.find('&', start);
        std::string part = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t equals = part.find('=');
        std::string name = part.substr(0, equals);
        if (!part.empty() && name != key) {
            parts.push_back(part);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    parts.push_back(key + "=" + value);

    url = base + "?";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            url += "&";
        }
        url += parts[i];
    }
    url += fragment;
}

std::string NormalizeConnectionType(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (value.empty()) {
        return "normal";
    }
    return value;
}

std::string BuildWebSocketUrl(std::string base_url,
                              const std::string& device_id,
                              const std::string& client_id,
                              const std::string& connectionType) {
    if (!device_id.empty()) {
        UpsertQueryParam(base_url, "device_id", device_id);
    }
    if (!client_id.empty()) {
        UpsertQueryParam(base_url, "client_id", client_id);
    }
    UpsertQueryParam(base_url, "connectionType", NormalizeConnectionType(connectionType));
    return base_url;
}

}  // namespace url_utils
