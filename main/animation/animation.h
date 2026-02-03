#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <cstdint>
#include <cstddef>

typedef struct _Animation_t{
    const lv_image_dsc_t **imges;
    int *animations;
    int len;
    bool use_spiffs;
    lv_image_dsc_t **spiffs_imgs;  // For SD card-loaded images
    // GIF support
    bool use_gif;
    char* gif_path;
    uint8_t* gif_data;
    size_t gif_data_size;
    // Start+loop GIF support
    bool has_start_gif;
    uint8_t* gif_start_data;
    size_t gif_start_data_size;
    uint8_t* gif_loop_data;
    size_t gif_loop_data_size;
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
    ANIMATION_TALK,
    ANIMATION_LAUGH,
    ANIMATION_SAD,
    ANIMATION_SILENCE,
    ANIMATION_LISTENING,
    ANIMATION_SMIRK,
    ANIMATION_NUM
}AnimationType_e;

void animation_set_now_animation(int animation);
void animation_init(void);
void animation_cleanup_sd_card_animation(Animation_t* anim);
Animation_t* animation_get_normal_animation(void);
Animation_t* animation_get_embarrass_animation(void);
Animation_t* animation_get_fire_animation(void);
Animation_t* animation_get_happy_animation(void);
Animation_t* animation_get_inspiration_animation(void);
Animation_t* animation_get_question_animation(void);
Animation_t* animation_get_shy_animation(void);
Animation_t* animation_get_sleep_animation(void);
Animation_t* animation_get_talk_animation(void);
Animation_t* animation_get_laugh_animation(void);
Animation_t* animation_get_sad_animation(void);
Animation_t* animation_get_silence_animation(void);
Animation_t* animation_get_listening_animation(void);
Animation_t* animation_get_smirk_animation(void);
void animation_load_sd_card_animations(void);
void animation_show_current_sources(void);

// SD Card animation loading functions
bool animation_load_from_sd_card(const char* filename, lv_image_dsc_t* img_dsc);
bool animation_create_sd_card_animation(Animation_t* anim, const char* filenames[], int count);
bool animation_create_sd_card_animation_from_merged(Animation_t* anim, const char* merged_filename, int count);
bool animation_load_all_from_sd_card(void);

// Convenient wrapper function for loading mega animation from SD card (test.bin or TEST.BIN)
bool load_mega_animation_from_sd_card(void);
bool animation_load_normal_from_sd_card(void);
bool animation_load_embarrass_from_sd_card(void);
bool animation_load_fire_from_sd_card(void);
bool animation_load_happy_from_sd_card(void);
bool animation_load_inspiration_from_sd_card(void);
bool animation_load_question_from_sd_card(void);
bool animation_load_shy_from_sd_card(void);
bool animation_load_sleep_from_sd_card(void);
bool animation_load_talk_from_sd_card(void);
bool animation_load_gifs_from_test_bin(void);
bool animation_extract_gif_from_test_bin(const char* gif_name, uint8_t** data, size_t* size);
bool animation_load_gif_animation(Animation_t* anim, const char* gif_name, uint8_t* gif_data, size_t gif_size);
bool animation_load_gif_animation_with_start_loop(Animation_t* anim,
                                                  const char* gif_loop_name, uint8_t* gif_loop_data, size_t gif_loop_size,
                                                  const char* gif_start_name, uint8_t* gif_start_data, size_t gif_start_size);