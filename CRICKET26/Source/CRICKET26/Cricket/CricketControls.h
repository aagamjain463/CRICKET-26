// Player controls. The touch UI fills an FCricketControls for the frame (on desktop the mouse is the finger,
// so every platform plays the same way) and the game mode drives batting, bowling and running from it. Everything here only produces data - a
// shot (intent, direction, power, press time), a delivery plan (type, target, effort, movement, release
// timing) and run calls - which the simulation turns into outcomes; nothing here scores, moves a player or
// picks an animation.
//
// Batting is the pull-and-release gesture of the previous game: a finger lands anywhere in the batting zone,
// the pull from there sets the direction (the way the ball should go on screen) and how hard (the pull's
// length through a power curve), and letting go swings - the release time is the shot's timing. GROUND,
// LOFT and DEFEND are modes; a tap or a pull too short to commit is a defensive push; not playing is a leave.
//
// The touch layout is pure (screen-height units, origin top left) so the HUD draws what the input reads, and
// tests can press it.

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.h"
#include "BallSimulation.h"
#include "FieldingModel.h"
#include "CricketControls.generated.h"

struct FDeliveryResult;
struct FBatInput;

/** Every feel value of the controls in one place. Defaults are the previous game's tuning. */
USTRUCT(BlueprintType)
struct FCricketControlTuning
{
	GENERATED_BODY()

	// Batting pull, in screen heights. PullMax is also the on-screen full-power ring radius,
	// so a bigger value reads as a bigger, calmer target; the dead zone stays small so the pull starts at once.
	UPROPERTY(EditAnywhere, Category = "Batting") float PullDeadZone = 0.012f;
	UPROPERTY(EditAnywhere, Category = "Batting") float PullMax = 0.27f;
	UPROPERTY(EditAnywhere, Category = "Batting") float PullSensitivity = 1.f;
	UPROPERTY(EditAnywhere, Category = "Batting") float DirectionSensitivity = 1.f;
	UPROPERTY(EditAnywhere, Category = "Batting") float MaxAimDeg = 135.f;
	/** Pull (0..1 after the dead zone) below which a release is a defensive push. */
	UPROPERTY(EditAnywhere, Category = "Batting") float MinCommit = 0.045f;
	/** FBatInput::Power at the shortest and longest committed pull (0.8 is the batter's natural stroke). */
	UPROPERTY(EditAnywhere, Category = "Batting") float MinPower = 0.35f;
	UPROPERTY(EditAnywhere, Category = "Batting") float MaxPower = 1.f;
	/** Timing grades (s from ideal): perfect, good, and early/late; beyond that very early/late. Phone windows
	 * are ~15% wider than desktop so taps feel fair on touch (M3 touch 10/10). */
	UPROPERTY(EditAnywhere, Category = "Batting") float PerfectTiming = 0.017f;
	UPROPERTY(EditAnywhere, Category = "Batting") float GoodTiming = 0.046f;
	UPROPERTY(EditAnywhere, Category = "Batting") float EarlyLateTiming = 0.092f;

	// Crease movement (guard): the striker shuffles laterally before the ball is bowled.
	/** Metres per tap of the crease arrows (batter-relative, + off side). */
	UPROPERTY(EditAnywhere, Category = "Batting") float GuardStep = 0.15f;
	/** How far the striker can move either way from middle guard (m, batter-relative). */
	UPROPERTY(EditAnywhere, Category = "Batting") float GuardMax = 0.9f;

	// Bowling.
	/** Metres of line and length per screen height of drag. */
	UPROPERTY(EditAnywhere, Category = "Bowling") float LineDragScale = 6.f;
	UPROPERTY(EditAnywhere, Category = "Bowling") float LengthDragScale = 28.f;
	/** Swing dial magnitude below which a swing ball is a stock ball. */
	UPROPERTY(EditAnywhere, Category = "Bowling") float DialStockBand = 0.1f;
	/** Release meter grades (|meter|, 0 is perfect); past 0.85 late is a no-ball (the simulation's rule). */
	UPROPERTY(EditAnywhere, Category = "Bowling") float PerfectRelease = 0.06f;
	UPROPERTY(EditAnywhere, Category = "Bowling") float GoodRelease = 0.2f;
	UPROPERTY(EditAnywhere, Category = "Bowling") float EarlyLateRelease = 0.45f;

	// Running.
	/** Seconds before the bat meets the ball that a run call is already taken. */
	UPROPERTY(EditAnywhere, Category = "Running") float RunCallGrace = 0.3f;
	UPROPERTY(EditAnywhere, Category = "Running") int32 MaxRuns = 6;

	// Pitch marker: where the ball will land, shown to a human batter from the start of the run-up. Per difficulty
	// (Easy, Medium, Hard, Legend): how long into the run-up it appears (s) and its radius (m); 0 hides it.
	UPROPERTY(EditAnywhere, Category = "Pitch Marker") float PitchMarkerDelay[4] = { 0.f, 0.4f, 0.9f, 0.f };
	UPROPERTY(EditAnywhere, Category = "Pitch Marker") float PitchMarkerRadius[4] = { 0.3f, 0.22f, 0.15f, 0.f };
	/** Fade in after the delay, and out once the ball has pitched (s). */
	UPROPERTY(EditAnywhere, Category = "Pitch Marker") float PitchMarkerFade = 0.12f;

