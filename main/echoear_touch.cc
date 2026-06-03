#include "echoear_touch.h"

#include "config.h"
#include "i2c_device.h"

#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <touch_button_sensor.h>
#include <touch_sensor_lowlevel.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#define TAG "EchoEarTouch"

namespace {

constexpr uint32_t kGpio7TouchChannel = 7;
constexpr float kGpio7TouchThreshold = 0.01f;
constexpr uint8_t kCst816sAddress = 0x15;
constexpr gpio_num_t kCstInterruptGpio = GPIO_NUM_10;
constexpr int kMinSwipePx = 35;
constexpr int kAxisRatioNum = 3;
constexpr int kAxisRatioDen = 2;
constexpr int kLongPressMs = 800;
constexpr int kLongPressDriftPx = 20;
constexpr int kCooldownMs = 250;
constexpr int kGestureStep = 10;

}  // namespace

struct TouchPoint {
    int num = 0;
    int x = -1;
    int y = -1;
};

class Cst816sDevice : public I2cDevice {
public:
    Cst816sDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0xFA, 0x01);
    }

    TouchPoint ReadTouchPoint() {
        uint8_t data[6] = {};
        TouchPoint point;
        if (TryReadRegs(0x02, data, sizeof(data)) != ESP_OK) {
            return point;
        }
        point.num = data[0] & 0x0F;
        point.x = ((data[1] & 0x0F) << 8) | data[2];
        point.y = ((data[3] & 0x0F) << 8) | data[4];
        return point;
    }
};

namespace {

int AbsInt(int value) {
    return value < 0 ? -value : value;
}

int ClampToDisplay(int value, int max_value) {
    if (value < 0) {
        return 0;
    }
    if (value >= max_value) {
        return max_value - 1;
    }
    return value;
}

}  // namespace

struct EchoEarTouchController::GpioTouchEvent {
    uint32_t channel = 0;
    int state = 0;
};

EchoEarTouchController::~EchoEarTouchController() {
    delete cst_;
}

esp_err_t EchoEarTouchController::Init(i2c_master_bus_handle_t i2c_bus, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    esp_err_t gpio_err = InitGpio7Touch();
    esp_err_t cst_err = InitCst816sTouch(i2c_bus);
    return gpio_err == ESP_OK ? cst_err : gpio_err;
}

void EchoEarTouchController::SetInteractionState(bool audio_busy, bool sleep_active) {
    audio_busy_.store(audio_busy);
    sleep_active_.store(sleep_active);
}

esp_err_t EchoEarTouchController::InitGpio7Touch() {
    ESP_LOGI(TAG, "[TOUCH] Initializing GPIO7 capacitive touch channel=%lu threshold=%.3f",
             static_cast<unsigned long>(kGpio7TouchChannel), static_cast<double>(kGpio7TouchThreshold));

    uint32_t touch_channel_list[] = {kGpio7TouchChannel};
    touch_lowlevel_type_t* channel_type = static_cast<touch_lowlevel_type_t*>(calloc(1, sizeof(touch_lowlevel_type_t)));
    if (channel_type == nullptr) {
        ESP_LOGE(TAG, "[TOUCH] Failed to allocate GPIO7 touch channel type");
        return ESP_ERR_NO_MEM;
    }
    channel_type[0] = TOUCH_LOWLEVEL_TYPE_TOUCH;
    touch_lowlevel_config_t low_config = {};
    low_config.channel_num = 1;
    low_config.channel_list = touch_channel_list;
    low_config.channel_type = channel_type;
    esp_err_t ret = touch_sensor_lowlevel_create(&low_config);
    free(channel_type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[TOUCH] GPIO7 low-level touch create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    float channel_threshold[] = {kGpio7TouchThreshold};
    touch_button_config_t touch_cfg = {};
    touch_cfg.channel_num = 1;
    touch_cfg.channel_list = touch_channel_list;
    touch_cfg.channel_threshold = channel_threshold;
    touch_cfg.channel_gold_value = nullptr;
    touch_cfg.debounce_times = 1;
    touch_cfg.skip_lowlevel_init = true;

    gpio7_app_queue_ = xQueueCreate(5, sizeof(GpioTouchEvent));
    if (gpio7_app_queue_ == nullptr) {
        ESP_LOGE(TAG, "[TOUCH] Failed to create GPIO7 app queue");
        return ESP_ERR_NO_MEM;
    }

    touch_button_handle_t handle = nullptr;
    ret = touch_button_sensor_create(&touch_cfg, &handle,
                                     [](touch_button_handle_t button, uint32_t channel, touch_state_t state, void* arg) {
                                         (void)button;
                                         static_cast<EchoEarTouchController*>(arg)->QueueGpio7Event(channel, static_cast<int>(state));
                                     },
                                     this);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[TOUCH] GPIO7 touch button create failed: %s", esp_err_to_name(ret));
        return ret;
    }
    gpio7_touch_handle_ = handle;

    BaseType_t task_ok = xTaskCreatePinnedToCore(Gpio7SensorTaskEntry, "touch_btn_task", 4096, this, 4, &gpio7_sensor_task_, 1);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "[TOUCH] Failed to create GPIO7 sensor task");
        return ESP_FAIL;
    }
    task_ok = xTaskCreatePinnedToCore(Gpio7AppTaskEntry, "touch_btn_app", 4096, this, 3, &gpio7_app_task_, 1);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "[TOUCH] Failed to create GPIO7 app task");
        return ESP_FAIL;
    }

    ret = touch_sensor_lowlevel_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[TOUCH] GPIO7 touch start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "[TOUCH] GPIO7 touch ready; app work is queued outside the sensor callback");
    return ESP_OK;
}

