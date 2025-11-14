#include "scripted_playback.h"
#include "sd_card.h"
#include "animation/animation.h"
#include "display/display.h"
#include "board.h"
#include "application.h"
#include <esp_log.h>
#include <cJSON.h>
#include <sys/stat.h>
#include <dirent.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_jpeg_dec.h>
#include <esp_heap_caps.h>
#include <cstring>

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
    ESP_LOGI(TAG, "Checking for script file: %s", script_filename.c_str());
    
    if (!SdCard::IsMounted()) {
        ESP_LOGW(TAG, "SD card not mounted");
        return false;
    }

    // Check for exact filename first
    std::string full_path = std::string(MOUNT_POINT) + "/" + script_filename;
    struct stat st;
    if (stat(full_path.c_str(), &st) == 0) {
        ESP_LOGI(TAG, "✅ Script file found: %s (%ld bytes)", full_path.c_str(), st.st_size);
        return true;
    }
    
    // Also check for common variations (case-insensitive, .txt extension)
    std::string alt_filename = script_filename;
    // Replace .json with .txt (Windows sometimes saves as .txt)
    size_t pos = alt_filename.find(".json");
    if (pos != std::string::npos) {
        alt_filename.replace(pos, 5, ".txt");
        std::string alt_path = std::string(MOUNT_POINT) + "/" + alt_filename;
        if (stat(alt_path.c_str(), &st) == 0) {
            ESP_LOGI(TAG, "✅ Script file found (as .txt): %s (%ld bytes)", alt_path.c_str(), st.st_size);
            return true;
        }
    }
    
    // Check for uppercase version
    std::string upper_filename = script_filename;
    std::transform(upper_filename.begin(), upper_filename.end(), upper_filename.begin(), ::toupper);
    std::string upper_path = std::string(MOUNT_POINT) + "/" + upper_filename;
    if (stat(upper_path.c_str(), &st) == 0) {
        ESP_LOGI(TAG, "✅ Script file found (uppercase): %s (%ld bytes)", upper_path.c_str(), st.st_size);
        return true;
    }
    
    // Check for playback.json.txt (Windows sometimes adds .txt extension)
    std::string json_txt_path = std::string(MOUNT_POINT) + "/playback.json.txt";
    if (stat(json_txt_path.c_str(), &st) == 0) {
        ESP_LOGI(TAG, "✅ Script file found (as .json.txt): %s (%ld bytes)", json_txt_path.c_str(), st.st_size);
        return true;
    }
    
    // Search for files matching the pattern (handles Windows short filenames like PLAYBA~1.TXT)
    ESP_LOGI(TAG, "Searching SD card for script files...");
    DIR* dir = opendir(MOUNT_POINT);
    std::string found_script_file;
    if (dir != NULL) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                std::string filename = entry->d_name;
                std::string lower_filename = filename;
                std::transform(lower_filename.begin(), lower_filename.end(), lower_filename.begin(), ::tolower);
                
                // Check if filename contains "play" and ends with .txt, .json, or is a short filename
                bool has_play = (lower_filename.find("play") != std::string::npos);
                bool is_json_or_txt = (lower_filename.find(".json") != std::string::npos || 
                                       lower_filename.find(".txt") != std::string::npos);
                bool is_short_name = (filename.length() <= 12 && filename.find("~") != std::string::npos);
                
                if (has_play && (is_json_or_txt || is_short_name)) {
                    char file_path[512];
                    int written = snprintf(file_path, sizeof(file_path), "%s/%s", MOUNT_POINT, entry->d_name);
                    if (written >= 0 && written < (int)sizeof(file_path)) {
                        struct stat file_st;
                        if (stat(file_path, &file_st) == 0) {
                            ESP_LOGI(TAG, "  Found potential script file: %s (%ld bytes)", entry->d_name, file_st.st_size);
                            // Use the first matching file we find
                            if (found_script_file.empty()) {
                                found_script_file = entry->d_name;
                            }
                        }
                    }
                }
            }
        }
        closedir(dir);
    }
    
    // If we found a matching file, accept it
    if (!found_script_file.empty()) {
        std::string found_path = std::string(MOUNT_POINT) + "/" + found_script_file;
        if (stat(found_path.c_str(), &st) == 0) {
            ESP_LOGI(TAG, "✅ Script file found (matched pattern): %s (%ld bytes)", found_path.c_str(), st.st_size);
            return true;
        }
    }
    
    ESP_LOGW(TAG, "Script file not found: %s", full_path.c_str());
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
    
    // Note: app and display variables reserved for future use
    (void)Application::GetInstance();
    (void)Board::GetInstance().GetDisplay();
    
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
    
    // Try to find the script file (check .json, .txt, and uppercase versions)
    std::string full_path = std::string(MOUNT_POINT) + "/" + script_filename;
    FILE* file = fopen(full_path.c_str(), "r");
    
    // If .json not found, try .txt
    if (file == NULL) {
        std::string alt_filename = script_filename;
        size_t pos = alt_filename.find(".json");
        if (pos != std::string::npos) {
            alt_filename.replace(pos, 5, ".txt");
            std::string alt_path = std::string(MOUNT_POINT) + "/" + alt_filename;
            file = fopen(alt_path.c_str(), "r");
            if (file != NULL) {
                full_path = alt_path;
                ESP_LOGI(TAG, "Found script file as .txt: %s", alt_path.c_str());
            }
        }
    }
    
    // If still not found, try uppercase
    if (file == NULL) {
        std::string upper_filename = script_filename;
        std::transform(upper_filename.begin(), upper_filename.end(), upper_filename.begin(), ::toupper);
        std::string upper_path = std::string(MOUNT_POINT) + "/" + upper_filename;
        file = fopen(upper_path.c_str(), "r");
        if (file != NULL) {
            full_path = upper_path;
            ESP_LOGI(TAG, "Found script file (uppercase): %s", upper_path.c_str());
        }
    }
    
    // If still not found, try playback.json.txt
    if (file == NULL) {
        std::string json_txt_path = std::string(MOUNT_POINT) + "/playback.json.txt";
        file = fopen(json_txt_path.c_str(), "r");
        if (file != NULL) {
            full_path = json_txt_path;
            ESP_LOGI(TAG, "Found script file (as .json.txt): %s", json_txt_path.c_str());
        }
    }
    
    // If still not found, search for files matching the pattern (handles Windows short filenames)
    if (file == NULL) {
        DIR* dir = opendir(MOUNT_POINT);
        if (dir != NULL) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL && file == NULL) {
                if (entry->d_type == DT_REG) {
                    std::string filename = entry->d_name;
                    std::string lower_filename = filename;
                    std::transform(lower_filename.begin(), lower_filename.end(), lower_filename.begin(), ::tolower);
                    
                    // Check if filename contains "play" and ends with .txt, .json, or is a short filename
                    bool has_play = (lower_filename.find("play") != std::string::npos);
                    bool is_json_or_txt = (lower_filename.find(".json") != std::string::npos || 
                                           lower_filename.find(".txt") != std::string::npos);
                    bool is_short_name = (filename.length() <= 12 && filename.find("~") != std::string::npos);
                    
                    if (has_play && (is_json_or_txt || is_short_name)) {
                        char file_path[512];
                        int written = snprintf(file_path, sizeof(file_path), "%s/%s", MOUNT_POINT, entry->d_name);
                        if (written >= 0 && written < (int)sizeof(file_path)) {
                            FILE* test_file = fopen(file_path, "r");
                            if (test_file != NULL) {
                                file = test_file;
                                full_path = file_path;
                                ESP_LOGI(TAG, "Found script file (matched pattern): %s", file_path);
                            }
                        }
                    }
                }
            }
            closedir(dir);
        }
    }
    
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open script file: %s (tried .txt, uppercase, .json.txt, and pattern matching)", full_path.c_str());
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

