#include "lcd_display.h"

#include <vector>
#include <algorithm>
#include <font_awesome_symbols.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <esp_heap_caps.h>
#include "assets/lang_config.h"
#include <cstring>
#include "settings.h"
#include "animation.h"
#include "board.h"

#define TAG "LcdDisplay"

// Color definitions for dark theme
#define DARK_BACKGROUND_COLOR lv_color_hex(0x121212)       // Dark background
#define DARK_TEXT_COLOR lv_color_white()                   // White text
#define DARK_CHAT_BACKGROUND_COLOR lv_color_hex(0x1E1E1E)  // Slightly lighter than background
#define DARK_USER_BUBBLE_COLOR lv_color_hex(0x1A6C37)      // Dark green
#define DARK_ASSISTANT_BUBBLE_COLOR lv_color_hex(0x333333) // Dark gray
#define DARK_SYSTEM_BUBBLE_COLOR lv_color_hex(0x2A2A2A)    // Medium gray
#define DARK_SYSTEM_TEXT_COLOR lv_color_hex(0xAAAAAA)      // Light gray text
#define DARK_BORDER_COLOR lv_color_hex(0x333333)           // Dark gray border
#define DARK_LOW_BATTERY_COLOR lv_color_hex(0xFF0000)      // Red for dark mode

// Color definitions for light theme
#define LIGHT_BACKGROUND_COLOR lv_color_white()            // White background
#define LIGHT_TEXT_COLOR lv_color_black()                  // Black text
#define LIGHT_CHAT_BACKGROUND_COLOR lv_color_hex(0xE0E0E0) // Light gray background
#define LIGHT_USER_BUBBLE_COLOR lv_color_hex(0x95EC69)     // WeChat green
#define LIGHT_ASSISTANT_BUBBLE_COLOR lv_color_white()      // White
#define LIGHT_SYSTEM_BUBBLE_COLOR lv_color_hex(0xE0E0E0)   // Light gray
#define LIGHT_SYSTEM_TEXT_COLOR lv_color_hex(0x666666)     // Dark gray text
#define LIGHT_BORDER_COLOR lv_color_hex(0xE0E0E0)          // Light gray border
#define LIGHT_LOW_BATTERY_COLOR lv_color_black()           // Black for light mode

// Define dark theme colors
const ThemeColors DARK_THEME = {
    .background = DARK_BACKGROUND_COLOR,
    .text = DARK_TEXT_COLOR,
    .chat_background = DARK_CHAT_BACKGROUND_COLOR,
    .user_bubble = DARK_USER_BUBBLE_COLOR,
    .assistant_bubble = DARK_ASSISTANT_BUBBLE_COLOR,
    .system_bubble = DARK_SYSTEM_BUBBLE_COLOR,
    .system_text = DARK_SYSTEM_TEXT_COLOR,
    .border = DARK_BORDER_COLOR,
    .low_battery = DARK_LOW_BATTERY_COLOR};

// Define light theme colors
const ThemeColors LIGHT_THEME = {
    .background = LIGHT_BACKGROUND_COLOR,
    .text = LIGHT_TEXT_COLOR,
    .chat_background = LIGHT_CHAT_BACKGROUND_COLOR,
    .user_bubble = LIGHT_USER_BUBBLE_COLOR,
    .assistant_bubble = LIGHT_ASSISTANT_BUBBLE_COLOR,
    .system_bubble = LIGHT_SYSTEM_BUBBLE_COLOR,
    .system_text = LIGHT_SYSTEM_TEXT_COLOR,
    .border = LIGHT_BORDER_COLOR,
    .low_battery = LIGHT_LOW_BATTERY_COLOR};

LV_FONT_DECLARE(font_awesome_30_4);

LcdDisplay::LcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, DisplayFonts fonts, int width, int height)
    : panel_io_(panel_io), panel_(panel), fonts_(fonts)
{
    width_ = width;
    height_ = height;

    // Load theme from settings
    Settings settings("display", false);
    current_theme_name_ = settings.GetString("theme", "light");

    // Update the theme
    if (current_theme_name_ == "dark")
    {
        current_theme_ = DARK_THEME;
    }
    else if (current_theme_name_ == "light")
    {
        current_theme_ = LIGHT_THEME;
    }
}

SpiLcdDisplay::SpiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                             int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy,
                             DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts, width, height)
{

    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++)
    {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr)
    {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0)
    {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    SetupUI();
}

// RGB LCD实现
RgbLcdDisplay::RgbLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                             int width, int height, int offset_x, int offset_y,
                             bool mirror_x, bool mirror_y, bool swap_xy,
                             DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts, width, height)
{

    // draw white
    std::vector<uint16_t> buffer(width_, 0xFFFF);
    for (int y = 0; y < height_; y++)
    {
        esp_lcd_panel_draw_bitmap(panel_, 0, y, width_, y + 1, buffer.data());
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .buffer_size = static_cast<uint32_t>(width_ * 20),
        .double_buffer = true,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = 1,
            .swap_bytes = 0,
            .full_refresh = 1,
            .direct_mode = 1,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        }};

    display_ = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    if (display_ == nullptr)
    {
        ESP_LOGE(TAG, "Failed to add RGB display");
        return;
    }

    if (offset_x != 0 || offset_y != 0)
    {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    SetupUI();
}

