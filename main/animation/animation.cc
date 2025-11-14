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
#include "sd_card.h"
#include "custom_logging.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <type_traits>
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>



// Global SD card-based animations
static Animation_t sd_normal = {0};
static Animation_t sd_embarrass = {0};
static Animation_t sd_fire = {0};
static Animation_t sd_happy = {0};
static Animation_t sd_inspiration = {0};
static Animation_t sd_question = {0};
static Animation_t sd_shy = {0};
static Animation_t sd_sleep = {0};

// Function to get the appropriate animation (SD card only)
Animation_t* get_animation(int index) {
    switch(index) {
        case 0: // ANIMATION_STATIC_NORMAL
            return animation_get_normal_animation();
        case 1: // ANIMATION_EMBARRESSED
            return animation_get_embarrass_animation();
        case 2: // ANIMATION_FIRE
            return animation_get_fire_animation();
        case 3: // ANIMATION_INSPIRATION
            return animation_get_inspiration_animation();
        case 4: // ANIMATION_NORMAL
            return animation_get_normal_animation();
        case 5: // ANIMATION_QUESTION
            return animation_get_question_animation();
        case 6: // ANIMATION_SHY
            return animation_get_shy_animation();
        case 7: // ANIMATION_SLEEP
            return animation_get_sleep_animation();
        case 8: // ANIMATION_HAPPY
            return animation_get_happy_animation();
        default:
            return animation_get_normal_animation();
    }
}

