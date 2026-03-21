#!/usr/bin/env bash
# Ubuntu/Debian/WSL: system Python is PEP 668 "externally managed" — use a venv.
set -euo pipefail
cd "$(dirname "$0")"
python3 -m venv .venv
./.venv/bin/pip install -U pip
./.venv/bin/pip install -r requirements.txt
echo
echo "Done. From repo root, activate the venv then run scripts as usual:"
echo "  source scripts/p3_tools/.venv/bin/activate"
echo "  python scripts/p3_tools/batch_convert_gui.py"
echo "  python scripts/p3_tools/convert_audio_to_p3.py in.mp3 out.p3"
echo "(Or skip activate and use scripts/p3_tools/.venv/bin/python ... if you prefer.)"
