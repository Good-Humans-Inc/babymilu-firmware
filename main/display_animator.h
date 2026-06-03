#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>
#include <lvgl.h>

class DisplayAnimator {
public:
    ~DisplayAnimator();

    esp_err_t Init();
    bool IsReady() const { return ready_; }

    bool ShowLoop(const uint8_t* data, size_t size, const char* name);
    bool ShowStartThenLoop(const uint8_t* start_data, size_t start_size, const char* start_name,
                           const uint8_t* loop_data, size_t loop_size, const char* loop_name);
    void SetBacklightBrightness(int percent);
    int AdjustBacklightBrightness(int delta);
    void RestoreBacklightBrightness();
    int backlight_brightness() const { return backlight_brightness_; }
    void ShowStatusCanvas(const char* network_label, int network_percent, int battery_percent,
                          bool charging, uint32_t duration_ms);
    void ShowValueOverlay(const char* label, int percent, uint32_t duration_ms);
    void ClearOverlay();

private:
    static void GifEventHandler(lv_event_t* event);
    static void OverlayTimerCallback(void* arg);
    static uint8_t* CopyGifData(const uint8_t* data, size_t size);

    bool InitPanel();
    bool InitBacklight();
    bool InitLvgl();
    void FreeBuffersLocked();
    void ClearOverlayLocked();
    void ScheduleOverlayClear(uint32_t duration_ms);
    void SetSourceLocked(const char* name);
    void SwitchPendingLoopLocked();

    bool ready_ = false;
    bool spi_ready_ = false;
    bool panel_ready_ = false;
    bool backlight_ready_ = false;
    int normal_backlight_brightness_ = 100;
    int backlight_brightness_ = 100;
    bool awaiting_start_ready_ = false;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    lv_display_t* display_ = nullptr;
    lv_obj_t* gif_ = nullptr;
    lv_obj_t* overlay_container_ = nullptr;
    esp_timer_handle_t overlay_timer_ = nullptr;
    lv_image_dsc_t gif_desc_ = {};
    uint8_t* current_data_ = nullptr;
    size_t current_size_ = 0;
    std::string current_name_;
    uint8_t* pending_loop_data_ = nullptr;
    size_t pending_loop_size_ = 0;
    std::string pending_loop_name_;
};
