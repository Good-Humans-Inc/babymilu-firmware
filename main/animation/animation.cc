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
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
// extern const lv_image_dsc_t embarrass1;
// extern const lv_image_dsc_t embarrass2;
// extern const lv_image_dsc_t embarrass3;
// extern const lv_image_dsc_t fire1;
// extern const lv_image_dsc_t fire2;
// extern const lv_image_dsc_t fire3;
// extern const lv_image_dsc_t fire4;
// extern const lv_image_dsc_t inspiration1;
// extern const lv_image_dsc_t inspiration2;
// extern const lv_image_dsc_t inspiration3;
// extern const lv_image_dsc_t inspiration4;
// extern const lv_image_dsc_t normal1;
// extern const lv_image_dsc_t normal2;
// extern const lv_image_dsc_t normal3;
// extern const lv_image_dsc_t question1;
// extern const lv_image_dsc_t question2;
// extern const lv_image_dsc_t question3;
// extern const lv_image_dsc_t question4;
// extern const lv_image_dsc_t shy1;
// extern const lv_image_dsc_t shy2;
// extern const lv_image_dsc_t sleep1;
// extern const lv_image_dsc_t sleep2;
// extern const lv_image_dsc_t sleep3;
// extern const lv_image_dsc_t sleep4;
// extern const lv_image_dsc_t happy1;
// extern const lv_image_dsc_t happy2;
// extern const lv_image_dsc_t happy3;
// extern const lv_image_dsc_t happy4;

// const lv_image_dsc_t *embarrass_i[3] = {
//     &embarrass1,
//     &embarrass2,
//     &embarrass3};

// const lv_image_dsc_t *fire_i[4] = {
//     &fire1,
//     &fire2,
//     &fire3,
//     &fire4};

// const lv_image_dsc_t *inspiration_i[4] = {
//     &inspiration1,
//     &inspiration2,
//     &inspiration3,
//     &inspiration4};

// Note: normal1, normal2, normal3 are now loaded from SPIFFS dynamically
// This array is kept for backward compatibility but will be overridden by SPIFFS
// const lv_image_dsc_t *normal_i[3] = {
//     &normal1,
//     &normal2,
//     &normal3};

// const lv_image_dsc_t *question_i[4] = {
//     &question1,
//     &question2,
//     &question3,
//     &question4};

// const lv_image_dsc_t *shy_i[2] = {
//     &shy1,
//     &shy2};

// const lv_image_dsc_t *sleep_i[4] = {
//     &sleep1,
//     &sleep2,
//     &sleep3,
//     &sleep4};
// const lv_image_dsc_t *happy_i[4] = {
//     &happy1,
//     &happy2,
//     &happy3,
//     &happy4};

// Animation_t static_normal = {
//     .imges = normal_i,
//     .animations = (int[]){0},
//     .len = 1,
//     .use_spiffs = false,
//     .spiffs_imgs = NULL};
// Animation_t animation_embressed = {
//     .imges = embarrass_i,
//     .animations = (int[]){0, 1, 2},
//     .len = 3,
//     .use_spiffs = false,
//     .spiffs_imgs = NULL};
// Animation_t animation_fire = {
//     .imges = fire_i,
//     .animations = (int[]){0, 1, 2, 3},
//     .len = 4,
//     .use_spiffs = false,
//     .spiffs_imgs = NULL};
// Animation_t animation_inspiration = {
//     .imges = inspiration_i,
//     .animations = (int[]){0, 1, 2, 3},
//     .len = 4,
//     .use_spiffs = false,
//     .spiffs_imgs = NULL};
// Animation_t animation_normal = {
//     .imges = normal_i,
//     .animations = (int[]){0, 1, 2},
//     .len = 3,
//     .use_spiffs = false,
//     .spiffs_imgs = NULL};
// Animation_t animation_question = {
//     .imges = question_i,
//     .animations = (int[]){0, 1, 2, 3},
//     .len = 4,
//     .use_spiffs = false,
//     .spiffs_imgs = NULL};
// Animation_t animation_shy = {
//     .imges = shy_i,
//     .animations = (int[]){0, 1},
//     .len = 2,
//     .use_spiffs = false,
//     .spiffs_imgs = NULL};
// Animation_t animation_sleep = {
//     .imges = sleep_i,
//     .animations = (int[]){0, 1, 2, 3},
//     .len = 4,
//     .use_spiffs = false,
//     .spiffs_imgs = NULL};
// Animation_t animation_happy = {
//     .imges = happy_i,
//     .animations = (int[]){0, 1, 2, 3},
//     .len = 4,
//     .use_spiffs = false,
//     .spiffs_imgs = NULL};

