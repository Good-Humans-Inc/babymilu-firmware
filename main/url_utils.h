#pragma once

#include <string>

namespace url_utils {

bool IsUsableWebSocketUrl(const std::string& url);
std::string QueryParam(const std::string& url, const std::string& key);
void UpsertQueryParam(std::string& url, const std::string& key, const std::string& value);
std::string NormalizeConnectionType(std::string value);
std::string BuildWebSocketUrl(std::string base_url,
                              const std::string& device_id,
                              const std::string& client_id,
                              const std::string& connectionType);

}  // namespace url_utils
