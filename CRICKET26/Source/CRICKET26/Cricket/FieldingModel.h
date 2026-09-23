// Field placements, the interception solver, catches, throws and running between the wickets.
// Everything here is decided from the simulated ball path, fielder positions and attributes:
// a fielder gets to a ball only if they can physically reach that point before the ball does.

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.h"
#include "BallSimulation.h"

struct FFielder
{
	FString Position;
	FVector2D Home = FVector2D::ZeroVector; // simulation frame (m)
	bool bKeeper = false;
	bool bBowler = false;
};

enum class EFieldPreset : uint8 { PaceDeath, SpinDefensive };

struct FFieldingOutcome
{
	int32 Boundary = 0;
	float BoundaryTime = 0.f;
	int32 Fielder = -1;             // who gets to the ball (or catches it)
	float ChaseStart = 0.f;         // times are seconds after contact
	float FieldTime = 0.f;
	FVector FieldPos = FVector::ZeroVector;
	bool bCatchChance = false;
	bool bCaught = false;
	float CatchDifficulty = 0.f;
};

struct FRunningOutcome
{
	int32 Attempted = 0;            // runs set off for
	int32 Completed = 0;
	bool bRunOut = false;
	bool bRunOutStriker = false;    // original striker is the one out
	bool bThrowToStrikerEnd = true;
	bool bDirectHit = false;
	float ThrowRelease = 0.f;       // seconds after contact
	float ThrowArrive = 0.f;
	float BreakTime = 0.f;          // when the stumps can be broken
	TArray<float> RunTimes;         // completion time of each attempted run
};

namespace CricketField
{
	EFieldPreset PresetFor(EBowlerType Type);
	TArray<FFielder> Make(EFieldPreset Preset, ECricketHand BatHand, ECricketHand BowlHand);

	/** Seconds to cover Dist metres from standing, accelerating at 6 m/s^2 up to TopSpeed. */
	float TimeToCover(float Dist, float TopSpeed);

	/** Samples are post-contact ball states at a fixed Dt, starting at contact. */
	FFieldingOutcome SolveFielding(const TArray<FBallState>& Samples, float Dt, const TArray<FFielder>& Field,
		const FCricketPlayer& FieldingSkill, bool bBatContact, FRandomStream& Rng);

	/** Margin is the time buffer (s) the batters insist on; smaller = riskier. */
	FRunningOutcome SolveRunning(const FFieldingOutcome& Fielding, const FCricketPlayer& Striker, const FCricketPlayer& NonStriker,
		const FCricketPlayer& FieldingSkill, bool bKeeperFielded, float Margin, FRandomStream& Rng);
}