// Animation array is no longer used - use get_animation() function instead
Animation_t *animations[] = {
    NULL,  // ANIMATION_STATIC_NORMAL
    NULL,  // ANIMATION_EMBARRESSED
    NULL,  // ANIMATION_FIRE
    NULL,  // ANIMATION_INSPIRATION
    NULL,  // ANIMATION_NORMAL
    NULL,  // ANIMATION_QUESTION
    NULL,  // ANIMATION_SHY
    NULL,  // ANIMATION_SLEEP
    NULL}; // ANIMATION_HAPPY

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
        
        // Use get_animation() to get the appropriate animation (SD card only)
        Animation_t* current_anim = get_animation(now_animation);
        
        // Check for NULL animation to prevent crashes
        if (current_anim == NULL) {
            ESP_LOGW("plat_animation_task", "Animation %d is not available, skipping frame", now_animation);
            vTaskDelay(pdMS_TO_TICKS(1000));  // Longer delay when no animation
            continue;
        }
        
        // Check if animation has valid data
        if (current_anim->imges == NULL || current_anim->animations == NULL || current_anim->len <= 0) {
            ESP_LOGW("plat_animation_task", "Animation %d has invalid data, skipping", now_animation);
            vTaskDelay(pdMS_TO_TICKS(1000));  // Longer delay when invalid
            continue;
        }
        
        if (pos >= current_anim->len)
        {
            pos = 0;
        }
        
        // Additional safety check before accessing array
        if (pos < 0 || pos >= current_anim->len) {
            ESP_LOGE("plat_animation_task", "Invalid pos %d for animation len %d, resetting", pos, current_anim->len);
            pos = 0;
        }
        
        const lv_image_dsc_t* img = current_anim->imges[current_anim->animations[pos]];
        if (img != NULL) {
            display->SetEmotionImg(img);
        } else {
            ESP_LOGW("plat_animation_task", "Image at pos %d is NULL, skipping", pos);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void animation_set_now_animation(int animation)
{
    if (animation < 0 || animation >= ANIMATION_NUM)
    {
        ESP_LOGW("animation_set_now_animation", "Invalid animation index: %d, using neutral", animation);
        animation = ANIMATION_STATIC_NORMAL;
    }
    
    // Check if animation is available before starting task
    Animation_t* anim = get_animation(animation);
    if (anim == NULL || anim->imges == NULL || anim->len <= 0) {
        ESP_LOGW("animation_set_now_animation", "Animation %d is not available, not starting task", animation);
        // Don't start task if no valid animation
        return;
    }
    
    if (animation_task_handle == nullptr)
    {
        // Increased stack size from 2048 to 4096 to prevent stack overflow
        xTaskCreatePinnedToCore(plat_animation_task, "plat_animation_task", 4096, nullptr, 4, &animation_task_handle, 0);
    }
    
    ESP_LOGI("animation_set_now_animation", "Set now animation: %d", animation);
    now_animation = animation;
    pos = 0;
}

// Animation initialization function
void animation_init(void)
{
    ESP_LOGI("animation", "Initializing animations from SD card...");
    
    // Try to load animations from SD card
    animation_load_sd_card_animations();
}


void animation_load_sd_card_animations(void)
{
    ESP_LOGI("animation", "Attempting to load animations from SD card...");
    
    // Debug SD card status before attempting to load
    SdCard::DebugStatus();
    
    // Try to load ALL animations from SD card mega file
    if (animation_load_all_from_sd_card()) {
        ESP_LOGI("animation", "🎉 Successfully loaded ALL animations from SD card!");
        ESP_LOGI("animation", "   - All 8 animation types loaded in one operation");
        ESP_LOGI("animation", "   - Total of 28 frames loaded from test.bin on SD card");
        ESP_LOGI("animation", "   - Ultimate optimization achieved!");
        return; // Success! No need to load individual animations
    }
    
    // Fall back to individual animation loading from SD card
    ESP_LOGI("animation", "Mega file not found on SD card, loading individual animations from SD card...");
    
    // Try to load individual animations from SD card
    bool normal_loaded = animation_load_normal_from_sd_card();
    bool embarrass_loaded = animation_load_embarrass_from_sd_card();
    bool fire_loaded = animation_load_fire_from_sd_card();
    bool happy_loaded = animation_load_happy_from_sd_card();
    bool inspiration_loaded = animation_load_inspiration_from_sd_card();
    bool question_loaded = animation_load_question_from_sd_card();
    bool shy_loaded = animation_load_shy_from_sd_card();
    bool sleep_loaded = animation_load_sleep_from_sd_card();
    
    if (normal_loaded || embarrass_loaded || fire_loaded || happy_loaded || inspiration_loaded || question_loaded || shy_loaded || sleep_loaded) {
        ESP_LOGI("animation", "✅ SD card animations loaded successfully!");
        if (normal_loaded) {
            ESP_LOGI("animation", "   - Normal animation now uses SD card (normal1.bin, normal2.bin, normal3.bin)");
            ESP_LOGI("animation", "   - Normal SD card animation has %d frames", sd_normal.len);
        }
        if (embarrass_loaded) {
            ESP_LOGI("animation", "   - Embarrass animation now uses SD card (embarrass1.bin, embarrass2.bin, embarrass3.bin)");
            ESP_LOGI("animation", "   - Embarrass SD card animation has %d frames", sd_embarrass.len);
        }
        if (fire_loaded) {
            ESP_LOGI("animation", "   - Fire animation now uses SD card (fire1.bin, fire2.bin, fire3.bin, fire4.bin)");
            ESP_LOGI("animation", "   - Fire SD card animation has %d frames", sd_fire.len);
        }
        if (happy_loaded) {
            ESP_LOGI("animation", "   - Happy animation now uses SD card (happy1.bin, happy2.bin, happy3.bin, happy4.bin)");
            ESP_LOGI("animation", "   - Happy SD card animation has %d frames", sd_happy.len);
        }
        if (inspiration_loaded) {
            ESP_LOGI("animation", "   - Inspiration animation now uses SD card (inspiration1.bin, inspiration2.bin, inspiration3.bin, inspiration4.bin)");
            ESP_LOGI("animation", "   - Inspiration SD card animation has %d frames", sd_inspiration.len);
        }
        if (question_loaded) {
            ESP_LOGI("animation", "   - Question animation now uses SD card (question1.bin, question2.bin, question3.bin, question4.bin)");
            ESP_LOGI("animation", "   - Question SD card animation has %d frames", sd_question.len);
        }
        if (shy_loaded) {
            ESP_LOGI("animation", "   - Shy animation now uses SD card (shy1.bin, shy2.bin)");
            ESP_LOGI("animation", "   - Shy SD card animation has %d frames", sd_shy.len);
        }
        if (sleep_loaded) {
            ESP_LOGI("animation", "   - Sleep animation now uses SD card (sleep1.bin, sleep2.bin, sleep3.bin, sleep4.bin)");
            ESP_LOGI("animation", "   - Sleep SD card animation has %d frames", sd_sleep.len);
        }
    } else {
        ESP_LOGW("animation", "⚠️  SD card animations not found");
        ESP_LOGW("animation", "   - To use SD card animations, place test.bin on the SD card");
        ESP_LOGW("animation", "   - Or place individual .bin files on the SD card");
    }
}






void animation_cleanup_sd_card_animation(Animation_t* anim)
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

// Function to get the appropriate normal animation (SD card only)
Animation_t* animation_get_normal_animation(void)
{
    // Check if SD card normal animation is loaded and valid
    if (sd_normal.use_spiffs && sd_normal.imges && sd_normal.len > 0) {
        return &sd_normal;
    } else {
        ESP_LOGW("animation", "No normal animation available from SD card");
        return NULL; // This will be handled by the fallback logic in plat_animation_task
    }
}

// Function to get the appropriate embarrass animation (SD card only)
Animation_t* animation_get_embarrass_animation(void)
{
    if (sd_embarrass.use_spiffs && sd_embarrass.imges && sd_embarrass.len > 0) {
        return &sd_embarrass;
    } else {
        ESP_LOGW("animation", "No embarrass animation available from SD card");
        return NULL;
    }
}

// Function to get the appropriate fire animation (SD card only)
Animation_t* animation_get_fire_animation(void)
{
    if (sd_fire.use_spiffs && sd_fire.imges && sd_fire.len > 0) {
        return &sd_fire;
    } else {
        ESP_LOGW("animation", "No fire animation available from SD card");
        return NULL;
    }
}

// Function to get the appropriate happy animation (SD card only)
Animation_t* animation_get_happy_animation(void)
{
    if (sd_happy.use_spiffs && sd_happy.imges && sd_happy.len > 0) {
        return &sd_happy;
    } else {
        ESP_LOGW("animation", "No happy animation available from SD card");
        return NULL;
    }
}

// Function to get the appropriate inspiration animation (SD card only)
Animation_t* animation_get_inspiration_animation(void)
{
    if (sd_inspiration.use_spiffs && sd_inspiration.imges && sd_inspiration.len > 0) {
        return &sd_inspiration;
    } else {
        ESP_LOGW("animation", "No inspiration animation available from SD card");
        return NULL;
    }
}

// Function to get the appropriate question animation (SD card only)
Animation_t* animation_get_question_animation(void)
{
    if (sd_question.use_spiffs && sd_question.imges && sd_question.len > 0) {
        return &sd_question;
    } else {
        ESP_LOGW("animation", "No question animation available from SD card");
        return NULL;
    }
}

// Function to get the appropriate shy animation (SD card only)
Animation_t* animation_get_shy_animation(void)
{
    if (sd_shy.use_spiffs && sd_shy.imges && sd_shy.len > 0) {
        return &sd_shy;
    } else {
        ESP_LOGW("animation", "No shy animation available from SD card");
        return NULL;
    }
}

// Function to get the appropriate sleep animation (SD card only)
Animation_t* animation_get_sleep_animation(void)
{
    if (sd_sleep.use_spiffs && sd_sleep.imges && sd_sleep.len > 0) {
        return &sd_sleep;
    } else {
        ESP_LOGW("animation", "No sleep animation available from SD card");
        return NULL;
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
        
        if (anim && anim->use_spiffs) {
            ESP_LOGI("animation", "  %s: SD Card (dynamic, RAM)", anim_names[i]);
        } else {
            ESP_LOGI("animation", "  %s: Not available", anim_names[i]);
        }
    }
    ESP_LOGI("animation", "=================================");
}



// ============================================================================
// SD CARD ANIMATION LOADING FUNCTIONS
// ============================================================================

bool animation_load_from_sd_card(const char* filename, lv_image_dsc_t* img_dsc)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "/sdcard/%s", filename);
    
    FILE* f = fopen(full_path, "rb");
    if (f == NULL) {
        ESP_LOGE("animation", "Failed to open %s", full_path);
        return false;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    ESP_LOGI("animation", "Loading %s from SD card: %d bytes", filename, file_size);
    
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
    
    ESP_LOGI("animation", "Loaded image from SD card: %dx%d, format=%d, data_size=%d", 
             img_dsc->header.w, img_dsc->header.h, img_dsc->header.cf, img_dsc->data_size);
    
    fclose(f);
    ESP_LOGI("animation", "Successfully loaded %s from SD card (%d bytes)", filename, img_dsc->data_size);
    return true;
}