	/** Scales every vibration; 0 turns haptics off. */
	UPROPERTY(EditAnywhere, Category = "Haptics") float HapticScale = 1.f;
};

struct FCricketControls
{
	// Pressed this frame.
	bool bGround = false, bLoft = false, bDefend = false; // batting modes
	bool bRun = false, bCancel = false;
	bool bAction = false;   // bowling: start the run-up, then release; batting: PLAY calls for the ball
	bool bProgress = false; // next innings / next match, skip a replay, accept the umpire's call
	bool bReview = false;   // challenge the umpire's call
	int32 DeliveryPick = -1;
	/** IPL selection (next batter / next bowler): the candidate index tapped this frame, else -1. */
	int32 PickIndex = -1;
	// Field editor (the human bowling side's captain): open it, finish it, or load a preset field.
	bool bFieldOpen = false, bFieldApply = false, bFieldCancel = false, bFieldReset = false;
	int32 FieldPreset = -1;
	// A finger on the field map: grabbed this frame, held, or let go this frame, at FieldAt (screen-height units).
	bool bFieldGrab = false, bFieldHeld = false, bFieldDrop = false;
	FVector2D FieldAt = FVector2D::ZeroVector;
	// The pull gesture: held with its pull from where the finger landed, or let go this frame.
	bool bPullHeld = false, bPullReleased = false;
	FVector2D PullOrigin = FVector2D::ZeroVector, Pull = FVector2D::ZeroVector;
	// Crease movement: tapped this frame (screen-left / screen-right arrows, or A/D keys on desktop).
	bool bGuardLeft = false, bGuardRight = false;
	// Bowling: target drag this frame, and the sliders where a finger holds them (-2: not held).
	FVector2D TargetDrag = FVector2D::ZeroVector;
	float Effort = -2.f, Dial = -2.f;
};

/** A shot as aimed: what the pull means before the release. */
struct FShotAim
{
	float Magnitude = 0.f;   // 0..1 pull after the dead zone
	float DirectionDeg = 0.f; // batter-relative, 0 straight, + off side
	float Power = 0.f;       // FBatInput::Power
	bool bCommitted = false; // long enough to be a stroke rather than a push
};

enum class ETimingGrade : uint8 { None, VeryEarly, Early, Good, Perfect, Late, VeryLate, Miss };
enum class EReleaseGrade : uint8 { VeryEarly, Early, Good, Perfect, Late, VeryLate, NoBall };

/** Where the batters are in a run, for the HUD and the debug overlay; derived from the calls and the outcome. */
enum class ERunState : uint8 { AtCrease, RunRequested, Running, Crossing, ApproachingCrease, RunComplete, NextRunRequested, Turning, Returning, BallDead };

enum class ECancelResult : uint8 { Nothing, Dropped, TurnBack };

namespace CricketControl
{
	/** The pull's length 0..1 past the dead zone. */
	float PullMagnitude(const FVector2D& Pull, const FCricketControlTuning& T);
	/** The power curve: fine control over short pulls, the top end saved for the full stretch. */
	float ShapeMagnitude(float Magnitude);
	/** Pull on screen to batter-relative direction: the ball goes the way the finger pulls (down the screen is straight). */
	float AimFromPull(const FVector2D& Pull, ECricketHand BatHand, const FCricketControlTuning& T);
	FShotAim Aim(const FVector2D& Pull, ECricketHand BatHand, const FCricketControlTuning& T);
	/** The stroke a release plays: a committed pull in Mode, else a defensive push. */
	FBatInput ShotFor(const FShotAim& Aim, EBatIntent Mode, float PressTime);
	const TCHAR* ZoneName(float DirectionDeg);
	const TCHAR* PowerBandName(float Magnitude);

	ETimingGrade GradeShot(const FDeliveryResult& Result, const FBatInput& Input, const FCricketControlTuning& T);
	const TCHAR* TimingGradeName(ETimingGrade Grade);
	EReleaseGrade GradeRelease(float Meter, const FCricketControlTuning& T);
	const TCHAR* ReleaseGradeName(EReleaseGrade Grade);

	/** How visible the pitch marker is (0..1) Since seconds into the run-up for a ball pitching PitchAt seconds into it (<0: a full toss, never shown). */
	float PitchMarkerAlpha(const FCricketControlTuning& T, int32 Difficulty, float Since, float PitchAt);

