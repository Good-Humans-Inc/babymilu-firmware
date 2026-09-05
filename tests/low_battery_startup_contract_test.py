import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
APPLICATION = (ROOT / "main/application.cc").read_text()
BOARD = (ROOT / "main/boards/common/board.h").read_text()
ECHOEAR = (ROOT / "main/boards/echoear/echoear.cc").read_text()


class LowBatteryStartupContractTest(unittest.TestCase):
    def test_guard_runs_before_audio_and_network_startup(self):
        start = APPLICATION.index("void Application::Start()")
        guard = APPLICATION.index("board.WaitForSafeStartupPower()", start)
        audio = APPLICATION.index("board.GetAudioCodec()", start)
        network = APPLICATION.index("board.StartNetwork()", start)
        self.assertLess(guard, audio)
        self.assertLess(guard, network)
        self.assertIn("virtual void WaitForSafeStartupPower() {}", BOARD)

    def test_guard_requires_confirmed_low_samples_and_hysteresis(self):
        self.assertIn("kCriticalStartupVoltageMv = 3060", ECHOEAR)
        self.assertIn("kStartupRecoveryVoltageMv = 3180", ECHOEAR)
        self.assertIn("kCriticalSamplesRequired = 4", ECHOEAR)
        self.assertIn("kRecoverySamplesRequired = 3", ECHOEAR)
        self.assertIn("critical_samples < kCriticalSamplesRequired", ECHOEAR)
        self.assertIn("voltage_mv >= kStartupRecoveryVoltageMv", ECHOEAR)

    def test_guard_warns_without_rebooting_and_bad_reads_are_not_zero_percent(self):
        start = ECHOEAR.index("void WaitForSafeStartupPower() override")
        end = ECHOEAR.index("virtual void StartNetwork() override", start)
        guard = ECHOEAR[start:end]
        self.assertIn("Battery too low", ECHOEAR)
        self.assertIn("Please plug in charger", ECHOEAR)
        self.assertNotIn("esp_restart", guard)
        self.assertIn("voltage_mv == 0", ECHOEAR)
        self.assertIn("return false", ECHOEAR)

    def test_runtime_guard_pauses_heavy_work_and_recovers_with_hysteresis(self):
        self.assertIn("kRuntimeCriticalSamplesRequired = 3", ECHOEAR)
        self.assertIn("runtime_low_battery_protected_", ECHOEAR)
        self.assertIn("SetLowBatteryPaused(true)", ECHOEAR)
        self.assertIn("kDeviceStateLowBattery", APPLICATION)
        self.assertIn("voltage_mv >= kStartupRecoveryVoltageMv", ECHOEAR)
        self.assertIn("SetLowBatteryPaused(false)", ECHOEAR)


if __name__ == "__main__":
    unittest.main()
