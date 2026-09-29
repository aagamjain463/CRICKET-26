#!/bin/zsh
# Plays the game in a window. Build first (Scripts/build.sh). Extra args pass through, e.g.
# Scripts/play.sh -CricketLeftHanded -CricketSlowMo=0.25
ROOT="${0:A:h:h}"
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$ROOT/CRICKET26.uproject" \
  "/Engine/Maps/Entry?game=/Script/CRICKET26.SuperOverGameMode" -game -windowed -ResX=1600 -ResY=900 -abslog="$ROOT/Saved/Play.log" "$@"