bool animation_create_sd_card_animation(Animation_t* anim, const char* filenames[], int count)
{
    ESP_LOGI("animation", "Creating SD card animation with %d frames", count);
    
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    // Allocate memory for SD card images
    anim->spiffs_imgs = (lv_image_dsc_t**)malloc(count * sizeof(lv_image_dsc_t*));
    if (anim->spiffs_imgs == NULL) {
        ESP_LOGE("animation", "Failed to allocate memory for SD card images");
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
        
        // Load image from SD card
        ESP_LOGI("animation", "Loading frame %d: %s", i, filenames[i]);
        if (!animation_load_from_sd_card(filenames[i], anim->spiffs_imgs[i])) {
            ESP_LOGE("animation", "Failed to load %s from SD card", filenames[i]);
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
    
    ESP_LOGI("animation", "Successfully created SD card animation with %d frames", count);
    return true;
}

bool animation_create_sd_card_animation_from_merged(Animation_t* anim, const char* merged_filename, int count)
{
    ESP_LOGI("animation", "Creating SD card animation from merged file %s with %d frames", merged_filename, count);
    
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "/sdcard/%s", merged_filename);
    
    FILE* f = fopen(full_path, "rb");
    if (f == NULL) {
        ESP_LOGE("animation", "Failed to open merged file %s", full_path);
        return false;
    }
    
    // Allocate memory for SD card images
    anim->spiffs_imgs = (lv_image_dsc_t**)malloc(count * sizeof(lv_image_dsc_t*));
    if (anim->spiffs_imgs == NULL) {
        ESP_LOGE("animation", "Failed to allocate memory for SD card images");
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
        ESP_LOGI("animation", "Loading frame %d from merged file on SD card", i);
        
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
    
    ESP_LOGI("animation", "Successfully created SD card animation from merged file with %d frames", count);
    return true;
}

// Convenient wrapper function with a clear name
bool load_mega_animation_from_sd_card(void)
{
    return animation_load_all_from_sd_card();
}

bool animation_load_all_from_sd_card(void)
{
    ESP_LOGI("animation", "Checking SD card mount status...");
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted - cannot load animations from SD card");
        ESP_LOGE("animation", "This may happen if SD card initialization failed during startup");
        return false;
    }
    
    ESP_LOGI("animation", "✅ SD card is mounted, proceeding with animation loading...");
    ESP_LOGI("animation", "Attempting to load ALL animations from SD card mega file...");
    
    // First, let's list what files are actually on the SD card
    ESP_LOGI("animation", "Listing files on SD card to debug...");
    DIR* dir = opendir("/sdcard");
    if (dir != NULL) {
        struct dirent* entry;
        int file_count = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {  // Regular file
                file_count++;
                ESP_LOGI("animation", "  Found file on SD card: %s", entry->d_name);
                
                // Check file size
                char full_path[512];
                snprintf(full_path, sizeof(full_path), "/sdcard/%s", entry->d_name);
                struct stat st;
                if (stat(full_path, &st) == 0) {
                    ESP_LOGI("animation", "    Size: %ld bytes", st.st_size);
                }
            }
        }
        closedir(dir);
        ESP_LOGI("animation", "Total files found on SD card: %d", file_count);
    } else {
        ESP_LOGE("animation", "Failed to open /sdcard directory");
        return false;
    }
    
    char mega_path[512];  // Increased buffer size to accommodate full path
    FILE* f = NULL;
    
    // Try to find the animation file with case-insensitive matching
    ESP_LOGI("animation", "Searching for animation file (test.bin or TEST.BIN)...");
    DIR* dir2 = opendir("/sdcard");
    if (dir2 != NULL) {
        struct dirent* entry;
        char found_animation_file[256] = {0};
        
        while ((entry = readdir(dir2)) != NULL) {
            if (entry->d_type == DT_REG) {
                // Case-insensitive check for test.bin / TEST.BIN
                if (strcasecmp(entry->d_name, "test.bin") == 0 || 
                    strcasecmp(entry->d_name, "TEST.BIN") == 0) {
                    ESP_LOGI("animation", "🎯 Found test.bin file: %s", entry->d_name);
                    strncpy(found_animation_file, entry->d_name, sizeof(found_animation_file) - 1);
                    break;  // Prioritize test.bin
                }
                // Also check for other animation file patterns as fallback
                else if (strlen(found_animation_file) == 0 &&
                         (strstr(entry->d_name, "mega") != NULL || 
                          strstr(entry->d_name, "MEGA") != NULL ||
                          strstr(entry->d_name, "ANIMAT") != NULL ||
                          strstr(entry->d_name, "animation") != NULL ||
                          strstr(entry->d_name, ".bin") != NULL)) {
                    ESP_LOGI("animation", "  Found potential animation file: %s", entry->d_name);
                    strncpy(found_animation_file, entry->d_name, sizeof(found_animation_file) - 1);
                }
            }
        }
        closedir(dir2);
        
        // Try to open the found animation file
        if (strlen(found_animation_file) > 0) {
            snprintf(mega_path, sizeof(mega_path), "/sdcard/%s", found_animation_file);
            ESP_LOGI("animation", "Attempting to open animation file: %s", mega_path);
            f = fopen(mega_path, "rb");
            if (f != NULL) {
                ESP_LOGI("animation", "✅ Successfully opened animation file: %s", mega_path);
            } else {
                ESP_LOGE("animation", "❌ Failed to open animation file: %s (errno: %d)", mega_path, errno);
                return false;
            }
        } else {
            ESP_LOGE("animation", "❌ No animation files found on SD card");
            ESP_LOGE("animation", "Make sure test.bin or TEST.BIN exists in the root of the SD card");
            return false;
        }
    } else {
        ESP_LOGE("animation", "Failed to open /sdcard directory for file search");
        return false;
    }
    
    // Get file size for verification
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    ESP_LOGI("animation", "✅ Successfully opened mega file: %s (%ld bytes)", mega_path, file_size);
    
    // Animation frame counts: Normal(3), Embarrass(3), Fire(4), Happy(4), Inspiration(4), Question(4), Shy(2), Sleep(4)
    int animation_frame_counts[] = {3, 3, 4, 4, 4, 4, 2, 4};
    Animation_t* animations[] = {
        &sd_normal, &sd_embarrass, &sd_fire, &sd_happy,
        &sd_inspiration, &sd_question, &sd_shy, &sd_sleep
    };
    
    int total_frames = 0;
    for (int i = 0; i < 8; i++) {
        total_frames += animation_frame_counts[i];
    }
    
    ESP_LOGI("animation", "Loading %d total frames from SD card mega file", total_frames);
    
    // Clean up existing animations
    for (int i = 0; i < 8; i++) {
        animation_cleanup_sd_card_animation(animations[i]);
    }
    
    // Allocate memory for all SD card images
    lv_image_dsc_t** all_sd_card_imgs = (lv_image_dsc_t**)malloc(total_frames * sizeof(lv_image_dsc_t*));
    if (all_sd_card_imgs == NULL) {
        ESP_LOGE("animation", "Failed to allocate memory for all SD card images");
        fclose(f);
        return false;
    }
    
    // Initialize all image descriptors
    for (int i = 0; i < total_frames; i++) {
        all_sd_card_imgs[i] = (lv_image_dsc_t*)malloc(sizeof(lv_image_dsc_t));
        if (all_sd_card_imgs[i] == NULL) {
            ESP_LOGE("animation", "Failed to allocate memory for image %d", i);
            // Clean up
            for (int j = 0; j < i; j++) {
                if (all_sd_card_imgs[j]) {
                    if (all_sd_card_imgs[j]->data) free((void*)all_sd_card_imgs[j]->data);
                    free(all_sd_card_imgs[j]);
                }
            }
            free(all_sd_card_imgs);
            fclose(f);
            return false;
        }
        
        all_sd_card_imgs[i]->data = NULL;
        all_sd_card_imgs[i]->data_size = 0;
    }
    
    // Read all frames from mega file
    int current_frame = 0;
    bool success = true;
    
    for (int anim_idx = 0; anim_idx < 8 && success; anim_idx++) {
        int frame_count = animation_frame_counts[anim_idx];
        Animation_t* anim = animations[anim_idx];
        
        ESP_LOGI("animation", "Loading animation %d from SD card: %d frames", anim_idx, frame_count);
        
        // Allocate memory for this animation's images
        anim->spiffs_imgs = (lv_image_dsc_t**)malloc(frame_count * sizeof(lv_image_dsc_t*));
        if (anim->spiffs_imgs == NULL) {
            ESP_LOGE("animation", "Failed to allocate memory for animation %d images", anim_idx);
            success = false;
            break;
        }
        
        // Load frames for this animation
        for (int frame_idx = 0; frame_idx < frame_count && success; frame_idx++) {
            ESP_LOGD("animation", "Loading frame %d from SD card mega file", current_frame);
            
            // Read header (6 uint32_t values)
            uint32_t header_data[6];
            if (fread(header_data, sizeof(uint32_t), 6, f) != 6) {
                ESP_LOGE("animation", "Failed to read header for frame %d", current_frame);
                success = false;
                break;
            }
            
            // Validate the magic number
            if (header_data[0] != 0x4C56474C) {
                ESP_LOGE("animation", "Invalid image magic for frame %d: 0x%x", current_frame, header_data[0]);
                success = false;
                break;
            }
            
            // Calculate data size from image dimensions
            uint32_t width = header_data[3];
            uint32_t height = header_data[4];
            uint32_t stride = header_data[5];
            size_t data_size = height * stride;
            
            // Set up the LVGL image descriptor
            lv_image_dsc_t* img_dsc = all_sd_card_imgs[current_frame];
            img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
            img_dsc->header.cf = (lv_color_format_t)header_data[1];
            img_dsc->header.flags = (uint32_t)header_data[2];
            img_dsc->header.w = width;
            img_dsc->header.h = height;
            img_dsc->header.stride = stride;
            img_dsc->data_size = data_size;
            
            // Allocate memory for pixel data
            img_dsc->data = (const uint8_t*)malloc(data_size);
            if (img_dsc->data == NULL) {
                ESP_LOGE("animation", "Failed to allocate memory for frame %d data (%d bytes)", current_frame, data_size);
                success = false;
                break;
            }
            
            // Read pixel data
            if (fread((void*)img_dsc->data, 1, data_size, f) != data_size) {
                ESP_LOGE("animation", "Failed to read pixel data for frame %d", current_frame);
                success = false;
                break;
            }
            
            // Assign to animation
            anim->spiffs_imgs[frame_idx] = img_dsc;
            
            ESP_LOGD("animation", "Successfully loaded frame %d: %dx%d, %d bytes", current_frame, width, height, data_size);
            current_frame++;
        }
        
        if (success) {
            // Set up animation structure
            anim->imges = (const lv_image_dsc_t**)anim->spiffs_imgs;
            anim->use_spiffs = true;
            anim->len = frame_count;
            
            // Create animation sequence (0, 1, 2, ...)
            anim->animations = (int*)malloc(frame_count * sizeof(int));
            if (anim->animations == NULL) {
                ESP_LOGE("animation", "Failed to allocate memory for animation %d sequence", anim_idx);
                success = false;
                break;
            }
            
            for (int i = 0; i < frame_count; i++) {
                anim->animations[i] = i;
            }
            
            ESP_LOGI("animation", "✅ Successfully loaded animation %d from SD card with %d frames", anim_idx, frame_count);
        }
    }
    
    fclose(f);
    
    if (success) {
        ESP_LOGI("animation", "✅ Successfully loaded ALL animations from SD card mega file (%d total frames)", total_frames);
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load animations from SD card mega file");
        
        // Clean up on failure
        for (int i = 0; i < total_frames; i++) {
            if (all_sd_card_imgs[i]) {
                if (all_sd_card_imgs[i]->data) free((void*)all_sd_card_imgs[i]->data);
                free(all_sd_card_imgs[i]);
            }
        }
        free(all_sd_card_imgs);
        
        // Clean up partial animations
        for (int i = 0; i < 8; i++) {
            animation_cleanup_sd_card_animation(animations[i]);
        }
        
        return false;
    }
}

