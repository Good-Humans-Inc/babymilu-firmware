#include "animation.h"
#include "esp_log.h"

static const char* TAG = "test_integration";

void test_animation_integration(void)
{
    ESP_LOGI(TAG, "=== Testing Animation Integration ===");
    
    // Show current animation sources
    animation_show_current_sources();
    
    // Test setting different animations
    ESP_LOGI(TAG, "Testing animation switching...");
    
    // Test static normal (index 0)
    animation_set_now_animation(0);
    ESP_LOGI(TAG, "Set to animation 0 (STATIC_NORMAL)");
    
    // Test other animations
    animation_set_now_animation(4); // ANIMATION_NORMAL
    ESP_LOGI(TAG, "Set to animation 4 (NORMAL)");
    
    // Show which source is being used for each
    for (int i = 0; i < 5; i++) {
        Animation_t* anim = get_animation(i);
        if (anim->use_spiffs) {
            ESP_LOGI(TAG, "Animation %d: Using SPIFFS", i);
        } else {
            ESP_LOGI(TAG, "Animation %d: Using static (img/)", i);
        }
    }
    
    ESP_LOGI(TAG, "=== Integration Test Complete ===");
}
