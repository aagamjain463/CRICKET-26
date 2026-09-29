// Authoritative Super Over rules and scoring. Pure data + rules: no actors, no presentation.
// Every mutation goes through Start / BeginDelivery / CompleteDelivery / StartSecondInnings /
// StartNextSuperOver, each of which refuses calls from the wrong phase, so the state cannot drift
// into an impossible combination (see CheckInvariants).

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.h"
#include "SuperOverMatch.generated.h"

UENUM(BlueprintType)
enum class EMatchPhase : uint8 { PreMatch, ReadyForDelivery, DeliveryInProgress, InningsBreak, MatchComplete };

/** Semantic events for UI, camera, audio and commentary. The rules never call those systems. */
UENUM(BlueprintType)
enum class ECricketEvent : uint8
{
	DeliveryCompleted, DotBall, RunsScored, BoundaryFour, BoundarySix, Wide, NoBall, FreeHitNext,
	Wicket, InningsCompleted, TargetSet, RequiredRunsChanged, LastBallSituation, MatchWon, MatchTied
};

USTRUCT(BlueprintType)
struct FSuperOverRules
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxLegalBalls = 6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxWickets = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bFreeHitAfterNoBall = true;
	/** Bouncers (above shoulder height) allowed per over; the next one is a no-ball. Playing conditions vary (1 or 2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaxBouncersPerOver = 1;
	/** A short ball passing above head height: wide (true) or no-ball (false), depending on the playing conditions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bOverHeadIsWide = true;
	/** Unsuccessful player reviews each side may make in a Super Over (ICC playing conditions: one). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ReviewsPerTeam = 1;
};

/** Result of one delivery as decided by the simulation (umpire's view). */
USTRUCT(BlueprintType)
struct FDeliveryOutcome
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 RunsRun = 0;   // completed runs (byes / leg byes when no bat contact)
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Boundary = 0;  // 0, 4 or 6
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bBatContact = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bLegBye = false;     // runs came off the body, not the bat
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bOverthrow = false;  // a throw went to the boundary: RunsRun + 4
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bBouncer = false;    // passed above shoulder height; counts toward the over's limit
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bWide = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bNoBall = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EDismissal Dismissal = EDismissal::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRunOutStriker = true; // which batter, for run outs
};

USTRUCT(BlueprintType)
struct FBatterCard
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Runs = 0;
	UPROPERTY(BlueprintReadOnly) int32 Balls = 0;
	UPROPERTY(BlueprintReadOnly) int32 Fours = 0;
	UPROPERTY(BlueprintReadOnly) int32 Sixes = 0;
	UPROPERTY(BlueprintReadOnly) EDismissal HowOut = EDismissal::None;
};

USTRUCT(BlueprintType)
struct FBowlerCard
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Balls = 0;
	UPROPERTY(BlueprintReadOnly) int32 Runs = 0;
	UPROPERTY(BlueprintReadOnly) int32 Wickets = 0;
	UPROPERTY(BlueprintReadOnly) int32 Wides = 0;
	UPROPERTY(BlueprintReadOnly) int32 NoBalls = 0;
};