// Individual animation loading functions for SD card
bool animation_load_normal_from_sd_card(void)
{
    ESP_LOGI("animation", "Checking SD card mount status for normal animation...");
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted - cannot load normal animation from SD card");
        return false;
    }
    
    ESP_LOGI("animation", "✅ SD card is mounted, loading normal animation...");
    
    // Clean up existing SD card normal animation if any
    animation_cleanup_sd_card_animation(&sd_normal);
    
    // First try to load from merged file
    ESP_LOGI("animation", "Attempting to load normal animation from merged file on SD card...");
    if (animation_create_sd_card_animation_from_merged(&sd_normal, "normal_all.bin", 3)) {
        ESP_LOGI("animation", "✅ Successfully loaded normal animation from merged file on SD card");
        return true;
    }
    
    // Fall back to individual files
    ESP_LOGI("animation", "Merged file not found on SD card, trying individual files...");
    const char* normal_frames[] = {"normal1.bin", "normal2.bin", "normal3.bin"};
    
    if (animation_create_sd_card_animation(&sd_normal, normal_frames, 3)) {
        ESP_LOGI("animation", "✅ Successfully loaded normal animation from individual SD card files");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load normal animation from SD card (both merged and individual files)");
        return false;
    }
}

bool animation_load_embarrass_from_sd_card(void)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    // Clean up existing SD card embarrass animation if any
    animation_cleanup_sd_card_animation(&sd_embarrass);
    
    // Load embarrass animation from SD card
    const char* embarrass_frames[] = {"embarrass1.bin", "embarrass2.bin", "embarrass3.bin"};
    
    if (animation_create_sd_card_animation(&sd_embarrass, embarrass_frames, 3)) {
        ESP_LOGI("animation", "✅ Successfully loaded embarrass animation from SD card");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load embarrass animation from SD card");
        return false;
    }
}

