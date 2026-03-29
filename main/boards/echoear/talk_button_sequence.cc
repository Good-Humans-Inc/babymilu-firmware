#include "talk_button_sequence.h"

#include "sd_card.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include <dirent.h>
#include <sys/stat.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>

namespace talk_button_sequence {

static const char* const TAG = "talk_seq";

static std::string SequenceFilePath() {
    return std::string(SdCard::MountPoint()) + "/" + kSequenceFileName;
}

static constexpr int kMaxSteps = 48;
static constexpr int kMinDurationMs = 50;
static constexpr int kMaxDurationMs = 600000;
static constexpr size_t kLineBuf = 160;

static void TrimInPlace(std::string& s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
}

static bool AppendLine(const char* line_raw, std::vector<std::pair<std::string, int>>& out) {
    std::string line(line_raw);
    TrimInPlace(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') {
        return true;
    }
    size_t i = 0;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
        ++i;
    }
    size_t start_em = i;
    while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) {
        ++i;
    }
    if (start_em == i) {
        return true;
    }
    std::string emotion = line.substr(start_em, i - start_em);
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
        ++i;
    }
    if (i >= line.size()) {
        ESP_LOGW(TAG, "Line missing duration, skipping: %s", line.c_str());
        return true;
    }
    char* endptr = nullptr;
    long ms = std::strtol(line.c_str() + i, &endptr, 10);
    if (endptr == line.c_str() + i) {
        ESP_LOGW(TAG, "Bad duration, skipping line: %s", line.c_str());
        return true;
    }
    if (ms < kMinDurationMs) {
        ms = kMinDurationMs;
    }
    if (ms > kMaxDurationMs) {
        ms = kMaxDurationMs;
    }
    out.emplace_back(std::move(emotion), static_cast<int>(ms));
    return true;
}

static void LogSdRootEntriesForDebug(const char* failed_path) {
    const char* root = SdCard::MountPoint();
    DIR* d = opendir(root);
    if (d == nullptr) {
        ESP_LOGW(TAG, "Debug: opendir(%s) failed errno=%d %s", root, errno, strerror(errno));
        return;
    }
    ESP_LOGW(TAG, "Debug: could not open %s — listing %s:", failed_path, root);
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.' && (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0'))) {
            continue;
        }
        ESP_LOGI(TAG, "  SD entry: \"%s\" (d_type=%d)", e->d_name, static_cast<int>(e->d_type));
    }
    closedir(d);
}

static bool LoadFromPath(const char* path, std::vector<std::pair<std::string, int>>& out) {
    out.clear();
    if (!SdCard::IsMounted()) {
        ESP_LOGW(TAG, "SD not mounted yet; cannot read %s", path);
        return false;
    }

    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            ESP_LOGW(TAG, "%s exists but is a directory, not a text file", path);
            return false;
        }
    }

    FILE* f = nullptr;
    for (int attempt = 0; attempt < 3 && f == nullptr; ++attempt) {
        if (attempt > 0) {
            vTaskDelay(pdMS_TO_TICKS(80));
        }
        errno = 0;
        f = std::fopen(path, "rb");
    }
    if (!f) {
        int e = errno;
        ESP_LOGW(TAG, "Cannot open %s (errno=%d %s)", path, e, e ? strerror(e) : "fopen failed (errno unset)");
        LogSdRootEntriesForDebug(path);
        return false;
    }
    char line[kLineBuf];
    while (std::fgets(line, sizeof(line), f) && static_cast<int>(out.size()) < kMaxSteps) {
        AppendLine(line, out);
    }
    std::fclose(f);
    return !out.empty();
}

void LoadSteps(std::vector<std::pair<std::string, int>>& out) {
    const std::string path = SequenceFilePath();
    if (LoadFromPath(path.c_str(), out)) {
        ESP_LOGI(TAG, "Loaded %u steps from %s", static_cast<unsigned>(out.size()), path.c_str());
        return;
    }
    out.clear();
    ESP_LOGW(TAG, "No steps loaded from %s (no fallback)", path.c_str());
}

}  // namespace talk_button_sequence
