#include "wifi_board.h"
#include "audio_codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"
#include "backlight.h"
#include "animation/animation.h"
#include "sd_card.h"

#include <wifi_station.h>
#include <esp_log.h>

#include <driver/i2c_master.h>
#include <driver/i2c.h>
#include "i2c_device.h"
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_st77916.h>
#include "touch.h"
#include <touch_sensor_lowlevel.h>
#include <touch_button_sensor.h>

#include "driver/temperature_sensor.h"

#define TAG "EchoEar"

LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);
temperature_sensor_handle_t temp_sensor = NULL;
static const st77916_lcd_init_cmd_t vendor_specific_init_yysj[] = {
    {0xF0, (uint8_t []){0x28}, 1, 0},
    {0xF2, (uint8_t []){0x28}, 1, 0},
    {0x73, (uint8_t []){0xF0}, 1, 0},
    {0x7C, (uint8_t []){0xD1}, 1, 0},
    {0x83, (uint8_t []){0xE0}, 1, 0},
    {0x84, (uint8_t []){0x61}, 1, 0},
    {0xF2, (uint8_t []){0x82}, 1, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0},
    {0xF0, (uint8_t []){0x01}, 1, 0},
    {0xF1, (uint8_t []){0x01}, 1, 0},
    {0xB0, (uint8_t []){0x56}, 1, 0},
    {0xB1, (uint8_t []){0x4D}, 1, 0},
    {0xB2, (uint8_t []){0x24}, 1, 0},
    {0xB4, (uint8_t []){0x87}, 1, 0},
    {0xB5, (uint8_t []){0x44}, 1, 0},
    {0xB6, (uint8_t []){0x8B}, 1, 0},
    {0xB7, (uint8_t []){0x40}, 1, 0},
    {0xB8, (uint8_t []){0x86}, 1, 0},
    {0xBA, (uint8_t []){0x00}, 1, 0},
    {0xBB, (uint8_t []){0x08}, 1, 0},
    {0xBC, (uint8_t []){0x08}, 1, 0},
    {0xBD, (uint8_t []){0x00}, 1, 0},
    {0xC0, (uint8_t []){0x80}, 1, 0},
    {0xC1, (uint8_t []){0x10}, 1, 0},
    {0xC2, (uint8_t []){0x37}, 1, 0},
    {0xC3, (uint8_t []){0x80}, 1, 0},
    {0xC4, (uint8_t []){0x10}, 1, 0},
    {0xC5, (uint8_t []){0x37}, 1, 0},
    {0xC6, (uint8_t []){0xA9}, 1, 0},
    {0xC7, (uint8_t []){0x41}, 1, 0},
    {0xC8, (uint8_t []){0x01}, 1, 0},
    {0xC9, (uint8_t []){0xA9}, 1, 0},
    {0xCA, (uint8_t []){0x41}, 1, 0},
    {0xCB, (uint8_t []){0x01}, 1, 0},
    {0xD0, (uint8_t []){0x91}, 1, 0},
    {0xD1, (uint8_t []){0x68}, 1, 0},
    {0xD2, (uint8_t []){0x68}, 1, 0},
    {0xF5, (uint8_t []){0x00, 0xA5}, 2, 0},
    {0xDD, (uint8_t []){0x4F}, 1, 0},
    {0xDE, (uint8_t []){0x4F}, 1, 0},
    {0xF1, (uint8_t []){0x10}, 1, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0},
    {0xF0, (uint8_t []){0x02}, 1, 0},
    {0xE0, (uint8_t []){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
    {0xE1, (uint8_t []){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
    {0xF0, (uint8_t []){0x10}, 1, 0},
    {0xF3, (uint8_t []){0x10}, 1, 0},
    {0xE0, (uint8_t []){0x07}, 1, 0},
    {0xE1, (uint8_t []){0x00}, 1, 0},
    {0xE2, (uint8_t []){0x00}, 1, 0},
    {0xE3, (uint8_t []){0x00}, 1, 0},
    {0xE4, (uint8_t []){0xE0}, 1, 0},
    {0xE5, (uint8_t []){0x06}, 1, 0},
    {0xE6, (uint8_t []){0x21}, 1, 0},
    {0xE7, (uint8_t []){0x01}, 1, 0},
    {0xE8, (uint8_t []){0x05}, 1, 0},
    {0xE9, (uint8_t []){0x02}, 1, 0},
    {0xEA, (uint8_t []){0xDA}, 1, 0},
    {0xEB, (uint8_t []){0x00}, 1, 0},
    {0xEC, (uint8_t []){0x00}, 1, 0},
    {0xED, (uint8_t []){0x0F}, 1, 0},
    {0xEE, (uint8_t []){0x00}, 1, 0},
    {0xEF, (uint8_t []){0x00}, 1, 0},
    {0xF8, (uint8_t []){0x00}, 1, 0},
    {0xF9, (uint8_t []){0x00}, 1, 0},
    {0xFA, (uint8_t []){0x00}, 1, 0},
    {0xFB, (uint8_t []){0x00}, 1, 0},
    {0xFC, (uint8_t []){0x00}, 1, 0},
    {0xFD, (uint8_t []){0x00}, 1, 0},
    {0xFE, (uint8_t []){0x00}, 1, 0},
    {0xFF, (uint8_t []){0x00}, 1, 0},
    {0x60, (uint8_t []){0x40}, 1, 0},
    {0x61, (uint8_t []){0x04}, 1, 0},
    {0x62, (uint8_t []){0x00}, 1, 0},
    {0x63, (uint8_t []){0x42}, 1, 0},
    {0x64, (uint8_t []){0xD9}, 1, 0},
    {0x65, (uint8_t []){0x00}, 1, 0},
    {0x66, (uint8_t []){0x00}, 1, 0},
    {0x67, (uint8_t []){0x00}, 1, 0},
    {0x68, (uint8_t []){0x00}, 1, 0},
    {0x69, (uint8_t []){0x00}, 1, 0},
    {0x6A, (uint8_t []){0x00}, 1, 0},
    {0x6B, (uint8_t []){0x00}, 1, 0},
    {0x70, (uint8_t []){0x40}, 1, 0},
    {0x71, (uint8_t []){0x03}, 1, 0},
    {0x72, (uint8_t []){0x00}, 1, 0},
    {0x73, (uint8_t []){0x42}, 1, 0},
    {0x74, (uint8_t []){0xD8}, 1, 0},
    {0x75, (uint8_t []){0x00}, 1, 0},
    {0x76, (uint8_t []){0x00}, 1, 0},
    {0x77, (uint8_t []){0x00}, 1, 0},
    {0x78, (uint8_t []){0x00}, 1, 0},
    {0x79, (uint8_t []){0x00}, 1, 0},
    {0x7A, (uint8_t []){0x00}, 1, 0},
    {0x7B, (uint8_t []){0x00}, 1, 0},
    {0x80, (uint8_t []){0x48}, 1, 0},
    {0x81, (uint8_t []){0x00}, 1, 0},
    {0x82, (uint8_t []){0x06}, 1, 0},
    {0x83, (uint8_t []){0x02}, 1, 0},
    {0x84, (uint8_t []){0xD6}, 1, 0},
    {0x85, (uint8_t []){0x04}, 1, 0},
    {0x86, (uint8_t []){0x00}, 1, 0},
    {0x87, (uint8_t []){0x00}, 1, 0},
    {0x88, (uint8_t []){0x48}, 1, 0},
    {0x89, (uint8_t []){0x00}, 1, 0},
    {0x8A, (uint8_t []){0x08}, 1, 0},
    {0x8B, (uint8_t []){0x02}, 1, 0},
    {0x8C, (uint8_t []){0xD8}, 1, 0},
    {0x8D, (uint8_t []){0x04}, 1, 0},
    {0x8E, (uint8_t []){0x00}, 1, 0},
    {0x8F, (uint8_t []){0x00}, 1, 0},
    {0x90, (uint8_t []){0x48}, 1, 0},
    {0x91, (uint8_t []){0x00}, 1, 0},
    {0x92, (uint8_t []){0x0A}, 1, 0},
    {0x93, (uint8_t []){0x02}, 1, 0},
    {0x94, (uint8_t []){0xDA}, 1, 0},
    {0x95, (uint8_t []){0x04}, 1, 0},
    {0x96, (uint8_t []){0x00}, 1, 0},
    {0x97, (uint8_t []){0x00}, 1, 0},
    {0x98, (uint8_t []){0x48}, 1, 0},
    {0x99, (uint8_t []){0x00}, 1, 0},
    {0x9A, (uint8_t []){0x0C}, 1, 0},
    {0x9B, (uint8_t []){0x02}, 1, 0},
    {0x9C, (uint8_t []){0xDC}, 1, 0},
    {0x9D, (uint8_t []){0x04}, 1, 0},
    {0x9E, (uint8_t []){0x00}, 1, 0},
    {0x9F, (uint8_t []){0x00}, 1, 0},
    {0xA0, (uint8_t []){0x48}, 1, 0},
    {0xA1, (uint8_t []){0x00}, 1, 0},
    {0xA2, (uint8_t []){0x05}, 1, 0},
    {0xA3, (uint8_t []){0x02}, 1, 0},
    {0xA4, (uint8_t []){0xD5}, 1, 0},
    {0xA5, (uint8_t []){0x04}, 1, 0},
    {0xA6, (uint8_t []){0x00}, 1, 0},
    {0xA7, (uint8_t []){0x00}, 1, 0},
    {0xA8, (uint8_t []){0x48}, 1, 0},
    {0xA9, (uint8_t []){0x00}, 1, 0},
    {0xAA, (uint8_t []){0x07}, 1, 0},
    {0xAB, (uint8_t []){0x02}, 1, 0},
    {0xAC, (uint8_t []){0xD7}, 1, 0},
    {0xAD, (uint8_t []){0x04}, 1, 0},
    {0xAE, (uint8_t []){0x00}, 1, 0},
    {0xAF, (uint8_t []){0x00}, 1, 0},
    {0xB0, (uint8_t []){0x48}, 1, 0},
    {0xB1, (uint8_t []){0x00}, 1, 0},
    {0xB2, (uint8_t []){0x09}, 1, 0},
    {0xB3, (uint8_t []){0x02}, 1, 0},
    {0xB4, (uint8_t []){0xD9}, 1, 0},
    {0xB5, (uint8_t []){0x04}, 1, 0},
    {0xB6, (uint8_t []){0x00}, 1, 0},
    {0xB7, (uint8_t []){0x00}, 1, 0},
    {0xB8, (uint8_t []){0x48}, 1, 0},
    {0xB9, (uint8_t []){0x00}, 1, 0},
    {0xBA, (uint8_t []){0x0B}, 1, 0},
    {0xBB, (uint8_t []){0x02}, 1, 0},
    {0xBC, (uint8_t []){0xDB}, 1, 0},
    {0xBD, (uint8_t []){0x04}, 1, 0},
    {0xBE, (uint8_t []){0x00}, 1, 0},
    {0xBF, (uint8_t []){0x00}, 1, 0},
    {0xC0, (uint8_t []){0x10}, 1, 0},
    {0xC1, (uint8_t []){0x47}, 1, 0},
    {0xC2, (uint8_t []){0x56}, 1, 0},
    {0xC3, (uint8_t []){0x65}, 1, 0},
    {0xC4, (uint8_t []){0x74}, 1, 0},
    {0xC5, (uint8_t []){0x88}, 1, 0},
    {0xC6, (uint8_t []){0x99}, 1, 0},
    {0xC7, (uint8_t []){0x01}, 1, 0},
    {0xC8, (uint8_t []){0xBB}, 1, 0},
    {0xC9, (uint8_t []){0xAA}, 1, 0},
    {0xD0, (uint8_t []){0x10}, 1, 0},
    {0xD1, (uint8_t []){0x47}, 1, 0},
    {0xD2, (uint8_t []){0x56}, 1, 0},
    {0xD3, (uint8_t []){0x65}, 1, 0},
    {0xD4, (uint8_t []){0x74}, 1, 0},
    {0xD5, (uint8_t []){0x88}, 1, 0},
    {0xD6, (uint8_t []){0x99}, 1, 0},
    {0xD7, (uint8_t []){0x01}, 1, 0},
    {0xD8, (uint8_t []){0xBB}, 1, 0},
    {0xD9, (uint8_t []){0xAA}, 1, 0},
    {0xF3, (uint8_t []){0x01}, 1, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0},
    {0x21, (uint8_t []){}, 0, 0},
    {0x11, (uint8_t []){}, 0, 0},
    {0x00, (uint8_t []){}, 0, 120},
};
float tsens_value;

class Charge : public I2cDevice {
public:
    Charge(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        read_buffer_ = new uint8_t[8];
    }
    ~Charge() {
        delete[] read_buffer_;
    }
    void Printcharge() {
        ReadRegs(0x08, read_buffer_, 2);        
        ReadRegs(0x0c, read_buffer_ + 2, 2);
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_sensor, &tsens_value));

        // Read voltage and current values (currently unused but available for future use)
        (void)((uint16_t)(read_buffer_[1] << 8 | read_buffer_[0]));  // voltage
        (void)((int16_t)(read_buffer_[3] << 8 | read_buffer_[2]));  // current
    } 
    static void TaskFunction(void *pvParameters) {
        Charge* charge = static_cast<Charge*>(pvParameters);
        while (true) {
            charge->Printcharge();
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }

private:
    uint8_t* read_buffer_ = nullptr;
};


class Cst816s : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };
    Cst816s(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        read_buffer_ = new uint8_t[6];
    }

    ~Cst816s() {
        delete[] read_buffer_;
    }

    void UpdateTouchPoint() {
        ReadRegs(0x02, read_buffer_, 6);
        tp_.num = read_buffer_[0] & 0x0F;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    const TouchPoint_t& GetTouchPoint() {
        return tp_;
    }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;
};
class EspS3Cat : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Cst816s* cst816s_;
    Charge* charge_;
    Button boot_button_;
    LcdDisplay* display_;
    PwmBacklight* backlight_ = nullptr;
    esp_timer_handle_t touchpad_timer_;
    esp_lcd_touch_handle_t tp;   // LCD touch handle
    touch_button_handle_t touch_button_handle_ = nullptr;  // Touch button sensor handle for GPIO7
    static volatile uint32_t touch_event_count_;  // Counter for touch events

    void InitializeI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

        temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
        ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));
        ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));
        
    }

    static void touchpad_timer_callback(void* arg) {
        auto& board = (EspS3Cat&)Board::GetInstance();
        auto touchpad = board.GetTouchpad();
        static bool was_touched = false;
        static int64_t touch_start_time = 0;
        const int64_t TOUCH_THRESHOLD_MS = 500;
        
        touchpad->UpdateTouchPoint();
        auto touch_point = touchpad->GetTouchPoint();
        
        if (touch_point.num > 0 && !was_touched) {
            was_touched = true;
            touch_start_time = esp_timer_get_time() / 1000;
        } 
        else if (touch_point.num == 0 && was_touched) {
            was_touched = false;
            int64_t touch_duration = (esp_timer_get_time() / 1000) - touch_start_time;
            
            if (touch_duration < TOUCH_THRESHOLD_MS) {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting && 
                    !WifiStation::GetInstance().IsConnected()) {
                    board.ResetWifiConfiguration();
                }
                app.ToggleChatState();
            }
        }
    }
    static void touchpad_callback(Cst816s::TouchPoint_t touch_point) {
        auto& board = (EspS3Cat&)Board::GetInstance();
        static bool was_touched = false;
        static int64_t touch_start_time = 0;
        const int64_t TOUCH_THRESHOLD_MS = 500;
        
        if (touch_point.num > 0 && !was_touched) {
            was_touched = true;
            touch_start_time = esp_timer_get_time() / 1000;
        } 
        else if (touch_point.num == 0 && was_touched) {
            was_touched = false;
            int64_t touch_duration = (esp_timer_get_time() / 1000) - touch_start_time;
            
            if (touch_duration < TOUCH_THRESHOLD_MS) {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting && 
                    !WifiStation::GetInstance().IsConnected()) {
                    board.ResetWifiConfiguration();
                }
                app.ToggleChatState();
            }
        }
    }

    static void lvgl_port_touch_isr_cb(void* arg)
    {
        static int64_t touch_start_time = 0;
        static int64_t touch_last_time = 0;
        touch_start_time = esp_timer_get_time() / 1000;
        if (touch_start_time - touch_last_time >= 300) {
            auto& board = (EspS3Cat&)Board::GetInstance();
            auto& app = Application::GetInstance();
                    if (app.GetDeviceState() == kDeviceStateStarting && 
                        !WifiStation::GetInstance().IsConnected()) {
                        board.ResetWifiConfiguration();
                    }
            app.ToggleChatState();
            touch_last_time = touch_start_time;
        }   
    }

    static void touch_log_task(void* arg)
    {
        ESP_LOGI(TAG, "[TOUCH] touch_log_task started");
        EspS3Cat* board = static_cast<EspS3Cat*>(arg);
        if (board == nullptr) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Invalid board pointer in touch_log_task");
            vTaskDelete(NULL);
            return;
        }

        uint32_t last_count = 0;
        ESP_LOGI(TAG, "[TOUCH] touch_log_task entering main loop");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));  // Wait 1 second
            
            uint32_t current_count = EspS3Cat::touch_event_count_;
            if (current_count > last_count) {
                ESP_LOGI(TAG, "[TOUCH] Touch sensor status: Events detected! Total events: %lu (GPIO7)", 
                         (unsigned long)current_count);
                last_count = current_count;
            } else {
                ESP_LOGI(TAG, "[TOUCH] Touch sensor status: No new events (GPIO7), total=%lu", 
                         (unsigned long)current_count);
            }
        }
    }

    static void touch_button_event_task(void* arg)
    {
        ESP_LOGI(TAG, "[TOUCH] touch_button_event_task started");
        EspS3Cat* board = static_cast<EspS3Cat*>(arg);
        if (board == nullptr) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Invalid board pointer in touch_button_event_task");
            vTaskDelete(NULL);
            return;
        }

        ESP_LOGI(TAG, "[TOUCH] touch_button_event_task handle=%p", (void*)board->touch_button_handle_);
        ESP_LOGI(TAG, "[TOUCH] touch_button_event_task entering main loop");
        while (true) {
            if (board->touch_button_handle_ != nullptr) {
                touch_button_sensor_handle_events(board->touch_button_handle_);
            }
            vTaskDelay(pdMS_TO_TICKS(10));  // Check every 10ms
        }
    }

    void InitializeTouchButton()
    {
        ESP_LOGI(TAG, "[TOUCH] ===== Starting touch button initialization =====");
        ESP_LOGI(TAG, "[TOUCH] GPIO7 -> Touch Channel 7");
        ESP_LOGI(TAG, "[TOUCH] Threshold: %.3f", LIGHT_TOUCH_THRESHOLD);
        
        // Step 1: Prepare channel configuration
        ESP_LOGI(TAG, "[TOUCH] Step 1: Preparing channel configuration");
        uint32_t touch_channel_list[] = {TOUCH_CHANNEL_1};  // GPIO7 = Channel 7
        ESP_LOGI(TAG, "[TOUCH] Channel list: [%lu]", (unsigned long)touch_channel_list[0]);
        
        touch_lowlevel_type_t *channel_type = (touch_lowlevel_type_t*)calloc(1, sizeof(touch_lowlevel_type_t));
        if (channel_type == NULL) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Memory allocation failed for channel_type");
            return;
        }
        channel_type[0] = TOUCH_LOWLEVEL_TYPE_TOUCH;
        ESP_LOGI(TAG, "[TOUCH] Channel type allocated and set to TOUCH");
        
        // Step 2: Create low-level touch sensor
        ESP_LOGI(TAG, "[TOUCH] Step 2: Creating low-level touch sensor");
        touch_lowlevel_config_t low_config = {
            .channel_num = 1,
            .channel_list = touch_channel_list,
            .channel_type = channel_type,
        };
        ESP_LOGI(TAG, "[TOUCH] Low-level config: channel_num=%d", low_config.channel_num);
        
        esp_err_t ret = touch_sensor_lowlevel_create(&low_config);
        ESP_LOGI(TAG, "[TOUCH] touch_sensor_lowlevel_create() returned: %d (%s)", ret, esp_err_to_name(ret));
        free(channel_type);
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Touch sensor lowlevel create failed: %d (%s)", ret, esp_err_to_name(ret));
            return;
        }
        ESP_LOGI(TAG, "[TOUCH] ✓ Low-level touch sensor created successfully");
        
        // Step 3: Configure touch button sensor
        ESP_LOGI(TAG, "[TOUCH] Step 3: Configuring touch button sensor");
        float channel_threshold[] = {LIGHT_TOUCH_THRESHOLD};
        
        touch_button_config_t touch_cfg = {
            .channel_num = 1,
            .channel_list = touch_channel_list,
            .channel_threshold = channel_threshold,
            .channel_gold_value = NULL,
            .debounce_times = 1,
            .skip_lowlevel_init = true,
        };
        
        ESP_LOGI(TAG, "[TOUCH] Touch button config: channel_num=%d, threshold=%.3f, debounce=%d, skip_lowlevel_init=%d",
                 touch_cfg.channel_num, touch_cfg.channel_threshold[0], touch_cfg.debounce_times, touch_cfg.skip_lowlevel_init);
        
        // Step 4: Create touch button sensor with callback
        ESP_LOGI(TAG, "[TOUCH] Step 4: Creating touch button sensor with callback");
        touch_event_count_ = 0;  // Initialize counter
        ESP_LOGI(TAG, "[TOUCH] Touch event counter initialized to 0");
        
        ret = touch_button_sensor_create(&touch_cfg, &touch_button_handle_, 
                                         [](touch_button_handle_t handle, uint32_t channel, touch_state_t state, void *cb_arg) {
                                             // Lightweight callback - increment counter and log
                                             EspS3Cat::touch_event_count_++;  // Increment on any touch event
                                             
                                             if (state == TOUCH_STATE_ACTIVE) {
                                                 ESP_LOGI(TAG, "[TOUCH] Touch button pressed (channel %lu)", (unsigned long)channel);
                                             } else {
                                                 ESP_LOGI(TAG, "[TOUCH] Touch button released (channel %lu)", (unsigned long)channel);
                                             }
                                         }, this);
        
        ESP_LOGI(TAG, "[TOUCH] touch_button_sensor_create() returned: %d (%s)", ret, esp_err_to_name(ret));
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Create touch button sensor failed: %d (%s)", ret, esp_err_to_name(ret));
            return;
        }
        ESP_LOGI(TAG, "[TOUCH] ✓ Touch button sensor created successfully, handle=%p", (void*)touch_button_handle_);
        
        // Create periodic logging task
        ESP_LOGI(TAG, "[TOUCH] Creating periodic logging task");
        BaseType_t task_ret1 = xTaskCreatePinnedToCore(touch_log_task, "touch_log_task", 2 * 1024, this, 1, NULL, 1);
        ESP_LOGI(TAG, "[TOUCH] touch_log_task creation result: %d", task_ret1);
        if (task_ret1 == pdPASS) {
            ESP_LOGI(TAG, "[TOUCH] ✓ touch_log_task created successfully");
        } else {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Failed to create touch_log_task");
        }
        
        // Create task to handle touch button sensor events
        ESP_LOGI(TAG, "[TOUCH] Creating touch button event handling task");
        BaseType_t task_ret2 = xTaskCreatePinnedToCore(touch_button_event_task, "touch_btn_task", 4 * 1024, this, 5, NULL, 1);
        ESP_LOGI(TAG, "[TOUCH] touch_button_event_task creation result: %d", task_ret2);
        if (task_ret2 == pdPASS) {
            ESP_LOGI(TAG, "[TOUCH] ✓ touch_button_event_task created successfully");
        } else {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Failed to create touch_button_event_task");
        }
        
        // Step 5: Start touch sensor
        ESP_LOGI(TAG, "[TOUCH] Step 5: Starting touch sensor");
        ret = touch_sensor_lowlevel_start();
        ESP_LOGI(TAG, "[TOUCH] touch_sensor_lowlevel_start() returned: %d (%s)", ret, esp_err_to_name(ret));
        
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[TOUCH] ERROR: Touch sensor start failed: %d (%s)", ret, esp_err_to_name(ret));
            return;
        }
        
        ESP_LOGI(TAG, "[TOUCH] ===== Touch button initialization complete =====");
        ESP_LOGI(TAG, "[TOUCH] Touch sensor is now active and monitoring GPIO7");
    }

    void InitializeCharge() {
        charge_ = new Charge(i2c_bus_, 0x55);
        xTaskCreatePinnedToCore(Charge::TaskFunction, "batterydecTask", 3 * 1024, charge_, 6, NULL, 0);
    }

    void InitializeCst816sTouchPad() {
        cst816s_ = new Cst816s(i2c_bus_, 0x15);
        
        const gpio_config_t int_gpio_config = {
            .pin_bit_mask = (1ULL << TP_PIN_NUM_INT),
            .mode = GPIO_MODE_INPUT,
            .intr_type = GPIO_INTR_NEGEDGE
        };
        gpio_config(&int_gpio_config);
        gpio_install_isr_service(0);
        gpio_intr_enable(TP_PIN_NUM_INT);
        gpio_isr_handler_add(TP_PIN_NUM_INT, EspS3Cat::lvgl_port_touch_isr_cb, NULL);
    }

    void InitializeSpi() {
        const spi_bus_config_t bus_config = TAIJIPI_ST77916_PANEL_BUS_QSPI_CONFIG(QSPI_PIN_NUM_LCD_PCLK,
                                                                        QSPI_PIN_NUM_LCD_DATA0,
                                                                        QSPI_PIN_NUM_LCD_DATA1,
                                                                        QSPI_PIN_NUM_LCD_DATA2,
                                                                        QSPI_PIN_NUM_LCD_DATA3,
                                                                        QSPI_LCD_H_RES * 80 * sizeof(uint16_t));
        ESP_ERROR_CHECK(spi_bus_initialize(QSPI_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void Initializest77916Display() {

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        const esp_lcd_panel_io_spi_config_t io_config = ST77916_PANEL_IO_QSPI_CONFIG(QSPI_PIN_NUM_LCD_CS, NULL, NULL);
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)QSPI_LCD_HOST, &io_config, &panel_io));
        st77916_vendor_config_t vendor_config = {
            .init_cmds = vendor_specific_init_yysj,
            .init_cmds_size = sizeof(vendor_specific_init_yysj) / sizeof(st77916_lcd_init_cmd_t),
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
        ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_disp_on_off(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_20_4,
                                        .emoji_font = font_emoji_64_init(),
                                    });
        // Rotate display 180° (upside down)
        display_->SetDisplayRotation180(true);
        backlight_ = new PwmBacklight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        backlight_->RestoreBrightness();
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
         gpio_config_t power_gpio_config = {
            .pin_bit_mask = (BIT64(POWER_CTRL) ),
            .mode = GPIO_MODE_OUTPUT,
            
      };
        ESP_ERROR_CHECK(gpio_config(&power_gpio_config));

        gpio_set_level(POWER_CTRL, 0);
    }

