// Playable Super Over vertical slice. Owns the authoritative match state and the per-delivery flow
// (wait -> run-up -> ball in play -> dead ball), reads input, and drives placeholder presentation.
// The cricket itself (rules, physics, contact, fielding, AI) lives in the pure modules; this class
// only feeds them input and replays their results.
//
// Launch: UnrealEditor CRICKET26.uproject "/Engine/Maps/Entry?game=/Script/CRICKET26.SuperOverGameMode" -game

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CricketAI.h"
#include "CricketAudio.h"
#include "CricketControls.h"
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
	UPROPERTY(EditAnywhere, Category = "Super Over") float RunUpSeconds = 1.8f;
	static constexpr float IdealRelease = 0.f; // bowling meter value of a perfectly timed release
	static constexpr float RunUpLength = 8.f;  // metres of approach shown before the crease
	UPROPERTY(EditAnywhere, Category = "Super Over") float ExposureEV100 = 15.f; // fixed camera exposure: the sunny-16 rule for a 100000 lux sun
	static constexpr float SunLux = 100000.f;
	static constexpr float NightLux = 2000.f; // on the field under the floodlights
	/** The sun's light travels from the bowler's end toward the striker, 42 degrees above the horizon. */
	static inline const FRotator SunRotation = FRotator(-42.f, 215.f, 0.f);
	static class ADirectionalLight* SpawnSun(UWorld* World);

	/** Semantic match events for HUD, camera, audio and (later) commentary. */
	FOnCricketEvent OnCricketEvent;

	// Read by the HUD.
	FSuperOverMatch Match;
	EDeliveryPhase DPhase = EDeliveryPhase::Waiting;
	float PhaseTime = 0.f;         // seconds in the current delivery phase (ball time once released)
	FDeliveryPlan HumanPlan;
	float Meter = -1.f;            // human release meter, -1..1, perfect at 0
	FDeliveryRelease Release;
	FDeliveryResult Result;
	FBatInput BatInput;
	FResolveContext Ctx;
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
	TArray<float> DevCam;    // -CricketDevCam
	bool bDevCamFielder = false;                 // -CricketQuitAfter=N: quit after N deliveries (default: after the shot ball)
	float ShotClock = 0.f;
	/**
	 * Action replay of the key moment after a wicket or boundary, in two angles of ReplayAngleTime each: side-on at
	 * half speed from ReplayLead before the contact, then down the pitch from the main camera, tight on the striker,
	 * in super slow motion from SuperSlowLead before it.
	 */
	static constexpr float ReplayDelay = 2.f, ReplaySpeed = 0.5f, ReplayLead = 0.8f, ReplayAction = 1.6f;
	static constexpr float SuperSlowSpeed = 0.2f, SuperSlowLead = 0.3f;
	static constexpr float ReplayAngleTime = ReplayAction / ReplaySpeed, ReplayTime = 2.f * ReplayAngleTime;
	bool bReplayThis = false, bWicketThis = false;
	bool IsReplaying() const { return bReplayThis && DPhase == EDeliveryPhase::DeadBall && PhaseTime >= ReplayDelay && PhaseTime < ReplayDelay + ReplayTime; }
	int32 ReplayAngle() const { return PhaseTime - ReplayDelay < ReplayAngleTime ? 0 : 1; }
	/** The ball time the replay is showing. */
	float ReplayBallTime() const
	{
		const float Into = PhaseTime - ReplayDelay;
		return ReplayAngle() == 0 ? FMath::Max(0.f, Result.ContactTime - ReplayLead) + Into * ReplaySpeed
			: FMath::Max(0.f, Result.ContactTime - SuperSlowLead) + (Into - ReplayAngleTime) * SuperSlowSpeed;
	}
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
	 * against may review it while it has reviews left: a human with V (or Enter to accept) within ReviewWindow, the
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
	float ReviewFrom() const { return ReplayDelay + (bReplayThis ? ReplayTime : 0.f); }
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

	/** On-screen touch controls: on for phones and tablets, or -CricketTouch on desktop (the mouse is a finger). */
	bool bTouchUI = false;
	CricketTouch::EMode TouchMode() const;
	float ViewAspect = 16.f / 9.f;

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
	const FCricketPlayer& StrikerPlayer() const { return Teams[Match.BattingTeam()].Batters[Match.Cur().Striker]; }
	const FCricketPlayer& BowlerPlayer() const { return Teams[Match.BowlingTeam()].Bowler; }
	FString DirectionName() const;

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
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> Figures; // everyone who stands on the field
	struct FFigureState { FVector Last = FVector::ZeroVector; float Speed = 0.f; };
	TMap<TObjectPtr<AStaticMeshActor>, FFigureState> FigureStates;
	// Sound: one channel for the ball's cues (bat, edge, pitching, stumps) and a looping crowd bed whose
	// level swells on boundaries and wickets.
	UPROPERTY() TObjectPtr<UAudioComponent> FieldAudio;
	UPROPERTY() TObjectPtr<UAudioComponent> CrowdAudio;
	UPROPERTY() TObjectPtr<USoundWaveProcedural> FieldWave;
	UPROPERTY() TObjectPtr<USoundWaveProcedural> CrowdWave;
	TArray<int16> CuePcm[int32(CricketAudio::ECue::Count)];
	float CrowdLevel = 0.3f, PrevCueT = 0.f;
	FVector LastBallPos = FVector::ZeroVector;
	/** Frame timings for the performance summary logged at the end of play (ms; draw calls). */
	struct FPerfSample { float Frame, Game, Render, Gpu; };
	TArray<FPerfSample> Perf;
	TArray<float> HandMiss; // striker's hands from their bat targets (cm), logged with the perf summary
	bool bRecordAudio = false, bRecording = false;
	bool bTouchWasDown[10] = {};
	// -CricketTouchScript: plays a whole match through the touch layer by injecting touches into the
	// player controller, batting with the AI's shot choices and bowling with a set plan.
	bool bTouchScript = false, bScriptTapDown = false, bScriptStickDown = false;
	FVector2D ScriptTapAt = FVector2D::ZeroVector;
	FBatInput ScriptShot;

	FRandomStream Rng;
	TArray<int32> RecentPlans;
	float ReleaseTiming = 0.f;
	bool bViewSet = false, bCutCamera = true;
	int32 LastShot = -1; // the camera director's shot last frame: a change of shot is a cut

	AStaticMeshActor* Spawn(UStaticMesh* Mesh, const FVector& PosM, const FVector& SizeM, const FLinearColor& Colour);
	void Paint(AStaticMeshActor* Actor, const FLinearColor& Colour);
	void BuildScene();
	void HandleInput(class APlayerController* PC, float Dt);
	void BeginRunUp();
	void DoRelease(float Timing);
	void FinishDelivery();
	void ScoreDelivery(FDeliveryOutcome Outcome);
	void SettleReview(bool bReview);
	bool AiReviews() const;
	void Emit(const TArray<ECricketEvent>& Events);
	void PlaceForDelivery();
	void UpdatePresentation(float Dt);
	void AddBody(AStaticMeshActor* Marker, const TCHAR* MetaHuman);
	USkeletalMeshComponent* BodyOf(const AActor* Figure) const;
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
	void InjectTouch(class APlayerController* PC, int32 Finger, uint8 Type, const FVector2D& At);
	void RunTouchScript(class APlayerController* PC);
	void PlayCue(CricketAudio::ECue Cue, float Volume);
};
