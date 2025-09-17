/*
 * @Descripttion:
 * @Author: Xvsenfeng helloworldjiao@163.com
 * @LastEditors: Xvsenfeng helloworldjiao@163.com
 * Copyright (c) 2025 by helloworldjiao@163.com, All Rights Reserved.
 */
#include "animation.h"
#include "lvgl.h"
#include "board.h"
#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <type_traits>
#include "esp_spiffs.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <cJSON.h>
#include <esp_crc.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
extern const lv_image_dsc_t embarrass1;
extern const lv_image_dsc_t embarrass2;
extern const lv_image_dsc_t embarrass3;
extern const lv_image_dsc_t fire1;
extern const lv_image_dsc_t fire2;
extern const lv_image_dsc_t normal1;
extern const lv_image_dsc_t normal2;
extern const lv_image_dsc_t normal3;
extern const lv_image_dsc_t fire3;
extern const lv_image_dsc_t fire4;
extern const lv_image_dsc_t inspiration1;
extern const lv_image_dsc_t inspiration2;
extern const lv_image_dsc_t inspiration3;
extern const lv_image_dsc_t inspiration4;
extern const lv_image_dsc_t normal1;
extern const lv_image_dsc_t normal2;
extern const lv_image_dsc_t normal3;
extern const lv_image_dsc_t question1;
extern const lv_image_dsc_t question2;
extern const lv_image_dsc_t question3;
extern const lv_image_dsc_t question4;
extern const lv_image_dsc_t shy1;
extern const lv_image_dsc_t shy2;
extern const lv_image_dsc_t sleep1;
extern const lv_image_dsc_t sleep2;
extern const lv_image_dsc_t sleep3;
extern const lv_image_dsc_t sleep4;
extern const lv_image_dsc_t happy1;
extern const lv_image_dsc_t happy2;
extern const lv_image_dsc_t happy3;
extern const lv_image_dsc_t happy4;

const lv_image_dsc_t *embarrass_i[3] = {
    &embarrass1,
    &embarrass2,
    &embarrass3};

const lv_image_dsc_t *fire_i[4] = {
    &fire1,
    &fire2,
    &fire3,
    &fire4};

const lv_image_dsc_t *inspiration_i[4] = {
    &inspiration1,
    &inspiration2,
    &inspiration3,
    &inspiration4};

// Note: normal1, normal2, normal3 are now loaded from SPIFFS dynamically
// This array is kept for backward compatibility but will be overridden by SPIFFS
const lv_image_dsc_t *normal_i[3] = {
    &normal1,
    &normal2,
    &normal3};

const lv_image_dsc_t *question_i[4] = {
    &question1,
    &question2,
    &question3,
    &question4};

const lv_image_dsc_t *shy_i[2] = {
    &shy1,
    &shy2};

const lv_image_dsc_t *sleep_i[4] = {
    &sleep1,
    &sleep2,
    &sleep3,
    &sleep4};
const lv_image_dsc_t *happy_i[4] = {
    &happy1,
    &happy2,
    &happy3,
    &happy4};

Animation_t static_normal = {
    .imges = normal_i,
    .animations = (int[]){0},
    .len = 1,
    .use_spiffs = false,
    .spiffs_imgs = NULL};
Animation_t animation_embressed = {
    .imges = embarrass_i,
    .animations = (int[]){0, 1, 2},
    .len = 3,
    .use_spiffs = false,
    .spiffs_imgs = NULL};
Animation_t animation_fire = {
    .imges = fire_i,
    .animations = (int[]){0, 1, 2, 3},
    .len = 4,
    .use_spiffs = false,
    .spiffs_imgs = NULL};
Animation_t animation_inspiration = {
    .imges = inspiration_i,
    .animations = (int[]){0, 1, 2, 3},
    .len = 4,
    .use_spiffs = false,
    .spiffs_imgs = NULL};
Animation_t animation_normal = {
    .imges = normal_i,
    .animations = (int[]){0, 1, 2},
    .len = 3,
    .use_spiffs = false,
    .spiffs_imgs = NULL};