MipiLcdDisplay::MipiLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, int offset_x, int offset_y,
                               bool mirror_x, bool mirror_y, bool swap_xy,
                               DisplayFonts fonts)
    : LcdDisplay(panel_io, panel, fonts, width, height)
{

    // Set the display to on
    ESP_LOGI(TAG, "Turning display on");
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    ESP_LOGI(TAG, "Initialize LVGL port");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding LCD display");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * 50),
        .double_buffer = false,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = swap_xy,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
        },
    };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {
        .flags = {
            .avoid_tearing = false,
        }};
    display_ = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
    if (display_ == nullptr)
    {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0)
    {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    SetupUI();
}

LcdDisplay::~LcdDisplay()
{
    // 然后再清理 LVGL 对象
    if (content_ != nullptr)
    {
        lv_obj_del(content_);
    }
    // Status bar commented out for full image scaling
    /*
    if (status_bar_ != nullptr)
    {
        lv_obj_del(status_bar_);
    }
    */
    if (side_bar_ != nullptr)
    {
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr)
    {
        lv_obj_del(container_);
    }
    if (display_ != nullptr)
    {
        lv_display_delete(display_);
    }

    if (panel_ != nullptr)
    {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr)
    {
        esp_lcd_panel_io_del(panel_io_);
    }
}

bool LcdDisplay::Lock(int timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void LcdDisplay::Unlock()
{
    lvgl_port_unlock();
}

#if CONFIG_USE_WECHAT_MESSAGE_STYLE
void LcdDisplay::SetupUI()
{
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, fonts_.text_font, 0);
    lv_obj_set_style_text_color(screen, current_theme_.text, 0);
    lv_obj_set_style_bg_color(screen, current_theme_.background, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, current_theme_.background, 0);
    lv_obj_set_style_border_color(container_, current_theme_.border, 0);

    /* Status bar - COMMENTED OUT FOR FULL IMAGE SCALING */
    /*
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
    lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);
    */

    /* Content - Chat area */
    content_ = lv_obj_create(container_);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, 0, 0);  // REMOVE ALL PADDING FOR FULL SCREEN
    lv_obj_set_style_bg_color(content_, current_theme_.chat_background, 0); // Background for chat area
    lv_obj_set_style_border_color(content_, current_theme_.border, 0);      // Border color for chat area

    // Enable scrolling for chat content
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(content_, LV_DIR_VER);

    // Create a flex container for chat messages
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); // CENTER ALIGNMENT FOR FULL SCREEN
    lv_obj_set_style_pad_row(content_, 0, 0); // Remove space between messages for full screen

    // We'll create chat messages dynamically in SetChatMessage
    chat_message_label_ = nullptr;

    /* Status bar elements - COMMENTED OUT FOR FULL IMAGE SCALING */
    /*
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 10, 0);
    lv_obj_set_style_pad_right(status_bar_, 10, 0);
    lv_obj_set_style_pad_top(status_bar_, 2, 0);
    lv_obj_set_style_pad_bottom(status_bar_, 2, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    // 设置状态栏的内容垂直居中
    lv_obj_set_flex_align(status_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 创建emotion_label_在状态栏最左侧
    emotion_label_ = lv_label_create(status_bar_);
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_4, 0);
    lv_obj_set_style_text_color(emotion_label_, current_theme_.text, 0);
    lv_label_set_text(emotion_label_, FONT_AWESOME_AI_CHIP);
    lv_obj_set_style_margin_right(emotion_label_, 5, 0); // 添加右边距，与后面的元素分隔

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, current_theme_.text, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, current_theme_.text, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, current_theme_.text, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(network_label_, current_theme_.text, 0);
    lv_obj_set_style_margin_left(network_label_, 5, 0); // 添加左边距，与前面的元素分隔

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, current_theme_.text, 0);
    lv_obj_set_style_margin_left(battery_label_, 5, 0); // 添加左边距，与前面的元素分隔
    */

    // Low battery popup - COMMENTED OUT FOR FULL SCREEN DISPLAY
    /*
    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, fonts_.text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, current_theme_.low_battery, 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
    */
}
#if CONFIG_IDF_TARGET_ESP32P4
#define MAX_MESSAGES 40
#else
#define MAX_MESSAGES 20
#endif

