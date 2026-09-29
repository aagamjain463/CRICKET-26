// Audio Director: match ambience, crowd, field sounds, player sounds, commentary ducking,
// musical stings (result only), replay treatment, priorities, concurrency, emotional intensity.
// Pure logic + selection; the game mode owns the audio components and the clock.
//
// Gameplay owns score/wickets/physics. This system consumes semantic events (ECricketEvent +
// the finished delivery) and turns them into target levels, ducking and cue picks. It never
// decides what happened on the field.

#pragma once

#include "CoreMinimal.h"
#include "CricketAudio.h"
#include "DeliveryResolver.h"
#include "SuperOverMatch.h"

namespace CricketAudioDirector
{
	/** 0..1 chase pressure from authoritative match state (never from volume or vibes). */
	float PressureLevel(const FSuperOverMatch& M);
	/** 0..1 emotional intensity twin of the commentary director's (drives crowd + tension). */
	float IntensityOf(const FDeliveryResult& R, const FDeliveryOutcome& O, const FSuperOverMatch& After);

	/** Semantic player-vocal categories. No samples ship yet: the director schedules *when* each
	 *  call happens (timing-accurate hooks); studio-recorded calls replace the silent queue. */
	enum class EVocal : uint8 { None, Howzat, Run, No, Wait, CatchCall, Celebrate, Frustrated };

	struct FState
	{
		float CrowdEnergy = 0.3f;  // smoothed 0..1 crowd level (the bed's volume)
		float CrowdTarget = 0.3f;
		float Tension = 0.f;       // pre-delivery anticipation 0..1
		float TensionTarget = 0.f;
		float DipUntil = -1.f;     // micro-drop silence around the release
		float CelebrateUntil = -1.f;
		float Duck = 1.f;          // smoothed commentary duck multiplier
		float LastPlayed[int32(CricketAudio::ECue::Count)] = {};
		bool bPlayedInit = false;
		// One-slot vocal queue (calls are serial; a new high-priority call replaces a pending one).
		bool bVocalPending = false;
		EVocal PendingVocal = EVocal::None;
		float VocalAt = 0.f;
		float Now = 0.f;

		void Reset();
	};

	/** Semantic match event from the rules (crowd + tension reaction). */
	void OnEvent(FState& S, ECricketEvent E, const FSuperOverMatch& After, float Now);
	/** Finished delivery: contact/outcome-driven crowd reaction + vocal scheduling. */
	void OnDelivery(FState& S, const FDeliveryResult& R, const FDeliveryOutcome& O, const FSuperOverMatch& After, float Now);
	/** Pre-delivery: aim tension at the chase pressure (called during Waiting/RunUp). */
	void PreDelivery(FState& S, const FSuperOverMatch& M);
	/** Release: micro-drop so the impact lands (called at DoRelease). */
	void OnRelease(FState& S, float Now);
	/** Per-frame: smooth energy/tension/duck; returns the crowd channel volume multiplier. */
	float TickCrowd(FState& S, float Dt, float Now, bool bCommentaryActive, bool bReplay);

	/** Contact-driven bat pick (cue + volume). Never one sample with only volume changed. */
	struct FSfxPick { CricketAudio::ECue Cue = CricketAudio::ECue::BatCrack; float Volume = 0.5f; };
	FSfxPick ContactSfx(const FDeliveryResult& R);
	FSfxPick KeeperSfx(const FDeliveryResult& R); // keeper take or catch behind
	FSfxPick CatchSfx(const FDeliveryResult& R);  // outfield catch pop, sized by difficulty
	float ThrowVolume(const FDeliveryResult& R);
	inline float FootstepVolume() { return 0.15f; }
	/** Run-up stride interval (s) at run-up pace; batters sprinting between wickets use SprintInterval. */
	constexpr float RunUpStride = 0.45f;
	constexpr float SprintStride = 0.35f;

	/** Concurrency: min separation per cue so footsteps/crowd/cues never stack audibly. */
	bool ShouldPlay(FState& S, CricketAudio::ECue Cue, float Now);
	void MarkPlayed(FState& S, CricketAudio::ECue Cue, float Now);

	/** Which vocal (if any) this delivery schedules, and when relative to contact. */
	struct FVocalPick { EVocal Vocal = EVocal::None; float AfterContact = 0.f; };
	FVocalPick VocalFor(const FDeliveryResult& R, const FDeliveryOutcome& O);
	/** Queue a vocal for time At (replaces a pending lesser call). Returns true if queued. */
	bool QueueVocal(FState& S, EVocal V, float At);
	/** Due vocal at Now (call once per frame; clears the slot). */
	EVocal PollVocal(FState& S, float Now);
}
