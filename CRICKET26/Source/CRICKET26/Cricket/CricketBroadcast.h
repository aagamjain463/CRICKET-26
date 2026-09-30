// Broadcast camera director: shot selection, framing, smoothing, cuts and replay direction.
//
// A real broadcast truck, not a ball-following game camera. Everything here is pure logic over
// snapshots of gameplay state (positions, velocities, outcomes): it OBSERVES the match and decides
// what the viewer should see. It never touches gameplay, animation, physics or match state.
//
// Units: simulation metres are converted on entry; all geometry in here is Unreal world units (cm),
// matching the game mode's presentation (ToWorld = x100). Time is seconds.
//
// Frame-rate independence: every smoother uses exponential damping (1 - exp(-lambda * Dt)), so 30,
// 60 and 120 fps converge to the same path (tested in BroadcastTests).

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.h"
#include "DeliveryResolver.h"
#include "SuperOverMatch.h"
#include "CricketBroadcast.generated.h"

/** Phase of play as the broadcast director sees it. Maps from the game mode's delivery phase. */
UENUM(BlueprintType)
enum class EBroadcastPhase : uint8
{
	PreDelivery,
	RunUp,
	Delivery,     // ball live, before contact: the delivery corridor
	Contact,      // brief window around bat meeting ball: let the shot breathe
	BallInPlay,   // outgoing ball: covered by shot class
	Fielding,     // a fielder is the story
	Running,      // batters running / throw coming in
	BallDead,     // presentation close-ups
	Wicket,       // dismissal presentation
	Boundary,     // ball going to / over the rope
	Replay,
	ReturnToLive
};

/** Camera roles. A role is a job ("cover the lofted ball"), not a fixed transform. */
UENUM(BlueprintType)
enum class EBroadcastShot : uint8
{
	StandardDelivery,  // Cricket-24-style: high behind the bowler, long lens
	AlternateDelivery, // replay/beauty: side-on or batter-end delivery view
	StraightOn,        // behind the batter's end, down the pitch
	BatterEnd,         // low behind the keeper: edges, stumpings
	SideOn,            // square of the pitch: stroke mechanics, run-outs
	GroundFollow,      // pulled-wide follow of a ground shot
	OutfieldFollow,    // high chase view of an outfield shot
	AerialBall,        // lofted-ball coverage framed by trajectory prediction
	Boundary,          // beyond the rope where the ball crosses it
	Keeper,            // tight behind the stumps: edges, keeper takes
	Slip,              // cordon angle for the edge that carries
	RunOut,            // side-on to the broken stumps: runner, crease, ball
	WicketClose,       // bowler/striker close-up after a dismissal
	Catch,             // descending ball + catching fielder
	Presentation,      // crowd/ground beauty (scorecards etc. keep their own shots)
	ReplayBeauty,      // cinematic replay angle (side/batter side)
	ReplaySlowMo,      // tight super-slow-motion replay angle
	Review,            // ball-tracking view (owned by the review flow)
	Scorecard,         // wide ground shot (owned by the scorecard flow)
	// Replay angles measured off the Cricket 26 reference (Docs/BROADCAST_REFERENCE_GAME_MP4.md §4).
	ReplayBowlerTrack, // behind the bowler at waist height, then down the pitch behind the ball to the batter
	ReplayGroundLevel, // on the turf a few metres up the pitch, wide lens: the batter against the sky
	ReplayCrane,       // high behind the striker, looking over them at the stroke
	ReplayStandTilt,   // the six into the stands, tilting up past the roof
	ReplayLongLens,    // long lens from the far stand, following the ball to the rope
	ReplayStumpCam,    // side-on at the striker's stumps, knee height: the bails flying
	Count
};

/** What the outgoing ball is doing, from the resolver's actual result. Drives post-contact coverage. */
UENUM(BlueprintType)
enum class EShotClass : uint8
{
	NoContact,          // beaten, left, or pad: stay on the delivery story
	DeadDefence,        // soft hands, ball dies near the pitch: hold, then close-up
	GroundInfield,      // along the ground, infield stop likely
	GroundOutfield,     // along the ground through/over the infield
	LoftedInfield,      // in the air, catch chance in the ring
	LoftedOutfield,     // in the air toward the deep
	HighCatchChance,    // hanging ball, fielders converging
	BoundaryTrajectory, // will reach the rope (resolver says so)
	KeeperEdge,         // edge through to keeper/slips
	RunningPlay,        // live running + throw: ends decide
	WicketEvent         // stumps hit / catch taken / stumping: dismissal coverage
};