bool animation_load_fire_from_sd_card(void)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    // Clean up existing SD card fire animation if any
    animation_cleanup_sd_card_animation(&sd_fire);
    
    // Load fire animation from SD card
    const char* fire_frames[] = {"fire1.bin", "fire2.bin", "fire3.bin", "fire4.bin"};
    
    if (animation_create_sd_card_animation(&sd_fire, fire_frames, 4)) {
        ESP_LOGI("animation", "✅ Successfully loaded fire animation from SD card");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load fire animation from SD card");
        return false;
    }
}

bool animation_load_happy_from_sd_card(void)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    // Clean up existing SD card happy animation if any
    animation_cleanup_sd_card_animation(&sd_happy);
    
    // Load happy animation from SD card
    const char* happy_frames[] = {"happy1.bin", "happy2.bin", "happy3.bin", "happy4.bin"};
    
    if (animation_create_sd_card_animation(&sd_happy, happy_frames, 4)) {
        ESP_LOGI("animation", "✅ Successfully loaded happy animation from SD card");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load happy animation from SD card");
        return false;
    }
}

bool animation_load_inspiration_from_sd_card(void)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    // Clean up existing SD card inspiration animation if any
    animation_cleanup_sd_card_animation(&sd_inspiration);
    
    // Load inspiration animation from SD card
    const char* inspiration_frames[] = {"inspiration1.bin", "inspiration2.bin", "inspiration3.bin", "inspiration4.bin"};
    
    if (animation_create_sd_card_animation(&sd_inspiration, inspiration_frames, 4)) {
        ESP_LOGI("animation", "✅ Successfully loaded inspiration animation from SD card");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load inspiration animation from SD card");
        return false;
    }
}

