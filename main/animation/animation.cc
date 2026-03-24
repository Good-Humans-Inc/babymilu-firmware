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
#include "application.h"
#include "sd_card.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <type_traits>
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // For strcasecmp
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>  // For unlink() and access()
#include <wifi_station.h>

// Overlay pixel format magic number (0x4F50584C = "OPXL" in ASCII)
#define OVERLAY_PIXELS_FORMAT 0x4F50584C
#define OVERLAY_ENTRY_SIZE_BYTES 6  // uint16 x, uint16 y, uint16 color

/**
 * Apply overlay pixels to a base frame and create a new RGB565 frame
 * @param base_frame The base frame (RGB565 format)
 * @param overlay_data Pointer to overlay pixel data (array of {x, y, color})
 * @param overlay_count Number of overlay pixels
 * @param width Frame width
 * @param height Frame height
 * @param output_data Output buffer (must be pre-allocated, size = width * height * 2)
 * @return true on success, false on failure
 */
static bool apply_overlay_to_frame(const uint8_t* base_frame, const uint8_t* overlay_data, 
                                    uint32_t overlay_count, uint32_t width, uint32_t height,
                                    uint8_t* output_data) {
    if (!base_frame || !overlay_data || !output_data) {
        ESP_LOGE("animation", "Invalid parameters for overlay application");
        return false;
    }
    
    // Copy base frame to output
    size_t frame_size = width * height * 2;  // RGB565 = 2 bytes per pixel
    memcpy(output_data, base_frame, frame_size);
    
    // Apply overlay pixels
    for (uint32_t i = 0; i < overlay_count; i++) {
        const uint8_t* entry = overlay_data + (i * OVERLAY_ENTRY_SIZE_BYTES);
        uint16_t x = entry[0] | (entry[1] << 8);
        uint16_t y = entry[2] | (entry[3] << 8);
        uint16_t color = entry[4] | (entry[5] << 8);
        
        // Bounds check
        if (x >= width || y >= height) {
            ESP_LOGW("animation", "Overlay pixel out of bounds: (%u, %u) for %ux%u frame", x, y, width, height);
            continue;
        }
        
        // Apply pixel (RGB565 is little-endian: low byte first)
        size_t pixel_offset = (y * width + x) * 2;
        output_data[pixel_offset] = color & 0xFF;
        output_data[pixel_offset + 1] = (color >> 8) & 0xFF;
    }
    
    return true;
}

// Global SD card-based animations
static Animation_t sd_normal = {0};
static Animation_t sd_embarrass = {0};
static Animation_t sd_fire = {0};
static Animation_t sd_happy = {0};
static Animation_t sd_inspiration = {0};
static Animation_t sd_shy = {0};
static Animation_t sd_sleep = {0};
static Animation_t sd_laugh = {0};
static Animation_t sd_sad = {0};
static Animation_t sd_cry = {0};
static Animation_t sd_silence = {0};
static Animation_t sd_listening = {0};
static Animation_t sd_smirk = {0};
static Animation_t sd_wifi = {0};
static Animation_t sd_battery = {0};
// Set true when test.bin is present but corrupt/incompatible for GIF extraction.
// In that case we must not parse it as frame-based mega animation.
static bool g_test_bin_incompatible = false;
static TickType_t g_missing_anim_log_ticks[ANIMATION_NUM] = {0};

// Initialize GIF fields
#define INIT_ANIM(anim) do { \
    (anim).use_gif = false; \
    (anim).gif_path = NULL; \
    (anim).gif_data = NULL; \
    (anim).gif_data_size = 0; \
    (anim).has_start_gif = false; \
    (anim).gif_start_data = NULL; \
    (anim).gif_start_data_size = 0; \
    (anim).gif_loop_data = NULL; \
    (anim).gif_loop_data_size = 0; \
} while(0)

// Function to get the appropriate animation (SD card only)
Animation_t* get_animation(int index) {
    auto is_anim_ready = [](Animation_t* anim) -> bool {
        if (!anim) {
            return false;
        }
        if (anim->use_gif && anim->gif_data && anim->gif_data_size > 0) {
            return true;
        }
        if (anim->use_spiffs && anim->imges && anim->len > 0) {
            return true;
        }
        return false;
    };

    auto get_any_available_animation = [&]() -> Animation_t* {
        Animation_t* candidates[] = {
            &sd_normal, &sd_embarrass, &sd_fire, &sd_happy, &sd_inspiration,
            &sd_shy, &sd_sleep, &sd_laugh, &sd_sad, &sd_silence,
            &sd_listening, &sd_smirk, &sd_wifi, &sd_battery, &sd_cry
        };
        for (Animation_t* candidate : candidates) {
            if (is_anim_ready(candidate)) {
                return candidate;
            }
        }
        return NULL;
    };

    auto log_missing_animation_throttled = [&](int missing_index, const char* fallback_name) {
        if (missing_index < 0 || missing_index >= ANIMATION_NUM) {
            return;
        }
        TickType_t now = xTaskGetTickCount();
        if ((now - g_missing_anim_log_ticks[missing_index]) >= pdMS_TO_TICKS(10000)) {
            g_missing_anim_log_ticks[missing_index] = now;
            ESP_LOGW("animation", "Animation %s is unavailable, using fallback %s",
                     get_animation_name(missing_index), fallback_name);
        }
    };

    // Check WiFi and battery status for normal animations
    if (index == ANIMATION_NORMAL) {
        // First check WiFi connection. When disconnected, only show WiFi animation
        // in specific interaction states (audio testing / connecting).
        auto& wifi_station = WifiStation::GetInstance();
        if (!wifi_station.IsConnected()) {
            auto& app = Application::GetInstance();
            DeviceState state = app.GetDeviceState();
            bool should_show_wifi_anim =
                (state == kDeviceStateAudioTesting || state == kDeviceStateConnecting);

            if (should_show_wifi_anim) {
                Animation_t* wifi_anim = animation_get_wifi_animation();
                if (wifi_anim != NULL &&
                    (wifi_anim->use_gif || wifi_anim->use_spiffs)) {
                    ESP_LOGI("animation", "WiFi disconnected in state %d, showing wifi animation instead of normal", state);
                    return wifi_anim;
                } else {
                    ESP_LOGW("animation", "WiFi disconnected in state %d, but wifi animation unavailable, using normal", state);
                }
            }
        }
        
        // Then check battery level - if battery < 20%, show battery animation
        int battery_level = 100;
        bool charging = false, discharging = false;
        auto& board = Board::GetInstance();
        if (board.GetBatteryLevel(battery_level, charging, discharging)) {
            if (battery_level < 20) {
                // Battery is below 20%, show battery animation instead of normal
                Animation_t* battery_anim = animation_get_battery_animation();
                if (battery_anim != NULL && 
                    (battery_anim->use_gif || battery_anim->use_spiffs)) {
                    ESP_LOGI("animation", "Battery level %d%% < 20%%, showing battery animation instead of normal", battery_level);
                    return battery_anim;
                } else {
                    ESP_LOGW("animation", "Battery level %d%% < 20%%, but battery animation not available, using normal", battery_level);
                }
            }
        }
    }
    
    Animation_t* requested = NULL;
    switch(index) {
        case ANIMATION_NORMAL:
            requested = animation_get_normal_animation();
            break;
        case ANIMATION_BLUSH:
            requested = animation_get_embarrass_animation();
            break;
        case ANIMATION_ANGRY:
            requested = animation_get_fire_animation();
            break;
        case ANIMATION_STARRY:
            requested = animation_get_inspiration_animation();
            break;
        case ANIMATION_SHY:
            requested = animation_get_shy_animation();
            break;
        case ANIMATION_SLEEP:
            requested = animation_get_sleep_animation();
            break;
        case ANIMATION_HEARTY:
            requested = animation_get_happy_animation();
            break;
        case ANIMATION_LAUGH:
            requested = animation_get_laugh_animation();
            break;
        case ANIMATION_SAD:
            requested = animation_get_sad_animation();
            break;
        case ANIMATION_SILENCE:
            requested = animation_get_silence_animation();
            break;
        case ANIMATION_LISTENING:
            requested = animation_get_listening_animation();
            break;
        case ANIMATION_SMIRK:
            requested = animation_get_smirk_animation();
            break;
        case ANIMATION_WIFI:
            requested = animation_get_wifi_animation();
            break;
        case ANIMATION_BATTERY:
            requested = animation_get_battery_animation();
            break;
        case ANIMATION_CRY:
            requested = animation_get_cry_animation();
            break;
        default:
            requested = animation_get_normal_animation();
            break;
    }

    if (is_anim_ready(requested)) {
        return requested;
    }

    Animation_t* normal_fallback = animation_get_normal_animation();
    if (is_anim_ready(normal_fallback)) {
        log_missing_animation_throttled(index, "NORMAL");
        return normal_fallback;
    }

    Animation_t* any_fallback = get_any_available_animation();
    if (is_anim_ready(any_fallback)) {
        log_missing_animation_throttled(index, "ANY_AVAILABLE");
        return any_fallback;
    }

    return NULL;
}