Animation_t animation_question = {
    .imges = question_i,
    .animations = (int[]){0, 1, 2, 3},
    .len = 4,
    .use_spiffs = false,
    .spiffs_imgs = NULL};
Animation_t animation_shy = {
    .imges = shy_i,
    .animations = (int[]){0, 1},
    .len = 2,
    .use_spiffs = false,
    .spiffs_imgs = NULL};
Animation_t animation_sleep = {
    .imges = sleep_i,
    .animations = (int[]){0, 1, 2, 3},
    .len = 4,
    .use_spiffs = false,
    .spiffs_imgs = NULL};
Animation_t animation_happy = {
    .imges = happy_i,
    .animations = (int[]){0, 1, 2, 3},
    .len = 4,
    .use_spiffs = false,
    .spiffs_imgs = NULL};

// Global SPIFFS-based normal animation
static Animation_t spiffs_normal = {0};

// Function to get the appropriate animation (static or SPIFFS)
Animation_t* get_animation(int index) {
    switch(index) {
        case 0: // ANIMATION_STATIC_NORMAL
            return animation_get_normal_animation(); // This will return SPIFFS or static
        case 1: // ANIMATION_EMBARRESSED
            return &animation_embressed;
        case 2: // ANIMATION_FIRE
            return &animation_fire;
        case 3: // ANIMATION_INSPIRATION
            return &animation_inspiration;
        case 4: // ANIMATION_NORMAL
            return &animation_normal;
        case 5: // ANIMATION_QUESTION
            return &animation_question;
        case 6: // ANIMATION_SHY
            return &animation_shy;
        case 7: // ANIMATION_SLEEP
            return &animation_sleep;
        case 8: // ANIMATION_HAPPY
            return &animation_happy;
        default:
            return &static_normal;
    }
}

// Keep the old array for backward compatibility, but use get_animation() instead
Animation_t *animations[] = {
    &static_normal,  // This will be overridden by get_animation(0)
    &animation_embressed,
    &animation_fire,
    &animation_inspiration,
    &animation_normal,
    &animation_question,
    &animation_shy,
    &animation_sleep,
    &animation_happy};

static int now_animation = 0;
int pos = 0;
TaskHandle_t animation_task_handle = nullptr;

