#include "esp32_music_sdcard.h"
#include "board.h"
#include "system_info.h"
#include "audio_codec.h"
#include "application.h"
#include "protocols/protocol.h"
#include "display/display.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <cJSON.h>
#include <cstring>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <cctype> // 为isdigit函数
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Esp32MusicSdcard"

#include <iconv.h>
#include <string>
#include <vector>
#include "esp_log.h"

#include <dirent.h>
#include <vector>
#include <string>

// 缓存音乐文件列表（避免重复扫描SD卡）
std::vector<MusicFile> cached_music_files_;
// 缓存有效性标志（判断是否需要重新扫描）
bool is_cache_valid_ = false;
static sdmmc_card_t *mount_sdcard(void)
{
    // 定义并初始化返回值指针
    sdmmc_card_t *card = NULL;

    // 配置SD卡电源引脚（如果需要）
    if (1)
    {
        gpio_config_t power_gpio_config = {
            .pin_bit_mask = (BIT64(GPIO_NUM_9)),
            .mode = GPIO_MODE_OUTPUT,
        };

        esp_err_t err = gpio_config(&power_gpio_config);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to configure SD card power GPIO: %s", esp_err_to_name(err));
            // 继续执行，不使用电源控制引脚
        }
        else
        {
            gpio_set_level(GPIO_NUM_9, 0);
        }
    }

    // 配置SD卡挂载参数
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 20,
        .allocation_unit_size = 64 * 1024};

    // 配置SDMMC主机和插槽
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    slot_config.width = 1;
    slot_config.clk = ESP_SD_PIN_CLK;
    slot_config.cmd = ESP_SD_PIN_CMD;
    slot_config.d0 = ESP_SD_PIN_D0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting filesystem");

    // 尝试挂载SD卡
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                          "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place.",
                     esp_err_to_name(ret));
        }
        // 不返回错误，而是返回NULL表示挂载失败
        card = NULL;
    }
    else
    {
        ESP_LOGI(TAG, "Filesystem mounted successfully");
        // 打印SD卡信息
        sdmmc_card_print_info(stdout, card);
    }

    return card; // 返回NULL或有效的card指针
}

