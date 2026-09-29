#!/bin/zsh
# Builds the auction room's MetaHumans (make_players.py, MHMAKE_SET=auction), then dresses them in the parametric
# long-sleeve shirt, trousers and shoes (outfit_ue.py) that the game tints per franchise. Skips characters already built.
ROOT="${0:A:h:h:h}"
ENGINE="/Users/Shared/Epic Games/UE_5.8/Engine"
MHMAKE_SET=auction "$ROOT/Scripts/metahuman/make_players.sh"
NAMES=$(python3 -c "import re;print(' '.join(re.findall(r'\(\"(MH_(?:Auctioneer|Staff_\w+))\"', open('$ROOT/Scripts/metahuman/make_players.py').read())))")
for N in ${=NAMES}; do
  ls "$ROOT/Content/MetaHumans/$N/Clothing" 2>/dev/null | grep -q "WI_OA_Tshirt" && continue
  OUTFIT_WEAR=$N "$ENGINE/Binaries/Mac/UnrealEditor-Cmd" "$ROOT/CRICKET26.uproject" -run=pythonscript -script="$ROOT/Scripts/metahuman/outfit_ue.py" \
    -AllowCommandletRendering -dpcvars=TextureGraph.AllowCommandlets=1 -unattended -nosplash -NoZen -nosound -abslog="$ROOT/Saved/MHOutfit.log" > /dev/null 2>&1
  grep -E "OUTFIT|LogPython: Error" "$ROOT/Saved/MHOutfit.log"
done