/** How a camera change is presented. Broadcast cuts between roles; it blends within one. */
UENUM(BlueprintType)
enum class ECameraTransition : uint8 { Cut, Blend };

/** Replay-worthy events (§24). Dismissals name their kind so angles can be specific. */
UENUM(BlueprintType)
enum class EReplayEventType : uint8
{
	None,
	Four,
	Six,
	Bowled,
	Caught,
	DivingCatch,
	LBW,
	RunOut,
	CloseRunOut,   // survived, but too close for the naked eye
	Stumped,
	HitWicket,
	EdgeNotOut,    // beat the bat / through the keeper, no wicket
	DroppedCatch,  // catch chance put down
	SpectacularStop,
	Milestone      // chase won, landmark reached: the moment matters
};

/** Not every event deserves the same package (§25). */
UENUM(BlueprintType)
enum class EReplayPriority : uint8 { None, Low, Medium, High, Hero };

/**
 * One key of the delivery camera's zoom-and-tilt curve. The defaults were measured frame by frame off
 * the Cricket 24 broadcast reference (two deliveries, consistent): the operator holds a fixed stand
 * position and rides one continuous, accelerating zoom from the run-up to the batter.
 */
USTRUCT(BlueprintType)
struct FDeliveryLensKey
{
	GENERATED_BODY()
	/** Delivery clock: 0 = run-up start, 1 = release, 2 = ball at the batter. */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float U = 0.f;
	/** Horizontal FOV on a 16:9 screen (degrees). Other aspects keep the same vertical view. */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float FOV = 26.f;
	/** Screen height of the striker's popping crease (0 top .. 1 bottom): sets the camera's tilt. */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float CreaseY = 0.5f;
};

/**
 * Standard delivery camera calibration (§5). Defaults reproduce the Cricket 24 broadcast framing,
 * measured off the reference footage: a fixed long-lens camera high in the stand behind the bowler
 * whose zoom (LensCurve) tightens from the run-up to the batter.
 *
 * Cricket-24 reference notes encoded here: near-central (bowler's-arm side reads best for right-arm
 * over; default 0 keeps both arms safe), high enough that the pitch reads as a corridor, long enough
 * that bowler and batter share the frame without the camera chasing the ball.
 */
USTRUCT(BlueprintType)
struct FDeliveryCameraTune
{
	GENERATED_BODY()

