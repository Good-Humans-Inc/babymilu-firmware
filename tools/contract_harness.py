#!/usr/bin/env python3
"""Local BabyMilu firmware contract harness.

This validates Stage 1 protocol/config behavior without network, cloud, or
hardware mutations.
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from urllib.parse import parse_qsl, urlencode, urlsplit, urlunsplit


ROOT = Path(__file__).resolve().parents[1]


def normalize_connection_type(value: str | None) -> str:
    return (value or "normal").lower()


def upsert_query(url: str, key: str, value: str) -> str:
    parts = urlsplit(url)
    query = [(k, v) for k, v in parse_qsl(parts.query, keep_blank_values=True) if k != key]
    query.append((key, value))
    return urlunsplit((parts.scheme, parts.netloc, parts.path, urlencode(query), parts.fragment))


def build_ws_url(base: str, device_id: str, client_id: str, connectionType: str) -> str:
    url = upsert_query(base, "device_id", device_id)
    url = upsert_query(url, "client_id", client_id)
    url = upsert_query(url, "connectionType", normalize_connection_type(connectionType))
    return url


def parse_manifest(payload: str) -> dict:
    manifest = json.loads(payload)
    parsed = {
        "activation_ignored": "activation" in manifest,
        "websocket": manifest.get("websocket") or {},
        "mqtt": manifest.get("mqtt") or {},
        "server_time": manifest.get("server_time") or {},
        "firmware": None,
    }
    firmware = manifest.get("firmware")
    if isinstance(firmware, dict) and firmware.get("version") and firmware.get("url"):
        parsed["firmware"] = {
            "version": firmware["version"],
            "url": firmware["url"],
            "force": bool(firmware.get("force", False)),
        }
    return parsed


def hello_payload(device_id: str, client_id: str, connectionType: str) -> dict:
    return {
        "type": "hello",
        "version": 3,
        "transport": "websocket",
        "device_id": device_id,
        "client_id": client_id,
        "connectionType": normalize_connection_type(connectionType),
        "audio_params": {
            "format": "opus",
            "sample_rate": 16000,
            "channels": 1,
            "frame_duration": 60,
        },
    }


def listen_payload(state: str, connectionType: str, session_id: str = "session-1") -> dict:
    return {
        "type": "listen",
        "state": state,
        "session_id": session_id,
        "connectionType": normalize_connection_type(connectionType),
    }


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def validate_defaults() -> None:
    sdkconfig = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
    kconfig = (ROOT / "main" / "Kconfig.projbuild").read_text(encoding="utf-8")
    combined = sdkconfig + "\n" + kconfig
    require("/babymilu/ota/" in combined, "OTA default must use /babymilu/ota/")
    require("/babymilu/v1/" in combined, "WS default must use /babymilu/v1/")
    require(":8000" in combined, "port 8000 default missing")
    for forbidden in ("/xiaozhi/v1/", ":8003", ":8080"):
        require(forbidden not in combined, f"forbidden legacy default found: {forbidden}")
    require("normal_chat" not in combined, "legacy connectionType value alias found in defaults")


def validate_protocol() -> None:
    ws_url = build_ws_url(
        "ws://35.225.248.38:8000/babymilu/v1/?connectionType=alarm",
        "90:e5:b1:d6:fb:0c",
        "echoear-90-e5-b1-d6-fb-0c",
        "reminder",
    )
    require("connectionType=reminder" in ws_url, "connectionType should be upserted")
    require("mode=" not in ws_url and "connection_type=" not in ws_url, "new URLs must not emit mode aliases")
    alias_url = build_ws_url("ws://35.225.248.38:8000/babymilu/v1/", "device-1", "client-1", "morning_alarm")
    require("connectionType=morning_alarm" in alias_url, "legacy value aliases must not be rewritten")

    hello = hello_payload("device-1", "client-1", "reminder")
    require(hello["audio_params"]["format"] == "opus", "hello audio format mismatch")
    require(hello["audio_params"]["sample_rate"] == 16000, "hello sample rate mismatch")
    require(hello["audio_params"]["frame_duration"] == 60, "hello frame duration mismatch")
    require(hello["connectionType"] == "reminder", "hello connectionType mismatch")

    for state in ("start", "stop"):
        listen = listen_payload(state, "alarm")
        require(listen["connectionType"] == "alarm", "listen connectionType mismatch")
        require("mode" not in listen, "listen payload must not emit mode")
        require("connection_type" not in listen, "listen payload must not emit connection_type")


def validate_manifest() -> None:
    parsed = parse_manifest(
        json.dumps(
            {
                "server": "babymilu-voice-server",
                "activation": {"code": "legacy-ignored"},
                "websocket": {"url": "ws://35.225.248.38:8000/babymilu/v1/", "version": 3},
                "mqtt": {
                    "endpoint": "mqtt://35.225.248.38:1883",
                    "keepalive": 60,
                    "client_id": "device-1",
                    "publish_topic": "xiaozhi/device-1/up",
                    "subscribe_topic": "xiaozhi/device-1/down",
                },
                "server_time": {"timestamp": 1780123456789, "timezone_offset": 0},
                "firmware": {
                    "version": "0.1.1",
                    "url": "http://35.225.248.38:8000/firmware/echoear.bin",
                },
            }
        )
    )
    require(parsed["activation_ignored"], "activation field should be ignored, not acted on")
    require(parsed["websocket"]["version"] == 3, "manifest websocket version mismatch")
    require(parsed["mqtt"]["endpoint"].startswith("mqtt://"), "manifest MQTT endpoint mismatch")
    require(parsed["firmware"]["version"] == "0.1.1", "firmware metadata not parsed")

    no_firmware = parse_manifest('{"firmware":{"version":"0.1.2"}}')
    require(no_firmware["firmware"] is None, "partial firmware metadata must not trigger update")


def validate_source_strings() -> None:
    files = [ROOT / "sdkconfig.defaults", ROOT / "main" / "Kconfig.projbuild"]
    files.extend((ROOT / "main").glob("*.cc"))
    files.extend((ROOT / "main").glob("*.h"))
    text = "\n".join(path.read_text(encoding="utf-8", errors="ignore") for path in files)
    for forbidden in ("/xiaozhi/v1/", ":8003", "normal_chat", "morning_alarm", "connection_type"):
        require(forbidden not in text, f"forbidden source string found: {forbidden}")
    emitted_mode = re.findall(r'cJSON_AddStringToObject\(root,\s*"mode"', text)
    require(not emitted_mode, "firmware source must not emit mode in new JSON")


def validate_ble_wifi_contract() -> None:
    ble = (ROOT / "main" / "ble_wifi_provisioner.cc").read_text(encoding="utf-8")
    wifi = (ROOT / "main" / "wifi_manager.cc").read_text(encoding="utf-8")
    sdkconfig = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
    for needle in ('0x0180', '0xFEF4', '0xDEAD', '"ssid:"', '"pwd:"', '"wifi:"', '"BabyMilu"'):
        require(needle in ble or needle in wifi, f"BLE Wi-Fi compatibility token missing: {needle}")
    require("AddLowestPriority" in wifi, "BLE credentials must be saved as lowest priority")
    require('"nxt_boot_ssid"' in wifi, "BLE credential must be preferred once after reboot")
    require('"force_ble_cfg"' in wifi, "BLE reconfigure flag must be consumed")
    require("CONFIG_BT_NIMBLE_ENABLED=y" in sdkconfig, "NimBLE must be enabled in defaults")
    require("CONFIG_BT_CONTROLLER_ENABLED=y" in sdkconfig, "Bluetooth controller must be enabled")
    require("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y" in sdkconfig, "HTTPS certificate bundle must be enabled")


def validate_firestore_contract() -> None:
    wifi = (ROOT / "main" / "wifi_manager.cc").read_text(encoding="utf-8")
    require("https://firestore.googleapis.com" in wifi, "Firestore ranking URL must use HTTPS")
    require("esp_crt_bundle_attach" in wifi, "Firestore HTTPS client must attach ESP cert bundle")
    require('"Accept-Encoding", "identity"' in wifi, "Firestore fetch must request identity encoding")
    require('"Accept", "application/json"' in wifi, "Firestore fetch must request JSON")


def validate_animation_contract() -> None:
    animation = (ROOT / "main" / "animation_store.cc").read_text(encoding="utf-8")
    display = (ROOT / "main" / "display_animator.cc").read_text(encoding="utf-8")
    main_cc = (ROOT / "main" / "main.cc").read_text(encoding="utf-8")
    wifi = (ROOT / "main" / "wifi_manager.cc").read_text(encoding="utf-8")
    config = (ROOT / "main" / "config.h").read_text(encoding="utf-8")
    sdkconfig = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")

    for needle in ("ECHOEAR_STARTUP_GIF_PATH", "ECHOEAR_STARTUP_WAV_PATH", "ECHOEAR_TEST_BIN_PATH"):
        require(needle in config, f"animation path missing from config: {needle}")
    for downloader in (
        "DownloadStartupGifToTempAndSwap",
        "DownloadStartupWavToTempAndSwap",
        "DownloadBundleToTempAndSwap",
    ):
        require(downloader in animation, f"animation updater missing: {downloader}")
    require("PlayStartupWav" in animation and "codec->OutputData" in animation, "startup WAV playback missing")
    require("kAnimationSpecs" in animation, "animation GIF mapping must be table-driven")
    require("LoadFirstAvailable(spec.loop)" in animation, "displayed loop GIFs must lazy-load from test.bin")
    require("LoadFirstAvailable(spec.start, false)" in animation, "displayed start GIFs must lazy-load from test.bin")
    require("heart_loop.gif" in animation and "heart.gif" in animation,
            "loop GIF candidates must support both *_loop.gif and old-firmware *.gif names")
    require("happy.gif" in animation and "happy_start.gif" in animation,
            "happy/heart speaking macro must support happy GIF fallbacks")
    require('"listening", { "listening.gif", "listen.gif"' in animation and "listening_start.gif" not in animation,
            "listening state must always use the listen/listening loop GIF without a start transition")
    for token in ("happy", "hearty", "speaking", "talking", "tts", "idle",
                  "embarressed", "sleepy", "relaxed", "no_wifi", "low_battery"):
        require(f'"{token}"' in animation, f"old-firmware emotion alias missing: {token}")
    for token in ("❤️‍🩹", "❤️‍🔥", "🤩", "🥰", "😄", "😡", "😶"):
        require(token in animation, f"voice-platform emoji mapping missing: {token}")
    for token in ("emotion", "animation", "emoji", "text"):
        require(f'"{token}"' in main_cc, f"llm animation token fallback missing: {token}")
    require("LV_EVENT_READY" in display, "start GIF completion must use LV_EVENT_READY")
    require("ShowStartThenLoop" in display, "start-to-loop display path missing")
    require("CONFIG_LV_USE_GIF=y" in sdkconfig, "LVGL GIF support must be enabled")
    require("CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0" in sdkconfig, "default malloc must prefer PSRAM for LVGL/audio coexistence")
    require("CONFIG_LV_USE_CLIB_MALLOC=y" in sdkconfig, "LVGL must use CLIB malloc so allocations can go to PSRAM")
    require("CONFIG_LV_BUILD_EXAMPLES=n" in sdkconfig or "# CONFIG_LV_BUILD_EXAMPLES is not set" in sdkconfig,
            "LVGL examples must not be built into firmware")
    require(".buff_spiram = 1" in display, "LVGL display buffer must be PSRAM-backed")
    require("task_stack_caps = MALLOC_CAP_SPIRAM" in display, "LVGL task stack must be PSRAM-backed")
    require("lv_display_set_rotation" not in display,
            "startup must not call runtime LVGL rotation after the LVGL port task starts")
    require(".mirror_x = !DISPLAY_MIRROR_X" in display and ".mirror_y = !DISPLAY_MIRROR_Y" in display,
            "180-degree display orientation must be encoded in hardware mirror config")
    require("lvgl_port_lock(1000)" in display and "lv_display_set_offset" in display,
            "LVGL display setup must hold the port lock before mutating display objects")
    require("kTaskStackMemoryCaps = MALLOC_CAP_SPIRAM" in main_cc, "audio task stacks must be PSRAM-backed")
    require("audio queue allocation failed" in main_cc, "audio queue failure must log instead of reboot-looping")
    require("ShowIdleAnimation" in main_cc and 'animation_.ShowEmotion("normal")' in main_cc,
            "idle display state must map to normal.gif")
    require("ShowListeningAnimation" in main_cc and 'animation_.ShowUtility("listening")' in main_cc,
            "listening display state must map to listening.gif")
    require("ShowSpeakingAnimation" in main_cc and 'animation_.ShowEmotion("speaking")' in main_cc,
            "speaking display state must map to happy/heart animation")
    require('ShowUtility("silence")' not in main_cc,
            "normal speaking must not use silence.gif")
    require('"wifi_conn_cb"' in wifi and "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT" in wifi,
            "Wi-Fi connected callback must use an internal stack for NVS/flash work")
    require('"anim_update"' in animation and "xTaskCreateWithCaps" in animation
            and "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT" in animation,
            "animation update task must use an internal stack for NVS/flash work")

    order = [
        "animation_.InitDisplay();",
        "InitAudio();",
        "animation_.InitStartupMedia();",
        "animation_.PlayStartupWav",
        "animation_.InitBundle();",
        "wifi_.Start();",
        "StartAudioTasks();",
    ]
    positions = [main_cc.find(item) for item in order]
    require(all(pos >= 0 for pos in positions), "startup animation/audio order tokens missing")
    require(positions == sorted(positions), "startup must play media before bundle/Wi-Fi/audio tasks continue")


def validate_audio_contract() -> None:
    afe = (ROOT / "main" / "afe_audio_processor.cc").read_text(encoding="utf-8")
    main_cc = (ROOT / "main" / "main.cc").read_text(encoding="utf-8")
    require("afe_config->vad_init = false" in afe, "local ESP-SR AFE VAD must stay disabled")
    require("enable_vad" not in afe, "firmware must not re-enable hidden local AFE VAD")
    require("server-side ASR owns VAD" in afe, "AFE init log should document server-side VAD ownership")
    require("local AFE VAD remains disabled" in afe, "AEC toggles must not imply VAD is enabled")
    require(afe.count("vTaskDelay(pdMS_TO_TICKS(1));") >= 1,
            "AFE fetch loop must yield so CPU1 idle can feed the watchdog")
    require(main_cc.count("processor_->Start();") == 1,
            "AFE processor should only start in StartListeningCycle, not during TTS playback")
    require("processor_->Feed(data);\n                vTaskDelay(pdMS_TO_TICKS(1));" in main_cc,
            "audio feed loop must yield so CPU1 idle can feed the watchdog")
    require("std::mutex opus_encoder_mutex_" in main_cc, "Opus encoder state must have a dedicated mutex")
    require(main_cc.count("std::lock_guard<std::mutex> lock(opus_encoder_mutex_);") >= 2,
            "Opus encode and reset paths must both hold the encoder mutex")
    require("DrainOutgoingAudio();" in main_cc and "drained %d queued outbound opus frames" in main_cc,
            "listen stop must drain queued outbound Opus frames")
    require("ws_connected_ && streaming_ && frame.bytes > 0" in main_cc,
            "network send task must drop queued Opus when streaming is stopped")
    require('strcmp(type->valuestring, "listen") == 0' in main_cc and "HandleServerListenState" in main_cc,
            "firmware must act on server listen:start/listen:stop controls")
    require('"server listen:stop; %s"' in main_cc and '"server listen:start"' in main_cc,
            "server listen control must pause and resume local audio upload")
    require('"server listen:start ignored: conversation is stopped"' in main_cc,
            "server auto-resume must not restart after BOOT exits conversation mode")
    require("SendListen(\"stop\");" in main_cc and "SendAbort();" in main_cc,
            "BOOT stop must notify the server even in the post-TTS auto-resume gap")


def main() -> None:
    validate_defaults()
    validate_protocol()
    validate_manifest()
    validate_source_strings()
    validate_ble_wifi_contract()
    validate_firestore_contract()
    validate_animation_contract()
    validate_audio_contract()
    print("BabyMilu Stage 1 contract harness: PASS")


if __name__ == "__main__":
    main()
