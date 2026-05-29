#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "ml307_ssl_transport.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "iot/thing_manager.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "settings.h"
#include "audio_debugger.h"
#include "animation/animation_updater.h"
#include "animation/animation.h"
#include "ssid_manager.h"
#include "wifi_station.h"
#include "display/lcd_display.h"
#include "error_log_uploader.h"

#if CONFIG_USE_AUDIO_PROCESSOR
#include "afe_audio_processor.h"
#else
#include "no_audio_processor.h"
#endif

#if CONFIG_USE_AFE_WAKE_WORD
#include "afe_wake_word.h"
#elif CONFIG_USE_ESP_WAKE_WORD
#include "esp_wake_word.h"
#else
#include "no_wake_word.h"
#endif

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <unistd.h>

#define TAG "Application"

#ifndef DEFAULT_MQTT_ENDPOINT
#define DEFAULT_MQTT_ENDPOINT ""
#endif
#ifndef DEFAULT_MQTT_PUBLISH_TEMPLATE
#define DEFAULT_MQTT_PUBLISH_TEMPLATE "xiaozhi/%s/up"
#endif

namespace {

constexpr size_t kCustomRestoreMinFreeSram = 16 * 1024;
constexpr size_t kCustomRestoreMinLargestBlock = 8 * 1024;
constexpr int kCustomRestoreMemoryWaitMs = 5000;
constexpr int kCustomRestoreMemoryPollMs = 500;
constexpr const char* kOfflineReminderNamespace = "offline_alarm";
constexpr const char* kOfflineReminderDefaultPath = "/sdcard/ALARM.WAV";
constexpr const char* kOfflineReminderTempPath = "/sdcard/ALARM.TMP";
constexpr int kOfflineReminderMicWindowMs = 5000;
constexpr int kOfflineReminderMicThreshold = 1200;
constexpr size_t kOfflineReminderMaxBytes = 2 * 1024 * 1024;

void ShowStartupProgressOverlay(const char* title, int progress, const char* detail = nullptr) {
    auto* display = Board::GetInstance().GetDisplay();
    auto* lcd_display = static_cast<LcdDisplay*>(display);
    if (lcd_display != nullptr) {
        lcd_display->CreateOverlayProgress(title, progress, detail);
    }
}

void ClearStartupProgressOverlay() {
    auto* display = Board::GetInstance().GetDisplay();
    auto* lcd_display = static_cast<LcdDisplay*>(display);
    if (lcd_display != nullptr) {
        lcd_display->ClearOverlayMessage();
    }
}

void SetStartupVisualLock(bool locked) {
    auto* display = Board::GetInstance().GetDisplay();
    auto* lcd_display = static_cast<LcdDisplay*>(display);
    if (lcd_display != nullptr) {
        lcd_display->SetStartupVisualLock(locked);
    }
}

bool CustomRestoreMemoryReady(bool log_status) {
    const size_t free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    const size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const bool ready = free_sram >= kCustomRestoreMinFreeSram &&
                       largest_block >= kCustomRestoreMinLargestBlock;

    if (log_status) {
        ESP_LOGI(TAG,
                 "[PWR_SAVE] Restore memory check: free_sram=%u min_sram=%u largest_block=%u ready=%s",
                 static_cast<unsigned>(free_sram),
                 static_cast<unsigned>(min_free_sram),
                 static_cast<unsigned>(largest_block),
                 ready ? "yes" : "no");
    }

    return ready;
}

}  // namespace

static bool DownloadFileFromUrl(const std::string& url, const std::string& path, size_t max_bytes)
{
    auto& board = Board::GetInstance();
    std::unique_ptr<Http> http(board.CreateHttp());
    if (!http) {
        ESP_LOGE(TAG, "[RTC_ALARM] Failed to create HTTP client for %s", url.c_str());
        return false;
    }

    http->SetHeader("Accept", "audio/wav,application/octet-stream");
    http->SetHeader("Accept-Encoding", "identity");
    http->SetTimeout(15000);

    ESP_LOGI(TAG, "[RTC_ALARM] Downloading offline reminder WAV: %s", url.c_str());
    if (!http->Open("GET", url)) {
        ESP_LOGE(TAG, "[RTC_ALARM] HTTP open failed for offline reminder WAV");
        return false;
    }

    int status = http->GetStatusCode();
    if (status != 200) {
        ESP_LOGE(TAG, "[RTC_ALARM] Offline reminder WAV HTTP status %d", status);
        http->Close();
        return false;
    }

    std::string tmp_path = kOfflineReminderTempPath;
    FILE* file = fopen(tmp_path.c_str(), "wb");
    if (!file) {
        ESP_LOGE(TAG, "[RTC_ALARM] Failed to open %s for offline reminder download: errno=%d (%s)",
                 tmp_path.c_str(),
                 errno,
                 strerror(errno));
        http->Close();
        return false;
    }

    constexpr size_t kBufferSize = 1024;
    std::unique_ptr<char[]> buffer(new char[kBufferSize]);
    size_t total = 0;
    bool ok = true;
    while (true) {
        int n = http->Read(buffer.get(), kBufferSize);
        if (n < 0) {
            ok = false;
            ESP_LOGE(TAG, "[RTC_ALARM] HTTP read failed while downloading offline reminder");
            break;
        }
        if (n == 0) {
            break;
        }
        total += static_cast<size_t>(n);
        if (total > max_bytes) {
            ok = false;
            ESP_LOGE(TAG, "[RTC_ALARM] Offline reminder WAV exceeds limit: %u > %u",
                     static_cast<unsigned>(total),
                     static_cast<unsigned>(max_bytes));
            break;
        }
        if (fwrite(buffer.get(), 1, static_cast<size_t>(n), file) != static_cast<size_t>(n)) {
            ok = false;
            ESP_LOGE(TAG, "[RTC_ALARM] Failed writing offline reminder WAV");
            break;
        }
    }

    fclose(file);
    http->Close();

    if (!ok || total == 0) {
        remove(tmp_path.c_str());
        ESP_LOGE(TAG, "[RTC_ALARM] Offline reminder download failed, bytes=%u", static_cast<unsigned>(total));
        return false;
    }

    remove(path.c_str());
    if (rename(tmp_path.c_str(), path.c_str()) != 0) {
        remove(tmp_path.c_str());
        ESP_LOGE(TAG, "[RTC_ALARM] Failed to move offline reminder WAV into place");
        return false;
    }

    ESP_LOGI(TAG, "[RTC_ALARM] Offline reminder WAV saved to %s (%u bytes)",
             path.c_str(),
             static_cast<unsigned>(total));
    return true;
}

static bool DetectRawMicActivity(int window_ms, int threshold)
{
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec == nullptr) {
        return false;
    }

    codec->EnableInput(true);
    const int input_sample_rate = codec->input_sample_rate();
    const int channels = std::max(1, codec->input_channels());
    const int chunk_ms = 60;
    const int samples_per_channel = std::max(1, input_sample_rate * chunk_ms / 1000);
    const int iterations = std::max(1, window_ms / chunk_ms);
    std::vector<int16_t> data(samples_per_channel * channels);

    for (int i = 0; i < iterations; ++i) {
        if (!codec->InputData(data)) {
            vTaskDelay(pdMS_TO_TICKS(chunk_ms));
            continue;
        }

        int64_t sum_abs = 0;
        int peak = 0;
        int count = 0;
        for (size_t j = 0; j < data.size(); j += channels) {
            int sample = data[j];
            int abs_sample = sample < 0 ? -sample : sample;
            sum_abs += abs_sample;
            peak = std::max(peak, abs_sample);
            ++count;
        }

        int avg = count > 0 ? static_cast<int>(sum_abs / count) : 0;
        if (peak >= threshold * 3 || avg >= threshold) {
            ESP_LOGI(TAG, "[RTC_ALARM] Mic activity detected after local reminder: avg=%d peak=%d", avg, peak);
            return true;
        }
    }

    ESP_LOGI(TAG, "[RTC_ALARM] No mic activity detected after %d ms", window_ms);
    return false;
}

// Minimal WAV-from-HTTP player for POC: expects mono 16-bit PCM at codec sample rate
static bool PlayWavFromUrl(const std::string &url, float gain)
{
    auto &board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (!codec)
    {
        ESP_LOGE(TAG, "Audio codec not available");
        return false;
    }

    std::unique_ptr<Http> http(board.CreateHttp());
    if (!http)
    {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return false;
    }
    http->SetHeader("Accept", "audio/wav,application/octet-stream");
    http->SetHeader("Accept-Encoding", "identity");
    http->SetTimeout(10000);

    ESP_LOGI(TAG, "Opening URL: %s", url.c_str());
    if (!http->Open("GET", url))
    {
        ESP_LOGE(TAG, "HTTP open failed");
        return false;
    }
    int status = http->GetStatusCode();
    if (status != 200)
    {
        ESP_LOGE(TAG, "HTTP status %d", status);
        http->Close();
        return false;
    }

    // Naively skip 44-byte WAV header for PCM WAV
    int to_skip = 44;
    char hdr[64];
    while (to_skip > 0)
    {
        int n = http->Read(hdr, to_skip > (int)sizeof(hdr) ? (int)sizeof(hdr) : to_skip);
        if (n <= 0)
        {
            break;
        }
        to_skip -= n;
    }

    // Stream PCM frames -> codec
    const size_t BUF = 1024;
    std::unique_ptr<uint8_t[]> buf(new uint8_t[BUF]);
    bool have_leftover = false;
    uint8_t leftover = 0;
    gain = (gain <= 0.0f) ? 1.0f : gain;
    codec->EnableOutput(true);

    while (true)
    {
        int n = http->Read(reinterpret_cast<char *>(buf.get()), BUF);
        if (n <= 0)
        {
            break;
        }

        size_t offset = 0;
        std::vector<int16_t> samples;
        samples.reserve(n / 2 + 1);

        // Handle odd byte from previous chunk
        if (have_leftover)
        {
            int16_t s = (int16_t)((uint16_t)buf[offset] << 8 | leftover);
            int32_t v = (int32_t)(s * gain);
            if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
            samples.push_back((int16_t)v);
            offset += 1;
            have_leftover = false;
        }

        // Convert little-endian 16-bit to int16_t, apply gain
        for (; offset + 1 < (size_t)n; offset += 2)
        {
            int16_t s = (int16_t)((uint16_t)buf[offset + 1] << 8 | buf[offset]);
            int32_t v = (int32_t)(s * gain);
            if (v > 32767)
                v = 32767;
            else if (v < -32768)
                v = -32768;
            samples.push_back((int16_t)v);
        }

        // Save leftover byte if odd length
        if (offset < (size_t)n)
        {
            leftover = buf[offset];
            have_leftover = true;
        }

        if (!samples.empty())
        {
            codec->OutputData(samples);
        }
    }

    http->Close();
    return true;
}

static uint16_t ReadUint16LE(FILE *file, bool &ok)
{
    uint8_t bytes[2];
    if (fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) {
        ok = false;
        return 0;
    }
    return static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
}

static uint32_t ReadUint32LE(FILE *file, bool &ok)
{
    uint8_t bytes[4];
    if (fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) {
        ok = false;
        return 0;
    }
    return static_cast<uint32_t>(bytes[0] | (bytes[1] << 8) |
                                (bytes[2] << 16) | (bytes[3] << 24));
}

