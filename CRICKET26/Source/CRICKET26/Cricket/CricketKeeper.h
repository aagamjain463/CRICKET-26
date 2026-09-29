// Wicketkeeping as geometry and selection, kept isolated from batting, bowling,
// general fielding, ball physics, scoring and dismissal rules.
//
// The simulation (DeliveryResolver + FieldingModel) stays authoritative for where
// the ball goes, who gets there and whether it is out. This module only decides
// what the keeper SHOULD look like doing it: where they stand, which take family
// the trajectory calls for, whether it is reachable, what footwork it needs, and
// where the gloves go. Presentation (SuperOverGameMode) renders the plan; it
// never feeds back into outcomes.
//
// Units and frame: simulation metres, seconds. X runs from the striker's stumps
// (X=0) to the bowler's (X=20.12), Z up, Y lateral. See CricketTypes.h.

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.h"

struct FFielder;
struct FFielderMove;

namespace CricketKeeper
{
	/** Take families. One family per genuinely different body shape; never one
	 *  generic catch for everything. Mirrored only where biomechanically valid
	 *  (off vs leg stays distinct: the stance, gloves and guard differ). */
	enum class EKeeperTake : uint8
	{
		CentralChest,   // routine pace take around chest/waist height, body behind the line
		CentralWaist,
		LowCentral,     // through ankles/knees/hips, not the spine
		HighCentral,    // feet reposition, arms rise, elbows stay valid
		SmallOff,       // efficient shuffle, stance adjust, no dive
		SmallLeg,
		LateralOff,     // stronger push, crossover, body travels then receives
		LateralLeg,
		LowLateral,
		HighLateral,
		DiveOff,        // full stretch: push-off, flight, landing, recovery
		DiveLeg,
		BounceTake,     // post-bounce trajectory, gloves under/behind the ball
		EdgeCatch,      // late sharp reaction AFTER bat contact, small correction
		EdgeDive,       // edge with large deviation: explosive push, dive
		SpinTake,       // standing up: low compact stance, beside the stumps
		SpinLegTake,    // turn down leg: footwork + body rotation + glove path
		Stumping,       // take -> control -> gloves to wicket -> break
		RunOutTake,     // receive a throw at the striker's end, then break
		Miss            // physically unreachable: honest miss, bye, chase
	};

	/** Solver owns reachability after footwork. A selected take is either
	 *  standing or diving; balls assigned elsewhere remain visual misses. */
	enum class EKeeperReach : uint8 { Reachable, DiveRequired, Unreachable };

	/** Footwork the take needs. Feet move first; hands never chase while the
	 *  feet stay frozen. */
	enum class EKeeperFootwork : uint8
	{
		Set,        // already behind the line, weight shift only
		Shuffle,    // small lateral shuffle / stance adjust (<= ~0.9 m)
		Crossover,  // larger displacement: push + crossover + travel (> ~0.9 m)
		PushDive,   // explosive push-off into flight
		Hold        // unreachable: stay honest, then turn and chase
	};

	/** Semantic keeper markers. Reuse where equivalents exist; these name the
	 *  moments QA and audio/presentation hook onto. */
	enum class EKeeperEvent : uint8
	{
		Ready,            // balanced reactive state at release
		PushOff,          // dive launch
		CatchContact,     // ball meets gloves (== FieldTime)
		SecureBall,       // ball controlled after absorb
		StumpBreak,       // gloves at the wicket (== BrokenTime)
		ThrowRelease,     // keeper's throw leaves the hand
		GroundContact,    // dive landing
		RecoveryComplete  // back to ready
	};

	/** Depth tiers. Pace stands back, spin stands up, medium/slower pace sits
	 *  between. Never one universal distance. */
	// Where this simulation's good-length ball has come down to the top of the knees: a 135 kph ball off 6 m is
	// ~0.5 m at 12 m, and every 5 kph carries it ~0.9 m further. At 9 m + 5 cm/kph a stock ball reached a 15.6 m
	// keeper on the grass, and the gloves had to go to the ground for every take.
	constexpr float PaceDepthAt135 = 11.8f;    // m behind the striker's stumps
	constexpr float PaceDepthPerKph = 0.18f;
	constexpr float PaceDepthMin = 7.0f;
	constexpr float PaceDepthMax = 16.0f;
	constexpr float SpinDepth = 0.9f;          // standing up, just behind the stumps
	constexpr float StandingUpRadius = 3.0f;   // Home.Size() below this: standing up

