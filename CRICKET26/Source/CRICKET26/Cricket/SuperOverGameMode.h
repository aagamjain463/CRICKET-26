// Playable Super Over vertical slice. Owns the authoritative match state and the per-delivery flow
// (wait -> run-up -> ball in play -> dead ball), reads input, and drives placeholder presentation.
// The cricket itself (rules, physics, contact, fielding, AI) lives in the pure modules; this class
// only feeds them input and replays their results.
//
// Launch: UnrealEditor CRICKET26.uproject "/Engine/Maps/Entry?game=/Script/CRICKET26.SuperOverGameMode" -game

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "IPLTypes.h"
#include "CricketAI.h"
#include "CricketAudio.h"
#include "CricketAudioDirector.h"
#include "CricketBroadcast.h"
#include "CricketCommentaryDirector.h"
#include "CricketControls.h"
#include "CricketKeeper.h"
#include "CricketPose.h"
#include "CricketReplayBuffer.h"
#include "SuperOverGameMode.generated.h"

class AStaticMeshActor;
class ACameraActor;
class UStaticMesh;
class UMaterialInterface; class UMaterialInstanceDynamic;
class USkeletalMesh;
class UAnimSequence;
class UAudioComponent;
class USoundWaveProcedural;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnCricketEvent, ECricketEvent);

enum class EDeliveryPhase : uint8 { Waiting, RunUp, BallInPlay, DeadBall };

