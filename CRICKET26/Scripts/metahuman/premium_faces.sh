#!/bin/zsh
# Premium face overhaul (see premium_faces.py). Tunes skin/eye/teeth/hair materials
# and bakes dense groom cards for all 10 match identities. No cloud, no rebuild.
# Progress lines start with PREMIUM_FACE.
ROOT="${0:A:h:h:h}"
ENGINE="/Users/Shared/Epic Games/UE_5.8/Engine"
LOG="$ROOT/Saved/PremiumFaces.log"
"$ENGINE/Binaries/Mac/UnrealEditor-Cmd" "$ROOT/CRICKET26.uproject" -run=pythonscript -script="$ROOT/Scripts/metahuman/premium_faces.py" \
  -AllowCommandletRendering -dpcvars=TextureGraph.AllowCommandlets=1 -unattended -nosplash -NoZen -nosound -abslog="$LOG" > /dev/null 2>&1
grep -E "PREMIUM_FACE|LogMetaHuman.*(Error|Warning)|LogPython: Error" "$LOG"