// Play a local WAV file from SD card during startup.
// Expects PCM RIFF/WAVE, 16-bit PCM, and channels that can be mixed to mono.
static bool PlayWavFromSdCard(const std::string& path, float gain) {
    auto &board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    if (!codec) {
        ESP_LOGE(TAG, "Audio codec not available");
        return false;
    }
    constexpr TickType_t WaitPerPoll = pdMS_TO_TICKS(100);
    constexpr int MaxPolls = 30;
    for (int i = 0; i < MaxPolls; ++i) {
        if (access(path.c_str(), F_OK) == 0) {
            break;
        }
        vTaskDelay(WaitPerPoll);
    }
    if (access(path.c_str(), F_OK) != 0) {
        ESP_LOGW(TAG, "Startup WAV not found at %s", path.c_str());
        return false;
    }

    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        ESP_LOGW(TAG, "Failed to open startup WAV: %s", path.c_str());
        return false;
    }

    bool ok = true;
    char tag[4];
    if (fread(tag, 1, sizeof(tag), file) != sizeof(tag) || memcmp(tag, "RIFF", sizeof(tag)) != 0) {
        ESP_LOGE(TAG, "startup.wav invalid header: missing RIFF");
        fclose(file);
        return false;
    }
    (void)ReadUint32LE(file, ok); // total_size (not needed)
    if (!ok || fread(tag, 1, sizeof(tag), file) != sizeof(tag) ||
        memcmp(tag, "WAVE", sizeof(tag)) != 0) {
        ESP_LOGE(TAG, "startup.wav invalid header: missing WAVE");
        fclose(file);
        return false;
    }

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    bool found_fmt = false;
    bool found_data = false;
    uint32_t data_size = 0;

    while (true) {
        if (fread(tag, 1, sizeof(tag), file) != sizeof(tag)) {
            break;
        }
        uint32_t chunk_size = ReadUint32LE(file, ok);
        if (!ok) {
            break;
        }
        if (memcmp(tag, "fmt ", sizeof(tag)) == 0) {
            audio_format = ReadUint16LE(file, ok);
            channels = ReadUint16LE(file, ok);
            sample_rate = ReadUint32LE(file, ok);
            (void)ReadUint32LE(file, ok); // byte_rate
            (void)ReadUint16LE(file, ok); // block_align
            bits_per_sample = ReadUint16LE(file, ok);

            if (!ok || audio_format != 1) {
                ESP_LOGE(TAG, "startup.wav must be PCM format");
                fclose(file);
                return false;
            }
            if (bits_per_sample != 16) {
                ESP_LOGE(TAG, "startup.wav must be 16-bit PCM");
                fclose(file);
                return false;
            }
            if (channels == 0 || channels > 2) {
                ESP_LOGE(TAG, "startup.wav supports 1 or 2 channels only");
                fclose(file);
                return false;
            }
            if (sample_rate != static_cast<uint32_t>(codec->output_sample_rate())) {
                ESP_LOGW(TAG, "startup.wav sample rate %u does not match codec output %d",
                         sample_rate, codec->output_sample_rate());
            }
            found_fmt = true;
            if (chunk_size > 16) {
                if (fseek(file, static_cast<long>(chunk_size - 16), SEEK_CUR) != 0) {
                    fclose(file);
                    return false;
                }
            }
        } else if (memcmp(tag, "data", sizeof(tag)) == 0) {
            data_size = chunk_size;
            found_data = true;
            break;
        } else {
            if (fseek(file, static_cast<long>(chunk_size), SEEK_CUR) != 0) {
                fclose(file);
                return false;
            }
        }
        if (chunk_size & 1) {
            if (fseek(file, 1, SEEK_CUR) != 0) {
                break;
            }
        }
    }

    if (!found_fmt || !found_data || data_size == 0) {
        ESP_LOGE(TAG, "startup.wav missing required fmt/data chunks");
        fclose(file);
        return false;
    }

    codec->EnableOutput(true);
    gain = (gain <= 0.0f) ? 1.0f : gain;

    constexpr size_t kReadBufferSize = 2048;
    std::vector<uint8_t> read_buf(kReadBufferSize);
    std::vector<int16_t> samples;
    samples.reserve(2048);
    uint32_t frame_size = static_cast<uint32_t>(channels) * sizeof(int16_t);
    uint32_t remaining = data_size;
    uint64_t emitted_samples = 0;
    bool decode_ok = false;
    constexpr uint32_t kTailMarginMs = 60;

    while (remaining > 0) {
        size_t request = sizeof(read_buf);
        if (remaining < request) {
            request = remaining;
        }
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
                int16_t s = static_cast<int16_t>(read_buf[sample_offset] |
                                                (read_buf[sample_offset + 1] << 8));
                sample_sum += s;
            }
            sample_sum /= channels;
            int32_t scaled = static_cast<int32_t>(sample_sum * gain);
            if (scaled > 32767) {
                scaled = 32767;
            } else if (scaled < -32768) {
                scaled = -32768;
            }
            samples.push_back(static_cast<int16_t>(scaled));
            offset += frame_size;
            ++emitted_samples;
            if (samples.size() >= 256) {
                codec->OutputData(samples);
                samples.clear();
            }
        }
        decode_ok = true;
    }

    if (!samples.empty()) {
        codec->OutputData(samples);
    }
    fclose(file);
    if (decode_ok && emitted_samples > 0) {
        const uint32_t output_sample_rate = static_cast<uint32_t>(codec->output_sample_rate());
        const uint32_t playback_ms = static_cast<uint32_t>(
            (emitted_samples * 1000ULL) / output_sample_rate);
        vTaskDelay(pdMS_TO_TICKS(playback_ms + kTailMarginMs));
    }

    if (!decode_ok || emitted_samples == 0) {
        ESP_LOGW(TAG, "No PCM frames emitted from startup.wav");
        return false;
    }

    ESP_LOGI(TAG, "Played %llu samples from %s", emitted_samples, path.c_str());
    return true;
}

static const char *const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"};

Application::Application()
{
    event_group_ = xEventGroupCreate();
    background_task_ = new BackgroundTask(4096 * 7);

#if CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

#if CONFIG_USE_AUDIO_PROCESSOR
    audio_processor_ = std::make_unique<AfeAudioProcessor>();
#else
    audio_processor_ = std::make_unique<NoAudioProcessor>();
#endif

#if CONFIG_USE_AFE_WAKE_WORD
    wake_word_ = std::make_unique<AfeWakeWord>();
#elif CONFIG_USE_ESP_WAKE_WORD
    wake_word_ = std::make_unique<EspWakeWord>();
#else
    wake_word_ = std::make_unique<NoWakeWord>();
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void *arg)
        {
            Application *app = (Application *)arg;
            app->OnClockTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true};
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);

    esp_timer_create_args_t rtc_reminder_timer_args = {
        .callback = [](void *arg)
        {
            Application *app = (Application *)arg;
            app->OnRtcReminderSoftwareTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "rtc_reminder",
        .skip_unhandled_events = true};
    esp_timer_create(&rtc_reminder_timer_args, &rtc_reminder_timer_handle_);
}

Application::~Application()
{
    if (clock_timer_handle_ != nullptr)
    {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    if (rtc_reminder_timer_handle_ != nullptr)
    {
        esp_timer_stop(rtc_reminder_timer_handle_);
        esp_timer_delete(rtc_reminder_timer_handle_);
    }
    if (background_task_ != nullptr)
    {
        delete background_task_;
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckNewVersion()
{
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    while (true)
    {
        SetDeviceState(kDeviceStateActivating);
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota_.CheckVersion())
        {
            retry_count++;
            if (retry_count >= MAX_RETRY)
            {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[128];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota_.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "sad", Lang::Sounds::P3_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++)
            {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle)
                {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota_.HasNewVersion())
        {
            if (Board::GetInstance().GetBoardType() == "echoear") {
                display->SetStatus(Lang::Strings::OTA_UPGRADE);
                ShowStartupProgressOverlay("System upgrading", 0);
                ResetDecoder();
                PlaySound(Lang::Sounds::P3_UPGRADE);
            } else {
                Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "happy", Lang::Sounds::P3_UPGRADE);
            }

            vTaskDelay(pdMS_TO_TICKS(3000));

            SetDeviceState(kDeviceStateUpgrading);

            // display->SetIcon(FONT_AWESOME_DOWNLOAD);
            std::string message = std::string(Lang::Strings::NEW_VERSION) + ota_.GetFirmwareVersion();
            // DISABLED: Comment out transcript display to reduce memory usage
            // display->SetChatMessage("system", message.c_str());

            auto &board = Board::GetInstance();
            board.SetPowerSaveMode(false);
            wake_word_->StopDetection();
            // 预先关闭音频输出，避免升级过程有音频操作
            auto codec = board.GetAudioCodec();
            codec->EnableInput(false);
            codec->EnableOutput(false);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                audio_decode_queue_.clear();
            }
            background_task_->WaitForCompletion();
            delete background_task_;
            background_task_ = nullptr;
            vTaskDelay(pdMS_TO_TICKS(1000));

            ota_.StartUpgrade([display](int progress, size_t speed)
                              {
                (void)display;
                (void)speed;
                ShowStartupProgressOverlay("System upgrading", progress);
                // DISABLED: Comment out transcript display to reduce memory usage
                // display->SetChatMessage("system", buffer); 
                });

            // If upgrade success, the device will reboot and never reach here
            display->SetStatus(Lang::Strings::UPGRADE_FAILED);
            ESP_LOGI(TAG, "Firmware upgrade failed...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            Reboot();
            return;
        }

        // No new version, mark the current version as valid
        ota_.MarkCurrentVersionValid();
        if (!ota_.HasActivationCode() && !ota_.HasActivationChallenge())
        {
            xEventGroupSetBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_.HasActivationCode())
        {
            ShowActivationCode();
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i)
        {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_.Activate();
            if (err == ESP_OK)
            {
                xEventGroupSetBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT);
                break;
            }
            else if (err == ESP_ERR_TIMEOUT)
            {
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle)
            {
                break;
            }
        }
    }
}

void Application::ShowActivationCode()
{
    auto &message = ota_.GetActivationMessage();
    auto &code = ota_.GetActivationCode();

    struct digit_sound
    {
        char digit;
        const std::string_view &sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{digit_sound{'0', Lang::Sounds::P3_0},
                                                           digit_sound{'1', Lang::Sounds::P3_1},
                                                           digit_sound{'2', Lang::Sounds::P3_2},
                                                           digit_sound{'3', Lang::Sounds::P3_3},
                                                           digit_sound{'4', Lang::Sounds::P3_4},
                                                           digit_sound{'5', Lang::Sounds::P3_5},
                                                           digit_sound{'6', Lang::Sounds::P3_6},
                                                           digit_sound{'7', Lang::Sounds::P3_7},
                                                           digit_sound{'8', Lang::Sounds::P3_8},
                                                           digit_sound{'9', Lang::Sounds::P3_9}}};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "happy", Lang::Sounds::P3_ACTIVATION);

    for (const auto &digit : code)
    {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
                               [digit](const digit_sound &ds)
                               { return ds.digit == digit; });
        if (it != digit_sounds.end())
        {
            PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char *status, const char *message, const char *emotion, const std::string_view &sound)
{
    if (custom_power_save_mode_.load(std::memory_order_acquire) || Board::GetInstance().IsSleepTransitionActive())
    {
        ESP_LOGI(TAG, "Suppressing alert while custom power-save mode is active");
        return;
    }

    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    // DISABLED: Comment out transcript display to reduce memory usage
    // display->SetChatMessage("system", message);
    if (!sound.empty())
    {
        ResetDecoder();
        PlaySound(sound);
    }
}

void Application::DismissAlert()
{
    if (custom_power_save_mode_.load(std::memory_order_acquire) || Board::GetInstance().IsSleepTransitionActive())
    {
        return;
    }

    if (device_state_ == kDeviceStateIdle)
    {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("normal");
        // DISABLED: Comment out transcript display to reduce memory usage
        // display->SetChatMessage("system", "");
    }
}

void Application::PlaySound(const std::string_view &sound)
{
    if (custom_power_save_mode_.load(std::memory_order_acquire) || Board::GetInstance().IsSleepTransitionActive())
    {
        ESP_LOGI(TAG, "Suppressing sound while custom power-save mode is active");
        return;
    }

    // Wait for the previous sound to finish
    {
        std::unique_lock<std::mutex> lock(mutex_);
        audio_decode_cv_.wait(lock, [this]()
                              { return audio_decode_queue_.empty(); });
    }
    background_task_->WaitForCompletion();

    const char *data = sound.data();
    size_t size = sound.size();
    for (const char *p = data; p < data + size;)
    {
        auto p3 = (BinaryProtocol3 *)p;
        p += sizeof(BinaryProtocol3);

        auto payload_size = ntohs(p3->payload_size);
        AudioStreamPacket packet;
        packet.sample_rate = 16000;
        packet.frame_duration = 60;
        packet.payload.resize(payload_size);
        memcpy(packet.payload.data(), p3->payload, payload_size);
        p += payload_size;

        std::lock_guard<std::mutex> lock(mutex_);
        audio_decode_queue_.emplace_back(std::move(packet));
    }
}

void Application::EnterAudioTestingMode()
{
    ESP_LOGI(TAG, "Entering audio testing mode");
    ResetDecoder();
    SetDeviceState(kDeviceStateAudioTesting);
}

void Application::ExitAudioTestingMode()
{
    ESP_LOGI(TAG, "Exiting audio testing mode");
    SetDeviceState(kDeviceStateWifiConfiguring);
    // Copy audio_testing_queue_ to audio_decode_queue_
    std::lock_guard<std::mutex> lock(mutex_);
    audio_decode_queue_ = std::move(audio_testing_queue_);
    audio_decode_cv_.notify_all();
}

void Application::ToggleChatState()
{
    if (custom_power_save_restore_in_progress_.load(std::memory_order_acquire))
    {
        ESP_LOGI(TAG, "[PWR_SAVE] Ignoring talk request while custom wake restore is in progress");
        return;
    }
    if (!custom_power_save_restore_memory_ready_.load(std::memory_order_acquire))
    {
        if (!CustomRestoreMemoryReady(true)) {
            ESP_LOGW(TAG, "[PWR_SAVE] Ignoring talk request until custom wake SRAM gate is ready");
            return;
        }
        custom_power_save_restore_memory_ready_.store(true, std::memory_order_release);
        ESP_LOGI(TAG, "[PWR_SAVE] Custom wake SRAM gate recovered; talk request allowed");
    }

    if (device_state_ == kDeviceStateActivating)
    {
        SetDeviceState(kDeviceStateIdle);
        return;
    }
    else if (device_state_ == kDeviceStateWifiConfiguring)
    {
        EnterAudioTestingMode();
        return;
    }
    else if (device_state_ == kDeviceStateAudioTesting)
    {
        ExitAudioTestingMode();
        return;
    }

    if (!protocol_)
    {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle)
    {
        auto &wifi_station = WifiStation::GetInstance();
        if (!wifi_station.IsConnected())
        {
            // Disconnected interaction UX:
            // - First press: show WiFi face reminder and try connect flow.
            // - Second press: exit reminder back to normal/idle.
            if (wifi_error_reminder_active_)
            {
                auto display = Board::GetInstance().GetDisplay();
                if (display != nullptr)
                {
                    display->SetEmotion("normal");
                }
                wifi_error_reminder_active_ = false;
                return;
            }

            // If user starts interaction while disconnected, show WiFi animation as
            // an immediate visual reminder of the network error condition.
            auto display = Board::GetInstance().GetDisplay();
            if (display != nullptr)
            {
                display->SetEmotion("wifi");
            }
            wifi_error_reminder_active_ = true;
        }
        else
        {
            wifi_error_reminder_active_ = false;
        }

        Schedule([this]()
                 {
            auto* active_protocol = GetActiveProtocol();
            if (!active_protocol->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                
                // Always use WebSocket for manual interactions (with default fallback URL)
                // WebSocket protocol will use default URL if none is configured
                if (!websocket_protocol_ || !websocket_protocol_->IsAudioChannelOpened()) {
                    ESP_LOGI(TAG, "Opening WebSocket connection for manual interaction (will use default URL if needed)");
                    OpenWebSocketConnection();
                    active_protocol = GetActiveProtocol();
                }
                
                // If WebSocket failed, fall back to primary protocol (MQTT)
                if (!active_protocol->IsAudioChannelOpened()) {
                    ESP_LOGW(TAG, "WebSocket connection failed, falling back to primary protocol");
                    if (!active_protocol->OpenAudioChannel()) {
                        return;
                    }
                }
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime); });
    }
    else if (device_state_ == kDeviceStateSpeaking)
    {
        Schedule([this]()
                 { 
                     ESP_LOGI(TAG, "Boot button pressed during speaking - forcefully closing audio channel");
                     
                     // Immediately close the audio channel to stop all audio operations
                     auto* active_protocol = GetActiveProtocol();
                     if (active_protocol && active_protocol->IsAudioChannelOpened()) {
                         ESP_LOGI(TAG, "Closing audio channel immediately");
                         active_protocol->CloseAudioChannel();
                     }
                     
                     // Clear all pending audio decode queue
                     {
                         std::lock_guard<std::mutex> lock(mutex_);
                         audio_decode_queue_.clear();
                         audio_decode_cv_.notify_all();
                     }
                     
                     // Reset decoder state
                     ResetDecoder();
                     
                     // Set abort flag and reset state
                     aborted_ = true;
                     state_before_tts_ = kDeviceStateUnknown;
                     
                     // Force immediate transition to idle - no waiting for server
                     ESP_LOGI(TAG, "Forcefully transitioning to idle state");
                     SetDeviceState(kDeviceStateIdle);
                     
                     // Reset abort flag after state change
                     aborted_ = false;
                 });
    }
    else if (device_state_ == kDeviceStateListening)
    {
        Schedule([this]()
                 { 
                     auto* active_protocol = GetActiveProtocol();
                     if (active_protocol) {
                         active_protocol->CloseAudioChannel();
                     }
                 });
    }
}

void Application::StartListening()
{
    if (device_state_ == kDeviceStateActivating)
    {
        SetDeviceState(kDeviceStateIdle);
        return;
    }
    else if (device_state_ == kDeviceStateWifiConfiguring)
    {
        EnterAudioTestingMode();
        return;
    }

    if (!protocol_)
    {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle)
    {
        Schedule([this]()
                 {
            auto* active_protocol = GetActiveProtocol();
            if (!active_protocol->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                
                // Always use WebSocket for manual interactions (with default fallback URL)
                // WebSocket protocol will use default URL if none is configured
                if (!websocket_protocol_ || !websocket_protocol_->IsAudioChannelOpened()) {
                    ESP_LOGI(TAG, "Opening WebSocket connection for manual interaction (will use default URL if needed)");
                    OpenWebSocketConnection();
                    active_protocol = GetActiveProtocol();
                }
                
                // If WebSocket failed, fall back to primary protocol (MQTT)
                if (!active_protocol->IsAudioChannelOpened()) {
                    ESP_LOGW(TAG, "WebSocket connection failed, falling back to primary protocol");
                    if (!active_protocol->OpenAudioChannel()) {
                        return;
                    }
                }
            }

            SetListeningMode(kListeningModeManualStop); });
    }
    else if (device_state_ == kDeviceStateSpeaking)
    {
        Schedule([this]()
                 {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop); });
    }
}

