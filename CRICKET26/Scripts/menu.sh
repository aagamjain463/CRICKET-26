#!/bin/zsh
# Opens the game at the main menu (FrontendGameMode) instead of straight into a Super Over. Build first.
ROOT="${0:A:h:h}"
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$ROOT/CRICKET26.uproject" \
  /Engine/Maps/Entry -game -windowed -ResX=1600 -ResY=900 -abslog="$ROOT/Saved/Menu.log" "$@"
