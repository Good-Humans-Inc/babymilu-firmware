#include "animation_store.h"

#include "audio_codec.h"
#include "config.h"
#include "settings_store.h"

#include <sdkconfig.h>
#include <driver/gpio.h>
#include <driver/sdmmc_host.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <string_view>
#include <utility>
#include <vector>

#define TAG "AnimationStore"
#define MOUNT_POINT "/sdcard"

namespace {

constexpr size_t kEntrySize = 44;
constexpr size_t kHeaderSize = 12;
constexpr uint32_t kMaxGifCount = 20;
constexpr uint32_t kMaxGifSize = 10 * 1024 * 1024;
constexpr size_t kDownloadTaskStackSize = 8192;
constexpr size_t kMaxGifCandidates = 5;

struct AnimationSpec {
    const char* state;
    const char* loop[kMaxGifCandidates];
    const char* start[kMaxGifCandidates];
};

struct AnimationAlias {
    const char* input;
    const char* state;
};

static constexpr AnimationSpec kAnimationSpecs[] = {
    {"normal", { "normal.gif", "normal_loop.gif", nullptr }, { nullptr }},
    {"smirk", { "smirk.gif", "smirk_loop.gif", nullptr }, { "smirk_start.gif", nullptr }},
    {"heart", { "heart.gif", "happy.gif", "heart_loop.gif", "happy_loop.gif", nullptr },
     { "heart_start.gif", "happy_start.gif", nullptr }},
    {"blush", { "blush.gif", "blush_loop.gif", nullptr }, { nullptr }},
    {"sad", { "sad.gif", "sad_loop.gif", nullptr }, { "sad_start.gif", nullptr }},
    {"laugh", { "laugh.gif", "laugh_loop.gif", nullptr }, { "laugh_start.gif", nullptr }},
    {"sleep", { "sleep.gif", "sleep_loop.gif", nullptr }, { nullptr }},
    {"starry", { "starry.gif", "starry_loop.gif", nullptr }, { "starry_start.gif", nullptr }},
    {"cry", { "cry.gif", "cry_loop.gif", nullptr }, { nullptr }},
    {"angry", { "angry.gif", "angry_loop.gif", nullptr }, { "angry_start.gif", nullptr }},
    {"listening", { "listening.gif", "listen.gif", "listening_loop.gif", "listen_loop.gif", nullptr }, { nullptr }},
    {"wifi", { "wifi.gif", "no_wifi.gif", "wifi_loop.gif", "no_wifi_loop.gif", nullptr },
     { "wifi_start.gif", "no_wifi_start.gif", nullptr }},
    {"battery", { "battery.gif", "low_battery.gif", "battery_loop.gif", "low_battery_loop.gif", nullptr },
     { "battery_start.gif", "low_battery_start.gif", nullptr }},
    {"silence", { "silence.gif", "silence_loop.gif", nullptr }, { "silence_start.gif", nullptr }},
};

static constexpr AnimationAlias kEmotionAliases[] = {
    {"normal", "normal"},
    {"idle", "normal"},
    {"smirk", "smirk"},
    {"heart", "heart"},
    {"happy", "heart"},
    {"hearty", "heart"},
    {"speaking", "heart"},
    {"speak", "heart"},
    {"talking", "heart"},
    {"tts", "heart"},
    {"blush", "blush"},
    {"embarressed", "blush"},
    {"embarrassed", "blush"},
    {"sad", "sad"},
    {"laugh", "laugh"},
    {"laughing", "laugh"},
    {"sleep", "sleep"},
    {"sleepy", "sleep"},
    {"relaxed", "sleep"},
    {"starry", "starry"},
    {"cry", "cry"},
    {"crying", "cry"},
    {"angry", "angry"},
    {"listening", "listening"},
    {"silence", "silence"},
    {"silent", "silence"},
    {"wifi", "wifi"},
    {"no_wifi", "wifi"},
    {"no-wifi", "wifi"},
    {"battery", "battery"},
    {"low_battery", "battery"},
    {"low-battery", "battery"},
};

// Keep this aligned with babymilu_server.animations.CONVERSATIONAL_ANIMATION_EMOJIS.
// Composite emojis must appear before their shorter prefixes.
static constexpr AnimationAlias kEmojiAliases[] = {
    {"❤️‍🩹", "sad"},
    {"❤️‍🔥", "starry"},
    {"😶", "normal"},
    {"😏", "smirk"}, {"🤨", "smirk"}, {"😐", "smirk"}, {"😑", "smirk"},
    {"😬", "smirk"}, {"😒", "smirk"}, {"🙄", "smirk"}, {"🤔", "smirk"},
    {"🥰", "heart"}, {"😍", "heart"}, {"😘", "heart"}, {"🤗", "heart"},
    {"😚", "heart"}, {"😙", "heart"}, {"❤️", "heart"},
    {"😳", "blush"}, {"😊", "blush"},
    {"🙁", "sad"}, {"☹️", "sad"}, {"😕", "sad"}, {"😟", "sad"},
    {"😨", "sad"}, {"😦", "sad"}, {"😞", "sad"}, {"😧", "sad"},
    {"🤕", "sad"}, {"😮‍💨", "sad"},
    {"😄", "laugh"}, {"😁", "laugh"}, {"😆", "laugh"}, {"🤣", "laugh"},
    {"😂", "laugh"}, {"😋", "laugh"}, {"😛", "laugh"},
    {"😴", "sleep"}, {"😪", "sleep"}, {"🥱", "sleep"},
    {"🤩", "starry"}, {"🤠", "starry"}, {"🥳", "starry"}, {"🤯", "starry"},
    {"😮", "starry"}, {"😯", "starry"}, {"😲", "starry"},
    {"😭", "cry"}, {"😥", "cry"}, {"😢", "cry"}, {"😫", "cry"},
    {"😩", "cry"}, {"😖", "cry"}, {"😣", "cry"}, {"💔", "cry"},
    {"😡", "angry"}, {"😤", "angry"}, {"😠", "angry"}, {"🤬", "angry"},
};

uint16_t ReadLe16(FILE* f, bool* ok = nullptr) {
    uint8_t b[2] = {};
    bool success = fread(b, 1, sizeof(b), f) == sizeof(b);
    if (ok != nullptr) {
        *ok = *ok && success;
    }
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}

uint32_t ReadLe32(FILE* f, bool* ok = nullptr) {
    uint8_t b[4] = {};
    bool success = fread(b, 1, sizeof(b), f) == sizeof(b);
    if (ok != nullptr) {
        *ok = *ok && success;
    }
    return static_cast<uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

bool FileExists(const char* path) {
    struct stat st = {};
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

std::string_view TrimLeft(std::string_view value) {
    while (!value.empty()) {
        char c = value.front();
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        value.remove_prefix(1);
    }
    return value;
}

char ToLowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

bool EqualsAsciiInsensitive(std::string_view left, const char* right) {
    if (right == nullptr || left.size() != std::strlen(right)) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (ToLowerAscii(left[i]) != ToLowerAscii(right[i])) {
            return false;
        }
    }
    return true;
}

bool StartsWith(std::string_view value, const char* prefix) {
    if (prefix == nullptr) {
        return false;
    }
    const size_t prefix_len = std::strlen(prefix);
    return value.size() >= prefix_len && value.substr(0, prefix_len) == prefix;
}

const char* ResolveByName(std::string_view value) {
    std::string_view trimmed = TrimLeft(value);
    for (const auto& alias : kEmotionAliases) {
        if (EqualsAsciiInsensitive(trimmed, alias.input)) {
            return alias.state;
        }
    }
    return nullptr;
}

const char* ResolveByEmoji(std::string_view value) {
    std::string_view trimmed = TrimLeft(value);
    for (const auto& alias : kEmojiAliases) {
        if (StartsWith(trimmed, alias.input)) {
            return alias.state;
        }
    }
    return nullptr;
}

const char* ResolveAnimationState(std::string_view value) {
    if (const char* state = ResolveByName(value)) {
        return state;
    }
    if (const char* state = ResolveByEmoji(value)) {
        return state;
    }
    return "normal";
}

const AnimationSpec& SpecForState(const char* state) {
    for (const auto& spec : kAnimationSpecs) {
        if (std::strcmp(spec.state, state) == 0) {
            return spec;
        }
    }
    return kAnimationSpecs[0];
}

size_t FileSize(const char* path) {
    struct stat st = {};
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        return 0;
    }
    return static_cast<size_t>(st.st_size);
}

void ReleaseLoadedGif(LoadedGif& gif) {
    if (gif.data != nullptr) {
        heap_caps_free(gif.data);
        gif.data = nullptr;
    }
    gif.size = 0;
    gif.name.clear();
}

std::string ConfiguredAnimationUrl() {
    SettingsStore settings("animation", false);
    std::string url = settings.GetString("test_bin_url");
    if (!url.empty()) {
        return url;
    }
    return CONFIG_ECHOEAR_BABYMILU_ANIMATION_BIN_URL;
}

std::string DerivedSiblingUrl(const std::string& base_url, const char* filename) {
    if (base_url.empty() || filename == nullptr) {
        return "";
    }
    size_t query = base_url.find_first_of("?#");
    std::string path = query == std::string::npos ? base_url : base_url.substr(0, query);
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash + 1 >= path.size()) {
        return "";
    }
    return path.substr(0, slash + 1) + filename;
}