	/** Moves the target by a screen drag (screen-height units): across is line, down the screen is fuller. */
	void DragTarget(FDeliveryPlan& Plan, const FVector2D& Drag, ECricketHand BatHand, const FCricketControlTuning& T);
	void ClampTarget(FDeliveryPlan& Plan);
	bool IsSwingFamily(EDeliveryType Type);
	/**
	 * The movement dial, -1..1. For swing balls it is signed on screen (+: the ball moves to the right of the
	 * screen) and picks outswing or inswing for the batter's hand; for everything else it is the amount, -1 none.
	 */
	float DialFor(const FDeliveryPlan& Plan, ECricketHand BatHand);
	void ApplyDial(FDeliveryPlan& Plan, float Dial, ECricketHand BatHand, const FCricketControlTuning& T);

	/** RUN at Post (s after contact): the first call or another run queued. False when refused. */
	bool CallRun(FRunCalls& Calls, const FRunningOutcome& Run, float Post, const FCricketControlTuning& T);
	/** CANCEL: drop the last call not yet set off on, else ask to turn back (the simulation may refuse: too far). */
	ECancelResult CancelRun(FRunCalls& Calls, const FRunningOutcome& Run, float Post);
	ERunState RunState(const FRunningOutcome& Run, const FRunCalls& Calls, float Post);
	const TCHAR* RunStateName(ERunState State);

	/**
	 * Crease movement (guard): batter-relative lateral shift of the stance, + off side (m).
	 * Screen-left is world +Y, so a screen step converts with the batter's off-side sign.
	 */
	float ClampGuard(float Guard, const FCricketControlTuning& T);
	float MoveGuard(float Guard, bool bLeft, bool bRight, ECricketHand BatHand, const FCricketControlTuning& T);
	const TCHAR* GuardName(float Guard);
}

namespace CricketTouch
{
	/**
	 * What the player can do right now; only that mode's controls are shown and read. Batting: before the shot (shot
	 * modes and the pull). Running: once the shot is played or the ball is past the bat (RUN, CANCEL). Bowling: the
	 * plan before the run-up. Release: the run-up (the release button alone). Ready: a human batter before the ball
	 * (the shot modes, and PLAY to call the bowler in). Ready is last so the saved coaching counts keep their slots.
	 * Pick is the IPL selector (next batter / next bowler): a tappable candidate list, appended after Ready so the
	 * coaching slots never move.
	 */
	enum class EMode : uint8 { None, Batting, Running, Bowling, Release, Progress, Review, FieldEdit, Ready, Pick };

	enum class EButton : uint8 { Defend, Ground, Loft, Run, Cancel, Bowl, Delivery, Effort, Dial, Review, Accept,
		Field, FieldApply, FieldCancel, FieldReset, FieldPreset, Play, GuardLeft, GuardRight, Pick };

	/** The field presets the editor offers, in EFieldPreset order. */
	constexpr int32 NumFieldPresets = int32(EFieldPreset::Count);

	/** The screen's safe area: what a notch, rounded corners or a home bar take from each edge (screen-height units). */
	struct FInsets
	{
		float L = 0.f, T = 0.f, R = 0.f, B = 0.f;
	};

	struct FButton
	{
		EButton Button;
		int32 Index = 0;  // which delivery, for Delivery buttons
		FBox2D Rect;      // screen-height units: y 0..1, x 0..Aspect
	};

	struct FFinger
	{
		int32 Id = 0;
		FVector2D Pos = FVector2D::ZeroVector;
		bool bNew = false; // touched down this frame
	};

	/** What a gesture carries between frames: the finger that owns it, where it landed and where it was last. */
	struct FGesture
	{
		int32 Finger = INDEX_NONE;
		FVector2D Origin = FVector2D::ZeroVector, Last = FVector2D::ZeroVector;
		/** The bowling slider the finger holds (Effort or Dial); Bowl when the gesture is not a slider. */
		EButton Slider = EButton::Bowl;
	};

	TArray<FButton> Layout(EMode Mode, int32 NumDeliveries, float Aspect, const FInsets& Safe = FInsets());
	/** Where a finger starts the pull (batting), drags the target (bowling) or moves a fielder (field editor); buttons inside it take priority. */
	FBox2D GestureZone(EMode Mode, float Aspect, const FInsets& Safe = FInsets());
	/** HUD furniture the buttons keep clear of: the broadcast score strip along the bottom and the field radar top right. */
	FBox2D ScoreStrip(float Aspect, const FInsets& Safe = FInsets());
	FBox2D Radar(float Aspect, const FInsets& Safe = FInsets());
	/** The field editor's map (a square): the ground drawn as the radar draws it, scaled up. */
	FBox2D FieldMap(float Aspect, const FInsets& Safe = FInsets());
	/** Field map point (screen-height units) to the simulation frame, and back: the striker at the top, the off side of a right-hander on the left. */
	FVector2D MapToField(const FBox2D& Map, const FVector2D& P);
	FVector2D FieldToMap(const FBox2D& Map, const FVector2D& Home);

	/**
	 * Controls from the fingers on the screen. Buttons act on touch-down so a tap is prompt; the
	 * sliders follow a finger held on them; one finger at a time owns the gesture.
	 */
	FCricketControls Read(EMode Mode, int32 NumDeliveries, float Aspect, TArrayView<const FFinger> Fingers, FGesture& Gesture, const FInsets& Safe = FInsets());
}