void Application::StopListening()
{
    if (device_state_ == kDeviceStateAudioTesting)
    {
        ExitAudioTestingMode();
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end())
    {
        return;
    }

    Schedule([this]()
             {
        if (device_state_ == kDeviceStateListening) {
            auto* active_protocol = GetActiveProtocol();
            if (active_protocol) {
                ESP_LOGI(TAG, "Sending listen stop message");
                active_protocol->SendStopListening();
            }
            SetDeviceState(kDeviceStateIdle);
        } });
}

void Application::Start()
{
    auto &board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    /* Setup the audio codec */
    auto codec = board.GetAudioCodec();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(codec->output_sample_rate(), 1, OPUS_FRAME_DURATION_MS);
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    if (aec_mode_ != kAecOff)
    {
        ESP_LOGI(TAG, "AEC mode: %d, setting opus encoder complexity to 0", aec_mode_);
        opus_encoder_->SetComplexity(0);
    }
    else if (board.GetBoardType() == "ml307")
    {
        ESP_LOGI(TAG, "ML307 board detected, setting opus encoder complexity to 5");
        opus_encoder_->SetComplexity(5);
    }
    else
    {
        ESP_LOGI(TAG, "WiFi board detected, setting opus encoder complexity to 0");
        opus_encoder_->SetComplexity(0);
    }

    if (codec->input_sample_rate() != 16000)
    {
        input_resampler_.Configure(codec->input_sample_rate(), 16000);
        reference_resampler_.Configure(codec->input_sample_rate(), 16000);
    }
    codec->Start();

#ifdef CONFIG_BOARD_TYPE_ECHOEAR
    animation_block_startup_load(true);
    if (!PlayWavFromSdCard("/sdcard/startup.wav", 1.0f)) {
        ESP_LOGW(TAG, "startup.wav playback skipped or failed");
    } else {
        ESP_LOGI(TAG, "startup.wav playback finished");
    }
    HandlePendingRtcReminderOnBoot();
#endif

#if CONFIG_USE_AUDIO_PROCESSOR
    xTaskCreatePinnedToCore([](void *arg)
                            {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL); }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_, 1);
#else
    xTaskCreate([](void *arg)
                {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL); }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_);
#endif

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Check if we should clear WiFi configuration to force nimBLE setup */
    // Only clear WiFi if no credentials exist (first boot or manual clearing)
    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    if (ssid_list.empty()) {
        ESP_LOGI(TAG, "No WiFi credentials found, nimBLE will start for configuration");
    } else {
        ESP_LOGI(TAG, "WiFi credentials found (%d networks), proceeding with normal startup", ssid_list.size());
    }

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);

    // Check WiFi connection status and show message if not connected
    // Note: If no WiFi credentials exist, StartNetwork() will block in WiFi config mode,
    // so this code only runs if WiFi credentials exist but connection failed or is in progress
    if (board.GetBoardType() != "ml307") {
        ESP_LOGI(TAG, "Checking WiFi connection status...");
        auto& wifi_station = WifiStation::GetInstance();
        bool is_connected = wifi_station.IsConnected();
        ESP_LOGI(TAG, "WiFi connection status: %s", is_connected ? "CONNECTED" : "NOT CONNECTED");
        ESP_LOGI(TAG, "Device state: %d (kDeviceStateWifiConfiguring=%d)", device_state_, kDeviceStateWifiConfiguring);
        
        if (!is_connected) {
            // This branch only runs when WiFi credentials already exist but
            // the connection is not up yet (or failed). In that case we do NOT
            // show the "Connect me to wifi with BabyMilu App..." onboarding
            // message -- wifi.gif stays on screen instead. The onboarding
            // message is shown only for brand-new devices with no credentials,
            // which is handled inside WifiBoard::StartNetwork().
            ESP_LOGI(TAG, "WiFi not connected but credentials exist; keeping wifi.gif on screen");
        } else {
            ESP_LOGI(TAG, "WiFi is connected, checking animation availability...");
            // WiFi is connected, check if animation is available
            Animation_t* current_anim = animation_get_normal_animation();
            ESP_LOGI(TAG, "Animation check: current_anim=%p", current_anim);
            if (current_anim != NULL) {
                ESP_LOGI(TAG, "Animation available: len=%d", current_anim->len);
            }
            
            if (current_anim == NULL || current_anim->len == 0) {
                ESP_LOGI(TAG, "No animation available, showing connected message");
                // No animation available, show connected message (display in center of screen)
                const char* connected_message = "Connected! I am traveling over :D";
                
                // Try to use LcdDisplay::CreateSystemMessage if available
                LcdDisplay* lcd_display = static_cast<LcdDisplay*>(display);
                if (lcd_display != nullptr) {
                    ESP_LOGI(TAG, "Display is LcdDisplay, using CreateSystemMessage for connected message");
                    lcd_display->CreateSystemMessage(connected_message);
                }
                
                // Also try standard methods as fallback
                display->SetChatMessage("system", connected_message);
                ESP_LOGI(TAG, "Called SetChatMessage with connected message");
                
                // Also try ShowNotification as fallback
                vTaskDelay(pdMS_TO_TICKS(100));
                display->ShowNotification(connected_message, 0);
                ESP_LOGI(TAG, "Called ShowNotification with connected message");
            } else {
                ESP_LOGI(TAG, "Animation is available, not showing connected message");
            }
        }
    } else {
        ESP_LOGI(TAG, "ML307 board detected, skipping WiFi message display");
    }

    // Initialize and start the animation updater
    // COMMENTED OUT: Disable automatic animation updater startup
    // Only MQTT remote_anim_update messages will trigger animation updates
    // AnimationUpdater::GetInstance().Initialize();
    // AnimationUpdater::GetInstance().Start();

    // Upload error log before other startup network jobs.
    ESP_LOGI(TAG, "Attempting to upload error log...");
    esp_err_t upload_result = ErrorLogUploader::UploadErrorLog();
    ESP_LOGI(TAG, "Error log upload attempt completed with result: %s", esp_err_to_name(upload_result));

    // After upload attempt (regardless of success/failure), enable error logging to SD card
    // This will capture all subsequent ESP errors and write them to /sdcard/err.txt
    // so they can be uploaded on the next startup
    ESP_LOGI(TAG, "Enabling error logging to SD card for future errors...");
    ErrorLogUploader::EnableErrorLoggingToSD();

    // Test error logging to SD card with sample error messages
    // These test messages will be captured and written to err.txt for verification
    ESP_LOGI(TAG, "Testing error logging to SD card...");
    ESP_LOGW(TAG, "[TEST] Warning: This is a test warning message to verify SD card error logging");
    ESP_LOGE(TAG, "[TEST] Error: This is a test error message to verify SD card error logging");
    ESP_LOGE(TAG, "[TEST] Simulated error condition: File operation failed");
    ESP_LOGW(TAG, "[TEST] Test completed - check /sdcard/err.txt for logged errors");

    // Check for new firmware version or get the MQTT broker address
    CheckNewVersion();

#ifdef CONFIG_BOARD_TYPE_ECHOEAR
    animation_block_startup_load(false);
    SetStartupVisualLock(false);
#endif

    // Check for animation updates after network is confirmed ready
    // Only trigger once after startup when server connection is established
    ESP_LOGI(TAG, "Checking if network is ready for animation update check...");
    auto& board_instance = Board::GetInstance();
    bool network_ready = false;
    
    if (board_instance.GetBoardType() != "ml307") {
        // For WiFi boards, check WiFi connection
        auto& wifi_station = WifiStation::GetInstance();
        network_ready = wifi_station.IsConnected();
        ESP_LOGI(TAG, "WiFi board - connection status: %s", network_ready ? "CONNECTED" : "NOT CONNECTED");
    } else {
        // For ML307 boards, network is ready after CheckNewVersion() succeeds
        // (CheckNewVersion makes HTTP requests, so network must be working)
        network_ready = true;
        ESP_LOGI(TAG, "ML307 board - assuming network ready after CheckNewVersion()");
    }
    
    if (network_ready) {
        ESP_LOGI(TAG, "Network is ready, running startup animation update check...");
        auto& animation_updater = AnimationUpdater::GetInstance();
        animation_updater.Initialize();
        animation_updater.TriggerUpdateLoop();

        const TickType_t wait_step = pdMS_TO_TICKS(500);
        bool saw_running = false;
        for (int i = 0; i < 20; ++i) {
            if (animation_updater.IsRunning()) {
                saw_running = true;
                break;
            }
            vTaskDelay(wait_step);
        }
        if (saw_running) {
            for (int i = 0; i < 240; ++i) {
                if (!animation_updater.IsRunning()) {
                    ESP_LOGI(TAG, "Startup animation update check finished");
                    break;
                }
                vTaskDelay(wait_step);
            }
            if (animation_updater.IsRunning()) {
                ESP_LOGW(TAG, "Timed out waiting for startup animation update check");
            }
        } else {
            ESP_LOGW(TAG, "Startup animation update check did not start");
        }
        ClearStartupProgressOverlay();
    } else {
        ESP_LOGW(TAG, "Network not ready yet, skipping animation update check");
    }

    board_instance.WaitForStartupNetworkTasks();

    // Seed MQTT config on first boot if unset (allows setting broker at build time)
    {
        Settings mqtt_settings("mqtt", true);
        auto endpoint = mqtt_settings.GetString("endpoint");
        if (endpoint.empty() && std::string(DEFAULT_MQTT_ENDPOINT).size() > 0)
        {
            auto mac = SystemInfo::GetMacAddress();
            char up_topic[128];
            snprintf(up_topic, sizeof(up_topic), DEFAULT_MQTT_PUBLISH_TEMPLATE, mac.c_str());
            mqtt_settings.SetString("endpoint", DEFAULT_MQTT_ENDPOINT);
            mqtt_settings.SetString("client_id", mac);
            mqtt_settings.SetString("publish_topic", up_topic);
            // Optional username/password can be added similarly if needed
            ESP_LOGI(TAG, "Seeded MQTT endpoint to %s", DEFAULT_MQTT_ENDPOINT);
        }
    }

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
#if CONFIG_IOT_PROTOCOL_MCP
    McpServer::GetInstance().AddCommonTools();