std::string ConfiguredStartupGifUrl(const std::string& test_bin_url) {
    SettingsStore settings("animation", false);
    std::string url = settings.GetString("startup_gif_url");
    return url.empty() ? DerivedSiblingUrl(test_bin_url, "startup.gif") : url;
}

std::string ConfiguredStartupWavUrl(const std::string& test_bin_url) {
    SettingsStore settings("animation", false);
    std::string url = settings.GetString("startup_wav_url");
    return url.empty() ? DerivedSiblingUrl(test_bin_url, "startup.wav") : url;
}

esp_err_t CollectDownload(esp_http_client_event_t* evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data == nullptr || evt->data_len <= 0) {
        return ESP_OK;
    }
    FILE* f = static_cast<FILE*>(evt->user_data);
    if (f != nullptr) {
        fwrite(evt->data, 1, evt->data_len, f);
    }
    return ESP_OK;
}

}  // namespace

AnimationStore::~AnimationStore() {
    ClearCache();
    ReleaseLoadedGif(startup_gif_);
}

esp_err_t AnimationStore::Init() {
    esp_err_t result = InitDisplay();
    if (InitStartupMedia() != ESP_OK && result == ESP_OK) {
        result = ESP_FAIL;
    }
    if (InitBundle() != ESP_OK && result == ESP_OK) {
        result = ESP_FAIL;
    }
    return result;
}

