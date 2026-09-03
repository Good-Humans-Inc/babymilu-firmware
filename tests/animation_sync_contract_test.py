import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
UPDATER = (ROOT / "main/animation/animation_updater.cc").read_text()
MQTT = (ROOT / "main/protocols/mqtt_protocol.cc").read_text()


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

    def test_download_is_verified_before_atomic_install(self):
        self.assertIn('const char* download_path = "/sdcard/test.bin.download"', UPDATER)
        self.assertIn("GetLocalSha256(download_path, downloaded_sha256)", UPDATER)
        self.assertIn('const char* backup_path = "/sdcard/test.bin.backup"', UPDATER)


if __name__ == "__main__":
    unittest.main()