	/** Reach envelopes, kept identical to FieldingModel's keeper values. */
	constexpr float KeeperReach = 1.5f;
	constexpr float KeeperDiveReach = 2.8f;
	constexpr float KeeperArmsLength = 0.5f;
	constexpr float KeeperGloveReach = 0.9f; // chest to glove target on this rig
	/** Existing captured dive travels sideways at glove height. Low and near-line takes use grounded footwork. */
	inline bool UseSideDive(const FVector2D& Home, const FVector& Take, bool bDive)
	{
		return bDive && Take.Z >= 0.55f && FMath::Abs(Take.Y - Home.Y) > 0.75f;
	}
	constexpr float BehindTheLine = 0.55f; // m a take on the feet is stepped fully behind
	/** The step across into a take on the feet, on top of the solver's run, which only brings the keeper within
	 *  KeeperReach of the ball by the take. A small take is stepped fully behind; a wide one lunges to within an
	 *  arm of it, so the gloves reach it without over-stretching. GapY: take minus body at the take, lateral m. */
	float TakeSideStep(float GapY);
	/** Where the keeper's body is at Post through a take on the feet: the solver's Run (null: none, stays home),
	 *  the step across from StepFrom to FieldTime, and once the ball is in the gloves the body stops there. The
	 *  Run is sent at the ball itself; it never carries on through the take point. */
	FVector2D TakeBodyAt(const FFielderMove* Run, const FFielder& Keeper, float RunSpeed, const FVector& Take,
		float FieldTime, float StepFrom, float Post);
	/** A take on the feet within the dive envelope is shuffled across (StepFeet steps the legs), never run, however
	 *  quick the shuffle: the walk/jog clips stay out until the get-up is done at FieldTime + 0.85 s. Beyond the
	 *  envelope (skiers, top edges) the keeper turns and runs on the locomotion clips. Body: at the take. */
	inline bool ShufflesToTake(const FVector2D& Home, const FVector2D& Body, float FieldTime, float Post)
	{
		return Post < FieldTime + 0.85f && FVector2D::Distance(Home, Body) <= KeeperDiveReach;
	}

	/** Where the keeper stands for this bowler and batter. X is negative
	 *  (behind the striker's stumps), Y offset to the off side. */
	FVector2D KeeperHome(EBowlerType BowlerType, float PaceKph, ECricketHand BatHand);
	/** Depth only (positive metres behind the stumps). */
	float KeeperDepth(EBowlerType BowlerType, float PaceKph);
	/** Standing up to spin (close) vs standing back to pace. */
	inline bool IsStandingUp(const FVector2D& Home) { return Home.Size() < StandingUpRadius; }

	/** Ready-stance geometry for this keeper: pelvis drop (m), chest bend
	 *  (deg), glove height above ground (m) and forward offset (m). */
	struct FKeeperReady
	{
		float Drop = 0.22f;        // pelvis below standing
		float ChestBend = 18.f;   // torso forward
		float GloveHeight = 0.55f;// gloves by the knees, presented naturally
		float GloveForward = 0.4f;
	};
	FKeeperReady ReadyFor(bool bStandingUp);

	/** Pelvis drop (m) through a delivery, as broadcast keepers move: relaxed
	 *  between balls, sinking into a deep squat on the toes as the bowler
	 *  gathers, rising into ReadyFor()'s half-crouch as the ball pitches.
	 *  Ttr is TimeToRelease (negative before release); RiseAt is when the
	 *  keeper rises (s after release: the bounce, or just before the bat),
	 *  negative for not yet known. Continuous in Ttr: never a snap. */
	constexpr float RelaxedDrop = 0.12f;
	float StanceDrop(bool bStandingUp, float Ttr, float RiseAt);
	/** The pelvis drop through a take on the feet, Receive 0..1 in and back out after the hold: a high ball
	 *  releases the set to stand taller, a low one sinks with it, and both come back up once it is secured. */
	constexpr float MaxTakeDrop = 0.42f; // m, the gather squat: any lower and a knee goes down on a take across
	float TakeDrop(float Drop, float ReadyDrop, float Height, float Receive);