esp_err_t AnimationStore::InitDisplay() {
    return display_.Init();
}

esp_err_t AnimationStore::InitStartupMedia() {
    mounted_ = MountSdCard();
    if (!mounted_) {
        return ESP_FAIL;
    }
    ShowStartup();
    return ESP_OK;
}

esp_err_t AnimationStore::InitBundle() {
    if (!mounted_ && !MountSdCard()) {
        return ESP_FAIL;
    }
    std::vector<GifEntry> parsed;
    if (ValidateBundle(ECHOEAR_TEST_BIN_PATH, &parsed)) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_ = std::move(parsed);
        bundle_ready_ = true;
        ESP_LOGI(TAG, "test.bin validated entries=%u", static_cast<unsigned>(entries_.size()));
        return ESP_OK;
    }
    bundle_ready_ = false;
    ESP_LOGW(TAG, "test.bin not available or invalid; animations will fall back to log-only state");
    return ESP_FAIL;
}

bool AnimationStore::MountSdCard() {
    if (mounted_) {
        return true;
    }

    gpio_config_t power_gpio_config = {};
    power_gpio_config.pin_bit_mask = BIT64(POWER_CTRL);
    power_gpio_config.mode = GPIO_MODE_OUTPUT;
    gpio_config(&power_gpio_config);
    gpio_set_level(POWER_CTRL, 0);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 20;
    mount_config.allocation_unit_size = 64 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = SD_SCK;
    slot_config.cmd = SD_MOSI;
    slot_config.d0 = SD_MISO;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    sdmmc_card_t* card = nullptr;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed: %s", esp_err_to_name(ret));
        return false;
    }
    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "SD mounted at %s", MOUNT_POINT);
    mounted_ = true;
    return true;
}

bool AnimationStore::ValidateBundleFile(const char* path) {
    return ValidateBundle(path, nullptr);
}