UCLASS()
class ASuperOverGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASuperOverGameMode();
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(EditAnywhere, Category = "Super Over") TArray<FCricketTeam> Teams;
	/** The fictional placeholder sides: three batters and one bowler each, as a Super Over picks them. */
	static TArray<FCricketTeam> DefaultSquads();
	UPROPERTY(EditAnywhere, Category = "Super Over") int32 HumanTeam = 0;
	UPROPERTY(EditAnywhere, Category = "Super Over") int32 MatchSeed = 2026;
	/** The release meter's sweep; an ideal release comes halfway, 2.5 s after setting off (Cricket 24's run-up, measured off the reference). */
	UPROPERTY(EditAnywhere, Category = "Super Over") float RunUpSeconds = 5.f;
	static constexpr float IdealRelease = 0.f; // bowling meter value of a perfectly timed release
	/** Metres of approach before the crease: a quick's 15 m at about 6 m/s, a spinner's 7 m jog. */
	float RunUpLength() const { return BowlerPlayer().BowlerType == EBowlerType::Pace ? 15.f : 7.f; }
	/** Pauses in the flow (s): before an AI-started ball, and after a dead ball with no replay. */
	static constexpr float BetweenBalls = 2.f, AfterDeadBall = 2.5f;
	UPROPERTY(EditAnywhere, Category = "Super Over") float ExposureEV100 = 15.f; // fixed camera exposure: the sunny-16 rule for a 100000 lux sun
	static constexpr float SunLux = 100000.f;
	static constexpr float NightLux = 2000.f; // on the field under the floodlights
	/** The sun's light travels from the bowler's end toward the striker, 42 degrees above the horizon. */
	static inline const FRotator SunRotation = FRotator(-42.f, 215.f, 0.f);
	static class ADirectionalLight* SpawnSun(UWorld* World);
	/** The sky's fill light, captured once (RecaptureSky) after the sky is up. */
	static class ASkyLight* SpawnSky(UWorld* World, bool bNight);
	/** Hair: grooms render as cards on every tier (hair, brows, lashes, beards); only strands are switched off. */
	static void ApplyHairQuality();
	/** The stadium's screen-space AO: a contact-scale radius, so shaded faces keep their sky fill. */
	static void SetAmbientOcclusion(struct FPostProcessSettings& Settings);
	/** Tints one material of the parametric outfit (outfit_ue.py) in a colour: the shirt in it, jeans darker, shoes white;
	 *  with Piping, the jeans become cricket trousers with a stripe of it down each leg. */
	static void TintOutfit(class UMaterialInstanceDynamic* Cloth, const FLinearColor& Colour, TOptional<FLinearColor> Piping = {});
	enum class EFigureRole : uint8 { Batter, Keeper, Umpire, Other };
	/** What is wrong with a spawned MetaHuman for its role: missing body, face or animation, a null material
	 *  slot, gear another role wears, or a groom switched off. Empty when it is fit to play. */
	static TArray<FString> FigureProblems(const AActor* Player, EFigureRole Role);

	/** Semantic match events for HUD, camera, audio and (later) commentary. */
	FOnCricketEvent OnCricketEvent;

	// Read by the HUD.
	FSuperOverMatch Match;
	int32 SelectedMatchOvers = 1;
	EDeliveryPhase DPhase = EDeliveryPhase::Waiting;
	float PhaseTime = 0.f;         // seconds in the current delivery phase (ball time once released)
	FDeliveryPlan HumanPlan;
	float Meter = -1.f;            // human release meter, -1..1, perfect at 0
	FDeliveryRelease Release;
	FDeliveryResult Result;
	/** For a human batter: where the AI's ball will pitch and when (s into the run-up), known as the run-up starts. */
	FVector PitchPreview = FVector::ZeroVector;
	float PitchPreviewAt = -1.f;
	/** Seconds into the run-up at which the ball is (or will be) let go. */
	float ReleaseAt() const { return 0.5f * (ReleaseTiming + 1.f) * RunUpSeconds; }
	FBatInput BatInput;
	FResolveContext Ctx;
	// Control layer state (player input -> intent data; the simulation owns outcomes).
	// Kept here so the HUD/debug overlay can read exactly what the player asked for.
	UPROPERTY(EditAnywhere, Category = "Controls") FCricketControlTuning ControlTuning;
	EBatIntent BatMode = EBatIntent::Ground; // GROUND / LOFT / DEFEND mode around the pull-and-release
	FShotAim LiveAim;              // the current pull's aim (touch held, or keyboard charging), for feedback
	bool bBatPullActive = false;   // a pull is being held/charged right now
	/** Crease guard: lateral stance shift in metres, batter-relative (+ off side). Persists across balls. */
	float StrikerGuard = 0.f;
	/** Stance centre for the current striker and guard (simulation metres, on the ground). */
	FVector StrikerHome() const;
	ETimingGrade LastTimingGrade = ETimingGrade::None;
	EReleaseGrade LastReleaseGrade = EReleaseGrade::Good;
	ERunState LastRunState = ERunState::AtCrease;
	FRunCalls HumanRunCalls;       // manual running calls; pressing RUN never scores by itself
	FString BowlerIntent;          // AI plan label (debug only: a human batter should not see it)
	FString LastSummary;           // the simulation's technical summary (debug overlay and log)
	FString Commentary;            // caption for the last delivery
	/** Every ball of the match, for the scorecard's pitch map and wagon wheel. Positions are simulation metres. */
	struct FBallMark
	{
		int32 SuperOver = 0, Innings = 0;
		FVector2D Pitch = FVector2D::ZeroVector; // where it pitched, if it did
		bool bPitched = false;
		FVector2D End = FVector2D::ZeroVector;   // where the stroke went (fielded, or over the rope), if the bat hit it
		bool bHit = false;
		int32 Runs = 0;
		bool bWicket = false;
	};
	TArray<FBallMark> Marks;
	float HumanRunMargin = 0.35f;
	float ShotDirection = 0.f;
	bool bDebug = false, bTrajectory = false, bAutoPlay = false, bForceWicket = false;
	bool bTimingFeedback = true;   // F9: the timing bar after each stroke (off for players who want to judge it themselves)
	/** First-use coaching: each control mode's hint shows until the player has used that mode CoachRepeats times (kept in the user settings). */
	bool bCoaching = true;
	static constexpr int32 CoachRepeats = 3;
	static constexpr const TCHAR* CoachSection = TEXT("CRICKET26.Coaching");
	int32 CoachUses[10] = {}; // by CricketTouch::EMode (Pick appended after Ready, slots kept)
	bool ShowCoach(CricketTouch::EMode Mode) const { return bCoaching && CoachUses[uint8(Mode)] < CoachRepeats; }
	void CoachUsed(CricketTouch::EMode Mode);
	CricketAI::EDifficulty Difficulty = CricketAI::EDifficulty::Hard; // F6 or -CricketDifficulty=0..3
	float AiSkill() const { return CricketAI::SkillOf(Difficulty); }
	/**
	 * Graphics tier 0 (Low) to 3 (Epic), F7 or -CricketQuality=0..3: the engine scalability level, which turns
	 * Lumen off below High, and (fixed at start-up) how full the stands are. Defaults to Medium on phones and
	 * High elsewhere.
	 */
	int32 Quality = PLATFORM_IOS || PLATFORM_ANDROID ? 1 : 2;
	void ApplyQuality();
	int32 BallsPlayed = 0, ShotBall = 0; // -CricketShotBall=N: save the game view while delivery N is live
	int32 QuitAfter = 0;
	float ShotEvery = 0.2f;  // -CricketShotEvery: capture interval (s)
	float SlowMo = 1.f;      // -CricketSlowMo: game speed while the ball is live, to inspect strokes in slow motion
	TArray<float> DevCam;    // -CricketDevCam
	bool bDevCamFielder = false;                 // -CricketQuitAfter=N: quit after N deliveries (default: after the shot ball)
	FString DevLookRole, DevLookBone; float DevLookDist = 1.f, DevLookYaw = 0.f, DevLookFov = 25.f; // -CricketDevLook
	int32 DevLookIndex = 0;
	AStaticMeshActor* DevLookFigure() const;
	float ShotClock = 0.f;
	float UIShotEvery = 0.f, UIShotClock = 0.f; // -CricketUIShotEvery=S: the whole screen, HUD included, every S s (UI QA)
	float ContactImpulseTime = 0.f, ContactImpulsePower = 0.f; // Subtle camera impact punch on middled shots
	/**
	 * Action replay of the key moment: an event-specific multi-angle package (CricketBroadcast) with
	 * smooth slow-motion curves per angle. The legacy two-angle timings stay as the fallback when no
	 * package was built (e.g. highlights clips from older saves).
	 */
	/** When the replay starts after the ball goes dead. */
	float ReplayDelay = 2.f;
	static constexpr float ReplayDelayMin = 2.f, ReplaySpeed = 0.5f, ReplayLead = 0.8f, ReplayAction = 1.6f;
	static constexpr float SuperSlowSpeed = 0.2f, SuperSlowLead = 0.3f;
	static constexpr float ReplayAngleTime = ReplayAction / ReplaySpeed, ReplayTime = 2.f * ReplayAngleTime;
	bool bReplayThis = false, bWicketThis = false;
	/** Central broadcast tuning (camera roles, lenses, replay packaging): every number in one place. */
	UPROPERTY(EditAnywhere, Category = "Broadcast") FBroadcastTuning BroadcastTuning;
	bool IsReplaying() const;
	int32 ReplayAngle() const;
	/** The ball time the replay is showing (smooth slow-motion remap inside each angle). */
	float ReplayBallTime() const;
	/** Screen duration of replay angle I (package, else the legacy angle time). */
	float ReplayAngleDuration(int32 Angle) const;
	/** Total replay screen time (package, else the legacy replay time). */
	float ReplayTotalTime() const;
	/** Ball-time span angle I shows (drives the HUD edge-signal overlay). */
	float ReplayAngleBallSpan(int32 Angle) const;
	/** Ball time angle I starts at (package, else the legacy lead). */
	float ReplayAngleStartTp(int32 Angle) const;
	/** True while the current replay angle is the tight slow-motion role. */
	bool IsReplaySlowAngle() const;
	/** The event the active replay package shows (the entry stinger names it). */
	EReplayEventType ReplayEvent() const { return ActivePackage.Event; }
	/** Broadcast camera debug line for the F1 overlay (role, state, lens, replay info). */
	FString CameraDebugString() const;
	/**
	 * Highlights: every wicket and boundary of the innings, replayed side-on one after another once its last ball
	 * is over, before the scorecard. Each clip swaps its ball in for the live one, which comes back at the end.
	 */
	struct FHighlight
	{
		FDeliveryResult Result;
		FResolveContext Ctx;
		FBatInput BatInput;
		float ReleaseTiming = 0.f;
		FString Commentary;
	};
	TArray<FHighlight> Highlights;
	FHighlight LiveClip;
	int32 ReelClip = -1;
	bool InReel() const { return ReelClip >= 0; }
	void PlayClip(int32 Clip);
	/**
	 * Ball tracking after every ball that hits the pad, once any replay is over: a view from behind the bowler's
	 * stumps with the players hidden, drawing the delivery up to the pad and then where it would have gone, while
	 * the HUD calls pitching, impact and wickets in turn.
	 */
	static constexpr float ReviewTime = 5.f;
	/**
	 * Player reviews. An LBW appeal gets the on-field umpire's decision (CricketUmpire::GivesLBW); the side it goes
	 * against may review it while it has reviews left: a human with the REVIEW (or ACCEPT) button within ReviewWindow, the
	 * AI after a moment's thought. The delivery is scored once that is settled.
	 */
	static constexpr float ReviewWindow = 5.f;
	bool bAwaitingReview = false, bOnFieldOut = false, bReviewTaken = false;
	int32 ReviewingTeam = -1;
	CricketUmpire::EReview ReviewResult = CricketUmpire::EReview::Upheld;
	FDeliveryOutcome PendingOutcome;
	bool HumanReviews() const { return bAwaitingReview && !bAutoPlay && ReviewingTeam == HumanTeam; }
	/**
	 * Third umpire. A run out or stumping too close to see (CricketUmpire::RefersToThirdUmpire) waits on frames
	 * around the stumps being broken: side-on to the crease, then from down the pitch back at it, each rolling in super slow
	 * motion from ThirdUmpireLead before the bails come off and freezing there. The delivery is scored after, and
	 * the verdict shows while the close-up plays.
	 */
	static constexpr float ThirdUmpireTime = 5.f, ThirdUmpireLead = 0.4f;
	bool bAwaitingThirdUmpire = false, bReferredThis = false;
	int32 ThirdUmpireAngle() const { return PhaseTime < 0.5f * ThirdUmpireTime ? 0 : 1; }
	float ThirdUmpireBallTime() const
	{
		const float Into = FMath::Fmod(PhaseTime, 0.5f * ThirdUmpireTime);
		return Result.BrokenTime - ThirdUmpireLead + FMath::Min(Into * SuperSlowSpeed, ThirdUmpireLead);
	}
	bool bReviewThis = false;
	float ReviewFrom() const { return ReplayDelay + (bReplayThis ? ReplayTotalTime() : 0.f); }
	bool IsReviewing() const { return bReviewThis && DPhase == EDeliveryPhase::DeadBall && PhaseTime >= ReviewFrom() && PhaseTime < ReviewFrom() + ReviewTime; }
	/** How far the review has got, 0 to 1: the path reaches the pad at 0.4 and the stumps at 0.7. */
	float ReviewProgress() const { return FMath::Clamp((PhaseTime - ReviewFrom()) / ReviewTime, 0.f, 1.f); }
	/** The innings break or result: after the last ball's banner and replay, a scorecard over a wide shot of the ground. */
	static constexpr float ScorecardDelay = 2.2f;
	bool ShowingScorecard() const
	{
		return (Match.Phase == EMatchPhase::InningsBreak || Match.Phase == EMatchPhase::MatchComplete) && !IsReplaying() && !IsReviewing()
			&& (DPhase != EDeliveryPhase::DeadBall || PhaseTime >= ScorecardDelay);
	}

	/** The on-screen touch controls, the only gameplay input on every platform (on desktop the mouse is the finger). */
	CricketTouch::EMode TouchMode() const;
	float ViewAspect = 16.f / 9.f;
	CricketTouch::FInsets TouchSafe; // the screen's safe area, refreshed every frame
	TArray<CricketTouch::FFinger> TouchFingers; // this frame's fingers, for the HUD's pressed states

	/**
	 * Field editor for the human bowling side. Before a ball (never once the run-up starts) the captain moves the
	 * outfielders' starting spots; CricketField::Validate is the only judge of what is legal. Only where they
	 * start changes: how they field is the fielding model's. The field is kept as it would be set to a
	 * right-hander and mirrored for a left-hander, as a captain would.
	 */
	bool bFieldEdit = false;
	TArray<FFielder> EditField;                 // the field being edited, in this striker's simulation frame
	int32 EditPick = INDEX_NONE;                // the fielder under the finger
	int32 EditPreset = INDEX_NONE;              // the preset the edit field still matches (INDEX_NONE once a fielder is moved)
	FVector2D EditDrag = FVector2D::ZeroVector; // where the finger holds them (simulation frame)
	FString EditDragWhy;                        // what would be illegal about dropping them there (empty: legal)
	FString EditError;                          // why the last drop was refused, shown for a moment
	double EditErrorAt = -100.0;
	TArray<FVector2D> HumanFieldRH;             // the captain's field for a right-hander, in Make's order; empty: the preset
	bool CanEditField() const
	{
		return HumanBowls() && DPhase == EDeliveryPhase::Waiting && Match.Phase == EMatchPhase::ReadyForDelivery && !IsReplaying();
	}
	/** The captain's field for this striker, over a preset field (keeper and bowler kept). False when there is none or it is illegal. */
	bool ApplyHumanField(TArray<FFielder>& Field, ECricketHand BatHand) const;
	/** For the HUD: the finger that owns the gesture and where it landed, and where the bowling target sits in the world. */
	const CricketTouch::FGesture& Gesture() const { return TouchGesture; }
	FVector TargetMarkerLocation() const;
	/** For the radar: where everyone stands right now (simulation frame), the field indexed as Ctx.Field. */
	struct FLiveField
	{
		TArray<FVector2D> Field;
		FVector2D Striker = FVector2D::ZeroVector, NonStriker = FVector2D::ZeroVector, Ball = FVector2D::ZeroVector;
		bool bBall = false;
	};
	FLiveField LiveField() const;

	/**
	 * Ball readability: the ball is drawn at its true size until, seen from DistanceM through a lens of
	 * HorizontalFovDeg, it would cover less than MinBallScreen of the screen height; then it is enlarged
	 * just enough to stay that size (at most MaxBallScale), so it never vanishes on a wide shot but is
	 * never oversized close up.
	 */
	static constexpr float MinBallScreen = 0.008f, MaxBallScale = 4.f;
	static float BallDisplayScale(float DistanceM, float HorizontalFovDeg, float Aspect);

	bool HumanBats() const { return !bAutoPlay && Match.BattingTeam() == HumanTeam; }
	bool HumanBowls() const { return !bAutoPlay && Match.BowlingTeam() == HumanTeam; }
	const FCricketPlayer& StrikerPlayer() const
	{
		if ((IsReplaying() || InReel()) && !Ctx.Striker.Name.IsEmpty())
		{
			return Ctx.Striker;
		}
		return Teams[Match.BattingTeam()].Batters[Match.Cur().Striker];
	}
	const FCricketPlayer& BowlerPlayer() const { return Teams[Match.BowlingTeam()].Bowler; }
	FString DirectionName() const;

	// ---- IPL tournament context (additive: standalone matches never set IPLFixtureId) ----
	/** Season fixture being played, or -1 for a standalone match. */
	int32 IPLFixtureId = INDEX_NONE;
	/** The user's franchise index (season space), or -1 outside a season. */
	int32 IPLUserTeam = INDEX_NONE;
	int32 IPLHomeTeam = INDEX_NONE, IPLAwayTeam = INDEX_NONE; // season-space franchises
	FIPLPlayingXI IPLHomeXI, IPLAwayXI;
	/** Toss: which match side batted first (0 home, 1 away). */
	int32 IPLBatFirst = 0;
	/** Legal balls bowled per XI slot this innings, per match side; drives the 4-over limit. */
	TArray<int32> IPLBowlerBalls[2];
	int32 IPLBowlerSlot[2] = { INDEX_NONE, INDEX_NONE };     // current bowler's XI slot per side
	int32 IPLLastBowlerSlot[2] = { INDEX_NONE, INDEX_NONE }; // previous over's bowler per side
	bool bAwaitingBatter = false; // wicket fell: the user picks who walks out next
	bool bAwaitingBowler = false; // over done: the user picks who bowls next
	int32 IPLAwaitingSlot = INDEX_NONE; // auto-assigned incoming batter slot (swap source)
	TArray<int32> IPLAwaitingCandidates; // XI slots offered (includes the auto pick for batters)
	bool bIPLCommitted = false;          // the result reached the season exactly once

	bool IsIPLMatch() const { return IPLFixtureId != INDEX_NONE; }
	bool IsAwaitingPick() const { return bAwaitingBatter || bAwaitingBowler; }
	const FIPLPlayingXI& IPLXIForSide(int32 Side) const { return Side == 0 ? IPLHomeXI : IPLAwayXI; }
	/** The selector panel: title plus one line per candidate, in touch-layout order. */
	FString IPLPickTitle() const;
	TArray<FString> IPLPickNames() const;
	bool ChooseNextBatter(int32 Candidate);
	bool ChooseNextBowler(int32 Candidate);
	/** Pause-menu restart: re-stages this exact fixture (same XIs) before its result commits. */
	void RestartIPLFixture();