#endif

    // Protocol initialization strategy:
    // - MQTT (primary): Always connected for control messages (ws_start, wake word, etc.)
    // - WebSocket (on-demand): Created when needed for audio conversations
    // 
    // Both can be active simultaneously:
    // - MQTT: Handles server-initiated conversations (receives ws_start, can handle MQTT+UDP audio)
    // - WebSocket: Handles user-initiated conversations (button press) and server-initiated (via ws_start)
    // 
    // Audio routing: WebSocket takes priority when open, otherwise MQTT is used
    if (ota_.HasMqttConfig())
    {
        protocol_ = std::make_unique<MqttProtocol>();
    }
    else if (ota_.HasWebsocketConfig())
    {
        // If only WebSocket is configured, use it as primary
        protocol_ = std::make_unique<WebsocketProtocol>();
    }
    else
    {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }
    
    // WebSocket protocol will be created on-demand for audio conversations
    // MQTT stays connected for control messages even when WebSocket is active

    protocol_->OnNetworkError([this](const std::string &message)
                              {
        SetDeviceState(kDeviceStateIdle);
        // Use WiFi face for transport/connectivity failures so users get a
        // consistent network-error visual cue.
        Alert(Lang::Strings::ERROR, message.c_str(), "wifi", Lang::Sounds::P3_EXCLAMATION); });
    protocol_->OnIncomingAudio([this](AudioStreamPacket &&packet)
                               {
        std::lock_guard<std::mutex> lock(mutex_);
        if (device_state_ == kDeviceStateSpeaking && audio_decode_queue_.size() < MAX_AUDIO_PACKETS_IN_QUEUE) {
            audio_decode_queue_.emplace_back(std::move(packet));
        } });
    protocol_->OnAudioChannelOpened([this, codec, &board]()
                                    {
                                        board.SetPowerSaveMode(false);
                                        if (protocol_->server_sample_rate() != codec->output_sample_rate())
                                        {
                                            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                                                     protocol_->server_sample_rate(), codec->output_sample_rate());
                                        }

#if CONFIG_IOT_PROTOCOL_XIAOZHI
                                        auto &thing_manager = iot::ThingManager::GetInstance();
                                        protocol_->SendIotDescriptors(thing_manager.GetDescriptorsJson());
                                        std::string states;
                                        if (thing_manager.GetStatesJson(states, false))
                                        {
                                            protocol_->SendIotStates(states);
                                        }
#endif
                                    });
    protocol_->OnAudioChannelClosed([this, &board]()
                                    {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            /* removed unused: display */
            // DISABLED: Comment out transcript display to reduce memory usage
            // display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        }); });
    protocol_->OnIncomingJson([this, display](const cJSON *root)
                              {
        // Parse JSON data
        ESP_LOGI(TAG, "OnIncomingJson: Entry point - message received");
        auto type = cJSON_GetObjectItem(root, "type");
        if (type && cJSON_IsString(type)) {
            ESP_LOGI(TAG, "OnIncomingJson: Message type='%s'", type->valuestring);
        } else {
            ESP_LOGW(TAG, "OnIncomingJson: Message missing or invalid type field");
        }
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        // Save state before TTS to determine if we should resume listening after TTS
                        state_before_tts_ = device_state_;
                        
                        // If we're in LISTENING state, stop listening before TTS starts
                        // This ensures no microphone audio interference during TTS playback
                        if (device_state_ == kDeviceStateListening) {
                            auto* active_protocol = GetActiveProtocol();
                            if (active_protocol && active_protocol->IsAudioChannelOpened()) {
                                ESP_LOGI(TAG, "TTS starting while listening - stopping listening before TTS");
                                active_protocol->SendStopListening();
                                // Small delay to ensure server receives listen:stop before TTS starts
                                vTaskDelay(pdMS_TO_TICKS(50));
                            }
                        }
                        
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    ESP_LOGI(TAG, "TTS stop (primary): state=%d, listening_mode=%d", device_state_, (int)listening_mode_);
                    background_task_->WaitForCompletion();
                    if (device_state_ == kDeviceStateSpeaking) {
                        // Check if user aborted speaking - don't auto-resume listening
                        if (aborted_) {
                            ESP_LOGI(TAG, "TTS stopped after user abort - going to idle without resuming listening");
                            aborted_ = false;  // Reset flag
                            state_before_tts_ = kDeviceStateUnknown;  // Reset
                            SetDeviceState(kDeviceStateIdle);
                            return;  // Early return, don't continue with auto-resume logic
                        }
                        
                        // Check if remote wakeup scenario (WebSocket still open)
                        auto* active_protocol = GetActiveProtocol();
                        bool is_remote_wakeup = (websocket_protocol_ != nullptr && 
                                                websocket_protocol_->IsAudioChannelOpened() &&
                                                active_protocol && active_protocol->IsAudioChannelOpened());
                        
                        if (is_remote_wakeup) {
                            // Automatically resume listening after TTS in remote wakeup scenario
                            if (is_alarm_mode_) {
                                // Alarm mode: Always start listening after TTS (even if wasn't listening before)
                                // This is the expected flow: ws_start → TTS → listening
                                ESP_LOGI(TAG, "TTS stopped, alarm mode - starting listening for user response");
                                SetDeviceState(kDeviceStateListening);
                                is_alarm_mode_ = false; // Reset alarm mode after first TTS completes
                            } else if (state_before_tts_ == kDeviceStateListening) {
                                // Normal remote wakeup: Only resume if we were listening before TTS started
                                ESP_LOGI(TAG, "TTS stopped, remote wakeup detected (WebSocket open) - automatically resuming listening");
                                SetDeviceState(kDeviceStateListening);
                            } else {
                                ESP_LOGI(TAG, "TTS stopped, remote wakeup detected but was not listening before TTS - going to idle");
                                SetDeviceState(kDeviceStateIdle);
                            }
                            state_before_tts_ = kDeviceStateUnknown; // Reset
                        } else if (listening_mode_ == kListeningModeManualStop) {
                            // Go to idle for manual interactions
                            ESP_LOGI(TAG, "TTS stopped, going to idle (manual stop mode)");
                            SetDeviceState(kDeviceStateIdle);
                            state_before_tts_ = kDeviceStateUnknown; // Reset
                        } else {
                            // Auto mode: resume listening
                            ESP_LOGI(TAG, "TTS stopped, automatically resuming listening (auto stop mode)");
                            SetDeviceState(kDeviceStateListening);
                            state_before_tts_ = kDeviceStateUnknown; // Reset
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    // DISABLED: Comment out transcript display to reduce memory usage
                    // Schedule([this, display, message = std::string(text->valuestring)]() {
                    //     display->SetChatMessage("assistant", message.c_str());
                    // });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                // DISABLED: Comment out transcript display to reduce memory usage
                // Schedule([this, display, message = std::string(text->valuestring)]() {
                //     display->SetChatMessage("user", message.c_str());
                // });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
#if CONFIG_IOT_PROTOCOL_MCP
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            ESP_LOGI(TAG, "OnIncomingJson: Received MCP message");
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                char* payload_str = cJSON_PrintUnformatted(payload);
                if (payload_str) {
                    ESP_LOGI(TAG, "OnIncomingJson: MCP payload: %s", payload_str);
                    cJSON_free(payload_str);
                }
                ESP_LOGI(TAG, "OnIncomingJson: Forwarding MCP payload to McpServer::ParseMessage");
                McpServer::GetInstance().ParseMessage(payload);
            } else {
                ESP_LOGW(TAG, "OnIncomingJson: MCP message missing or invalid payload");
            }
#endif
#if CONFIG_IOT_PROTOCOL_XIAOZHI
        } else if (strcmp(type->valuestring, "iot") == 0) {
            auto commands = cJSON_GetObjectItem(root, "commands");
            if (cJSON_IsArray(commands)) {
                auto& thing_manager = iot::ThingManager::GetInstance();
                for (int i = 0; i < cJSON_GetArraySize(commands); ++i) {
                    auto command = cJSON_GetArrayItem(commands, i);
                    thing_manager.Invoke(command);
                }
            }
#endif
        } else if (strcmp(type->valuestring, "listen") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (cJSON_IsString(state)) {
                if (strcmp(state->valuestring, "start") == 0) {
                    ESP_LOGI(TAG, "Received listen:start from server, starting listening");
                    Schedule([this]() { 
                        ESP_LOGI(TAG, "Executing listen:start - setting listening mode (AutoStop)");
                        SetListeningMode(kListeningModeAutoStop); 
                    });
                } else if (strcmp(state->valuestring, "stop") == 0) {
                    ESP_LOGI(TAG, "Received listen:stop from server, stopping listening");
                    Schedule([this]() { StopListening(); });
                } else {
                    ESP_LOGW(TAG, "Received listen message with unknown state: %s", state->valuestring);
                }
            } else {
                ESP_LOGW(TAG, "Received listen message without valid state field");
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::P3_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
        } else if (strcmp(type->valuestring, "play_url") == 0) {
            auto url = cJSON_GetObjectItem(root, "url");
            auto gain = cJSON_GetObjectItem(root, "gain");
            if (cJSON_IsString(url)) {
                float g = cJSON_IsNumber(gain) ? (float)gain->valuedouble : 1.0f;
                // Run playback in background to avoid blocking main loop
                Schedule([this, url_str = std::string(url->valuestring), g]() {
                    ResetDecoder();
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                    background_task_->Schedule([this, url_str, g]() {
                        bool ok = PlayWavFromUrl(url_str, g);
                        Schedule([this, ok]() {
                            if (device_state_ == kDeviceStateSpeaking) {
                                SetDeviceState(kDeviceStateIdle);
                            }
                            ESP_LOGI(TAG, "PlayWavFromUrl %s", ok ? "done" : "failed");
                        });
                    });
                });
            } else {
                ESP_LOGW(TAG, "play_url missing 'url' field");
            }
        } else if (strcmp(type->valuestring, "adjust_volume") == 0) {
            // Handle volume adjustment: accepts "volume" (number) or "message" (number as string)
            auto volume_item = cJSON_GetObjectItem(root, "volume");
            int target_volume = -1;
            
            if (cJSON_IsNumber(volume_item)) {
                target_volume = volume_item->valueint;
            } else {
                // Fallback to "message" field for compatibility with test script pattern
                auto message_item = cJSON_GetObjectItem(root, "message");
                if (cJSON_IsString(message_item)) {
                    // Try to parse message as integer
                    target_volume = atoi(message_item->valuestring);
                } else if (cJSON_IsNumber(message_item)) {
                    target_volume = message_item->valueint;
                }
            }
            
            if (target_volume >= 0 && target_volume <= 100) {
                ESP_LOGI(TAG, "Received adjust_volume command, setting volume to %d", target_volume);
                Schedule([this, target_volume]() {
                    auto codec = Board::GetInstance().GetAudioCodec();
                    if (codec != nullptr) {
                        codec->SetOutputVolume(target_volume);
                        ESP_LOGI(TAG, "Volume adjusted to %d", target_volume);
                    } else {
                        ESP_LOGW(TAG, "Cannot adjust volume: audio codec not available");
                    }
                });
            } else {
                ESP_LOGW(TAG, "adjust_volume requires volume value between 0-100, got: %d", target_volume);
            }
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        } });
    bool protocol_started = protocol_->Start();

    audio_debugger_ = std::make_unique<AudioDebugger>();
    audio_processor_->Initialize(codec);
    audio_processor_->OnOutput([this](std::vector<int16_t> &&data)
                               {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (audio_send_queue_.size() >= MAX_AUDIO_PACKETS_IN_QUEUE) {
                ESP_LOGW(TAG, "Too many audio packets in queue, drop the newest packet");
                return;
            }
        }
        background_task_->Schedule([this, data = std::move(data)]() mutable {
            opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                AudioStreamPacket packet;
                packet.payload = std::move(opus);
#ifdef CONFIG_USE_SERVER_AEC
                {
                    std::lock_guard<std::mutex> lock(timestamp_mutex_);
                    if (!timestamp_queue_.empty()) {
                        packet.timestamp = timestamp_queue_.front();
                        timestamp_queue_.pop_front();
                    } else {
                        packet.timestamp = 0;
                    }

                    if (timestamp_queue_.size() > 3) { // 限制队列长度3
                        timestamp_queue_.pop_front(); // 该包发送前先出队保持队列长度
                        return;
                    }
                }
#endif
                std::lock_guard<std::mutex> lock(mutex_);
                if (audio_send_queue_.size() >= MAX_AUDIO_PACKETS_IN_QUEUE) {
                    ESP_LOGW(TAG, "Too many audio packets in queue, drop the oldest packet");
                    audio_send_queue_.pop_front();
                }
                audio_send_queue_.emplace_back(std::move(packet));
                xEventGroupSetBits(event_group_, SEND_AUDIO_EVENT);
            });
        }); });
    audio_processor_->OnVadStateChange([this](bool speaking)
                                       {
        ESP_LOGD(TAG, "VAD state changed: %s", speaking ? "SPEECH" : "SILENCE");
        voice_detected_ = speaking;
        if (device_state_ == kDeviceStateListening) {
            Schedule([this, speaking]() {
                if (speaking) {
                    voice_detected_ = true;
                } else {
                    voice_detected_ = false;
                }
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            });
        } else if (device_state_ == kDeviceStateSpeaking) {
            // Reset debounce state immediately when VAD goes to silence
            if (!speaking && vad_debounce_active_) {
                vad_debounce_active_ = false;
                ESP_LOGD(TAG, "VAD silence detected during speaking, resetting debounce state");
            }
            // Set event bit for VAD interrupt handling during speaking state
            xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
        } });

    wake_word_->Initialize(codec);
    wake_word_->OnWakeWordDetected([this](const std::string &wake_word)
                                   { Schedule([this, &wake_word]()
                                              {
            if (!protocol_) {
                return;
            }

            if (device_state_ == kDeviceStateIdle) {
                wake_word_->EncodeWakeWordData();

                auto* active_protocol = GetActiveProtocol();
                if (!active_protocol->IsAudioChannelOpened()) {
                    SetDeviceState(kDeviceStateConnecting);
                    
                    // Always use WebSocket for wake word interactions (with default fallback URL)
                    // WebSocket protocol will use default URL if none is configured
                    if (!websocket_protocol_ || !websocket_protocol_->IsAudioChannelOpened()) {
                        ESP_LOGI(TAG, "Opening WebSocket connection for wake word (will use default URL if needed)");
                        OpenWebSocketConnection();
                        active_protocol = GetActiveProtocol();
                    }
                    
                    // If WebSocket failed, fall back to primary protocol (MQTT)
                    if (!active_protocol->IsAudioChannelOpened()) {
                        ESP_LOGW(TAG, "WebSocket connection failed, falling back to primary protocol");
                        if (!active_protocol->OpenAudioChannel()) {
                            wake_word_->StartDetection();
                            return;
                        }
                    }
                }

                ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD
                AudioStreamPacket packet;
                // Encode and send the wake word data to the server
                while (wake_word_->GetWakeWordOpus(packet.payload)) {
                    auto* active_protocol = GetActiveProtocol();
                    if (active_protocol) {
                        active_protocol->SendAudio(packet);
                    }
                }
                // Set the chat state to wake word detected
                // Use primary protocol for wake word detection (MQTT)
                if (protocol_) {
                    protocol_->SendWakeWordDetected(wake_word);
                }
#else
                // Play the pop up sound to indicate the wake word is detected
                // And wait 60ms to make sure the queue has been processed by audio task
                ResetDecoder();
                PlaySound(Lang::Sounds::P3_POPUP);
                vTaskDelay(pdMS_TO_TICKS(60));
#endif
                SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
            } else if (device_state_ == kDeviceStateSpeaking) {
                AbortSpeaking(kAbortReasonWakeWordDetected);
            } else if (device_state_ == kDeviceStateActivating) {
                SetDeviceState(kDeviceStateIdle);
            } }); });
    wake_word_->StartDetection();

    // Wait for the new version check to finish
    xEventGroupWaitBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);
    SetDeviceState(kDeviceStateIdle);

    if (protocol_started)
    {
        // Version display disabled on startup
        // std::string message = std::string(Lang::Strings::VERSION) + ota_.GetCurrentVersion();
        // display->ShowNotification(message.c_str());
        // DISABLED: Comment out transcript display to reduce memory usage
        // display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        ResetDecoder();
        PlaySound(Lang::Sounds::P3_SUCCESS);
    }

    // Print heap stats
    SystemInfo::PrintHeapStats();
    HandlePendingRtcReminderOnBoot();

    // Enter the main event loop
    MainEventLoop();
}