#else
void LcdDisplay::SetupUI()
{
    DisplayLockGuard lock(this);

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, fonts_.text_font, 0);
    lv_obj_set_style_text_color(screen, current_theme_.text, 0);
    lv_obj_set_style_bg_color(screen, current_theme_.background, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);
    lv_obj_set_style_bg_color(container_, current_theme_.background, 0);
    lv_obj_set_style_border_color(container_, current_theme_.border, 0);

    /* Status bar - COMMENTED OUT FOR FULL IMAGE SCALING */
    /*
    status_bar_ = lv_obj_create(container_);
    lv_obj_set_size(status_bar_, LV_HOR_RES, fonts_.text_font->line_height);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
    lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);
    */

    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_style_pad_all(content_, 0, 0);  // Remove padding to maximize space
    lv_obj_set_style_bg_color(content_, current_theme_.chat_background, 0);
    lv_obj_set_style_border_color(content_, current_theme_.border, 0); // Border color for content

    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_COLUMN);                                                     // 垂直布局（从上到下）
    lv_obj_set_flex_align(content_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); // 子对象居中对齐，优先显示emotion

    // UI elements commented out for full screen display
    /*
    emotion_label_ = lv_img_create(content_);

    preview_image_ = lv_image_create(content_);
    lv_obj_set_size(preview_image_, width_ * 0.5, height_ * 0.5);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);

    chat_message_label_ = lv_label_create(content_);
    lv_label_set_text(chat_message_label_, "");
    lv_obj_set_width(chat_message_label_, LV_HOR_RES * 0.9);                   // 限制宽度为屏幕宽度的 90%
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);           // 设置为自动换行模式
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_CENTER, 0); // 设置文本居中对齐
    lv_obj_set_style_text_color(chat_message_label_, current_theme_.text, 0);
    */

    /* Status bar elements - COMMENTED OUT FOR FULL IMAGE SCALING */
    /*
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);
    lv_obj_set_style_pad_left(status_bar_, 2, 0);
    lv_obj_set_style_pad_right(status_bar_, 2, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(network_label_, current_theme_.text, 0);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(notification_label_, current_theme_.text, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(status_label_, current_theme_.text, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(mute_label_, current_theme_.text, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, fonts_.icon_font, 0);
    lv_obj_set_style_text_color(battery_label_, current_theme_.text, 0);
    */

    // Low battery popup - COMMENTED OUT FOR FULL SCREEN DISPLAY
    /*
    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, fonts_.text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, current_theme_.low_battery, 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
    */
}

void LcdDisplay::SetPreviewImage(const lv_img_dsc_t *img_dsc)
{
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr)
    {
        return;
    }

    if (img_dsc != nullptr)
    {
        // Safety checks to prevent division by zero
        lv_coord_t img_width = img_dsc->header.w;
        if (img_width <= 0) {
            ESP_LOGE(TAG, "Invalid image width in simple SetPreviewImage: %d", img_width);
            return;
        }
        
        // Safe division with overflow protection
        lv_coord_t scale = 0;
        if (img_width > 0 && width_ > 0) {
            int64_t scale_64 = ((int64_t)128 * width_) / img_width;
            if (scale_64 > INT32_MAX) {
                scale = INT32_MAX;
            } else if (scale_64 < INT32_MIN) {
                scale = INT32_MIN;
            } else {
                scale = (lv_coord_t)scale_64;
            }
        } else {
            ESP_LOGE(TAG, "Division by zero prevented in simple SetPreviewImage: img_width=%d, width_=%d", img_width, width_);
            return;
        }
        
        // Ensure scale is valid before setting
        if (scale <= 0) {
            ESP_LOGW(TAG, "Invalid scale value in simple SetPreviewImage: %d, using default", scale);
            scale = 128;  // Default scale
        }
        
        // zoom factor 0.5
        lv_image_set_scale(preview_image_, scale);
        // 设置图片源并显示预览图片
        lv_image_set_src(preview_image_, img_dsc);
        lv_obj_clear_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        // 隐藏emotion_label_ - Status bar is disabled, skip
        /*
        if (emotion_label_ != nullptr)
        {
            lv_obj_add_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
        }
        */
    }
    else
    {
        // 隐藏预览图片并显示emotion_label_ - Status bar is disabled, skip
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        /*
        if (emotion_label_ != nullptr)
        {
            lv_obj_clear_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
        }
        */
    }
}
#endif

void LcdDisplay::SetEmotion(const char *emotion)
{
    struct Emotion
    {
        const AnimationType_e animation_num;
        const char *text;
    };

    static const std::vector<Emotion> emotions = {
        {ANIMATION_STATIC_NORMAL, "neutral"},
        {ANIMATION_HAPPY, "happy"},
        {ANIMATION_HAPPY, "laughing"},
        {ANIMATION_NORMAL, "funny"},
        {ANIMATION_SHY, "sad"},
        {ANIMATION_FIRE, "angry"},
        {ANIMATION_SHY, "crying"},
        {ANIMATION_INSPIRATION, "loving"},
        {ANIMATION_EMBARRESSED, "embarrassed"},
        {ANIMATION_HAPPY, "surprised"},
        {ANIMATION_INSPIRATION, "shocked"},
        {ANIMATION_QUESTION, "thinking"},
        {ANIMATION_NORMAL, "winking"},
        {ANIMATION_INSPIRATION, "cool"},
        {ANIMATION_SLEEP, "relaxed"},
        {ANIMATION_HAPPY, "delicious"},
        {ANIMATION_INSPIRATION, "kissy"},
        {ANIMATION_HAPPY, "confident"},
        {ANIMATION_SLEEP, "sleepy"},
        {ANIMATION_HAPPY, "silly"},
        {ANIMATION_QUESTION, "confused"}};

    // 查找匹配的表情
    std::string_view emotion_view(emotion);
    auto it = std::find_if(emotions.begin(), emotions.end(),
                           [&emotion_view](const Emotion &e)
                           { return e.text == emotion_view; });

    DisplayLockGuard lock(this);
    
    // Create emotion_label_ in content area if it doesn't exist (status bar disabled)
    if (emotion_label_ == nullptr && content_ != nullptr) {
        emotion_label_ = lv_img_create(content_);
        lv_obj_align(emotion_label_, LV_ALIGN_CENTER, 0, 0);
    }
    
    if (emotion_label_ == nullptr)
    {
        return; // Still can't create, skip emotion setting
    }

    // 如果找到匹配的表情就显示对应图标，否则显示默认的neutral表情
    if (it != emotions.end())
    {
        animation_set_now_animation(it->animation_num);
    }
    else
    {
        animation_set_now_animation(ANIMATION_NORMAL);
    }

#if !CONFIG_USE_WECHAT_MESSAGE_STYLE
    // 显示emotion_label_，隐藏preview_image_ - Status bar is disabled, skip
    /*
    lv_obj_clear_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
    if (preview_image_ != nullptr)
    {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
    */
#endif
}

