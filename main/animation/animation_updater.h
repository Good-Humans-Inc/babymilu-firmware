#ifndef ANIMATION_UPDATER_H
#define ANIMATION_UPDATER_H

#include <string>
#include <memory>
#include <atomic>
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
    
    // Force immediate update check (bypasses success flag)
    bool ForceUpdateCheck();
    
    // Reset the first download success flag (for testing/debugging)
    void ResetFirstDownloadSuccess();
    
    // Get status information
    std::string GetStatusJson() const;
    
    // Version management
    std::string GetCurrentVersion() const;
    void SetCurrentVersion(const std::string& version);

private:
    AnimationUpdater();
    ~AnimationUpdater();
    
    // Disable copy constructor and assignment operator
    AnimationUpdater(const AnimationUpdater&) = delete;
    AnimationUpdater& operator=(const AnimationUpdater&) = delete;
    
    // Background task
    static void UpdateTask(void* parameter);
    void UpdateLoop();
    
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
    bool SaveMegaAnimationToSpiffs(const std::string& data); // Note: Now saves to SD card
    bool ValidateMegaAnimationFile(const std::string& data);
    bool ValidateMegaAnimationFileFromDisk(const char* file_path);
    
    // Configuration management
    void LoadConfiguration();
    void SaveConfiguration();
    
    // File management
    bool ValidateAnimationFile(const std::string& data);
    void ReloadAnimations();
    
    // SPIFFS management
    bool IsSpiffsReady() const;
    
    // Member variables
    std::atomic<bool> is_running_{false};
    std::atomic<bool> enabled_{true};
    std::string server_url_;
    uint32_t check_interval_seconds_{10}; // Default 10 seconds
    TaskHandle_t update_task_handle_{nullptr};
    TimerHandle_t update_timer_{nullptr};
    
    // Statistics
    std::atomic<uint32_t> check_count_{0};
    std::atomic<uint32_t> update_count_{0};
    std::atomic<uint32_t> error_count_{0};
    std::atomic<uint64_t> last_check_time_{0};
    std::atomic<uint64_t> last_update_time_{0};
    
    // Success tracking
    std::atomic<bool> first_download_success_{false};
    std::atomic<uint32_t> spiffs_not_ready_count_{0};
    
    // Version management
    std::string current_version_{"1.0.0"}; // Default version
};

#endif // ANIMATION_UPDATER_H