// 辅助函数：修剪字符串前后空格
std::string trim(const std::string &s)
{
    size_t start = s.find_first_not_of(" \t");
    size_t end = s.find_last_not_of(" \t");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

// 辅助函数：转换为小写（增强大小写不敏感匹配）
std::string toLower(const std::string &s)
{
    std::string res;
    for (char c : s)
        res += tolower(c);
    return res;
}

// 实际执行SD卡扫描的私有函数（更新缓存）
void ScanAndCacheMusicFiles()
{
    cached_music_files_.clear(); // 清空旧缓存
    DIR *dir = opendir("/sdcard");

    if (!dir)
    {
        ESP_LOGE(TAG, "Failed to open SD card directory: %s", strerror(errno));
        is_cache_valid_ = false;
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        // 只处理普通文件（排除目录、链接等）
        if (entry->d_type != DT_REG)
            continue;

        std::string filename = entry->d_name;
        std::string trimmed_filename = trim(filename); // 去空格

        // 检查扩展名（支持.mp3/.MP3/.Mp3等）
        size_t ext_pos = trimmed_filename.rfind('.');
        if (ext_pos == std::string::npos)
            continue;

        std::string ext = trimmed_filename.substr(ext_pos + 1);
        if (toLower(ext) != "mp3")
            continue;

        // 提取显示名（去扩展名、去空格）
        std::string display_name = trim(trimmed_filename.substr(0, ext_pos));
        if (display_name.empty())
            continue; // 跳过空文件名

        // 缓存文件信息
        cached_music_files_.push_back({.filename = filename,
                                       .displayName = display_name});
        ESP_LOGI(TAG, "Cached music file: %s (display: %s)", filename.c_str(), display_name.c_str());
    }

    closedir(dir);
    is_cache_valid_ = true; // 标记缓存有效
    ESP_LOGI(TAG, "Scan completed, cached %zu music files", cached_music_files_.size());
}

// 辅助函数：计算匹配度（关键词在文件名中的占比，值越大越匹配）
float CalculateMatchScore(const std::string &keyword, const std::string &filename)
{
    if (keyword.empty())
        return 0.0f;
    size_t keyword_len = keyword.size();
    size_t filename_len = filename.size();
    size_t pos = filename.find(keyword);
    if (pos == std::string::npos)
        return 0.0f;
    // 匹配度 = 关键词长度 / 文件名长度（越长的关键词匹配越优先）
    return static_cast<float>(keyword_len) / filename_len;
}

// 手动刷新缓存（如SD卡内容更新时调用）
void RefreshMusicCache()
{
    is_cache_valid_ = false;
    ScanAndCacheMusicFiles();
}

// 获取缓存的音乐文件列表（自动刷新无效缓存）
const std::vector<MusicFile> &GetCachedMusicFiles()
{
    if (!is_cache_valid_)
    {
        ESP_LOGI(TAG, "Cache invalid, scanning SD card...");
        ScanAndCacheMusicFiles();
    }
    return cached_music_files_;
}

bool Esp32MusicSdcard::PlayMusicByName(const std::string &keyword)
{
    // 1. 获取缓存的音乐文件列表
    const std::vector<MusicFile> &music_files = GetCachedMusicFiles();
    if (music_files.empty())
    {
        ESP_LOGE(TAG, "No music files found in SD card");
        return false;
    }

    // 2. 预处理关键词（去空格、特殊字符、转小写）
    std::string processed_keyword = toLower(trim(keyword));
    if (processed_keyword.empty())
    {
        ESP_LOGE(TAG, "Keyword is empty after processing");
        return false;
    }

    // 3. 模糊匹配并计算匹配度
    std::vector<std::pair<const MusicFile *, float>> matches;
    for (const auto &file : music_files)
    {
        std::string processed_filename = toLower(trim(file.displayName));
        if (processed_filename.find(processed_keyword) != std::string::npos)
        {
            float score = CalculateMatchScore(processed_keyword, processed_filename);
            matches.emplace_back(&file, score);
        }
    }

    // 4. 处理匹配结果
    if (!matches.empty())
    {
        // 匹配成功，按匹配度排序并选择最佳结果
        std::sort(matches.begin(), matches.end(),
                  [](const auto &a, const auto &b)
                  { return a.second > b.second; });
        const MusicFile *selected_file = matches[0].first;
        current_music_url_ = "/sdcard/" + selected_file->filename;
        current_lyric_url_ = "/sdcard/" + selected_file->displayName + ".lrc";
        ESP_LOGI(TAG, "Selected music: %s (match score: %.2f)",
                 current_music_url_.c_str(), matches[0].second);
    }
    else
    {
        // 匹配失败，随机选择一首歌曲
        ESP_LOGW(TAG, "No matching music for keyword: %s, selecting random song", keyword.c_str());

        // 初始化随机数生成器（仅首次调用时初始化）
        static bool random_seeded = false;
        if (!random_seeded)
        {
            srand(time(nullptr)); // 使用系统时间作为随机数种子
            random_seeded = true;
        }

        // 随机选择索引
        size_t random_index = rand() % music_files.size();
        const MusicFile &random_file = music_files[random_index];
        current_music_url_ = "/sdcard/" + random_file.filename;
        current_lyric_url_ = "/sdcard/" + random_file.displayName + ".lrc";
        ESP_LOGI(TAG, "Randomly selected music: %s", current_music_url_.c_str());
    }

    ESP_LOGI(TAG, "Lyric path: %s", current_lyric_url_.c_str());

    // 开始播放
    return StartStreaming(current_music_url_);
}

Esp32MusicSdcard::Esp32MusicSdcard() : last_downloaded_data_(), current_music_url_(), current_song_name_(),
                                       song_name_displayed_(false), current_lyric_url_(), lyrics_(),
                                       current_lyric_index_(-1), lyric_thread_(), is_lyric_running_(false),
                                       is_playing_(false), is_downloading_(false),
                                       play_thread_(), download_thread_(), audio_buffer_(), buffer_mutex_(),
                                       buffer_cv_(), buffer_size_(0), mp3_decoder_(nullptr), mp3_frame_info_(),
                                       mp3_decoder_initialized_(false)
{
    sdmmc_card_t *card = mount_sdcard();
    if (card != NULL)
    {
        // SD卡挂载成功，可以继续执行文件操作
        ESP_LOGI(TAG, "SD card mounted successfully, proceeding...");
    }
    else
    {
        // SD卡挂载失败，执行备选逻辑或提示用户
        ESP_LOGW(TAG, "SD card mount failed, using fallback options...");
        // 可以选择使用内置文件系统、网络资源或其他替代方案
    }
    ESP_LOGI(TAG, "Music player initialized");
    InitializeMp3Decoder();
}

Esp32MusicSdcard::~Esp32MusicSdcard()
{
    ESP_LOGI(TAG, "Destroying music player - stopping all operations");

    // 停止所有操作
    is_downloading_ = false;
    is_playing_ = false;
    is_lyric_running_ = false;

    // 通知所有等待的线程
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    // 等待下载线程结束，设置5秒超时
    if (download_thread_.joinable())
    {
        ESP_LOGI(TAG, "Waiting for download thread to finish (timeout: 5s)");
        auto start_time = std::chrono::steady_clock::now();

        // 等待线程结束
        bool thread_finished = false;
        while (!thread_finished)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - start_time)
                               .count();

            if (elapsed >= 5)
            {
                ESP_LOGW(TAG, "Download thread join timeout after 5 seconds");
                break;
            }

            // 再次设置停止标志，确保线程能够检测到
            is_downloading_ = false;

            // 通知条件变量
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                buffer_cv_.notify_all();
            }

            // 检查线程是否已经结束
            if (!download_thread_.joinable())
            {
                thread_finished = true;
            }

            // 定期打印等待信息
            if (elapsed > 0 && elapsed % 1 == 0)
            {
                ESP_LOGI(TAG, "Still waiting for download thread to finish... (%ds)", (int)elapsed);
            }
        }

        if (download_thread_.joinable())
        {
            download_thread_.join();
        }
        ESP_LOGI(TAG, "Download thread finished");
    }

    // 等待播放线程结束，设置3秒超时
    if (play_thread_.joinable())
    {
        ESP_LOGI(TAG, "Waiting for playback thread to finish (timeout: 3s)");
        auto start_time = std::chrono::steady_clock::now();

        bool thread_finished = false;
        while (!thread_finished)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - start_time)
                               .count();

            if (elapsed >= 3)
            {
                ESP_LOGW(TAG, "Playback thread join timeout after 3 seconds");
                break;
            }

            // 再次设置停止标志
            is_playing_ = false;

            // 通知条件变量
            {
                std::lock_guard<std::mutex> lock(buffer_mutex_);
                buffer_cv_.notify_all();
            }

            // 检查线程是否已经结束
            if (!play_thread_.joinable())
            {
                thread_finished = true;
            }
        }

        if (play_thread_.joinable())
        {
            play_thread_.join();
        }
        ESP_LOGI(TAG, "Playback thread finished");
    }

    // 等待歌词线程结束
    if (lyric_thread_.joinable())
    {
        ESP_LOGI(TAG, "Waiting for lyric thread to finish");
        lyric_thread_.join();
        ESP_LOGI(TAG, "Lyric thread finished");
    }

    // 清理缓冲区和MP3解码器
    ClearAudioBuffer();
    CleanupMp3Decoder();

    ESP_LOGI(TAG, "Music player destroyed successfully");
}

