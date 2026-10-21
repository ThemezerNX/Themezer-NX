#!/bin/sh
# Regenerates romfs/sfx (Themezer-3DS's UI sounds, at 48 kHz) and romfs/bgm.wav (its music loop, at 24 kHz).
# Needs a Themezer-3DS checkout (THEMEZER_3DS, default ../themezer-3ds), Python with numpy, and ffmpeg.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${THEMEZER_3DS:-$ROOT/../themezer-3ds}
PYTHON=${PYTHON:-python3}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
"$PYTHON" "$SRC/tools/sfx/make_sfx.py" "$TMP"
for f in "$TMP"/*.wav; do
    ffmpeg -loglevel error -y -i "$f" -ar 48000 -ac 1 -c:a pcm_s16le -map_metadata -1 -fflags +bitexact "$ROOT/themezer-nx/romfs/sfx/$(basename "$f")"
done

# The background music: the 16-bar loop of "Aether" that Themezer-3DS plays (loop points from its tools/music/build.sh)
ffmpeg -loglevel error -y -i "$SRC/tools/music/Aether.m4a" -c:a pcm_s16le "$TMP/song.wav"
"$PYTHON" "$SRC/tools/music/make_loop.py" "$TMP/song.wav" 16.20 45.06 "$TMP/loop" 1.0 133.04 5
ffmpeg -loglevel error -y -i "$TMP/loop.wav" -ar 24000 -c:a pcm_s16le -map_metadata -1 -fflags +bitexact "$ROOT/themezer-nx/romfs/bgm.wav"