	/** Metres behind the bowler's stumps (pitch X = PitchLength + Distance). */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float Distance = 51.7f;
	/** Metres above the ground. */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float Height = 8.1f;
	/** Metres lateral from the pitch centre line (+ toward the off side of a right-hander). */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float LateralOffset = 0.f;
	/** Pitch station of the default look target (m from the striker's stumps). */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float LookAtX = 0.5f;
	/** Height of the default look target (m). */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float LookAtHeight = 1.3f;
	/** Horizontal field of view (degrees). Long lens: narrow. */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float FOV = 8.f;
	/** Extra pitch trim applied after the look-at solve (degrees, + looks further down). */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float PitchTrim = 0.f;
	/** Extra yaw trim (degrees, + toward the off side). */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float YawOffset = 0.f;
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float Roll = 0.f;
	/** 35 mm-equivalent focal length for reference (0 = derived from FOV). */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float FocalLengthMm = 0.f;
	/**
	 * Zoom and tilt through the delivery (51.7 m behind the stumps, 8.1 m up; 7.1 deg at release and 4.6 as
	 * the ball reaches the batter, as measured off Cricket 24). Before the ball and through the run-up the
	 * frame holds the whole pitch with the batter large at the top (11 deg), not the measured 25.9 deg wide
	 * of the outfield, which left the batter a speck; the bowler runs in from the bottom of the frame.
	 * Empty = the static FOV and LookAt above.
	 */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") TArray<FDeliveryLensKey> LensCurve = {
		{ 0.f, 11.f, 0.36f }, { 0.6f, 10.3f, 0.39f }, { 0.905f, 9.4f, 0.417f }, { 1.f, 7.1f, 0.424f }, { 1.39f, 6.3f, 0.444f }, { 1.71f, 5.3f, 0.493f },
		{ 2.f, 4.6f, 0.664f } };
	/** Share of the weighted target's sideways drift the operator pans with (0 = locked on the pitch line). */
	UPROPERTY(EditAnywhere, Category = "Delivery Camera") float LateralFollow = 0.15f;

	// Weighted framing target (§8): Target = Bowler*W1 + ReleaseArea*W2 + Ball*W3 + Batter*W4.
	// Blended smoothly by ball progress; abrupt changes are never applied raw.
	UPROPERTY(EditAnywhere, Category = "Delivery Tracking") float BowlerWeight = 0.34f;
	UPROPERTY(EditAnywhere, Category = "Delivery Tracking") float ReleaseWeight = 0.26f;
	UPROPERTY(EditAnywhere, Category = "Delivery Tracking") float BallWeight = 0.10f;
	UPROPERTY(EditAnywhere, Category = "Delivery Tracking") float BatterWeight = 0.30f;
	/** How far the ball-approach shifts weight from bowler toward ball+batter (0..1). */
	UPROPERTY(EditAnywhere, Category = "Delivery Tracking") float ReleaseBias = 0.55f;
	/** Overall tracking authority; 0 freezes the delivery composition, 1 is full operator. */
	UPROPERTY(EditAnywhere, Category = "Delivery Tracking") float TrackingStrength = 1.f;
	/** Position damping time constant (s): the operator's lag. */
	UPROPERTY(EditAnywhere, Category = "Delivery Tracking") float TrackingLag = 0.3f;
	/** Rotation damping lambda (1/s). */
	UPROPERTY(EditAnywhere, Category = "Delivery Tracking") float RotationDamping = 8.f;
	/** Follow-cam position lambda (1/s) once the ball is away. */
	UPROPERTY(EditAnywhere, Category = "Delivery Tracking") float PositionDamping = 2.f;
};

