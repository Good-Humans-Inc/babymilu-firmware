#ifndef ANIMATION_UPDATER_H
#define ANIMATION_UPDATER_H

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

class Http;

class AnimationUpdater {
public:
    static AnimationUpdater& GetInstance();
    
    // Initialize the animation updater
    void Initialize();
    
    // Start/stop the updater
    void Start();
    void Stop();
    
    // Check if updater is running
    bool IsRunning() const { return is_running_.load(); }
    
    // Configuration methods
    void SetServerUrl(const std::string& url);
    void SetCheckInterval(uint32_t interval_seconds);
    void SetEnabled(bool enabled);
    
    // Get current configuration
    std::string GetServerUrl() const { return server_url_; }
    uint32_t GetCheckInterval() const { return check_interval_seconds_; }
    bool IsEnabled() const { return enabled_.load(); }
    
    // Manual check for updates
    void CheckForUpdates();
    
    // Manual download of animations_mega.bin (for testing/debugging)
    bool DownloadMegaFileNow();
    
    // Download mega.bin from a specific URL
    bool DownloadMegaFileFromUrl(const std::string& url);
    
    // Force immediate update check (bypasses success flag)
    bool ForceUpdateCheck();
    
    // Reset the first download success flag (for testing/debugging)
    void ResetFirstDownloadSuccess();
    
    // Get status information
    std::string GetStatusJson() const;
    
    // Version management
    std::string GetCurrentVersion() const;
    void SetCurrentVersion(const std::string& version);
    
    // Trigger the update loop (runs in a separate task)
    void TriggerUpdateLoop();

    // Return the SHA-256 of the currently installed stable animation bundle.
    bool GetInstalledAnimationSha256(std::string& sha256);

    // Repair or discard files left by a power loss during atomic installation.
    void RecoverInterruptedInstall();

    // Apply an MQTT-provided stable asset identity before triggering an update.
    void PrepareRemoteUpdate(const std::string& asset_url, const std::string& sha256);

    // Cooperatively pause network and disk work while voltage is unsafe.
    void SetLowBatteryPaused(bool paused);

private:
    enum class UpdateResult {
        kSucceeded,
        kFailed,
        kInstalledRestartRequired,
    };

    AnimationUpdater();
    ~AnimationUpdater();
    
    // Disable copy constructor and assignment operator
    AnimationUpdater(const AnimationUpdater&) = delete;
    AnimationUpdater& operator=(const AnimationUpdater&) = delete;
    
    // Background task
    static void UpdateTask(void* parameter);
    static void RemoteUpdateTask(void* parameter);
    static void RetryTask(void* parameter);
    UpdateResult UpdateLoop();
    void TriggerUpdateLoopInternal(bool reset_retry_budget);
    void FinishUpdateTask(bool success);
    void ScheduleRetry(bool count_failure);
    
    // HTTP operations
    bool CheckServerForUpdates();
    bool DownloadAnimationFile(const std::string& url, const std::string& filename);
    bool SaveAnimationToSpiffs(const std::string& filename, const std::string& data); // Note: Now saves to SD card
    
    // HTTPS testing
    bool TestHttpsDownload();
    bool TestHttpsConnection(const std::string& url);
    std::string GetDownloadUrlFromResponse(const std::string& url);
    bool ParseUrlAndVersion(const std::string& response, std::string& url, std::string& version);
    std::string ExtractFilenameFromUrl(const std::string& url);
    
    // Mega file operations
    bool DownloadMegaAnimationFile(const std::string& url);
    bool DownloadStartupWavFile(const std::string& url);
    bool DownloadStartupGifFile(const std::string& url);
    bool SaveMegaAnimationToSpiffs(const std::string& data); // Note: Now saves to SD card
    bool ValidateMegaAnimationFile(const std::string& data);
    bool ValidateMegaAnimationFileFromDisk(const char* file_path);
    bool ValidateGifMegaAnimationFileFromDisk(const char* file_path);
    bool GetRemoteContentLength(const std::string& url, size_t &out_length);
    bool GetRemoteMegaContentLength(size_t &out_length);
    size_t GetLocalMegaFileSize(const char* file_path);
    bool GetRemoteStartupWavMetadata(const std::string& url, size_t& out_content_length, std::string& out_etag, std::string& out_last_modified);
    bool GetRemoteStartupGifMetadata(const std::string& url, size_t& out_content_length, std::string& out_etag, std::string& out_last_modified);
    bool LoadStartupWavMetadata(size_t& out_size, std::string& out_etag, std::string& out_last_modified);
    bool LoadStartupGifMetadata(size_t& out_size, std::string& out_etag, std::string& out_last_modified);
    bool SaveStartupWavMetadata(size_t size, const std::string& etag, const std::string& last_modified);
    bool SaveStartupGifMetadata(size_t size, const std::string& etag, const std::string& last_modified);
    size_t GetLocalFileSize(const char* file_path);
    bool GetLocalFileHeader(const char* file_path, uint32_t& file_count, uint32_t& checksum, uint32_t& combined_length);
    bool GetRemoteFileHeader(const std::string& url, uint32_t& file_count, uint32_t& checksum, uint32_t& combined_length);
    bool GetRemoteSha256(const std::string& url, std::string& sha256);
    bool GetLocalSha256(const char* file_path, std::string& sha256);
    std::string BuildMegaDownloadUrl();
    std::string BuildStartupWavDownloadUrl();
    std::string BuildStartupGifDownloadUrl();
    
    // Configuration management
    void LoadConfiguration();
    void SaveConfiguration();
    
    // File management
    bool ValidateAnimationFile(const std::string& data);
    void ReloadAnimations();
    
    // Member variables
    std::atomic<bool> is_running_{false};
    std::atomic<bool> rerun_requested_{false};
    std::atomic<uint32_t> retry_attempt_{0};
    std::atomic<bool> enabled_{true};
    std::atomic<bool> low_battery_paused_{false};
    std::string server_url_;
    std::string expected_sha256_;
    mutable std::mutex update_identity_mutex_;
    uint32_t check_interval_seconds_{10}; // Default 10 seconds
    TaskHandle_t update_task_handle_{nullptr};
    TimerHandle_t update_timer_{nullptr};
    TaskHandle_t retry_task_handle_{nullptr};
    std::atomic<uint32_t> retry_delay_ms_{0};
    
    // Statistics
    std::atomic<uint32_t> check_count_{0};
    std::atomic<uint32_t> update_count_{0};
    std::atomic<uint32_t> error_count_{0};
    std::atomic<uint64_t> last_check_time_{0};
    std::atomic<uint64_t> last_update_time_{0};
    
    // Success tracking
    std::atomic<bool> first_download_success_{false};
    
    // Version management
    std::string current_version_{"1.0.0"}; // Default version
};

#endif // ANIMATION_UPDATER_H