bool AnimationStore::ValidateBundle(const char* path, std::vector<GifEntry>* entries) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size < static_cast<long>(kHeaderSize + kEntrySize + 13)) {
        fclose(f);
        return false;
    }

    bool ok = true;
    uint32_t file_count = ReadLe32(f, &ok);
    uint32_t checksum = ReadLe32(f, &ok);
    uint32_t combined_length = ReadLe32(f, &ok);
    (void)checksum;
    if (!ok || file_count == 0 || file_count > kMaxGifCount) {
        fclose(f);
        return false;
    }
    const size_t table_size = file_count * kEntrySize;
    const size_t data_start = kHeaderSize + table_size;
    if (file_size < static_cast<long>(data_start) || combined_length < table_size) {
        fclose(f);
        return false;
    }

    std::vector<GifEntry> parsed;
    parsed.reserve(file_count);
    for (uint32_t i = 0; i < file_count; ++i) {
        char name[33] = {};
        uint32_t size = 0;
        uint32_t offset = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        if (fread(name, 32, 1, f) != 1 ||
            fread(&size, sizeof(size), 1, f) != 1 ||
            fread(&offset, sizeof(offset), 1, f) != 1 ||
            fread(&width, sizeof(width), 1, f) != 1 ||
            fread(&height, sizeof(height), 1, f) != 1) {
            fclose(f);
            return false;
        }
        (void)width;
        (void)height;
        if (strnlen(name, sizeof(name)) == 0 || size < 13 || size > kMaxGifSize ||
            data_start + offset + 2 + size > static_cast<size_t>(file_size)) {
            fclose(f);
            return false;
        }
        parsed.push_back({name, size, offset});
    }
    fclose(f);
    if (entries != nullptr) {
        *entries = std::move(parsed);
    }
    return true;
}

bool AnimationStore::ValidateGifFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    char header[6] = {};
    bool ok = fread(header, 1, sizeof(header), f) == sizeof(header);
    fclose(f);
    return ok && (std::memcmp(header, "GIF87a", 6) == 0 || std::memcmp(header, "GIF89a", 6) == 0);
}

bool AnimationStore::ValidateWavFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    bool ok = true;
    char tag[4] = {};
    if (fread(tag, 1, sizeof(tag), f) != sizeof(tag) || std::memcmp(tag, "RIFF", sizeof(tag)) != 0) {
        fclose(f);
        return false;
    }
    (void)ReadLe32(f, &ok);
    if (!ok || fread(tag, 1, sizeof(tag), f) != sizeof(tag) || std::memcmp(tag, "WAVE", sizeof(tag)) != 0) {
        fclose(f);
        return false;
    }

    bool found_fmt = false;
    bool found_data = false;
    while (ok && fread(tag, 1, sizeof(tag), f) == sizeof(tag)) {
        uint32_t chunk_size = ReadLe32(f, &ok);
        if (!ok) {
            break;
        }
        if (std::memcmp(tag, "fmt ", sizeof(tag)) == 0) {
            uint16_t audio_format = ReadLe16(f, &ok);
            uint16_t channels = ReadLe16(f, &ok);
            (void)ReadLe32(f, &ok);
            (void)ReadLe32(f, &ok);
            (void)ReadLe16(f, &ok);
            uint16_t bits_per_sample = ReadLe16(f, &ok);
            found_fmt = ok && audio_format == 1 && bits_per_sample == 16 && channels >= 1 && channels <= 2;
            if (chunk_size > 16) {
                fseek(f, static_cast<long>(chunk_size - 16), SEEK_CUR);
            }
        } else if (std::memcmp(tag, "data", sizeof(tag)) == 0) {
            found_data = chunk_size > 0;
            break;
        } else {
            fseek(f, static_cast<long>(chunk_size), SEEK_CUR);
        }
        if (chunk_size & 1) {
            fseek(f, 1, SEEK_CUR);
        }
    }
    fclose(f);
    return found_fmt && found_data;
}

bool AnimationStore::LoadRawFile(const char* path, LoadedGif& out) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size <= 0 || file_size > static_cast<long>(kMaxGifSize)) {
        fclose(f);
        return false;
    }
    uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(static_cast<size_t>(file_size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        fclose(f);
        ESP_LOGW(TAG, "PSRAM allocation failed for %s size=%ld", path, file_size);
        return false;
    }
    if (fread(buffer, 1, static_cast<size_t>(file_size), f) != static_cast<size_t>(file_size)) {
        heap_caps_free(buffer);
        fclose(f);
        return false;
    }
    fclose(f);
    ReleaseLoadedGif(out);
    out.name = path;
    out.data = buffer;
    out.size = static_cast<size_t>(file_size);
    return true;
}