bool Esp32MusicSdcard::Download(const std::string &song_name)
{
    ESP_LOGI(TAG, "Starting to get music details for: %s", song_name.c_str());

    // 清空之前的下载数据
    last_downloaded_data_.clear();
    // 根据索引播放音乐
    bool success = PlayMusicByName(song_name);
    if (!success)
    {
        ESP_LOGE(TAG, "Failed to play music at index: %d", success);
        return false;
    }

    // 启动歌词下载和显示
    if (is_lyric_running_)
    {
        is_lyric_running_ = false;
        if (lyric_thread_.joinable())
        {
            lyric_thread_.join();
        }
    }

    is_lyric_running_ = true;
    current_lyric_index_ = -1;
    lyrics_.clear();

    lyric_thread_ = std::thread(&Esp32MusicSdcard::LyricDisplayThread, this);
    return true;
}

bool Esp32MusicSdcard::Play()
{
    if (is_playing_.load())
    { // 使用atomic的load()
        ESP_LOGW(TAG, "Music is already playing");
        return true;
    }

    if (last_downloaded_data_.empty())
    {
        ESP_LOGE(TAG, "No music data to play");
        return false;
    }

    // 清理之前的播放线程
    if (play_thread_.joinable())
    {
        play_thread_.join();
    }

    // 实际应调用流式播放接口
    return StartStreaming(current_music_url_);
}

bool Esp32MusicSdcard::Stop()
{
    if (!is_playing_ && !is_downloading_)
    {
        ESP_LOGW(TAG, "Music is not playing or downloading");
        return true;
    }

    ESP_LOGI(TAG, "Stopping music playback and download");

    // 停止下载和播放
    is_downloading_ = false;
    is_playing_ = false;

    // 重置采样率到原始值
    ResetSampleRate();

    // 通知所有等待的线程
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    // 等待线程结束
    if (download_thread_.joinable())
    {
        download_thread_.join();
    }
    if (play_thread_.joinable())
    {
        play_thread_.join();
    }

    // 清空缓冲区
    ClearAudioBuffer();

    ESP_LOGI(TAG, "Music stopped successfully");
    return true;
}

std::string Esp32MusicSdcard::GetDownloadResult()
{
    return last_downloaded_data_;
}

// 开始流式播放
bool Esp32MusicSdcard::StartStreaming(const std::string &music_url)
{
    if (music_url.empty())
    {
        ESP_LOGE(TAG, "Music URL is empty");
        return false;
    }

    // 停止之前的播放和下载
    is_downloading_ = false;
    is_playing_ = false;

    // 等待之前的线程完全结束
    if (download_thread_.joinable())
    {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all(); // 通知线程退出
        }
        download_thread_.join();
    }

    ESP_LOGW(TAG, "Starting streaming for URL122222222222222222: %s", music_url.c_str());
    if (play_thread_.joinable())
    {
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_cv_.notify_all(); // 通知线程退出
        }
        play_thread_.join();
    }

    // 清空缓冲区
    // ClearAudioBuffer();
    // 配置线程栈大小以避免栈溢出
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = 8192; // 8KB栈大小
    cfg.prio = 5;          // 中等优先级
    cfg.thread_name = "audio_stream";
    esp_pthread_set_cfg(&cfg);

    // 开始下载线程
    is_downloading_ = true;
    download_thread_ = std::thread(&Esp32MusicSdcard::DownloadAudioStreamFromSDCard, this, music_url);
    // 开始播放线程（会等待缓冲区有足够数据）
    is_playing_ = true;
    play_thread_ = std::thread(&Esp32MusicSdcard::PlayAudioStream, this);

    ESP_LOGI(TAG, "Streaming threads started successfully");
    return true;
}

