#include "display_animator.h"

#include "config.h"

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_st77916.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <sdkconfig.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <vector>

#define TAG "DisplayAnimator"

namespace {

#ifndef DISPLAY_MIRROR_X
#define DISPLAY_MIRROR_X false
#endif
#ifndef DISPLAY_MIRROR_Y
#define DISPLAY_MIRROR_Y false
#endif
#ifndef DISPLAY_SWAP_XY
#define DISPLAY_SWAP_XY false
#endif
#ifndef DISPLAY_OFFSET_X
#define DISPLAY_OFFSET_X 0
#endif
#ifndef DISPLAY_OFFSET_Y
#define DISPLAY_OFFSET_Y 0
#endif
#ifndef QSPI_LCD_BIT_PER_PIXEL
#define QSPI_LCD_BIT_PER_PIXEL 16
#endif

constexpr ledc_mode_t kBacklightLedcMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kBacklightLedcTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kBacklightLedcChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_bit_t kBacklightDutyResolution = LEDC_TIMER_13_BIT;
constexpr uint32_t kBacklightDutyMax = (1u << 13) - 1;
constexpr int kHudWidth = 212;
constexpr int kHudHeight = 38;
constexpr int kStatusHudWidth = 104;
constexpr int kStatusHudHeight = 24;
constexpr int kHudOffsetY = 18;
constexpr int kGaugeTrackHeight = 8;
constexpr int kGaugeRadius = 4;
constexpr int kGaugeFillMinWidth = 3;

int ClampBrightness(int percent) {
    if (percent < 0) {
        return 0;
    }
    if (percent > 100) {
        return 100;
    }
    return percent;
}

int ClampPercent(int percent) {
    return std::clamp(percent, 0, 100);
}

uint32_t BrightnessToDuty(int percent) {
    return static_cast<uint32_t>(ClampBrightness(percent)) * kBacklightDutyMax / 100;
}

lv_obj_t* CreateOverlayRoot() {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_t* root = lv_obj_create(screen);
    lv_obj_set_size(root, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_foreground(root);
    return root;
}

lv_obj_t* CreateHudPanel(lv_obj_t* root, int width, int height) {
    lv_obj_t* panel = lv_obj_create(root);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, 9, 0);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_40, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

lv_obj_t* CreateHudLabel(lv_obj_t* parent, const char* text, int width, lv_align_t align, int x, int y,
                         lv_text_align_t text_align = LV_TEXT_ALIGN_CENTER) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, text_align, 0);
    lv_obj_align(label, align, x, y);
    return label;
}

