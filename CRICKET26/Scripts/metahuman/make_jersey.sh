#!/bin/zsh
# Bakes the crew-neck jersey's panels (jersey_panels.py) from the shirts as built and imports them. MetaHuman Creator
# cuts the shirt in two variants with different UV layouts: slim (nrw, as on MH_Home_Opener) and broad (ovw, as on
# MH_Away_WristSpinner); name the pairs (texture player ...) to redo only some. The trousers' piping masks
# (trouser_stripe.py) are baked the same way from the jeans of the same two players, when no pairs are named. Progress lines start with JERSEY; the lettering UVs printed are the ones ShirtPrint uses.
ROOT="${0:A:h:h:h}"
ENGINE="/Users/Shared/Epic Games/UE_5.8/Engine"
OUT="$ROOT/Saved/Jersey"
mkdir -p "$OUT"
run() { python3 "$ROOT/Scripts/link_module.py" "$ROOT"; "$ENGINE/Binaries/Mac/UnrealEditor-Cmd" "$ROOT/CRICKET26.uproject" -run=pythonscript -script="$ROOT/Scripts/metahuman/jersey_ue.py" \
  -unattended -nosplash -NoZen -nosound -abslog="$OUT/ue.log" > /dev/null 2>&1; grep -E "JERSEY|LogPython: Error" "$OUT/ue.log"; }
TROUSERS=$(( ! $# ))
(( $# )) || set -- T_JerseyPanels MH_Home_Opener T_JerseyPanels_Broad MH_Away_WristSpinner
for NAME PLAYER in "$@"; do
  JERSEY_PLAYER=$PLAYER JERSEY_OBJ="$OUT/$NAME.obj" run
  /Applications/Blender.app/Contents/MacOS/Blender -b -P "$ROOT/Scripts/metahuman/jersey_panels.py" -- "$OUT/$NAME.obj" "$OUT/$NAME.png" 1024 2>&1 | grep -E "JERSEY|Error"
  JERSEY_NAME=$NAME JERSEY_PNG="$OUT/$NAME.png" run
done
(( TROUSERS )) && for NAME PLAYER in T_TrouserMask MH_Home_Opener T_TrouserMask_Broad MH_Away_WristSpinner; do
  JERSEY_SLOT=jeans JERSEY_PLAYER=$PLAYER JERSEY_OBJ="$OUT/$NAME.obj" JERSEY_TGA="$OUT/$NAME.tga" run
  /Applications/Blender.app/Contents/MacOS/Blender -b -P "$ROOT/Scripts/metahuman/trouser_stripe.py" -- "$OUT/$NAME.obj" "$OUT/$NAME.tga" "$OUT/$NAME.png" 2>&1 | grep -E "TROUSERS|Error"
  JERSEY_NAME=$NAME JERSEY_PNG="$OUT/$NAME.png" run
done
