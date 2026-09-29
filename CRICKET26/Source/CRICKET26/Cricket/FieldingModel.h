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

/** Fields a captain can load. The first two are the AI's stock fields (PresetFor); every one is legal to either hand. */
enum class EFieldPreset : uint8
{
	PaceDeath, SpinDefensive, Balanced, Attacking, PaceAttack, SpinAttack, BoundaryRiders, OffSideHeavy, LegSideHeavy,
	YorkerDefence, ShortBall, Count
};

enum class EFieldRole : uint8 { Primary, Backup, Chase, CoverStumps, Relay };

/** What the primary fielder does with the ball. The kind of take sets how soon they can throw. */
enum class EFieldAction : uint8
{
	None,
	// In the air
	CatchFlat, CatchHigh, CatchLow, CatchDiving, CatchKeeper, CatchBoundary, CatchRelay,
	// Along the ground
	KeeperTake, PickupClean, PickupOnRun, LongBarrier, SlideStop, DiveStop, Fumble
};

enum class EThrowType : uint8 { None, Overarm, Underarm, Relay };

/** A fielder who leaves their mark: runs from Home toward Target from Start (s after contact), physically. */
struct FFielderMove
{
	int32 Fielder = -1;
	EFieldRole Role = EFieldRole::Primary;
	float Start = 0.f;
	FVector2D Target = FVector2D::ZeroVector;
};

struct FFieldingOutcome
{
	int32 Boundary = 0;
	float BoundaryTime = 0.f;
	int32 Fielder = -1;             // who gets to the ball (or catches it)
	float ChaseStart = 0.f;         // times are seconds after contact
	float FieldTime = 0.f;
	FVector FieldPos = FVector::ZeroVector;
	bool bDive = false;             // only reached at full stretch: slower to get up and throw
	bool bCatchChance = false;
	bool bCaught = false;
	float CatchDifficulty = 0.f;
	FVector2D FielderFrom = FVector2D::ZeroVector; // where the primary ran from: throwing back across it costs a turn
	EFieldAction Action = EFieldAction::None;
	int32 CatchPartner = -1;        // took the relay catch after the catcher's momentum carried them to the rope

	// Coordination: every fielder who moves, and when each end's stumps are manned for a throw.
	TArray<FFielderMove> Moves;
	float CoverTime[2] = { 0.f, 0.f }; // [0] striker's end (keeper), [1] bowler's end (bowler)
};

/**
 * The player calling the runs instead of the batters' own judgement. Times are seconds after contact: Go holds when
 * each run was called (a call made before the batters ground their bats at the end of the last run is a running
 * turn), Back when they were sent back (-1: never). A turn back is refused once TurnBackLimit of the leg is run.
 */
struct FRunCalls
{
	bool bManual = false;
	TArray<float> Go;
	float Back = -1.f;
	static constexpr float TurnBackLimit = 0.72f;
};

struct FRunningOutcome
{
	int32 Attempted = 0;            // runs set off for
	int32 Completed = 0;
	bool bRunOut = false;
	bool bRunOutStriker = false;    // original striker is the one out
	bool bThrowToStrikerEnd = true;
	bool bDirectHit = false;
	EThrowType ThrowType = EThrowType::None;
	float ThrowRelease = 0.f;       // seconds after contact
	FFielderMove RelayMove;         // the fielder who takes a relay throw (Fielder -1 if none)
	float RelayCatch = 0.f, RelayRelease = 0.f;
	float ThrowArrive = 0.f;
	float BreakTime = 0.f;          // when the stumps can be broken
	TArray<float> RunTimes;         // completion time of each run carried through (the last may be run out)
	TArray<float> Leaves;           // when the batters set off on each run attempted, including one sent back
	bool bSentBack = false;         // the last run was called off once the ball was gathered
	float SentBackAt = 0.f;         // when they turned back
	float SentBackFrom = 0.f;       // how far along that run they were (0..1)
	float BackIn = 0.f;             // when they regained their ground (or would have)
	float Margin = 0.f;             // how long the last runner was home before the stumps could be broken (negative: run out)

	/** When the last runner is home or out. */
	float EndTime() const { return FMath::Max(RunTimes.Num() ? RunTimes.Last() : 0.f, BackIn); }
};

