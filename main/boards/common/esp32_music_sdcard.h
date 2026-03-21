#ifndef ESP32_MUSIC_SDCARD_H
#define ESP32_MUSIC_SDCARD_H

/**
 * SD-card MP3 playback (Xiaozhi SP32SD1114 reference port).
 * Uses the firmware's existing FAT mount at /sdcard (see SdCard / SdCardStartup),
 * decodes MP3 with esp-libhelix-mp3, and feeds PCM via Application::AddAudioData
 * so built-in .p3 / Opus alert sounds are unchanged.
 */

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <string.h>
#include <fstream>
#include <sstream>

#include <esp_log.h>

#include "music.h"

extern "C" {
#include "mp3dec.h"
}

struct MusicFile {
    std::string filename;
    std::string displayName;
};

class Esp32MusicSdcard : public Music {
    friend void sd_mp3_single_task_thunk(void *param);

private:
    std::string last_downloaded_data_;
    std::string current_music_url_;
    std::string current_song_name_;
    bool song_name_displayed_;

    std::string current_lyric_url_;
    std::vector<std::pair<int, std::string>> lyrics_;
    std::mutex lyrics_mutex_;
    std::atomic<int> current_lyric_index_;
    std::thread lyric_thread_;
    std::atomic<bool> is_lyric_running_;
    std::atomic<bool> is_playing_;
    std::atomic<bool> is_downloading_;
    std::atomic<void*> single_play_task_{nullptr};
    int64_t current_play_time_ms_;
    int64_t last_frame_time_ms_;
    int total_frames_decoded_;

    std::queue<AudioChunk> audio_buffer_;
    std::mutex buffer_mutex_;
    std::condition_variable buffer_cv_;
    size_t buffer_size_;
    static constexpr size_t MAX_BUFFER_SIZE = 512 * 1024;
    static constexpr size_t MIN_BUFFER_SIZE = 64 * 1024;

    HMP3Decoder mp3_decoder_;
    MP3FrameInfo mp3_frame_info_;
    bool mp3_decoder_initialized_;

    bool PlayMusicByName(const std::string& songName);
    void PlayAudioStreamFromFileSingleTask();
    void WaitForSinglePlayTaskExit(int timeout_ms);
    void ClearAudioBuffer();
    bool InitializeMp3Decoder();
    void CleanupMp3Decoder();
    void ResetSampleRate();

    bool ReadLyricFromSDCard(const std::string& file_path);
    bool ParseLyrics(const std::string& lyric_content);
    void LyricDisplayThread();
    void UpdateLyricDisplay(int64_t current_time_ms);

    size_t SkipId3Tag(uint8_t* data, size_t size);

public:
    Esp32MusicSdcard();
    ~Esp32MusicSdcard();

    bool Download(const std::string& song_name) override;
    bool Play() override;
    bool Stop() override;
    std::string GetDownloadResult() override;

    void DownloadAudioStreamFromSDCard(const std::string& file_path);

    bool StartStreaming(const std::string& music_url) override;
    bool StopStreaming() override;
    size_t GetBufferSize() const override { return buffer_size_; }
    bool IsDownloading() const override { return is_downloading_.load(); }
};

#endif