esp_err_t EchoEarTouchController::InitCst816sTouch(i2c_master_bus_handle_t i2c_bus) {
    if (i2c_bus == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t probe = i2c_master_probe(i2c_bus, kCst816sAddress, 100);
    if (probe != ESP_OK) {
        ESP_LOGW(TAG, "[TOUCH] CST816S not detected at 0x%02x: %s", kCst816sAddress, esp_err_to_name(probe));
        return ESP_OK;
    }

    cst_ = new Cst816sDevice(i2c_bus, kCst816sAddress);
    cst_event_queue_ = xQueueCreate(5, sizeof(uint32_t));
    if (cst_event_queue_ == nullptr) {
        ESP_LOGE(TAG, "[TOUCH] Failed to create CST816S event queue");
        delete cst_;
        cst_ = nullptr;
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_ok = xTaskCreatePinnedToCore(CstTouchTaskEntry, "cst_touch", 4096, this, 4, &cst_task_, 1);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "[TOUCH] Failed to create CST816S touch task");
        delete cst_;
        cst_ = nullptr;
        return ESP_FAIL;
    }

    gpio_config_t gpio_cfg = {};
    gpio_cfg.pin_bit_mask = BIT64(kCstInterruptGpio);
    gpio_cfg.mode = GPIO_MODE_INPUT;
    gpio_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_cfg.intr_type = GPIO_INTR_NEGEDGE;
    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));
    esp_err_t isr = gpio_install_isr_service(0);
    if (isr != ESP_OK && isr != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "[TOUCH] gpio_install_isr_service failed: %s", esp_err_to_name(isr));
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(kCstInterruptGpio, CstIsrHandler, cst_event_queue_));
    ESP_ERROR_CHECK(gpio_intr_enable(kCstInterruptGpio));
    ESP_LOGI(TAG, "[TOUCH] CST816S screen touch ready");
    return ESP_OK;
}

void EchoEarTouchController::QueueGpio7Event(uint32_t channel, int state) {
    if (audio_busy_.load() || sleep_active_.load() || gpio7_app_queue_ == nullptr) {
        return;
    }
    GpioTouchEvent event = {
        .channel = channel,
        .state = state,
    };
    xQueueSend(gpio7_app_queue_, &event, 0);
}

void EchoEarTouchController::Gpio7SensorTaskEntry(void* arg) {
    static_cast<EchoEarTouchController*>(arg)->Gpio7SensorTask();
}

void EchoEarTouchController::Gpio7AppTaskEntry(void* arg) {
    static_cast<EchoEarTouchController*>(arg)->Gpio7AppTask();
}

void EchoEarTouchController::CstTouchTaskEntry(void* arg) {
    static_cast<EchoEarTouchController*>(arg)->CstTouchTask();
}

