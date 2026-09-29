// Commentary Director: event interpretation, context, speaker choice, excitement, priority, timing,
// anti-repetition, history, interruptions, handoffs, cooldowns. Pure logic (no audio objects), so
// automation tests can run whole Super Overs through it.
//
// The director NEVER decides what happened: it consumes the authoritative delivery result, the
// umpire-given outcome and the match state after the ball, and only picks how (and whether) to
// describe it. Gameplay owns score, wickets, physics and results.

#pragma once

#include "CoreMinimal.h"
#include "CricketCommentary.h"
#include "DeliveryResolver.h"
#include "SuperOverMatch.h"

namespace CricketCommentaryDirector
{
	/** Everything a commentary decision may read. Built once per finished delivery. */
	struct FContext
	{
		int32 Innings = 0;
		int32 SuperOverNumber = 0;
		FString BattingTeam, BowlingTeam;
		int32 BattingTeamIndex = 0;
		FString Striker, NonStriker, Bowler;
		ECricketHand BatterHand = ECricketHand::Right;
		EBowlerType BowlerType = EBowlerType::Pace;
		int32 Score = 0, Wickets = 0, LegalBalls = 0, BallsRemaining = 6;
		int32 Target = 0, RunsRequired = 0;
		bool bChase = false, bFinalBall = false, bFreeHit = false;
		bool bMatchComplete = false, bTied = false;
		int32 Winner = -1;
		bool bInningsBreak = false;
		// This ball (authoritative simulation data only).
		bool bBatContact = false;
		EContactZone Zone = EContactZone::Miss;
		EShotType Shot = EShotType::Leave;
		float ContactQuality = 0.f;
		float TimingError = 0.f;
		int32 Boundary = 0, RunsRun = 0;
		bool bWide = false, bNoBall = false, bLegBye = false, bOverthrow = false;
		bool bBouncer = false, bBeamer = false;
		float BallSpeedKph = 0.f;
		EDismissal Dismissal = EDismissal::None;
		bool bRunOutStriker = true, bDirectHit = false;
		bool bCaughtBehind = false, bDivingCatch = false;
		float CatchDifficulty = 0.f;
		bool bDroppedCatch = false;
		bool bCloseRunOut = false;
		bool bPadImpact = false;
		FString Region;
		FString FielderPosition;
		int32 PartnershipRuns = 0, PartnershipBalls = 0;

		static FContext Build(const FDeliveryResult& R, const FDeliveryOutcome& O, const FSuperOverMatch& After,
			const CricketCommentary::FNames& Names, float OffSign, EDeliveryType DeliveryType);
	};

	/** 0 calm .. 1 match-deciding climax, from actual match context (never from volume). */
	float ComputeIntensity(const FContext& C);
	/** 0 ambient .. 4 match-deciding. High priority may cancel pending low-priority lines. */
	int32 PriorityOf(const FContext& C);

	/** Short-term memory: recent line ids, opening phrases, topics, speakers, recent events. */
	struct FMemory
	{
		TArray<FString> RecentIds;      // last spoken line ids (cap 12)
		TArray<FString> RecentOpenings; // first two words of last lines (cap 8)
		TArray<FString> RecentTopics;   // tags of last lines (cap 6)
		TArray<int32> RecentEvents;     // event kinds, newest last (cap 6): 0 dot 1 runs 2 four 3 six 4 wicket 5 extra
		TMap<FString, int32> CooldownUntil; // line id -> ball index when reusable
		FString LastSpeaker;            // "A" play-by-play / "B" analyst
		FString LastId;
		int32 ConsecutiveSixes = 0, ConsecutiveBoundaries = 0, ConsecutiveDots = 0, ConsecutiveWickets = 0;
	};

	struct FPending
	{
		bool bHas = false;
		CricketCommentary::FLine Line;
		FString Text;
		int32 BallIndex = 0;
		float AvailableAt = 0.f; // earliest speak time (lets crowd/replay breathe first)
		float ExpiresAt = 0.f;   // stale after this: dropped, never played late
	};

	struct FState
	{
		FMemory Mem;
		FPending Queue;          // one analyst handoff at most (commentary is serial)
		FString CurrentId;
		CricketCommentary::FLineMeta CurrentMeta;
		float CurrentEndsAt = -1.f;
		FString CurrentSpeaker;
		void Reset();
	};

	struct FSelection
	{
		bool bSilent = false;     // professional silence: crowd/stadium carry this ball
		FString Text;             // filled caption / voice script
		FString Body, Suffix; // Text split for voicing: the line, then the situation call (may be empty)
		CricketCommentary::FLineMeta Meta;
		float DelaySeconds = 0.f; // wait after the event before speaking
		bool bInterrupted = false;// this selection cancelled a pending/lower line
	};

	/** Pick (or deliberately skip) the commentary for a finished delivery. Advances memory/queue. */
	FSelection SelectLine(FState& S, const FContext& C, const CricketCommentary::FNames& Names,
		float NowSeconds, int32 BallIndex, FRandomStream& Rng);

	/** Drop stale queue entries and finished current lines. */
	void Update(FState& S, float NowSeconds, int32 BallIndex);
	bool IsSpeaking(const FState& S, float NowSeconds);
	/** The analyst handoff once its slot opens (empty until then). Taking it makes the analyst the current speaker. */
	FString TakeHandoff(FState& S, float NowSeconds);
}
