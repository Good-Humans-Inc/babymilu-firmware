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



// Overlay pixel constants
static constexpr uint32_t LV_IMAGE_CF_OVERLAY_PIXELS = 0x4F50584C; // "OPXL"
static constexpr size_t NORMAL_OVERLAY_FRAME_COUNT = 2;  // normal2-normal3 (2 overlay frames)
static constexpr size_t EMBARRASS_OVERLAY_FRAME_COUNT = 2;
static constexpr size_t FIRE_OVERLAY_FRAME_COUNT = 3;     // Frames 2-4 (indices 1-3)
static constexpr size_t HAPPY_OVERLAY_FRAME_COUNT = 3;    // Frames 2-4 (indices 1-3)
static constexpr size_t INSPIRATION_OVERLAY_FRAME_COUNT = 3; // Frames 2-4 (indices 1-3)
static constexpr size_t QUESTION_OVERLAY_FRAME_COUNT = 3;    // Frames 2-4 (indices 1-3)
static constexpr size_t SHY_OVERLAY_FRAME_COUNT = 1;         // Frame 2 (index 1)
static constexpr size_t SLEEP_OVERLAY_FRAME_COUNT = 3;       // Frames 2-4 (indices 1-3)

// Overlay pixel runtime storage
static animation_overlay_pixel_t* normal_overlay_pixels_runtime[NORMAL_OVERLAY_FRAME_COUNT] = {nullptr, nullptr};
static size_t normal_overlay_pixel_counts[NORMAL_OVERLAY_FRAME_COUNT] = {0, 0};
static animation_overlay_frame_t normal_overlay_frame_views[NORMAL_OVERLAY_FRAME_COUNT] = {};

static animation_overlay_pixel_t* embarrass_overlay_pixels_runtime[EMBARRASS_OVERLAY_FRAME_COUNT] = {nullptr, nullptr};
static size_t embarrass_overlay_pixel_counts[EMBARRASS_OVERLAY_FRAME_COUNT] = {0, 0};
static animation_overlay_frame_t embarrass_overlay_frame_views[EMBARRASS_OVERLAY_FRAME_COUNT] = {};

static animation_overlay_pixel_t* fire_overlay_pixels_runtime[FIRE_OVERLAY_FRAME_COUNT] = {nullptr, nullptr, nullptr};
static size_t fire_overlay_pixel_counts[FIRE_OVERLAY_FRAME_COUNT] = {0, 0, 0};
static animation_overlay_frame_t fire_overlay_frame_views[FIRE_OVERLAY_FRAME_COUNT] = {};

static animation_overlay_pixel_t* happy_overlay_pixels_runtime[HAPPY_OVERLAY_FRAME_COUNT] = {nullptr, nullptr, nullptr};
static size_t happy_overlay_pixel_counts[HAPPY_OVERLAY_FRAME_COUNT] = {0, 0, 0};
static animation_overlay_frame_t happy_overlay_frame_views[HAPPY_OVERLAY_FRAME_COUNT] = {};

static animation_overlay_pixel_t* inspiration_overlay_pixels_runtime[INSPIRATION_OVERLAY_FRAME_COUNT] = {nullptr, nullptr, nullptr};
static size_t inspiration_overlay_pixel_counts[INSPIRATION_OVERLAY_FRAME_COUNT] = {0, 0, 0};
static animation_overlay_frame_t inspiration_overlay_frame_views[INSPIRATION_OVERLAY_FRAME_COUNT] = {};

static animation_overlay_pixel_t* question_overlay_pixels_runtime[QUESTION_OVERLAY_FRAME_COUNT] = {nullptr, nullptr, nullptr};
static size_t question_overlay_pixel_counts[QUESTION_OVERLAY_FRAME_COUNT] = {0, 0, 0};
static animation_overlay_frame_t question_overlay_frame_views[QUESTION_OVERLAY_FRAME_COUNT] = {};

static animation_overlay_pixel_t* shy_overlay_pixels_runtime[SHY_OVERLAY_FRAME_COUNT] = {nullptr};
static size_t shy_overlay_pixel_counts[SHY_OVERLAY_FRAME_COUNT] = {0};
static animation_overlay_frame_t shy_overlay_frame_views[SHY_OVERLAY_FRAME_COUNT] = {};

static animation_overlay_pixel_t* sleep_overlay_pixels_runtime[SLEEP_OVERLAY_FRAME_COUNT] = {nullptr, nullptr, nullptr};
static size_t sleep_overlay_pixel_counts[SLEEP_OVERLAY_FRAME_COUNT] = {0, 0, 0};
static animation_overlay_frame_t sleep_overlay_frame_views[SLEEP_OVERLAY_FRAME_COUNT] = {};

static_assert(sizeof(animation_overlay_pixel_t) == 6, "animation_overlay_pixel_t must remain 6 bytes");