// 停止流式播放
bool Esp32MusicSdcard::StopStreaming()
{
    ESP_LOGI(TAG, "Stopping music streaming - current state: downloading=%d, playing=%d",
             is_downloading_.load(), is_playing_.load());

    // 1. 停止歌词线程（同步音频停止状态）
    is_lyric_running_ = false;
    if (lyric_thread_.joinable())
    {
        lyric_thread_.join();
        ESP_LOGI(TAG, "Lyric thread stopped");
    }

    // 2. 停止下载和播放标志
    is_downloading_ = false;
    is_playing_ = false;

    // 3. 重置采样率
    ResetSampleRate();

    // 4. 通知线程退出并等待结束（确保旧线程完全停止）
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    // 等待下载线程结束（增加超时保护，避免卡死）
    if (download_thread_.joinable())
    {
        auto start = std::chrono::steady_clock::now();
        while (download_thread_.joinable())
        {
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() > 3)
            {
                ESP_LOGW(TAG, "Download thread join timeout");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            download_thread_.join();
        }
    }

    // 等待播放线程结束
    if (play_thread_.joinable())
    {
        auto start = std::chrono::steady_clock::now();
        while (play_thread_.joinable())
        {
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count() > 3)
            {
                ESP_LOGW(TAG, "Play thread join timeout");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            play_thread_.join();
        }
    }

    // 5. 强制清空缓冲区（关键：清除残留音频数据）
    ClearAudioBuffer();

    // 6. 重置解码器状态（清除上一首解码上下文）
    CleanupMp3Decoder();
    InitializeMp3Decoder();

    // 7. 重置歌名显示和歌词状态
    song_name_displayed_ = false;
    current_lyric_index_ = -1;
    lyrics_.clear();

    ESP_LOGI(TAG, "Music streaming stopped completely");
    return true;
}

void Esp32MusicSdcard::DownloadAudioStreamFromSDCard(const std::string &file_path)
{
    ESP_LOGD(TAG, "Starting audio stream from SD card file: %s", file_path.c_str());

    // 验证文件路径
    if (file_path.empty())
    {
        ESP_LOGE(TAG, "Invalid file path: %s", file_path.c_str());
        is_downloading_ = false;
        return;
    }

    // 打开文件
    FILE *file = fopen(file_path.c_str(), "rb");
    if (!file)
    {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path.c_str());
        is_downloading_ = false;
        return;
    }

    ESP_LOGI(TAG, "Started reading audio stream from SD card");

    const size_t chunk_size = 4096;                                         // 4KB每块
    char *buffer = (char *)heap_caps_malloc(chunk_size, MALLOC_CAP_SPIRAM); // 从堆分配
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for buffer");
        fclose(file);
        is_downloading_ = false;
        return;
    }

    size_t total_downloaded = 0;

    while (is_downloading_)
    {
        size_t bytes_read = fread(buffer, 1, chunk_size, file);
        if (bytes_read == 0)
        {
            if (feof(file))
            {
                ESP_LOGI(TAG, "End of file reached, total bytes read: %d", total_downloaded);
                break;
            }
            else
            {
                ESP_LOGE(TAG, "Failed to read audio data: error code %d", ferror(file));
                break;
            }
        }

        uint8_t *chunk_data = (uint8_t *)heap_caps_malloc(bytes_read, MALLOC_CAP_SPIRAM);
        if (!chunk_data)
        {
            ESP_LOGE(TAG, "Failed to allocate memory for audio chunk");
            break;
        }
        memcpy(chunk_data, buffer, bytes_read);

        {
            std::unique_lock<std::mutex> lock(buffer_mutex_);
            // 修正：等待条件中加入 is_downloading_ 检查
            buffer_cv_.wait(lock, [this]
                            { return !is_downloading_ || buffer_size_ < MAX_BUFFER_SIZE; });

            // 再次检查标志，避免虚假唤醒
            if (!is_downloading_)
            {
                heap_caps_free(chunk_data);
                break;
            }

            if (is_downloading_)
            {
                audio_buffer_.push(AudioChunk(chunk_data, bytes_read));
                buffer_size_ += bytes_read;
                total_downloaded += bytes_read;

                buffer_cv_.notify_one();

                if (total_downloaded % (256 * 1024) == 0)
                {
                    ESP_LOGI(TAG, "Downloaded %d bytes, buffer size: %d", total_downloaded, buffer_size_);
                }
            }
            else
            {
                heap_caps_free(chunk_data);
                break;
            }
        }
    }

    heap_caps_free(buffer);
    fclose(file);
    is_downloading_ = false;
    // 通知播放线程下载完成
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        buffer_cv_.notify_all();
    }

    ESP_LOGI(TAG, "Audio stream download thread finished");
}