namespace CricketField
{
	EFieldPreset PresetFor(EBowlerType Type);
	/** Short label for the field editor's preset buttons. */
	const TCHAR* PresetName(EFieldPreset Preset);
	TArray<FFielder> Make(EFieldPreset Preset, ECricketHand BatHand, ECricketHand BowlHand);

	// Field placement rules. The laws: at the instant of delivery no fielder on the pitch (28.5) and no more than
	// two besides the keeper behind the popping crease on the leg side (28.4); Super Over playing conditions: at
	// most five outside the 30-yard circle. The game adds: inside the rope, and no two fielders on the same spot.
	constexpr float InnerRing = 27.43f;   // the 30-yard circle: this far from either middle stump, joined along the pitch
	constexpr int32 MaxOutside = 5;
	constexpr int32 MaxBehindSquareLeg = 2;
	constexpr float RopeMargin = 3.f;     // a fielder stands at least this far inside the boundary
	constexpr float MinSpacing = 3.f;
	/** Distance from the pitch's centre line segment: over InnerRing is outside the circle. */
	float RingDistance(const FVector2D& Home);
	/** The two counted rules' tests, shared by Validate and the field editor's counters. */
	bool IsOutsideCircle(const FVector2D& Home);
	bool IsBehindSquareLeg(const FVector2D& Home, ECricketHand BatHand);
	/** Why this field is illegal (empty: legal); Offender is the fielder it is about, when there is one. Keeper and bowler are never moved. */
	FString Validate(const TArray<FFielder>& Field, ECricketHand BatHand, int32* Offender = nullptr);
	/** The fielding position for a spot, for this batter; bConfident false when it sits on a boundary between two names. */
	FString PositionName(const FVector2D& Home, ECricketHand BatHand, bool* bConfident = nullptr);

	/** Seconds to cover Dist metres from standing, accelerating at 6 m/s^2 up to TopSpeed. */
	float TimeToCover(float Dist, float TopSpeed);
	/** Inverse of TimeToCover: metres covered T seconds after setting off. */
	float DistanceCovered(float T, float TopSpeed);
	/** Where a moving fielder is Post seconds after contact. */
	FVector2D PositionOf(const FFielderMove& Move, const FFielder& Who, float Post, float TopSpeed);

	/**
	 * Samples are post-contact ball states at a fixed Dt, starting at contact. KeeperLead is how long
	 * (s) the keeper has already been tracking the ball's line at the first sample - a keeper reacts to
	 * a beaten ball off the pitch, not when it passes the stumps.
	 * Picks the primary interceptor (the first fielder who can physically reach the ball's path), then
	 * coordinates the rest: a backup on the line behind them, a boundary rider chasing a ball they cannot
	 * stop, and the keeper and bowler running to the stumps.
	 */
	FFieldingOutcome SolveFielding(const TArray<FBallState>& Samples, float Dt, const TArray<FFielder>& Field,
		const FCricketPlayer& FieldingSkill, bool bBatContact, FRandomStream& Rng, float KeeperLead = 0.f);
	/** The primary interception alone (no coordination). */
	FFieldingOutcome Intercept(const TArray<FBallState>& Samples, float Dt, const TArray<FFielder>& Field,
		const FCricketPlayer& FieldingSkill, bool bBatContact, FRandomStream& Rng, float KeeperLead = 0.f);

	/**
	 * Margin is the time buffer (s) the batters insist on; smaller = riskier. The first run is called at
	 * the stroke on a judgement of how quickly the ball will be gathered and returned (off by a seeded
	 * misjudgement); each further run is called at the turn, knowing the truth if the ball is in hand by
	 * then. A run shown to be lost once the ball is gathered is called off if they are not yet halfway.
	 * The throw is overarm, underarm (a close pickup on the run) or, from the deep, relayed through a free
	 * fielder when that is quicker; the relay needs Field.
	 */
	FRunningOutcome SolveRunning(const FFieldingOutcome& Fielding, const FCricketPlayer& Striker, const FCricketPlayer& NonStriker,
		const FCricketPlayer& FieldingSkill, bool bKeeperFielded, float Margin, FRandomStream& Rng, const TArray<FFielder>* Field = nullptr,
		const FRunCalls* Calls = nullptr);
	/** Flight time (s) of a throw over Dist metres: long throws have to be lobbed and lose pace. */
	float ThrowFlight(float Dist, float Throwing);
	const TCHAR* ActionName(EFieldAction Action);
}