struct DecodedFrameBuffer {
    uint8_t* data = nullptr;
    size_t capacity = 0;
    lv_image_dsc_t image;
};

// Helper function to load and decode a JPEG frame into a reusable buffer
static bool DecodeJPEGFrameIntoBuffer(const std::string& file_path,
                                      jpeg_dec_handle_t* jpeg_dec,
                                      jpeg_dec_io_t* jpeg_io,
                                      jpeg_dec_header_info_t* jpeg_out,
                                      DecodedFrameBuffer& buffer,
                                      Display* display) {
    // Open JPEG file
    FILE* file = fopen(file_path.c_str(), "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open frame file: %s", file_path.c_str());
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size == 0 || file_size > 1024 * 1024) { // Max 1MB per frame
        ESP_LOGE(TAG, "Invalid frame file size: %zu", file_size);
        fclose(file);
        return NULL;
    }
    
    // Allocate buffer for JPEG data
    uint8_t* jpeg_buffer = (uint8_t*)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (jpeg_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for JPEG data (%zu bytes)", file_size);
        fclose(file);
        return NULL;
    }
    
    // Read JPEG data
    size_t bytes_read = fread(jpeg_buffer, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != file_size) {
        ESP_LOGE(TAG, "Failed to read complete JPEG file: read %zu of %zu bytes", bytes_read, file_size);
        heap_caps_free(jpeg_buffer);
        return NULL;
    }
    
    // Parse JPEG header
    jpeg_io->inbuf = jpeg_buffer;
    jpeg_io->inbuf_len = file_size;
    esp_err_t ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, jpeg_out);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JPEG header: %s", esp_err_to_name(ret));
        heap_caps_free(jpeg_out);
        heap_caps_free(jpeg_io);
        jpeg_dec_close(jpeg_dec);
        heap_caps_free(jpeg_buffer);
        return NULL;
    }
    
    // Calculate output buffer size (RGB565 = 2 bytes per pixel)
    size_t output_size = jpeg_out->width * jpeg_out->height * 2;
    
    // Ensure buffer has enough capacity
    if (buffer.data == nullptr || buffer.capacity < output_size) {
        if (buffer.data != nullptr) {
            heap_caps_free(buffer.data);
        }
        buffer.data = (uint8_t*)heap_caps_malloc(output_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buffer.data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for decoded image (%zu bytes)", output_size);
            heap_caps_free(jpeg_buffer);
            return false;
        }
        buffer.capacity = output_size;
    }

    // Initialize LVGL descriptor metadata
    buffer.image.header.magic = LV_IMAGE_HEADER_MAGIC;
    buffer.image.header.cf = LV_COLOR_FORMAT_RGB565;
    buffer.image.header.flags = LV_IMAGE_FLAGS_ALLOCATED;
    buffer.image.header.stride = jpeg_out->width * 2;
    
    // Decode JPEG
    jpeg_io->outbuf = buffer.data;
    int inbuf_consumed = jpeg_io->inbuf_len - jpeg_io->inbuf_remain;
    jpeg_io->inbuf = jpeg_buffer + inbuf_consumed;
    jpeg_io->inbuf_len = jpeg_io->inbuf_remain;
    
    ret = jpeg_dec_process(jpeg_dec, jpeg_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to decode JPEG: %s", esp_err_to_name(ret));
        heap_caps_free(jpeg_buffer);
        return false;
    }
    
    // Populate LVGL image descriptor fields
    buffer.image.header.w = jpeg_out->width;
    buffer.image.header.h = jpeg_out->height;
    buffer.image.data_size = output_size;
    buffer.image.data = buffer.data;
    
    // Display the frame
    if (display != NULL) {
        display->SetEmotionImg(&buffer.image);
    }
    
    heap_caps_free(jpeg_buffer);
    
    return true;
}

