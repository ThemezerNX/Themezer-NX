#!/bin/sh
# Regenerates romfs/sfx from Themezer-3DS's synthesized UI sounds, resampled to the Switch's 48 kHz.
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