// 流式播放音频数据
void Esp32MusicSdcard::PlayAudioStream()
{
    ESP_LOGI(TAG, "Starting audio stream playback");

    // 初始化时间跟踪变量
    current_play_time_ms_ = 0;
    last_frame_time_ms_ = 0;
    total_frames_decoded_ = 0;

    auto codec = Board::GetInstance().GetAudioCodec();
    if (!codec || !codec->output_enabled())
    {
        ESP_LOGE(TAG, "Audio codec not available or not enabled");
        is_playing_ = false;
        return;
    }

    if (!mp3_decoder_initialized_)
    {
        ESP_LOGE(TAG, "MP3 decoder not initialized");
        is_playing_ = false;
        return;
    }

    // 等待缓冲区有足够数据开始播放
    {
        std::unique_lock<std::mutex> lock(buffer_mutex_);
        buffer_cv_.wait(lock, [this]
                        { return buffer_size_ >= MIN_BUFFER_SIZE || (!is_downloading_ && !audio_buffer_.empty()); });
    }

    ESP_LOGI(TAG, "Starting playback with buffer size: %d", buffer_size_);

    size_t total_played = 0;
    uint8_t *mp3_input_buffer = nullptr;
    int bytes_left = 0;
    uint8_t *read_ptr = nullptr;

    // 分配MP3输入缓冲区
    mp3_input_buffer = (uint8_t *)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!mp3_input_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate MP3 input buffer");
        is_playing_ = false;
        return;
    }

    // 标记是否已经处理过ID3标签
    bool id3_processed = false;

    while (is_playing_)
    {
        // 检查设备状态，只有在空闲状态才播放音乐
        auto &app = Application::GetInstance();
        DeviceState current_state = app.GetDeviceState();

        // 等小智把话说完了，变成聆听状态之后，马上转成待机状态，进入音乐播放
        if (current_state == kDeviceStateListening)
        {
            ESP_LOGI(TAG, "Device is in listening state, switching to idle state for music playback");
            // 切换状态
            app.ToggleChatState(); // 变成待机状态
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }
        else if (current_state != kDeviceStateIdle)
        { // 不是待机状态，就一直卡在这里，不让播放音乐
            ESP_LOGD(TAG, "Device state is %d, pausing music playback", current_state);
            // 如果不是空闲状态，暂停播放
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // 设备状态检查通过，显示当前播放的歌名
        if (!song_name_displayed_ && !current_song_name_.empty())
        {
            auto &board = Board::GetInstance();
            auto display = board.GetDisplay();
            if (display)
            {
                // 格式化歌名显示为《歌名》播放中...
                std::string formatted_song_name = "《" + current_song_name_ + "》播放中...";
                display->SetMusicInfo(formatted_song_name.c_str());
                ESP_LOGI(TAG, "Displaying song name: %s", formatted_song_name.c_str());
                song_name_displayed_ = true;
            }
        }

        // 如果需要更多MP3数据，从缓冲区读取
        if (bytes_left < 4096)
        { // 保持至少4KB数据用于解码
            AudioChunk chunk;

            // 从缓冲区获取音频数据
            {
                std::unique_lock<std::mutex> lock(buffer_mutex_);
                if (audio_buffer_.empty())
                {
                    if (!is_downloading_)
                    {
                        // 下载完成且缓冲区为空，播放结束
                        ESP_LOGI(TAG, "Playback finished, total played: %d bytes", total_played);
                        break;
                    }
                    // 等待新数据
                    buffer_cv_.wait(lock, [this]
                                    { return !audio_buffer_.empty() || !is_downloading_; });
                    if (audio_buffer_.empty())
                    {
                        continue;
                    }
                }

                chunk = audio_buffer_.front();
                audio_buffer_.pop();
                buffer_size_ -= chunk.size;

                // 通知下载线程缓冲区有空间
                buffer_cv_.notify_one();
            }

            // 将新数据添加到MP3输入缓冲区
            if (chunk.data && chunk.size > 0)
            {
                // 移动剩余数据到缓冲区开头
                if (bytes_left > 0 && read_ptr != mp3_input_buffer)
                {
                    memmove(mp3_input_buffer, read_ptr, bytes_left);
                }

                // 检查缓冲区空间
                size_t space_available = 8192 - bytes_left;
                size_t copy_size = std::min(chunk.size, space_available);

                // 复制新数据
                memcpy(mp3_input_buffer + bytes_left, chunk.data, copy_size);
                bytes_left += copy_size;
                read_ptr = mp3_input_buffer;

                // 检查并跳过ID3标签（仅在开始时处理一次）
                if (!id3_processed && bytes_left >= 10)
                {
                    size_t id3_skip = SkipId3Tag(read_ptr, bytes_left);
                    if (id3_skip > 0)
                    {
                        read_ptr += id3_skip;
                        bytes_left -= id3_skip;
                        ESP_LOGI(TAG, "Skipped ID3 tag: %u bytes", (unsigned int)id3_skip);
                    }
                    id3_processed = true;
                }

                // 释放chunk内存
                heap_caps_free(chunk.data);
            }
        }

        // 尝试找到MP3帧同步
        int sync_offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync_offset < 0)
        {
            ESP_LOGW(TAG, "No MP3 sync word found, skipping %d bytes", bytes_left);
            bytes_left = 0;
            continue;
        }

        // 跳过到同步位置
        if (sync_offset > 0)
        {
            read_ptr += sync_offset;
            bytes_left -= sync_offset;
        }

        // 解码MP3帧
        int16_t pcm_buffer[2304];
        int decode_result = MP3Decode(mp3_decoder_, &read_ptr, &bytes_left, pcm_buffer, 0);

        if (decode_result == 0)
        {
            // 解码成功，获取帧信息
            MP3GetLastFrameInfo(mp3_decoder_, &mp3_frame_info_);
            total_frames_decoded_++;

            // 基本的帧信息有效性检查，防止除零错误
            if (mp3_frame_info_.samprate == 0 || mp3_frame_info_.nChans == 0)
            {
                ESP_LOGW(TAG, "Invalid frame info: rate=%d, channels=%d, skipping",
                         mp3_frame_info_.samprate, mp3_frame_info_.nChans);
                continue;
            }

            // 计算当前帧的持续时间(毫秒)
            int frame_duration_ms = (mp3_frame_info_.outputSamps * 1000) /
                                    (mp3_frame_info_.samprate * mp3_frame_info_.nChans);

            // 更新当前播放时间
            current_play_time_ms_ += frame_duration_ms;

            ESP_LOGD(TAG, "Frame %d: time=%lldms, duration=%dms, rate=%d, ch=%d",
                     total_frames_decoded_, current_play_time_ms_, frame_duration_ms,
                     mp3_frame_info_.samprate, mp3_frame_info_.nChans);

            // 更新歌词显示
            int buffer_latency_ms = 600; // 实测调整值
            UpdateLyricDisplay(current_play_time_ms_ + buffer_latency_ms);

            // 将PCM数据发送到Application的音频解码队列
            if (mp3_frame_info_.outputSamps > 0)
            {
                int16_t *final_pcm_data = pcm_buffer;
                int final_sample_count = mp3_frame_info_.outputSamps;
                std::vector<int16_t> mono_buffer;

                // 如果是双通道，转换为单通道混合
                if (mp3_frame_info_.nChans == 2)
                {
                    // 双通道转单通道：将左右声道混合
                    int stereo_samples = mp3_frame_info_.outputSamps; // 包含左右声道的总样本数
                    int mono_samples = stereo_samples / 2;            // 实际的单声道样本数

                    mono_buffer.resize(mono_samples);

                    for (int i = 0; i < mono_samples; ++i)
                    {
                        // 混合左右声道 (L + R) / 2
                        int left = pcm_buffer[i * 2];      // 左声道
                        int right = pcm_buffer[i * 2 + 1]; // 右声道
                        mono_buffer[i] = (int16_t)((left + right) / 2);
                    }

                    final_pcm_data = mono_buffer.data();
                    final_sample_count = mono_samples;

                    ESP_LOGD(TAG, "Converted stereo to mono: %d -> %d samples",
                             stereo_samples, mono_samples);
                }
                else if (mp3_frame_info_.nChans == 1)
                {
                    // 已经是单声道，无需转换
                    ESP_LOGD(TAG, "Already mono audio: %d samples", final_sample_count);
                }
                else
                {
                    ESP_LOGW(TAG, "Unsupported channel count: %d, treating as mono",
                             mp3_frame_info_.nChans);
                }

                // 创建AudioStreamPacket
                AudioStreamPacket packet;
                packet.sample_rate = mp3_frame_info_.samprate;
                packet.frame_duration = 60; // 使用Application默认的帧时长
                packet.timestamp = 0;

                // 将int16_t PCM数据转换为uint8_t字节数组
                size_t pcm_size_bytes = final_sample_count * sizeof(int16_t);
                packet.payload.resize(pcm_size_bytes);
                memcpy(packet.payload.data(), final_pcm_data, pcm_size_bytes);

                ESP_LOGD(TAG, "Sending %d PCM samples (%d bytes, rate=%d, channels=%d->1) to Application",
                         final_sample_count, pcm_size_bytes, mp3_frame_info_.samprate, mp3_frame_info_.nChans);

                // 发送到Application的音频解码队列
                app.AddAudioData(std::move(packet));
                total_played += pcm_size_bytes;

                // 打印播放进度
                if (total_played % (128 * 1024) == 0)
                {
                    ESP_LOGI(TAG, "Played %d bytes, buffer size: %d", total_played, buffer_size_);
                }
            }
        }
        else
        {
            // 解码失败
            ESP_LOGW(TAG, "MP3 decode failed with error: %d", decode_result);

            // 跳过一些字节继续尝试
            if (bytes_left > 1)
            {
                read_ptr++;
                bytes_left--;
            }
            else
            {
                bytes_left = 0;
            }
        }
    }

    // 清理
    if (mp3_input_buffer)
    {
        heap_caps_free(mp3_input_buffer);
    }

    // 播放结束时清空歌名显示
    auto &board = Board::GetInstance();
    auto display = board.GetDisplay();
    if (display)
    {
        display->SetMusicInfo(""); // 清空歌名显示
        ESP_LOGI(TAG, "Cleared song name display on playback end");
    }

    // 重置采样率到原始值
    ResetSampleRate();

    // 播放结束时保持音频输出启用状态，让Application管理
    // 不在这里禁用音频输出，避免干扰其他音频功能
    ESP_LOGI(TAG, "Audio stream playback finished, total played: %d bytes", total_played);

    is_playing_ = false;
}

