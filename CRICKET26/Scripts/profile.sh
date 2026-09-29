#!/bin/zsh
# Plays N deliveries AI vs AI in a game window without capturing anything and prints the performance
# summary (frame, game thread, render thread and GPU times, average and 99th percentile).
# Extra args go to the game, e.g. -ExecCmds="scalability 0" for the lowest settings or -FeatureLevelES31
# for the mobile renderer.
# Usage: Scripts/profile.sh N [extra args]
set -e
ROOT="${0:A:h:h}"
ENGINE="/Users/Shared/Epic Games/UE_5.8/Engine"
N="${1:-12}"; shift || true
python3 "$ROOT/Scripts/link_module.py" "$ROOT"
"$ENGINE/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$ROOT/CRICKET26.uproject" "/Engine/Maps/Entry?game=/Script/CRICKET26.SuperOverGameMode" \
  -game -unattended -windowed -ResX=1280 -ResY=720 -nosound -CricketAutoPlay -CricketQuitAfter="$N" -abslog="$ROOT/Saved/Profile.log" "$@" > /dev/null 2>&1 || true
grep -h "Perf (" "$ROOT/Saved/Profile.log" | sed 's/^.*Display: //'