/** Central broadcast tuning (§43): one place for every camera number. FOVs are horizontal on a 16:9 screen. */
USTRUCT(BlueprintType)
struct FBroadcastTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Broadcast") FDeliveryCameraTune Delivery;
	/** Follow-cam lenses: pulled wide so the ball stays readable, never a fisheye. */
	UPROPERTY(EditAnywhere, Category = "Broadcast") float FollowFOV = 42.f;
	/** Tightest the ground follow gets while ball and striker are still close (Cricket 24: ~12). */
	UPROPERTY(EditAnywhere, Category = "Broadcast") float FollowMinFOV = 12.f;
	/** Stand-camera follows keep the horizon at least this high above frame centre (share of the half
	 *  height), so the field fills the frame and the crowd stays a strip along the top (Cricket 24). */
	UPROPERTY(EditAnywhere, Category = "Broadcast") float FollowHorizonY = 0.7f;
	/** Outfield follow (reverse angle from the far side of the ground): half-width of outfield (cm)
	 *  the lens holds round the ball, the camera's height (m) and its distance behind pitch centre (m). */
	UPROPERTY(EditAnywhere, Category = "Broadcast") float OutfieldHalfWidth = 2000.f;
	UPROPERTY(EditAnywhere, Category = "Broadcast") float OutfieldCamHeight = 14.f;
	UPROPERTY(EditAnywhere, Category = "Broadcast") float OutfieldCamBack = 70.f;
	/** Reverse angle's horizon height above frame centre (share of the half height): Cricket 24 keeps
	 *  boards and crowd across the top ~40% while the chase plays out below. */
	UPROPERTY(EditAnywhere, Category = "Broadcast") float OutfieldHorizonY = 0.2f;
	UPROPERTY(EditAnywhere, Category = "Broadcast") float BoundaryFOV = 50.f;
	UPROPERTY(EditAnywhere, Category = "Broadcast") float FieldingFOV = 30.f;
	UPROPERTY(EditAnywhere, Category = "Broadcast") float CloseUpFOV = 20.f;
	UPROPERTY(EditAnywhere, Category = "Broadcast") float ReplaySideFOV = 12.f;
	UPROPERTY(EditAnywhere, Category = "Broadcast") float ReplayWideFOV = 35.f;
	UPROPERTY(EditAnywhere, Category = "Broadcast") float SuperSlowFOV = 3.f;
	/** Minimum time a shot stays up before a non-forced change (s): no thrashing. */
	UPROPERTY(EditAnywhere, Category = "Broadcast") float MinShotDuration = 1.2f;
	/** The contact breath: how long the delivery shot holds after bat meets ball (s). */
	UPROPERTY(EditAnywhere, Category = "Broadcast") float ContactHoldTime = 0.3f;
	/** Angular speed cap (deg/s): the head can never whip. */
	UPROPERTY(EditAnywhere, Category = "Broadcast") float MaxAngularVelocity = 90.f;
	/**
	 * Look-target dead zone (cm): inside this, the operator's hands stay still. CRICKET26.mp4
	 * premium: 8 cm kills pixel shimmer on the long lens without stick-slip on the follows
	 * (15 cm held the head still a beat too long, then stepped).
	 */
	UPROPERTY(EditAnywhere, Category = "Broadcast") float LookDeadZoneCm = 8.f;
	/** Minimum camera height (m): never underground, never in the pitch. */
	UPROPERTY(EditAnywhere, Category = "Broadcast") float MinCameraHeightM = 0.6f;

	// Replay packaging (§23-30).
	/** Seconds of buffered history a replay may draw on before the decisive moment. */
	UPROPERTY(EditAnywhere, Category = "Replay") float ReplayLeadMax = 1.2f;
	/** Screen time per replay angle (s). */
	UPROPERTY(EditAnywhere, Category = "Replay") float ReplayAngleTime = 3.2f;
	/** Full replay: ball time shown before release (the delivery stride; the bowling clip starts 1 s out). */
	UPROPERTY(EditAnywhere, Category = "Replay") float ReplayFullLead = 0.9f;
	/** Full replay: longest ball span shown (s). */
	UPROPERTY(EditAnywhere, Category = "Replay") float ReplayFullMax = 7.f;
	/** Full replay: speed through the decisive moment (1 elsewhere). */
	UPROPERTY(EditAnywhere, Category = "Replay") float ReplayFullSlow = 0.6f;
	/** Slow factor at the decisive moment (0.25 = quarter speed). */
	UPROPERTY(EditAnywhere, Category = "Replay") float ReplaySlowFactor = 0.25f;
	/** Super-slow factor for the tight angle. */
	UPROPERTY(EditAnywhere, Category = "Replay") float SuperSlowFactor = 0.2f;
	/** Rolling buffer window (s): run-up + ball + presentation tail. */
	UPROPERTY(EditAnywhere, Category = "Replay") float BufferWindow = 14.f;
	/** Rolling buffer sample rate (Hz): fixed, bounded, mobile-safe. */
	UPROPERTY(EditAnywhere, Category = "Replay") float BufferRate = 30.f;
};

/** Minimal phase input: the director never sees the game mode, only these facts. */
struct FPhaseInput
{
	bool bRunUp = false;
	bool bBallLive = false;
	bool bDead = false;
	bool bReplaying = false;
	float BallT = 0.f;          // s since release (presentation clock)
	float ContactTime = 0.f;
	bool bHasContact = false;
	float AfterContact = -1.f;  // BallT - ContactTime, <0 before contact
	EDismissal Dismissal = EDismissal::None;
	int32 Boundary = 0;
	bool bCaught = false;
	bool bRunning = false;
	bool bWicketFallen = false;
};

/**
 * One frame of observed gameplay for the director, world units (cm). The game mode fills this from
 * its actors and the resolver result; the director only reads.
 */
