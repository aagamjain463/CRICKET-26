#!/bin/zsh
# Puts a game frame beside a reference frame, both scaled to 720 lines, for judging a look side by side.
# The reference is an image, or a video with the time of the frame to take (seconds or hh:mm:ss).
# Usage: Scripts/compare.sh GAME.png REFERENCE [TIME] [OUT.png]   (OUT defaults to Saved/Compare.png)
set -e
ROOT="${0:A:h:h}"
GAME="$1"; REF="$2"; TIME="${3:-}"; OUT="${4:-$ROOT/Saved/Compare.png}"
if [[ -n "$TIME" ]]; then SEEK=(-ss "$TIME"); else SEEK=(); fi
ffmpeg -loglevel error -y -i "$GAME" $SEEK -i "$REF" \
  -filter_complex "[0]scale=-2:720[a];[1]scale=-2:720[b];[a][b]hstack" -frames:v 1 "$OUT"
echo "$OUT"
