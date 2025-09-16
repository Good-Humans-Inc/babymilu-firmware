#include "animation.h"
#include "esp_log.h"

static const char* TAG = "test_spiffs";

void test_spiffs_animations(void)
{
    ESP_LOGI(TAG, "Testing SPIFFS animation loading...");
    
    // Test loading single image
    lv_image_dsc_t test_img = {0};
    if (animation_load_from_spiffs("normal1.bin", &test_img)) {
        ESP_LOGI(TAG, "✅ Successfully loaded normal1.bin from SPIFFS!");
        ESP_LOGI(TAG, "Image size: %dx%d, data size: %d bytes", 
                 test_img.header.w, test_img.header.h, test_img.data_size);
        
        // Clean up
        if (test_img.data) {
            free((void*)test_img.data);
        }
    } else {
        ESP_LOGE(TAG, "❌ Failed to load normal1.bin from SPIFFS");
    }
    
    // Test creating full animation
    Animation_t spiffs_anim = {0};
    const char* frames[] = {"normal1.bin", "normal2.bin", "normal3.bin"};
    
    if (animation_create_spiffs_animation(&spiffs_anim, frames, 3)) {
        ESP_LOGI(TAG, "✅ Successfully created SPIFFS animation with %d frames!", spiffs_anim.len);
        
        // Test using the animation
        ESP_LOGI(TAG, "Animation frames: %d", spiffs_anim.len);
        for (int i = 0; i < spiffs_anim.len; i++) {
            ESP_LOGI(TAG, "Frame %d: %dx%d", i, 
                     spiffs_anim.imges[i]->header.w, 
                     spiffs_anim.imges[i]->header.h);
        }
        
        // Clean up
        if (spiffs_anim.spiffs_imgs) {
            for (int i = 0; i < spiffs_anim.len; i++) {
                if (spiffs_anim.spiffs_imgs[i]->data) {
                    free((void*)spiffs_anim.spiffs_imgs[i]->data);
                }
                free(spiffs_anim.spiffs_imgs[i]);
            }
            free(spiffs_anim.spiffs_imgs);
        }
        if (spiffs_anim.animations) {
            free(spiffs_anim.animations);
        }
    } else {
        ESP_LOGE(TAG, "❌ Failed to create SPIFFS animation");
    }
}