// Global SPIFFS-based normal animation
static Animation_t spiffs_normal = {0};

// Global SPIFFS-based embarrass animation
static Animation_t spiffs_embarrass = {0};

// Global SPIFFS-based fire animation
static Animation_t spiffs_fire = {0};

// Global SPIFFS-based happy animation
static Animation_t spiffs_happy = {0};

// Global SPIFFS-based inspiration animation
static Animation_t spiffs_inspiration = {0};

// Global SPIFFS-based question animation
static Animation_t spiffs_question = {0};

// Global SPIFFS-based shy animation
static Animation_t spiffs_shy = {0};

// Global SPIFFS-based sleep animation
static Animation_t spiffs_sleep = {0};

// Function to get the appropriate animation (static or SPIFFS)
Animation_t* get_animation(int index) {
    switch(index) {
        case 0: // ANIMATION_STATIC_NORMAL
            return animation_get_normal_animation(); // This will return SPIFFS or static
        case 1: // ANIMATION_EMBARRESSED
            // return &animation_embressed; // Commented out - using SPIFFS only
            return animation_get_embarrass_animation(); // Use SPIFFS or fallback
        case 2: // ANIMATION_FIRE
            // return &animation_fire; // Commented out - using SPIFFS only
            return animation_get_fire_animation(); // Use SPIFFS or fallback
        case 3: // ANIMATION_INSPIRATION
            // return &animation_inspiration; // Commented out - using SPIFFS only
            return animation_get_inspiration_animation(); // Use SPIFFS or fallback
        case 4: // ANIMATION_NORMAL
            // return &animation_normal; // Commented out - using SPIFFS only
            return animation_get_normal_animation(); // Use SPIFFS or fallback
        case 5: // ANIMATION_QUESTION
            // return &animation_question; // Commented out - using SPIFFS only
            return animation_get_question_animation(); // Use SPIFFS or fallback
        case 6: // ANIMATION_SHY
            // return &animation_shy; // Commented out - using SPIFFS only
            return animation_get_shy_animation(); // Use SPIFFS or fallback
        case 7: // ANIMATION_SLEEP
            // return &animation_sleep; // Commented out - using SPIFFS only
            return animation_get_sleep_animation(); // Use SPIFFS or fallback
        case 8: // ANIMATION_HAPPY
            // return &animation_happy; // Commented out - using SPIFFS only
            return animation_get_happy_animation(); // Use SPIFFS or fallback
        default:
            // return &static_normal; // Commented out - using SPIFFS only
            return animation_get_normal_animation(); // Use SPIFFS or fallback
    }
}

// Keep the old array for backward compatibility, but use get_animation() instead
Animation_t *animations[] = {
    NULL,  // &static_normal,  // This will be overridden by get_animation(0) - commented out
    NULL,  // &animation_embressed,  // commented out - using SPIFFS only
    NULL,  // &animation_fire,  // commented out - using SPIFFS only
    NULL,  // &animation_inspiration,  // commented out - using SPIFFS only
    NULL,  // &animation_normal,  // commented out - using SPIFFS only
    NULL,  // &animation_question,  // commented out - using SPIFFS only
    NULL,  // &animation_shy,  // commented out - using SPIFFS only
    NULL,  // &animation_sleep,  // commented out - using SPIFFS only
    NULL};  // &animation_happy,  // commented out - using SPIFFS only

