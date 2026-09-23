// Procedurally synthesised sound cues for the slice: bat on ball, edges, the ball pitching, the stumps
// and a looping crowd bed. Pure DSP (no engine audio objects), so it is testable and needs no imported
// or licensed recordings; the game mode queues the samples into procedural sound waves.

#pragma once

#include "CoreMinimal.h"

namespace CricketAudio
{
	/** Mono 16-bit at a mobile-friendly rate. */
	constexpr int32 SampleRate = 22050;

	enum class ECue : uint8 { BatCrack, EdgeTick, Bounce, Stumps, Crowd, Count };

	/** Samples for a cue. Transient cues are one-shots; Crowd is a seamless 4 s loop. */
	TArray<int16> Synthesize(ECue Cue, int32 Seed = 1);
}