// 清空音频缓冲区
void Esp32MusicSdcard::ClearAudioBuffer()
{
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    while (!audio_buffer_.empty())
    {
        AudioChunk chunk = audio_buffer_.front();
        audio_buffer_.pop();
        if (chunk.data)
        {
            heap_caps_free(chunk.data);
        }
    }

    buffer_size_ = 0;
    ESP_LOGI(TAG, "Audio buffer cleared");
}

// 初始化MP3解码器
bool Esp32MusicSdcard::InitializeMp3Decoder()
{
    mp3_decoder_ = MP3InitDecoder();
    if (mp3_decoder_ == nullptr)
    {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        mp3_decoder_initialized_ = false;
        return false;
    }

    mp3_decoder_initialized_ = true;
    ESP_LOGI(TAG, "MP3 decoder initialized successfully");
    return true;
}

// 清理MP3解码器
void Esp32MusicSdcard::CleanupMp3Decoder()
{
    if (mp3_decoder_ != nullptr)
    {
        MP3FreeDecoder(mp3_decoder_);
        mp3_decoder_ = nullptr;
    }
    mp3_decoder_initialized_ = false;
    ESP_LOGI(TAG, "MP3 decoder cleaned up");
}

// 重置采样率到原始值
void Esp32MusicSdcard::ResetSampleRate()
{
    auto &board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    // if (codec && codec->original_output_sample_rate() > 0 &&
    //     codec->output_sample_rate() != codec->original_output_sample_rate())
    // {
    //     ESP_LOGI(TAG, "重置采样率：从 %d Hz 重置到原始值 %d Hz",
    //              codec->output_sample_rate(), codec->original_output_sample_rate());
    //     if (codec->SetOutputSampleRate(-1))
    //     { // -1 表示重置到原始值
    //         ESP_LOGI(TAG, "成功重置采样率到原始值: %d Hz", codec->output_sample_rate());
    //     }
    //     else
    //     {
    //         ESP_LOGW(TAG, "无法重置采样率到原始值");
    //     }
    // }
}