void StyleGaugeObject(lv_obj_t* obj, lv_color_t color, lv_opa_t opa, int radius) {
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void CreateGauge(lv_obj_t* parent, int x, int y, int width, int height, int percent,
                 bool framed, bool battery_tip) {
    percent = ClampPercent(percent);
    lv_obj_t* track = lv_obj_create(parent);
    lv_obj_set_size(track, width, height);
    lv_obj_align(track, LV_ALIGN_TOP_LEFT, x, y);
    StyleGaugeObject(track, lv_color_black(), LV_OPA_60, height / 2);
    if (framed) {
        lv_obj_set_style_border_width(track, 1, 0);
        lv_obj_set_style_border_color(track, lv_color_white(), 0);
        lv_obj_set_style_border_opa(track, LV_OPA_70, 0);
    }

    const int inner_pad = framed ? 2 : 0;
    const int inner_width = width - inner_pad * 2;
    const int fill_width = percent <= 0 ? 0 : std::max(kGaugeFillMinWidth, inner_width * percent / 100);
    if (fill_width > 0) {
        lv_obj_t* fill = lv_obj_create(track);
        lv_obj_set_size(fill, fill_width, height - inner_pad * 2);
        lv_obj_align(fill, LV_ALIGN_LEFT_MID, inner_pad, 0);
        StyleGaugeObject(fill, lv_color_hex(0x20E078), LV_OPA_COVER, (height - inner_pad * 2) / 2);
    }

    if (battery_tip) {
        lv_obj_t* tip = lv_obj_create(parent);
        lv_obj_set_size(tip, 4, height - 4);
        lv_obj_align(tip, LV_ALIGN_TOP_LEFT, x + width + 1, y + 2);
        StyleGaugeObject(tip, lv_color_white(), LV_OPA_70, 2);
    }
}

static const st77916_lcd_init_cmd_t kVendorInit[] = {
    {0xF0, (uint8_t[]){0x28}, 1, 0},
    {0xF2, (uint8_t[]){0x28}, 1, 0},
    {0x73, (uint8_t[]){0xF0}, 1, 0},
    {0x7C, (uint8_t[]){0xD1}, 1, 0},
    {0x83, (uint8_t[]){0xE0}, 1, 0},
    {0x84, (uint8_t[]){0x61}, 1, 0},
    {0xF2, (uint8_t[]){0x82}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x01}, 1, 0},
    {0xF1, (uint8_t[]){0x01}, 1, 0},
    {0xB0, (uint8_t[]){0x56}, 1, 0},
    {0xB1, (uint8_t[]){0x4D}, 1, 0},
    {0xB2, (uint8_t[]){0x24}, 1, 0},
    {0xB4, (uint8_t[]){0x87}, 1, 0},
    {0xB5, (uint8_t[]){0x44}, 1, 0},
    {0xB6, (uint8_t[]){0x8B}, 1, 0},
    {0xB7, (uint8_t[]){0x40}, 1, 0},
    {0xB8, (uint8_t[]){0x86}, 1, 0},
    {0xBA, (uint8_t[]){0x00}, 1, 0},
    {0xBB, (uint8_t[]){0x08}, 1, 0},
    {0xBC, (uint8_t[]){0x08}, 1, 0},
    {0xBD, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x80}, 1, 0},
    {0xC1, (uint8_t[]){0x10}, 1, 0},
    {0xC2, (uint8_t[]){0x37}, 1, 0},
    {0xC3, (uint8_t[]){0x80}, 1, 0},
    {0xC4, (uint8_t[]){0x10}, 1, 0},
    {0xC5, (uint8_t[]){0x37}, 1, 0},
    {0xC6, (uint8_t[]){0xA9}, 1, 0},
    {0xC7, (uint8_t[]){0x41}, 1, 0},
    {0xC8, (uint8_t[]){0x01}, 1, 0},
    {0xC9, (uint8_t[]){0xA9}, 1, 0},
    {0xCA, (uint8_t[]){0x41}, 1, 0},
    {0xCB, (uint8_t[]){0x01}, 1, 0},
    {0xD0, (uint8_t[]){0x91}, 1, 0},
    {0xD1, (uint8_t[]){0x68}, 1, 0},
    {0xD2, (uint8_t[]){0x68}, 1, 0},
    {0xF5, (uint8_t[]){0x00, 0xA5}, 2, 0},
    {0xDD, (uint8_t[]){0x4F}, 1, 0},
    {0xDE, (uint8_t[]){0x4F}, 1, 0},
    {0xF1, (uint8_t[]){0x10}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xF0, (uint8_t[]){0x02}, 1, 0},
    {0xE0, (uint8_t[]){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
    {0xE1, (uint8_t[]){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
    {0xF0, (uint8_t[]){0x10}, 1, 0},
    {0xF3, (uint8_t[]){0x10}, 1, 0},
    {0xE0, (uint8_t[]){0x07}, 1, 0},
    {0xE1, (uint8_t[]){0x00}, 1, 0},
    {0xE2, (uint8_t[]){0x00}, 1, 0},
    {0xE3, (uint8_t[]){0x00}, 1, 0},
    {0xE4, (uint8_t[]){0xE0}, 1, 0},
    {0xE5, (uint8_t[]){0x06}, 1, 0},
    {0xE6, (uint8_t[]){0x21}, 1, 0},
    {0xE7, (uint8_t[]){0x01}, 1, 0},
    {0xE8, (uint8_t[]){0x05}, 1, 0},
    {0xE9, (uint8_t[]){0x02}, 1, 0},
    {0xEA, (uint8_t[]){0xDA}, 1, 0},
    {0xEB, (uint8_t[]){0x00}, 1, 0},
    {0xEC, (uint8_t[]){0x00}, 1, 0},
    {0xED, (uint8_t[]){0x0F}, 1, 0},
    {0xEE, (uint8_t[]){0x00}, 1, 0},
    {0xEF, (uint8_t[]){0x00}, 1, 0},
    {0xF8, (uint8_t[]){0x00}, 1, 0},
    {0xF9, (uint8_t[]){0x00}, 1, 0},
    {0xFA, (uint8_t[]){0x00}, 1, 0},
    {0xFB, (uint8_t[]){0x00}, 1, 0},
    {0xFC, (uint8_t[]){0x00}, 1, 0},
    {0xFD, (uint8_t[]){0x00}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xFF, (uint8_t[]){0x00}, 1, 0},
    {0x60, (uint8_t[]){0x40}, 1, 0},
    {0x61, (uint8_t[]){0x04}, 1, 0},
    {0x62, (uint8_t[]){0x00}, 1, 0},
    {0x63, (uint8_t[]){0x42}, 1, 0},
    {0x64, (uint8_t[]){0xD9}, 1, 0},
    {0x65, (uint8_t[]){0x00}, 1, 0},
    {0x66, (uint8_t[]){0x00}, 1, 0},
    {0x67, (uint8_t[]){0x00}, 1, 0},
    {0x68, (uint8_t[]){0x00}, 1, 0},
    {0x69, (uint8_t[]){0x00}, 1, 0},
    {0x6A, (uint8_t[]){0x00}, 1, 0},
    {0x6B, (uint8_t[]){0x00}, 1, 0},
    {0x70, (uint8_t[]){0x40}, 1, 0},
    {0x71, (uint8_t[]){0x03}, 1, 0},
    {0x72, (uint8_t[]){0x00}, 1, 0},
    {0x73, (uint8_t[]){0x42}, 1, 0},
    {0x74, (uint8_t[]){0xD8}, 1, 0},
    {0x75, (uint8_t[]){0x00}, 1, 0},
    {0x76, (uint8_t[]){0x00}, 1, 0},
    {0x77, (uint8_t[]){0x00}, 1, 0},
    {0x78, (uint8_t[]){0x00}, 1, 0},
    {0x79, (uint8_t[]){0x00}, 1, 0},
    {0x7A, (uint8_t[]){0x00}, 1, 0},
    {0x7B, (uint8_t[]){0x00}, 1, 0},
    {0x80, (uint8_t[]){0x48}, 1, 0},
    {0x81, (uint8_t[]){0x00}, 1, 0},
    {0x82, (uint8_t[]){0x06}, 1, 0},
    {0x83, (uint8_t[]){0x02}, 1, 0},
    {0x84, (uint8_t[]){0xD6}, 1, 0},
    {0x85, (uint8_t[]){0x04}, 1, 0},
    {0x86, (uint8_t[]){0x00}, 1, 0},
    {0x87, (uint8_t[]){0x00}, 1, 0},
    {0x88, (uint8_t[]){0x48}, 1, 0},
    {0x89, (uint8_t[]){0x00}, 1, 0},
    {0x8A, (uint8_t[]){0x08}, 1, 0},
    {0x8B, (uint8_t[]){0x02}, 1, 0},
    {0x8C, (uint8_t[]){0xD8}, 1, 0},
    {0x8D, (uint8_t[]){0x04}, 1, 0},
    {0x8E, (uint8_t[]){0x00}, 1, 0},
    {0x8F, (uint8_t[]){0x00}, 1, 0},
    {0x90, (uint8_t[]){0x48}, 1, 0},
    {0x91, (uint8_t[]){0x00}, 1, 0},
    {0x92, (uint8_t[]){0x0A}, 1, 0},
    {0x93, (uint8_t[]){0x02}, 1, 0},
    {0x94, (uint8_t[]){0xDA}, 1, 0},
    {0x95, (uint8_t[]){0x04}, 1, 0},
    {0x96, (uint8_t[]){0x00}, 1, 0},
    {0x97, (uint8_t[]){0x00}, 1, 0},
    {0x98, (uint8_t[]){0x48}, 1, 0},
    {0x99, (uint8_t[]){0x00}, 1, 0},
    {0x9A, (uint8_t[]){0x0C}, 1, 0},
    {0x9B, (uint8_t[]){0x02}, 1, 0},
    {0x9C, (uint8_t[]){0xDC}, 1, 0},
    {0x9D, (uint8_t[]){0x04}, 1, 0},
    {0x9E, (uint8_t[]){0x00}, 1, 0},
    {0x9F, (uint8_t[]){0x00}, 1, 0},
    {0xA0, (uint8_t[]){0x48}, 1, 0},
    {0xA1, (uint8_t[]){0x00}, 1, 0},
    {0xA2, (uint8_t[]){0x05}, 1, 0},
    {0xA3, (uint8_t[]){0x02}, 1, 0},
    {0xA4, (uint8_t[]){0xD5}, 1, 0},
    {0xA5, (uint8_t[]){0x04}, 1, 0},
    {0xA6, (uint8_t[]){0x00}, 1, 0},
    {0xA7, (uint8_t[]){0x00}, 1, 0},
    {0xA8, (uint8_t[]){0x48}, 1, 0},
    {0xA9, (uint8_t[]){0x00}, 1, 0},
    {0xAA, (uint8_t[]){0x07}, 1, 0},
    {0xAB, (uint8_t[]){0x02}, 1, 0},
    {0xAC, (uint8_t[]){0xD7}, 1, 0},
    {0xAD, (uint8_t[]){0x04}, 1, 0},
    {0xAE, (uint8_t[]){0x00}, 1, 0},
    {0xAF, (uint8_t[]){0x00}, 1, 0},
    {0xB0, (uint8_t[]){0x48}, 1, 0},
    {0xB1, (uint8_t[]){0x00}, 1, 0},
    {0xB2, (uint8_t[]){0x09}, 1, 0},
    {0xB3, (uint8_t[]){0x02}, 1, 0},
    {0xB4, (uint8_t[]){0xD9}, 1, 0},
    {0xB5, (uint8_t[]){0x04}, 1, 0},
    {0xB6, (uint8_t[]){0x00}, 1, 0},
    {0xB7, (uint8_t[]){0x00}, 1, 0},
    {0xB8, (uint8_t[]){0x48}, 1, 0},
    {0xB9, (uint8_t[]){0x00}, 1, 0},
    {0xBA, (uint8_t[]){0x0B}, 1, 0},
    {0xBB, (uint8_t[]){0x02}, 1, 0},
    {0xBC, (uint8_t[]){0xDB}, 1, 0},
    {0xBD, (uint8_t[]){0x04}, 1, 0},
    {0xBE, (uint8_t[]){0x00}, 1, 0},
    {0xBF, (uint8_t[]){0x00}, 1, 0},
    {0xC0, (uint8_t[]){0x10}, 1, 0},
    {0xC1, (uint8_t[]){0x47}, 1, 0},
    {0xC2, (uint8_t[]){0x56}, 1, 0},
    {0xC3, (uint8_t[]){0x65}, 1, 0},
    {0xC4, (uint8_t[]){0x74}, 1, 0},
    {0xC5, (uint8_t[]){0x88}, 1, 0},
    {0xC6, (uint8_t[]){0x99}, 1, 0},
    {0xC7, (uint8_t[]){0x01}, 1, 0},
    {0xC8, (uint8_t[]){0xBB}, 1, 0},
    {0xC9, (uint8_t[]){0xAA}, 1, 0},
    {0xD0, (uint8_t[]){0x10}, 1, 0},
    {0xD1, (uint8_t[]){0x47}, 1, 0},
    {0xD2, (uint8_t[]){0x56}, 1, 0},
    {0xD3, (uint8_t[]){0x65}, 1, 0},
    {0xD4, (uint8_t[]){0x74}, 1, 0},
    {0xD5, (uint8_t[]){0x88}, 1, 0},
    {0xD6, (uint8_t[]){0x99}, 1, 0},
    {0xD7, (uint8_t[]){0x01}, 1, 0},
    {0xD8, (uint8_t[]){0xBB}, 1, 0},
    {0xD9, (uint8_t[]){0xAA}, 1, 0},
    {0xF3, (uint8_t[]){0x01}, 1, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0x21, (uint8_t[]){}, 0, 0},
    {0x11, (uint8_t[]){}, 0, 0},
    {0x00, (uint8_t[]){}, 0, 120},
};

}  // namespace

DisplayAnimator::~DisplayAnimator() {
    if (ready_ && gif_ != nullptr && lvgl_port_lock(1000)) {
        ClearOverlayLocked();
        lv_obj_delete(gif_);
        gif_ = nullptr;
        lvgl_port_unlock();
    }
    if (overlay_timer_ != nullptr) {
        esp_timer_stop(overlay_timer_);
        esp_timer_delete(overlay_timer_);
        overlay_timer_ = nullptr;
    }
    FreeBuffersLocked();
}

esp_err_t DisplayAnimator::Init() {
    if (ready_) {
        return ESP_OK;
    }
    if (!InitPanel() || !InitLvgl()) {
        ESP_LOGW(TAG, "display animation renderer disabled");
        return ESP_FAIL;
    }
    ready_ = true;
    ESP_LOGI(TAG, "display animation renderer ready");
    return ESP_OK;
}

bool DisplayAnimator::InitPanel() {
    if (!spi_ready_) {
        spi_bus_config_t bus_config = {};
        bus_config.sclk_io_num = QSPI_PIN_NUM_LCD_PCLK;
        bus_config.data0_io_num = QSPI_PIN_NUM_LCD_DATA0;
        bus_config.data1_io_num = QSPI_PIN_NUM_LCD_DATA1;
        bus_config.data2_io_num = QSPI_PIN_NUM_LCD_DATA2;
        bus_config.data3_io_num = QSPI_PIN_NUM_LCD_DATA3;
        bus_config.max_transfer_sz = DISPLAY_WIDTH * 80 * sizeof(uint16_t);
        esp_err_t err = spi_bus_initialize(QSPI_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
            return false;
        }
        spi_ready_ = true;
    }

    if (!panel_ready_) {
        const esp_lcd_panel_io_spi_config_t io_config = ST77916_PANEL_IO_QSPI_CONFIG(QSPI_PIN_NUM_LCD_CS, nullptr, nullptr);
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)QSPI_LCD_HOST, &io_config, &panel_io_));

        st77916_vendor_config_t vendor_config = {
            .init_cmds = kVendorInit,
            .init_cmds_size = sizeof(kVendorInit) / sizeof(kVendorInit[0]),
            .flags = {
                .use_qspi_interface = 1,
            },
        };
        const esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = QSPI_PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = QSPI_LCD_BIT_PER_PIXEL,
            .vendor_config = &vendor_config,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(panel_io_, &panel_config, &panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));

        std::vector<uint16_t> black_line(DISPLAY_WIDTH, 0x0000);
        for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
            esp_lcd_panel_draw_bitmap(panel_, 0, y, DISPLAY_WIDTH, y + 1, black_line.data());
        }
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        InitBacklight();
        panel_ready_ = true;
    }
    return true;
}

