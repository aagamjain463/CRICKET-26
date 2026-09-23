#!/bin/zsh
# Builds the editor target and prints only errors and the result; exits non-zero on failure so a
# following capture or test run does not launch against a stale module.
ROOT="${0:A:h:h}"
OUT=$("/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" CRICKET26Editor Mac Development -Project="$ROOT/CRICKET26.uproject" -WaitMutex 2>&1)
print -r -- "$OUT" | grep -E " error|Result"
[[ "$OUT" == *"Result: Succeeded"* ]]