bool animation_load_question_from_sd_card(void)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    // Clean up existing SD card question animation if any
    animation_cleanup_sd_card_animation(&sd_question);
    
    // Load question animation from SD card
    const char* question_frames[] = {"question1.bin", "question2.bin", "question3.bin", "question4.bin"};
    
    if (animation_create_sd_card_animation(&sd_question, question_frames, 4)) {
        ESP_LOGI("animation", "✅ Successfully loaded question animation from SD card");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load question animation from SD card");
        return false;
    }
}

bool animation_load_shy_from_sd_card(void)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    // Clean up existing SD card shy animation if any
    animation_cleanup_sd_card_animation(&sd_shy);
    
    // Load shy animation from SD card
    const char* shy_frames[] = {"shy1.bin", "shy2.bin"};
    
    if (animation_create_sd_card_animation(&sd_shy, shy_frames, 2)) {
        ESP_LOGI("animation", "✅ Successfully loaded shy animation from SD card");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load shy animation from SD card");
        return false;
    }
}

bool animation_load_sleep_from_sd_card(void)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    // Clean up existing SD card sleep animation if any
    animation_cleanup_sd_card_animation(&sd_sleep);
    
    // Load sleep animation from SD card
    const char* sleep_frames[] = {"sleep1.bin", "sleep2.bin", "sleep3.bin", "sleep4.bin"};
    
    if (animation_create_sd_card_animation(&sd_sleep, sleep_frames, 4)) {
        ESP_LOGI("animation", "✅ Successfully loaded sleep animation from SD card");
        return true;
    } else {
        ESP_LOGE("animation", "❌ Failed to load sleep animation from SD card");
        return false;
    }
}
