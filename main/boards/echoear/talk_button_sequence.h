#pragma once

#include <string>
#include <utility>
#include <vector>

namespace talk_button_sequence {

// SD root filename (FAT 8.3: TALK.TXT). Long names like talk_sequence.txt may only appear as TALK_S~N.TXT
// when LFN is disabled in FATFS — use talk.txt on the card.
inline constexpr const char* kSequenceFileName = "talk.txt";

// Reads talk.txt from the SD mount root (SdCard::MountPoint()). No fallback: if missing or invalid,
// LoadSteps leaves the vector empty.
//
// File format: one step per line
//   emotion_name duration_ms
// Lines starting with # or ; are comments. Blank lines ignored.
// Up to 48 steps; duration clamped to 50..600000 ms.
//
// Emotion names match LcdDisplay::SetEmotion (normal, angry, starry, love, happy, ...).

void LoadSteps(std::vector<std::pair<std::string, int>>& out);

}  // namespace talk_button_sequence