void Application::OnClockTimer()
{
    {
        Settings reminder_settings(kOfflineReminderNamespace, false);
        if (reminder_settings.GetInt("armed", 0) != 0 && Board::GetInstance().PollRtcAlarmFlag()) {
            ESP_LOGI(TAG, "[RTC_ALARM] RTC alarm flag detected from clock poll");
            Schedule([this]() {
                HandleRtcAlarmSignal(false);
            });
            return;
        }
    }

    if (custom_power_save_mode_.load(std::memory_order_acquire) || Board::GetInstance().IsSleepTransitionActive())
    {
        return;
    }

    clock_ticks_++;

    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar();

    {
        Settings reminder_settings(kOfflineReminderNamespace, false);
        if (reminder_settings.GetInt("armed", 0) != 0) {
            if (reminder_settings.GetInt("sw_fb", 1) != 0) {
                const time_t trigger_at = static_cast<time_t>(reminder_settings.GetInt("epoch", 0));
                if (trigger_at > 0 && time(nullptr) >= trigger_at) {
                    ESP_LOGI(TAG, "[RTC_ALARM] Software reminder trigger reached");
                    Schedule([this]() {
                        HandleRtcAlarmSignal(false);
                    });
                }
            }
        }
    }

    // Print the debug info every 10 seconds
    if (clock_ticks_ % 10 == 0)
    {
        // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
        // SystemInfo::PrintTaskList();
        SystemInfo::PrintHeapStats();

        // If we have synchronized server time, set the status to clock "HH:MM" if the device is idle
        if (ota_.HasServerTime())
        {
            if (device_state_ == kDeviceStateIdle)
            {
                Schedule([this]()
                         {
                    // Set status to clock "HH:MM"
                    time_t now = time(NULL);
                    char time_str[64];
                    strftime(time_str, sizeof(time_str), "%H:%M  ", localtime(&now));
                    Board::GetInstance().GetDisplay()->SetStatus(time_str); });
            }
        }
    }
}


// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, SCHEDULE_EVENT);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop()
{
    // Raise the priority of the main event loop to avoid being interrupted by background tasks (which has priority 2)
    vTaskPrioritySet(NULL, 3);

    while (true)
    {
        auto bits = xEventGroupWaitBits(event_group_, SCHEDULE_EVENT | SEND_AUDIO_EVENT | MAIN_EVENT_VAD_CHANGE, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & SEND_AUDIO_EVENT)
        {
            if (custom_power_save_mode_.load(std::memory_order_acquire) || Board::GetInstance().IsSleepTransitionActive())
            {
                std::lock_guard<std::mutex> lock(mutex_);
                audio_send_queue_.clear();
            }
            else
            {
                std::unique_lock<std::mutex> lock(mutex_);
                auto packets = std::move(audio_send_queue_);
                lock.unlock();
                for (auto &packet : packets)
                {
                    auto* active_protocol = GetActiveProtocol();
                    if (!active_protocol || !active_protocol->SendAudio(packet))
                    {
                        break;
                    }
                }
            }
        }

        if (bits & SCHEDULE_EVENT)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto &task : tasks)
            {
                task();
            }
        }

        if (bits & MAIN_EVENT_VAD_CHANGE)
        {
            // Handle VAD interrupt during speaking state with debounce mechanism
            if (device_state_ == kDeviceStateSpeaking && 
                aec_mode_ == kAecOnDeviceSide && 
                voice_detected_)
            {
                int64_t now_us = esp_timer_get_time();
                
                // Grace period: ignore interruptions for first 1 second after speaking starts
                int64_t time_since_speaking_start = now_us - speaking_start_time_us_;
                const int64_t GRACE_PERIOD_US = 1000000; // 1 second
                
                if (time_since_speaking_start >= GRACE_PERIOD_US)
                {
                    // Debounce: require VAD to be active for at least 400ms before interrupting
                    const int64_t DEBOUNCE_DURATION_US = 400000; // 400ms
                    
                    if (!vad_debounce_active_)
                    {
                        // VAD just became active, start debounce timer
                        vad_detected_time_us_ = now_us;
                        vad_debounce_active_ = true;
                        ESP_LOGD(TAG, "VAD detected, starting debounce timer");
                    }
                    else
                    {
                        // VAD still active, check if debounce period has elapsed
                        int64_t vad_duration = now_us - vad_detected_time_us_;
                        if (vad_duration >= DEBOUNCE_DURATION_US)
                        {
                            ESP_LOGI(TAG, "VAD detected real voice during playback (confirmed for %lld ms), interrupting",
                                vad_duration / 1000);
                            vad_debounce_active_ = false; // Reset debounce state
                            Schedule([this]() {
                                AbortSpeaking(kAbortReasonNone);
                                // Switch to listening mode to capture the user's voice
                                SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
                            });
                        }
                        else
                        {
                            ESP_LOGD(TAG, "VAD still in debounce period (%lld ms / %lld ms)",
                                vad_duration / 1000, DEBOUNCE_DURATION_US / 1000);
                        }
                    }
                }
                else
                {
                    ESP_LOGD(TAG, "VAD detected during grace period, ignoring (elapsed: %lld ms)",
                        time_since_speaking_start / 1000);
                }
            }
            else
            {
                // VAD not detected or AEC not enabled, reset debounce state
                vad_debounce_active_ = false;
            }
        }
    }
}

// The Audio Loop is used to input and output audio data
void Application::AudioLoop()
{
    auto codec = Board::GetInstance().GetAudioCodec();
    while (true)
    {
        OnAudioInput();
        if (codec->output_enabled())
        {
            OnAudioOutput();
        }
    }
}

void Application::OnAudioOutput()
{
    if (custom_power_save_mode_.load(std::memory_order_acquire) || Board::GetInstance().IsSleepTransitionActive())
    {
        vTaskDelay(pdMS_TO_TICKS(OPUS_FRAME_DURATION_MS));
        return;
    }

    if (busy_decoding_audio_)
    {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    const int max_silence_seconds = 10;

    std::unique_lock<std::mutex> lock(mutex_);
    if (audio_decode_queue_.empty())
    {
        // Disable the output if there is no audio data for a long time
        if (device_state_ == kDeviceStateIdle)
        {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_output_time_).count();
            if (duration > max_silence_seconds)
            {
                codec->EnableOutput(false);
            }
        }
        return;
    }

    auto packet = std::move(audio_decode_queue_.front());
    audio_decode_queue_.pop_front();
    lock.unlock();
    audio_decode_cv_.notify_all();

    // Synchronize the sample rate and frame duration
    SetDecodeSampleRate(packet.sample_rate, packet.frame_duration);

    busy_decoding_audio_ = true;
    background_task_->Schedule([this, codec, packet = std::move(packet)]() mutable
                               {
        busy_decoding_audio_ = false;
        if (aborted_) {
            return;
        }

        std::vector<int16_t> pcm;
        if (!opus_decoder_->Decode(std::move(packet.payload), pcm)) {
            return;
        }
        // Resample if the sample rate is different
        if (opus_decoder_->sample_rate() != codec->output_sample_rate()) {
            int target_size = output_resampler_.GetOutputSamples(pcm.size());
            std::vector<int16_t> resampled(target_size);
            output_resampler_.Process(pcm.data(), pcm.size(), resampled.data());
            pcm = std::move(resampled);
        }
        codec->OutputData(pcm);
#ifdef CONFIG_USE_SERVER_AEC
        std::lock_guard<std::mutex> lock(timestamp_mutex_);
        timestamp_queue_.push_back(packet.timestamp);
#endif
        last_output_time_ = std::chrono::steady_clock::now(); });
}

void Application::OnAudioInput()
{
    if (Board::GetInstance().IsSleepTransitionActive())
    {
        vTaskDelay(pdMS_TO_TICKS(OPUS_FRAME_DURATION_MS));
        return;
    }

    if (device_state_ == kDeviceStateAudioTesting)
    {
        if (audio_testing_queue_.size() >= AUDIO_TESTING_MAX_DURATION_MS / OPUS_FRAME_DURATION_MS)
        {
            ExitAudioTestingMode();
            return;
        }
        std::vector<int16_t> data;
        int samples = OPUS_FRAME_DURATION_MS * 16000 / 1000;
        if (ReadAudio(data, 16000, samples))
        {
            // For audio testing, extract mono (mic channel) from 2-channel input
            // This matches the official firmware behavior and ensures clear audio quality
            auto codec = Board::GetInstance().GetAudioCodec();
            if (codec->input_channels() == 2)
            {
                // Extract left channel (mic) only for mono encoding
                auto mono_data = std::vector<int16_t>(data.size() / 2);
                for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2)
                {
                    mono_data[i] = data[j];
                }
                data = std::move(mono_data);
            }
            
            background_task_->Schedule([this, data = std::move(data)]() mutable
                                       { opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t> &&opus)
                                                               {
                    AudioStreamPacket packet;
                    packet.payload = std::move(opus);
                    packet.frame_duration = OPUS_FRAME_DURATION_MS;
                    packet.sample_rate = 16000;
                    std::lock_guard<std::mutex> lock(mutex_);
                    audio_testing_queue_.push_back(std::move(packet)); }); });
            return;
        }
    }

    if (wake_word_->IsDetectionRunning())
    {
        std::vector<int16_t> data;
        int samples = wake_word_->GetFeedSize();
        if (samples > 0)
        {
            if (ReadAudio(data, 16000, samples))
            {
                wake_word_->Feed(data);
                return;
            }
        }
    }

    if (audio_processor_->IsRunning())
    {
        std::vector<int16_t> data;
        int samples = audio_processor_->GetFeedSize();
        if (samples > 0)
        {
            if (ReadAudio(data, 16000, samples))
            {
                audio_processor_->Feed(data);
                return;
            }
        }
    }

    vTaskDelay(pdMS_TO_TICKS(OPUS_FRAME_DURATION_MS / 2));
}

