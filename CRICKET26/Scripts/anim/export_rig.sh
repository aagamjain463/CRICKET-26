#!/bin/bash
# Exports the striker's skeleton, body, kit and the mannequin idle to FBX for authoring strokes in Blender.
# The whole body (skin not cut away under the kit) comes from the kit build, Scripts/metahuman/make_kit.sh.
# Usage: Scripts/anim/export_rig.sh [out dir, default ~/Downloads/mocap/rig]
cd "$(dirname "$0")/../.."
OUT="${1:-$HOME/Downloads/mocap/rig}"
mkdir -p Saved/Anim "$OUT"
cp Saved/Kit/MH_Home_Opener_Body.fbx "$OUT/Body.fbx" || exit 1
EXPORT_DIR="$OUT" "/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/CRICKET26.uproject" -run=pythonscript -script="$PWD/Scripts/anim/export_ue.py" \
  -unattended -nosplash -NoZen -AllowCommandletRendering -abslog="$PWD/Saved/Anim/export.log"
grep "EXPORT" Saved/Anim/export.log
