#include "scripted_playback.h"
#include "sd_card.h"
#include "animation/animation.h"
#include "display/display.h"
#include "board.h"
#include "application.h"
#include <esp_log.h>
#include <cJSON.h>
#include <sys/stat.h>
#include <vector>
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "ScriptedPlayback"
#define MOUNT_POINT "/sdcard"

bool ScriptedPlayback::s_is_playing = false;

esp_err_t ScriptedPlayback::Initialize()
{
    ESP_LOGI(TAG, "Scripted playback system initialized");
    return ESP_OK;
}

bool ScriptedPlayback::HasScript(const std::string& script_filename)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGD(TAG, "SD card not mounted");
        return false;
    }

    std::string full_path = std::string(MOUNT_POINT) + "/" + script_filename;
    struct stat st;
    if (stat(full_path.c_str(), &st) == 0) {
        ESP_LOGI(TAG, "Script file found: %s", full_path.c_str());
        return true;
    }
    
    ESP_LOGD(TAG, "Script file not found: %s", full_path.c_str());
    return false;
}

int ScriptedPlayback::GetAnimationIndex(const std::string& animation_name)
{
    // Map animation names to animation indices
    if (animation_name == "normal" || animation_name == "static_normal") {
        return ANIMATION_STATIC_NORMAL;
    } else if (animation_name == "embarrass" || animation_name == "embarrassed") {
        return ANIMATION_EMBARRESSED;
    } else if (animation_name == "fire") {
        return ANIMATION_FIRE;
    } else if (animation_name == "inspiration") {
        return ANIMATION_INSPIRATION;
    } else if (animation_name == "question") {
        return ANIMATION_QUESTION;
    } else if (animation_name == "shy") {
        return ANIMATION_SHY;
    } else if (animation_name == "sleep") {
        return ANIMATION_SLEEP;
    } else if (animation_name == "happy") {
        return ANIMATION_HAPPY;
    }
    
    ESP_LOGW(TAG, "Unknown animation name: %s, using normal", animation_name.c_str());
    return ANIMATION_STATIC_NORMAL;
}

void ScriptedPlayback::PlaybackTask(void* arg)
{
    std::vector<ScriptItem>* sequence = static_cast<std::vector<ScriptItem>*>(arg);
    
    ESP_LOGI(TAG, "Starting scripted playback with %zu items", sequence->size());
    
    auto& app = Application::GetInstance();
    auto display = Board::GetInstance().GetDisplay();
    
    for (size_t i = 0; i < sequence->size() && s_is_playing; i++) {
        const ScriptItem& item = (*sequence)[i];
        
        ESP_LOGI(TAG, "Playing item %zu: type=%s, duration=%dms", 
                 i + 1, item.type.c_str(), item.duration_ms);
        
        if (item.type == "animation") {
            // Set the animation
            int anim_index = GetAnimationIndex(item.animation);
            animation_set_now_animation(anim_index);
            ESP_LOGI(TAG, "  Animation: %s (index: %d)", item.animation.c_str(), anim_index);
            
        } else if (item.type == "audio") {
            // TODO: Implement audio playback from SD card
            // For now, just log it
            ESP_LOGI(TAG, "  Audio file: %s (not yet implemented)", item.file.c_str());
        }
        
        // Wait for the specified duration
        if (item.duration_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(item.duration_ms));
        }
        
        // Check if we should stop
        if (!s_is_playing) {
            ESP_LOGI(TAG, "Playback stopped by user");
            break;
        }
    }
    
    ESP_LOGI(TAG, "Scripted playback completed");
    
    // Clean up
    delete sequence;
    s_is_playing = false;
    vTaskDelete(NULL);
}

