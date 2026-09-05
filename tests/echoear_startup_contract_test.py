#!/usr/bin/env python3
"""Host-side regression checks for EchoEar startup resource ordering."""

from pathlib import Path
import json
import unittest


ROOT = Path(__file__).resolve().parents[1]
APPLICATION = (ROOT / "main/application.cc").read_text(encoding="utf-8")
ECHOEAR = (ROOT / "main/boards/echoear/echoear.cc").read_text(encoding="utf-8")
ECHOEAR_CONFIG = json.loads(
    (ROOT / "main/boards/echoear/config.json").read_text(encoding="utf-8")
)
NO_AUDIO_PROCESSOR = (
    ROOT / "main/audio_processing/no_audio_processor.cc"
).read_text(encoding="utf-8")
WIFI_BOARD = (ROOT / "main/boards/common/wifi_board.cc").read_text(encoding="utf-8")
LCD_DISPLAY = (ROOT / "main/display/lcd_display.cc").read_text(encoding="utf-8")
WIFI_STATION = (
    ROOT / "managed_components/78__esp-wifi-connect/wifi_station.cc"
).read_text(encoding="utf-8")
RELEASE_SCRIPT = (ROOT / "scripts/release.py").read_text(encoding="utf-8")
FIRMWARE_SOURCE = "\n".join(
    path.read_text(encoding="utf-8", errors="ignore")
    for path in (ROOT / "main").rglob("*.cc")
)


class EchoEarStartupContractTest(unittest.TestCase):
    def test_firmware_does_not_read_firestore_directly(self) -> None:
        self.assertNotIn("firestore.googleapis.com", FIRMWARE_SOURCE)
        self.assertNotIn("databases/(default)", FIRMWARE_SOURCE)
        self.assertNotIn("[FIRESTORE]", ECHOEAR)

    def test_echoear_uses_lightweight_raw_audio_path(self) -> None:
        build = next(
            item for item in ECHOEAR_CONFIG["builds"] if item["name"] == "EchoEar"
        )
        self.assertIn(
            "# CONFIG_USE_AFE_WAKE_WORD is not set", build["sdkconfig_append"]
        )
        self.assertIn(
            "# CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS is not set",
            build["sdkconfig_append"],
        )
        self.assertIn(
            "# CONFIG_USE_AUDIO_PROCESSOR is not set", build["sdkconfig_append"]
        )
        self.assertIn(
            "# CONFIG_USE_DEVICE_AEC is not set", build["sdkconfig_append"]
        )
        self.assertIn("microphone[i] = data[j]", NO_AUDIO_PROCESSOR)
        self.assertIn("kFrameDurationMs = 60", NO_AUDIO_PROCESSOR)

    def test_audio_runtime_is_initialized_before_transient_network_jobs(self) -> None:
        audio_processor = APPLICATION.index("audio_processor_->Initialize(codec);")
        wake_word = APPLICATION.index("wake_word_->Initialize(codec);")
        release_assets = APPLICATION.index("board.ReleaseDeferredStartupResources();")
        wifi_start = APPLICATION.index("board.StartNetwork();")
        error_upload = APPLICATION.index("ErrorLogUploader::UploadErrorLog()")
        ota_check = APPLICATION.index("CheckNewVersion();")
        animation_update = APPLICATION.index("animation_updater.TriggerUpdateLoop();")
        protocol_start = APPLICATION.index("protocol_->Start();")

        self.assertLess(audio_processor, wifi_start)
        self.assertLess(wake_word, wifi_start)
        self.assertLess(wifi_start, release_assets)
        self.assertLess(audio_processor, error_upload)
        self.assertLess(wake_word, error_upload)
        self.assertLess(audio_processor, ota_check)
        self.assertLess(wake_word, ota_check)
        self.assertLess(audio_processor, animation_update)
        self.assertLess(wake_word, animation_update)
        self.assertLess(audio_processor, protocol_start)
        self.assertLess(wake_word, protocol_start)
        self.assertEqual(APPLICATION.count("audio_processor_->Initialize(codec);"), 1)
        self.assertEqual(APPLICATION.count("wake_word_->Initialize(codec);"), 1)

    def test_wifi_allocation_failure_falls_back_without_rebooting(self) -> None:
        self.assertNotIn("ESP_ERROR_CHECK(esp_wifi_init", WIFI_STATION)
        self.assertIn("WiFi initialization failed without rebooting", WIFI_STATION)
        self.assertIn("if (!initialized_)", WIFI_STATION)
        self.assertIn("const bool wifi_started = wifi_station.Start();", WIFI_BOARD)
        self.assertIn(
            "if (!wifi_started || !wifi_station.WaitForConnected", WIFI_BOARD
        )

    def test_echoear_release_build_accepts_product_name_case(self) -> None:
        self.assertIn(
            "name.lower().startswith(board_type.lower())", RELEASE_SCRIPT
        )

    def test_connection_banner_is_bounded_and_cleared_when_character_loads(self) -> None:
        self.assertIn(
            "CreateSystemMessage(connected_message, 10000)", WIFI_BOARD
        )
        self.assertIn(
            "CreateSystemMessage(connected_message, 10000)", APPLICATION
        )
        self.assertIn("esp_timer_start_once(system_message_timer_", LCD_DISPLAY)
        self.assertIn(
            '"[SD/ANIM] Character ready; clearing connection banner"', ECHOEAR
        )
        self.assertIn("display->ClearSystemMessages();", ECHOEAR)


if __name__ == "__main__":
    unittest.main()
