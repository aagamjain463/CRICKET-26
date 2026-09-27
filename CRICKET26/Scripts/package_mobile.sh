#!/bin/zsh
# Mobile package entry point (M1). Fails with an actionable message when the
# owner-supplied toolchain is missing instead of a UAT stack trace.
# Owner must supply: Android SDK+NDK, or UE iOS platform + signing identity.
ROOT="${0:A:h:h}"
PLATFORM="$1"
if [[ "$PLATFORM" != "Android" && "$PLATFORM" != "IOS" ]]; then
  echo "usage: package_mobile.sh [Android|IOS]"
  exit 2
fi
if [[ "$PLATFORM" == "Android" ]]; then
  if [[ -z "$ANDROID_HOME" && -z "$ANDROID_SDK_ROOT" ]]; then
    echo "BLOCKED: no Android SDK (ANDROID_HOME/ANDROID_SDK_ROOT unset). Install Android Studio SDK + NDK, accept licences, rerun."
    exit 1
  fi
  UE="${UE_EDITOR:-/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"
  "$UE" "$ROOT/CRICKET26.uproject" -run=BuildCookRun -platform=Android -clientconfig=Shipping -cook -stage -package -pak -compressed -target=CRICKET26
else
  if [[ ! -d "/Users/Shared/Epic Games/UE_5.8/Engine/Platforms/IOS" ]]; then
    echo "BLOCKED: UE iOS target platform missing in this install. Reinstall engine with iOS support."
    exit 1
  fi
  if ! security find-identity -v -p codesigning 2>/dev/null | grep -q "iPhone"; then
    echo "BLOCKED: no iOS code-signing identity in keychain. Add Apple dev cert + provisioning."
    exit 1
  fi
  UE="${UE_EDITOR:-/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor}"
  "$UE" "$ROOT/CRICKET26.uproject" -run=BuildCookRun -platform=IOS -clientconfig=Shipping -cook -stage -package -pak -compressed -target=CRICKET26
fi