private:
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> ShapeMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> VertexColourMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> GrassMaterial;
	/** A material built by Scripts/stadium/make_stadium.sh into /Game/Stadium, or null when it has not been run. */
	static UMaterialInterface* LoadStadiumMaterial(const TCHAR* Name);
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> CrowdSections; // CricketStadium::Build's crowd meshes, in order
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CrowdMaterial; // the 3D crowd's, when it is built
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FlagMaterial;  // the crowd's flags', which wave harder with it
	UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> ScreenTarget; // what every big screen shows
	FString ScreenShown;
	void UpdateBigScreens();
	/** The ground: -CricketVenue=0..2, otherwise a random one (the first on AutoPlay runs). */
	int32 VenueIndex = 0;
	/** This ball's conditions: the venue's pitch, worn a little more by every ball bowled on it, and its weather. */
	FPitchConditions Conditions() const;
	/** The pitch's material, and the marks this match has left on it: ball marks and the bowlers' footmarks. */
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PitchFace;
	UPROPERTY() TObjectPtr<class UTextureRenderTarget2D> MarksTarget;
	void DrawPitchMarks(const FBallMark& Mark);
	UPROPERTY() TObjectPtr<AStaticMeshActor> Ball;
	UPROPERTY() TObjectPtr<AStaticMeshActor> Bat;
	UPROPERTY() TObjectPtr<AStaticMeshActor> NonStrikerBat;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> BatHandles; // striker's, non-striker's
	UPROPERTY() TObjectPtr<AStaticMeshActor> Striker;
	UPROPERTY() TObjectPtr<AStaticMeshActor> NonStriker;
	UPROPERTY() TObjectPtr<AStaticMeshActor> Bowler;
	UPROPERTY() TObjectPtr<AStaticMeshActor> TargetMarker;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> Fielders;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> Umpires;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> TrackSegments; // the ball-tracking trail, spawned on the first review
	UPROPERTY() TObjectPtr<USkeletalMesh> BodyMesh;
	UPROPERTY() TMap<TObjectPtr<AActor>, TObjectPtr<USkeletalMeshComponent>> Bodies; // each figure's posed body
	UPROPERTY() TObjectPtr<UAnimSequence> IdleAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> JogAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> SprintAnim;
	UPROPERTY() TObjectPtr<UAnimSequence> WalkAnim;
	CricketKeeper::FKeeperFeet KeeperFeet; // the keeper's planted feet, stepped by UpdatePoses
	static constexpr float SprintSpeed = 6.3f; // m/s the sprint clip's stride covers at its own rate (import log)
	UPROPERTY() TObjectPtr<UAnimSequence> ThrowAnim;    // optional, as are the dives
	UPROPERTY() TObjectPtr<UAnimSequence> DiveAnims[2]; // to the fielder's right, left
	UAnimSequence* Diving = nullptr;                    // this frame's dive for the fielder who took the ball, if any
	UPROPERTY() TObjectPtr<UAnimSequence> BowlAnims[6]; // the authored actions by EBowlerType, right arm then left; the IK arm otherwise
	UAnimSequence* BowlAnim() const;
	/** How far this bowler's run-in moves out to pass the stumps (CricketPose::StumpPush), for the clip and body size it was found for. */
	const UAnimSequence* StumpPushClip = nullptr;
	float StumpPushScale = 0.f, StumpPush = 0.f;
	/** The bowler's action at the presented time: in from setting off on the run-up, out after the follow-through. */
	CricketPose::FClipPlay BowlPlay(float T, bool bLive) const;
	/** The captured throw for fielder I this ball: the thrower's (unless underarm) or the relay fielder's. */
	CricketPose::FClipPlay ThrowPlay(int32 I, float Post) const;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> Figures; // everyone who stands on the field
	struct FFigureState { FVector Last = FVector::ZeroVector; FVector Look = FVector::ZeroVector; float Speed = 0.f; bool bHeld = false; bool bCarried = false; }; // held: a clip sets this frame's facing; carried: a clip moves the body, so the actor's move is not running
	TMap<TObjectPtr<AStaticMeshActor>, FFigureState> FigureStates;
	// Sound: one ball channel for contact cues (bat families, edges, pitching, pads, keeper,
	// catches, throws, stumps) plus run-up footsteps, and a looping crowd bed. The AudioDirector
	// picks every cue from the authoritative contact data and drives the crowd's energy; the
	// CommentaryDirector picks caption/voice lines. Player vocals are timing hooks (placeholder).
	UPROPERTY() TObjectPtr<UAudioComponent> FieldAudio;
	UPROPERTY() TObjectPtr<UAudioComponent> CrowdAudio;
	UPROPERTY() TObjectPtr<USoundWaveProcedural> FieldWave;
	UPROPERTY() TObjectPtr<USoundWaveProcedural> CrowdWave;
	TArray<int16> CuePcm[int32(CricketAudio::ECue::Count)];
	// Voiced commentary and player calls: ElevenLabs clips (Scripts/audio/eleven.py) under Content/Audio,
	// raw 22.05 kHz mono s16le. A line without a clip stays caption-only.
	UPROPERTY() TObjectPtr<UAudioComponent> VoiceAudio;
	UPROPERTY() TObjectPtr<UAudioComponent> VocalAudio;
	UPROPERTY() TObjectPtr<USoundWaveProcedural> VoiceWave;
	UPROPERTY() TObjectPtr<USoundWaveProcedural> VocalWave;
	TArray<int16> VoicePending;
	float VoiceAt = -1.f;
	/** Queue the body clip then the situation clip to start at `At`; nothing when the body has no clip. */
	void Say(const FString& Body, const FString& Suffix, float At);
	float CrowdLevel = 0.3f, PrevCueT = 0.f;
	// Broadcast audio: the directors consume semantic events and pick levels/cues/lines.
	// Gameplay never asks them what happened; they never tell gameplay what to do.
	CricketAudioDirector::FState AudioDir;
	CricketCommentaryDirector::FState CommDir;
	FRandomStream CommRng;
	float NextFootstepAt = 0.f;
	FVector LastBallPos = FVector::ZeroVector;
	/** Frame timings for the performance summary logged at the end of play (ms; draw calls). */
	struct FPerfSample { float Frame, Game, Render, Gpu; };
	TArray<FPerfSample> Perf;
	TArray<float> HandMiss; // striker's hands from their bat targets (cm), logged with the perf summary
	int32 ElbowChecks = 0, ElbowInside = 0; // striker's elbow and forearm samples, and those inside the torso
	int32 RaisedChecks = 0, ElbowFlared = 0; // striker's arm samples with the hand above the shoulder, and those with the elbow winged out
	int32 ArmChecks = 0, ElbowUp = 0; // striker's arm samples, and those with the elbow bowed up off the shoulder-to-wrist line
	TArray<float> FootSlide;                 // how far a planted foot's ball moved in a frame (cm)
	FVector LastBall[2] = { FVector::ZeroVector, FVector::ZeroVector };
	bool bWasPlanted[2] = { false, false };
	FTransform StrikerPosed;                 // the striker where the body poses were last set
	float StrikerPosedT = 0.f;               // and the ball time they were set for
	bool bStrikerCut = false;                // that time jumped (a replay's rewind, a new ball): not motion
	FVector2D StrikerFrom = FVector2D::ZeroVector; // where the striker's stroke left them, to set off for a run from
	bool bRecordAudio = false, bRecording = false;
	bool bTouchWasDown[10] = {}, bMouseWasDown = false;
	// -CricketTouchScript: plays a whole match through the touch layer by injecting touches into the
	// player controller, batting with the AI's shot choices (mode tap, then pull-and-release) and
	// bowling with a set plan.
	bool bTouchScript = false, bScriptTapDown = false, bScriptStickDown = false, bScriptRunTapped = false;
	bool bScriptFieldDone = false;
	int32 ScriptFieldStep = 0;
	float ScriptFieldFrom = 0.f;
	FVector2D ScriptTapAt = FVector2D::ZeroVector;
	FVector2D ScriptPullOrigin = FVector2D::ZeroVector, ScriptPullTarget = FVector2D::ZeroVector;
	FBatInput ScriptShot;

	FRandomStream Rng;
	TArray<int32> RecentPlans;
	float ReleaseTiming = 0.f;
	bool bViewSet = false, bCutCamera = true;
	int32 LastShot = -1; // legacy shot latch, kept for save/debug compat; the director owns shot state now
	// Broadcast camera + replay runtime (isolated modules; the game mode only feeds them state).
	CricketBroadcast::FDirectorState BroadcastDirector;
	CricketBroadcast::FSmoother BroadcastSmoother;
	CricketBroadcast::FDeliveryClockFollow DeliveryCamClock;
	CricketBroadcast::FReplayPackage ActivePackage;
	TArray<CricketBroadcast::FTimeRemap> ActiveRemaps;
	TArray<EBroadcastShot> RecentReplayShots;
	FCricketReplayBuffer ReplayBuffer;
	TArray<FReplayActorPose> ReplayScratch;
	TArray<TObjectPtr<AStaticMeshActor>> ReplayCast;
	bool bBufferPose = false;
	FString LastCameraDebug;
	EBroadcastShot LastSolvedShot = EBroadcastShot::StandardDelivery;
	void ClearBroadcastReplay();
	void BuildReplayPackageForResult(const FDeliveryOutcome& Outcome, bool bMilestone);
	void RebuildReplayCast();
	void RecordReplayFrame(float BallT, const FVector& BallPos, const FVector& BallVel);
	// Keyboard pull-and-release charge, and the touch gesture owning the pull / target drag.
	CricketTouch::FGesture TouchGesture;

	AStaticMeshActor* Spawn(UStaticMesh* Mesh, const FVector& PosM, const FVector& SizeM, const FLinearColor& Colour);
	/** Colours a figure's kit. With a team, its shirt also gets the team's sponsor and the name and number on the back. */
	void Paint(AStaticMeshActor* Actor, const FLinearColor& Colour, const FCricketTeam* Team = nullptr, const FString& Name = FString(), int32 Number = 0);
	/** The print for a crew-neck shirt, slim or broad cut, drawn once and kept: red marks take the print's first
	 *  colour, green its second, blue its third. */
	class UTexture* ShirtPrint(const FString& Name, int32 Number, const FString& Sponsor, bool bBroad);
	UPROPERTY() TMap<FString, TObjectPtr<class UTextureRenderTarget2D>> ShirtPrints;
	/** The Blender kit's shirt print (M_Kit): full-colour name, number and sponsor, drawn once and kept. */
	class UTexture* KitPrint(const FString& Name, int32 Number, const FString& Sponsor);
	UPROPERTY() TMap<FString, TObjectPtr<class UTextureRenderTarget2D>> KitPrints;
	void BuildScene();
	void HandleInput(class APlayerController* PC, float Dt);
	void BeginRunUp();
	void DoRelease(float Timing);
	void FinishDelivery();
	void ScoreDelivery(FDeliveryOutcome Outcome);
	// IPL tournament wiring (all no-ops without a staged fixture).
	void SetupIPLMatch();
	void AfterIPLDelivery(const FDeliveryOutcome& Outcome, const TArray<ECricketEvent>& Events, int32 PreNext);
	void IPLNewInningsSetup();
	void ApplyIPLBowler(int32 Side, int32 Slot);
	void CommitIPLResult();
	void ReturnToIPLHub();
	void SettleReview(bool bReview);
	bool AiReviews() const;
	void Emit(const TArray<ECricketEvent>& Events);
	void PlaceForDelivery();
	void UpdatePresentation(float Dt);
	void AddBody(AStaticMeshActor* Marker, const TCHAR* MetaHuman);
	USkeletalMeshComponent* BodyOf(const AActor* Figure) const;
	void RefreshCutFigures();
	void UpdateFigures(float Dt);
	void BuildStadium();
	void UpdateCrowd();
	void UpdateTracking();
	void UpdatePoses(float T, bool bLive, float Post, float Off, float Arm);
	class UCricketAnimInstance* AnimOf(AActor* Figure) const;
	void CheckFigures();
	bool bFiguresChecked = false;
	/** Seconds from the bowler's release (negative before it; very negative when no delivery is under way). */
	float TimeToRelease(float T, bool bLive) const;
	/** Where the bowler is along the run-up, arriving at the crease on an ideally timed release. */
	float RunUpX(float Time) const;
	void SetupAudio();
	FCricketControls ReadTouch(class APlayerController* PC);
	/** The field editor from this frame's controls: open, pick up and drop fielders, presets, apply or cancel; closes itself once editing is locked. */
	void UpdateFieldEdit(const FCricketControls& C);
	/** A short vibration on the player's device (touch phones, pads), scaled by the control tuning. */
	void Haptic(float Strength) const;
	void InjectTouch(class APlayerController* PC, int32 Finger, uint8 Type, const FVector2D& At);
	void RunTouchScript(class APlayerController* PC);
	void PlayCue(CricketAudio::ECue Cue, float Volume);
};
