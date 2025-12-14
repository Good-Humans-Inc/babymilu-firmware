#ifndef ESP32_MUSIC_SDCARD_H
#define ESP32_MUSIC_SDCARD_H

#include <string>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>


#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include <fstream>  // 用于文件输入输出流
#include <sstream>  // 用于字符串流
#include <string>   // 用于std::string
#include <iostream> // 用于标准输入输出（可选，用于调试）

#include <iconv.h> // 引入iconv库
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <ESP_LOG.h>
#include "music.h"

#define CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED 1
#define CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_1 1
#define MOUNT_POINT "/sdcard"
static char mount_point[] = MOUNT_POINT;
#define EXAMPLE_MAX_CHAR_SIZE 64

#define ESP_SD_PIN_CLK GPIO_NUM_16
#define ESP_SD_PIN_CMD GPIO_NUM_38
#define ESP_SD_PIN_D0 GPIO_NUM_17

// MP3解码器支持
extern "C" {
#include "mp3dec.h"
}
// 音乐文件信息结构体
struct MusicFile {
    std::string filename;    // 原始文件名
    std::string displayName; // 显示名称（可用于UI）
};


class Esp32MusicSdcard : public Music {
private:
    std::string last_downloaded_data_;
    std::string current_music_url_;
    std::string current_song_name_;
    bool song_name_displayed_;
    
    // 歌词相关
    std::string current_lyric_url_;
    std::vector<std::pair<int, std::string>> lyrics_;  // 时间戳和歌词文本
    std::mutex lyrics_mutex_;  // 保护lyrics_数组的互斥锁
    std::atomic<int> current_lyric_index_;
    std::thread lyric_thread_;
    std::atomic<bool> is_lyric_running_;
    std::atomic<bool> is_playing_;
    std::atomic<bool> is_downloading_;
    std::thread play_thread_;
    std::thread download_thread_;
    int64_t current_play_time_ms_;  // 当前播放时间(毫秒)
    int64_t last_frame_time_ms_;    // 上一帧的时间戳
    int total_frames_decoded_;      // 已解码的帧数

    // 音频缓冲区
    std::queue<AudioChunk> audio_buffer_;
    std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;
    size_t buffer_size_;
    static constexpr size_t MAX_BUFFER_SIZE = 512 * 1024;  // 512KB缓冲区
    static constexpr size_t MIN_BUFFER_SIZE = 64 * 1024;   // 64KB最小播放缓冲
    
    // MP3解码器相关
    HMP3Decoder mp3_decoder_;
    MP3FrameInfo mp3_frame_info_;
    bool mp3_decoder_initialized_;
    
    // 私有方法
    std::vector<MusicFile> ScanMusicFiles();
    bool PlayMusicByName(const std::string &songName);
    void DownloadAudioStream(const std::string& music_url);
    
    void PlayAudioStream();
    void ClearAudioBuffer();
    bool InitializeMp3Decoder();
    void CleanupMp3Decoder();
    void ResetSampleRate();  // 重置采样率到原始值
    
    // 歌词相关私有方法
    bool ReadLyricFromSDCard(const std::string& file_path);
    bool DownloadLyrics(const std::string& lyric_url);
    bool ParseLyrics(const std::string& lyric_content);
    void LyricDisplayThread();
    void UpdateLyricDisplay(int64_t current_time_ms);
    
    // ID3标签处理
    size_t SkipId3Tag(uint8_t* data, size_t size);

public:
    Esp32MusicSdcard();
    ~Esp32MusicSdcard();

    
    virtual bool Download(const std::string& song_name) override;
    virtual bool Play() override;
    virtual bool Stop() override;
    virtual std::string GetDownloadResult() override;
    
    // 新增方法
    virtual void DownloadAudioStreamFromSDCard(const std::string& file_path);

    virtual bool StartStreaming(const std::string& music_url) override;
    virtual bool StopStreaming() override;  // 停止流式播放
    virtual size_t GetBufferSize() const override { return buffer_size_; }
    virtual bool IsDownloading() const override { return is_downloading_; }
};

#endif // ESP32_MUSIC_H
