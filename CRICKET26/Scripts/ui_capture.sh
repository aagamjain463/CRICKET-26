#!/bin/zsh
# Plays a whole Super Over through the touch layer (-CricketTouchScript: the human side bats the first innings and
# bowls the second) and saves the whole screen, HUD included, every half second into Saved/UIShots/<name>/.
# File names carry the innings, balls played, delivery phase and match phase (see -CricketUIShotEvery).
# Usage: [UISHOT=seconds] Scripts/ui_capture.sh NAME [WIDTH HEIGHT] [extra args]
#   e.g. Scripts/ui_capture.sh after_phone 2400 1080
#        Scripts/ui_capture.sh notch 2400 1080 -ExecCmds="SafeZone.EnableOverrides 1, SafeZone.Ratio.Left 0.94, SafeZone.Ratio.Right 0.94"
set -e
ROOT="${0:A:h:h}"
ENGINE="/Users/Shared/Epic Games/UE_5.8/Engine"
NAME="${1:-ui}"; shift || true
W="${1:-1600}"; H="${2:-900}"; shift 2 || true
setopt null_glob; rm -f "$ROOT"/Saved/Screenshots/*/UI_*.png
"$ENGINE/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$ROOT/CRICKET26.uproject" "/Engine/Maps/Entry?game=/Script/CRICKET26.SuperOverGameMode" \
  -game -unattended -windowed -ResX="$W" -ResY="$H" -nosound -CricketTouchScript -CricketUIShotEvery=${UISHOT:-0.5} -CricketVenue=0 -abslog="$ROOT/Saved/UICapture.log" "$@" > /dev/null 2>&1 || true
OUT="$ROOT/Saved/UIShots/$NAME"; rm -rf "$OUT"; mkdir -p "$OUT"
mv "$ROOT"/Saved/Screenshots/*/UI_*.png "$OUT"/ 2>/dev/null || true
print -l "$OUT"/*.png | wc -l
# The HUD must actually paint (regression: it was added to the viewport but freed unpainted, so shots had no HUD).
grep -q "Match HUD OnPaint entered" "$ROOT/Saved/UICapture.log" || { print -u2 "ui_capture: the match HUD never painted"; exit 1; }
