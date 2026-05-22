#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

class Display;

typedef enum {
    FACTORY_TEST_POWER = 0,
    FACTORY_TEST_MEMORY,
    FACTORY_TEST_I2C_BUS,
    FACTORY_TEST_DISPLAY,
    FACTORY_TEST_VOLUME,
    FACTORY_TEST_BRIGHTNESS,
    FACTORY_TEST_BMI270,
    FACTORY_TEST_CODEC,
    FACTORY_TEST_BLUETOOTH,
    FACTORY_TEST_WIFI,
    FACTORY_TEST_RTC,
    FACTORY_TEST_TOUCH,
    FACTORY_TEST_BATTERY,
    FACTORY_TEST_AUDIO,
    FACTORY_TEST_SD_CARD,
    FACTORY_TEST_COUNT
} factory_test_id_t;

typedef enum {
    FACTORY_TEST_STATE_PENDING = 0,
    FACTORY_TEST_STATE_RUNNING,
    FACTORY_TEST_STATE_PASS,
    FACTORY_TEST_STATE_FAIL,
} factory_test_state_t;

typedef struct {
    bool touch_triggered;
    bool volume_changed;
    bool brightness_changed;
    bool audio_executed;
} factory_required_actions_t;

namespace FactoryTest {
bool ShouldEnterFactoryMode();
bool IsFactoryTestMode();
void SetFactoryMode(bool enabled);
bool MaybeLatchFactoryModeFromBootButton();

bool GetWifiMac(char* out, size_t out_size);
bool GetBtMac(char* out, size_t out_size);
bool BuildQrPayload(char* out, size_t out_size, const char* fw_tag, const char* hw_tag);

void TestsInit();
void TestsStartAuto();
void TestsPoll();
void SetTestState(factory_test_id_t id, factory_test_state_t state);
factory_test_state_t GetTestState(factory_test_id_t id);
bool AllTestsPassed();
bool CanEnterQrPage();

void MarkTouchTriggered();
void MarkVolumeChanged();
void MarkBrightnessChanged();
void MarkAudioExecuted();
factory_required_actions_t GetRequiredActions();

class Controller {
public:
    static Controller& GetInstance();

    void Start(Display* display, const char* fw_tag, const char* hw_tag);
    void Poll();

    void OnTouchDetected();
    void OnTouchscreenLongPress();
    void OnVolumeChanged(int volume);
    void OnGpioTouchDetected();
    void OnVolumeSwipe(bool up, int volume);
    void OnBrightnessSwipe(bool left, int brightness);
    void OnAudioRecordingStarted();
    void OnAudioPlaybackTriggered();
    void OnAudioPlaybackFinished();
    void OnBmiDotMoved(int x, int y);
    void OnTestFinished(factory_test_id_t id, bool passed, const char* detail);
    void OnWifiTestFinished(bool passed, const char* detail);
    void OnBluetoothTestFinished(bool passed, const char* detail);
    void OnBatterySample(bool passed, int level);
    void OnSdSample(bool present);

    bool IsQrPageVisible() const;
    bool IsDisplayTestActive() const;
    uint8_t GetDisplayTestBacklight() const;
    bool ShouldExitFactoryMode(int64_t held_ms) const;
    void Render();

private:
    Controller() = default;
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
};
} // namespace FactoryTest

inline bool IsFactoryTestMode() {
    return FactoryTest::IsFactoryTestMode();
}
