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
#include "SuperOverGameMode.generated.h"

class AStaticMeshActor;
class ACameraActor;
class UStaticMesh;
class UMaterialInterface;

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

	UPROPERTY(EditAnywhere, Category = "Super Over") TArray<FCricketTeam> Teams;
	UPROPERTY(EditAnywhere, Category = "Super Over") int32 HumanTeam = 0;
	UPROPERTY(EditAnywhere, Category = "Super Over") int32 MatchSeed = 2026;
	UPROPERTY(EditAnywhere, Category = "Super Over") float RunUpSeconds = 1.8f;
	UPROPERTY(EditAnywhere, Category = "Super Over") float ExposureBias = -1.5f;

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
	FString LastSummary;
	float HumanRunMargin = 0.35f;
	float ShotDirection = 0.f;
	bool bDebug = false, bTrajectory = false, bAutoPlay = false, bForceWicket = false;
	int32 BallsPlayed = 0, ShotBall = 0; // -CricketShotBall=N: save the game view while delivery N is live
	float ShotClock = 0.f;

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
	UPROPERTY() TObjectPtr<AStaticMeshActor> Ball;
	UPROPERTY() TObjectPtr<AStaticMeshActor> Bat;
	UPROPERTY() TObjectPtr<AStaticMeshActor> Striker;
	UPROPERTY() TObjectPtr<AStaticMeshActor> NonStriker;
	UPROPERTY() TObjectPtr<AStaticMeshActor> Bowler;
	UPROPERTY() TObjectPtr<AStaticMeshActor> TargetMarker;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> Fielders;
	UPROPERTY() TObjectPtr<ACameraActor> Camera;

	FRandomStream Rng;
	TArray<int32> RecentPlans;
	float ReleaseTiming = 0.f;
	bool bViewSet = false;

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
};
