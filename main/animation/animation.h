#pragma once
#include "lvgl.h"
#include <stdbool.h>

typedef struct _Animation_t{
    const lv_image_dsc_t **imges;
    int *animations;
    int len;
    bool use_spiffs;
    lv_image_dsc_t **spiffs_imgs;  // For SPIFFS-loaded images
}Animation_t;


typedef enum _AnimationType_e {
    ANIMATION_STATIC_NORMAL = 0,
    ANIMATION_EMBARRESSED,
    ANIMATION_FIRE,
    ANIMATION_INSPIRATION,
    ANIMATION_NORMAL,
    ANIMATION_QUESTION,
    ANIMATION_SHY,
    ANIMATION_SLEEP,
    ANIMATION_HAPPY,
    ANIMATION_NUM
}AnimationType_e;

void animation_set_now_animation(int animation);
bool animation_load_from_spiffs(const char* filename, lv_image_dsc_t* img_dsc);
void animation_init_spiffs(void);
bool animation_create_spiffs_animation(Animation_t* anim, const char* filenames[], int count);
bool animation_load_normal_from_spiffs(void);
void animation_cleanup_spiffs_animation(Animation_t* anim);
Animation_t* animation_get_normal_animation(void);
void animation_load_spiffs_animations(void);
void animation_show_current_sources(void);
void test_spiffs_debug(void);

// Runtime file management functions
bool animation_write_file_atomic(const char* filename, const uint8_t* data, size_t size);
bool animation_delete_file(const char* filename);
bool animation_file_exists(const char* filename);

// Manifest management functions
bool animation_update_manifest(const char* filename, size_t size, const char* hash);
bool animation_reload_animations_from_manifest(void);
std::string animation_get_manifest_json(void);