// Static buffer for composed image (reused for overlay frames)
static lv_image_dsc_t* composed_img_dsc = nullptr;
static uint8_t* composed_img_data = nullptr;

// Helper function to create composed image with sparse overlay pixels
// Only applies to normal2/normal3 (frame_index 1 and 2), embarrass2/embarrass3 (frame_index 1 and 2),
// fire2-4/happy2-4/inspiration2-4 (frame_index 1, 2, and 3)
static lv_image_dsc_t* compose_image_with_overlay(const lv_image_dsc_t* base_img, int frame_index, bool rotated_180) {
    if (base_img == nullptr || base_img->data == nullptr) {
        ESP_LOGE(TAG, "Invalid base image for composition");
        return nullptr;
    }
    
    // Validate base image dimensions
    if (base_img->header.w <= 0 || base_img->header.h <= 0 || 
        base_img->header.w > 10000 || base_img->header.h > 10000) {
        ESP_LOGE(TAG, "Invalid base image dimensions: %dx%d", base_img->header.w, base_img->header.h);
        return nullptr;
    }
    
    // Only compose for overlay frames (frame_index >= 1, depends on animation type)
    // Normal animation supports frame_index 1-2 (normal2-normal3, 2 overlays)
    // Other animations support frame_index 1-3
    if (frame_index < 1) {
        return nullptr; // No composition needed
    }
    
    // Get current animation type to determine which overlay to use
    int current_animation = animation_get_now_animation();
    bool is_normal_animation = (current_animation == ANIMATION_NORMAL || current_animation == ANIMATION_STATIC_NORMAL);
    bool is_embarrass_animation = (current_animation == ANIMATION_EMBARRESSED);
    bool is_fire_animation = (current_animation == ANIMATION_FIRE);
    bool is_happy_animation = (current_animation == ANIMATION_HAPPY);
    bool is_inspiration_animation = (current_animation == ANIMATION_INSPIRATION);
    bool is_question_animation = (current_animation == ANIMATION_QUESTION);
    bool is_shy_animation = (current_animation == ANIMATION_SHY);
    bool is_sleep_animation = (current_animation == ANIMATION_SLEEP);
    
    // Only apply overlays for supported animations
    if (!is_normal_animation && !is_embarrass_animation && !is_fire_animation && 
        !is_happy_animation && !is_inspiration_animation && !is_question_animation &&
        !is_shy_animation && !is_sleep_animation) {
        return nullptr; // No overlay for this animation type
    }
    
    // Normal animation supports frame_index 1-2 (normal2-normal3)
    // Embarrass animation supports frame_index 1-2
    if (is_normal_animation && frame_index > 2) {
        return nullptr; // No overlay for this frame
    }
    if (is_embarrass_animation && frame_index > 2) {
        return nullptr; // No overlay for this frame
    }
    
    // Shy only supports frame_index 1
    if (is_shy_animation && frame_index > 1) {
        return nullptr; // No overlay for this frame
    }
    
    lv_coord_t img_width = base_img->header.w;
    lv_coord_t img_height = base_img->header.h;
    
    if (img_width <= 0 || img_height <= 0) {
        ESP_LOGE(TAG, "Invalid image dimensions: %dx%d", img_width, img_height);
        return nullptr;
    }
    
    // Calculate buffer size for RGB565 (2 bytes per pixel)
    size_t data_size = img_width * img_height * 2;
    
    // Free previous composed image if it exists
    if (composed_img_data != nullptr) {
        heap_caps_free(composed_img_data);
        composed_img_data = nullptr;
    }
    if (composed_img_dsc != nullptr) {
        heap_caps_free(composed_img_dsc);
        composed_img_dsc = nullptr;
    }
    
    // Allocate memory for composed image
    composed_img_data = (uint8_t*)heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (composed_img_data == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate memory for composed image data");
        return nullptr;
    }
    
    composed_img_dsc = (lv_image_dsc_t*)heap_caps_malloc(sizeof(lv_image_dsc_t), MALLOC_CAP_8BIT);
    if (composed_img_dsc == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate memory for composed image descriptor");
        heap_caps_free(composed_img_data);
        composed_img_data = nullptr;
        return nullptr;
    }
    
    // Copy base image data
    memcpy(composed_img_data, base_img->data, data_size);
    
    // Get the appropriate overlay based on animation type
    const animation_overlay_frame_t* runtime_overlay = nullptr;
    if (is_normal_animation) {
        runtime_overlay = animation_get_normal_overlay_frame(frame_index);
    } else if (is_embarrass_animation) {
        runtime_overlay = animation_get_embarrass_overlay_frame(frame_index);
    } else if (is_fire_animation) {
        runtime_overlay = animation_get_fire_overlay_frame(frame_index);
    } else if (is_happy_animation) {
        runtime_overlay = animation_get_happy_overlay_frame(frame_index);
    } else if (is_inspiration_animation) {
        runtime_overlay = animation_get_inspiration_overlay_frame(frame_index);
    } else if (is_question_animation) {
        runtime_overlay = animation_get_question_overlay_frame(frame_index);
    } else if (is_shy_animation) {
        runtime_overlay = animation_get_shy_overlay_frame(frame_index);
    } else if (is_sleep_animation) {
        runtime_overlay = animation_get_sleep_overlay_frame(frame_index);
    }
    
    const animation_overlay_pixel_t* overlay_list = nullptr;
    size_t overlay_count = 0;
    
    if (runtime_overlay != nullptr && runtime_overlay->pixels != nullptr && runtime_overlay->count > 0) {
        overlay_list = runtime_overlay->pixels;
        overlay_count = runtime_overlay->count;
        ESP_LOGD(TAG, "Using %zu runtime overlay pixels for animation %d frame %d", overlay_count, current_animation, frame_index);
    } else {
        ESP_LOGD(TAG, "No overlay available for animation %d frame %d", current_animation, frame_index);
        return nullptr; // No overlay available for this animation
    }
    
    // Apply sparse overlay pixels within image bounds
    for (size_t i = 0; i < overlay_count; i++) {
        uint16_t x = overlay_list[i].x;
        uint16_t y = overlay_list[i].y;
        uint16_t color = overlay_list[i].color;
        
        // Check bounds
        if (x >= (uint16_t)img_width || y >= (uint16_t)img_height) {
            continue; // Skip pixels outside image bounds
        }
        
        // Calculate pixel offset (RGB565: 2 bytes per pixel)
        int offset = (y * img_width + x) * 2;
        if (offset + 1 < (int)data_size) {
            // Write pixel in little-endian format (low byte, then high byte)
            composed_img_data[offset] = color & 0xFF;
            composed_img_data[offset + 1] = (color >> 8) & 0xFF;
        }
    }
    
    // Set up composed image descriptor
    composed_img_dsc->header = base_img->header;
    composed_img_dsc->data_size = data_size;
    composed_img_dsc->data = composed_img_data;
    
    ESP_LOGD(TAG, "Composed image with sparse overlay pixels for frame %d (%zu pixels)", frame_index, overlay_count);
    return composed_img_dsc;
}