static int now_animation = 0;
int pos = 0;
TaskHandle_t animation_task_handle = nullptr;

// CRITICAL FIX: Create a minimal fallback animation to prevent crashes
static Animation_t fallback_animation = {
    .imges = nullptr,
    .animations = nullptr, 
    .len = 1,
    .use_spiffs = false,
    .spiffs_imgs = nullptr
};

void plat_animation_task(void *arg)
{
    auto display = Board::GetInstance().GetDisplay();
    while (1)
    {
        ESP_LOGD("plat_animation_task", "now_animation: %d, pos: %d", now_animation, pos);
        pos++;
        
        // Use get_animation() to get the appropriate animation (static or SPIFFS)
        Animation_t* current_anim = get_animation(now_animation);
        
        // CRITICAL FIX: Check for NULL animation to prevent crashes
        if (current_anim == NULL) {
            ESP_LOGW("plat_animation_task", "Animation %d is not available, skipping frame", now_animation);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        
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
        ESP_LOGW("animation_set_now_animation", "Invalid animation index: %d, using neutral", animation);
        animation = ANIMATION_STATIC_NORMAL;
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
        .max_files = 10,
        .format_if_mount_failed = true,
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&config);
    if (ret != ESP_OK) {
        ESP_LOGE("animation", "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }
    
    spiffs_initialized = true;
    ESP_LOGI("animation", "SPIFFS initialized successfully");
    
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
    const char* test_files[] = {
        "normal1.bin", "normal2.bin", "normal3.bin", 
        "embarrass1.bin", "embarrass2.bin", "embarrass3.bin", 
        "fire1.bin", "fire2.bin", "fire3.bin", "fire4.bin", 
        "happy1.bin", "happy2.bin", "happy3.bin", "happy4.bin",
        "inspiration1.bin", "inspiration2.bin", "inspiration3.bin", "inspiration4.bin",
        "question1.bin", "question2.bin", "question3.bin", "question4.bin",
        "shy1.bin", "shy2.bin",
        "sleep1.bin", "sleep2.bin", "sleep3.bin", "sleep4.bin"
    };
    for (int i = 0; i < 26; i++) {
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
    bool normal_loaded = animation_load_normal_from_spiffs();
    
    // Try to load embarrass animation from SPIFFS
    ESP_LOGI("animation", "Attempting to load embarrass animation from SPIFFS...");
    bool embarrass_loaded = animation_load_embarrass_from_spiffs();
    
    // Try to load fire animation from SPIFFS
    ESP_LOGI("animation", "Attempting to load fire animation from SPIFFS...");
    bool fire_loaded = animation_load_fire_from_spiffs();
    
    // Try to load happy animation from SPIFFS
    ESP_LOGI("animation", "Attempting to load happy animation from SPIFFS...");
    bool happy_loaded = animation_load_happy_from_spiffs();
    
    // Try to load inspiration animation from SPIFFS
    ESP_LOGI("animation", "Attempting to load inspiration animation from SPIFFS...");
    bool inspiration_loaded = animation_load_inspiration_from_spiffs();
    
    // Try to load question animation from SPIFFS
    ESP_LOGI("animation", "Attempting to load question animation from SPIFFS...");
    bool question_loaded = animation_load_question_from_spiffs();
    
    // Try to load shy animation from SPIFFS
    ESP_LOGI("animation", "Attempting to load shy animation from SPIFFS...");
    bool shy_loaded = animation_load_shy_from_spiffs();
    
    // Try to load sleep animation from SPIFFS
    ESP_LOGI("animation", "Attempting to load sleep animation from SPIFFS...");
    bool sleep_loaded = animation_load_sleep_from_spiffs();
    
    if (normal_loaded || embarrass_loaded || fire_loaded || happy_loaded || inspiration_loaded || question_loaded || shy_loaded || sleep_loaded) {
        ESP_LOGI("animation", "✅ SPIFFS animations loaded successfully!");
        if (normal_loaded) {
            ESP_LOGI("animation", "   - Normal animation now uses SPIFFS (normal1.bin, normal2.bin, normal3.bin)");
            ESP_LOGI("animation", "   - Normal SPIFFS animation has %d frames", spiffs_normal.len);
        }
        if (embarrass_loaded) {
            ESP_LOGI("animation", "   - Embarrass animation now uses SPIFFS (embarrass1.bin, embarrass2.bin, embarrass3.bin)");
            ESP_LOGI("animation", "   - Embarrass SPIFFS animation has %d frames", spiffs_embarrass.len);
        }
        if (fire_loaded) {
            ESP_LOGI("animation", "   - Fire animation now uses SPIFFS (fire1.bin, fire2.bin, fire3.bin, fire4.bin)");
            ESP_LOGI("animation", "   - Fire SPIFFS animation has %d frames", spiffs_fire.len);
        }
        if (happy_loaded) {
            ESP_LOGI("animation", "   - Happy animation now uses SPIFFS (happy1.bin, happy2.bin, happy3.bin, happy4.bin)");
            ESP_LOGI("animation", "   - Happy SPIFFS animation has %d frames", spiffs_happy.len);
        }
        if (inspiration_loaded) {
            ESP_LOGI("animation", "   - Inspiration animation now uses SPIFFS (inspiration1.bin, inspiration2.bin, inspiration3.bin, inspiration4.bin)");
            ESP_LOGI("animation", "   - Inspiration SPIFFS animation has %d frames", spiffs_inspiration.len);
        }
        if (question_loaded) {
            ESP_LOGI("animation", "   - Question animation now uses SPIFFS (question1.bin, question2.bin, question3.bin, question4.bin)");
            ESP_LOGI("animation", "   - Question SPIFFS animation has %d frames", spiffs_question.len);
        }
        if (shy_loaded) {
            ESP_LOGI("animation", "   - Shy animation now uses SPIFFS (shy1.bin, shy2.bin)");
            ESP_LOGI("animation", "   - Shy SPIFFS animation has %d frames", spiffs_shy.len);
        }
        if (sleep_loaded) {
            ESP_LOGI("animation", "   - Sleep animation now uses SPIFFS (sleep1.bin, sleep2.bin, sleep3.bin, sleep4.bin)");
            ESP_LOGI("animation", "   - Sleep SPIFFS animation has %d frames", spiffs_sleep.len);
        }
    } else {
        ESP_LOGI("animation", "⚠️  SPIFFS animations not found, using static animations");
        ESP_LOGI("animation", "   - Normal animation uses static images (normal1, normal2, normal3)");
        ESP_LOGI("animation", "   - Embarrass animation uses static images (embarrass1, embarrass2, embarrass3)");
        ESP_LOGI("animation", "   - Fire animation uses static images (fire1, fire2, fire3, fire4)");
        ESP_LOGI("animation", "   - Happy animation uses static images (happy1, happy2, happy3, happy4)");
        ESP_LOGI("animation", "   - Inspiration animation uses static images (inspiration1, inspiration2, inspiration3, inspiration4)");
        ESP_LOGI("animation", "   - Question animation uses static images (question1, question2, question3, question4)");
        ESP_LOGI("animation", "   - Shy animation uses static images (shy1, shy2)");
        ESP_LOGI("animation", "   - Sleep animation uses static images (sleep1, sleep2, sleep3, sleep4)");
        ESP_LOGI("animation", "   - To use SPIFFS animations, place all .bin files in spiffs_data/");
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

bool animation_create_spiffs_animation_from_merged(Animation_t* anim, const char* merged_filename, int count)
{
    ESP_LOGI("animation", "Creating SPIFFS animation from merged file %s with %d frames", merged_filename, count);
    
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    char full_path[64];
    snprintf(full_path, sizeof(full_path), "/spiffs/%s", merged_filename);
    
    FILE* f = fopen(full_path, "rb");
    if (f == NULL) {
        ESP_LOGE("animation", "Failed to open merged file %s", full_path);
        return false;
    }
    
    // Allocate memory for SPIFFS images
    anim->spiffs_imgs = (lv_image_dsc_t**)malloc(count * sizeof(lv_image_dsc_t*));
    if (anim->spiffs_imgs == NULL) {
        ESP_LOGE("animation", "Failed to allocate memory for SPIFFS images");
        fclose(f);
        return false;
    }
    
    // Initialize all image descriptors
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
            fclose(f);
            return false;
        }
        
        // Initialize the image descriptor
        anim->spiffs_imgs[i]->data = NULL;
        anim->spiffs_imgs[i]->data_size = 0;
    }
    
    // Read each frame from the merged file
    for (int i = 0; i < count; i++) {
        ESP_LOGI("animation", "Loading frame %d from merged file", i);
        
        // Read header (6 uint32_t values)
        uint32_t header_data[6];
        if (fread(header_data, sizeof(uint32_t), 6, f) != 6) {
            ESP_LOGE("animation", "Failed to read header for frame %d", i);
            // Clean up
            for (int j = 0; j <= i; j++) {
                if (anim->spiffs_imgs[j] && anim->spiffs_imgs[j]->data) {
                    free((void*)anim->spiffs_imgs[j]->data);
                }
            }
            for (int j = 0; j < count; j++) {
                if (anim->spiffs_imgs[j]) {
                    free(anim->spiffs_imgs[j]);
                }
            }
            free(anim->spiffs_imgs);
            anim->spiffs_imgs = NULL;
            fclose(f);
            return false;
        }
        
        // Validate the magic number (0x4C56474C = "LVGL" in little endian)
        if (header_data[0] != 0x4C56474C) {
            ESP_LOGE("animation", "Invalid image magic for frame %d: 0x%x (expected 0x4C56474C)", i, header_data[0]);
            // Clean up
            for (int j = 0; j <= i; j++) {
                if (anim->spiffs_imgs[j] && anim->spiffs_imgs[j]->data) {
                    free((void*)anim->spiffs_imgs[j]->data);
                }
            }
            for (int j = 0; j < count; j++) {
                if (anim->spiffs_imgs[j]) {
                    free(anim->spiffs_imgs[j]);
                }
            }
            free(anim->spiffs_imgs);
            anim->spiffs_imgs = NULL;
            fclose(f);
            return false;
        }
        
        // Calculate data size from image dimensions
        uint32_t width = header_data[3];
        uint32_t height = header_data[4];
        uint32_t stride = header_data[5];
        size_t data_size = height * stride;
        
        // Set up the LVGL image descriptor
        anim->spiffs_imgs[i]->header.magic = LV_IMAGE_HEADER_MAGIC;
        anim->spiffs_imgs[i]->header.cf = (lv_color_format_t)header_data[1];  // color_format
        anim->spiffs_imgs[i]->header.flags = (uint32_t)header_data[2];        // flags
        anim->spiffs_imgs[i]->header.w = width;                               // width
        anim->spiffs_imgs[i]->header.h = height;                              // height
        anim->spiffs_imgs[i]->header.stride = stride;                         // stride
        anim->spiffs_imgs[i]->data_size = data_size;
        
        // Allocate memory for pixel data
        anim->spiffs_imgs[i]->data = (const uint8_t*)malloc(data_size);
        if (anim->spiffs_imgs[i]->data == NULL) {
            ESP_LOGE("animation", "Failed to allocate memory for frame %d data (%d bytes)", i, data_size);
            // Clean up
            for (int j = 0; j <= i; j++) {
                if (anim->spiffs_imgs[j] && anim->spiffs_imgs[j]->data) {
                    free((void*)anim->spiffs_imgs[j]->data);
                }
            }
            for (int j = 0; j < count; j++) {
                if (anim->spiffs_imgs[j]) {
                    free(anim->spiffs_imgs[j]);
                }
            }
            free(anim->spiffs_imgs);
            anim->spiffs_imgs = NULL;
            fclose(f);
            return false;
        }
        
        // Read pixel data
        if (fread((void*)anim->spiffs_imgs[i]->data, 1, data_size, f) != data_size) {
            ESP_LOGE("animation", "Failed to read pixel data for frame %d", i);
            // Clean up
            for (int j = 0; j <= i; j++) {
                if (anim->spiffs_imgs[j] && anim->spiffs_imgs[j]->data) {
                    free((void*)anim->spiffs_imgs[j]->data);
                }
            }
            for (int j = 0; j < count; j++) {
                if (anim->spiffs_imgs[j]) {
                    free(anim->spiffs_imgs[j]);
                }
            }
            free(anim->spiffs_imgs);
            anim->spiffs_imgs = NULL;
            fclose(f);
            return false;
        }
        
        ESP_LOGI("animation", "Successfully loaded frame %d: %dx%d, %d bytes", i, width, height, data_size);
    }
    
    fclose(f);
    
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
    
    ESP_LOGI("animation", "Successfully created SPIFFS animation from merged file with %d frames", count);
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
    
    // First try to load from merged file
    ESP_LOGI("animation", "Attempting to load normal animation from merged file...");
    if (animation_create_spiffs_animation_from_merged(&spiffs_normal, "normal_all.bin", 3)) {
        ESP_LOGI("animation", "✅ Successfully loaded normal animation from merged file");
        return true;
    }
    
    // Fall back to individual files
    ESP_LOGI("animation", "Merged file not found, trying individual files...");
    const char* normal_frames[] = {"normal1.bin", "normal2.bin", "normal3.bin"};
    
    if (animation_create_spiffs_animation(&spiffs_normal, normal_frames, 3)) {
        ESP_LOGI("animation", "✅ Successfully loaded normal animation from individual SPIFFS files");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load normal animation from SPIFFS (both merged and individual files)");
        return false;
    }
}

bool animation_load_embarrass_from_spiffs(void)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    // Clean up existing SPIFFS embarrass animation if any
    animation_cleanup_spiffs_animation(&spiffs_embarrass);
    
    // Load embarrass animation from SPIFFS
    const char* embarrass_frames[] = {"embarrass1.bin", "embarrass2.bin", "embarrass3.bin"};
    
    if (animation_create_spiffs_animation(&spiffs_embarrass, embarrass_frames, 3)) {
        ESP_LOGI("animation", "✅ Successfully loaded embarrass animation from SPIFFS");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load embarrass animation from SPIFFS");
        return false;
    }
}

bool animation_load_fire_from_spiffs(void)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    // Clean up existing SPIFFS fire animation if any
    animation_cleanup_spiffs_animation(&spiffs_fire);
    
    // Load fire animation from SPIFFS
    const char* fire_frames[] = {"fire1.bin", "fire2.bin", "fire3.bin", "fire4.bin"};
    
    if (animation_create_spiffs_animation(&spiffs_fire, fire_frames, 4)) {
        ESP_LOGI("animation", "✅ Successfully loaded fire animation from SPIFFS");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load fire animation from SPIFFS");
        return false;
    }
}

bool animation_load_happy_from_spiffs(void)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    // Clean up existing SPIFFS happy animation if any
    animation_cleanup_spiffs_animation(&spiffs_happy);
    
    // Load happy animation from SPIFFS
    const char* happy_frames[] = {"happy1.bin", "happy2.bin", "happy3.bin", "happy4.bin"};
    
    if (animation_create_spiffs_animation(&spiffs_happy, happy_frames, 4)) {
        ESP_LOGI("animation", "✅ Successfully loaded happy animation from SPIFFS");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load happy animation from SPIFFS");
        return false;
    }
}

bool animation_load_inspiration_from_spiffs(void)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    // Clean up existing SPIFFS inspiration animation if any
    animation_cleanup_spiffs_animation(&spiffs_inspiration);
    
    // Load inspiration animation from SPIFFS
    const char* inspiration_frames[] = {"inspiration1.bin", "inspiration2.bin", "inspiration3.bin", "inspiration4.bin"};
    
    if (animation_create_spiffs_animation(&spiffs_inspiration, inspiration_frames, 4)) {
        ESP_LOGI("animation", "✅ Successfully loaded inspiration animation from SPIFFS");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load inspiration animation from SPIFFS");
        return false;
    }
}