void AnimationStore::ShowStartup() {
    if (!mounted_ || !ValidateGifFile(ECHOEAR_STARTUP_GIF_PATH)) {
        ESP_LOGW(TAG, "startup.gif not available; startup animation skipped");
        startup_ready_ = false;
        return;
    }
    if (!LoadRawFile(ECHOEAR_STARTUP_GIF_PATH, startup_gif_)) {
        ESP_LOGW(TAG, "startup.gif load failed");
        startup_ready_ = false;
        return;
    }
    startup_ready_ = display_.ShowLoop(startup_gif_.data, startup_gif_.size, "startup.gif");
    ESP_LOGI(TAG, "startup.gif %s size=%u", startup_ready_ ? "shown" : "loaded without display",
             static_cast<unsigned>(startup_gif_.size));
}

bool AnimationStore::PlayStartupWav(AudioCodec* codec) {
    if (codec == nullptr || !mounted_ || !FileExists(ECHOEAR_STARTUP_WAV_PATH)) {
        ESP_LOGW(TAG, "startup.wav skipped: codec=%p mounted=%d present=%d",
                 codec, mounted_, FileExists(ECHOEAR_STARTUP_WAV_PATH));
        return false;
    }

    FILE* file = fopen(ECHOEAR_STARTUP_WAV_PATH, "rb");
    if (file == nullptr) {
        ESP_LOGW(TAG, "failed to open startup.wav");
        return false;
    }

    bool ok = true;
    char tag[4] = {};
    if (fread(tag, 1, sizeof(tag), file) != sizeof(tag) || std::memcmp(tag, "RIFF", sizeof(tag)) != 0) {
        fclose(file);
        ESP_LOGE(TAG, "startup.wav invalid header: missing RIFF");
        return false;
    }
    (void)ReadLe32(file, &ok);
    if (!ok || fread(tag, 1, sizeof(tag), file) != sizeof(tag) || std::memcmp(tag, "WAVE", sizeof(tag)) != 0) {
        fclose(file);
        ESP_LOGE(TAG, "startup.wav invalid header: missing WAVE");
        return false;
    }

    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint32_t data_size = 0;
    bool found_fmt = false;
    bool found_data = false;

    while (ok && fread(tag, 1, sizeof(tag), file) == sizeof(tag)) {
        uint32_t chunk_size = ReadLe32(file, &ok);
        if (!ok) {
            break;
        }
        if (std::memcmp(tag, "fmt ", sizeof(tag)) == 0) {
            uint16_t audio_format = ReadLe16(file, &ok);
            channels = ReadLe16(file, &ok);
            sample_rate = ReadLe32(file, &ok);
            (void)ReadLe32(file, &ok);
            (void)ReadLe16(file, &ok);
            uint16_t bits_per_sample = ReadLe16(file, &ok);
            if (!ok || audio_format != 1 || bits_per_sample != 16 || channels == 0 || channels > 2) {
                fclose(file);
                ESP_LOGE(TAG, "startup.wav must be 16-bit PCM mono/stereo");
                return false;
            }
            if (sample_rate != static_cast<uint32_t>(codec->output_sample_rate())) {
                ESP_LOGW(TAG, "startup.wav sample rate %u differs from codec output %d",
                         sample_rate, codec->output_sample_rate());
            }
            found_fmt = true;
            if (chunk_size > 16) {
                fseek(file, static_cast<long>(chunk_size - 16), SEEK_CUR);
            }
        } else if (std::memcmp(tag, "data", sizeof(tag)) == 0) {
            data_size = chunk_size;
            found_data = true;
            break;
        } else {
            fseek(file, static_cast<long>(chunk_size), SEEK_CUR);
        }
        if (chunk_size & 1) {
            fseek(file, 1, SEEK_CUR);
        }
    }

    if (!found_fmt || !found_data || data_size == 0) {
        fclose(file);
        ESP_LOGE(TAG, "startup.wav missing required fmt/data chunks");
        return false;
    }

    codec->EnableOutput(true);
    constexpr size_t kReadBufferSize = 2048;
    std::vector<uint8_t> read_buf(kReadBufferSize);
    std::vector<int16_t> samples;
    samples.reserve(512);
    const uint32_t frame_size = static_cast<uint32_t>(channels) * sizeof(int16_t);
    uint32_t remaining = data_size;
    uint64_t emitted_samples = 0;

    while (remaining > 0) {
        size_t request = std::min<size_t>(read_buf.size(), remaining);
        size_t got = fread(read_buf.data(), 1, request, file);
        if (got == 0) {
            break;
        }
        remaining -= got;
        size_t offset = 0;
        while (offset + frame_size <= got) {
            int32_t sample_sum = 0;
            for (uint16_t ch = 0; ch < channels; ++ch) {
                size_t sample_offset = offset + ch * sizeof(int16_t);
                int16_t s = static_cast<int16_t>(read_buf[sample_offset] | (read_buf[sample_offset + 1] << 8));
                sample_sum += s;
            }
            samples.push_back(static_cast<int16_t>(sample_sum / channels));
            offset += frame_size;
            ++emitted_samples;
            if (samples.size() >= 256) {
                codec->OutputData(samples);
                samples.clear();
            }
        }
    }
    if (!samples.empty()) {
        codec->OutputData(samples);
    }
    fclose(file);

    if (emitted_samples == 0) {
        ESP_LOGW(TAG, "no PCM frames emitted from startup.wav");
        return false;
    }

    const uint32_t output_sample_rate = static_cast<uint32_t>(codec->output_sample_rate());
    uint32_t playback_ms = static_cast<uint32_t>((emitted_samples * 1000ULL) / output_sample_rate);
    vTaskDelay(pdMS_TO_TICKS(playback_ms + 60));
    ESP_LOGI(TAG, "startup.wav playback finished samples=%llu", emitted_samples);
    return true;
}