bool DisplayAnimator::InitBacklight() {
    if (backlight_ready_) {
        return true;
    }

    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = kBacklightLedcMode;
    timer_cfg.duty_resolution = kBacklightDutyResolution;
    timer_cfg.timer_num = kBacklightLedcTimer;
    timer_cfg.freq_hz = 5000;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "backlight LEDC timer init failed: %s", esp_err_to_name(err));
        gpio_config_t backlight = {};
        backlight.pin_bit_mask = BIT64(QSPI_PIN_NUM_LCD_BL);
        backlight.mode = GPIO_MODE_OUTPUT;
        gpio_config(&backlight);
        gpio_set_level(QSPI_PIN_NUM_LCD_BL, backlight_brightness_ > 0 ? 1 : 0);
        return false;
    }

    ledc_channel_config_t channel_cfg = {};
    channel_cfg.gpio_num = QSPI_PIN_NUM_LCD_BL;
    channel_cfg.speed_mode = kBacklightLedcMode;
    channel_cfg.channel = kBacklightLedcChannel;
    channel_cfg.intr_type = LEDC_INTR_DISABLE;
    channel_cfg.timer_sel = kBacklightLedcTimer;
    channel_cfg.duty = BrightnessToDuty(backlight_brightness_);
    channel_cfg.hpoint = 0;
    err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "backlight LEDC channel init failed: %s", esp_err_to_name(err));
        return false;
    }

    backlight_ready_ = true;
    return true;
}

