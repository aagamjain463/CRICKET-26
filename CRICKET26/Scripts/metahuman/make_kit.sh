#!/bin/zsh
# Makes every player's cricket kit and batting gear, and the bat: Unreal exports each full body (kit_ue.py), Blender
# cuts the kit from it (make_kit.py) and models the bat (make_bat.py), and Unreal imports them onto the player's
# skeleton. Run after make_players.sh. Takes about a minute a player the first time (the bodies are built once more
# without their garment), seconds after.
# Usage: Scripts/metahuman/make_kit.sh [Name,Name...]   (default: all ten)
set -e
ROOT="${0:A:h:h:h}"
ENGINE="/Users/Shared/Epic Games/UE_5.8/Engine"
BLENDER="/Applications/Blender.app/Contents/MacOS/Blender"
export KIT_DIR="$ROOT/Saved/Kit"
export KIT_NAMES="${1:-MH_Home_Opener,MH_Home_Finisher,MH_Home_Allrounder,MH_Home_Quick,MH_Away_Hitter,MH_Away_Anchor,MH_Away_KeeperBat,MH_Away_WristSpinner,MH_Umpire_1,MH_Umpire_2}"
mkdir -p "$KIT_DIR"
unreal() {
  KIT_STEP=$1 "$ENGINE/Binaries/Mac/UnrealEditor-Cmd" "$ROOT/CRICKET26.uproject" -run=pythonscript -script="$ROOT/Scripts/metahuman/kit_ue.py" \
    -AllowCommandletRendering -dpcvars=TextureGraph.AllowCommandlets=1 -unattended -nosplash -NoZen -abslog="$KIT_DIR/$1.log" > /dev/null 2>&1 || true
  grep -E "LogPython: (KIT|Error)" "$KIT_DIR/$1.log" | grep -v LogInit | sed 's/^.*LogPython: //'
}
unreal export
for NAME in ${(s:,:)KIT_NAMES}; do
  "$BLENDER" -b --python "$ROOT/Scripts/metahuman/make_kit.py" -- "$KIT_DIR/${NAME}_Body.fbx" "$KIT_DIR/${NAME}_Kit.fbx" "$KIT_DIR/${NAME}_Gear.fbx" 2>&1 | grep -E "^KIT written|Error" || true
done
"$BLENDER" -b --python "$ROOT/Scripts/metahuman/make_bat.py" -- "$KIT_DIR/Bat.fbx" 2>&1 | grep -E "^KIT written|Error" || true
unreal import