USTRUCT(BlueprintType)
struct FInningsState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 BattingTeam = 0;
	UPROPERTY(BlueprintReadOnly) int32 Runs = 0;
	UPROPERTY(BlueprintReadOnly) int32 Wickets = 0;
	UPROPERTY(BlueprintReadOnly) int32 LegalBalls = 0;
	UPROPERTY(BlueprintReadOnly) int32 Deliveries = 0;
	UPROPERTY(BlueprintReadOnly) int32 Extras = 0;
	UPROPERTY(BlueprintReadOnly) int32 Byes = 0;
	UPROPERTY(BlueprintReadOnly) int32 LegByes = 0;
	UPROPERTY(BlueprintReadOnly) int32 Bouncers = 0;   // this over (a Super Over innings is one over)
	UPROPERTY(BlueprintReadOnly) int32 PartnershipRuns = 0;  // since the last wicket, extras included
	UPROPERTY(BlueprintReadOnly) int32 PartnershipBalls = 0; // legal balls since the last wicket
	UPROPERTY(BlueprintReadOnly) int32 Striker = 0;    // index into the batting order
	UPROPERTY(BlueprintReadOnly) int32 NonStriker = 1;
	UPROPERTY(BlueprintReadOnly) int32 NextBatter = 2;
	UPROPERTY(BlueprintReadOnly) TArray<FBatterCard> Batters;
	UPROPERTY(BlueprintReadOnly) FBowlerCard Bowler; // the current bowler's figures this innings
	/**
	 * IPL rotation: one card per bowling-side XI slot, BowlerSlot selecting the current one.
	 * Standalone matches never set a slot and keep the single aggregate card exactly as before.
	 */
	UPROPERTY(BlueprintReadOnly) TArray<FBowlerCard> BowlerCards;
	UPROPERTY(BlueprintReadOnly) int32 BowlerSlot = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) TArray<FString> BallLog;
	UPROPERTY(BlueprintReadOnly) int32 OverLogStart = 0;
	UPROPERTY(BlueprintReadOnly) bool bComplete = false;
};

USTRUCT(BlueprintType)
struct FSuperOverMatch
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FSuperOverRules Rules;
	UPROPERTY(BlueprintReadOnly) EMatchPhase Phase = EMatchPhase::PreMatch;
	UPROPERTY(BlueprintReadOnly) int32 SuperOverNumber = 0;
	UPROPERTY(BlueprintReadOnly) int32 CurrentInnings = 0;
	UPROPERTY(BlueprintReadOnly) TArray<FInningsState> Innings;
	UPROPERTY(BlueprintReadOnly) int32 Target = 0;   // 0 until the first innings is complete
	UPROPERTY(BlueprintReadOnly) int32 Winner = -1;  // team index, -1 while undecided or tied
	UPROPERTY(BlueprintReadOnly) bool bTied = false;
	UPROPERTY(BlueprintReadOnly) bool bFreeHit = false;
	int32 ReviewsLeft[2] = { 0, 0 }; // by team: a review that fails is lost, a successful one or umpire's call kept

	void Start(int32 FirstBattingTeam);
	bool BeginDelivery();
	/** Applies an umpire-decided outcome. Returns false (state untouched) if illegal for this phase/ball. */
	bool CompleteDelivery(const FDeliveryOutcome& Outcome, TArray<ECricketEvent>& OutEvents);
	bool StartSecondInnings();
	/** After a tie: the side that batted second bats first (ICC playing conditions). */
	bool StartNextSuperOver();
	/**
	 * IPL bowling change: makes XI slot Slot the current bowler, stashing the outgoing bowler's
	 * figures into BowlerCards and loading the incoming one's. Refuses calls mid-delivery or with
	 * a bad slot, so figures can never be attributed to the wrong bowler.
	 */
	bool SetBowlerSlot(int32 Slot);

	const FInningsState& Cur() const { return Innings[CurrentInnings]; }
	int32 BattingTeam() const { return Cur().BattingTeam; }
	int32 BowlingTeam() const { return 1 - Cur().BattingTeam; }
	bool IsChase() const { return CurrentInnings == 1; }
	int32 RunsRequired() const { return IsChase() ? FMath::Max(0, Target - Cur().Runs) : 0; }
	int32 BallsRemaining() const { return Rules.MaxLegalBalls - Cur().LegalBalls; }
	bool BouncerAllowed() const { return Cur().Bouncers < Rules.MaxBouncersPerOver; }
	/** Broadcast pressure line, e.g. "11 REQUIRED FROM 4". */
	FString PressureText() const;
	bool CheckInvariants(FString& OutError) const;

private:
	FInningsState& Mut() { return Innings[CurrentInnings]; }
	void BeginInnings(int32 BattingTeam);
	void FinishInnings(TArray<ECricketEvent>& OutEvents);
};
