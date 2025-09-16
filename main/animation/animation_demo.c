#include "animation.h"
#include "esp_log.h"

static const char* TAG = "animation_demo";

void demonstrate_animation_sources(void)
{
    ESP_LOGI(TAG, "=== Animation Source Demonstration ===");
    
    // 1. Show static normal animation (from img directory)
    ESP_LOGI(TAG, "1. Static Normal Animation (from img/ directory):");
    ESP_LOGI(TAG, "   - Uses: &normal1, &normal2, &normal3");
    ESP_LOGI(TAG, "   - Source: Compiled C files in main/animation/img/");
    ESP_LOGI(TAG, "   - Memory: Flash memory (read-only)");
    ESP_LOGI(TAG, "   - Access: animations[0] (static_normal)");
    
    // 2. Try to load SPIFFS normal animation
    ESP_LOGI(TAG, "2. Attempting to load SPIFFS Normal Animation:");
    if (animation_load_normal_from_spiffs()) {
        ESP_LOGI(TAG, "   ✅ SPIFFS normal animation loaded successfully!");
        ESP_LOGI(TAG, "   - Uses: normal1.bin, normal2.bin, normal3.bin");
        ESP_LOGI(TAG, "   - Source: SPIFFS partition (/spiffs/)");
        ESP_LOGI(TAG, "   - Memory: RAM (dynamically allocated)");
        ESP_LOGI(TAG, "   - Access: animation_get_normal_animation()");
    } else {
        ESP_LOGI(TAG, "   ❌ SPIFFS normal animation failed to load");
        ESP_LOGI(TAG, "   - Fallback: Will use static animation");
    }
    
    // 3. Show which animation is currently active
    Animation_t* current_normal = animation_get_normal_animation();
    if (current_normal->use_spiffs) {
        ESP_LOGI(TAG, "3. Current Active: SPIFFS-based normal animation");
        ESP_LOGI(TAG, "   - Frames: %d", current_normal->len);
        ESP_LOGI(TAG, "   - Memory: Dynamic (RAM)");
    } else {
        ESP_LOGI(TAG, "3. Current Active: Static normal animation");
        ESP_LOGI(TAG, "   - Frames: %d", current_normal->len);
        ESP_LOGI(TAG, "   - Memory: Static (Flash)");
    }
    
    // 4. Show how to check animation source
    ESP_LOGI(TAG, "4. How to check animation source in code:");
    ESP_LOGI(TAG, "   if (anim->use_spiffs) {");
    ESP_LOGI(TAG, "       // This is a SPIFFS-loaded animation");
    ESP_LOGI(TAG, "       // Data is in RAM, needs cleanup");
    ESP_LOGI(TAG, "   } else {");
    ESP_LOGI(TAG, "       // This is a static animation");
    ESP_LOGI(TAG, "       // Data is in Flash, no cleanup needed");
    ESP_LOGI(TAG, "   }");
    
    ESP_LOGI(TAG, "=== End Demonstration ===");
}

void test_animation_switching(void)
{
    ESP_LOGI(TAG, "=== Testing Animation Switching ===");
    
    // Test switching between different normal animations
    Animation_t* normal_anim = animation_get_normal_animation();
    
    if (normal_anim->use_spiffs) {
        ESP_LOGI(TAG, "Currently using SPIFFS normal animation");
        
        // You could switch back to static by cleaning up SPIFFS
        animation_cleanup_spiffs_animation(normal_anim);
        ESP_LOGI(TAG, "Switched back to static normal animation");
    } else {
        ESP_LOGI(TAG, "Currently using static normal animation");
        
        // Try to load SPIFFS version
        if (animation_load_normal_from_spiffs()) {
            ESP_LOGI(TAG, "Switched to SPIFFS normal animation");
        }
    }
}