struct FBroadcastFrame
{
	FVector BallPos = FVector::ZeroVector;
	FVector BallVel = FVector::ZeroVector;   // cm/s, world
	FVector PredictedPos = FVector::ZeroVector; // where the ball is going (look-ahead)
	bool bHasPrediction = false;
	FVector BowlerPos = FVector::ZeroVector;
	FVector StrikerPos = FVector::ZeroVector;
	FVector NonStrikerPos = FVector::ZeroVector;
	FVector KeeperPos = FVector::ZeroVector;
	bool bHasKeeper = false;
	FVector FielderPos = FVector::ZeroVector; // the relevant fielder, if any
	bool bHasFielder = false;
	FVector ContactPos = FVector::ZeroVector; // sim metres (pitch frame): where bat met ball
	FVector ExitVel = FVector::ZeroVector;    // sim m/s at contact
	FVector BoundaryCrossPos = FVector::ZeroVector; // world: where the ball reaches the rope
	bool bHasBoundaryCross = false;
	float OffSign = 1.f;  // +1: off side is +Y (right-hander)
	float ArmSign = 1.f;  // bowling arm side
	EShotClass ShotClass = EShotClass::NoContact;
	EDismissal Dismissal = EDismissal::None;
	int32 Boundary = 0;
	bool bCaught = false;
	bool bCatchChance = false;
	bool bWicketThis = false;
	float FieldTime = 0.f;   // s after contact of the take/stop
	/** A run-out is on (the resolver's race is lost or within a whisker): the crease camera earns its cut. */
	bool bRunOutChance = false;
	/** s after contact when the throw is released (the crease cut waits for it). */
	float ThrowTime = 0.f;
	/** The end the throw goes to, when runs are attempted (else the crease camera takes the striker's nearer end). */
	bool bHasThrowEnd = false;
	bool bThrowToStrikerEnd = true;
	float BoundaryTime = -1.f; // s after contact when the ball reaches the rope (-1: none)
	float AfterContact = -1.f;
	float BallT = 0.f;
	float DeadTime = 0.f;
};

/** What the solver wants this frame. */
struct FLiveCameraSolution
{
	FVector Location = FVector::ZeroVector; // world cm
	FVector LookAt = FVector::ZeroVector;   // world cm
	float FOV = 8.f;
	EBroadcastShot Shot = EBroadcastShot::StandardDelivery;
	ECameraTransition Transition = ECameraTransition::Cut;
};

struct FDeliveryWeights
{
	float Bowler = 0.34f, Release = 0.26f, Ball = 0.10f, Batter = 0.30f;
};

namespace CricketBroadcast
{
	// ---------- Phase & classification ----------

	/** Game facts -> broadcast phase. */
	EBroadcastPhase ResolvePhase(const FPhaseInput& In, const FBroadcastTuning& Tune);

	/**
	 * Post-contact coverage class from the resolver's ACTUAL result (§10). Uses exit velocity
	 * direction/height, the fielding outcome (boundary? catch? keeper take?) and the dismissal.
	 * Pass the umpire-decided dismissal (EDismissal::None while live).
	 */
	EShotClass ClassifyShot(const FDeliveryResult& Result, EDismissal Dismissal);

	/** Delivery-ball progress 0 (release) .. 1 (contact plane) for weight blending. */
	float DeliveryProgress(float BallT, float ReleaseTime, float ContactTime);

	/** Smooth phase weights for the delivery framing target (§8). Never steps. */
	FDeliveryWeights DeliveryWeights(float Progress01, const FDeliveryCameraTune& Tune);

	// ---------- Geometry ----------

