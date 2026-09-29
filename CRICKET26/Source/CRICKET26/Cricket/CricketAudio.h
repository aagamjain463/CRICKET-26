// Procedurally synthesised sound cues for the slice: bat on ball (contact-driven families), edges,
// the ball pitching, pads, keeper gloves, catches, throws, footsteps, the stumps and a looping crowd
// bed. Pure DSP (no engine audio objects), so it is testable and needs no imported or licensed
// recordings; the game mode queues the samples into procedural sound waves.
//
// These are timing- and mix-accurate placeholders with a real cricket envelope: licensed or
// studio-recorded samples replace them cue-for-cue (same ECue ids, same selection rules) when audio
// assets are sourced. Nothing here imitates a real ground, broadcast or player voice.

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.h"

namespace CricketAudio
{
	/** Mono 16-bit at a mobile-friendly rate. */
	constexpr int32 SampleRate = 22050;
	/**
	 * Gain on every channel. The mixer's platform headroom and the mono-to-stereo pan take about 9 dB off a
	 * full-scale cue; with this a middled bat crack peaks at about -7 dBFS in the recorded game mix, unclipped.
	 */
	constexpr float MixGain = 2.f;

	// NOTE: the first five values are stable (automation + saved mixes reference them by index).
	enum class ECue : uint8
	{
		BatCrack,   // generic good contact (legacy; kept for compatibility)
		EdgeTick,   // generic edge (legacy; per-edge variants below for new code)
		Bounce,     // ball pitching: hard-length thud (legacy)
		Stumps,     // stumps + bails (legacy)
		Crowd,      // seamless 4 s crowd bed loop (legacy)
		BatMiddle,  // perfect middle: clean, bright, satisfying crack
		BatToe,     // toe-end: duller, less ring
		PadThud,    // ball into pad / body: soft, low, no ring
		KeeperGlove,// keeper take: short leather snap
		CatchPop,   // outfield catch: duller hand pop, slight give
		ThrowRelease,// throw release: short, subtle (never a whoosh)
		Footstep,   // single footfall: very short, very quiet
		Count
	};

	/** A raw 22.05 kHz mono s16le clip under Content/Audio (Rel: "Sfx/Bounce.pcm"); empty when absent. Kept in memory
	 *  after the first load, to spare the game thread the disk, unless bKeep is false (large one-off libraries). */
	TArray<int16> LoadClip(const FString& Rel, bool bKeep = true);

	/** Ensures a commentary clip exists on disk; on supported platforms (macOS), generates it via TTS if missing. */
	bool EnsureClip(const FString& Text);

	/** Samples for a cue. Transient cues are one-shots; Crowd is a seamless 4 s loop. */
	TArray<int16> Synthesize(ECue Cue, int32 Seed = 1);

	/** Contact-driven bat selection: perfect middle sounds different from a mistimed edge. */
	ECue SelectBatCue(EContactZone Zone, float Quality);
	/** Volume 0..1 for the selected contact cue. */
	float BatVolume(EContactZone Zone, float Quality);
	/** Pitch bounce volume from ball speed; spin/short balls stay believable, never cartoon. */
	float PitchVolume(float SpeedKph);

	/** Mix buses. Logical today (GameMode scales its two procedural channels with them); each maps
	 *  1:1 onto a future Unreal submix / SoundClass. Nothing feeds the master uncontrolled. */
	enum class EMixBus : uint8 { Master, Commentary, Crowd, FieldSfx, BatBall, PlayerVocal, Ambience, Ui, Music, Count };
	/** Static bus trim, linear gain. Commentary and impacts sit above the bed; ambience below it. */
	float BusTrim(EMixBus Bus);
	/** Crowd ducking while voiced commentary is active: slight, never a mute (0..1 multiplier). */
	constexpr float CommentaryDuck = 0.8f;
	/** Crowd scale inside action replays: reduced bed, impact emphasised. */
	constexpr float ReplayCrowdScale = 0.6f;

	/** Asset validation for automated QA: non-empty, finite peak, audible, never clipped. */
	bool ValidateCue(const TArray<int16>& Pcm, float MinPeak = 0.2f);
}