LoadedGif* AnimationStore::LoadGifByName(const std::string& name, bool log_missing) {
    auto cached = cache_.find(name);
    if (cached != cache_.end()) {
        return &cached->second;
    }
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&name](const GifEntry& entry) { return entry.name == name; });
    if (it == entries_.end()) {
        if (log_missing) {
            ESP_LOGW(TAG, "GIF not found in test.bin: %s", name.c_str());
        }
        return nullptr;
    }
    LoadedGif loaded;
    if (!ExtractGif(*it, loaded)) {
        return nullptr;
    }
    auto inserted = cache_.emplace(name, loaded);
    return &inserted.first->second;
}

bool AnimationStore::ExtractGif(const GifEntry& entry, LoadedGif& out) {
    FILE* f = fopen(ECHOEAR_TEST_BIN_PATH, "rb");
    if (f == nullptr) {
        return false;
    }
    const size_t data_start = kHeaderSize + entries_.size() * kEntrySize;
    if (fseek(f, static_cast<long>(data_start + entry.offset), SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    uint8_t magic[2] = {};
    if (fread(magic, 1, sizeof(magic), f) != sizeof(magic) || magic[0] != 0x5A || magic[1] != 0x5A) {
        fclose(f);
        ESP_LOGW(TAG, "GIF entry has bad magic: %s", entry.name.c_str());
        return false;
    }
    uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(entry.size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
        fclose(f);
        ESP_LOGW(TAG, "PSRAM allocation failed for GIF %s size=%u", entry.name.c_str(), entry.size);
        return false;
    }
    if (fread(buffer, 1, entry.size, f) != entry.size) {
        heap_caps_free(buffer);
        fclose(f);
        return false;
    }
    fclose(f);
    out.name = entry.name;
    out.data = buffer;
    out.size = entry.size;
    ESP_LOGI(TAG, "lazy-loaded GIF %s size=%u from PSRAM", out.name.c_str(), static_cast<unsigned>(out.size));
    return true;
}

LoadedGif* AnimationStore::LoadFirstAvailable(const char* const* candidates, bool log_missing) {
    if (candidates == nullptr) {
        return nullptr;
    }
    const char* first = nullptr;
    for (size_t i = 0; candidates[i] != nullptr; ++i) {
        if (first == nullptr) {
            first = candidates[i];
        }
        if (LoadedGif* gif = LoadGifByName(candidates[i], false)) {
            return gif;
        }
    }
    if (first != nullptr && log_missing) {
        ESP_LOGW(TAG, "GIF candidates not found in test.bin, first=%s", first);
    }
    return nullptr;
}

void AnimationStore::ShowEmotion(const std::string& emotion) {
    const char* selected = ResolveAnimationState(emotion);
    const AnimationSpec& spec = SpecForState(selected);

    std::lock_guard<std::mutex> lock(mutex_);
    LoadedGif* loop = LoadFirstAvailable(spec.loop);
    LoadedGif* start = LoadFirstAvailable(spec.start, false);
    bool ready = false;
    if (start != nullptr && loop != nullptr) {
        ready = display_.ShowStartThenLoop(start->data, start->size, start->name.c_str(),
                                           loop->data, loop->size, loop->name.c_str());
    } else if (loop != nullptr) {
        ready = display_.ShowLoop(loop->data, loop->size, loop->name.c_str());
    }
    ESP_LOGI(TAG, "show emotion=%s state=%s gif=%s start=%s ready=%d",
             emotion.c_str(), selected,
             loop != nullptr ? loop->name.c_str() : "<missing>",
             start != nullptr ? start->name.c_str() : "<none>", ready);
}

void AnimationStore::ShowUtility(const std::string& name) {
    const char* selected = ResolveAnimationState(name);
    const AnimationSpec& spec = SpecForState(selected);

    std::lock_guard<std::mutex> lock(mutex_);
    LoadedGif* loop = LoadFirstAvailable(spec.loop);
    LoadedGif* start = LoadFirstAvailable(spec.start, false);
    bool ready = false;
    if (start != nullptr && loop != nullptr) {
        ready = display_.ShowStartThenLoop(start->data, start->size, start->name.c_str(),
                                           loop->data, loop->size, loop->name.c_str());
    } else if (loop != nullptr) {
        ready = display_.ShowLoop(loop->data, loop->size, loop->name.c_str());
    }
    ESP_LOGI(TAG, "show utility=%s state=%s gif=%s start=%s ready=%d",
             name.c_str(), selected,
             loop != nullptr ? loop->name.c_str() : "<missing>",
             start != nullptr ? start->name.c_str() : "<none>", ready);
}

void AnimationStore::SetBacklightBrightness(int percent) {
    display_.SetBacklightBrightness(percent);
}

int AnimationStore::AdjustBacklightBrightness(int delta) {
    return display_.AdjustBacklightBrightness(delta);
}

void AnimationStore::RestoreBacklightBrightness() {
    display_.RestoreBacklightBrightness();
}

void AnimationStore::ShowStatusCanvas(const char* network_label, int network_percent, int battery_percent,
                                      bool charging, uint32_t duration_ms) {
    display_.ShowStatusCanvas(network_label, network_percent, battery_percent, charging, duration_ms);
}

void AnimationStore::ShowValueOverlay(const char* label, int percent, uint32_t duration_ms) {
    display_.ShowValueOverlay(label, percent, duration_ms);
}

void AnimationStore::ClearOverlay() {
    display_.ClearOverlay();
}

void AnimationStore::TriggerUpdateCheck(const std::string& test_bin_url) {
    if (!test_bin_url.empty()) {
        SettingsStore settings("animation", true);
        settings.SetString("test_bin_url", test_bin_url);
    }
    if (update_running_.exchange(true)) {
        ESP_LOGI(TAG, "animation update check already running");
        return;
    }
    BaseType_t ok = xTaskCreateWithCaps(UpdateTask, "anim_update", kDownloadTaskStackSize, this, 2,
                                        &update_task_, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "animation update task create failed; running inline");
        RunUpdateCheck();
        update_running_ = false;
        update_task_ = nullptr;
    }
}

void AnimationStore::UpdateTask(void* arg) {
    auto* self = static_cast<AnimationStore*>(arg);
    self->RunUpdateCheck();
    self->update_running_ = false;
    self->update_task_ = nullptr;
    vTaskDelete(nullptr);
}

void AnimationStore::RunUpdateCheck() {
    if (!mounted_ && !MountSdCard()) {
        update_running_ = false;
        return;
    }
    const std::string test_bin_url = ConfiguredAnimationUrl();
    if (test_bin_url.empty()) {
        ESP_LOGI(TAG, "animation update skipped: no remote test.bin URL configured");
        return;
    }

    const std::string startup_gif_url = ConfiguredStartupGifUrl(test_bin_url);
    const std::string startup_wav_url = ConfiguredStartupWavUrl(test_bin_url);
    if (!startup_gif_url.empty()) {
        DownloadStartupGifToTempAndSwap(startup_gif_url);
    }
    if (!startup_wav_url.empty()) {
        DownloadStartupWavToTempAndSwap(startup_wav_url);
    }
    if (DownloadBundleToTempAndSwap(test_bin_url)) {
        ClearCache();
        InitBundle();
        ESP_LOGI(TAG, "animation bundle updated and cache reloaded");
    }
}

bool AnimationStore::FetchRemoteContentLength(const std::string& url, size_t* out_length) {
    if (url.empty() || out_length == nullptr) {
        return false;
    }
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_HEAD;
    cfg.timeout_ms = 10000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        return false;
    }
    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int64_t length = esp_http_client_get_content_length(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200 || length <= 0) {
        return false;
    }
    *out_length = static_cast<size_t>(length);
    return true;
}