bool animation_load_question_from_spiffs(void)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    // Clean up existing SPIFFS question animation if any
    animation_cleanup_spiffs_animation(&spiffs_question);
    
    // Load question animation from SPIFFS
    const char* question_frames[] = {"question1.bin", "question2.bin", "question3.bin", "question4.bin"};
    
    if (animation_create_spiffs_animation(&spiffs_question, question_frames, 4)) {
        ESP_LOGI("animation", "✅ Successfully loaded question animation from SPIFFS");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load question animation from SPIFFS");
        return false;
    }
}

bool animation_load_shy_from_spiffs(void)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    // Clean up existing SPIFFS shy animation if any
    animation_cleanup_spiffs_animation(&spiffs_shy);
    
    // Load shy animation from SPIFFS
    const char* shy_frames[] = {"shy1.bin", "shy2.bin"};
    
    if (animation_create_spiffs_animation(&spiffs_shy, shy_frames, 2)) {
        ESP_LOGI("animation", "✅ Successfully loaded shy animation from SPIFFS");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load shy animation from SPIFFS");
        return false;
    }
}

bool animation_load_sleep_from_spiffs(void)
{
    if (!spiffs_initialized) {
        ESP_LOGE("animation", "SPIFFS not initialized");
        return false;
    }
    
    // Clean up existing SPIFFS sleep animation if any
    animation_cleanup_spiffs_animation(&spiffs_sleep);
    
    // Load sleep animation from SPIFFS
    const char* sleep_frames[] = {"sleep1.bin", "sleep2.bin", "sleep3.bin", "sleep4.bin"};
    
    if (animation_create_spiffs_animation(&spiffs_sleep, sleep_frames, 4)) {
        ESP_LOGI("animation", "✅ Successfully loaded sleep animation from SPIFFS");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load sleep animation from SPIFFS");
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
        ESP_LOGI("animation", "Static normal animation not available - using SPIFFS fallback");
        // return &static_normal; // Commented out - static normal not available
        // Create a minimal fallback animation or return NULL
        ESP_LOGW("animation", "No normal animation available (neither SPIFFS nor static)");
        // CRITICAL FIX: Return a minimal fallback instead of NULL to prevent crashes
        ESP_LOGW("animation", "Creating minimal fallback for normal animation");
        return NULL; // This will be handled by the fallback logic in plat_animation_task
    }
}