void EchoEarTouchController::CstIsrHandler(void* arg) {
    auto queue = static_cast<QueueHandle_t>(arg);
    uint32_t event = 1;
    BaseType_t higher_priority_task_woken = pdFALSE;
    if (queue != nullptr) {
        xQueueSendFromISR(queue, &event, &higher_priority_task_woken);
    }
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void EchoEarTouchController::Gpio7SensorTask() {
    while (true) {
        if (!audio_busy_.load() && !sleep_active_.load() && gpio7_touch_handle_ != nullptr) {
            touch_button_sensor_handle_events(static_cast<touch_button_handle_t>(gpio7_touch_handle_));
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void EchoEarTouchController::Gpio7AppTask() {
    const char* emotions[] = {"angry", "happy", "embarressed"};
    const uint32_t durations_ms[] = {2000, 2000, 1500};
    GpioTouchEvent event;
    while (true) {
        if (xQueueReceive(gpio7_app_queue_, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (audio_busy_.load() || sleep_active_.load()) {
            continue;
        }
        if (event.state == TOUCH_STATE_ACTIVE && callbacks_.on_gpio7_emotion) {
            uint32_t index = esp_random() % 3;
            callbacks_.on_gpio7_emotion(emotions[index], durations_ms[index]);
            ESP_LOGI(TAG, "[TOUCH] GPIO7 selected emotion=%s channel=%lu",
                     emotions[index], static_cast<unsigned long>(event.channel));
        }
    }
}

void EchoEarTouchController::CstTouchTask() {
    bool in_stroke = false;
    int x0 = 0;
    int y0 = 0;
    int last_x = 0;
    int last_y = 0;
    int max_drift = 0;
    int64_t t0_ms = 0;
    int64_t last_fire_ms = 0;

    uint32_t dummy = 0;
    while (true) {
        if (xQueueReceive(cst_event_queue_, &dummy, portMAX_DELAY) != pdTRUE || cst_ == nullptr) {
            continue;
        }

        TouchPoint point = cst_->ReadTouchPoint();
        const int64_t now_ms = esp_timer_get_time() / 1000;
        if (point.num > 0 && point.x >= 0 && point.y >= 0) {
            int x = ClampToDisplay(point.x, DISPLAY_WIDTH);
            int y = ClampToDisplay(point.y, DISPLAY_HEIGHT);
            if (!in_stroke) {
                in_stroke = true;
                x0 = last_x = x;
                y0 = last_y = y;
                max_drift = 0;
                t0_ms = now_ms;
                if (sleep_active_.load()) {
                    ESP_LOGI(TAG, "[TOUCH] CST816S press during sleep; staying asleep");
                } else if (callbacks_.on_activity) {
                    callbacks_.on_activity();
                }
            } else {
                max_drift = std::max(max_drift, std::max(AbsInt(x - x0), AbsInt(y - y0)));
                last_x = x;
                last_y = y;
            }
            continue;
        }

        if (!in_stroke) {
            continue;
        }
        in_stroke = false;
        const int dx = last_x - x0;
        const int dy = last_y - y0;
        const int adx = AbsInt(dx);
        const int ady = AbsInt(dy);
        const int duration_ms = static_cast<int>(now_ms - t0_ms);
        const bool sleep_mode = sleep_active_.load();

        if (now_ms - last_fire_ms < kCooldownMs) {
            continue;
        }

        if (duration_ms >= kLongPressMs && max_drift < kLongPressDriftPx) {
            if (sleep_mode) {
                ESP_LOGI(TAG, "[TOUCH] CST816S long press ignored during sleep");
            } else {
                ESP_LOGI(TAG, "[TOUCH] CST816S long press status requested");
                if (callbacks_.on_status_request) {
                    callbacks_.on_status_request();
                }
            }
            last_fire_ms = now_ms;
            continue;
        }

        if (adx < kMinSwipePx && ady < kMinSwipePx) {
            if (sleep_mode) {
                ESP_LOGI(TAG, "[TOUCH] CST816S tap ignored during sleep");
            }
            continue;
        }

        int axis = 0;
        int dir = 0;
        if (adx * kAxisRatioDen >= ady * kAxisRatioNum) {
            axis = 1;
            dir = dx > 0 ? 1 : -1;
        } else if (ady * kAxisRatioDen >= adx * kAxisRatioNum) {
            axis = 2;
            dir = dy > 0 ? 1 : -1;
        } else {
            ESP_LOGI(TAG, "[TOUCH] CST816S diagonal swipe ignored");
            continue;
        }

        const int delta = (dir > 0 ? kGestureStep : -kGestureStep);
        if (axis == 2 && callbacks_.adjust_volume) {
            int value = callbacks_.adjust_volume(delta);
            ESP_LOGI(TAG, "[TOUCH] CST816S %s volume delta=%d value=%d sleep=%d",
                     dir > 0 ? "down" : "up", delta, value, sleep_mode);
        } else if (axis == 1 && callbacks_.adjust_brightness) {
            const int brightness_delta = -delta;
            int value = callbacks_.adjust_brightness(brightness_delta);
            ESP_LOGI(TAG, "[TOUCH] CST816S %s brightness delta=%d value=%d sleep=%d",
                     dir > 0 ? "right" : "left", brightness_delta, value, sleep_mode);
        }
        last_fire_ms = now_ms;
    }
}