// 跳过MP3文件开头的ID3标签
size_t Esp32MusicSdcard::SkipId3Tag(uint8_t *data, size_t size)
{
    if (!data || size < 10)
    {
        return 0;
    }

    // 检查ID3v2标签头 "ID3"
    if (memcmp(data, "ID3", 3) != 0)
    {
        return 0;
    }

    // 计算标签大小（synchsafe integer格式）
    uint32_t tag_size = ((uint32_t)(data[6] & 0x7F) << 21) |
                        ((uint32_t)(data[7] & 0x7F) << 14) |
                        ((uint32_t)(data[8] & 0x7F) << 7) |
                        ((uint32_t)(data[9] & 0x7F));

    // ID3v2头部(10字节) + 标签内容
    size_t total_skip = 10 + tag_size;

    // 确保不超过可用数据大小
    if (total_skip > size)
    {
        total_skip = size;
    }

    ESP_LOGI(TAG, "Found ID3v2 tag, skipping %u bytes", (unsigned int)total_skip);
    return total_skip;
}



bool Esp32MusicSdcard::ReadLyricFromSDCard(const std::string &file_path)
{
    std::ifstream file(file_path);
    if (!file.is_open())
    {
        ESP_LOGW(TAG, "Failed to open lyric file: %s", file_path.c_str());
        return false; // 如果打开文件失败，返回 false
    }

    std::stringstream lyric_content;
    std::string line;

    while (std::getline(file, line))
    {
        lyric_content << line << '\n'; // 将文件内容写入stringstream
    }
    file.close();

    // 读取文件后，将歌词内容传递给ParseLyrics进行解析
    ParseLyrics(lyric_content.str());

    return true; // 如果文件读取并解析成功，返回 true
}