bool DisplayAnimator::InitLvgl() {
    static bool s_lvgl_port_ready = false;
    if (!s_lvgl_port_ready) {
        lv_init();
        lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
        port_cfg.task_priority = 1;
        port_cfg.timer_period_ms = 20;
        port_cfg.task_stack_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        if (lvgl_port_init(&port_cfg) != ESP_OK) {
            ESP_LOGE(TAG, "LVGL port init failed");
            return false;
        }
        s_lvgl_port_ready = true;
    }

    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = DISPLAY_WIDTH * 8,
        .double_buffer = false,
        .trans_size = 0,
        .hres = DISPLAY_WIDTH,
        .vres = DISPLAY_HEIGHT,
        .monochrome = false,
        .rotation = {
            .swap_xy = DISPLAY_SWAP_XY,
            .mirror_x = !DISPLAY_MIRROR_X,
            .mirror_y = !DISPLAY_MIRROR_Y,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 1,
            .sw_rotate = 0,
            .swap_bytes = 1,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };
    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "LVGL display add failed");
        return false;
    }

    if (!lvgl_port_lock(1000)) {
        return false;
    }
    lv_display_set_offset(display_, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y);
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    gif_ = lv_gif_create(screen);
    lv_obj_center(gif_);
    lv_obj_add_flag(gif_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_all(gif_, 0, 0);
    lv_obj_set_style_margin_all(gif_, 0, 0);
    lv_obj_set_style_border_width(gif_, 0, 0);
    lv_obj_set_style_radius(gif_, 0, 0);
    lv_obj_add_event_cb(gif_, GifEventHandler, LV_EVENT_READY, this);
    lvgl_port_unlock();
    return true;
}