void plat_animation_task(void *arg)
{
    auto display = Board::GetInstance().GetDisplay();
    while (1)
    {
        ESP_LOGD("plat_animation_task", "now_animation: %d, pos: %d", now_animation, pos);
        pos++;
        
        // Use get_animation() to get the appropriate animation (static or SPIFFS)
        Animation_t* current_anim = get_animation(now_animation);
        if (pos >= current_anim->len)
        {
            pos = 0;
        }
        display->SetEmotionImg(current_anim->imges[current_anim->animations[pos]]);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void animation_set_now_animation(int animation)
{
    if (animation_task_handle == nullptr)
    {
        xTaskCreate(plat_animation_task, "plat_animation_task", 2048, nullptr, 4, &animation_task_handle);
    }
    if (animation < 0 || animation >= ANIMATION_NUM)
    {
        return;
    }
    ESP_LOGI("animation_set_now_animation", "Set now animation: %d", animation);
    now_animation = animation;
    pos = 0;
}

// SPIFFS Animation Functions
static bool spiffs_initialized = false;

void animation_init_spiffs(void)
{
    if (spiffs_initialized) return;
    
    esp_vfs_spiffs_conf_t config = {
        .base_path = "/spiffs",
        .partition_label = "animations",
        .max_files = 20,  // Increased for more files
        .format_if_mount_failed = true,
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&config);
    if (ret != ESP_OK) {
        ESP_LOGE("animation", "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }
    
    spiffs_initialized = true;
    ESP_LOGI("animation", "SPIFFS initialized successfully (read-write mode)");
    
    // Create updates directory for atomic operations
    mkdir("/spiffs/.updates", 0755);
    
    // Try to load SPIFFS animations
    animation_load_spiffs_animations();
}

void test_spiffs_debug(void)
{
    ESP_LOGI("animation", "=== SPIFFS Debug Test ===");
    
    // Check SPIFFS partition info
    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info("animations", &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI("animation", "SPIFFS partition info:");
        ESP_LOGI("animation", "  Total: %d bytes", total);
        ESP_LOGI("animation", "  Used: %d bytes", used);
        ESP_LOGI("animation", "  Free: %d bytes", total - used);
    } else {
        ESP_LOGE("animation", "Failed to get SPIFFS info: %s", esp_err_to_name(ret));
    }
    
    // List files in SPIFFS
    ESP_LOGI("animation", "Listing files in /spiffs/");
    DIR* dir = opendir("/spiffs");
    if (dir != NULL) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            ESP_LOGI("animation", "  Found file: %s", entry->d_name);
            
            // Get file size
            char full_path[512];
            int ret = snprintf(full_path, sizeof(full_path), "/spiffs/%s", entry->d_name);
            if (ret >= sizeof(full_path)) {
                ESP_LOGW("animation", "Path too long, skipping: %s", entry->d_name);
                continue;
            }
            struct stat st;
            if (stat(full_path, &st) == 0) {
                ESP_LOGI("animation", "    Size: %ld bytes", st.st_size);
            }
        }
        closedir(dir);
    } else {
        ESP_LOGE("animation", "Failed to open /spiffs directory");
    }
    
    // Test opening specific files
    const char* test_files[] = {"normal1.bin", "normal2.bin", "normal3.bin"};
    for (int i = 0; i < 3; i++) {
        char full_path[512];
        int ret = snprintf(full_path, sizeof(full_path), "/spiffs/%s", test_files[i]);
        if (ret >= sizeof(full_path)) {
            ESP_LOGW("animation", "Path too long, skipping: %s", test_files[i]);
            continue;
        }
        
        FILE* f = fopen(full_path, "rb");
        if (f != NULL) {
            ESP_LOGI("animation", "✅ Successfully opened %s", test_files[i]);
            fclose(f);
        } else {
            ESP_LOGE("animation", "❌ Failed to open %s", test_files[i]);
        }
    }
    
    ESP_LOGI("animation", "=== SPIFFS Debug Test Complete ===");
}

void animation_load_spiffs_animations(void)
{
    if (!spiffs_initialized) {
        ESP_LOGW("animation", "SPIFFS not initialized, skipping SPIFFS animation loading");
        return;
    }
    
    ESP_LOGI("animation", "Attempting to load animations from SPIFFS...");
    
    // Debug SPIFFS contents
    test_spiffs_debug();
    
    // Try to load normal animation from SPIFFS
    ESP_LOGI("animation", "Attempting to load normal animation from SPIFFS...");
    if (animation_load_normal_from_spiffs()) {
        ESP_LOGI("animation", "✅ SPIFFS animations loaded successfully!");
        ESP_LOGI("animation", "   - Normal animation now uses SPIFFS (normal1.bin, normal2.bin, normal3.bin)");
        ESP_LOGI("animation", "   - SPIFFS animation has %d frames", spiffs_normal.len);
    } else {
        ESP_LOGI("animation", "⚠️  SPIFFS animations not found, using static animations");
        ESP_LOGI("animation", "   - Normal animation uses static images (normal1, normal2, normal3)");
        ESP_LOGI("animation", "   - To use SPIFFS animations, place normal1.bin, normal2.bin, normal3.bin in spiffs_data/");
    }
}

bool animation_load_from_spiffs(const char* filename, lv_image_dsc_t* img_dsc)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    char full_path[64];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);
    
    FILE* f = fopen(full_path, "rb");
    if (f == NULL) {
        ESP_LOGE("animation", "Failed to open %s", full_path);
        return false;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    ESP_LOGI("animation", "Loading %s: %d bytes", filename, file_size);
    
    // The .bin files contain a custom format: 6 uint32_t header + raw pixel data
    // Header format: magic, color_format, flags, width, height, stride
    uint32_t header_data[6];
    if (fread(header_data, sizeof(uint32_t), 6, f) != 6) {
        ESP_LOGE("animation", "Failed to read image header from %s", full_path);
        fclose(f);
        return false;
    }
    
    // Validate the magic number (0x4C56474C = "LVGL" in little endian)
    if (header_data[0] != 0x4C56474C) {
        ESP_LOGE("animation", "Invalid image magic in %s: 0x%x (expected 0x4C56474C)", filename, header_data[0]);
        fclose(f);
        return false;
    }
    
    // Calculate remaining data size
    size_t header_size = 6 * sizeof(uint32_t);
    size_t remaining_size = file_size - header_size;
    
    // Set up the LVGL image descriptor
    img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    img_dsc->header.cf = (lv_color_format_t)header_data[1];  // color_format
    img_dsc->header.flags = (uint32_t)header_data[2];        // flags
    img_dsc->header.w = (uint32_t)header_data[3];            // width
    img_dsc->header.h = (uint32_t)header_data[4];            // height
    img_dsc->header.stride = (uint32_t)header_data[5];       // stride
    img_dsc->data_size = remaining_size;
    
    // Allocate memory for pixel data
    img_dsc->data = (const uint8_t*)malloc(img_dsc->data_size);
    if (img_dsc->data == NULL) {
        ESP_LOGE("animation", "Failed to allocate %d bytes for image data", img_dsc->data_size);
        fclose(f);
        return false;
    }
    
    // Read pixel data
    if (fread((void*)img_dsc->data, 1, img_dsc->data_size, f) != img_dsc->data_size) {
        ESP_LOGE("animation", "Failed to read image data from %s", full_path);
        free((void*)img_dsc->data);
        img_dsc->data = NULL;
        fclose(f);
        return false;
    }
    
    ESP_LOGI("animation", "Loaded image: %dx%d, format=%d, data_size=%d", 
             img_dsc->header.w, img_dsc->header.h, img_dsc->header.cf, img_dsc->data_size);
    
    fclose(f);
    ESP_LOGI("animation", "Successfully loaded %s from SPIFFS (%d bytes)", filename, img_dsc->data_size);
    return true;
}

bool animation_create_spiffs_animation(Animation_t* anim, const char* filenames[], int count)
{
    ESP_LOGI("animation", "Creating SPIFFS animation with %d frames", count);
    
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    // Allocate memory for SPIFFS images
    anim->spiffs_imgs = (lv_image_dsc_t**)malloc(count * sizeof(lv_image_dsc_t*));
    if (anim->spiffs_imgs == NULL) {
        ESP_LOGE("animation", "Failed to allocate memory for SPIFFS images");
        return false;
    }
    
    // Allocate memory for each image descriptor
    for (int i = 0; i < count; i++) {
        anim->spiffs_imgs[i] = (lv_image_dsc_t*)malloc(sizeof(lv_image_dsc_t));
        if (anim->spiffs_imgs[i] == NULL) {
            ESP_LOGE("animation", "Failed to allocate memory for image %d", i);
        // Clean up previously allocated memory
        for (int j = 0; j < i; j++) {
            if (anim->spiffs_imgs[j]) {
                if (anim->spiffs_imgs[j]->data) free((void*)anim->spiffs_imgs[j]->data);
                free(anim->spiffs_imgs[j]);
            }
        }
            free(anim->spiffs_imgs);
            anim->spiffs_imgs = NULL;
            return false;
        }
        
        // Initialize the image descriptor
        anim->spiffs_imgs[i]->data = NULL;
        anim->spiffs_imgs[i]->data_size = 0;
        
        // Load image from SPIFFS
        ESP_LOGI("animation", "Loading frame %d: %s", i, filenames[i]);
        if (!animation_load_from_spiffs(filenames[i], anim->spiffs_imgs[i])) {
            ESP_LOGE("animation", "Failed to load %s from SPIFFS", filenames[i]);
            // Clean up - only free what was actually allocated
            for (int j = 0; j <= i; j++) {
                if (anim->spiffs_imgs[j]) {
                    if (anim->spiffs_imgs[j]->data) {
                        free((void*)anim->spiffs_imgs[j]->data);
                    }
                    free(anim->spiffs_imgs[j]);
                }
            }
            free(anim->spiffs_imgs);
            anim->spiffs_imgs = NULL;
            return false;
        }
        ESP_LOGI("animation", "Successfully loaded frame %d: %s", i, filenames[i]);
    }
    
    // Set up animation structure
    anim->imges = (const lv_image_dsc_t**)anim->spiffs_imgs;
    anim->use_spiffs = true;
    anim->len = count;
    
    // Create animation sequence (0, 1, 2, ...)
    anim->animations = (int*)malloc(count * sizeof(int));
    if (anim->animations == NULL) {
        ESP_LOGE("animation", "Failed to allocate memory for animation sequence");
        // Clean up images
        for (int i = 0; i < count; i++) {
            if (anim->spiffs_imgs[i]) {
                if (anim->spiffs_imgs[i]->data) free((void*)anim->spiffs_imgs[i]->data);
                free(anim->spiffs_imgs[i]);
            }
        }
        free(anim->spiffs_imgs);
        anim->spiffs_imgs = NULL;
        return false;
    }
    
    for (int i = 0; i < count; i++) {
        anim->animations[i] = i;
    }
    
    ESP_LOGI("animation", "Successfully created SPIFFS animation with %d frames", count);
    return true;
}

bool animation_load_normal_from_spiffs(void)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    // Clean up existing SPIFFS normal animation if any
    animation_cleanup_spiffs_animation(&spiffs_normal);
    
    // Load normal animation from SPIFFS
    const char* normal_frames[] = {"normal1.bin", "normal2.bin", "normal3.bin"};
    
    if (animation_create_spiffs_animation(&spiffs_normal, normal_frames, 3)) {
        ESP_LOGI("animation", "✅ Successfully loaded normal animation from SPIFFS");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load normal animation from SPIFFS");
        return false;
    }
}

