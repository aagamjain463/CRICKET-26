#!/bin/zsh
# Imports the motion-capture clips and retargets them onto the mannequin skeleton the players play (anim_ue.py):
# DeepMotion Animate 3D takes (character "Adult Male (UE)") from <dir>/fbx and Mixamo clips from <dir>/mixamo.
# Name each file after the clip, e.g. fbx/Bat_Drive.fbx or mixamo/Run_Sprint.fbx.
# Usage: Scripts/anim/import_anims.sh [dir]   (default: ~/Downloads/mocap)
set -e
ROOT="${0:A:h:h:h}"
ENGINE="/Users/Shared/Epic Games/UE_5.8/Engine"
export ANIM_DIR="${1:-$HOME/Downloads/mocap}"
LOG="$ROOT/Saved/Anim/ue.log"
mkdir -p "${LOG:h}"
"$ENGINE/Binaries/Mac/UnrealEditor-Cmd" "$ROOT/CRICKET26.uproject" -run=pythonscript -script="$ROOT/Scripts/anim/anim_ue.py" \
  -unattended -nosplash -NoZen -abslog="$LOG" > /dev/null 2>&1 || true
grep -E "LogPython: (ANIM|Error)|Error:" "$LOG" | sed 's/^.*LogPython: //' | head -60