void DisplayAnimator::SetBacklightBrightness(int percent) {
    backlight_brightness_ = ClampBrightness(percent);
    if (backlight_ready_) {
        ledc_set_duty(kBacklightLedcMode, kBacklightLedcChannel, BrightnessToDuty(backlight_brightness_));
        ledc_update_duty(kBacklightLedcMode, kBacklightLedcChannel);
    } else {
        gpio_set_level(QSPI_PIN_NUM_LCD_BL, backlight_brightness_ > 0 ? 1 : 0);
    }
}

int DisplayAnimator::AdjustBacklightBrightness(int delta) {
    SetBacklightBrightness(backlight_brightness_ + delta);
    return backlight_brightness_;
}

void DisplayAnimator::RestoreBacklightBrightness() {
    SetBacklightBrightness(normal_backlight_brightness_);
}

void DisplayAnimator::ShowStatusCanvas(const char* network_label, int network_percent, int battery_percent,
                                       bool charging, uint32_t duration_ms) {
    if (!ready_) {
        return;
    }

    (void)network_percent;
    (void)charging;
    const int battery = battery_percent < 0 ? 0 : ClampPercent(battery_percent);
    char ssid[80];
    std::snprintf(ssid, sizeof(ssid), "%s", network_label && network_label[0] ? network_label : "na");

    if (!lvgl_port_lock(1000)) {
        return;
    }

    ClearOverlayLocked();
    overlay_container_ = CreateOverlayRoot();
    lv_obj_t* panel = CreateHudPanel(overlay_container_, kStatusHudWidth, kStatusHudHeight);
    CreateHudLabel(panel, ssid, 49, LV_ALIGN_LEFT_MID, 5, 0, LV_TEXT_ALIGN_RIGHT);
    CreateGauge(panel, 59, 6, 30, 12, battery, true, true);
    lv_obj_update_layout(panel);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, kHudOffsetY);
    lv_obj_move_foreground(overlay_container_);
    lvgl_port_unlock();

    ScheduleOverlayClear(duration_ms);
}

