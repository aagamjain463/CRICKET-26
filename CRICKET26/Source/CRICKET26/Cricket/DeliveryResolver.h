// Resolves one delivery end to end: release -> flight -> pitch -> bat/pad/stumps -> field -> runs.
// Pure and deterministic for a given (release, input, context). Presentation replays BallPath in
// real time; when the player's input arrives mid-flight the delivery is re-resolved with it, and
// because the simulation is deterministic the already-played part of the path is identical.

#pragma once

#include "CoreMinimal.h"
#include "BattingModel.h"
#include "FieldingModel.h"
#include "SuperOverMatch.h"

struct FBatInput
{
	EBatIntent Intent = EBatIntent::Leave;
	float DirectionDeg = 0.f;
	float PressTime = -1.f; // seconds after release
	bool IsShot() const { return Intent != EBatIntent::Leave && PressTime >= 0.f; }
};

struct FResolveContext
{
	FCricketPlayer Striker, NonStriker, Bowler, Fielding;
	TArray<FFielder> Field;
	FPitchConditions Conditions;
	float RunMargin = 0.4f;
	bool bFreeHit = false;
	FSuperOverRules Rules;
	int32 BouncersBowled = 0;  // this over, before this delivery
	int32 Seed = 0;
};

/** What the batter can see of a delivery at a moment in time. */
struct FBallRead
{
	float PitchX = 0.f;        // predicted pitching distance from the striker's stumps
	float PitchLine = 0.f;     // batter-relative, + off side
	float HeightAtBat = 0.f;   // predicted height at the front-foot contact plane
	float ArrivalTime = 0.f;   // predicted time at the front-foot contact plane
	bool bPitched = false;     // the ball had already pitched when read
};

struct FDeliveryResult
{
	static constexpr float SampleDt = CricketBall::FixedDt;
	TArray<FVector> BallPath; // from release; ball position for playback
	float SpeedKph = 0.f;
	bool bNoBall = false;
	bool bWide = false;
	bool bBeamer = false;          // full toss above waist height: no-ball
	bool bBouncer = false;         // passed (or would have) above shoulder height
	bool bRunsAllowed = true;      // false: dead ball off the pad with no stroke offered (no leg byes)
	FVector PitchPos = FVector::ZeroVector;
	float PitchTime = -1.f;
	FShotProfile Shot;
	FContactResult Contact;
	bool bTooLate = false;
	bool bPadImpact = false;
	bool bStumpsHit = false;
	float StumpsTime = 0.f;
	EDismissal Dismissal = EDismissal::None;
	FFieldingOutcome Fielding;
	FRunningOutcome Running;
	float ContactTime = 0.f;   // absolute (s after release); fielding/running times are relative to this
	float DeadTime = 0.f;
	FString Summary;

	FDeliveryOutcome ToOutcome() const;
	FVector BallAt(float Time) const;
};

namespace CricketDelivery
{
	FBallRead Read(const FBallState& Release, float AtTime, const FPitchConditions& Conditions);
	FDeliveryResult Resolve(const FDeliveryRelease& Release, const FBatInput& Input, const FResolveContext& Context);
	FString ZoneName(EContactZone Zone);
	FString ShotName(EShotType Shot);
	/** The player's timing grade for a swing: within PerfectTiming of ideal, within GoodTiming, else early or late. */
	constexpr float PerfectTiming = 0.015f, GoodTiming = 0.04f;
	FString TimingName(float TimingError);
}
