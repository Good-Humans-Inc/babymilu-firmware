/*
 * Example: How to use SPIFFS-based animations
 * 
 * This file demonstrates how to:
 * 1. Create animation files for SPIFFS
 * 2. Load animations from SPIFFS at runtime
 * 3. Use SPIFFS animations in your application
 */

#include "animation.h"
#include "esp_log.h"

static const char* TAG = "spiffs_example";

// Example: Create a custom animation from SPIFFS files
void create_custom_spiffs_animation(void)
{
    // Example filenames for SPIFFS animation frames
    // These files will be stored in the dedicated 1MB animations partition
    const char* custom_frames[] = {
        "normal1.bin",
        "normal2.bin", 
        "normal3.bin"
    };
    
    // Create a new animation structure
    Animation_t custom_animation = {0};
    
    // Load animation from SPIFFS
    if (animation_create_spiffs_animation(&custom_animation, custom_frames, 3)) {
        ESP_LOGI(TAG, "Custom SPIFFS animation created successfully!");
        
        // Now you can use this animation like any other
        // For example, you could add it to your animations array
        // or use it directly in your animation system
        
        // Clean up when done (if needed)
        // Note: In a real application, you'd manage this lifecycle properly
    } else {
        ESP_LOGE(TAG, "Failed to create custom SPIFFS animation");
    }
}

// Example: Load a single image from SPIFFS
void load_single_spiffs_image(void)
{
    lv_image_dsc_t custom_image = {0};
    
    if (animation_load_from_spiffs("custom_single.bin", &custom_image)) {
        ESP_LOGI(TAG, "Single image loaded from SPIFFS successfully!");
        
        // Use the image with LVGL
        // lv_obj_t* img_obj = lv_image_create(parent);
        // lv_image_set_src(img_obj, &custom_image);
        
        // Clean up when done
        if (custom_image.data) {
            free(custom_image.data);
        }
    } else {
        ESP_LOGE(TAG, "Failed to load single image from SPIFFS");
    }
}

/*
 * How to create SPIFFS animation files:
 * 
 * 1. Convert your existing animation images to binary format:
 *    - Take your existing normal1.c, normal2.c, etc.
 *    - Extract the lv_image_dsc_t structure data
 *    - Write header + data to a .bin file
 * 
 * 2. Example Python script to create SPIFFS files:
 * 
 * import struct
 * 
 * def create_spiffs_animation_file(c_file_path, output_path):
 *     # Read the C file and extract the lv_image_dsc_t data
 *     # This is a simplified example - you'd need to parse the C file
 *     # or extract the data programmatically
 *     
 *     with open(output_path, 'wb') as f:
 *         # Write header (you'd extract this from the C file)
 *         f.write(header_bytes)
 *         # Write pixel data
 *         f.write(pixel_data_bytes)
 * 
 * 3. Upload the .bin files to SPIFFS partition
 * 
 * 4. Use the animation functions to load them at runtime
 */