void DisplayAnimator::ShowValueOverlay(const char* label, int percent, uint32_t duration_ms) {
    if (!ready_) {
        return;
    }

    const int value = ClampPercent(percent);
    char title[64];
    std::snprintf(title, sizeof(title), "%s %d%%", label && label[0] ? label : "value", value);

    if (!lvgl_port_lock(1000)) {
        return;
    }

    ClearOverlayLocked();
    overlay_container_ = CreateOverlayRoot();
    lv_obj_t* panel = CreateHudPanel(overlay_container_, kHudWidth, kHudHeight);
    CreateHudLabel(panel, title, kHudWidth - 20, LV_ALIGN_TOP_MID, 0, 4);
    CreateGauge(panel, 12, 24, kHudWidth - 24, kGaugeTrackHeight, value, false, false);
    lv_obj_update_layout(panel);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, kHudOffsetY);
    lv_obj_move_foreground(overlay_container_);
    lvgl_port_unlock();

    ScheduleOverlayClear(duration_ms);
}

void DisplayAnimator::ClearOverlay() {
    if (!ready_) {
        return;
    }
    if (!lvgl_port_lock(1000)) {
        return;
    }
    ClearOverlayLocked();
    lvgl_port_unlock();
}

uint8_t* DisplayAnimator::CopyGifData(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return nullptr;
    }
    uint8_t* copy = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (copy == nullptr) {
        copy = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
    }
    if (copy != nullptr) {
        std::memcpy(copy, data, size);
    }
    return copy;
}