// Animation array is no longer used - use get_animation() function instead
Animation_t *animations[] = {
    NULL,  // ANIMATION_NORMAL
    NULL,  // ANIMATION_BLUSH
    NULL,  // ANIMATION_ANGRY
    NULL,  // ANIMATION_STARRY
    NULL,  // ANIMATION_SHY
    NULL,  // ANIMATION_SLEEP
    NULL,  // ANIMATION_HEARTY
    NULL,  // ANIMATION_LAUGH
    NULL,  // ANIMATION_SAD
    NULL,  // ANIMATION_SILENCE
    NULL,  // ANIMATION_LISTENING
    NULL,  // ANIMATION_SMIRK
    NULL,  // ANIMATION_WIFI
    NULL,  // ANIMATION_BATTERY
    NULL   // ANIMATION_CRY
};

static int now_animation = ANIMATION_NORMAL;
int pos = 0;
TaskHandle_t animation_task_handle = nullptr;
static bool animation_locked_by_silence = false;  // Lock animation when volume is 0


// Helper function to get animation name string
static const char* get_animation_name(int animation_index) {
        const char* anim_names[] = {
        "NORMAL", "BLUSH", "ANGRY", "STARRY", "SHY",
        "SLEEP", "HEARTY", "LAUGH", "SAD", "SILENCE",
        "LISTENING", "SMIRK", "WIFI", "BATTERY", "CRY"
    };
    
    if (animation_index >= 0 && animation_index < ANIMATION_NUM) {
        return anim_names[animation_index];
    }
    return "UNKNOWN";
}

