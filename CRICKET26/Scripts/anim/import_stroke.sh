#!/bin/zsh
# Imports the strokes author_stroke.py baked (Bat_<Stroke>.fbx in the given directory) into Unreal (stroke_ue.py).
# Usage: Scripts/anim/import_stroke.sh [dir]   (default: Saved/AnimQA/drive)
ROOT="${0:A:h:h:h}"
export STROKE_DIR="${1:-$ROOT/Saved/AnimQA/drive}"
LOG="$ROOT/Saved/Anim/stroke.log"
mkdir -p "${LOG:h}"
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" "$ROOT/CRICKET26.uproject" -run=pythonscript \
  -script="$ROOT/Scripts/anim/stroke_ue.py" -unattended -nosplash -NoZen -abslog="$LOG" > /dev/null 2>&1
grep -E "LogPython: (STROKE|Error)|Error:" "$LOG" | sed 's/^.*LogPython: //' | head -30