	/** Standard delivery camera location in world cm (pitch frame + tune). */
	FVector DeliveryLocation(const FDeliveryCameraTune& Tune, float ArmSign);
	/** Standard delivery default look target in world cm. */
	FVector DeliveryLookAt(const FDeliveryCameraTune& Tune);
	/** Delivery clock (see FDeliveryLensKey): run-up progress before release (BallT < 0), then 1 + flight progress. */
	float DeliveryClock(float RunUpProgress01, float BallT, float ContactTime);
	/**
	 * The camera's own delivery clock. It follows the game's clock with that clock's speed fed forward, so a steady
	 * run-up or flight is tracked with no lag, and catches up on a critically damped spring when the game's clock
	 * jumps (a human bowler letting go before or after the assumed ideal), so the zoom never lurches. A clock that
	 * runs backwards is a new pass (next ball, replay) and snaps.
	 */
	struct FDeliveryClockFollow
	{
		float U = 0.f, Rate = 0.f, Prev = -1.f;
		/** Spring stiffness (1/s): a jump is mostly absorbed in about 4 / Stiffness seconds. */
		float Stiffness = 7.f;
		/** Fastest game-clock speed fed forward (clock units/s): a one-frame jump is left to the spring. */
		float MaxFeed = 3.f;
		void Reset(float At) { U = Prev = At; Rate = 0.f; }
		float Update(float Target, float Dt);
	};
	/** Horizontal FOV at Aspect (width / height) with the vertical view Fov16x9 has on a 16:9 screen. */
	float AspectFOV(float Fov16x9, float Aspect);
	/** The lens curve at clock U (monotone cubic zoom in log space through the keys). False when the curve is empty. */
	bool SampleLensCurve(const FDeliveryCameraTune& Tune, float U, float& OutFov16x9, float& OutCreaseY);
	/**
	 * The standard delivery shot at clock U: fixed stand position, the curve's lens and tilt (the
	 * striker's crease held at the curve's screen height), panned a LateralFollow share of the way
	 * toward PanTarget. Aspect-corrected FOV.
	 */
	FLiveCameraSolution SolveDeliveryShot(const FDeliveryCameraTune& Tune, float U, float Aspect, float ArmSign, const FVector& PanTarget);

	/**
	 * Weighted framing target during the delivery (§8): bowler / release corridor / ball / batter
	 * blended by smooth phase weights, then eased toward the delivery corridor at release. All world cm.
	 */
	FVector WeightedDeliveryTarget(const FBroadcastFrame& Frame, const FDeliveryCameraTune& Tune, float Progress01);

	/** Full per-shot geometry: location, look target and lens for a role (§20, §39). */
	FLiveCameraSolution SolveShotGeometry(EBroadcastShot Shot, const FBroadcastFrame& Frame, const FBroadcastTuning& Tune);

	/** Keeps cameras out of the ground/geometry without jitter: clamps height, pulls inside the bowl. */
	FVector ApplyCameraSafeguards(const FVector& WantLoc, const FBroadcastTuning& Tune);

	/**
	 * Occlusion pull-in, called ONLY on cuts (§19): a single trace from the look target toward the
	 * wanted location; if blocked, the camera sits just inside the blockage. Never per-frame.
	 */
	FVector ApplyOcclusion(UWorld* World, const FVector& LookAt, const FVector& WantLoc);

	// ---------- Director: selection, cuts, blends (§17-18) ----------

	/** Pure shot decision for live play (before min-duration / anti-thrash gating). */
	EBroadcastShot SelectLiveShot(EBroadcastPhase Phase, const FBroadcastFrame& Frame);

	/** Broadcast grammar: role changes cut; one role repositioning blends. Delivery pulling out blends. */
	ECameraTransition TransitionFor(EBroadcastShot From, EBroadcastShot To);

	/** Minimum screen time for a role (s). */
	float MinShotDuration(EBroadcastShot Shot, const FBroadcastTuning& Tune);

	/** Director runtime: gates the pure decision with shot durations and forced events. */
	struct FDirectorState
	{
		EBroadcastShot ActiveShot = EBroadcastShot::StandardDelivery;
		float ShotTime = 99.f;
		bool bSnapped = false;
		int32 ReplayAngle = INDEX_NONE; // the replay angle on screen, INDEX_NONE when live
		void Reset(EBroadcastShot Shot = EBroadcastShot::StandardDelivery);
		/**
		 * Returns the shot to show and whether this frame is a cut. Angle is the replay angle being shown (INDEX_NONE
		 * live): a new replay angle is always a cut, even into a shot kind that would otherwise blend from the last.
		 */
		EBroadcastShot Update(float Dt, EBroadcastShot Desired, bool bForce, const FBroadcastTuning& Tune, bool& bOutCut, int32 Angle = INDEX_NONE);
	};

	// ---------- Smoothing (§9, §41) ----------

	/** Frame-rate-independent exponential factor: 1 - exp(-Lambda * Dt). */
	inline float DampFactor(float Lambda, float Dt) { return 1.f - FMath::Exp(-Lambda * FMath::Max(Dt, 0.f)); }