bool DisplayAnimator::ShowLoop(const uint8_t* data, size_t size, const char* name) {
    if (!ready_ || data == nullptr || size == 0) {
        return false;
    }
    uint8_t* current = CopyGifData(data, size);
    if (current == nullptr) {
        ESP_LOGW(TAG, "failed to copy GIF %s size=%u", name ? name : "<unnamed>", static_cast<unsigned>(size));
        return false;
    }
    if (!lvgl_port_lock(1000)) {
        heap_caps_free(current);
        return false;
    }
    if (gif_ != nullptr) {
        lv_gif_pause(gif_);
    }
    FreeBuffersLocked();
    current_data_ = current;
    current_size_ = size;
    current_name_ = name ? name : "<unnamed>";
    awaiting_start_ready_ = false;
    SetSourceLocked(current_name_.c_str());
    lvgl_port_unlock();
    return true;
}

bool DisplayAnimator::ShowStartThenLoop(const uint8_t* start_data, size_t start_size, const char* start_name,
                                        const uint8_t* loop_data, size_t loop_size, const char* loop_name) {
    if (!ready_ || start_data == nullptr || start_size == 0 || loop_data == nullptr || loop_size == 0) {
        return false;
    }
    uint8_t* start_copy = CopyGifData(start_data, start_size);
    uint8_t* loop_copy = CopyGifData(loop_data, loop_size);
    if (start_copy == nullptr || loop_copy == nullptr) {
        if (start_copy != nullptr) {
            heap_caps_free(start_copy);
        }
        if (loop_copy != nullptr) {
            heap_caps_free(loop_copy);
        }
        ESP_LOGW(TAG, "failed to copy start/loop GIFs %s -> %s",
                 start_name ? start_name : "<start>",
                 loop_name ? loop_name : "<loop>");
        return false;
    }

    if (!lvgl_port_lock(1000)) {
        heap_caps_free(start_copy);
        heap_caps_free(loop_copy);
        return false;
    }
    if (gif_ != nullptr) {
        lv_gif_pause(gif_);
    }
    FreeBuffersLocked();
    current_data_ = start_copy;
    current_size_ = start_size;
    current_name_ = start_name ? start_name : "<start>";
    pending_loop_data_ = loop_copy;
    pending_loop_size_ = loop_size;
    pending_loop_name_ = loop_name ? loop_name : "<loop>";
    awaiting_start_ready_ = true;
    SetSourceLocked(current_name_.c_str());
    lvgl_port_unlock();
    return true;
}

