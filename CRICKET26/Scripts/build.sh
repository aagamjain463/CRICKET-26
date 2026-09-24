#!/bin/zsh
# Builds the editor target and prints only errors and the result; exits non-zero on failure so a
# following capture or test run does not launch against a stale module.
ROOT="${0:A:h:h}"
OUT=$("/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" CRICKET26Editor Mac Development -Project="$ROOT/CRICKET26.uproject" -WaitMutex 2>&1)
print -r -- "$OUT" | grep -E " error|Result"
[[ "$OUT" == *"Result: Succeeded"* ]] || exit 1
# While an editor or the game is open, UBT writes a numbered copy of the module (hot reload) and can leave the
# manifest naming the old one, so the next launch would run stale code. Point the manifest at the newest.
NEWEST=$(ls -t "$ROOT"/Binaries/Mac/libUnrealEditor-CRICKET26*.dylib | head -1)
sed -i '' -E "s/libUnrealEditor-CRICKET26[-0-9]*\.dylib/${NEWEST:t}/" "$ROOT/Binaries/Mac/UnrealEditor.modules"