	struct FSmoother
	{
		FVector Location = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
		float FOV = 8.f;
		bool bInit = false;
		void Snap(const FVector& Loc, const FQuat& Rot, float InFOV);
		/**
		 * Critically-damped-feel follow: exponential position, slew-limited rotation with a dead
		 * zone, exponential FOV (FovLambda < 0 = PosLambda). Dt is real frame time: identical path at
		 * any frame rate. The dead zone is DeadZoneCm at a 40 deg lens, narrowing with the FOV so it is
		 * the same few pixels on a long lens as on a wide one.
		 */
		void Update(const FVector& WantLoc, const FVector& LookAt, float WantFOV, float Dt,
			float PosLambda, float RotLambda, float MaxDegPerSec, float DeadZoneCm, float FovLambda = -1.f);
	};

	// ---------- Replay direction (§24-30) ----------

	struct FReplayTrigger
	{
		EReplayEventType Event = EReplayEventType::None;
		EReplayPriority Priority = EReplayPriority::None;
		/** Ball time of the decisive moment (contact / stumps / take). */
		float DecisiveT = 0.f;
	};

	/**
	 * Trigger + priority from the actual result and the umpire outcome. bMilestone marks moments
	 * like winning the chase. Dot balls never trigger: priority stays None.
	 */
	FReplayTrigger ClassifyReplayEvent(const FDeliveryResult& Result, const FDeliveryOutcome& Outcome, bool bMilestone);

	/** One replay angle: role, ball-time window, slow factor and screen time. */
	struct FReplayAnglePlay
	{
		EBroadcastShot Shot = EBroadcastShot::ReplayBeauty;
		float StartTp = 0.f;    // ball time shown at angle start
		float EndTp = 1.f;      // ball time shown at angle end
		float DecisiveTp = 0.f;
		float SlowFactor = 0.25f;
		float WallTime = 3.2f;  // screen seconds
		/** The whole ball, from the delivery stride to the end of the event, directed like live coverage. */
		bool bFullPass = false;
	};

	struct FReplayPackage
	{
		EReplayEventType Event = EReplayEventType::None;
		EReplayPriority Priority = EReplayPriority::None;
		TArray<FReplayAnglePlay> Angles;
		bool IsValid() const { return Priority != EReplayPriority::None && Angles.Num() > 0; }
		float TotalWallTime() const;
	};

	/**
	 * Event-specific multi-angle packages (§27): wickets get broadcast + release/contact + dismissal
	 * angles; sixes get contact + cinematic + flight; boundaries get contact + rope; close run-outs
	 * get diagnostic crease angles. Count follows priority (§25); RecentShots (mutable history) is
	 * consulted so packages vary without ever picking a bad angle (§28).
	 */
	FReplayPackage BuildReplayPackage(const FReplayTrigger& Trigger, const FDeliveryResult& Result,
		const FBroadcastFrame& Frame, const FBroadcastTuning& Tune, TArray<EBroadcastShot>& RecentShots);

	/** Smooth replay speed curve (§30): near-normal approach, slow through the moment, recover after. */
	float ReplaySpeedAt(float BallT, float DecisiveT, float SlowFactor, float Width = 0.45f);

	/** Wall-clock -> ball-time remap for an angle, baked at package time (monotonic, exact endpoints). */
	struct FTimeRemap
	{
		TArray<float> BallAt; // Num+1 entries over WallTime
		float WallTime = 1.f;
		float Sample(float WallInto) const;
		float BallSpan() const;
	};
	FTimeRemap BuildTimeRemap(const FReplayAnglePlay& Angle, int32 Samples = 64);

	/** Screen time that plays an angle's ball span at the true speed curve (no stretch, no squeeze). */
	float NaturalWallTime(const FReplayAnglePlay& Angle, int32 Samples = 64);

	/** Recent-shot anti-repetition: prefer unused roles, weight toward the best ones first. */
	EBroadcastShot PickReplayShot(const TArray<EBroadcastShot>& Candidates, TArray<EBroadcastShot>& Recent);

	// ---------- Debug (§45) ----------

	const TCHAR* ShotName(EBroadcastShot Shot);
	const TCHAR* PhaseName(EBroadcastPhase Phase);
	const TCHAR* ReplayEventName(EReplayEventType Event);
	FString SummarizeShot(EBroadcastShot Shot, const FLiveCameraSolution& Sol, float ShotTime);
}