// 解析歌词
bool Esp32MusicSdcard::ParseLyrics(const std::string &lyric_content)
{
    ESP_LOGI(TAG, "Parsing lyrics content");

    // 使用锁保护lyrics_数组访问
    std::lock_guard<std::mutex> lock(lyrics_mutex_);

    lyrics_.clear();

    // 按行分割歌词内容
    std::istringstream stream(lyric_content);
    std::string line;

    while (std::getline(stream, line))
    {
        // 去除行尾的回车符
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        // 跳过空行
        if (line.empty())
        {
            continue;
        }

        // 解析LRC格式: [mm:ss.xx]歌词文本
        if (line.length() > 10 && line[0] == '[')
        {
            size_t close_bracket = line.find(']');
            if (close_bracket != std::string::npos)
            {
                std::string tag_or_time = line.substr(1, close_bracket - 1);
                std::string content = line.substr(close_bracket + 1);

                // 检查是否是元数据标签而不是时间戳
                // 元数据标签通常是 [ti:标题], [ar:艺术家], [al:专辑] 等
                size_t colon_pos = tag_or_time.find(':');
                if (colon_pos != std::string::npos)
                {
                    std::string left_part = tag_or_time.substr(0, colon_pos);

                    // 检查冒号左边是否是时间（数字）
                    bool is_time_format = true;
                    for (char c : left_part)
                    {
                        if (!isdigit(c))
                        {
                            is_time_format = false;
                            break;
                        }
                    }

                    // 如果不是时间格式，跳过这一行（元数据标签）
                    if (!is_time_format)
                    {
                        // 可以在这里处理元数据，例如提取标题、艺术家等信息
                        ESP_LOGD(TAG, "Skipping metadata tag: [%s]", tag_or_time.c_str());
                        continue;
                    }

                    // 是时间格式，解析时间戳
                    try
                    {
                        int minutes = std::stoi(tag_or_time.substr(0, colon_pos));
                        float seconds = std::stof(tag_or_time.substr(colon_pos + 1));
                        int timestamp_ms = minutes * 60 * 1000 + (int)(seconds * 1000);

                        // 安全处理歌词文本，确保UTF-8编码正确
                        std::string safe_lyric_text;
                        if (!content.empty())
                        {
                            // 创建安全副本并验证字符串
                            safe_lyric_text = content;
                            // 确保字符串以null结尾
                            safe_lyric_text.shrink_to_fit();
                        }

                        lyrics_.push_back(std::make_pair(timestamp_ms, safe_lyric_text));

                        if (!safe_lyric_text.empty())
                        {
                            // 限制日志输出长度，避免中文字符截断问题
                            size_t log_len = std::min(safe_lyric_text.length(), size_t(50));
                            std::string log_text = safe_lyric_text.substr(0, log_len);
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] %s", timestamp_ms, log_text.c_str());
                        }
                        else
                        {
                            ESP_LOGD(TAG, "Parsed lyric: [%d ms] (empty)", timestamp_ms);
                        }
                    }
                    catch (const std::exception &e)
                    {
                        ESP_LOGW(TAG, "Failed to parse time: %s", tag_or_time.c_str());
                    }
                }
            }
        }
    }

    // 按时间戳排序
    std::sort(lyrics_.begin(), lyrics_.end());

    ESP_LOGI(TAG, "Parsed %d lyric lines", lyrics_.size());
    return !lyrics_.empty();
}

// 歌词显示线程
void Esp32MusicSdcard::LyricDisplayThread()
{
    ESP_LOGI(TAG, "Lyric display thread started");

    if (!ReadLyricFromSDCard(current_lyric_url_))
    {
        ESP_LOGE(TAG, "Failed to download or parse lyrics");
        is_lyric_running_ = false;
        return;
    }
    // 定期检查是否需要更新显示(频率可以降低)
    while (is_lyric_running_ && is_playing_)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ESP_LOGI(TAG, "Lyric display thread finished");
}

void Esp32MusicSdcard::UpdateLyricDisplay(int64_t current_time_ms)
{
    std::lock_guard<std::mutex> lock(lyrics_mutex_);

    if (lyrics_.empty())
    {
        return;
    }

    // 查找当前应该显示的歌词
    int new_lyric_index = -1;

    // 从当前歌词索引开始查找，提高效率
    int start_index = (current_lyric_index_.load() >= 0) ? current_lyric_index_.load() : 0;

    // 正向查找：找到最后一个时间戳小于等于当前时间的歌词
    for (int i = start_index; i < (int)lyrics_.size(); i++)
    {
        if (lyrics_[i].first <= current_time_ms)
        {
            new_lyric_index = i;
        }
        else
        {
            break; // 时间戳已超过当前时间
        }
    }

    // 如果没有找到(可能当前时间比第一句歌词还早)，显示空
    if (new_lyric_index == -1)
    {
        new_lyric_index = -1;
    }

    // 如果歌词索引发生变化，更新显示
    if (new_lyric_index != current_lyric_index_)
    {
        current_lyric_index_ = new_lyric_index;

        auto &board = Board::GetInstance();
        auto display = board.GetDisplay();
        if (display)
        {
            std::string lyric_text;

            if (current_lyric_index_ >= 0 && current_lyric_index_ < (int)lyrics_.size())
            {
                lyric_text = lyrics_[current_lyric_index_].second;
            }

            // 显示歌词
            display->SetChatMessage("lyric", lyric_text.c_str());

            ESP_LOGD(TAG, "Lyric update at %lldms: %s",
                     current_time_ms,
                     lyric_text.empty() ? "(no lyric)" : lyric_text.c_str());
        }
    }
}