#!/usr/bin/env python3
"""Source-level contract for terminal WebSocket conversation messages."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
APPLICATION = (ROOT / "main" / "application.cc").read_text(encoding="utf-8")


class WebSocketGoodbyeContractTest(unittest.TestCase):
    def test_goodbye_is_handled_by_websocket_application_path(self) -> None:
        handler_start = APPLICATION.index("websocket_protocol_->OnIncomingJson")
        handler_end = APPLICATION.index("// Start the WebSocket protocol", handler_start)
        handler = APPLICATION[handler_start:handler_end]

        self.assertIn('strcmp(type->valuestring, "goodbye") == 0', handler)
        goodbye_start = handler.index('strcmp(type->valuestring, "goodbye") == 0')
        tts_start = handler.index('strcmp(type->valuestring, "tts") == 0', goodbye_start)
        goodbye = handler[goodbye_start:tts_start]

        self.assertIn("SetDeviceState(kDeviceStateIdle)", goodbye)
        self.assertIn('SetWebSocketConnectionMode("normal")', goodbye)
        self.assertIn("websocket_protocol_->CloseAudioChannel()", goodbye)

    def test_goodbye_clears_conversation_only_state_before_close(self) -> None:
        match = re.search(
            r'if \(strcmp\(type->valuestring, "goodbye"\) == 0\).*?'
            r'} else if \(strcmp\(type->valuestring, "tts"\) == 0\)',
            APPLICATION,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        goodbye = match.group(0)

        self.assertIn("is_alarm_mode_ = false", goodbye)
        self.assertIn("aborted_ = false", goodbye)
        self.assertIn("state_before_tts_ = kDeviceStateUnknown", goodbye)
        self.assertLess(
            goodbye.index("SetDeviceState(kDeviceStateIdle)"),
            goodbye.index("websocket_protocol_->CloseAudioChannel()"),
        )


if __name__ == "__main__":
    unittest.main()
