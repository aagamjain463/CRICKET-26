// Presentation Director: turns a finished delivery (or a match beat such as the toss, the innings
// break or the result) into one broadcast scene: importance level, variant, participants, shot list,
// body beats, crowd swell, graphic and replay permission. Pure logic (no actors), so automation tests
// can run whole matches through it.
//
// Like the commentary director it NEVER decides what happened. It reads the authoritative delivery
// context after the umpire's decision and only chooses how to present it. Gameplay state (score,
// wickets, strike, over, identities) is never written here.

#pragma once

#include "CoreMinimal.h"
#include "CricketCommentaryDirector.h"

namespace CricketPresentation
{
	/** FULL plays every beat, BALANCED (default) trims routine ones, QUICK keeps only big moments. */
	enum class EPacing : uint8 { Full, Balanced, Quick };

	enum class EScene : uint8
	{
		None, Toss, PreMatch, Dot, Beaten, Edge, Drop, Runs, Four, Six, Wicket, EndOfOver, Maiden, ExpensiveOver,
		InningsBreak, LastBall, MatchWinBoundary, MatchWinWicket, MatchWinRuns, MatchTied, Result, Count
	};

	enum class EMilestone : uint8 { None, Fifty, Hundred, OneFifty, TwoHundred, FiveWickets, HatTrick };

	/** Who a shot frames or a beat moves. Catcher is the fielder who completed the dismissal. */
	enum class ERole : uint8
	{
		None, Striker, NonStriker, Bowler, Keeper, Catcher, Captain, DismissedBatter, BowlingTeam, BattingPair, Umpire, Crowd, Ground
	};

	enum class EShot : uint8 { Establishing, CloseUp, Medium, TwoShot, Group, Tracking, PitchLevel, Crowd };

	enum class EAction : uint8
	{
		Idle, Celebrate, Converge, Clap, Talk, WalkOff, WalkIn, Dejected, RaiseBat, Handshake, LookAround, Gloves
	};

	enum class EGraphic : uint8
	{
		None, Toss, MatchUp, NewBatter, Wicket, Milestone, OverSummary, Target, Equation, Result, PlayerOfMatch
	};

	struct FShot
	{
		EShot Kind = EShot::Medium;
		ERole Subject = ERole::Striker;
		ERole Second = ERole::None; // two-shots
		float Start = 0.f, Duration = 1.f;
	};

	struct FBeat
	{
		ERole Who = ERole::Striker;
		EAction Action = EAction::Idle;
		ERole LookAt = ERole::None;
		float Start = 0.f, Duration = 1.f;
	};

	struct FScenePlan
	{
		EScene Type = EScene::None;
		int32 Level = 0;   // 0 routine .. 5 match-deciding
		int32 Variant = 0;
		EMilestone Milestone = EMilestone::None;
		int32 MilestoneRuns = 0; // the threshold crossed, for the graphic
		bool bMerged = false;    // more than one moment folded into this scene
		float Duration = 0.f;    // live presentation before the replay (seconds)
		float Crowd = 0.f;       // 0..1 extra crowd swell
		EGraphic Graphic = EGraphic::None;
		bool bReplay = true;     // pacing permits a replay after the scene
		bool bSkippable = true;
		TArray<FShot> Shots;
		TArray<FBeat> Beats;

		bool IsValid() const { return Type != EScene::None && Duration > 0.f; }
		/** The shot on screen at scene time T (seconds from the scene start), or nullptr past the end. */
		const FShot* ShotAt(float T) const;
		/** Every beat running for Who at scene time T, latest-started first. */
		const FBeat* BeatFor(ERole Who, float T) const;
	};

	/** Situation pressure 0..1 from the match state only (not from this ball's drama). */
	float Pressure(const CricketCommentaryDirector::FContext& C, int32 MaxWickets);

	/** Data-driven thresholds; each (player, milestone) fires exactly once per match. */
	struct FMilestoneTracker
	{
		TArray<int32> BattingThresholds = { 50, 100, 150, 200 };
		int32 BowlingHaul = 5;
		TSet<FString> Fired;
		TMap<FString, int32> WicketStreak; // bowler -> consecutive legal-ball wickets

		/** Batting milestone crossed going from Before to After runs (None if none or already fired). */
		EMilestone Batting(const FString& Batter, int32 Before, int32 After, int32& OutThreshold);
		/** Bowling milestone from this legal ball (hat-trick first). Wides and no-balls don't break a streak. */
		EMilestone Bowling(const FString& Bowler, int32 WicketsAfter, bool bBowlerWicket, bool bLegalBall);
		void Reset() { Fired.Reset(); WicketStreak.Reset(); }
	};

	/** Everything about the finished ball the director needs beyond the commentary context. */
	struct FDeliveryInput
	{
		CricketCommentaryDirector::FContext Ctx;
		int32 MaxWickets = 2;
		FString StrikerId, BowlerId;     // stable identities for milestones
		int32 StrikerRunsBefore = 0, StrikerRunsAfter = 0;
		int32 BowlerWicketsAfter = 0;
		bool bBeaten = false;            // no contact, passed close to off stump or the bat
		bool bEdge = false;              // any edge contact
		bool bOverComplete = false;
		int32 OverRuns = 0;              // runs conceded in the over just completed
		bool bKeeperCatch = false, bCaughtAndBowled = false;
	};

	struct FDirector
	{
		EPacing Pacing = EPacing::Balanced;
		FMilestoneTracker Milestones;
		TMap<uint8, TArray<int32>> RecentVariants; // scene type -> last variants, newest last
		TArray<EShot> RecentOpeners;
		FRandomStream Rng{ 2026 };

		void Reset() { Milestones.Reset(); RecentVariants.Reset(); RecentOpeners.Reset(); }
	};

	/** Number of authored variants per scene type (anti-repetition cycles through them). */
	int32 VariantCount(EScene Type);

	/** Picks the variant for Type, weighted to the ones not seen recently, and records it. */
	int32 PickVariant(FDirector& D, EScene Type);

	/** Seconds for a level at a pacing (0 means no live scene). */
	float LevelDuration(int32 Level, EPacing Pacing);

	/** Importance level of a scene type in its context (before merging). */
	int32 LevelOf(EScene Type, float Pressure);

	/** The one scene for a finished delivery: every moment of the ball merged into a single scene. */
	FScenePlan Direct(FDirector& D, const FDeliveryInput& In);

	/** Match beats that are not deliveries. */
	FScenePlan TossScene(FDirector& D);
	FScenePlan InningsBreakScene(FDirector& D);
	FScenePlan ResultScene(FDirector& D, bool bTied);

	/** Player of the match from real cards: returns the team and the batting-order index (or -1 for the bowler). */
	struct FPlayerImpact { int32 Team = -1; int32 Batter = -1; bool bBowler = false; float Score = 0.f; };
	FPlayerImpact PlayerOfMatch(const FSuperOverMatch& M);
}