void plat_animation_task(void *arg)
{
    auto display = Board::GetInstance().GetDisplay();
    int last_animation = -1; // Track last animation to detect changes
    bool in_start_phase = false; // Track if we're in start phase (for start+loop animations)
    bool start_gif_played = false; // Flag to ensure start GIF is only played once per animation
    bool loop_gif_set = false; // Flag to track if loop GIF has been set (prevent multiple calls)
    TickType_t start_phase_start_time = 0; // When we started the start phase
    TickType_t last_log_time = xTaskGetTickCount(); // Track time for periodic logging
    
    while (1)
    {
        ESP_LOGD("plat_animation_task", "now_animation: %d, pos: %d", now_animation, pos);
        
        // Use get_animation() to get the appropriate animation (SD card only)
        Animation_t* current_anim = get_animation(now_animation);
        
        // Check for NULL animation to prevent crashes
        if (current_anim == NULL) {
            // When animations aren't available, reduce task frequency to avoid interfering
            // with wake word detection and other critical audio tasks
            // Only log warning every 10 seconds instead of every 500ms
            static TickType_t last_warning_time = 0;
            TickType_t current_time_check = xTaskGetTickCount();
            if ((current_time_check - last_warning_time) >= pdMS_TO_TICKS(10000)) {
                ESP_LOGW("plat_animation_task", "Animation %d is not available, skipping frame", now_animation);
                last_warning_time = current_time_check;
            }
            // Use longer delay (5 seconds) to reduce CPU usage and avoid interfering with audio tasks
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        // Log current animation every 3 seconds
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_log_time) >= pdMS_TO_TICKS(3000)) {
            const char* anim_name = get_animation_name(now_animation);
            const char* anim_type = "UNKNOWN";
            const char* anim_source = "NONE";
            
            if (current_anim->use_gif && current_anim->gif_data) {
                anim_type = "GIF";
                if (current_anim->gif_path) {
                    anim_source = current_anim->gif_path;
                } else {
                    anim_source = "test.bin";
                }
            } else if (current_anim->use_spiffs) {
                anim_type = "FRAME_BASED";
                anim_source = "SD_CARD";
            }
            
            /*ESP_LOGI("animation", "Currently displaying: %s (index: %d, type: %s, source: %s, frames: %d)", 
                     anim_name, now_animation, anim_type, anim_source, current_anim->len);*/
            
            last_log_time = current_time;
        }
        
        // Handle GIF animations
        if (current_anim->use_gif && current_anim->gif_data) {
            // Check if animation has changed
            bool animation_changed = (last_animation != now_animation);
            
            if (animation_changed) {
                // Animation changed - reset all flags and start fresh
                in_start_phase = false;
                start_gif_played = false;
                loop_gif_set = false;
                start_phase_start_time = 0;
                
                // For animations with start GIF: start with start GIF
                // For animations without start GIF: use main/loop GIF directly
                if (current_anim->has_start_gif && current_anim->gif_start_data && current_anim->gif_loop_data) {
                    // New animation with start+loop: start with start GIF (only once)
                    in_start_phase = true;
                    start_gif_played = true; // Mark that we've played the start GIF
                    loop_gif_set = false; // Loop GIF not set yet
                    start_phase_start_time = xTaskGetTickCount();
                    display->SetEmotionGif(current_anim->gif_start_data, current_anim->gif_start_data_size);
                    ESP_LOGD("plat_animation_task", "Animation changed to %d: Starting with start GIF", now_animation);
                } else {
                    // New animation without start GIF: use main/loop GIF directly
                    in_start_phase = false;
                    start_gif_played = true; // Mark as played (no start GIF to play)
                    loop_gif_set = true; // Main GIF is the loop
                    display->SetEmotionGif(current_anim->gif_data, current_anim->gif_data_size);
                    ESP_LOGD("plat_animation_task", "Animation changed to %d: Using main GIF", now_animation);
                }
                last_animation = now_animation;
            } else {
                // Same animation - preserve current state, only transition from start to loop
                if (current_anim->has_start_gif && current_anim->gif_start_data && current_anim->gif_loop_data) {
                    if (in_start_phase && start_gif_played && !loop_gif_set) {
                        // Still in start phase - check if we should switch to loop (after ~1 second)
                        TickType_t elapsed = xTaskGetTickCount() - start_phase_start_time;
                        if (elapsed >= pdMS_TO_TICKS(1000)) {
                            // Switch to loop phase (only once)
                            display->SetEmotionGif(current_anim->gif_loop_data, current_anim->gif_loop_data_size);
                            in_start_phase = false; // Stay in loop phase
                            loop_gif_set = true; // Mark loop GIF as set
                            ESP_LOGD("plat_animation_task", "Switched to GIF loop animation %d", now_animation);
                        }
                        // else: still playing start GIF, do nothing (LVGL handles the animation)
                    }
                    // else: already in loop phase (loop_gif_set == true), do nothing (LVGL handles the animation)
                }
                // else: animation without start GIF, already displaying main GIF, do nothing
            }
            
            // Check less frequently for GIFs since they animate themselves
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        
        // Handle frame-based animations (existing code)
        // Reset last_animation when switching to frame-based
        if (last_animation != now_animation) {
            pos = 0; // Reset position when animation changes
            last_animation = now_animation;
        }
        
        pos++;
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
    // If animation is locked by silence, only allow silence animation
    if (animation_locked_by_silence && animation != ANIMATION_SILENCE) {
        ESP_LOGI("animation_set_now_animation", "Animation locked by silence, ignoring request for animation %d", animation);
        return;
    }
    
    if (animation_task_handle == nullptr)
    {
        // Increased stack size to 4096 bytes to handle GIF operations
        // Reduced priority from 4 to 2 to ensure wake word detection (priority 3) has higher priority
        // This prevents animation task from interfering with critical audio processing
        xTaskCreatePinnedToCore(plat_animation_task, "plat_animation_task", 4096, nullptr, 2, &animation_task_handle, 0);
    }
    if (animation < 0 || animation >= ANIMATION_NUM)
    {
        ESP_LOGW("animation_set_now_animation", "Invalid animation index: %d, using normal", animation);
        animation = ANIMATION_NORMAL;
    }
    
    ESP_LOGI("animation_set_now_animation", "Set now animation: %d", animation);
    now_animation = animation;
    pos = 0;
}

// Function to check volume and lock/unlock silence animation
void animation_check_volume_and_lock(int volume)
{
    if (volume == 0 && !animation_locked_by_silence) {
        // Volume is 0, lock animation to silence
        animation_locked_by_silence = true;
        ESP_LOGI("animation", "Volume is 0, locking animation to silence");
        animation_set_now_animation(ANIMATION_SILENCE);
    } else if (volume > 0 && animation_locked_by_silence) {
        // Volume is restored, unlock animation
        animation_locked_by_silence = false;
        ESP_LOGI("animation", "Volume restored to %d, unlocking animation and restoring to normal", volume);
        // Immediately restore to normal animation
        animation_set_now_animation(ANIMATION_NORMAL);
    }
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
    g_test_bin_incompatible = false;
    
    // Debug SD card status before attempting to load
    SdCard::DebugStatus();
    
    // Initialize GIF fields for all animations
    INIT_ANIM(sd_normal);
    INIT_ANIM(sd_embarrass);
    INIT_ANIM(sd_fire);
    INIT_ANIM(sd_happy);
    INIT_ANIM(sd_inspiration);
    INIT_ANIM(sd_shy);
    INIT_ANIM(sd_sleep);
    INIT_ANIM(sd_laugh);
    INIT_ANIM(sd_sad);
    INIT_ANIM(sd_cry);
    INIT_ANIM(sd_silence);
    INIT_ANIM(sd_listening);
    INIT_ANIM(sd_smirk);
    INIT_ANIM(sd_wifi);
    INIT_ANIM(sd_battery);
    
    // First, try to load GIFs from test.bin
    ESP_LOGI("animation", "Trying to load GIF animations from test.bin...");
    if (animation_load_gifs_from_test_bin()) {
        ESP_LOGI("animation", "🎉 Successfully loaded GIF animations from test.bin!");
        ESP_LOGI("animation", "   - Using GIF format for animations");
        return; // Success with GIFs!
    }

    if (g_test_bin_incompatible) {
        ESP_LOGE("animation", "test.bin is present but corrupt/incompatible for GIF parser.");
        ESP_LOGE("animation", "Skipping frame-based fallback to avoid parsing GIF archive as LVGL frame file.");
        ESP_LOGE("animation", "Please re-upload a valid test.bin from the marketing tool.");
        return;
    }
    
    ESP_LOGI("animation", "GIFs not found, trying frame-based animations...");
    
    // Try to load ALL animations from SD card mega file (frame-based)
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
    bool shy_loaded = animation_load_shy_from_sd_card();
    bool sleep_loaded = animation_load_sleep_from_sd_card();
    bool laugh_loaded = animation_load_laugh_from_sd_card();
    bool sad_loaded = animation_load_sad_from_sd_card();
    if (normal_loaded || embarrass_loaded || fire_loaded || happy_loaded || inspiration_loaded || shy_loaded || sleep_loaded || laugh_loaded || sad_loaded) {
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
        if (shy_loaded) {
            ESP_LOGI("animation", "   - Shy animation now uses SD card (shy1.bin, shy2.bin)");
            ESP_LOGI("animation", "   - Shy SD card animation has %d frames", sd_shy.len);
        }
        if (sleep_loaded) {
            ESP_LOGI("animation", "   - Sleep animation now uses SD card (sleep1.bin, sleep2.bin, sleep3.bin, sleep4.bin)");
            ESP_LOGI("animation", "   - Sleep SD card animation has %d frames", sd_sleep.len);
        }
        if (laugh_loaded) {
            ESP_LOGI("animation", "   - Laugh animation now uses SD card");
            ESP_LOGI("animation", "   - Laugh SD card animation has %d frames", sd_laugh.len);
        }
        if (sad_loaded) {
            ESP_LOGI("animation", "   - Sad animation now uses SD card");
            ESP_LOGI("animation", "   - Sad SD card animation has %d frames", sd_sad.len);
        }
    } else {
        ESP_LOGW("animation", "⚠️  SD card animations not found");
        ESP_LOGW("animation", "   - To use SD card animations, place test.bin on the SD card");
        ESP_LOGW("animation", "   - Or place individual .bin files on the SD card");
    }
}





void animation_cleanup_sd_card_animation(Animation_t* anim)
{
    if (!anim) return;
    
    // Free GIF data if used
    if (anim->use_gif) {
        if (anim->gif_data) {
            free(anim->gif_data);
            anim->gif_data = NULL;
        }
        if (anim->gif_path) {
            free(anim->gif_path);
            anim->gif_path = NULL;
        }
        // Free start+loop GIF data
        if (anim->gif_start_data) {
            free(anim->gif_start_data);
            anim->gif_start_data = NULL;
        }
        if (anim->gif_loop_data) {
            free(anim->gif_loop_data);
            anim->gif_loop_data = NULL;
        }
        anim->use_gif = false;
        anim->has_start_gif = false;
        anim->gif_data_size = 0;
        anim->gif_start_data_size = 0;
        anim->gif_loop_data_size = 0;
    }
    
    // Free image data and descriptors
    // Note: Pointers may be NULL if already freed/reset by all_sd_card_imgs cleanup
    if (anim->spiffs_imgs) {
        int len = anim->len;
        if (len < 0 || len > 256) {
            ESP_LOGW("animation", "Skipping descriptor cleanup due to suspicious frame count: %d", len);
            len = 0;
        }
        for (int i = 0; i < len; i++) {
            // Only free if pointer is non-NULL (NULL means already handled by all_sd_card_imgs cleanup)
            if (anim->spiffs_imgs[i] != NULL) {
                // Free data if not already freed (pointers are reset to NULL before all_sd_card_imgs cleanup)
                if (anim->spiffs_imgs[i]->data != NULL) {
                    free((void*)anim->spiffs_imgs[i]->data);
                }
                free(anim->spiffs_imgs[i]);
                anim->spiffs_imgs[i] = NULL;
            }
        }
        free(anim->spiffs_imgs);
        anim->spiffs_imgs = NULL;
    }
    
    if (anim->animations) {
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
    // Check if GIF animation is loaded
    if (sd_normal.use_gif && sd_normal.gif_data && sd_normal.gif_data_size > 0) {
        return &sd_normal;
    }
    // Check if SD card frame-based animation is loaded and valid
    if (sd_normal.use_spiffs && sd_normal.imges && sd_normal.len > 0) {
        return &sd_normal;
    }
    // Only log warning every 10 seconds to avoid log spam that interferes with wake word detection
    static TickType_t last_warning_time = 0;
    TickType_t current_time = xTaskGetTickCount();
    if ((current_time - last_warning_time) >= pdMS_TO_TICKS(10000)) {
        ESP_LOGW("animation", "No normal animation available from SD card");
        last_warning_time = current_time;
    }
    return NULL; // This will be handled by the fallback logic in plat_animation_task
}

// Function to get the appropriate embarrass animation (SD card only)
Animation_t* animation_get_embarrass_animation(void)
{
    if (sd_embarrass.use_gif && sd_embarrass.gif_data && sd_embarrass.gif_data_size > 0) {
        return &sd_embarrass;
    }
    if (sd_embarrass.use_spiffs && sd_embarrass.imges && sd_embarrass.len > 0) {
        return &sd_embarrass;
    }
    return NULL;
}

// Function to get the appropriate fire animation (SD card only)
Animation_t* animation_get_fire_animation(void)
{
    if (sd_fire.use_gif && sd_fire.gif_data && sd_fire.gif_data_size > 0) {
        return &sd_fire;
    }
    if (sd_fire.use_spiffs && sd_fire.imges && sd_fire.len > 0) {
        return &sd_fire;
    }
    return NULL;
}

// Function to get the appropriate happy animation (SD card only)
Animation_t* animation_get_happy_animation(void)
{
    if (sd_happy.use_gif && sd_happy.gif_data && sd_happy.gif_data_size > 0) {
        return &sd_happy;
    }
    if (sd_happy.use_spiffs && sd_happy.imges && sd_happy.len > 0) {
        return &sd_happy;
    }
    return NULL;
}

// Function to get the appropriate inspiration animation (SD card only)
Animation_t* animation_get_inspiration_animation(void)
{
    if (sd_inspiration.use_gif && sd_inspiration.gif_data && sd_inspiration.gif_data_size > 0) {
        return &sd_inspiration;
    }
    if (sd_inspiration.use_spiffs && sd_inspiration.imges && sd_inspiration.len > 0) {
        return &sd_inspiration;
    }
    return NULL;
}

// Function to get the appropriate shy animation (SD card only)
Animation_t* animation_get_shy_animation(void)
{
    if (sd_shy.use_gif && sd_shy.gif_data && sd_shy.gif_data_size > 0) {
        return &sd_shy;
    }
    if (sd_shy.use_spiffs && sd_shy.imges && sd_shy.len > 0) {
        return &sd_shy;
    }
    return NULL;
}

// Function to get the appropriate sleep animation (SD card only)
Animation_t* animation_get_sleep_animation(void)
{
    if (sd_sleep.use_gif && sd_sleep.gif_data && sd_sleep.gif_data_size > 0) {
        return &sd_sleep;
    }
    if (sd_sleep.use_spiffs && sd_sleep.imges && sd_sleep.len > 0) {
        return &sd_sleep;
    }
    return NULL;
}

// Function to get the appropriate laugh animation (SD card only)
Animation_t* animation_get_laugh_animation(void)
{
    if (sd_laugh.use_gif && sd_laugh.gif_data && sd_laugh.gif_data_size > 0) {
        return &sd_laugh;
    }
    if (sd_laugh.use_spiffs && sd_laugh.imges && sd_laugh.len > 0) {
        return &sd_laugh;
    }
    return NULL;
}

// Function to get the appropriate sad animation (SD card only)
Animation_t* animation_get_sad_animation(void)
{
    if (sd_sad.use_gif && sd_sad.gif_data && sd_sad.gif_data_size > 0) {
        return &sd_sad;
    }
    if (sd_sad.use_spiffs && sd_sad.imges && sd_sad.len > 0) {
        return &sd_sad;
    }
    return NULL;
}

// Function to get the appropriate cry animation (SD card only)
Animation_t* animation_get_cry_animation(void)
{
    if (sd_cry.use_gif && sd_cry.gif_data && sd_cry.gif_data_size > 0) {
        return &sd_cry;
    }
    if (sd_cry.use_gif && sd_cry.gif_loop_data && sd_cry.gif_loop_data_size > 0) {
        return &sd_cry;
    }
    if (sd_cry.use_spiffs && sd_cry.imges && sd_cry.len > 0) {
        return &sd_cry;
    }
    return NULL;
}

// Function to get the appropriate silence animation (SD card only)
Animation_t* animation_get_silence_animation(void)
{
    if (sd_silence.use_gif && sd_silence.gif_data && sd_silence.gif_data_size > 0) {
        return &sd_silence;
    }
    if (sd_silence.use_spiffs && sd_silence.imges && sd_silence.len > 0) {
        return &sd_silence;
    }
    return NULL;
}

// Function to get the appropriate listening animation (SD card only)
Animation_t* animation_get_listening_animation(void)
{
    if (sd_listening.use_gif && sd_listening.gif_data && sd_listening.gif_data_size > 0) {
        return &sd_listening;
    }
    if (sd_listening.use_spiffs && sd_listening.imges && sd_listening.len > 0) {
        return &sd_listening;
    }
    return NULL;
}

// Function to get the appropriate smirk animation (SD card only)
Animation_t* animation_get_smirk_animation(void)
{
    if (sd_smirk.use_gif && sd_smirk.gif_data && sd_smirk.gif_data_size > 0) {
        return &sd_smirk;
    }
    if (sd_smirk.use_spiffs && sd_smirk.imges && sd_smirk.len > 0) {
        return &sd_smirk;
    }
    return NULL;
}

// Function to get the appropriate battery animation (SD card only)
Animation_t* animation_get_battery_animation(void)
{
    if (sd_battery.use_gif && sd_battery.gif_data && sd_battery.gif_data_size > 0) {
        return &sd_battery;
    }
    if (sd_battery.use_spiffs && sd_battery.imges && sd_battery.len > 0) {
        return &sd_battery;
    }
    return NULL;
}

// Function to get the appropriate wifi animation (SD card only)
Animation_t* animation_get_wifi_animation(void)
{
    if (sd_wifi.use_gif && sd_wifi.gif_data && sd_wifi.gif_data_size > 0) {
        return &sd_wifi;
    }
    if (sd_wifi.use_spiffs && sd_wifi.imges && sd_wifi.len > 0) {
        return &sd_wifi;
    }
    return NULL;
}

void animation_show_current_sources(void)
{
    ESP_LOGI("animation", "=== Current Animation Sources ===");
    
    for (int i = 0; i < ANIMATION_NUM; i++) {
        Animation_t* anim = get_animation(i);
        const char* anim_names[] = {
            "NORMAL", "BLUSH", "ANGRY", "STARRY", "SHY",
            "SLEEP", "HEARTY", "LAUGH", "SAD", "SILENCE",
            "LISTENING", "SMIRK", "WIFI", "BATTERY", "CRY"
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
        
        // Validate frame dimensions
        if (width == 0 || height == 0) {
            ESP_LOGE("animation", "Invalid frame dimensions for frame %d: width=%u, height=%u (must be > 0)", 
                     i, width, height);
            ESP_LOGE("animation", "This may indicate file corruption or misalignment. File position: %ld", ftell(f));
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
        
        // Set up the LVGL image descriptor
        anim->spiffs_imgs[i]->header.magic = LV_IMAGE_HEADER_MAGIC;
        anim->spiffs_imgs[i]->header.cf = (lv_color_format_t)header_data[1];  // color_format
        anim->spiffs_imgs[i]->header.flags = (uint32_t)header_data[2];        // flags
        anim->spiffs_imgs[i]->header.w = width;                               // width
        anim->spiffs_imgs[i]->header.h = height;                              // height
        anim->spiffs_imgs[i]->header.stride = stride;                         // stride
        anim->spiffs_imgs[i]->data_size = data_size;
        
        // Handle zero-sized frames (shouldn't happen with valid dimensions, but be safe)
        if (data_size == 0) {
            ESP_LOGW("animation", "Frame %d has zero data size, skipping allocation and reading", i);
            anim->spiffs_imgs[i]->data = NULL;
            continue;
        }
        
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
    
    // Animation frame counts: Normal(3), Embarrass(3), Fire(4), Happy(4), Inspiration(4), Shy(2), Sleep(4)
    // Note: Laugh, Sad, Talk are GIF-only (no frame counts)
    int animation_frame_counts[] = {3, 3, 4, 4, 2, 4};
    Animation_t* animations[] = {
        &sd_normal, &sd_embarrass, &sd_fire, &sd_inspiration,
        &sd_shy, &sd_sleep
    };
    
    int total_frames = 0;
    for (int i = 0; i < 6; i++) {
        total_frames += animation_frame_counts[i];
    }
    
    ESP_LOGI("animation", "Loading %d total frames from SD card mega file", total_frames);
    
    // Clean up existing animations
    for (int i = 0; i < 6; i++) {
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
    
    for (int anim_idx = 0; anim_idx < 6 && success; anim_idx++) {
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
        // Set len immediately so cleanup knows the true allocated pointer count
        // even if this animation fails part-way through loading.
        anim->len = frame_count;
        // Initialize all pointers to NULL to prevent accessing uninitialized memory
        for (int i = 0; i < frame_count; i++) {
            anim->spiffs_imgs[i] = NULL;
        }
        
        // Load frames for this animation
        for (int frame_idx = 0; frame_idx < frame_count && success; frame_idx++) {
            ESP_LOGD("animation", "Loading frame %d from SD card mega file", current_frame);
            
            // Check current file position before reading
            long file_pos = ftell(f);
            if (file_pos < 0) {
                ESP_LOGE("animation", "Failed to get file position for frame %d", current_frame);
                success = false;
                break;
            }
            ESP_LOGD("animation", "File position before reading frame %d: %ld", current_frame, file_pos);
            
            // Read header (6 uint32_t values)
            uint32_t header_data[6];
            size_t header_read = fread(header_data, sizeof(uint32_t), 6, f);
            if (header_read != 6) {
                ESP_LOGE("animation", "Failed to read header for frame %d: read %zu of 6 uint32_t", current_frame, header_read);
                ESP_LOGE("animation", "File position: %ld, error: %s", ftell(f), feof(f) ? "EOF reached" : strerror(errno));
                success = false;
                break;
            }
            
            // Validate the magic number
            if (header_data[0] != 0x4C56474C) {
                ESP_LOGE("animation", "Invalid image magic for frame %d: 0x%x (expected 0x%x)", current_frame, header_data[0], 0x4C56474C);
                ESP_LOGE("animation", "Header data: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x", 
                         header_data[0], header_data[1], header_data[2], header_data[3], header_data[4], header_data[5]);
                ESP_LOGE("animation", "File position after header read: %ld", ftell(f));
                success = false;
                break;
            }
            
            // Check if this is an overlay frame FIRST (before dimension validation)
            // Overlay frames use width/height fields differently
            uint32_t color_format = header_data[1];
            uint32_t flags = header_data[2];
            bool is_overlay = (color_format == OVERLAY_PIXELS_FORMAT);
            
            // Calculate data size from image dimensions
            uint32_t width = header_data[3];
            uint32_t height = header_data[4];
            uint32_t stride = header_data[5];
            
            // For overlay frames, width=entry_count, height=1, stride=payload_bytes
            // For regular frames, calculate data_size normally
            size_t data_size;
            if (is_overlay) {
                data_size = stride;  // stride stores total payload bytes for overlays
                ESP_LOGI("animation", "Frame %d header: overlay_count=%u, payload_size=%u", 
                         current_frame, width, stride);
            } else {
                data_size = height * stride;
                ESP_LOGI("animation", "Frame %d header: width=%u, height=%u, stride=%u, data_size=%u", 
                         current_frame, width, height, stride, (unsigned int)data_size);
                
                // Validate frame dimensions (only for regular frames)
                if (width == 0 || height == 0) {
                    ESP_LOGE("animation", "Invalid frame dimensions for frame %d: width=%u, height=%u (must be > 0)", 
                             current_frame, width, height);
                    ESP_LOGE("animation", "This may indicate file corruption or misalignment. File position: %ld", ftell(f));
                    success = false;
                    break;
                }
            }
            
            // Set up the LVGL image descriptor
            lv_image_dsc_t* img_dsc = all_sd_card_imgs[current_frame];
            img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
            
            // For overlay frames, width stores entry count, stride stores payload bytes
            if (is_overlay) {
                uint32_t overlay_count = width;  // width field repurposed for entry count
                uint32_t overlay_payload_size = stride;  // stride stores total payload bytes
                
                ESP_LOGI("animation", "Frame %d is overlay frame: %u overlay pixels, base frame index=%u", 
                         current_frame, overlay_count, flags);
                
                // Get base frame index from flags
                uint32_t base_frame_idx = flags;
                if (base_frame_idx >= current_frame) {
                    ESP_LOGE("animation", "Invalid base frame index %u for overlay frame %d", base_frame_idx, current_frame);
                    success = false;
                    break;
                }
                
                // Get base frame (must be RGB565)
                lv_image_dsc_t* base_img = all_sd_card_imgs[base_frame_idx];
                if (!base_img || !base_img->data) {
                    ESP_LOGE("animation", "Base frame %u not available for overlay frame %d", base_frame_idx, current_frame);
                    success = false;
                    break;
                }
                
                if (base_img->header.cf != LV_COLOR_FORMAT_RGB565) {
                    ESP_LOGE("animation", "Base frame %u must be RGB565 format, got %d", base_frame_idx, base_img->header.cf);
                    success = false;
                    break;
                }
                
                // Get base frame dimensions
                uint32_t frame_width = base_img->header.w;
                uint32_t frame_height = base_img->header.h;
                size_t frame_size = frame_width * frame_height * 2;  // RGB565 = 2 bytes per pixel
                
                // Read overlay pixel data (if any)
                if (overlay_payload_size > 0 && overlay_count > 0) {
                    uint8_t* overlay_data = (uint8_t*)malloc(overlay_payload_size);
                    if (!overlay_data) {
                        ESP_LOGE("animation", "Failed to allocate memory for overlay data (%u bytes)", overlay_payload_size);
                        success = false;
                        break;
                    }
                    
                    size_t overlay_read = fread(overlay_data, 1, overlay_payload_size, f);
                    if (overlay_read != overlay_payload_size) {
                        ESP_LOGE("animation", "Failed to read overlay data: read %zu of %u bytes", overlay_read, overlay_payload_size);
                        free(overlay_data);
                        success = false;
                        break;
                    }
                    
                    // Allocate output buffer for RGB565 frame
                    uint8_t* output_data = (uint8_t*)malloc(frame_size);
                    if (!output_data) {
                        ESP_LOGE("animation", "Failed to allocate memory for overlay output (%zu bytes)", frame_size);
                        free(overlay_data);
                        success = false;
                        break;
                    }
                    
                    // Apply overlay to base frame
                    if (!apply_overlay_to_frame(base_img->data, overlay_data, overlay_count, 
                                                frame_width, frame_height, output_data)) {
                        ESP_LOGE("animation", "Failed to apply overlay to base frame");
                        free(overlay_data);
                        free(output_data);
                        success = false;
                        break;
                    }
                    
                    // Set up image descriptor for the composited frame
                    img_dsc->header.cf = LV_COLOR_FORMAT_RGB565;
                    img_dsc->header.flags = 0;
                    img_dsc->header.w = frame_width;
                    img_dsc->header.h = frame_height;
                    img_dsc->header.stride = frame_width * 2;  // RGB565 = 2 bytes per pixel
                    img_dsc->data_size = frame_size;
                    img_dsc->data = output_data;
                    
                    free(overlay_data);
                    ESP_LOGI("animation", "✅ Applied overlay to frame %d (base=%u, %u pixels)", 
                             current_frame, base_frame_idx, overlay_count);
                } else {
                    // Empty overlay (overlay_count=0 or payload_size=0) - reuse base frame
                    ESP_LOGI("animation", "Empty overlay for frame %d (count=%u, payload=%u), reusing base frame %u", 
                             current_frame, overlay_count, overlay_payload_size, base_frame_idx);
                    img_dsc->header.cf = base_img->header.cf;
                    img_dsc->header.flags = 0;
                    img_dsc->header.w = base_img->header.w;
                    img_dsc->header.h = base_img->header.h;
                    img_dsc->header.stride = base_img->header.stride;
                    img_dsc->data_size = base_img->data_size;
                    
                    // Copy base frame data
                    img_dsc->data = (const uint8_t*)malloc(base_img->data_size);
                    if (!img_dsc->data) {
                        ESP_LOGE("animation", "Failed to allocate memory for copied base frame");
                        success = false;
                        break;
                    }
                    memcpy((void*)img_dsc->data, base_img->data, base_img->data_size);
                }
                
                // Assign to animation
                anim->spiffs_imgs[frame_idx] = img_dsc;
                current_frame++;
                continue;
            }
            
            // Regular frame (not overlay)
            img_dsc->header.cf = (lv_color_format_t)color_format;
            img_dsc->header.flags = flags;
            img_dsc->header.w = width;
            img_dsc->header.h = height;
            img_dsc->header.stride = stride;
            img_dsc->data_size = data_size;
            
            // Handle zero-sized frames (shouldn't happen with valid dimensions, but be safe)
            if (data_size == 0) {
                ESP_LOGW("animation", "Frame %d has zero data size, skipping allocation and reading", current_frame);
                img_dsc->data = NULL;
                // Still assign to animation, but frame won't be usable
                anim->spiffs_imgs[frame_idx] = img_dsc;
                current_frame++;
                continue;
            }
            
            // Check file position before reading pixel data
            long pos_before_data = ftell(f);
            ESP_LOGI("animation", "File position before reading frame %d pixel data: %ld", current_frame, pos_before_data);
            
            // Check if we have enough data remaining in file
            fseek(f, 0, SEEK_END);
            long file_end = ftell(f);
            fseek(f, pos_before_data, SEEK_SET);
            long remaining = file_end - pos_before_data;
            
            ESP_LOGI("animation", "File size: %ld, remaining bytes: %ld, need: %u", 
                     file_end, remaining, (unsigned int)data_size);
            
            if (remaining < (long)data_size) {
                ESP_LOGE("animation", "Not enough data in file for frame %d: need %u bytes, have %ld bytes", 
                         current_frame, (unsigned int)data_size, remaining);
                success = false;
                break;
            }
            
            // Allocate memory for pixel data
            img_dsc->data = (const uint8_t*)malloc(data_size);
            if (img_dsc->data == NULL) {
                ESP_LOGE("animation", "Failed to allocate memory for frame %d data (%u bytes)", 
                         current_frame, (unsigned int)data_size);
                success = false;
                break;
            }
            
            // Read pixel data
            size_t pixel_read = fread((void*)img_dsc->data, 1, data_size, f);
            long pos_after_data = ftell(f);
            
            if (pixel_read != data_size) {
                ESP_LOGE("animation", "Failed to read pixel data for frame %d: read %u of %u bytes", 
                         current_frame, (unsigned int)pixel_read, (unsigned int)data_size);
                ESP_LOGE("animation", "File position after read: %ld, EOF: %s", pos_after_data, feof(f) ? "yes" : "no");
                free((void*)img_dsc->data);
                success = false;
                break;
            }
            
            // Assign to animation
            anim->spiffs_imgs[frame_idx] = img_dsc;
            
            ESP_LOGI("animation", "✅ Successfully loaded frame %d: %dx%d, %u bytes, file pos now: %ld (remaining: %ld)", 
                     current_frame, width, height, (unsigned int)data_size, pos_after_data, file_end - pos_after_data);
            
            // Check if we've consumed the entire file
            if (pos_after_data >= file_end) {
                ESP_LOGW("animation", "⚠️  File completely consumed after frame %d (file size: %ld)", 
                         current_frame, file_end);
                if (frame_idx < frame_count - 1) {
                    ESP_LOGE("animation", "File only contains %d frames, but animation %d needs %d frames", 
                             frame_idx + 1, anim_idx, frame_count);
                    success = false;
                    break;
                }
            }
            
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
        
        // Reset animation pointers first to avoid double-free
        // Since anim->spiffs_imgs[i] points to all_sd_card_imgs[j], we must
        // clear these pointers before freeing all_sd_card_imgs
        for (int i = 0; i < 6; i++) {
            if (animations[i] && animations[i]->spiffs_imgs) {
                // Reset pointers to prevent double-free, but keep the array
                // The array itself will be freed by animation_cleanup_sd_card_animation
                int frame_count = animation_frame_counts[i];
                for (int j = 0; j < frame_count; j++) {
                    animations[i]->spiffs_imgs[j] = NULL;
                }
            }
        }
        
        // Clean up on failure
        for (int i = 0; i < total_frames; i++) {
            if (all_sd_card_imgs[i]) {
                if (all_sd_card_imgs[i]->data) free((void*)all_sd_card_imgs[i]->data);
                free(all_sd_card_imgs[i]);
            }

        }
        free(all_sd_card_imgs);
        
        // Clean up partial animations (now safe since pointers are NULL)
        for (int i = 0; i < 6; i++) {
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

bool animation_load_laugh_from_sd_card(void)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    // Clean up existing SD card laugh animation if any
    animation_cleanup_sd_card_animation(&sd_laugh);
    
    // Load laugh animation from SD card (GIF format, no frame files needed)
    // This will be loaded from test.bin as laugh.gif
    ESP_LOGI("animation", "Laugh animation will be loaded from test.bin as laugh.gif");
    return false; // Return false to indicate frame-based loading not available
}

bool animation_load_sad_from_sd_card(void)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    // Clean up existing SD card sad animation if any
    animation_cleanup_sd_card_animation(&sd_sad);
    
    // Load sad animation from SD card (GIF format, no frame files needed)
    // This will be loaded from test.bin as sad.gif
    ESP_LOGI("animation", "Sad animation will be loaded from test.bin as sad.gif");
    return false; // Return false to indicate frame-based loading not available
}

// ============================================================================
// GIF LOADING FUNCTIONS
// ============================================================================

/**
 * Extract a GIF file from test.bin by name
 * @param gif_name Name of the GIF file (e.g., "normal.gif")
 * @param data Output pointer to GIF data (caller must free)
 * @param size Output size of GIF data
 * @return true on success, false on failure
 */
bool animation_extract_gif_from_test_bin(const char* gif_name, uint8_t** data, size_t* size)
{
    // Static flag to track if we've already failed to read the header
    static bool header_read_failed = false;
    
    if (!gif_name || !data || !size) {
        ESP_LOGE("animation", "Invalid parameters for GIF extraction");
        return false;
    }
    
    // If we've already failed to read the header, skip immediately
    if (header_read_failed) {
        return false;
    }
    
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    // Find test.bin file
    FILE* f = NULL;
    char test_bin_path[512];
    
    DIR* dir = opendir("/sdcard");
    if (dir != NULL) {
        struct dirent* entry;
        bool found = false;
        
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                if (strcasecmp(entry->d_name, "test.bin") == 0 || 
                    strcasecmp(entry->d_name, "TEST.BIN") == 0) {
                    snprintf(test_bin_path, sizeof(test_bin_path), "/sdcard/%s", entry->d_name);
                    found = true;
                    break;
                }
            }
        }
        closedir(dir);
        
        if (!found) {
            ESP_LOGE("animation", "test.bin not found on SD card");
            return false;
        }
    } else {
        ESP_LOGE("animation", "Failed to open /sdcard directory");
        return false;
    }
    
    f = fopen(test_bin_path, "rb");
    if (!f) {
        ESP_LOGE("animation", "Failed to open test.bin: %s", test_bin_path);
        g_test_bin_incompatible = true;
        return false;
    }

    // Get actual file size for integrity checks.
    if (fseek(f, 0, SEEK_END) != 0) {
        ESP_LOGE("animation", "Failed to seek test.bin end for size check");
        fclose(f);
        g_test_bin_incompatible = true;
        return false;
    }
    long actual_file_size = ftell(f);
    if (actual_file_size <= 0) {
        ESP_LOGE("animation", "Invalid test.bin size: %ld", actual_file_size);
        fclose(f);
        g_test_bin_incompatible = true;
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        ESP_LOGE("animation", "Failed to seek test.bin back to start");
        fclose(f);
        g_test_bin_incompatible = true;
        return false;
    }
    
    // Read header
    uint32_t file_count, checksum, data_length;
    if (fread(&file_count, sizeof(uint32_t), 1, f) != 1 ||
        fread(&checksum, sizeof(uint32_t), 1, f) != 1 ||
        fread(&data_length, sizeof(uint32_t), 1, f) != 1) {
        ESP_LOGE("animation", "Failed to read test.bin header");
        fclose(f);
        
        // First failure: delete test.bin and set flag to skip future attempts
        ESP_LOGE("animation", "Deleting corrupted test.bin file and skipping animation loading");
        if (unlink(test_bin_path) == 0) {
            ESP_LOGI("animation", "Successfully deleted corrupted test.bin: %s", test_bin_path);
        } else {
            ESP_LOGW("animation", "Failed to delete test.bin: %s (error: %s)", test_bin_path, strerror(errno));
        }
        
        header_read_failed = true;
        g_test_bin_incompatible = true;
        return false;
    }
    
    ESP_LOGI("animation", "test.bin header: %d files, checksum=0x%08X, length=%d", 
             file_count, checksum, data_length);

    uint64_t table_size_u64 = (uint64_t)file_count * 44ULL;
    uint64_t expected_total_size = 12ULL + table_size_u64 + (uint64_t)data_length;
    if (expected_total_size > (uint64_t)actual_file_size) {
        ESP_LOGE("animation", "test.bin truncated/corrupt: expected at least %llu bytes, actual %ld bytes",
                 (unsigned long long)expected_total_size, actual_file_size);
        fclose(f);
        g_test_bin_incompatible = true;
        return false;
    }
    
    // Read file table and find the GIF
    bool found_gif = false;
    uint32_t gif_offset = 0;
    uint32_t gif_size = 0;
    
    for (uint32_t i = 0; i < file_count; i++) {
        char name[33] = {0};
        uint32_t file_size, file_offset;
        uint16_t width, height;
        
        if (fread(name, 32, 1, f) != 1 ||
            fread(&file_size, sizeof(uint32_t), 1, f) != 1 ||
            fread(&file_offset, sizeof(uint32_t), 1, f) != 1 ||
            fread(&width, sizeof(uint16_t), 1, f) != 1 ||
            fread(&height, sizeof(uint16_t), 1, f) != 1) {
            ESP_LOGE("animation", "Failed to read file table entry %d", i);
            fclose(f);
            g_test_bin_incompatible = true;
            return false;
        }
        
        // Remove null padding from name
        size_t name_len = strnlen(name, 32);
        name[name_len] = '\0';
        
        ESP_LOGD("animation", "File %d: %s, size=%d, offset=%d", i, name, file_size, file_offset);
        
        if (strcmp(name, gif_name) == 0) {
            found_gif = true;
            gif_offset = file_offset;
            gif_size = file_size;
            ESP_LOGI("animation", "Found GIF: %s at offset %d, size %d", gif_name, gif_offset, gif_size);
            break;
        }
    }
    
    if (!found_gif) {
        ESP_LOGE("animation", "GIF not found in test.bin: %s", gif_name);
        fclose(f);
        return false;
    }
    
    // Calculate data section start (after header and file table)
    uint32_t table_size = file_count * 44; // 44 bytes per entry
    uint32_t data_start = 12 + table_size; // 12 byte header + table

    // Determine payload start robustly: some files store offset at magic, others at GIF payload.
    uint64_t base_offset = (uint64_t)data_start + (uint64_t)gif_offset;
    uint64_t candidate_no_skip_end = base_offset + (uint64_t)gif_size;
    uint64_t candidate_skip2_end = base_offset + 2ULL + (uint64_t)gif_size;
    uint64_t file_size_u64 = (uint64_t)actual_file_size;
    uint64_t payload_start = 0;

    if (candidate_no_skip_end <= file_size_u64) {
        payload_start = base_offset;
    } else if (candidate_skip2_end <= file_size_u64) {
        payload_start = base_offset + 2ULL;
    } else {
        ESP_LOGE("animation", "GIF entry out of range for %s: offset=%u size=%u file_size=%ld",
                 gif_name, gif_offset, gif_size, actual_file_size);
        fclose(f);
        g_test_bin_incompatible = true;
        return false;
    }

    if (fseek(f, (long)payload_start, SEEK_SET) != 0) {
        ESP_LOGE("animation", "Failed to seek to GIF payload");
        fclose(f);
        g_test_bin_incompatible = true;
        return false;
    }
    
    // Allocate memory for GIF data
    *data = (uint8_t*)malloc(gif_size);
    if (!*data) {
        ESP_LOGE("animation", "Failed to allocate %d bytes for GIF data", gif_size);
        fclose(f);
        return false;
    }
    
    // Read GIF data
    if (fread(*data, 1, gif_size, f) != gif_size) {
        ESP_LOGE("animation", "Failed to read GIF data");
        free(*data);
        *data = NULL;
        fclose(f);
        g_test_bin_incompatible = true;
        return false;
    }
    
    *size = gif_size;
    fclose(f);
    
    ESP_LOGI("animation", "Successfully extracted GIF: %s (%d bytes)", gif_name, gif_size);
    return true;
}

/**
 * Load a GIF animation into an Animation_t structure
 * @param anim Animation structure to populate
 * @param gif_name Name of the GIF (e.g., "normal.gif")
 * @param gif_data GIF file data
 * @param gif_size Size of GIF data
 * @return true on success, false on failure
 */
bool animation_load_gif_animation(Animation_t* anim, const char* gif_name, uint8_t* gif_data, size_t gif_size)
{
    if (!anim || !gif_name || !gif_data || gif_size == 0) {
        ESP_LOGE("animation", "Invalid parameters for GIF animation loading");
        return false;
    }
    
    // Clean up existing animation
    animation_cleanup_sd_card_animation(anim);
    
    // Allocate memory for GIF data (copy it)
    anim->gif_data = (uint8_t*)malloc(gif_size);
    if (!anim->gif_data) {
        ESP_LOGE("animation", "Failed to allocate memory for GIF data");
        return false;
    }
    
    memcpy(anim->gif_data, gif_data, gif_size);
    anim->gif_data_size = gif_size;
    anim->use_gif = true;
    anim->use_spiffs = true; // Mark as loaded from storage
    anim->has_start_gif = false; // Single GIF, no start/loop separation
    
    // Store GIF name for reference
    anim->gif_path = (char*)malloc(strlen(gif_name) + 1);
    if (anim->gif_path) {
        strcpy(anim->gif_path, gif_name);
    }
    
    // For GIFs, we don't use the frame-based system
    anim->len = 1; // Single GIF animation
    anim->imges = NULL;
    anim->animations = NULL;
    anim->spiffs_imgs = NULL;
    
    ESP_LOGI("animation", "Successfully loaded GIF animation: %s (%d bytes)", gif_name, gif_size);
    return true;
}

/**
 * Load a GIF animation with start+loop support
 * @param anim Animation structure to load into
 * @param gif_loop_name Name of the loop GIF (e.g., "happy_loop.gif")
 * @param gif_loop_data Loop GIF file data
 * @param gif_loop_size Size of loop GIF data
 * @param gif_start_name Name of the start GIF (e.g., "happy_start.gif") - can be NULL
 * @param gif_start_data Start GIF file data - can be NULL
 * @param gif_start_size Size of start GIF data - can be 0
 * @return true on success, false on failure
 */
bool animation_load_gif_animation_with_start_loop(Animation_t* anim, 
                                                   const char* gif_loop_name, uint8_t* gif_loop_data, size_t gif_loop_size,
                                                   const char* gif_start_name, uint8_t* gif_start_data, size_t gif_start_size)
{
    if (!anim || !gif_loop_name || !gif_loop_data || gif_loop_size == 0) {
        ESP_LOGE("animation", "Invalid parameters for GIF animation loading (loop required)");
        return false;
    }
    
    // Clean up existing animation
    animation_cleanup_sd_card_animation(anim);
    
    // Allocate memory for loop GIF data (copy it)
    anim->gif_loop_data = (uint8_t*)malloc(gif_loop_size);
    if (!anim->gif_loop_data) {
        ESP_LOGE("animation", "Failed to allocate memory for loop GIF data");
        return false;
    }
    memcpy(anim->gif_loop_data, gif_loop_data, gif_loop_size);
    anim->gif_loop_data_size = gif_loop_size;
    
    // Set loop GIF as the main GIF data for backward compatibility
    anim->gif_data = anim->gif_loop_data;
    anim->gif_data_size = gif_loop_size;
    
    // Load start GIF if provided
    if (gif_start_name && gif_start_data && gif_start_size > 0) {
        anim->gif_start_data = (uint8_t*)malloc(gif_start_size);
        if (!anim->gif_start_data) {
            ESP_LOGE("animation", "Failed to allocate memory for start GIF data");
            free(anim->gif_loop_data);
            anim->gif_loop_data = NULL;
            anim->gif_data = NULL;
            return false;
        }
        memcpy(anim->gif_start_data, gif_start_data, gif_start_size);
        anim->gif_start_data_size = gif_start_size;
        anim->has_start_gif = true;
    } else {
        anim->has_start_gif = false;
    }
    
    anim->use_gif = true;
    anim->use_spiffs = true; // Mark as loaded from storage
    
    // Store GIF name for reference (use loop name)
    anim->gif_path = (char*)malloc(strlen(gif_loop_name) + 1);
    if (anim->gif_path) {
        strcpy(anim->gif_path, gif_loop_name);
    }
    
    // For GIFs, we don't use the frame-based system
    anim->len = 1; // Single GIF animation
    anim->imges = NULL;
    anim->animations = NULL;
    anim->spiffs_imgs = NULL;
    
    if (anim->has_start_gif) {
        ESP_LOGI("animation", "Successfully loaded GIF animation with start+loop: %s + %s (%d + %d bytes)", 
                 gif_start_name, gif_loop_name, gif_start_size, gif_loop_size);
    } else {
        ESP_LOGI("animation", "Successfully loaded GIF animation (loop only): %s (%d bytes)", 
                 gif_loop_name, gif_loop_size);
    }
    return true;
}

/**
 * Load all GIF animations from test.bin
 * @return true if at least one GIF was loaded, false otherwise
 */
bool animation_load_gifs_from_test_bin(void)
{
    if (!SdCard::IsMounted()) {
        ESP_LOGE("animation", "SD card not mounted");
        return false;
    }
    
    ESP_LOGI("animation", "Loading GIF animations from test.bin (20 fixed GIFs)...");

    typedef struct {
        const char* logical_name;      // For logging only
        const char* loop_gif;         // Required .gif inside test.bin
        const char* start_gif;        // Optional *_start.gif (may be NULL)
        Animation_t* target_anim;     // Target animation struct (sd_*)
    } GifAnimDef;

    // Map the 20 GIF assets to our internal Animation_t slots.
    // Multiple enums may later point to the same Animation_t via get_animation().
    const GifAnimDef gif_anims[] = {
        // Main emotional animations
        {"normal",   "normal.gif",      NULL,               &sd_normal},
        {"smirk",    "smirk.gif",       "smirk_start.gif",  &sd_smirk},
        {"heart",    "heart.gif",       "heart_start.gif",  &sd_happy},       // reuse sd_happy for heart
        {"blush",    "blush.gif",       NULL,               &sd_embarrass},   // reuse sd_embarrass for blush
        {"sad",      "sad.gif",         "sad_start.gif",    &sd_sad},
        {"laugh",    "laugh.gif",       "laugh_start.gif",  &sd_laugh},
        {"sleep",    "sleep.gif",       NULL,               &sd_sleep},
        {"starry",   "starry.gif",      "starry_start.gif", &sd_inspiration}, // reuse sd_inspiration for starry
        {"cry",      "cry.gif",         NULL,               &sd_cry},
        {"angry",    "angry.gif",       "angry_start.gif",  &sd_fire},        // reuse sd_fire for angry
        {"silence",  "silence.gif",     NULL,               &sd_silence},

        // Extra / status animations
        {"listening","listening.gif",   NULL,               &sd_listening},
        {"battery",  "battery.gif",     NULL,               &sd_battery},
        {"wifi",     "wifi.gif",        NULL,               &sd_wifi},
    };

    const size_t gif_anim_count = sizeof(gif_anims) / sizeof(gif_anims[0]);
    int loaded_count = 0;

    for (size_t i = 0; i < gif_anim_count; ++i) {
        const GifAnimDef& def = gif_anims[i];

        uint8_t* loop_data = NULL;
        size_t loop_size = 0;
        uint8_t* start_data = NULL;
        size_t start_size = 0;

        // Loop GIF is required
        if (!animation_extract_gif_from_test_bin(def.loop_gif, &loop_data, &loop_size)) {
            ESP_LOGW("animation", "⚠ Loop GIF not found in test.bin for %s: %s",
                     def.logical_name, def.loop_gif);
            continue;
        }

        // Optional start GIF
        if (def.start_gif != NULL) {
            if (!animation_extract_gif_from_test_bin(def.start_gif, &start_data, &start_size)) {
                ESP_LOGW("animation", "⚠ Start GIF not found in test.bin for %s: %s (using loop only)",
                         def.logical_name, def.start_gif);
                start_data = NULL;
                start_size = 0;
            }
        }

        bool ok = false;
        if (start_data != NULL && start_size > 0) {
            ok = animation_load_gif_animation_with_start_loop(
                def.target_anim,
                def.loop_gif, loop_data, loop_size,
                def.start_gif, start_data, start_size
            );
        } else {
            ok = animation_load_gif_animation_with_start_loop(
                def.target_anim,
                def.loop_gif, loop_data, loop_size,
                NULL, NULL, 0
            );
        }

        if (ok) {
            loaded_count++;
            if (start_data != NULL && start_size > 0) {
                ESP_LOGI("animation", "✅ Loaded GIF animation %s: %s (start) + %s (loop)",
                         def.logical_name, def.start_gif, def.loop_gif);
            } else {
                ESP_LOGI("animation", "✅ Loaded GIF animation %s: %s",
                         def.logical_name, def.loop_gif);
            }
        } else {
            ESP_LOGE("animation", "❌ Failed to load GIF animation %s", def.logical_name);
        }

        // Free extracted data (copied into Animation_t)
        if (loop_data) {
            free(loop_data);
        }
        if (start_data) {
            free(start_data);
        }
    }
    
    // Check if test.bin was deleted (header read failure)
    if (loaded_count == 0) {
        if (access("/sdcard/test.bin", F_OK) != 0 && access("/sdcard/TEST.BIN", F_OK) != 0) {
            ESP_LOGE("animation", "test.bin header read failed and file was deleted, skipping all GIF loading");
            return false;
        }
    }
    
    if (loaded_count > 0) {
        ESP_LOGI("animation", "🎉 Successfully loaded %d GIF animation(s) from test.bin", loaded_count);
        return true;
    } else {
        ESP_LOGE("animation", "❌ No GIF animations loaded from test.bin");
        return false;
    }
}