esp_err_t ScriptedPlayback::PlayScript(const std::string& script_filename)
{
    if (s_is_playing) {
        ESP_LOGW(TAG, "Playback already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!SdCard::IsMounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    std::string full_path = std::string(MOUNT_POINT) + "/" + script_filename;
    
    // Read the script file
    FILE* file = fopen(full_path.c_str(), "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open script file: %s", full_path.c_str());
        return ESP_FAIL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 10240) { // Max 10KB script file
        ESP_LOGE(TAG, "Invalid script file size: %ld", file_size);
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Read file content
    std::vector<char> buffer(file_size + 1);
    size_t bytes_read = fread(buffer.data(), 1, file_size, file);
    fclose(file);
    
    if (bytes_read != static_cast<size_t>(file_size)) {
        ESP_LOGE(TAG, "Failed to read script file completely");
        return ESP_FAIL;
    }
    
    buffer[file_size] = '\0';
    
    // Parse JSON
    cJSON* json = cJSON_Parse(buffer.data());
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON: %s", cJSON_GetErrorPtr());
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check if this is a video playback script
    cJSON* type_json = cJSON_GetObjectItem(json, "type");
    if (type_json != NULL && cJSON_IsString(type_json) && 
        strcmp(cJSON_GetStringValue(type_json), "video") == 0) {
        // Handle video playback
        ESP_LOGI(TAG, "Detected video playback script");
        cJSON* video_json = cJSON_GetObjectItem(json, "video");
        cJSON* audio_json = cJSON_GetObjectItem(json, "audio");
        
        if (video_json == NULL) {
            ESP_LOGE(TAG, "Video configuration not found");
            cJSON_Delete(json);
            return ESP_ERR_INVALID_ARG;
        }
        
        // Extract video configuration
        cJSON* frame_dir_json = cJSON_GetObjectItem(video_json, "frame_directory");
        cJSON* frame_prefix_json = cJSON_GetObjectItem(video_json, "frame_prefix");
        cJSON* frame_format_json = cJSON_GetObjectItem(video_json, "frame_format");
        cJSON* frame_count_json = cJSON_GetObjectItem(video_json, "frame_count");
        cJSON* fps_json = cJSON_GetObjectItem(video_json, "fps");
        
        if (!frame_dir_json || !frame_prefix_json || !frame_format_json || 
            !frame_count_json || !fps_json) {
            ESP_LOGE(TAG, "Incomplete video configuration");
            cJSON_Delete(json);
            return ESP_ERR_INVALID_ARG;
        }
        
        std::string frame_dir = cJSON_GetStringValue(frame_dir_json);
        std::string frame_prefix = cJSON_GetStringValue(frame_prefix_json);
        std::string frame_format = cJSON_GetStringValue(frame_format_json);
        int frame_count = frame_count_json->valueint;
        int fps = fps_json->valueint;
        
        std::string audio_file = "";
        if (audio_json != NULL) {
            cJSON* audio_file_json = cJSON_GetObjectItem(audio_json, "file");
            if (audio_file_json != NULL && cJSON_IsString(audio_file_json)) {
                audio_file = cJSON_GetStringValue(audio_file_json);
            }
        }
        
        cJSON_Delete(json);
        
        // Start video playback
        return PlayVideo(frame_dir, frame_prefix, frame_format, frame_count, fps, audio_file);
    }
    
    // Parse sequence array (for animation-based scripts)
    cJSON* sequence_json = cJSON_GetObjectItem(json, "sequence");
    if (sequence_json == NULL || !cJSON_IsArray(sequence_json)) {
        ESP_LOGE(TAG, "Invalid script format: 'sequence' array not found");
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Build sequence vector
    std::vector<ScriptItem>* sequence = new std::vector<ScriptItem>();
    int array_size = cJSON_GetArraySize(sequence_json);
    
    for (int i = 0; i < array_size; i++) {
        cJSON* item_json = cJSON_GetArrayItem(sequence_json, i);
        if (item_json == NULL) {
            continue;
        }
        
        ScriptItem item;
        
        // Get type
        cJSON* type_json = cJSON_GetObjectItem(item_json, "type");
        if (type_json == NULL || !cJSON_IsString(type_json)) {
            ESP_LOGW(TAG, "Item %d: missing or invalid 'type', skipping", i);
            continue;
        }
        item.type = cJSON_GetStringValue(type_json);
        
        // Get duration
        cJSON* duration_json = cJSON_GetObjectItem(item_json, "duration_ms");
        if (duration_json != NULL && cJSON_IsNumber(duration_json)) {
            item.duration_ms = duration_json->valueint;
        } else {
            item.duration_ms = 1000; // Default 1 second
        }
        
        // Get type-specific fields
        if (item.type == "animation") {
            cJSON* anim_json = cJSON_GetObjectItem(item_json, "animation");
            if (anim_json != NULL && cJSON_IsString(anim_json)) {
                item.animation = cJSON_GetStringValue(anim_json);
            } else {
                ESP_LOGW(TAG, "Item %d: missing 'animation' field, using 'normal'", i);
                item.animation = "normal";
            }
        } else if (item.type == "audio") {
            cJSON* file_json = cJSON_GetObjectItem(item_json, "file");
            if (file_json != NULL && cJSON_IsString(file_json)) {
                item.file = cJSON_GetStringValue(file_json);
            } else {
                ESP_LOGW(TAG, "Item %d: missing 'file' field, skipping", i);
                continue;
            }
        } else {
            ESP_LOGW(TAG, "Item %d: unknown type '%s', skipping", i, item.type.c_str());
            continue;
        }
        
        sequence->push_back(item);
    }
    
    cJSON_Delete(json);
    
    if (sequence->empty()) {
        ESP_LOGE(TAG, "Script sequence is empty");
        delete sequence;
        return ESP_ERR_INVALID_ARG;
    }
    
    // Start playback task
    s_is_playing = true;
    xTaskCreatePinnedToCore(
        PlaybackTask,
        "scripted_playback",
        4096,
        sequence,
        5,
        NULL,
        1  // Run on core 1
    );
    
    ESP_LOGI(TAG, "Scripted playback started with %zu items", sequence->size());
    return ESP_OK;
}

void ScriptedPlayback::Stop()
{
    if (s_is_playing) {
        ESP_LOGI(TAG, "Stopping scripted playback");
        s_is_playing = false;
    }
}

bool ScriptedPlayback::IsPlaying()
{
    return s_is_playing;
}

// Video playback task structure
struct VideoPlaybackData {
    std::string frame_directory;
    std::string frame_prefix;
    std::string frame_format;
    int frame_count;
    int fps;
    std::string audio_file;
};

void VideoPlaybackTask(void* arg)
{
    VideoPlaybackData* data = static_cast<VideoPlaybackData*>(arg);
    
    ESP_LOGI(TAG, "Starting video playback:");
    ESP_LOGI(TAG, "  Frame directory: %s", data->frame_directory.c_str());
    ESP_LOGI(TAG, "  Frame prefix: %s", data->frame_prefix.c_str());
    ESP_LOGI(TAG, "  Frame format: %s", data->frame_format.c_str());
    ESP_LOGI(TAG, "  Frame count: %d", data->frame_count);
    ESP_LOGI(TAG, "  FPS: %d", data->fps);
    ESP_LOGI(TAG, "  Audio file: %s", data->audio_file.empty() ? "none" : data->audio_file.c_str());
    
    auto display = Board::GetInstance().GetDisplay();
    int frame_delay_ms = 1000 / data->fps; // Calculate delay between frames
    
    // TODO: Start audio playback in a separate task if audio_file is provided
    // For now, we'll just play the video frames
    
    for (int frame_num = 1; frame_num <= data->frame_count && s_is_playing; frame_num++) {
        // Construct frame filename
        char frame_filename[256];
        if (data->frame_format == "bin") {
            snprintf(frame_filename, sizeof(frame_filename), "%s/%s%04d.bin",
                    data->frame_directory.c_str(), data->frame_prefix.c_str(), frame_num);
        } else {
            snprintf(frame_filename, sizeof(frame_filename), "%s/%s%04d.jpg",
                    data->frame_directory.c_str(), data->frame_prefix.c_str(), frame_num);
        }
        
        std::string full_path = std::string(MOUNT_POINT) + "/" + frame_filename;
        
        ESP_LOGD(TAG, "Loading frame %d/%d: %s", frame_num, data->frame_count, full_path.c_str());
        
        // Load and display frame
        // For JPG format, we'd need to decode it first
        // For now, this is a placeholder - actual implementation would:
        // 1. Load frame from SD card
        // 2. Decode JPG (if needed)
        // 3. Convert to LVGL image format
        // 4. Display on screen
        
        // TODO: Implement frame loading and display
        // This requires:
        // - JPG decoder (ESP32 has built-in JPEG decoder)
        // - LVGL image creation from decoded frame
        // - Display update
        
        ESP_LOGI(TAG, "Frame %d/%d (would display: %s)", frame_num, data->frame_count, frame_filename);
        
        // Wait for next frame
        vTaskDelay(pdMS_TO_TICKS(frame_delay_ms));
        
        // Check if we should stop
        if (!s_is_playing) {
            ESP_LOGI(TAG, "Video playback stopped by user");
            break;
        }
    }
    
    ESP_LOGI(TAG, "Video playback completed");
    
    // Clean up
    delete data;
    s_is_playing = false;
    vTaskDelete(NULL);
}

esp_err_t ScriptedPlayback::PlayVideo(const std::string& frame_directory,
                                      const std::string& frame_prefix,
                                      const std::string& frame_format,
                                      int frame_count,
                                      int fps,
                                      const std::string& audio_file)
{
    if (s_is_playing) {
        ESP_LOGW(TAG, "Playback already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!SdCard::IsMounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (frame_count <= 0 || fps <= 0) {
        ESP_LOGE(TAG, "Invalid video parameters: frame_count=%d, fps=%d", frame_count, fps);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Validate frame format
    if (frame_format != "jpg" && frame_format != "bin") {
        ESP_LOGE(TAG, "Unsupported frame format: %s (supported: jpg, bin)", frame_format.c_str());
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // Create video playback data
    VideoPlaybackData* data = new VideoPlaybackData();
    data->frame_directory = frame_directory;
    data->frame_prefix = frame_prefix;
    data->frame_format = frame_format;
    data->frame_count = frame_count;
    data->fps = fps;
    data->audio_file = audio_file;
    
    // Start video playback task
    s_is_playing = true;
    xTaskCreatePinnedToCore(
        VideoPlaybackTask,
        "video_playback",
        8192,  // Larger stack for video processing
        data,
        5,
        NULL,
        1  // Run on core 1
    );
    
    ESP_LOGI(TAG, "Video playback started: %d frames at %d FPS", frame_count, fps);
    return ESP_OK;
}