bool Application::ReadAudio(std::vector<int16_t> &data, int sample_rate, int samples)
{
    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec->input_enabled())
    {
        return false;
    }

    if (codec->input_sample_rate() != sample_rate)
    {
        data.resize(samples * codec->input_sample_rate() / sample_rate);
        if (!codec->InputData(data))
        {
            return false;
        }
        if (codec->input_channels() == 2)
        {
            auto mic_channel = std::vector<int16_t>(data.size() / 2);
            auto reference_channel = std::vector<int16_t>(data.size() / 2);
            for (size_t i = 0, j = 0; i < mic_channel.size(); ++i, j += 2)
            {
                mic_channel[i] = data[j];
                reference_channel[i] = data[j + 1];
            }
            auto resampled_mic = std::vector<int16_t>(input_resampler_.GetOutputSamples(mic_channel.size()));
            auto resampled_reference = std::vector<int16_t>(reference_resampler_.GetOutputSamples(reference_channel.size()));
            input_resampler_.Process(mic_channel.data(), mic_channel.size(), resampled_mic.data());
            reference_resampler_.Process(reference_channel.data(), reference_channel.size(), resampled_reference.data());
            data.resize(resampled_mic.size() + resampled_reference.size());
            for (size_t i = 0, j = 0; i < resampled_mic.size(); ++i, j += 2)
            {
                data[j] = resampled_mic[i];
                data[j + 1] = resampled_reference[i];
            }
        }
        else
        {
            auto resampled = std::vector<int16_t>(input_resampler_.GetOutputSamples(data.size()));
            input_resampler_.Process(data.data(), data.size(), resampled.data());
            data = std::move(resampled);
        }
    }
    else
    {
        // Resize to accommodate all input channels (mono or stereo)
        data.resize(samples * codec->input_channels());
        if (!codec->InputData(data))
        {
            return false;
        }
    }

    // 音频调试：发送原始音频数据
    if (audio_debugger_)
    {
        audio_debugger_->Feed(data);
    }

    return true;
}

void Application::AbortSpeaking(AbortReason reason)
{
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    protocol_->SendAbortSpeaking(reason);
}

void Application::SetListeningMode(ListeningMode mode)
{
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state)
{
    if (custom_power_save_restore_in_progress_.load(std::memory_order_acquire) &&
        state != kDeviceStateIdle)
    {
        ESP_LOGI(TAG, "[PWR_SAVE] Ignoring state change to %s while custom wake restore is in progress",
                 STATE_STRINGS[state]);
        return;
    }

    if (device_state_ == state)
    {
        return;
    }

    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    if (custom_power_save_mode_.load(std::memory_order_acquire) || Board::GetInstance().IsSleepTransitionActive())
    {
        ESP_LOGI(TAG, "Custom power-save mode active, suppressing state side effects");
        return;
    }

    // The state is changed, wait for all background tasks to finish
    background_task_->WaitForCompletion();

    auto &board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state)
    {
    case kDeviceStateUnknown:
    case kDeviceStateIdle:
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("normal");
        audio_processor_->Stop();
        wake_word_->StartDetection();
        break;
    case kDeviceStateConnecting:
        display->SetStatus(Lang::Strings::CONNECTING);
        display->SetEmotion("normal");
        // DISABLED: Comment out transcript display to reduce memory usage
        // display->SetChatMessage("system", "");
        timestamp_queue_.clear();
        break;
    case kDeviceStateListening:
        display->SetStatus(Lang::Strings::LISTENING);
        // Use "listening" emotion which maps to ANIMATION_LISTENING (listening_loop.gif)
        display->SetEmotion("listening");
        ESP_LOGI(TAG, "Listening state: showing listening animation");
        // Update the IoT states before sending the start listening command
#if CONFIG_IOT_PROTOCOL_XIAOZHI
        UpdateIotStates();
#endif

        // Make sure the audio processor is running
        if (!audio_processor_->IsRunning())
        {
            // Send the start listening command FIRST
            auto* active_protocol = GetActiveProtocol();
            if (active_protocol && active_protocol->IsAudioChannelOpened()) {
                // Only send listen start if the connection is actually open and ready
                // Detect if this is a remote wakeup scenario (WebSocket already open from ws_start)
                // vs manual connection (opening WebSocket now)
                bool is_remote_wakeup = (websocket_protocol_ && 
                                       websocket_protocol_->IsAudioChannelOpened() && 
                                       previous_state == kDeviceStateIdle);
                // Debug preflight to confirm active protocol, channel status, audio state, and mode
                {
                    bool ws_open = (websocket_protocol_ && websocket_protocol_->IsAudioChannelOpened());
                    const char* active_name = (active_protocol == websocket_protocol_.get() ? "ws"
                                                : (active_protocol ? "mqtt" : "null"));
                    ESP_LOGI(TAG, "DEBUG listen preflight: active=%s, ws_open=%d, audio_running=%d, mode=%d",
                             active_name,
                             ws_open ? 1 : 0,
                             audio_processor_->IsRunning() ? 1 : 0,
                             (int)listening_mode_);
                }
                // Hard-force AutoStop for remote wake so TTS stop resumes listening automatically
                if (is_remote_wakeup) {
                    listening_mode_ = kListeningModeAutoStop;
                }
                ESP_LOGI(TAG, "Sending listen start message, mode=%d (0=auto, 1=manual, 2=realtime)%s", 
                        listening_mode_, is_remote_wakeup ? " [remote wakeup]" : "");
                active_protocol->SendStartListening(listening_mode_);
                
                // For remote wakeup ONLY: Add small delay after listen:start to ensure server
                // has time to process the message before receiving audio frames.
                // Manual connections don't need this delay as they open the connection fresh.
                if (is_remote_wakeup) {
                    ESP_LOGI(TAG, "Remote wakeup: waiting 50ms after listen:start for server to process");
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            } else {
                ESP_LOGW(TAG, "Cannot send listen start: protocol not available or connection not open");
                ESP_LOGW(TAG, "Will send listen start when connection is ready (e.g., after ws_start opens WebSocket)");
            }
            
            if (previous_state == kDeviceStateSpeaking)
            {
                audio_decode_queue_.clear();
                audio_decode_cv_.notify_all();
                // FIXME: Wait for the speaker to empty the buffer
                vTaskDelay(pdMS_TO_TICKS(120));
            }
            
            opus_encoder_->ResetState();
            
            // Log Opus encoder configuration
            auto codec = Board::GetInstance().GetAudioCodec();
            ESP_LOGI(TAG, "Audio config: input_sample_rate=%d, Opus encoder=16000Hz mono, frame_duration=%dms (960 samples)", 
                     codec ? codec->input_sample_rate() : 0, OPUS_FRAME_DURATION_MS);
            
            ESP_LOGI(TAG, "Starting audio capture and streaming...");
            audio_processor_->Start();
            wake_word_->StopDetection();
        }
        break;
    case kDeviceStateSpeaking: {
        display->SetStatus(Lang::Strings::SPEAKING);
        
        // Record when speaking started for grace period
        speaking_start_time_us_ = esp_timer_get_time();
        vad_debounce_active_ = false; // Reset debounce state when entering speaking state

        // Keep audio processor running when device-side AEC is enabled to allow VAD interrupt detection
        // This enables users to interrupt the AI assistant during speech playback
        if (aec_mode_ == kAecOnDeviceSide)
        {
            // Keep audio processor running for VAD interrupt capability
            // VAD will detect user speech and trigger interrupt via MAIN_EVENT_VAD_CHANGE
            if (!audio_processor_->IsRunning())
            {
                audio_processor_->Start();
                ESP_LOGI(TAG, "Audio processor kept running for VAD interrupt (device-side AEC enabled)");
            }
            // Ensure listening mode is set to realtime for continuous listening
            if (listening_mode_ != kListeningModeRealtime)
            {
                listening_mode_ = kListeningModeRealtime;
                ESP_LOGI(TAG, "Listening mode set to realtime for VAD interrupt capability");
            }
            wake_word_->StopDetection();
        }
        else if (listening_mode_ != kListeningModeRealtime)
        {
            // Without device-side AEC, stop audio processor during speaking
            audio_processor_->Stop();
            // Only AFE wake word can be detected in speaking mode
#if CONFIG_USE_AFE_WAKE_WORD
            wake_word_->StartDetection();
#else
            wake_word_->StopDetection();
#endif
        }
        ResetDecoder();
        break;
    }
    case kDeviceStateStarting:
        break;
    case kDeviceStateWifiConfiguring:
        // Keep WiFi config mode on normal animation. Touch interactions can still
        // temporarily switch emotions (angry/happy/embarressed) via board logic.
        display->SetEmotion("normal");
        break;
    case kDeviceStateAudioTesting:
        // In WiFi config flow, show WiFi animation only when talk button enters
        // audio testing mode.
        display->SetEmotion("wifi");
        break;
    case kDeviceStateUpgrading:
    case kDeviceStateActivating:
    case kDeviceStateFatalError:
        // These states are handled elsewhere or don't require special handling here
        break;
    default:
        // Do nothing
        break;
    }
}

void Application::ResetDecoder()
{
    std::lock_guard<std::mutex> lock(mutex_);
    opus_decoder_->ResetState();
    audio_decode_queue_.clear();
    audio_decode_cv_.notify_all();
    last_output_time_ = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableOutput(true);
}

void Application::SetDecodeSampleRate(int sample_rate, int frame_duration)
{
    if (opus_decoder_->sample_rate() == sample_rate && opus_decoder_->duration_ms() == frame_duration)
    {
        return;
    }

    opus_decoder_.reset();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(sample_rate, 1, frame_duration);

    auto codec = Board::GetInstance().GetAudioCodec();
    if (opus_decoder_->sample_rate() != codec->output_sample_rate())
    {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", opus_decoder_->sample_rate(), codec->output_sample_rate());
        output_resampler_.Configure(opus_decoder_->sample_rate(), codec->output_sample_rate());
    }
}

void Application::UpdateIotStates()
{
#if CONFIG_IOT_PROTOCOL_XIAOZHI
    auto &thing_manager = iot::ThingManager::GetInstance();
    std::string states;
    if (thing_manager.GetStatesJson(states, true))
    {
        protocol_->SendIotStates(states);
    }
#endif
}

void Application::ClearWifiConfiguration()
{
    ESP_LOGI(TAG, "Clearing WiFi configuration from application");
    auto& board = Board::GetInstance();
    board.ClearWifiConfiguration();
    
    // After clearing WiFi, restart to enter BLE configuration mode
    ESP_LOGI(TAG, "WiFi cleared, restarting to enter BLE configuration mode");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void Application::Reboot()
{
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

bool Application::ArmRtcReminder(time_t trigger_at, bool custom_mode, const std::string& wav_url,
                                 int priority, const std::string& reminder_id, bool replay_if_no_mic,
                                 int fallback_delay_ms, bool software_fallback_enabled)
{
    if (trigger_at <= 0) {
        ESP_LOGW(TAG, "[RTC_ALARM] Ignoring RTC reminder with invalid epoch: %ld", static_cast<long>(trigger_at));
        return false;
    }

    const std::string path = kOfflineReminderDefaultPath;
    remove(path.c_str());
    remove(kOfflineReminderTempPath);
    {
        Settings settings(kOfflineReminderNamespace, true);
        settings.SetInt("armed", 1);
        settings.SetInt("fired", 0);
        settings.SetInt("cached", 0);
        settings.SetInt("epoch", static_cast<int32_t>(trigger_at));
        settings.SetInt("custom", custom_mode ? 1 : 0);
        settings.SetInt("prio", priority);
        settings.SetInt("replay", replay_if_no_mic ? 1 : 0);
        settings.SetInt("delay_ms", fallback_delay_ms);
        settings.SetInt("sw_fb", software_fallback_enabled ? 1 : 0);
        settings.SetString("path", path);
        settings.SetString("id", reminder_id);
        if (!wav_url.empty()) {
            settings.SetString("url", wav_url);
        }
    }

    bool rtc_ok = Board::GetInstance().ScheduleRtcAlarmAt(trigger_at, custom_mode);
    if (!rtc_ok) {
        ESP_LOGW(TAG, "[RTC_ALARM] Board RTC alarm schedule failed%s",
                 software_fallback_enabled ? "; software fallback remains armed" : "; RTC-only alarm may not fire");
    }
    ScheduleRtcReminderSoftwareFallback(trigger_at, fallback_delay_ms, software_fallback_enabled);

    if (!wav_url.empty()) {
        background_task_->Schedule([wav_url, path]() {
            bool ok = DownloadFileFromUrl(wav_url, path, kOfflineReminderMaxBytes);
            Settings settings(kOfflineReminderNamespace, true);
            settings.SetInt("cached", ok ? 1 : 0);
            ESP_LOGI(TAG, "[RTC_ALARM] Offline WAV cache %s", ok ? "ready" : "failed");
        });
    } else {
        ESP_LOGI(TAG, "[RTC_ALARM] RTC reminder armed without offline WAV URL");
    }

    ESP_LOGI(TAG, "[RTC_ALARM] Armed reminder id=%s epoch=%ld custom=%d priority=%d replay=%d sw_fb=%d",
             reminder_id.empty() ? "<none>" : reminder_id.c_str(),
             static_cast<long>(trigger_at),
             custom_mode ? 1 : 0,
             priority,
             replay_if_no_mic ? 1 : 0,
             software_fallback_enabled ? 1 : 0);
    return rtc_ok;
}

void Application::ScheduleRtcReminderSoftwareFallback(time_t trigger_at, int fallback_delay_ms,
                                                      bool software_fallback_enabled)
{
    if (rtc_reminder_timer_handle_ == nullptr || trigger_at <= 0) {
        return;
    }

    esp_timer_stop(rtc_reminder_timer_handle_);
    if (!software_fallback_enabled) {
        ESP_LOGI(TAG, "[RTC_ALARM] RTC-only reminder armed; software fallback disabled");
        return;
    }

    int64_t delay_us = -1;
    if (fallback_delay_ms > 0) {
        delay_us = static_cast<int64_t>(fallback_delay_ms) * 1000LL;
        ESP_LOGI(TAG, "[RTC_ALARM] Using MQTT relative delay for software fallback: %d ms",
                 fallback_delay_ms);
    } else {
        const time_t now = time(nullptr);
        delay_us = static_cast<int64_t>(trigger_at - now) * 1000000LL;
        ESP_LOGI(TAG, "[RTC_ALARM] Using device wall clock for software fallback: epoch=%ld now=%ld",
                 static_cast<long>(trigger_at),
                 static_cast<long>(now));
    }

    if (delay_us < 1000000LL) {
        delay_us = 1000000LL;
    }

    esp_err_t err = esp_timer_start_once(rtc_reminder_timer_handle_, delay_us);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[RTC_ALARM] Software fallback armed in %ld ms",
                 static_cast<long>(delay_us / 1000LL));
    } else {
        ESP_LOGW(TAG, "[RTC_ALARM] Failed to arm software fallback: %s", esp_err_to_name(err));
    }
}