void animation_cleanup_spiffs_animation(Animation_t* anim)
{
    if (anim && anim->use_spiffs && anim->spiffs_imgs) {
        for (int i = 0; i < anim->len; i++) {
            if (anim->spiffs_imgs[i] && anim->spiffs_imgs[i]->data) {
                free((void*)anim->spiffs_imgs[i]->data);
            }
            if (anim->spiffs_imgs[i]) {
                free(anim->spiffs_imgs[i]);
            }
        }
        free(anim->spiffs_imgs);
        anim->spiffs_imgs = NULL;
    }
    
    if (anim && anim->animations) {
        free(anim->animations);
        anim->animations = NULL;
    }
    
    // Reset animation structure
    anim->imges = NULL;
    anim->len = 0;
    anim->use_spiffs = false;
}

// Function to get the appropriate normal animation (static or SPIFFS)
Animation_t* animation_get_normal_animation(void)
{
    // Check if SPIFFS normal animation is loaded and valid
    if (spiffs_normal.use_spiffs && spiffs_normal.imges && spiffs_normal.len > 0) {
        ESP_LOGI("animation", "Using SPIFFS-based normal animation");
        return &spiffs_normal;
    } else {
        ESP_LOGI("animation", "Using static normal animation from img directory");
        return &static_normal;
    }
}