void LcdDisplay::SetEmotionImg(const lv_image_dsc_t *img, int frame_index)
{
    DisplayLockGuard lock(this);
    
    // Create emotion_label_ in content area if it doesn't exist (status bar disabled)
    if (emotion_label_ == nullptr && content_ != nullptr) {
        emotion_label_ = lv_img_create(content_);
        lv_obj_align(emotion_label_, LV_ALIGN_CENTER, 0, 0);
        // Remove any padding/margins for full screen display
        lv_obj_set_style_pad_all(emotion_label_, 0, 0);
        lv_obj_set_style_margin_all(emotion_label_, 0, 0);
        lv_obj_set_style_border_width(emotion_label_, 0, 0);
    }
    
    if (emotion_label_ == nullptr) {
        return; // Still can't create, skip emotion image setting
    }
    
    // Additional safety check for image data validity
    if (img != nullptr && img->data == nullptr) {
        ESP_LOGE(TAG, "Image data is null");
        return;
    }
    
    // For overlay frames, compose with sparse overlay pixels
    // Base frame (frame_index 0) remains unchanged
    // Normal animation: frame_index 1-2 (normal2-normal3, 2 overlays)
    // Other animations: frame_index 1-3
    // Overlays are always rotated 180° to match the globally rotated emotion1 base images
    const lv_image_dsc_t* img_to_display = img;
    
    // Additional validation: check if image is valid before processing
    if (img == nullptr || img->data == nullptr) {
        ESP_LOGE(TAG, "Invalid image passed to SetEmotionImg");
        return;
    }
    
    // Validate image dimensions to prevent crashes
    if (img->header.w <= 0 || img->header.h <= 0 || img->header.w > 10000 || img->header.h > 10000) {
        ESP_LOGE(TAG, "Invalid image dimensions: %dx%d - skipping display", img->header.w, img->header.h);
        return;
    }
    
    int current_animation = animation_get_now_animation();
    bool is_normal_animation = (current_animation == ANIMATION_NORMAL || current_animation == ANIMATION_STATIC_NORMAL);
    
    if (frame_index >= 1) {
        // For normal animation, allow up to frame_index 2 (normal2-normal3, 2 overlays)
        // For other animations, allow up to frame_index 3
        if ((is_normal_animation && frame_index <= 2) || (!is_normal_animation && frame_index <= 3)) {
            lv_image_dsc_t* composed = compose_image_with_overlay(img, frame_index, true);
            if (composed != nullptr) {
                img_to_display = composed;
            }
        }
    }
    
    lv_img_set_src(emotion_label_, img_to_display);
    
    // Scale animation to fit display better - 128x128 -> 412x412 FULL SCREEN SCALING
    if (img != nullptr) {
        // Safety checks to prevent division by zero
        lv_coord_t img_width = img->header.w;
        lv_coord_t img_height = img->header.h;
        
        // Prevent division by zero and invalid image dimensions
        if (img_width <= 0 || img_height <= 0) {
            ESP_LOGE(TAG, "Invalid image dimensions: %dx%d", img_width, img_height);
            return;
        }
        
        // Additional safety checks for reasonable image dimensions
        if (img_width > 2048 || img_height > 2048) {
            ESP_LOGE(TAG, "Image dimensions too large: %dx%d", img_width, img_height);
            return;
        }
        
        // For 128x128 image to become 408x408 SPD2010 ALIGNED: scale = 408/128 = 3.1875
        // In LVGL scale units: 3.1875 * 256 = 816
        // 408 is perfectly divisible by 4 (SPD2010 requirement): 408 ÷ 4 = 102
        lv_coord_t target_width = 408;   // 4-pixel aligned for SPD2010 driver
        lv_coord_t target_height = 408;  // 4-pixel aligned for SPD2010 driver
        
        // Additional safety check for target dimensions
        if (target_width <= 0 || target_height <= 0) {
            ESP_LOGE(TAG, "Invalid target dimensions: %dx%d", target_width, target_height);
            return;
        }
        
        // Calculate scale factors for both dimensions with additional safety
        lv_coord_t scale_w = 0;
        lv_coord_t scale_h = 0;
        
        // Safe division with overflow protection
        if (img_width > 0 && target_width > 0) {
            // Use 64-bit arithmetic to prevent overflow
            int64_t scale_w_64 = ((int64_t)target_width * 256) / img_width;
            if (scale_w_64 > INT32_MAX) {
                scale_w = INT32_MAX;
            } else if (scale_w_64 < INT32_MIN) {
                scale_w = INT32_MIN;
            } else {
                scale_w = (lv_coord_t)scale_w_64;
            }
        } else {
            ESP_LOGE(TAG, "Division by zero prevented: img_width=%d, target_width=%d", img_width, target_width);
            return;
        }
        
        if (img_height > 0 && target_height > 0) {
            // Use 64-bit arithmetic to prevent overflow
            int64_t scale_h_64 = ((int64_t)target_height * 256) / img_height;
            if (scale_h_64 > INT32_MAX) {
                scale_h = INT32_MAX;
            } else if (scale_h_64 < INT32_MIN) {
                scale_h = INT32_MIN;
            } else {
                scale_h = (lv_coord_t)scale_h_64;
            }
        } else {
            ESP_LOGE(TAG, "Division by zero prevented: img_height=%d, target_height=%d", img_height, target_height);
            return;
        }
        
        // Use the smaller scale to ensure image fits within screen bounds
        lv_coord_t scale = (scale_w < scale_h) ? scale_w : scale_h;
        
        // Debug logging - COMMENTED OUT
        // ESP_LOGI(TAG, "SPD2010 ALIGNED TEST 408x408: %dx%d -> %dx%d, scale_w=%d, scale_h=%d, final_scale=%d", 
        //          img_width, img_height, target_width, target_height, scale_w, scale_h, scale);
        // ESP_LOGI(TAG, "SCREEN SIZE: %dx%d, SCALED IMAGE: %dx%d, MARGIN: %dx%d pixels", 
        //          LV_HOR_RES, LV_VER_RES, target_width, target_height, 
        //          LV_HOR_RES - target_width, LV_VER_RES - target_height);
        // ESP_LOGI(TAG, "SPD2010 ALIGNMENT: target_width%%4=%d, target_height%%4=%d", 
        //          target_width % 4, target_height % 4);
        
        // Ensure scale is within safe bounds for full screen
        if (scale > 1024) scale = 1024;  // Max 400% scale for full screen
        if (scale < 64) scale = 64;       // Min 25% scale
        if (scale <= 0) scale = 256;      // Fallback to 100% if calculation failed
        
        // ESP_LOGI(TAG, "FINAL SPD2010 ALIGNED SCALE 408x408: %d (%.2fx) - Image has %d pixel margin on each side", 
        //          scale, (float)scale / 256.0f, (LV_HOR_RES - target_width) / 2);
        
        // Use older LVGL API methods for img objects with additional safety
        if (scale > 0 && scale <= 1024) {  // Ensure scale is within valid LVGL range
            lv_img_set_zoom(emotion_label_, scale);
            lv_img_set_antialias(emotion_label_, true);  // Enable anti-aliasing for better quality
        } else {
            ESP_LOGW(TAG, "Scale value out of range, using default: %d", scale);
            lv_img_set_zoom(emotion_label_, 256);  // Default 100% scale
        }
    }
}

