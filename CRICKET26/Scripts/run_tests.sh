#!/bin/zsh
# Builds the editor target and runs the CRICKET26 automation tests headlessly.
# Usage: Scripts/run_tests.sh [TestFilter]   (default: CRICKET26.)
set -e
ROOT="${0:A:h:h}"
ENGINE="/Users/Shared/Epic Games/UE_5.8/Engine"
FILTER="${1:-CRICKET26.}"
"$ENGINE/Build/BatchFiles/Mac/Build.sh" CRICKET26Editor Mac Development -Project="$ROOT/CRICKET26.uproject" -WaitMutex | grep -E "error|Result" || true
LOG="$ROOT/Saved/TestRun.log"
"$ENGINE/Binaries/Mac/UnrealEditor-Cmd" "$ROOT/CRICKET26.uproject" -ExecCmds="Automation RunTests $FILTER; Quit" \
  -unattended -nullrhi -nosplash -nosound -NoZen -log -abslog="$LOG" > /dev/null 2>&1 || true
echo "PASS: $(grep -c 'Result={Success}' "$LOG")  FAIL: $(grep -c 'Result={Fail' "$LOG")"
grep -E "Result=\{Fail" "$LOG" | sed -E 's/.*Path=\{([^}]*)\}.*/FAILED \1/'
grep -E "LogAutomationController: Error|LogTemp: Display" "$LOG" | sed -E 's/^\[[^]]*\]\[[ 0-9]*\]//' | head -80