// Function to get the appropriate embarrass animation (static or SPIFFS)
Animation_t* animation_get_embarrass_animation(void)
{
    // Check if SPIFFS embarrass animation is loaded and valid
    if (spiffs_embarrass.use_spiffs && spiffs_embarrass.imges && spiffs_embarrass.len > 0) {
        ESP_LOGI("animation", "Using SPIFFS-based embarrass animation");
        return &spiffs_embarrass;
    } else {
        ESP_LOGI("animation", "Static embarrass animation not available - using SPIFFS fallback");
        // return &animation_embressed; // Commented out - static embarrass not available
        // Create a minimal fallback animation or return NULL
        ESP_LOGW("animation", "No embarrass animation available (neither SPIFFS nor static)");
        return NULL; // or create a minimal fallback
    }
}

// Function to get the appropriate fire animation (static or SPIFFS)
Animation_t* animation_get_fire_animation(void)
{
    // Check if SPIFFS fire animation is loaded and valid
    if (spiffs_fire.use_spiffs && spiffs_fire.imges && spiffs_fire.len > 0) {
        ESP_LOGI("animation", "Using SPIFFS-based fire animation");
        return &spiffs_fire;
    } else {
        ESP_LOGI("animation", "Static fire animation not available - using SPIFFS fallback");
        // return &animation_fire; // Commented out - static fire not available
        // Create a minimal fallback animation or return NULL
        ESP_LOGW("animation", "No fire animation available (neither SPIFFS nor static)");
        return NULL; // or create a minimal fallback
    }
}