void Application::OnRtcReminderSoftwareTimer()
{
    Schedule([this]() {
        Settings settings(kOfflineReminderNamespace, false);
        if (settings.GetInt("armed", 0) == 0) {
            ESP_LOGI(TAG, "[RTC_ALARM] Software fallback ignored; reminder is no longer armed");
            return;
        }
        if (settings.GetInt("sw_fb", 1) == 0) {
            ESP_LOGI(TAG, "[RTC_ALARM] Software fallback ignored; reminder is RTC-only");
            return;
        }

        ESP_LOGW(TAG, "[RTC_ALARM] BM8563 interrupt not observed; software fallback firing reminder");
        HandleRtcAlarmSignal(false);
    });
}

void Application::HandleRtcAlarmSignal(bool from_custom_mode)
{
    if (rtc_reminder_timer_handle_ != nullptr) {
        esp_timer_stop(rtc_reminder_timer_handle_);
    }

    Settings settings(kOfflineReminderNamespace, true);
    const bool custom_mode = settings.GetInt("custom", 0) != 0;
    settings.SetInt("armed", 0);
    settings.SetInt("fired", 1);

    ESP_LOGI(TAG, "[RTC_ALARM] RTC alarm signal received: custom=%d from_custom=%d",
             custom_mode ? 1 : 0,
             from_custom_mode ? 1 : 0);

    if (custom_mode || from_custom_mode || custom_power_save_mode_.load(std::memory_order_acquire)) {
        ESP_LOGI(TAG, "[RTC_ALARM] Custom-mode RTC alarm will reboot before reminder playback");
        Reboot();
        return;
    }

    Schedule([this]() {
        PlayOfflineReminderFromSettings(false);
    });
}

void Application::HandlePendingRtcReminderOnBoot()
{
    Settings settings(kOfflineReminderNamespace, false);
    if (settings.GetInt("fired", 0) != 0) {
        ESP_LOGI(TAG, "[RTC_ALARM] Pending fired RTC reminder found on boot");
        PlayOfflineReminderFromSettings(true);
        return;
    }

    if (settings.GetInt("armed", 0) == 0) {
        return;
    }

    const time_t trigger_at = static_cast<time_t>(settings.GetInt("epoch", 0));
    if (trigger_at <= 0) {
        return;
    }

    if (time(nullptr) >= trigger_at) {
        if (settings.GetInt("sw_fb", 1) != 0) {
            ESP_LOGW(TAG, "[RTC_ALARM] Armed RTC reminder is overdue on boot; firing now");
            HandleRtcAlarmSignal(false);
        } else {
            ESP_LOGW(TAG, "[RTC_ALARM] RTC-only reminder is overdue on boot, but BM8563 flag was not set");
        }
    } else {
        const bool software_fallback_enabled = settings.GetInt("sw_fb", 1) != 0;
        ESP_LOGI(TAG, "[RTC_ALARM] Restoring armed reminder epoch=%ld sw_fb=%d",
                 static_cast<long>(trigger_at),
                 software_fallback_enabled ? 1 : 0);
        ScheduleRtcReminderSoftwareFallback(
            trigger_at,
            settings.GetInt("delay_ms", -1),
            software_fallback_enabled);
    }
}

void Application::PlayOfflineReminderFromSettings(bool from_custom_reboot)
{
    Settings settings(kOfflineReminderNamespace, true);
    const std::string path = settings.GetString("path", kOfflineReminderDefaultPath);
    const bool replay_if_no_mic = settings.GetInt("replay", 1) != 0;
    const bool cached = settings.GetInt("cached", 0) != 0 || access(path.c_str(), F_OK) == 0;
    const bool was_fired = settings.GetInt("fired", 0) != 0;
    const time_t trigger_at = static_cast<time_t>(settings.GetInt("epoch", 0));

    if (!was_fired && trigger_at > 0 && time(nullptr) < trigger_at) {
        return;
    }

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    const bool online = WifiStation::GetInstance().IsConnected();
    if (online) {
        settings.SetInt("armed", 0);
        settings.SetInt("fired", 0);
        ESP_LOGI(TAG, "[RTC_ALARM] RTC reminder fired online; opening regular WebSocket alarm session");
        SetAlarmMode(true);
        OpenWebSocketConnection();
        return;
    }

    if (display != nullptr) {
        display->SetEmotion("wifi");
    }

    if (!cached) {
        ESP_LOGW(TAG, "[RTC_ALARM] Offline reminder fired but no cached WAV is available");
        settings.SetInt("armed", 0);
        settings.SetInt("fired", 1);
        ESP_LOGW(TAG, "[RTC_ALARM] Keeping fired reminder pending until network or cached WAV is available");
        return;
    }

    settings.SetInt("armed", 0);
    settings.SetInt("fired", 0);

    ESP_LOGI(TAG, "[RTC_ALARM] Playing cached reminder WAV%s: %s",
             from_custom_reboot ? " after custom reboot" : "",
             path.c_str());

    const bool runtime_audio_ready = audio_loop_task_handle_ != nullptr;
    if (runtime_audio_ready && audio_processor_) {
        audio_processor_->Stop();
    }
    if (runtime_audio_ready && wake_word_) {
        wake_word_->StopDetection();
    }

    ResetDecoder();
    DeviceState previous_state = device_state_;
    if (runtime_audio_ready && (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening)) {
        SetDeviceState(kDeviceStateSpeaking);
    }

    bool played = PlayWavFromSdCard(path, 1.0f);
    bool heard_user = false;
    if (played && replay_if_no_mic) {
        heard_user = DetectRawMicActivity(kOfflineReminderMicWindowMs, kOfflineReminderMicThreshold);
        if (!heard_user) {
            ESP_LOGI(TAG, "[RTC_ALARM] Replaying cached reminder because no mic activity was detected");
            played = PlayWavFromSdCard(path, 1.0f);
        }
    }

    if (runtime_audio_ready) {
        if (previous_state == kDeviceStateListening && WifiStation::GetInstance().IsConnected()) {
            SetListeningMode(kListeningModeAutoStop);
        } else {
            SetDeviceState(kDeviceStateIdle);
        }
    }

    ESP_LOGI(TAG, "[RTC_ALARM] Cached reminder playback %s, user_activity=%d",
             played ? "complete" : "failed",
             heard_user ? 1 : 0);
}

void Application::EnterCustomPowerSaveMode()
{
    custom_power_save_mode_.store(true, std::memory_order_release);
    custom_power_save_restore_in_progress_.store(false, std::memory_order_release);
    custom_power_save_restore_memory_ready_.store(false, std::memory_order_release);
    Schedule([this]() {
        ESP_LOGI(TAG, "Entering custom power-save mode: stopping audio and wake detection");
        aborted_ = true;
        voice_detected_ = false;
        vad_debounce_active_ = false;
        is_alarm_mode_ = false;
        state_before_tts_ = kDeviceStateUnknown;

        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        if (websocket_protocol_ && websocket_protocol_->IsAudioChannelOpened()) {
            websocket_protocol_->CloseAudioChannel();
        }

        if (audio_processor_) {
            audio_processor_->Stop();
        }
        if (wake_word_) {
            wake_word_->StopDetection();
        }

        auto codec = Board::GetInstance().GetAudioCodec();
        if (codec != nullptr) {
            codec->EnableInput(false);
            codec->EnableOutput(false);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            audio_send_queue_.clear();
            audio_decode_queue_.clear();
            audio_testing_queue_.clear();
        }
        audio_decode_cv_.notify_all();

        if (device_state_ != kDeviceStateIdle) {
            device_state_ = kDeviceStateIdle;
            ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);
        }
    });
}

void Application::ExitCustomPowerSaveMode(std::function<void()> on_restored)
{
    custom_power_save_mode_.store(false, std::memory_order_release);
    custom_power_save_restore_in_progress_.store(true, std::memory_order_release);
    Schedule([this, on_restored]() {
        ESP_LOGI(TAG, "[PWR_SAVE] Custom wake restore started; BOOT/touch/talk ignored until ready");
        aborted_ = false;
        voice_detected_ = false;
        vad_debounce_active_ = false;
        is_alarm_mode_ = false;
        state_before_tts_ = kDeviceStateUnknown;

        if (audio_processor_) {
            audio_processor_->Stop();
        }
        if (wake_word_) {
            wake_word_->StopDetection();
        }

        auto codec = Board::GetInstance().GetAudioCodec();
        if (codec != nullptr) {
            codec->EnableOutput(false);
            codec->EnableInput(true);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            audio_send_queue_.clear();
            audio_decode_queue_.clear();
            audio_testing_queue_.clear();
            timestamp_queue_.clear();
        }
        audio_decode_cv_.notify_all();

        device_state_ = kDeviceStateIdle;
        ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        auto led = board.GetLed();
        if (display != nullptr) {
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("normal");
        }
        if (led != nullptr) {
            led->OnStateChanged();
        }
        if (wake_word_ && audio_loop_task_handle_ != nullptr) {
            wake_word_->StartDetection();
        }

        bool memory_ready = false;
        for (int elapsed_ms = 0; elapsed_ms <= kCustomRestoreMemoryWaitMs; elapsed_ms += kCustomRestoreMemoryPollMs) {
            memory_ready = CustomRestoreMemoryReady(true);
            if (memory_ready) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(kCustomRestoreMemoryPollMs));
        }

        if (!memory_ready) {
            ESP_LOGW(TAG, "[PWR_SAVE] Custom wake restore memory gate timed out; talk stays blocked until SRAM recovers");
        }
        custom_power_save_restore_memory_ready_.store(memory_ready, std::memory_order_release);

        custom_power_save_restore_in_progress_.store(false, std::memory_order_release);
        if (on_restored) {
            on_restored();
        }
        ESP_LOGI(TAG, "[PWR_SAVE] Custom wake restore complete; BOOT click %s",
                 memory_ready ? "enabled" : "will enable talk after memory recovers");
    });
}

void Application::WakeWordInvoke(const std::string &wake_word)
{
    if (custom_power_save_mode_.load(std::memory_order_acquire) || Board::GetInstance().IsSleepTransitionActive())
    {
        ESP_LOGI(TAG, "Ignoring wake word while custom power-save mode is active");
        return;
    }

    if (device_state_ == kDeviceStateIdle)
    {
        ToggleChatState();
        Schedule([this, wake_word]()
                 {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            } });
    }
    else if (device_state_ == kDeviceStateSpeaking)
    {
        Schedule([this]()
                 { AbortSpeaking(kAbortReasonNone); });
    }
    else if (device_state_ == kDeviceStateListening)
    {
        Schedule([this]()
                 {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            } });
    }
}

