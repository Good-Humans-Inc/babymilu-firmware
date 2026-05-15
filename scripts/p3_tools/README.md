# P3 Audio Tools

This folder contains helper scripts for the firmware's protocol-v3 audio stream
format. The tools are useful for inspecting or creating `.p3` files outside the
device.

## P3 file format

Each frame is:

```text
1 byte  packet type
1 byte  reserved
2 bytes big-endian Opus payload length
N bytes Opus payload
```

The scripts assume:

- sample rate: 16000 Hz
- channels: mono
- frame duration: 60 ms
- codec: Opus

## Install dependencies

```bash
pip install -r requirements.txt
```

The requirements include audio decoding/encoding and playback packages such as
`librosa`, `opuslib`, `sounddevice`, `soundfile`, and `pyloudnorm`.

## Convert audio to P3

```bash
python convert_audio_to_p3.py input.wav output.p3
```

By default the converter applies loudness normalization to `-16 LUFS`.

Disable normalization for short clips, already mastered clips, or TTS output:

```bash
python convert_audio_to_p3.py input.wav output.p3 -d
```

Set a custom loudness target:

```bash
python convert_audio_to_p3.py input.wav output.p3 -l -18
```

Supported input formats depend on the local `librosa`/audio backend setup.

## Convert P3 back to audio

```bash
python convert_p3_to_audio.py input.p3 output.wav
```

The output is written as 16 kHz mono PCM WAV.

## Play P3 from the command line

```bash
python play_p3.py output.p3
```

The player decodes frames and streams PCM audio through the local sound device.

## GUI helpers

Batch conversion GUI:

```bash
python batch_convert_gui.py
```

P3 playlist/player GUI:

```bash
python p3_gui_player.py
```

These GUI scripts are convenience tools for desktop inspection and asset
preparation. They are not part of the firmware build.
