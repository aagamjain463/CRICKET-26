#!/bin/zsh
# Plays AI vs AI in a game window and saves the game view (only) 5 times a second while delivery N is live,
# then quits. Frames land in Saved/Screenshots/<platform>/BallN_*.png.
# With -CricketRecordAudio the game runs with sound and also writes the delivery's mixed audio to Saved/BallN.wav.
# Usage: Scripts/capture.sh N [extra args]
set -e
ROOT="${0:A:h:h}"
ENGINE="/Users/Shared/Epic Games/UE_5.8/Engine"
N="${1:-1}"; shift || true
SOUND=-nosound; [[ " $* " == *" -CricketRecordAudio "* ]] && SOUND=
setopt null_glob; rm -f "$ROOT"/Saved/Screenshots/*/Ball${N}_*.png
"$ENGINE/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$ROOT/CRICKET26.uproject" "/Engine/Maps/Entry?game=/Script/CRICKET26.SuperOverGameMode" \
  -game -windowed -ResX=1280 -ResY=720 $SOUND -CricketAutoPlay -CricketShotBall="$N" -abslog="$ROOT/Saved/Capture.log" "$@" > /dev/null 2>&1 || true
print -l "$ROOT"/Saved/Screenshots/*/Ball${N}_*.png | wc -l