bool Application::CanEnterSleepMode()
{
    if (custom_power_save_mode_.load(std::memory_order_acquire) || Board::GetInstance().IsSleepTransitionActive())
    {
        return false;
    }

    if (device_state_ != kDeviceStateIdle)
    {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened())
    {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string &payload)
{
    ESP_LOGI(TAG, "SendMcpMessage: Scheduling MCP message send, payload_size=%u bytes", (unsigned)payload.length());
    Schedule([this, payload]()
             {
        // Route the reply back over the same channel the MCP request arrived on.
        // When a voice session is active the request came in over the WebSocket
        // secondary protocol, and the reply MUST go back out through it (the
        // WebSocket carries the real conversation session_id). Falling through
        // to the primary (MQTT) would send the reply with an empty session_id
        // and the server would drop it, so tools/list would never register.
        Protocol* active = GetActiveProtocol();
        if (active) {
            const bool via_ws = (websocket_protocol_ && active == websocket_protocol_.get());
            ESP_LOGI(TAG, "SendMcpMessage: Executing scheduled send via %s protocol",
                     via_ws ? "WebSocket" : "primary");
            active->SendMcpMessage(payload);
        } else {
            ESP_LOGW(TAG, "SendMcpMessage: No active protocol, cannot send MCP message");
        } });
}

void Application::SetAecMode(AecMode mode)
{
    aec_mode_ = mode;
    Schedule([this]()
             {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_processor_->EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_processor_->EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_processor_->EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        auto* active_protocol = GetActiveProtocol();
        if (active_protocol && active_protocol->IsAudioChannelOpened()) {
            active_protocol->CloseAudioChannel();
        } });
}

Protocol* Application::GetActiveProtocol() {
    // Protocol selection for audio streaming:
    // - WebSocket: Used for all audio conversations (both user-initiated and server-initiated via ws_start)
    // - MQTT: Used as fallback if WebSocket is not available, or for control messages only
    // 
    // Both protocols can be "live" simultaneously:
    // - MQTT: Always connected (if configured) for control messages (ws_start, wake word, etc.)
    // - WebSocket: Opened on-demand for audio streaming, takes priority when available
    if (websocket_protocol_ && websocket_protocol_->IsAudioChannelOpened()) {
        return websocket_protocol_.get();
    }
    return protocol_.get();
}

void Application::OpenWebSocketConnection() {
    const bool websocket_already_open = websocket_protocol_ && websocket_protocol_->IsAudioChannelOpened();
    if (websocket_already_open) {
        ESP_LOGI(TAG, "WebSocket connection already open; reusing it for remote wakeup");
    }
    
    // Create WebSocket protocol if it doesn't exist
    if (!websocket_protocol_) {
        ESP_LOGI(TAG, "Creating WebSocket protocol instance");
        websocket_protocol_ = std::make_unique<WebsocketProtocol>();
        
        // Set up callbacks same as primary protocol
        websocket_protocol_->OnNetworkError([this](const std::string &message) {
            SetDeviceState(kDeviceStateIdle);
            // Mirror primary protocol behavior: show WiFi face on connectivity errors.
            Alert(Lang::Strings::ERROR, message.c_str(), "wifi", Lang::Sounds::P3_EXCLAMATION);
        });
        
        websocket_protocol_->OnIncomingAudio([this](AudioStreamPacket &&packet) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (device_state_ == kDeviceStateSpeaking && audio_decode_queue_.size() < MAX_AUDIO_PACKETS_IN_QUEUE) {
                audio_decode_queue_.emplace_back(std::move(packet));
            }
        });
        
        websocket_protocol_->OnAudioChannelOpened([this, codec = Board::GetInstance().GetAudioCodec(), &board = Board::GetInstance()]() {
            board.SetPowerSaveMode(false);
            if (websocket_protocol_->server_sample_rate() != codec->output_sample_rate()) {
                ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                         websocket_protocol_->server_sample_rate(), codec->output_sample_rate());
            }
        });
        
        websocket_protocol_->OnAudioChannelClosed([this, &board = Board::GetInstance()]() {
            board.SetPowerSaveMode(true);
            Schedule([this]() {
                is_alarm_mode_ = false; // Reset alarm mode when WebSocket closes
                SetDeviceState(kDeviceStateIdle);
            });
        });
        
        // Set up JSON handler - use the same handler as primary protocol
        auto display = Board::GetInstance().GetDisplay();
        websocket_protocol_->OnIncomingJson([this, display](const cJSON *root) {
            // Use the same JSON parsing logic as primary protocol
            auto type = cJSON_GetObjectItem(root, "type");
            if (!cJSON_IsString(type)) {
                return;
            }
            
            if (strcmp(type->valuestring, "tts") == 0) {
                auto state = cJSON_GetObjectItem(root, "state");
                if (strcmp(state->valuestring, "start") == 0) {
                    Schedule([this]() {
                        aborted_ = false;
                        if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                            // Save state before TTS to determine if we should resume listening after TTS
                            state_before_tts_ = device_state_;
                            
                            // If we're in LISTENING state, stop listening before TTS starts
                            // This ensures no microphone audio interference during TTS playback
                            if (device_state_ == kDeviceStateListening) {
                                auto* active_protocol = GetActiveProtocol();
                                if (active_protocol && active_protocol->IsAudioChannelOpened()) {
                                    ESP_LOGI(TAG, "TTS starting while listening (WebSocket) - stopping listening before TTS");
                                    active_protocol->SendStopListening();
                                    // Small delay to ensure server receives listen:stop before TTS starts
                                    vTaskDelay(pdMS_TO_TICKS(50));
                                }
                            }
                            
                            SetDeviceState(kDeviceStateSpeaking);
                        }
                    });
                } else if (strcmp(state->valuestring, "stop") == 0) {
                    Schedule([this]() {
                        ESP_LOGI(TAG, "TTS stop (WebSocket): state=%d, listening_mode=%d", device_state_, (int)listening_mode_);
                        background_task_->WaitForCompletion();
                        if (device_state_ == kDeviceStateSpeaking) {
                            // Check if user aborted speaking - don't auto-resume listening
                            if (aborted_) {
                                ESP_LOGI(TAG, "TTS stopped after user abort (WebSocket) - going to idle without resuming listening");
                                aborted_ = false;  // Reset flag
                                state_before_tts_ = kDeviceStateUnknown;  // Reset
                                SetDeviceState(kDeviceStateIdle);
                                return;  // Early return, don't continue with auto-resume logic
                            }
                            
                            // Check if remote wakeup scenario (WebSocket still open)
                            auto* active_protocol = GetActiveProtocol();
                            bool is_remote_wakeup = (websocket_protocol_ != nullptr && 
                                                    websocket_protocol_->IsAudioChannelOpened() &&
                                                    active_protocol && active_protocol->IsAudioChannelOpened());
                            
                            if (is_remote_wakeup) {
                                // Automatically resume listening after TTS in remote wakeup scenario
                                if (is_alarm_mode_) {
                                    // Alarm mode: Always start listening after TTS (even if wasn't listening before)
                                    // This is the expected flow: ws_start → TTS → listening
                                    ESP_LOGI(TAG, "TTS stopped (WebSocket), alarm mode - starting listening for user response");
                                    SetDeviceState(kDeviceStateListening);
                                    is_alarm_mode_ = false; // Reset alarm mode after first TTS completes
                                } else if (state_before_tts_ == kDeviceStateListening) {
                                    // Normal remote wakeup: Only resume if we were listening before TTS started
                                    ESP_LOGI(TAG, "TTS stopped (WebSocket), remote wakeup detected - automatically resuming listening");
                                    SetDeviceState(kDeviceStateListening);
                                } else {
                                    ESP_LOGI(TAG, "TTS stopped (WebSocket), remote wakeup detected but was not listening before TTS - going to idle");
                                    SetDeviceState(kDeviceStateIdle);
                                }
                                state_before_tts_ = kDeviceStateUnknown; // Reset
                            } else if (listening_mode_ == kListeningModeManualStop) {
                                // Go to idle for manual interactions
                                ESP_LOGI(TAG, "TTS stopped (WebSocket), going to idle (manual stop mode)");
                                SetDeviceState(kDeviceStateIdle);
                                state_before_tts_ = kDeviceStateUnknown; // Reset
                            } else {
                                // Auto mode: resume listening
                                ESP_LOGI(TAG, "TTS stopped (WebSocket), automatically resuming listening (auto stop mode)");
                                SetDeviceState(kDeviceStateListening);
                                state_before_tts_ = kDeviceStateUnknown; // Reset
                            }
                        }
                    });
                } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                    auto text = cJSON_GetObjectItem(root, "text");
                    if (cJSON_IsString(text)) {
                        ESP_LOGI(TAG, "<< %s", text->valuestring);
                    }
                }
            } else if (strcmp(type->valuestring, "stt") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, ">> %s", text->valuestring);
                }
            } else if (strcmp(type->valuestring, "llm") == 0) {
                auto emotion = cJSON_GetObjectItem(root, "emotion");
                if (cJSON_IsString(emotion)) {
                    Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                        display->SetEmotion(emotion_str.c_str());
                    });
                }
            } else if (strcmp(type->valuestring, "listen") == 0) {
                auto state = cJSON_GetObjectItem(root, "state");
                if (cJSON_IsString(state)) {
                    if (strcmp(state->valuestring, "start") == 0) {
                        ESP_LOGI(TAG, "Received listen:start from server (WebSocket), starting listening");
                        Schedule([this]() { 
                            ESP_LOGI(TAG, "Executing listen:start - setting listening mode (AutoStop)");
                            SetListeningMode(kListeningModeAutoStop); 
                        });
                    } else if (strcmp(state->valuestring, "stop") == 0) {
                        ESP_LOGI(TAG, "Received listen:stop from server (WebSocket), stopping listening");
                        Schedule([this]() { StopListening(); });
                    } else {
                        ESP_LOGW(TAG, "Received listen message with unknown state: %s", state->valuestring);
                    }
                } else {
                    ESP_LOGW(TAG, "Received listen message without valid state field");
                }
#if CONFIG_IOT_PROTOCOL_MCP
            } else if (strcmp(type->valuestring, "mcp") == 0) {
                ESP_LOGI(TAG, "OnIncomingJson (WebSocket): Received MCP message");
                auto payload = cJSON_GetObjectItem(root, "payload");
                if (cJSON_IsObject(payload)) {
                    char* payload_str = cJSON_PrintUnformatted(payload);
                    if (payload_str) {
                        ESP_LOGI(TAG, "OnIncomingJson (WebSocket): MCP payload: %s", payload_str);
                        cJSON_free(payload_str);
                    }
                    ESP_LOGI(TAG, "OnIncomingJson (WebSocket): Forwarding MCP payload to McpServer::ParseMessage");
                    McpServer::GetInstance().ParseMessage(payload);
                } else {
                    ESP_LOGW(TAG, "OnIncomingJson (WebSocket): MCP message missing or invalid payload");
                }
#endif
            }
            // Add other message type handlers as needed
        });
        
        // Start the WebSocket protocol
        if (!websocket_protocol_->Start()) {
            ESP_LOGE(TAG, "Failed to start WebSocket protocol");
            websocket_protocol_.reset();
            return;
        }
    }
    
    if (!websocket_already_open) {
        ESP_LOGI(TAG, "Opening WebSocket audio channel");
        if (!websocket_protocol_->OpenAudioChannel()) {
            ESP_LOGE(TAG, "Failed to open WebSocket audio channel");
            return;
        }
        ESP_LOGI(TAG, "WebSocket connection opened successfully");
    }
    
    // For remote wakeup (ws_start): Behavior depends on alarm mode
    // - Normal mode: Automatically enter listening state (for normal conversations)
    // - Alarm mode: Still send listen:start to establish voice connection, but TTS can interrupt
    if (device_state_ == kDeviceStateIdle) {
        // Small delay to ensure WebSocket connection is fully ready before starting listening
        // This prevents race conditions where connection might not be ready immediately
        vTaskDelay(pdMS_TO_TICKS(100));
        // Verify connection is still open before proceeding
        if (websocket_protocol_ && websocket_protocol_->IsAudioChannelOpened()) {
            if (is_alarm_mode_) {
                // Alarm mode: Still send listen:start to establish voice connection
                // Server can send TTS first (which will stop listening), or process audio immediately
                // After TTS stops, listening will automatically resume
                ESP_LOGI(TAG, "Alarm mode: WebSocket opened via ws_start, sending listen:start to establish voice connection (TTS can interrupt)");
                // Use AutoStop for remote wake so TTS stop resumes listening automatically
                SetListeningMode(kListeningModeAutoStop);
                // SetListeningMode() calls SetDeviceState(kDeviceStateListening) which will:
                // - Turn on red light (via led->OnStateChanged())
                // - Send listen start message
                // - Start audio capture
            } else {
                // Normal mode: Automatically enter listening state
                ESP_LOGI(TAG, "Remote wakeup: WebSocket opened via ws_start, automatically entering listening state");
                // Use AutoStop for remote wake so TTS stop resumes listening automatically
                SetListeningMode(kListeningModeAutoStop);
                // SetListeningMode() calls SetDeviceState(kDeviceStateListening) which will:
                // - Turn on red light (via led->OnStateChanged())
                // - Send listen start message
                // - Start audio capture
            }
        } else {
            ESP_LOGW(TAG, "WebSocket connection not ready after delay, cannot start listening automatically");
        }
    }
    else if (device_state_ == kDeviceStateSpeaking) {
        // If speaking, abort speaking and enter listening mode (same as button press behavior)
        ESP_LOGI(TAG, "Remote wakeup: Device is speaking, aborting and entering listening state");
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeAutoStop);
        });
    }
    // If we're already in listening state (e.g., user pressed button before ws_start),
    // send the listen start message now that the connection is ready
    else if (device_state_ == kDeviceStateListening && !audio_processor_->IsRunning()) {
        ESP_LOGI(TAG, "Already in listening state, sending listen start to new WebSocket connection");
        websocket_protocol_->SendStartListening(listening_mode_);
        // Small delay to ensure server processes the message
        vTaskDelay(pdMS_TO_TICKS(50));
        // Start audio capture if not already running
        if (!audio_processor_->IsRunning()) {
            opus_encoder_->ResetState();
            auto codec = Board::GetInstance().GetAudioCodec();
            ESP_LOGI(TAG, "Starting audio capture: input_sample_rate=%d, Opus encoder=16000Hz mono, frame_duration=%dms", 
                     codec ? codec->input_sample_rate() : 0, OPUS_FRAME_DURATION_MS);
            audio_processor_->Start();
            wake_word_->StopDetection();
        }
    }
    else if (device_state_ == kDeviceStateConnecting) {
        // If we're in connecting state (e.g., connection just opened), enter listening mode
        // This handles the case where ws_start arrives while device is connecting
        ESP_LOGI(TAG, "Remote wakeup: Device is connecting, entering listening state");
        SetListeningMode(kListeningModeAutoStop);
    }
}
