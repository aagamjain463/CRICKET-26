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
	"$SRC"/AuctionData.cpp "$SRC"/AuctionEngine.cpp "$SRC"/AuctionAI.cpp \
	"$SRC"/Tests/AuctionEngineTests.cpp "$HERE"/main.cpp -o "$OUT/auction_sim"
# The auctioneer's lines too, against stand-ins for the room layout and the audio (calls_stubs/). Compiled from a copy,
# so its quoted includes find the stand-ins rather than the real room header beside it.
cp "$SRC"/AuctionCalls.cpp "$OUT"/AuctionCalls.cpp
"$CXX" -std=c++20 -O2 -g -Wall -Wshadow -Werror -Wno-unused-function -Wno-unused-variable -Wno-sign-compare \
	-Wno-unused-but-set-variable -Wno-unused-private-field \
	-I"$HERE/shim" -I"$HERE/calls_stubs" -I"$SRC" -I"$SRC/Tests" \
	"$SRC"/AuctionData.cpp "$SRC"/AuctionEngine.cpp "$SRC"/AuctionAI.cpp "$OUT"/AuctionCalls.cpp \
	"$HERE"/calls_test.cpp "$HERE"/main.cpp -o "$OUT/auction_calls"
"$OUT/auction_sim" "$@"
"$OUT/auction_calls" "$@"
