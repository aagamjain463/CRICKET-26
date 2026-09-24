#!/bin/zsh
# Builds the stadium's assets into Content/Stadium: the printed textures (make_textures.py), the crowd's spectator
# (make_fan.py, in Blender), then the materials and the imports (stadium_ue.py, run by Unreal as a commandlet). The game falls back to flat colours when they are missing.
# Usage: Scripts/stadium/make_stadium.sh
set -e
ROOT="${0:A:h:h:h}"
ENGINE="/Users/Shared/Epic Games/UE_5.8/Engine"
export STADIUM_DIR="$ROOT/Saved/Stadium"
mkdir -p "$STADIUM_DIR"
python3 "$ROOT/Scripts/stadium/make_textures.py" "$STADIUM_DIR"
# The crowd's spectator, from a player's body (Scripts/metahuman/make_kit.sh exports it). Without it the game keeps
# its block crowd.
BODY="$ROOT/Saved/Kit/MH_Home_Finisher_Body.fbx"
if [[ -f "$BODY" ]]; then
  /Applications/Blender.app/Contents/MacOS/Blender -b --python "$ROOT/Scripts/stadium/make_fan.py" -- "$BODY" "$STADIUM_DIR/Fan" 2>&1 | grep -E "^FAN|Error" || true
fi
"$ENGINE/Binaries/Mac/UnrealEditor-Cmd" "$ROOT/CRICKET26.uproject" -run=pythonscript -script="$ROOT/Scripts/stadium/stadium_ue.py" \
  -unattended -nosplash -NoZen -abslog="$STADIUM_DIR/ue.log" > /dev/null 2>&1 || true
grep -E "LogPython: (STADIUM|Error)|Error:.*(Custom|HLSL|Material)|error" "$STADIUM_DIR/ue.log" | sed 's/^.*\(LogPython\|Error\)/\1/' | head -40