// Overlay frame management functions
static void animation_clear_normal_overlay_frames(void)
{
    for (size_t i = 0; i < NORMAL_OVERLAY_FRAME_COUNT; ++i) {
        if (normal_overlay_pixels_runtime[i] != nullptr) {
            free(normal_overlay_pixels_runtime[i]);
            normal_overlay_pixels_runtime[i] = nullptr;
        }
        normal_overlay_pixel_counts[i] = 0;
        normal_overlay_frame_views[i].pixels = nullptr;
        normal_overlay_frame_views[i].count = 0;
    }
}

static bool animation_set_normal_overlay_frame(int frame_index, animation_overlay_pixel_t* pixels, size_t count)
{
    if (frame_index < 1 || frame_index > 2) {  // normal2-normal3 (indices 1-2)
        if (pixels != nullptr) {
            free(pixels);
        }
        return false;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    if (normal_overlay_pixels_runtime[idx] != nullptr) {
        free(normal_overlay_pixels_runtime[idx]);
    }
    
    normal_overlay_pixels_runtime[idx] = pixels;
    normal_overlay_pixel_counts[idx] = count;
    return true;
}

const animation_overlay_frame_t* animation_get_normal_overlay_frame(int frame_index)
{
    if (frame_index < 1 || frame_index > 2) {  // normal2-normal3 (indices 1-2)
        return nullptr;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    normal_overlay_frame_views[idx].pixels = normal_overlay_pixels_runtime[idx];
    normal_overlay_frame_views[idx].count = normal_overlay_pixel_counts[idx];
    return &normal_overlay_frame_views[idx];
}

static void animation_clear_embarrass_overlay_frames(void)
{
    for (size_t i = 0; i < EMBARRASS_OVERLAY_FRAME_COUNT; ++i) {
        if (embarrass_overlay_pixels_runtime[i] != nullptr) {
            free(embarrass_overlay_pixels_runtime[i]);
            embarrass_overlay_pixels_runtime[i] = nullptr;
        }
        embarrass_overlay_pixel_counts[i] = 0;
        embarrass_overlay_frame_views[i].pixels = nullptr;
        embarrass_overlay_frame_views[i].count = 0;
    }
}

static bool animation_set_embarrass_overlay_frame(int frame_index, animation_overlay_pixel_t* pixels, size_t count)
{
    if (frame_index < 1 || frame_index > 2) {
        if (pixels != nullptr) {
            free(pixels);
        }
        return false;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    if (embarrass_overlay_pixels_runtime[idx] != nullptr) {
        free(embarrass_overlay_pixels_runtime[idx]);
    }
    
    embarrass_overlay_pixels_runtime[idx] = pixels;
    embarrass_overlay_pixel_counts[idx] = count;
    return true;
}

const animation_overlay_frame_t* animation_get_embarrass_overlay_frame(int frame_index)
{
    if (frame_index < 1 || frame_index > 2) {
        return nullptr;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    embarrass_overlay_frame_views[idx].pixels = embarrass_overlay_pixels_runtime[idx];
    embarrass_overlay_frame_views[idx].count = embarrass_overlay_pixel_counts[idx];
    return &embarrass_overlay_frame_views[idx];
}

static void animation_clear_fire_overlay_frames(void)
{
    for (size_t i = 0; i < FIRE_OVERLAY_FRAME_COUNT; ++i) {
        if (fire_overlay_pixels_runtime[i] != nullptr) {
            free(fire_overlay_pixels_runtime[i]);
            fire_overlay_pixels_runtime[i] = nullptr;
        }
        fire_overlay_pixel_counts[i] = 0;
        fire_overlay_frame_views[i].pixels = nullptr;
        fire_overlay_frame_views[i].count = 0;
    }
}

static bool animation_set_fire_overlay_frame(int frame_index, animation_overlay_pixel_t* pixels, size_t count)
{
    if (frame_index < 1 || frame_index > 3) {
        if (pixels != nullptr) {
            free(pixels);
        }
        return false;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    if (fire_overlay_pixels_runtime[idx] != nullptr) {
        free(fire_overlay_pixels_runtime[idx]);
    }
    
    fire_overlay_pixels_runtime[idx] = pixels;
    fire_overlay_pixel_counts[idx] = count;
    return true;
}

const animation_overlay_frame_t* animation_get_fire_overlay_frame(int frame_index)
{
    if (frame_index < 1 || frame_index > 3) {
        return nullptr;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    fire_overlay_frame_views[idx].pixels = fire_overlay_pixels_runtime[idx];
    fire_overlay_frame_views[idx].count = fire_overlay_pixel_counts[idx];
    return &fire_overlay_frame_views[idx];
}

static void animation_clear_happy_overlay_frames(void)
{
    for (size_t i = 0; i < HAPPY_OVERLAY_FRAME_COUNT; ++i) {
        if (happy_overlay_pixels_runtime[i] != nullptr) {
            free(happy_overlay_pixels_runtime[i]);
            happy_overlay_pixels_runtime[i] = nullptr;
        }
        happy_overlay_pixel_counts[i] = 0;
        happy_overlay_frame_views[i].pixels = nullptr;
        happy_overlay_frame_views[i].count = 0;
    }
}

static bool animation_set_happy_overlay_frame(int frame_index, animation_overlay_pixel_t* pixels, size_t count)
{
    if (frame_index < 1 || frame_index > 3) {
        if (pixels != nullptr) {
            free(pixels);
        }
        return false;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    if (happy_overlay_pixels_runtime[idx] != nullptr) {
        free(happy_overlay_pixels_runtime[idx]);
    }
    
    happy_overlay_pixels_runtime[idx] = pixels;
    happy_overlay_pixel_counts[idx] = count;
    return true;
}

const animation_overlay_frame_t* animation_get_happy_overlay_frame(int frame_index)
{
    if (frame_index < 1 || frame_index > 3) {
        return nullptr;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    happy_overlay_frame_views[idx].pixels = happy_overlay_pixels_runtime[idx];
    happy_overlay_frame_views[idx].count = happy_overlay_pixel_counts[idx];
    return &happy_overlay_frame_views[idx];
}

static void animation_clear_inspiration_overlay_frames(void)
{
    for (size_t i = 0; i < INSPIRATION_OVERLAY_FRAME_COUNT; ++i) {
        if (inspiration_overlay_pixels_runtime[i] != nullptr) {
            free(inspiration_overlay_pixels_runtime[i]);
            inspiration_overlay_pixels_runtime[i] = nullptr;
        }
        inspiration_overlay_pixel_counts[i] = 0;
        inspiration_overlay_frame_views[i].pixels = nullptr;
        inspiration_overlay_frame_views[i].count = 0;
    }
}

static bool animation_set_inspiration_overlay_frame(int frame_index, animation_overlay_pixel_t* pixels, size_t count)
{
    if (frame_index < 1 || frame_index > 3) {
        if (pixels != nullptr) {
            free(pixels);
        }
        return false;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    if (inspiration_overlay_pixels_runtime[idx] != nullptr) {
        free(inspiration_overlay_pixels_runtime[idx]);
    }
    
    inspiration_overlay_pixels_runtime[idx] = pixels;
    inspiration_overlay_pixel_counts[idx] = count;
    return true;
}

const animation_overlay_frame_t* animation_get_inspiration_overlay_frame(int frame_index)
{
    if (frame_index < 1 || frame_index > 3) {
        return nullptr;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    inspiration_overlay_frame_views[idx].pixels = inspiration_overlay_pixels_runtime[idx];
    inspiration_overlay_frame_views[idx].count = inspiration_overlay_pixel_counts[idx];
    return &inspiration_overlay_frame_views[idx];
}

static void animation_clear_question_overlay_frames(void)
{
    for (size_t i = 0; i < QUESTION_OVERLAY_FRAME_COUNT; ++i) {
        if (question_overlay_pixels_runtime[i] != nullptr) {
            free(question_overlay_pixels_runtime[i]);
            question_overlay_pixels_runtime[i] = nullptr;
        }
        question_overlay_pixel_counts[i] = 0;
        question_overlay_frame_views[i].pixels = nullptr;
        question_overlay_frame_views[i].count = 0;
    }
}

static bool animation_set_question_overlay_frame(int frame_index, animation_overlay_pixel_t* pixels, size_t count)
{
    if (frame_index < 1 || frame_index > 3) {
        if (pixels != nullptr) {
            free(pixels);
        }
        return false;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    if (question_overlay_pixels_runtime[idx] != nullptr) {
        free(question_overlay_pixels_runtime[idx]);
    }
    
    question_overlay_pixels_runtime[idx] = pixels;
    question_overlay_pixel_counts[idx] = count;
    return true;
}

const animation_overlay_frame_t* animation_get_question_overlay_frame(int frame_index)
{
    if (frame_index < 1 || frame_index > 3) {
        return nullptr;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    question_overlay_frame_views[idx].pixels = question_overlay_pixels_runtime[idx];
    question_overlay_frame_views[idx].count = question_overlay_pixel_counts[idx];
    return &question_overlay_frame_views[idx];
}

static void animation_clear_shy_overlay_frames(void)
{
    for (size_t i = 0; i < SHY_OVERLAY_FRAME_COUNT; ++i) {
        if (shy_overlay_pixels_runtime[i] != nullptr) {
            free(shy_overlay_pixels_runtime[i]);
            shy_overlay_pixels_runtime[i] = nullptr;
        }
        shy_overlay_pixel_counts[i] = 0;
        shy_overlay_frame_views[i].pixels = nullptr;
        shy_overlay_frame_views[i].count = 0;
    }
}

static bool animation_set_shy_overlay_frame(int frame_index, animation_overlay_pixel_t* pixels, size_t count)
{
    if (frame_index < 1 || frame_index > 1) {
        if (pixels != nullptr) {
            free(pixels);
        }
        return false;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    if (shy_overlay_pixels_runtime[idx] != nullptr) {
        free(shy_overlay_pixels_runtime[idx]);
    }
    
    shy_overlay_pixels_runtime[idx] = pixels;
    shy_overlay_pixel_counts[idx] = count;
    return true;
}

const animation_overlay_frame_t* animation_get_shy_overlay_frame(int frame_index)
{
    if (frame_index < 1 || frame_index > 1) {
        return nullptr;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    shy_overlay_frame_views[idx].pixels = shy_overlay_pixels_runtime[idx];
    shy_overlay_frame_views[idx].count = shy_overlay_pixel_counts[idx];
    return &shy_overlay_frame_views[idx];
}

static void animation_clear_sleep_overlay_frames(void)
{
    for (size_t i = 0; i < SLEEP_OVERLAY_FRAME_COUNT; ++i) {
        if (sleep_overlay_pixels_runtime[i] != nullptr) {
            free(sleep_overlay_pixels_runtime[i]);
            sleep_overlay_pixels_runtime[i] = nullptr;
        }
        sleep_overlay_pixel_counts[i] = 0;
        sleep_overlay_frame_views[i].pixels = nullptr;
        sleep_overlay_frame_views[i].count = 0;
    }
}

static bool animation_set_sleep_overlay_frame(int frame_index, animation_overlay_pixel_t* pixels, size_t count)
{
    if (frame_index < 1 || frame_index > 3) {
        if (pixels != nullptr) {
            free(pixels);
        }
        return false;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    if (sleep_overlay_pixels_runtime[idx] != nullptr) {
        free(sleep_overlay_pixels_runtime[idx]);
    }
    
    sleep_overlay_pixels_runtime[idx] = pixels;
    sleep_overlay_pixel_counts[idx] = count;
    return true;
}

const animation_overlay_frame_t* animation_get_sleep_overlay_frame(int frame_index)
{
    if (frame_index < 1 || frame_index > 3) {
        return nullptr;
    }
    
    size_t idx = static_cast<size_t>(frame_index - 1);
    sleep_overlay_frame_views[idx].pixels = sleep_overlay_pixels_runtime[idx];
    sleep_overlay_frame_views[idx].count = sleep_overlay_pixel_counts[idx];
    return &sleep_overlay_frame_views[idx];
}

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
    ESP_LOGI("plat_animation_task", "Animation task started!");
    auto display = Board::GetInstance().GetDisplay();
    while (1)
    {
        ESP_LOGD("plat_animation_task", "now_animation: %d, pos: %d", now_animation, pos);
        pos++;
        
        // Use get_animation() to get the appropriate animation (SD card only)
        Animation_t* current_anim = get_animation(now_animation);
        
        // Check for NULL animation to prevent crashes
        if (current_anim == NULL) {
            ESP_LOGW("plat_animation_task", "Animation %d is NULL, skipping frame", now_animation);
            vTaskDelay(pdMS_TO_TICKS(334)); // 15 FPS: 1000ms / 15 ≈ 67ms per frame
            continue;
        }
        
        if (pos >= current_anim->len)
        {
            pos = 0;
        }
        
        // Validate frame index before accessing
        int frame_idx = current_anim->animations[pos];
        if (frame_idx < 0 || frame_idx >= current_anim->len) {
            ESP_LOGW("plat_animation_task", "Invalid frame index %d for animation %d (len=%d), resetting", frame_idx, now_animation, current_anim->len);
            pos = 0;
            frame_idx = 0;
        }
        
        // Validate image before displaying
        const lv_image_dsc_t* frame_img = current_anim->imges[frame_idx];
        if (frame_img == nullptr || frame_img->data == nullptr) {
            ESP_LOGW("plat_animation_task", "Frame %d image is null for animation %d, skipping", frame_idx, now_animation);
            vTaskDelay(pdMS_TO_TICKS(334));
            continue;
        }
        
        // Validate image dimensions to prevent crashes
        if (frame_img->header.w <= 0 || frame_img->header.h <= 0 || 
            frame_img->header.w > 10000 || frame_img->header.h > 10000) {
            ESP_LOGW("plat_animation_task", "Invalid image dimensions %dx%d for frame %d, skipping", 
                     frame_img->header.w, frame_img->header.h, frame_idx);
            vTaskDelay(pdMS_TO_TICKS(334));
            continue;
        }
        
        // Log frame change with animation type and frame number
        // ESP_LOGI("plat_animation_task", "Animation %d: Frame %d/%d", now_animation, pos, current_anim->len);
        // Pass frame index for overlay composition (normal2/normal3, etc.)
        display->SetEmotionImg(frame_img, frame_idx);
        
        // Use frame-specific delay if available, otherwise default to 334ms
        int delay_ms = 334; // Default delay
        if (current_anim->frame_delays != nullptr && frame_idx >= 0 && frame_idx < current_anim->len) {
            delay_ms = current_anim->frame_delays[frame_idx];
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void animation_set_now_animation(int animation)
{
    if (animation_task_handle == nullptr)
    {
        // Increased stack size from 2048 to 4096 to prevent stack overflow
        // Display operations and LVGL calls require more stack space
        xTaskCreatePinnedToCore(plat_animation_task, "plat_animation_task", 4096, nullptr, 4, &animation_task_handle, 0);
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

int animation_get_now_animation(void)
{
    return now_animation;
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
    
    if (anim && anim->frame_delays) {
        free(anim->frame_delays);
        anim->frame_delays = NULL;
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
    
    char mega_path[128];
    snprintf(mega_path, sizeof(mega_path), "/sdcard/test.bin");
    
    ESP_LOGI("animation", "Attempting to open mega file: %s", mega_path);
    FILE* f = fopen(mega_path, "rb");
    if (f == NULL) {
        ESP_LOGE("animation", "❌ Failed to open mega file: %s (errno: %d)", mega_path, errno);
        ESP_LOGE("animation", "Make sure test.bin exists in the root of the SD card");
        
        // Check if there are any files with similar names and try to use them
        ESP_LOGI("animation", "Checking for animation files on SD card...");
        DIR* dir2 = opendir("/sdcard");
        if (dir2 != NULL) {
            struct dirent* entry;
            char found_animation_file[256] = {0};
            while ((entry = readdir(dir2)) != NULL) {
                if (entry->d_type == DT_REG) {
                    // Check for various patterns that could be the mega file
                    if (strstr(entry->d_name, "mega") != NULL || 
                        strstr(entry->d_name, "ANIMAT") != NULL ||
                        strstr(entry->d_name, "animation") != NULL ||
                        (strstr(entry->d_name, ".bin") != NULL && strlen(found_animation_file) == 0)) {
                        
                        ESP_LOGI("animation", "  Found potential animation file: %s", entry->d_name);
                        if (strlen(found_animation_file) == 0) {
                            strncpy(found_animation_file, entry->d_name, sizeof(found_animation_file) - 1);
                        }
                    }
                }
            }
            closedir(dir2);
            
            // If we found an animation file, try to use it
            if (strlen(found_animation_file) > 0) {
                ESP_LOGI("animation", "🎯 Found animation file: %s, attempting to use it as mega file", found_animation_file);
                char alternative_path[512];
                snprintf(alternative_path, sizeof(alternative_path), "/sdcard/%s", found_animation_file);
                
                FILE* alt_f = fopen(alternative_path, "rb");
                if (alt_f != NULL) {
                    ESP_LOGI("animation", "✅ Successfully opened animation file: %s", alternative_path);
                    f = alt_f;   // Use the alternative file
                    strncpy(mega_path, alternative_path, sizeof(mega_path) - 1);
                    mega_path[sizeof(mega_path) - 1] = '\0';
                } else {
                    ESP_LOGE("animation", "❌ Failed to open animation file: %s", alternative_path);
                    return false;
                }
            } else {
                ESP_LOGE("animation", "No animation files found on SD card");
                return false;
            }
        } else {
            ESP_LOGE("animation", "Failed to open /sdcard directory for alternative search");
            return false;
        }
    }
    
    // Get file size for verification
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    ESP_LOGI("animation", "✅ Successfully opened mega file: %s (%ld bytes)", mega_path, file_size);
    
    // Animation frame counts: Normal(3: 1 base + 2 overlays), Embarrass(3), Fire(4), Happy(4), Inspiration(4), Question(4), Shy(2), Sleep(4)
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
    
    // Clear overlay frames before loading
    animation_clear_normal_overlay_frames();
    animation_clear_embarrass_overlay_frames();
    animation_clear_fire_overlay_frames();
    animation_clear_happy_overlay_frames();
    animation_clear_inspiration_overlay_frames();
    animation_clear_question_overlay_frames();
    animation_clear_shy_overlay_frames();
    animation_clear_sleep_overlay_frames();
    
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
        
        // Determine animation type for overlay handling
        bool is_normal_animation = (anim_idx == 0);
        bool is_embarrass_animation = (anim_idx == 1);
        bool is_fire_animation = (anim_idx == 2);
        bool is_happy_animation = (anim_idx == 3);
        bool is_inspiration_animation = (anim_idx == 4);
        bool is_question_animation = (anim_idx == 5);
        bool is_shy_animation = (anim_idx == 6);
        bool is_sleep_animation = (anim_idx == 7);
        
        // Store base frame pointers for overlay reuse
        lv_image_dsc_t* normal1_base_frame = NULL;
        lv_image_dsc_t* embarrass1_base_frame = NULL;
        lv_image_dsc_t* fire1_base_frame = NULL;
        lv_image_dsc_t* happy1_base_frame = NULL;
        lv_image_dsc_t* inspiration1_base_frame = NULL;
        lv_image_dsc_t* question1_base_frame = NULL;
        lv_image_dsc_t* shy1_base_frame = NULL;
        lv_image_dsc_t* sleep1_base_frame = NULL;
        
        // Load frames for this animation
        for (int frame_idx = 0; frame_idx < frame_count && success; frame_idx++) {
            ESP_LOGD("animation", "Loading frame %d from SD card mega file", current_frame);
            
            // Read header (6 uint32_t values)
            uint32_t header_data[6];
            if (fread(header_data, sizeof(uint32_t), 6, f) != 6) {
                ESP_LOGE("animation", "Failed to read header for frame %d (animation %d, frame_idx %d)", current_frame, anim_idx, frame_idx);
                ESP_LOGE("animation", "File may be truncated, corrupted, or contain invalid format (JPG/PNG mix issue?)");
                // Check if we've reached end of file
                long current_pos = ftell(f);
                fseek(f, 0, SEEK_END);
                long file_size = ftell(f);
                fseek(f, current_pos, SEEK_SET);
                ESP_LOGE("animation", "Current file position: %ld, File size: %ld, Remaining: %ld bytes", current_pos, file_size, file_size - current_pos);
                success = false;
                break;
            }
            
            // Validate the magic number
            if (header_data[0] != 0x4C56474C) {
                ESP_LOGE("animation", "Invalid image magic for frame %d (animation %d, frame_idx %d): 0x%08x", 
                         current_frame, anim_idx, frame_idx, header_data[0]);
                ESP_LOGE("animation", "Expected: 0x4C56474C (LVGL), got: 0x%08x", header_data[0]);
                // Check for common image format magic numbers
                if (header_data[0] == 0xFFD8FFE0 || header_data[0] == 0xFFD8FFE1) {
                    ESP_LOGE("animation", "Detected JPEG magic number - file may contain JPEG data instead of LVGL format!");
                } else if ((header_data[0] & 0xFFFFFFFF) == 0x89504E47) {
                    ESP_LOGE("animation", "Detected PNG magic number - file may contain PNG data instead of LVGL format!");
                }
                ESP_LOGE("animation", "All frames must be converted to LVGL binary format (RGB565) before creating mega file");
                success = false;
                break;
            }
            
            // Calculate data size from image dimensions
            uint32_t width = header_data[3];
            uint32_t height = header_data[4];
            uint32_t stride = header_data[5];
            size_t data_size = height * stride;
            
            // Check if this is an overlay frame (frame_idx > 0 and color_format is OVERLAY_PIXELS)
            bool is_overlay_frame = ((is_normal_animation || is_embarrass_animation || is_fire_animation || 
                                     is_happy_animation || is_inspiration_animation || is_question_animation ||
                                     is_shy_animation || is_sleep_animation) && 
                                    (frame_idx > 0) && (header_data[1] == LV_IMAGE_CF_OVERLAY_PIXELS));
            
            if (is_overlay_frame) {
                // This is an overlay frame - width field contains entry count
                uint32_t entry_count = width;
                
                animation_overlay_pixel_t* overlay_pixels = nullptr;
                if (entry_count > 0) {
                    size_t expected_size = entry_count * sizeof(animation_overlay_pixel_t);
                    if (expected_size != data_size) {
                        ESP_LOGW("animation", "Overlay payload size mismatch for frame %d (entries=%u, expected=%zu, actual=%zu)",
                                 current_frame, entry_count, expected_size, data_size);
                    }
                    
                    overlay_pixels = (animation_overlay_pixel_t*)malloc(entry_count * sizeof(animation_overlay_pixel_t));
                    if (overlay_pixels == nullptr) {
                        ESP_LOGE("animation", "Failed to allocate memory for overlay pixels (frame %d, count=%u)", current_frame, entry_count);
                        success = false;
                        break;
                    }
                    
                    // Read overlay pixel data (3 uint16_t per pixel: x, y, color)
                    for (uint32_t i = 0; i < entry_count; ++i) {
                        uint16_t components[3];
                        if (fread(components, sizeof(uint16_t), 3, f) != 3) {
                            ESP_LOGE("animation", "Failed to read overlay pixel %u for frame %d", i, current_frame);
                            free(overlay_pixels);
                            success = false;
                            break;
                        }
                        overlay_pixels[i].x = components[0];
                        overlay_pixels[i].y = components[1];
                        overlay_pixels[i].color = components[2];
                    }
                    
                    if (!success) {
                        break;
                    }
                    
                    // Skip any padding
                    size_t consumed = entry_count * sizeof(animation_overlay_pixel_t);
                    if (data_size > consumed) {
                        size_t remaining = data_size - consumed;
                        if (fseek(f, remaining, SEEK_CUR) != 0) {
                            ESP_LOGE("animation", "Failed to skip overlay padding for frame %d", current_frame);
                            free(overlay_pixels);
                            success = false;
                            break;
                        }
                    }
                } else if (data_size > 0) {
                    // Skip empty overlay payload
                    if (fseek(f, data_size, SEEK_CUR) != 0) {
                        ESP_LOGE("animation", "Failed to skip overlay payload for frame %d", current_frame);
                        success = false;
                        break;
                    }
                }
                
                // Store overlay pixels and reuse base frame
                bool overlay_set = false;
                if (is_normal_animation) {
                    overlay_set = animation_set_normal_overlay_frame(frame_idx, overlay_pixels, entry_count);
                    if (!overlay_set) {
                        ESP_LOGE("animation", "Failed to store normal overlay pixels for frame %d", current_frame);
                    } else if (normal1_base_frame == nullptr) {
                        ESP_LOGE("animation", "Overlay frame %d encountered before base normal frame loaded", frame_idx);
                        overlay_set = false;
                    } else {
                        anim->spiffs_imgs[frame_idx] = normal1_base_frame;
                        ESP_LOGI("animation", "Loaded %u sparse overlay pixels for normal%d (frame %d)", entry_count, frame_idx + 1, current_frame);
                    }
                } else if (is_embarrass_animation) {
                    overlay_set = animation_set_embarrass_overlay_frame(frame_idx, overlay_pixels, entry_count);
                    if (!overlay_set) {
                        ESP_LOGE("animation", "Failed to store embarrass overlay pixels for frame %d", current_frame);
                    } else if (embarrass1_base_frame == nullptr) {
                        ESP_LOGE("animation", "Overlay frame %d encountered before base embarrass frame loaded", frame_idx);
                        overlay_set = false;
                    } else {
                        anim->spiffs_imgs[frame_idx] = embarrass1_base_frame;
                        ESP_LOGI("animation", "Loaded %u sparse overlay pixels for embarrass%d (frame %d)", entry_count, frame_idx + 1, current_frame);
                    }
                } else if (is_fire_animation) {
                    overlay_set = animation_set_fire_overlay_frame(frame_idx, overlay_pixels, entry_count);
                    if (!overlay_set) {
                        ESP_LOGE("animation", "Failed to store fire overlay pixels for frame %d", current_frame);
                    } else if (fire1_base_frame == nullptr) {
                        ESP_LOGE("animation", "Overlay frame %d encountered before base fire frame loaded", frame_idx);
                        overlay_set = false;
                    } else {
                        anim->spiffs_imgs[frame_idx] = fire1_base_frame;
                        ESP_LOGI("animation", "Loaded %u sparse overlay pixels for fire%d (frame %d)", entry_count, frame_idx + 1, current_frame);
                    }
                } else if (is_happy_animation) {
                    overlay_set = animation_set_happy_overlay_frame(frame_idx, overlay_pixels, entry_count);
                    if (!overlay_set) {
                        ESP_LOGE("animation", "Failed to store happy overlay pixels for frame %d", current_frame);
                    } else if (happy1_base_frame == nullptr) {
                        ESP_LOGE("animation", "Overlay frame %d encountered before base happy frame loaded", frame_idx);
                        overlay_set = false;
                    } else {
                        anim->spiffs_imgs[frame_idx] = happy1_base_frame;
                        ESP_LOGI("animation", "Loaded %u sparse overlay pixels for happy%d (frame %d)", entry_count, frame_idx + 1, current_frame);
                    }
                } else if (is_inspiration_animation) {
                    overlay_set = animation_set_inspiration_overlay_frame(frame_idx, overlay_pixels, entry_count);
                    if (!overlay_set) {
                        ESP_LOGE("animation", "Failed to store inspiration overlay pixels for frame %d", current_frame);
                    } else if (inspiration1_base_frame == nullptr) {
                        ESP_LOGE("animation", "Overlay frame %d encountered before base inspiration frame loaded", frame_idx);
                        overlay_set = false;
                    } else {
                        anim->spiffs_imgs[frame_idx] = inspiration1_base_frame;
                        ESP_LOGI("animation", "Loaded %u sparse overlay pixels for inspiration%d (frame %d)", entry_count, frame_idx + 1, current_frame);
                    }
                } else if (is_question_animation) {
                    overlay_set = animation_set_question_overlay_frame(frame_idx, overlay_pixels, entry_count);
                    if (!overlay_set) {
                        ESP_LOGE("animation", "Failed to store question overlay pixels for frame %d", current_frame);
                    } else if (question1_base_frame == nullptr) {
                        ESP_LOGE("animation", "Overlay frame %d encountered before base question frame loaded", frame_idx);
                        overlay_set = false;
                    } else {
                        anim->spiffs_imgs[frame_idx] = question1_base_frame;
                        ESP_LOGI("animation", "Loaded %u sparse overlay pixels for question%d (frame %d)", entry_count, frame_idx + 1, current_frame);
                    }
                } else if (is_shy_animation) {
                    overlay_set = animation_set_shy_overlay_frame(frame_idx, overlay_pixels, entry_count);
                    if (!overlay_set) {
                        ESP_LOGE("animation", "Failed to store shy overlay pixels for frame %d", current_frame);
                    } else if (shy1_base_frame == nullptr) {
                        ESP_LOGE("animation", "Overlay frame %d encountered before base shy frame loaded", frame_idx);
                        overlay_set = false;
                    } else {
                        anim->spiffs_imgs[frame_idx] = shy1_base_frame;
                        ESP_LOGI("animation", "Loaded %u sparse overlay pixels for shy%d (frame %d)", entry_count, frame_idx + 1, current_frame);
                    }
                } else if (is_sleep_animation) {
                    overlay_set = animation_set_sleep_overlay_frame(frame_idx, overlay_pixels, entry_count);
                    if (!overlay_set) {
                        ESP_LOGE("animation", "Failed to store sleep overlay pixels for frame %d", current_frame);
                    } else if (sleep1_base_frame == nullptr) {
                        ESP_LOGE("animation", "Overlay frame %d encountered before base sleep frame loaded", frame_idx);
                        overlay_set = false;
                    } else {
                        anim->spiffs_imgs[frame_idx] = sleep1_base_frame;
                        ESP_LOGI("animation", "Loaded %u sparse overlay pixels for sleep%d (frame %d)", entry_count, frame_idx + 1, current_frame);
                    }
                }
                
                if (!overlay_set) {
                    if (overlay_pixels != nullptr) {
                        free(overlay_pixels);
                    }
                    success = false;
                    break;
                }
                
                current_frame++;
                continue;
            }
            
            // Regular image frame - load full image data
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
            
            // Store base frame pointer for overlay reuse
            if (is_normal_animation && frame_idx == 0) {
                normal1_base_frame = img_dsc;
            } else if (is_embarrass_animation && frame_idx == 0) {
                embarrass1_base_frame = img_dsc;
            } else if (is_fire_animation && frame_idx == 0) {
                fire1_base_frame = img_dsc;
            } else if (is_happy_animation && frame_idx == 0) {
                happy1_base_frame = img_dsc;
            } else if (is_inspiration_animation && frame_idx == 0) {
                inspiration1_base_frame = img_dsc;
            } else if (is_question_animation && frame_idx == 0) {
                question1_base_frame = img_dsc;
            } else if (is_shy_animation && frame_idx == 0) {
                shy1_base_frame = img_dsc;
            } else if (is_sleep_animation && frame_idx == 0) {
                sleep1_base_frame = img_dsc;
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
            
            // Allocate and initialize frame delays only for normal animation
            // Other animations will use the default 334ms from plat_animation_task
            bool is_normal_animation = (anim_idx == 0);  // Normal animation is first (index 0)
            
            if (is_normal_animation) {
                // Only normal animation gets explicit frame_delays
                anim->frame_delays = (int*)malloc(frame_count * sizeof(int));
                if (anim->frame_delays == NULL) {
                    ESP_LOGE("animation", "Failed to allocate memory for animation %d frame delays", anim_idx);
                    success = false;
                    break;
                }
                
                // Initialize frame delays for normal animation
                // normal1 (frame 0): 668ms, normal2/normal3: 200ms
                for (int i = 0; i < frame_count; i++) {
                    if (i == 0) {
                        // normal1: 668ms
                        anim->frame_delays[i] = 668;
                    } else if (i == 1 || i == 2) {
                        // normal2 and normal3: 200ms
                        anim->frame_delays[i] = 200;
                    } else {
                        // Any additional frames (shouldn't happen for normal): 334ms
                        anim->frame_delays[i] = 334;
                    }
                }
            } else {
                // Other animations: don't allocate frame_delays, will use default 334ms from plat_animation_task
                anim->frame_delays = nullptr;
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
        
        // Clear overlay frames on error
        animation_clear_normal_overlay_frames();
        animation_clear_embarrass_overlay_frames();
        animation_clear_fire_overlay_frames();
        animation_clear_happy_overlay_frames();
        animation_clear_inspiration_overlay_frames();
        animation_clear_question_overlay_frames();
        animation_clear_shy_overlay_frames();
        animation_clear_sleep_overlay_frames();
        
        // Reset animation structures to prevent double-free
        // Free spiffs_imgs arrays (but not the image descriptors they point to, those are in all_sd_card_imgs)
        for (int i = 0; i < 8; i++) {
            if (animations[i] != nullptr) {
                // Free the spiffs_imgs array itself (it's just an array of pointers, not the actual images)
                if (animations[i]->spiffs_imgs != nullptr) {
                    free(animations[i]->spiffs_imgs);
                    animations[i]->spiffs_imgs = nullptr;
                }
                animations[i]->imges = nullptr;
                animations[i]->len = 0;
                animations[i]->use_spiffs = false;
                if (animations[i]->animations != nullptr) {
                    free(animations[i]->animations);
                    animations[i]->animations = nullptr;
                }
                if (animations[i]->frame_delays != nullptr) {
                    free(animations[i]->frame_delays);
                    animations[i]->frame_delays = nullptr;
                }
            }
        }
        
        // Clean up on failure - free all image data
        for (int i = 0; i < total_frames; i++) {
            if (all_sd_card_imgs[i] != nullptr) {
                if (all_sd_card_imgs[i]->data != nullptr) {
                    free((void*)all_sd_card_imgs[i]->data);
                    all_sd_card_imgs[i]->data = nullptr;
                }
                free(all_sd_card_imgs[i]);
                all_sd_card_imgs[i] = nullptr;
            }
        }
        free(all_sd_card_imgs);
        all_sd_card_imgs = nullptr;
        
        return false;
    }
}



// ============================================================================
// END of major SD loading methods
// ============================================================================
