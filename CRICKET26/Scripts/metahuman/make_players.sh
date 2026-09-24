#!/bin/zsh
# Builds the MetaHuman players (see make_players.py). The editor runs with rendering on: texture synthesis
# and the build use the GPU. Progress lines start with MHMAKE.
ROOT="${0:A:h:h:h}"
ENGINE="/Users/Shared/Epic Games/UE_5.8/Engine"
LOG="$ROOT/Saved/MHMake.log"
"$ENGINE/Binaries/Mac/UnrealEditor-Cmd" "$ROOT/CRICKET26.uproject" -run=pythonscript -script="$ROOT/Scripts/metahuman/make_players.py" \
  -AllowCommandletRendering -dpcvars=TextureGraph.AllowCommandlets=1 -unattended -nosplash -NoZen -abslog="$LOG" > /dev/null 2>&1
grep -E "MHMAKE|LogMetaHuman.*(Error|Warning)|LogPython: Error" "$LOG"