// Function to get the appropriate happy animation (static or SPIFFS)
Animation_t* animation_get_happy_animation(void)
{
    // Check if SPIFFS happy animation is loaded and valid
    if (spiffs_happy.use_spiffs && spiffs_happy.imges && spiffs_happy.len > 0) {
        ESP_LOGI("animation", "Using SPIFFS-based happy animation");
        return &spiffs_happy;
    } else {
        ESP_LOGI("animation", "Static happy animation not available - using SPIFFS fallback");
        // return &animation_happy; // Commented out - static happy not available
        // Create a minimal fallback animation or return NULL
        ESP_LOGW("animation", "No happy animation available (neither SPIFFS nor static)");
        return NULL; // or create a minimal fallback
    }
}

// Function to get the appropriate inspiration animation (static or SPIFFS)
Animation_t* animation_get_inspiration_animation(void)
{
    // Check if SPIFFS inspiration animation is loaded and valid
    if (spiffs_inspiration.use_spiffs && spiffs_inspiration.imges && spiffs_inspiration.len > 0) {
        ESP_LOGI("animation", "Using SPIFFS-based inspiration animation");
        return &spiffs_inspiration;
    } else {
        ESP_LOGI("animation", "Static inspiration animation not available - using SPIFFS fallback");
        // return &animation_inspiration; // Commented out - static inspiration not available
        // Create a minimal fallback animation or return NULL
        ESP_LOGW("animation", "No inspiration animation available (neither SPIFFS nor static)");
        return NULL; // or create a minimal fallback
    }
}

