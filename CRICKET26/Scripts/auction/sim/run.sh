#!/usr/bin/env bash
# Builds the auction engine and its engine-only tests against the Core stand-in in shim/ and runs them, no editor
# needed: the rules, the AI and the calibration against the 2025 mega auction in a few seconds.
#   Scripts/auction/sim/run.sh               all engine tests
#   Scripts/auction/sim/run.sh -v Calib      one test, with its report lines
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../../../Source/CRICKET26/Auction"
OUT="${TMPDIR:-/tmp}/auction_sim"
CXX="${CXX:-clang++}"
mkdir -p "$OUT"
# -Wshadow -Werror as Scripts/build.sh builds the game: a shadowed local fails here before it fails there.
"$CXX" -std=c++20 -O2 -g -Wall -Wshadow -Werror -Wno-unused-function -Wno-unused-variable -Wno-sign-compare \
	-Wno-unused-but-set-variable -Wno-unused-private-field \
	-I"$HERE/shim" -I"$SRC" -I"$SRC/Tests" \
	"$SRC"/AuctionData.cpp "$SRC"/AuctionEngine.cpp $(ls "$SRC"/AuctionAI.cpp 2>/dev/null) \
	"$SRC"/Tests/AuctionEngineTests.cpp "$HERE"/main.cpp -o "$OUT/auction_sim"
"$OUT/auction_sim" "$@"
