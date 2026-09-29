#!/bin/zsh
# Tiles frames into one contact sheet: Scripts/sheet.sh OUT.jpg COLS frame.png ...
OUT=$1; COLS=$2; shift 2
N=$#; ROWS=$(( (N + COLS - 1) / COLS ))
ffmpeg -loglevel error -y $(for F in "$@"; do print -- -i $F; done) -filter_complex \
  "$(for I in $(seq 0 $((N-1))); do printf '[%d]scale=480:270[s%d];' $I $I; done)$(for I in $(seq 0 $((N-1))); do printf '[s%d]' $I; done)xstack=inputs=${N}:layout=$(for I in $(seq 0 $((N-1))); do printf '%d_%d|' $(( (I % COLS) * 480 )) $(( (I / COLS) * 270 )); done | sed 's/|$//')" \
  -frames:v 1 -q:v 3 "$OUT"
