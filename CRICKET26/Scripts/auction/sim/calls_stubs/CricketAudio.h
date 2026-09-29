// Headless stand-in: no audio files, so every line stays a caption.
#pragma once
#include "CoreMinimal.h"

namespace CricketAudio
{
	constexpr int32 SampleRate = 22050;
	inline TArray<int16> LoadClip(const FString&, bool = true) { return {}; }
}
