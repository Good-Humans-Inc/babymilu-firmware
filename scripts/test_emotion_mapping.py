#!/usr/bin/env python3
"""Lightweight regression check for firmware emotion-string mappings."""

from __future__ import annotations

import re
import sys
from pathlib import Path


REQUIRED_MAPPINGS = {
    "cheerful": "ANIMATION_SMILEY",
    "speechless": "ANIMATION_SPEECHLESS",
}


def extract_mappings(source: str) -> dict[str, str]:
    pattern = re.compile(r'\{\s*(ANIMATION_[A-Z_]+)\s*,\s*"([^"]+)"\s*\}')
    return {emotion: animation for animation, emotion in pattern.findall(source)}


def main() -> int:
    repo_root = Path(__file__).resolve().parents[1]
    source_path = repo_root / "main" / "display" / "lcd_display.cc"
    source = source_path.read_text(encoding="utf-8")
    mappings = extract_mappings(source)

    missing: list[str] = []
    for emotion, animation in REQUIRED_MAPPINGS.items():
        actual = mappings.get(emotion)
        if actual != animation:
            missing.append(
                f"{emotion!r} -> expected {animation}, found {actual or 'missing'}"
            )

    if missing:
        print("Emotion mapping regression check failed:")
        for issue in missing:
            print(f"  - {issue}")
        return 1

    print("Emotion mapping regression check passed.")
    for emotion, animation in REQUIRED_MAPPINGS.items():
        print(f"  - {emotion} -> {animation}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