// Atomic file write functions for runtime updates
bool animation_write_file_atomic(const char* filename, const uint8_t* data, size_t size)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    char temp_path[128];
    char final_path[128];
    snprintf(temp_path, sizeof(temp_path), "/spiffs/.updates/%s.tmp", filename);
    snprintf(final_path, sizeof(final_path), "/spiffs/%s", filename);
    
    ESP_LOGI("animation", "Writing file atomically: %s (%d bytes)", filename, size);
    
    // Write to temp file
    FILE* f = fopen(temp_path, "wb");
    if (!f) {
        ESP_LOGE("animation", "Failed to create temp file: %s", temp_path);
        return false;
    }
    
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    
    if (written != size) {
        ESP_LOGE("animation", "Failed to write complete file: %d/%d bytes", written, size);
        unlink(temp_path);  // Clean up temp file
        return false;
    }
    
    // Atomic rename
    if (rename(temp_path, final_path) != 0) {
        ESP_LOGE("animation", "Failed to rename temp file to final: %s", final_path);
        unlink(temp_path);  // Clean up temp file
        return false;
    }
    
    ESP_LOGI("animation", "Successfully wrote file: %s", final_path);
    return true;
}

bool animation_delete_file(const char* filename)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);
    
    if (unlink(full_path) == 0) {
        ESP_LOGI("animation", "Successfully deleted file: %s", filename);
        return true;
    } else {
        ESP_LOGE("animation", "Failed to delete file: %s", filename);
        return false;
    }
}

