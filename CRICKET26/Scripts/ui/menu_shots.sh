#!/bin/zsh
# Captures every frontend page, silently, into Saved/MenuShots/<SET>/.
# Scripts/ui/menu_shots.sh SET [WIDTH HEIGHT] [TAB...]   default 1600x740 (a 19.5:9 phone in landscape), all tabs.
# Tab numbers follow EFrontendTab: 0 Home, 1 Play, 2 Live, 3 Franchise, 5 Store, 6 Settings, 7 Match setup.
ROOT="${0:A:h:h:h}"
ENGINE="/Users/Shared/Epic Games/UE_5.8/Engine"
SET="${1:-menu}"; W="${2:-1600}"; H="${3:-740}"; shift 3 2>/dev/null
TABS=($@); (( $#TABS )) || TABS=(0 1 2 3 5 6 7)
OUT="$ROOT/Saved/MenuShots/$SET"; mkdir -p "$OUT"
for TAB in $TABS; do
	rm -f "$ROOT"/Saved/Screenshots/*/Tab${TAB}.png(N)
	"$ENGINE/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$ROOT/CRICKET26.uproject" "/Engine/Maps/Entry?Tab=$TAB" \
		-game -unattended -windowed -ResX="$W" -ResY="$H" -nosound -FrontendShot=Tab$TAB -abslog="$OUT/Tab$TAB.log" >/dev/null 2>&1
	mv "$ROOT"/Saved/Screenshots/*/Tab${TAB}.png(N) "$OUT/" 2>/dev/null && echo "Tab$TAB ok" || echo "Tab$TAB missing"
done