bool AnimationStore::DownloadToTempAndSwap(const std::string& url, const char* temp_path, const char* final_path,
                                           bool (AnimationStore::*validator)(const char*), bool skip_same_size) {
    if (!mounted_ || url.empty() || temp_path == nullptr || final_path == nullptr || validator == nullptr) {
        return false;
    }
    if (skip_same_size && FileExists(final_path)) {
        size_t remote_size = 0;
        size_t local_size = FileSize(final_path);
        if (local_size > 0 && FetchRemoteContentLength(url, &remote_size) && remote_size == local_size) {
            ESP_LOGI(TAG, "remote asset unchanged by size: %s", final_path);
            return false;
        }
    }

    FILE* f = fopen(temp_path, "wb");
    if (f == nullptr) {
        ESP_LOGW(TAG, "failed to open temp animation path: %s", temp_path);
        return false;
    }
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = 30000;
    cfg.event_handler = CollectDownload;
    cfg.user_data = f;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        fclose(f);
        remove(temp_path);
        return false;
    }
    esp_http_client_set_header(client, "Accept", "*/*");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    fflush(f);
    fclose(f);
    if (err != ESP_OK || status != 200 || !(this->*validator)(temp_path)) {
        ESP_LOGW(TAG, "remote asset download invalid path=%s err=%s status=%d",
                 final_path, esp_err_to_name(err), status);
        remove(temp_path);
        return false;
    }
    remove(final_path);
    if (rename(temp_path, final_path) != 0) {
        ESP_LOGW(TAG, "failed to swap updated asset into %s", final_path);
        remove(temp_path);
        return false;
    }
    ESP_LOGI(TAG, "updated asset: %s", final_path);
    return true;
}