	/** Where the balls of the feet belong for a keeper standing at Centre
	 *  facing Forward (unit, horizontal, sim m) at this pelvis drop: wider
	 *  and a touch ahead of the pelvis as the squat deepens. */
	void StanceFeet(const FVector2D& Centre, const FVector2D& Forward, float Drop, FVector2D Out[2]);

	/** Planted feet. A foot only moves while stepping (off the ground), so a
	 *  shuffle is real steps, never a slide under a moving body. [0] left. */
	struct FKeeperFeet
	{
		FVector2D Ball[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };
		FVector2D From[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };
		float Phase[2] = { 1.f, 1.f }; // through the step, 1 planted
		float Rest = 0.f;              // s both feet have been down
		bool bValid = false;
	};
	constexpr float StepTrigger = 0.08f; // m a foot may lag where it belongs before it steps
	constexpr float SettleTrigger = 0.03f; // and once the feet have been down a moment: a small step to square up
	constexpr float StepSeconds = 0.14f;
	constexpr float StepLift = 0.06f;    // m, the peak of a shuffle step
	/** Moves Feet toward Ideal by steps, the lead foot first, the other once
	 *  the first is most of the way down. OutLift is each foot's height (m). Dt outside
	 *  (0, 0.2] s or a jump past 1.5 m (new ball, cut, replay) snaps. */
	void StepFeet(FKeeperFeet& Feet, const FVector2D Ideal[2], float Dt, float OutLift[2]);

	/** What the keeper should do about this ball. All inputs are real
	 *  trajectory data, never a generic CatchBall event.
	 *
	 *  @param Home        keeper's starting spot (sim frame, m)
	 *  @param FieldPos    solver's interception point (sim m); only meaningful
	 *                     when bKeeperTakes is true
	 *  @param Height      FieldPos.Z (m)
	 *  @param bKeeperTakes solver gave this ball to the keeper
	 *  @param bDive       solver says only reachable at full stretch
	 *  @param bContact    ball came off the bat (edge path) vs beaten ball
	 *  @param bBounced    ball bounced before the keeper
	 *  @param bStandingUp keeper is standing up (spin)
	 *  @param TimeToArrive s from release/contact to FieldTime; drives urgency
	 */
	struct FKeeperSelection
	{
		EKeeperTake Family = EKeeperTake::CentralChest;
		EKeeperReach Reach = EKeeperReach::Reachable;
		EKeeperFootwork Footwork = EKeeperFootwork::Set;
		float Lateral = 0.f;      // m, + off side (batter-relative)
		float Height = 0.7f;      // m
		float Dist2D = 0.f;       // m from Home to the take
		float TimeToArrival = 0.f;
		int32 Steps = 0;          // shuffle/crossover steps
		const TCHAR* AnimName = TEXT("KeeperCentralTake");
		float MaxIKCorrection = 0.15f; // m: larger means the WRONG animation was picked
		bool bUnreachableMiss = false;
	};
	FKeeperSelection Classify(const FVector2D& Home, const FVector& FieldPos, bool bKeeperTakes,
		bool bDive, bool bContact, bool bBounced, bool bStandingUp, float TimeToArrival, float OffSign);

	/** Name of the take family, for logs and QA. */
	const TCHAR* TakeName(EKeeperTake Take);
	const TCHAR* ReachName(EKeeperReach Reach);
	const TCHAR* FootworkName(EKeeperFootwork Footwork);
	const TCHAR* EventName(EKeeperEvent Event);

	/** Cap the procedural glove target to anatomical chest-to-glove reach.
	 *  Returns the unmet distance for contact diagnostics. */
	float GloveTarget(const FKeeperSelection& Sel, const FVector& KeeperChest, const FVector& FieldPos, FVector& OutGloves);

	/** Stumping transfer: after possessing the ball at TakePos, the gloves move
	 *  to the wicket (StumpTop) and break it at BreakTime. Returns 0..1 progress
	 *  at Post (s after contact). TakeTime is FieldTime, BreakTime the sim's
	 *  BrokenTime (s after contact). Enforces possession first: no wicket break
	 *  before the take. */
	float StumpingProgress(float Post, float TakeTime, float BreakTime);

	/** One-line debug log of the selection: trajectory, interception, family,
	 *  animation, IK magnitude. Wire to the F1 overlay / capture log. */
	FString SelectionLog(const FKeeperSelection& Sel, const FVector& BallAtTake, float KeeperDepthM, EBowlerType BowlerType);
}