void LcdDisplay::SetIcon(const char *icon)
{
    DisplayLockGuard lock(this);
    
    // Create emotion_label_ in content area if it doesn't exist (status bar disabled)
    if (emotion_label_ == nullptr && content_ != nullptr) {
        emotion_label_ = lv_label_create(content_);
        lv_obj_align(emotion_label_, LV_ALIGN_CENTER, 0, 0);
    }
    
    if (emotion_label_ == nullptr)
    {
        return; // Still can't create, skip icon setting
    }
    
    lv_obj_set_style_text_font(emotion_label_, &font_awesome_30_4, 0);
    lv_label_set_text(emotion_label_, icon);

#if !CONFIG_USE_WECHAT_MESSAGE_STYLE
    // 显示emotion_label_，隐藏preview_image_ - Status bar is disabled, skip
    /*
    lv_obj_clear_flag(emotion_label_, LV_OBJ_FLAG_HIDDEN);
    if (preview_image_ != nullptr)
    {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    }
    */
#endif
}

void LcdDisplay::SetTheme(const std::string &theme_name)
{
    DisplayLockGuard lock(this);

    if (theme_name == "dark" || theme_name == "DARK")
    {
        current_theme_ = DARK_THEME;
    }
    else if (theme_name == "light" || theme_name == "LIGHT")
    {
        current_theme_ = LIGHT_THEME;
    }
    else
    {
        // Invalid theme name, return false
        ESP_LOGE(TAG, "Invalid theme name: %s", theme_name.c_str());
        return;
    }

    // Get the active screen
    lv_obj_t *screen = lv_screen_active();

    // Update the screen colors
    lv_obj_set_style_bg_color(screen, current_theme_.background, 0);
    lv_obj_set_style_text_color(screen, current_theme_.text, 0);

    // Update container colors
    if (container_ != nullptr)
    {
        lv_obj_set_style_bg_color(container_, current_theme_.background, 0);
        lv_obj_set_style_border_color(container_, current_theme_.border, 0);
    }

    // Update status bar colors - STATUS BAR IS DISABLED
    /*
    if (status_bar_ != nullptr)
    {
        lv_obj_set_style_bg_color(status_bar_, current_theme_.background, 0);
        lv_obj_set_style_text_color(status_bar_, current_theme_.text, 0);

        // Update status bar elements
        if (network_label_ != nullptr)
        {
            lv_obj_set_style_text_color(network_label_, current_theme_.text, 0);
        }
        if (status_label_ != nullptr)
        {
            lv_obj_set_style_text_color(status_label_, current_theme_.text, 0);
        }
        if (notification_label_ != nullptr)
        {
            lv_obj_set_style_text_color(notification_label_, current_theme_.text, 0);
        }
        if (mute_label_ != nullptr)
        {
            lv_obj_set_style_text_color(mute_label_, current_theme_.text, 0);
        }
        if (battery_label_ != nullptr)
        {
            lv_obj_set_style_text_color(battery_label_, current_theme_.text, 0);
        }
    }
    */

    // Update content area colors
    if (content_ != nullptr)
    {
        lv_obj_set_style_bg_color(content_, current_theme_.chat_background, 0);
        lv_obj_set_style_border_color(content_, current_theme_.border, 0);

        // If we have the chat message style, update all message bubbles
#if CONFIG_USE_WECHAT_MESSAGE_STYLE
        // Iterate through all children of content (message containers or bubbles)
        uint32_t child_count = lv_obj_get_child_cnt(content_);
        for (uint32_t i = 0; i < child_count; i++)
        {
            lv_obj_t *obj = lv_obj_get_child(content_, i);
            if (obj == nullptr)
                continue;

            lv_obj_t *bubble = nullptr;

            // 检查这个对象是容器还是气泡
            // 如果是容器（用户或系统消息），则获取其子对象作为气泡
            // 如果是气泡（助手消息），则直接使用
            if (lv_obj_get_child_cnt(obj) > 0)
            {
                // 可能是容器，检查它是否为用户或系统消息容器
                // 用户和系统消息容器是透明的
                lv_opa_t bg_opa = lv_obj_get_style_bg_opa(obj, 0);
                if (bg_opa == LV_OPA_TRANSP)
                {
                    // 这是用户或系统消息的容器
                    bubble = lv_obj_get_child(obj, 0);
                }
                else
                {
                    // 这可能是助手消息的气泡自身
                    bubble = obj;
                }
            }
            else
            {
                // 没有子元素，可能是其他UI元素，跳过
                continue;
            }

            if (bubble == nullptr)
                continue;

            // 使用保存的用户数据来识别气泡类型
            void *bubble_type_ptr = lv_obj_get_user_data(bubble);
            if (bubble_type_ptr != nullptr)
            {
                const char *bubble_type = static_cast<const char *>(bubble_type_ptr);

                // 根据气泡类型应用正确的颜色
                if (strcmp(bubble_type, "user") == 0)
                {
                    lv_obj_set_style_bg_color(bubble, current_theme_.user_bubble, 0);
                }
                else if (strcmp(bubble_type, "assistant") == 0)
                {
                    lv_obj_set_style_bg_color(bubble, current_theme_.assistant_bubble, 0);
                }
                else if (strcmp(bubble_type, "system") == 0)
                {
                    lv_obj_set_style_bg_color(bubble, current_theme_.system_bubble, 0);
                }
                else if (strcmp(bubble_type, "image") == 0)
                {
                    lv_obj_set_style_bg_color(bubble, current_theme_.system_bubble, 0);
                }

                // Update border color
                lv_obj_set_style_border_color(bubble, current_theme_.border, 0);

                // Update text color for the message
                if (lv_obj_get_child_cnt(bubble) > 0)
                {
                    lv_obj_t *text = lv_obj_get_child(bubble, 0);
                    if (text != nullptr)
                    {
                        // 根据气泡类型设置文本颜色
                        if (strcmp(bubble_type, "system") == 0)
                        {
                            lv_obj_set_style_text_color(text, current_theme_.system_text, 0);
                        }
                        else
                        {
                            lv_obj_set_style_text_color(text, current_theme_.text, 0);
                        }
                    }
                }
            }
            else
            {
                // 如果没有标记，回退到之前的逻辑（颜色比较）
                // ...保留原有的回退逻辑...
                lv_color_t bg_color = lv_obj_get_style_bg_color(bubble, 0);

                // 改进bubble类型检测逻辑，不仅使用颜色比较
                bool is_user_bubble = false;
                bool is_assistant_bubble = false;
                bool is_system_bubble = false;

                // 检查用户bubble
                if (lv_color_eq(bg_color, DARK_USER_BUBBLE_COLOR) ||
                    lv_color_eq(bg_color, LIGHT_USER_BUBBLE_COLOR) ||
                    lv_color_eq(bg_color, current_theme_.user_bubble))
                {
                    is_user_bubble = true;
                }
                // 检查系统bubble
                else if (lv_color_eq(bg_color, DARK_SYSTEM_BUBBLE_COLOR) ||
                         lv_color_eq(bg_color, LIGHT_SYSTEM_BUBBLE_COLOR) ||
                         lv_color_eq(bg_color, current_theme_.system_bubble))
                {
                    is_system_bubble = true;
                }
                // 剩余的都当作助手bubble处理
                else
                {
                    is_assistant_bubble = true;
                }

                // 根据bubble类型应用正确的颜色
                if (is_user_bubble)
                {
                    lv_obj_set_style_bg_color(bubble, current_theme_.user_bubble, 0);
                }
                else if (is_assistant_bubble)
                {
                    lv_obj_set_style_bg_color(bubble, current_theme_.assistant_bubble, 0);
                }
                else if (is_system_bubble)
                {
                    lv_obj_set_style_bg_color(bubble, current_theme_.system_bubble, 0);
                }

                // Update border color
                lv_obj_set_style_border_color(bubble, current_theme_.border, 0);

                // Update text color for the message
                if (lv_obj_get_child_cnt(bubble) > 0)
                {
                    lv_obj_t *text = lv_obj_get_child(bubble, 0);
                    if (text != nullptr)
                    {
                        // 回退到颜色检测逻辑
                        if (lv_color_eq(bg_color, current_theme_.system_bubble) ||
                            lv_color_eq(bg_color, DARK_SYSTEM_BUBBLE_COLOR) ||
                            lv_color_eq(bg_color, LIGHT_SYSTEM_BUBBLE_COLOR))
                        {
                            lv_obj_set_style_text_color(text, current_theme_.system_text, 0);
                        }
                        else
                        {
                            lv_obj_set_style_text_color(text, current_theme_.text, 0);
                        }
                    }
                }
            }
        }
#else
        // Simple UI mode - just update the main chat message
        if (chat_message_label_ != nullptr)
        {
            lv_obj_set_style_text_color(chat_message_label_, current_theme_.text, 0);
        }

#endif
    }

    // Update low battery popup
    if (low_battery_popup_ != nullptr)
    {
        lv_obj_set_style_bg_color(low_battery_popup_, current_theme_.low_battery, 0);
    }

    // No errors occurred. Save theme to settings
    Display::SetTheme(theme_name);
}

void LcdDisplay::SetDisplayRotation180(bool upside_down)
{
    DisplayLockGuard lock(this);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Display is null, cannot set rotation");
        return;
    }
    
    // Track rotation state for overlay coordinate transformation
    display_rotated_180_ = upside_down;
    
    // Use LVGL display rotation API - most efficient for 180° rotation
    lv_display_rotation_t rotation = upside_down ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0;
    lv_display_set_rotation(display_, rotation);
    ESP_LOGI(TAG, "Display rotation set to %s", upside_down ? "180° (upside down)" : "0° (normal)");
}
