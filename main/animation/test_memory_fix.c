#include "animation.h"
#include "esp_log.h"

static const char* TAG = "test_memory_fix";

void test_memory_fix(void)
{
    ESP_LOGI(TAG, "=== Testing Memory Fix ===");
    
    // Test 1: Initialize SPIFFS (should not crash even if files don't exist)
    ESP_LOGI(TAG, "Test 1: Initializing SPIFFS...");
    animation_init_spiffs();
    
    // Test 2: Try to load animations (should gracefully fail)
    ESP_LOGI(TAG, "Test 2: Loading animations...");
    animation_load_spiffs_animations();
    
    // Test 3: Check animation sources
    ESP_LOGI(TAG, "Test 3: Checking animation sources...");
    animation_show_current_sources();
    
    // Test 4: Test animation switching (should work with static animations)
    ESP_LOGI(TAG, "Test 4: Testing animation switching...");
    animation_set_now_animation(0); // Should use static normal
    animation_set_now_animation(4); // Should use static normal
    
    ESP_LOGI(TAG, "=== Memory Fix Test Complete - No Crashes! ===");
}