bool animation_file_exists(const char* filename)
{
    if (!spiffs_initialized) {
        return false;
    }
    
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", filename);
    
    FILE* f = fopen(full_path, "rb");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

// Manifest management functions
bool animation_update_manifest(const char* filename, size_t size, const char* hash)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    // Read existing manifest
    cJSON* manifest = NULL;
    char manifest_path[128];
    snprintf(manifest_path, sizeof(manifest_path), "/spiffs/manifest.json");
    
    FILE* f = fopen(manifest_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        size_t file_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        char* json_string = (char*)malloc(file_size + 1);
        if (json_string) {
            fread(json_string, 1, file_size, f);
            json_string[file_size] = '\0';
            manifest = cJSON_Parse(json_string);
            free(json_string);
        }
        fclose(f);
    }
    
    if (!manifest) {
        manifest = cJSON_CreateObject();
        cJSON_AddNumberToObject(manifest, "version", 1);
        cJSON_AddObjectToObject(manifest, "files");
    }
    
    // Update file entry
    cJSON* files = cJSON_GetObjectItem(manifest, "files");
    if (!files) {
        files = cJSON_CreateObject();
        cJSON_AddItemToObject(manifest, "files", files);
    }
    
    cJSON* file_entry = cJSON_CreateObject();
    cJSON_AddNumberToObject(file_entry, "size", size);
    cJSON_AddStringToObject(file_entry, "hash", hash);
    cJSON_AddNumberToObject(file_entry, "timestamp", esp_timer_get_time() / 1000000); // Unix timestamp
    
    cJSON_AddItemToObject(files, filename, file_entry);
    
    // Write manifest back
    char* json_string = cJSON_Print(manifest);
    if (!json_string) {
        ESP_LOGE("animation", "Failed to create JSON string");
        cJSON_Delete(manifest);
        return false;
    }
    
    bool success = animation_write_file_atomic("manifest.json", (uint8_t*)json_string, strlen(json_string));
    
    free(json_string);
    cJSON_Delete(manifest);
    
    if (success) {
        ESP_LOGI("animation", "Manifest updated for file: %s", filename);
    } else {
        ESP_LOGE("animation", "Failed to update manifest for file: %s", filename);
    }
    
    return success;
}

bool animation_reload_animations_from_manifest(void)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    ESP_LOGI("animation", "Reloading animations from manifest...");
    
    // Reload SPIFFS animations
    animation_load_spiffs_animations();
    
    return true;
}

std::string animation_get_manifest_json(void)
{
    if (!spiffs_initialized) {
        return "{}";
    }
    
    char manifest_path[128];
    snprintf(manifest_path, sizeof(manifest_path), "/spiffs/manifest.json");
    
    FILE* f = fopen(manifest_path, "rb");
    if (!f) {
        return "{}";
    }
    
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    std::string result;
    result.resize(file_size);
    fread(&result[0], 1, file_size, f);
    fclose(f);
    
    return result;
}

void animation_show_current_sources(void)
{
    ESP_LOGI("animation", "=== Current Animation Sources ===");
    
    for (int i = 0; i < ANIMATION_NUM; i++) {
        Animation_t* anim = get_animation(i);
        const char* anim_names[] = {
            "STATIC_NORMAL", "EMBARRESSED", "FIRE", "INSPIRATION", "NORMAL",
            "QUESTION", "SHY", "SLEEP", "HAPPY"
        };
        
        if (anim->use_spiffs) {
            ESP_LOGI("animation", "  %s: SPIFFS (dynamic, RAM)", anim_names[i]);
        } else {
            ESP_LOGI("animation", "  %s: Static (img/, Flash)", anim_names[i]);
        }
    }
    ESP_LOGI("animation", "=================================");
}
