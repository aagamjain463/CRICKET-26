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
class UMaterialInterface;
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
	float HumanRunMargin = 0.35f;
	float ShotDirection = 0.f;
	bool bDebug = false, bTrajectory = false, bAutoPlay = false, bForceWicket = false;
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
	/** Action replay of the key moment after a wicket or boundary, from side-on at half speed. */
	static constexpr float ReplayDelay = 1.2f, ReplaySpeed = 0.5f, ReplayLead = 0.8f, ReplayAction = 1.6f;
	bool bReplayThis = false;
	bool IsReplaying() const { return bReplayThis && DPhase == EDeliveryPhase::DeadBall && PhaseTime >= ReplayDelay && PhaseTime < ReplayDelay + ReplayAction / ReplaySpeed; }
	/** The innings break or result: after the last ball's banner and replay, a scorecard over a wide shot of the ground. */
	static constexpr float ScorecardDelay = 2.2f;
	bool ShowingScorecard() const
	{
		return (Match.Phase == EMatchPhase::InningsBreak || Match.Phase == EMatchPhase::MatchComplete) && !IsReplaying()
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
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> CrowdSections; // CricketStadium::Build's crowd meshes, in order
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
	UPROPERTY() TObjectPtr<USkeletalMesh> BodyMesh;
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
	bool bViewSet = false, bCutCamera = true, bWasReplaying = false, bWasScorecard = false;

	AStaticMeshActor* Spawn(UStaticMesh* Mesh, const FVector& PosM, const FVector& SizeM, const FLinearColor& Colour);
	void Paint(AStaticMeshActor* Actor, const FLinearColor& Colour);
	void BuildScene();
	void HandleInput(class APlayerController* PC, float Dt);
	void BeginRunUp();
	void DoRelease(float Timing);
	void FinishDelivery();
	void Emit(const TArray<ECricketEvent>& Events);
	void PlaceForDelivery();
	void UpdatePresentation(float Dt);
	void AddBody(AStaticMeshActor* Marker);
	void UpdateFigures(float Dt);
	void BuildStadium();
	void UpdateCrowd();
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
