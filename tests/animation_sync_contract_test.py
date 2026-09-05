import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
UPDATER = (ROOT / "main/animation/animation_updater.cc").read_text()
MQTT = (ROOT / "main/protocols/mqtt_protocol.cc").read_text()
ESP_MQTT = (ROOT / "managed_components/78__esp-ml307/esp_mqtt.cc").read_text()


class AnimationSyncContractTest(unittest.TestCase):
    def test_uses_one_stable_device_bin_and_sha_sidecar(self):
        self.assertIn("milu-public-new/device_bin/", UPDATER)
        self.assertIn('url + ".sha256"', UPDATER)
        self.assertNotIn("storage.googleapis.com/milu-public/device_bin/", UPDATER)

    def test_mqtt_passes_asset_url_and_sha_and_reconciles_on_connect(self):
        self.assertIn('GetStringField(root, "assetUrl")', MQTT)
        self.assertIn('GetStringField(root, "sha256")', MQTT)
        self.assertIn("PrepareRemoteUpdate", MQTT)
        self.assertIn("TriggerUpdateLoop", MQTT)
        self.assertIn("on_connected_callback_();", ESP_MQTT)
        self.assertIn("on_disconnected_callback_();", ESP_MQTT)
        self.assertNotIn("Attempting initial subscription", MQTT)

    def test_overlapping_triggers_are_coalesced_and_failures_retry_with_a_bound(self):
        self.assertIn("compare_exchange_strong", UPDATER)
        self.assertIn("rerun_requested_", UPDATER)
        self.assertIn("ANIMATION_UPDATE_MAX_RETRIES", UPDATER)
        self.assertIn("Animation update retry budget exhausted", UPDATER)

    def test_installed_sha_is_reported_after_mqtt_reconnect(self):
        self.assertIn('"animation_sync_status"', MQTT)
        self.assertIn('"applied"', MQTT)
        self.assertIn('"installedSha256"', MQTT)
        self.assertIn("GetInstalledAnimationSha256", MQTT)
        self.assertIn("PublishAnimationSyncStatus();", MQTT)

    def test_download_is_verified_before_atomic_install(self):
        self.assertIn('const char* download_path = "/sdcard/test.tmp"', UPDATER)
        self.assertIn("GetLocalSha256(download_path, downloaded_sha256)", UPDATER)
        self.assertIn('const char* backup_path = "/sdcard/test.bak"', UPDATER)

    def test_sha_is_authoritative_and_interrupted_install_recovers(self):
        self.assertIn("Animation SHA sidecar unavailable or invalid", UPDATER)
        self.assertIn("downloaded_sha256 != expected_sha256", UPDATER)
        active_loop = UPDATER[UPDATER.index("AnimationUpdater::UpdateResult AnimationUpdater::UpdateLoop()") :]
        active_loop = active_loop[: active_loop.index("// COMMENTED OUT: Size comparison")]
        self.assertNotIn("FinishUpdateTask(true)", active_loop)
        self.assertIn("RecoverInterruptedInstall", UPDATER)
        self.assertIn('rename(kBackupPath, kFinalPath)', UPDATER)

    def test_task_cleanup_happens_after_update_loop_returns(self):
        loop = UPDATER[UPDATER.index("AnimationUpdater::UpdateResult AnimationUpdater::UpdateLoop()") :]
        loop = loop[: loop.index("// Original HTTP server checking logic")]
        self.assertNotIn("FinishUpdateTask", loop)
        self.assertNotIn("esp_restart", loop)
        self.assertIn("const UpdateResult result = updater->UpdateLoop();", UPDATER)

    def test_retry_does_not_run_network_work_on_timer_service_stack(self):
        self.assertIn('xTaskCreate(RetryTask, "anim_retry", 2048', UPDATER)
        self.assertNotIn("RetryTimerCallback", UPDATER)


if __name__ == "__main__":
    unittest.main()