bool AnimationStore::DownloadStartupGifToTempAndSwap(const std::string& url) {
    ESP_LOGI(TAG, "checking startup.gif from %s", url.c_str());
    return DownloadToTempAndSwap(url, "/sdcard/startup.gif.tmp", ECHOEAR_STARTUP_GIF_PATH,
                                 &AnimationStore::ValidateGifFile, true);
}

bool AnimationStore::DownloadStartupWavToTempAndSwap(const std::string& url) {
    ESP_LOGI(TAG, "checking startup.wav from %s", url.c_str());
    return DownloadToTempAndSwap(url, "/sdcard/startup.wav.tmp", ECHOEAR_STARTUP_WAV_PATH,
                                 &AnimationStore::ValidateWavFile, true);
}

bool AnimationStore::DownloadBundleToTempAndSwap(const std::string& url) {
    ESP_LOGI(TAG, "checking test.bin from %s", url.c_str());
    const char* temp_path = "/sdcard/test.tmp";
    bool downloaded = DownloadToTempAndSwap(url, temp_path, ECHOEAR_TEST_BIN_PATH,
                                            &AnimationStore::ValidateBundleFile, false);
    return downloaded;
}

void AnimationStore::ClearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : cache_) {
        ReleaseLoadedGif(item.second);
    }
    cache_.clear();
}