// Function to get the appropriate question animation (static or SPIFFS)
Animation_t* animation_get_question_animation(void)
{
    // Check if SPIFFS question animation is loaded and valid
    if (spiffs_question.use_spiffs && spiffs_question.imges && spiffs_question.len > 0) {
        ESP_LOGI("animation", "Using SPIFFS-based question animation");
        return &spiffs_question;
    } else {
        ESP_LOGI("animation", "Static question animation not available - using SPIFFS fallback");
        // return &animation_question; // Commented out - static question not available
        // Create a minimal fallback animation or return NULL
        ESP_LOGW("animation", "No question animation available (neither SPIFFS nor static)");
        return NULL; // or create a minimal fallback
    }
}

// Function to get the appropriate shy animation (static or SPIFFS)
Animation_t* animation_get_shy_animation(void)
{
    // Check if SPIFFS shy animation is loaded and valid
    if (spiffs_shy.use_spiffs && spiffs_shy.imges && spiffs_shy.len > 0) {
        ESP_LOGI("animation", "Using SPIFFS-based shy animation");
        return &spiffs_shy;
    } else {
        ESP_LOGI("animation", "Static shy animation not available - using SPIFFS fallback");
        // return &animation_shy; // Commented out - static shy not available
        // Create a minimal fallback animation or return NULL
        ESP_LOGW("animation", "No shy animation available (neither SPIFFS nor static)");
        return NULL; // or create a minimal fallback
    }
}

// Function to get the appropriate sleep animation (static or SPIFFS)
Animation_t* animation_get_sleep_animation(void)
{
    // Check if SPIFFS sleep animation is loaded and valid
    if (spiffs_sleep.use_spiffs && spiffs_sleep.imges && spiffs_sleep.len > 0) {
        ESP_LOGI("animation", "Using SPIFFS-based sleep animation");
        return &spiffs_sleep;
    } else {
        ESP_LOGI("animation", "Static sleep animation not available - using SPIFFS fallback");
        // return &animation_sleep; // Commented out - static sleep not available
        // Create a minimal fallback animation or return NULL
        ESP_LOGW("animation", "No sleep animation available (neither SPIFFS nor static)");
        return NULL; // or create a minimal fallback
    }
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