public:
    EspS3Cat() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeCharge();
        InitializeCst816sTouchPad();
        
        InitializeSpi();
        Initializest77916Display();
        InitializeButtons();
        ESP_LOGI(TAG, "[TOUCH] About to call InitializeTouchButton()");
        InitializeTouchButton();
        ESP_LOGI(TAG, "[TOUCH] InitializeTouchButton() returned");
        
        // Initialize SD card BEFORE animations to ensure it's available for animation loading
        ESP_LOGI(TAG, "Initializing SD card before animations...");
        esp_err_t ret = SdCard::Initialize();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "SD card initialized successfully before animations");
        } else {
            ESP_LOGW(TAG, "SD card initialization failed before animations: %s", esp_err_to_name(ret));
            ESP_LOGW(TAG, "Animations will not be loaded from SD card");
        }
        
        // Initialize animations (will try SD card if available, otherwise animations won't be loaded)
        ESP_LOGI(TAG, "=== Initializing animations ===");
        animation_init();  // This will try SD card first, animations won't be available if SD card fails
        ESP_LOGI(TAG, "=== Animations initialization completed ===");
    }

    virtual AudioCodec* GetAudioCodec() override {
        static BoxAudioCodec audio_codec(
            i2c_bus_, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, 
            AUDIO_CODEC_ES8311_ADDR, 
            AUDIO_CODEC_ES7210_ADDR, 
            AUDIO_INPUT_REFERENCE);
        return &audio_codec;
    }
    
    virtual Display* GetDisplay() override {
        return display_;
    }

    Cst816s* GetTouchpad() {
        return cst816s_;
    }

    virtual Backlight* GetBacklight() override {
        return backlight_;
    }
};

// Static member variable definition
volatile uint32_t EspS3Cat::touch_event_count_ = 0;

DECLARE_BOARD(EspS3Cat);