void DisplayAnimator::OverlayTimerCallback(void* arg) {
    auto* self = static_cast<DisplayAnimator*>(arg);
    if (self != nullptr) {
        self->ClearOverlay();
    }
}

void DisplayAnimator::ClearOverlayLocked() {
    if (overlay_container_ != nullptr) {
        lv_obj_delete(overlay_container_);
        overlay_container_ = nullptr;
    }
}

void DisplayAnimator::ScheduleOverlayClear(uint32_t duration_ms) {
    if (overlay_timer_ == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = OverlayTimerCallback;
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "display_overlay";
        args.skip_unhandled_events = true;
        if (esp_timer_create(&args, &overlay_timer_) != ESP_OK) {
            overlay_timer_ = nullptr;
            return;
        }
    }
    esp_timer_stop(overlay_timer_);
    if (duration_ms > 0) {
        esp_timer_start_once(overlay_timer_, static_cast<uint64_t>(duration_ms) * 1000);
    }
}

void DisplayAnimator::SetSourceLocked(const char* name) {
    if (gif_ == nullptr || current_data_ == nullptr || current_size_ == 0) {
        return;
    }
    gif_desc_ = {};
    gif_desc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    gif_desc_.header.cf = LV_COLOR_FORMAT_L8;
    gif_desc_.header.flags = 0;
    gif_desc_.header.w = 0;
    gif_desc_.header.h = 0;
    gif_desc_.header.stride = 0;
    gif_desc_.data_size = current_size_;
    gif_desc_.data = current_data_;
    lv_gif_set_src(gif_, &gif_desc_);
    lv_obj_clear_flag(gif_, LV_OBJ_FLAG_HIDDEN);
    if (!lv_gif_is_loaded(gif_)) {
        ESP_LOGW(TAG, "GIF source failed to load in LVGL: %s size=%u",
                 name ? name : current_name_.c_str(), static_cast<unsigned>(current_size_));
        return;
    }
    ESP_LOGI(TAG, "displaying GIF %s size=%u", name ? name : current_name_.c_str(), static_cast<unsigned>(current_size_));
}

void DisplayAnimator::GifEventHandler(lv_event_t* event) {
    auto* self = static_cast<DisplayAnimator*>(lv_event_get_user_data(event));
    if (self == nullptr || lv_event_get_code(event) != LV_EVENT_READY) {
        return;
    }
    self->SwitchPendingLoopLocked();
}

void DisplayAnimator::SwitchPendingLoopLocked() {
    if (!awaiting_start_ready_ || pending_loop_data_ == nullptr || pending_loop_size_ == 0) {
        return;
    }
    if (gif_ != nullptr) {
        lv_gif_pause(gif_);
    }
    if (current_data_ != nullptr) {
        heap_caps_free(current_data_);
    }
    current_data_ = pending_loop_data_;
    current_size_ = pending_loop_size_;
    current_name_ = pending_loop_name_;
    pending_loop_data_ = nullptr;
    pending_loop_size_ = 0;
    pending_loop_name_.clear();
    awaiting_start_ready_ = false;
    SetSourceLocked(current_name_.c_str());
}

void DisplayAnimator::FreeBuffersLocked() {
    if (current_data_ != nullptr) {
        heap_caps_free(current_data_);
        current_data_ = nullptr;
    }
    current_size_ = 0;
    current_name_.clear();
    if (pending_loop_data_ != nullptr) {
        heap_caps_free(pending_loop_data_);
        pending_loop_data_ = nullptr;
    }
    pending_loop_size_ = 0;
    pending_loop_name_.clear();
    awaiting_start_ready_ = false;
}