void ScriptedPlayback::VideoPlaybackTask(void* arg)
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
    
    // Initialize JPEG decoder resources once per playback
    jpeg_dec_config_t config = {
        .output_type = JPEG_RAW_TYPE_RGB565_LE,
        .rotate = JPEG_ROTATE_0D
    };
    jpeg_dec_handle_t* jpeg_dec = jpeg_dec_open(&config);
    jpeg_dec_io_t* jpeg_io = nullptr;
    jpeg_dec_header_info_t* jpeg_out = nullptr;
    
    if (jpeg_dec == nullptr) {
        ESP_LOGE(TAG, "Failed to open JPEG decoder");
        delete data;
        s_is_playing = false;
        vTaskDelete(NULL);
        return;
    }
    
    jpeg_io = (jpeg_dec_io_t*)heap_caps_calloc(1, sizeof(jpeg_dec_io_t), MALLOC_CAP_SPIRAM);
    jpeg_out = (jpeg_dec_header_info_t*)heap_caps_calloc(1, sizeof(jpeg_dec_header_info_t), MALLOC_CAP_SPIRAM);
    
    if (jpeg_io == nullptr || jpeg_out == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate JPEG decoder structures");
        if (jpeg_io) heap_caps_free(jpeg_io);
        if (jpeg_out) heap_caps_free(jpeg_out);
        jpeg_dec_close(jpeg_dec);
        delete data;
        s_is_playing = false;
        vTaskDelete(NULL);
        return;
    }
    
    // Prepare double buffers
    DecodedFrameBuffer frame_buffers[2];
    
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
        
        // Select buffer (double buffering)
        DecodedFrameBuffer& buffer = frame_buffers[(frame_num - 1) % 2];
        
        // Load and display frame
        if (data->frame_format == "jpg" || data->frame_format == "jpeg") {
            if (!DecodeJPEGFrameIntoBuffer(full_path, jpeg_dec, jpeg_io, jpeg_out, buffer, display)) {
                ESP_LOGW(TAG, "Failed to load frame %d, skipping", frame_num);
            } else {
                ESP_LOGI(TAG, "Displayed frame %d/%d", frame_num, data->frame_count);
            }
        } else if (data->frame_format == "bin") {
            // For BIN format, use existing animation loading (if compatible)
            ESP_LOGW(TAG, "BIN format not yet implemented for video playback");
        }
        
        // Wait for next frame
        vTaskDelay(pdMS_TO_TICKS(frame_delay_ms));
        
        // Check if we should stop
        if (!s_is_playing) {
            ESP_LOGI(TAG, "Video playback stopped by user");
            break;
        }
    }
    
    // Clean up buffers
    for (int i = 0; i < 2; ++i) {
        if (frame_buffers[i].data != nullptr) {
            heap_caps_free(frame_buffers[i].data);
            frame_buffers[i].data = nullptr;
            frame_buffers[i].capacity = 0;
        }
    }
    
    if (jpeg_io) heap_caps_free(jpeg_io);
    if (jpeg_out) heap_caps_free(jpeg_out);
    if (jpeg_dec) jpeg_dec_close(jpeg_dec);
    
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
        ScriptedPlayback::VideoPlaybackTask,
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

