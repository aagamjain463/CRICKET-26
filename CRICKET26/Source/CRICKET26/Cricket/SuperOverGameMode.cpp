#include "SuperOverGameMode.h"
#include "SuperOverHUD.h"
#include "CRICKET26.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	FVector ToWorld(const FVector& M) { return M * 100.f; } // simulation metres -> Unreal cm

	FCricketPlayer MakePlayer(const TCHAR* Name, ECricketHand Bat, float Timing, float Power)
	{
		FCricketPlayer P;
		P.Name = Name;
		P.BatHand = Bat;
		P.Timing = Timing;
		P.Technique = Timing;
		P.Power = Power;
		return P;
	}

	const FLinearColor Grass(0.09f, 0.28f, 0.07f), Strip(0.55f, 0.47f, 0.3f), White(0.9f, 0.9f, 0.9f), Wood(0.8f, 0.65f, 0.4f);
}

ASuperOverGameMode::ASuperOverGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = nullptr;
	HUDClass = ASuperOverHUD::StaticClass();

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeF(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereF(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylF(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatF(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = CubeF.Object;
	SphereMesh = SphereF.Object;
	CylinderMesh = CylF.Object;
	ShapeMaterial = MatF.Object;

	// Placeholder squads (fictional). Super Over: three batters and one bowler per side.
	FCricketTeam Home;
	Home.Name = TEXT("Home XI");
	Home.Short = TEXT("HOM");
	Home.Colour = FLinearColor(0.05f, 0.2f, 0.75f);
	Home.Batters = { MakePlayer(TEXT("Opener"), ECricketHand::Right, 0.7f, 0.65f),
		MakePlayer(TEXT("Finisher"), ECricketHand::Left, 0.65f, 0.85f), MakePlayer(TEXT("Allrounder"), ECricketHand::Right, 0.55f, 0.7f) };
	Home.Bowler = MakePlayer(TEXT("Quick"), ECricketHand::Right, 0.3f, 0.3f);
	Home.Bowler.PaceKph = 142.f;
	Home.Bowler.Accuracy = 0.7f;

	FCricketTeam Away;
	Away.Name = TEXT("Away XI");
	Away.Short = TEXT("AWY");
	Away.Colour = FLinearColor(0.6f, 0.08f, 0.1f);
	Away.Batters = { MakePlayer(TEXT("Hitter"), ECricketHand::Left, 0.6f, 0.9f),
		MakePlayer(TEXT("Anchor"), ECricketHand::Right, 0.75f, 0.55f), MakePlayer(TEXT("Keeper-bat"), ECricketHand::Right, 0.6f, 0.7f) };
	Away.Bowler = MakePlayer(TEXT("Wrist spinner"), ECricketHand::Right, 0.3f, 0.3f);
	Away.Bowler.BowlerType = EBowlerType::LegSpin;
	Away.Bowler.PaceKph = 86.f;
	Away.Bowler.Accuracy = 0.7f;
	Away.Bowler.Movement = 0.7f;
	Teams = { Home, Away };
	HumanPlan.Length = 5.f;
}

void ASuperOverGameMode::StartPlay()
{
	Super::StartPlay();
	Rng.Initialize(MatchSeed);
	bAutoPlay = FParse::Param(FCommandLine::Get(), TEXT("CricketAutoPlay")); // soak/smoke runs
	BuildScene();
	Match.Start(HumanTeam);
	PlaceForDelivery();
}

AStaticMeshActor* ASuperOverGameMode::Spawn(UStaticMesh* Mesh, const FVector& PosM, const FVector& SizeM, const FLinearColor& Colour)
{
	AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(ToWorld(PosM), FRotator::ZeroRotator);
	UStaticMeshComponent* C = A->GetStaticMeshComponent();
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	A->SetActorScale3D(SizeM); // basic shapes are 1 m
	Paint(A, Colour);
	return A;
}

void ASuperOverGameMode::Paint(AStaticMeshActor* A, const FLinearColor& Colour)
{
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ShapeMaterial, A);
	M->SetVectorParameterValue(TEXT("Color"), Colour);
	A->GetStaticMeshComponent()->SetMaterial(0, M);
}

void ASuperOverGameMode::BuildScene()
{
	using namespace CricketGeo;
	UWorld* W = GetWorld();

	ADirectionalLight* Sun = W->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-42.f, 35.f, 0.f));
	Sun->GetComponent()->SetMobility(EComponentMobility::Movable);
	Sun->GetComponent()->SetAtmosphereSunLight(true);
	Sun->GetComponent()->SetIntensity(8.f);
	ASkyLight* Sky = W->SpawnActor<ASkyLight>();
	Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sky->GetLightComponent()->bRealTimeCapture = true;
	Sky->GetLightComponent()->RecaptureSky();
	W->SpawnActor<ASkyAtmosphere>();
	AExponentialHeightFog* Fog = W->SpawnActor<AExponentialHeightFog>();
	Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(0.45f, 0.6f, 0.85f));
	Fog->GetComponent()->SetFogDensity(0.004f);
	// The project disables auto exposure; darken the fixed exposure so placeholder colours read true.
	APostProcessVolume* PP = W->SpawnActor<APostProcessVolume>();
	PP->bUnbound = true;
	PP->Settings.bOverride_AutoExposureBias = true;
	PP->Settings.AutoExposureBias = ExposureBias;

	const FVector C = PitchCentre();
	Spawn(CylinderMesh, FVector(C.X, 0.f, -0.06f), FVector(800.f, 800.f, 0.1f), Grass);
	Spawn(CubeMesh, FVector(C.X, 0.f, -0.005f), FVector(PitchLength + 2.4f, 2.f * PitchHalfWidth, 0.01f), Strip);
	for (float X : { 0.f, PitchLength })
	{
		const float Dir = X == 0.f ? 1.f : -1.f;
		Spawn(CubeMesh, FVector(X + Dir * PoppingCrease, 0.f, 0.001f), FVector(0.05f, 3.66f, 0.004f), White);
		Spawn(CubeMesh, FVector(X, 0.f, 0.001f), FVector(0.05f, 2.64f, 0.004f), White);
		for (float Y : { -1.32f, 1.32f }) Spawn(CubeMesh, FVector(X + Dir * 0.6f, Y, 0.001f), FVector(2.44f, 0.05f, 0.004f), White);
		for (float Y : { -StumpsHalfWidth + 0.018f, 0.f, StumpsHalfWidth - 0.018f })
		{
			Spawn(CylinderMesh, FVector(X, Y, StumpHeight * 0.5f), FVector(0.036f, 0.036f, StumpHeight), Wood);
		}
	}
	// Boundary rope and the 30-yard circle.
	for (int32 I = 0; I < 96; ++I)
	{
		const float A = 2.f * PI * I / 96.f;
		const FVector P(C.X + BoundaryRadius * FMath::Cos(A), BoundaryRadius * FMath::Sin(A), 0.04f);
		AStaticMeshActor* Seg = Spawn(CubeMesh, P, FVector(0.08f, 2.f * PI * BoundaryRadius / 96.f, 0.08f), White);
		Seg->SetActorRotation(FRotator(0.f, FMath::RadiansToDegrees(A), 0.f));
	}
	// ponytail: stands are raked slabs so the follow camera never sees past the ground; swap for a stadium mesh.
	for (int32 I = 0; I < 48; ++I)
	{
		const float A = 2.f * PI * I / 48.f;
		const float R = BoundaryRadius + 22.f;
		AStaticMeshActor* Stand = Spawn(CubeMesh, FVector(C.X + R * FMath::Cos(A), R * FMath::Sin(A), 8.f),
			FVector(28.f, 2.f * PI * R / 48.f + 0.5f, 1.f), FLinearColor(0.18f, 0.2f, 0.26f));
		Stand->SetActorRotation(FRotator(35.f, FMath::RadiansToDegrees(A), 0.f));
	}
	for (int32 I = 0; I < 72; ++I)
	{
		const float A = 2.f * PI * I / 72.f;
		Spawn(CylinderMesh, FVector(C.X + 27.4f * FMath::Cos(A), 27.4f * FMath::Sin(A), 0.01f), FVector(0.25f, 0.25f, 0.02f), White);
	}

	// ponytail: ball drawn at 2x size so it reads on a telephoto view; replace with a streak/trail when real assets land.
	Ball = Spawn(SphereMesh, FVector(0.f, 0.f, -5.f), FVector(2.f * 2.f * BallRadius), FLinearColor(0.85f, 0.85f, 0.8f));
	Bat = Spawn(CubeMesh, FVector::ZeroVector, FVector(0.11f, 0.05f, 0.85f), Wood);
	Striker = Spawn(CylinderMesh, FVector::ZeroVector, FVector(0.45f, 0.45f, 1.8f), FLinearColor::White);
	NonStriker = Spawn(CylinderMesh, FVector::ZeroVector, FVector(0.45f, 0.45f, 1.8f), FLinearColor::White);
	Bowler = Spawn(CylinderMesh, FVector::ZeroVector, FVector(0.45f, 0.45f, 1.85f), FLinearColor::White);
	TargetMarker = Spawn(CylinderMesh, FVector::ZeroVector, FVector(0.3f, 0.3f, 0.004f), FLinearColor(1.f, 0.85f, 0.f));
	for (int32 I = 0; I < 11; ++I) Fielders.Add(Spawn(CylinderMesh, FVector::ZeroVector, FVector(0.45f, 0.45f, 1.8f), FLinearColor::White));

	Camera = W->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
}

FString ASuperOverGameMode::DirectionName() const
{
	const float D = ShotDirection;
	if (FMath::Abs(D) < 15.f) return TEXT("straight");
	const TCHAR* Side = D > 0.f ? TEXT("off") : TEXT("leg");
	const float A = FMath::Abs(D);
	return FString::Printf(TEXT("%s (%s side)"), A < 60.f ? (D > 0.f ? TEXT("cover") : TEXT("midwicket"))
		: A < 110.f ? TEXT("square") : TEXT("fine"), Side);
}

void ASuperOverGameMode::PlaceForDelivery()
{
	const FCricketPlayer& Batter = StrikerPlayer();
	const FCricketPlayer& Bwl = BowlerPlayer();
	Ctx = FResolveContext();
	Ctx.Striker = Batter;
	Ctx.NonStriker = Teams[Match.BattingTeam()].Batters[Match.Cur().NonStriker];
	Ctx.Bowler = Bwl;
	Ctx.Fielding.Catching = 0.75f;
	Ctx.Fielding.Throwing = 0.65f;
	Ctx.Field = CricketField::Make(CricketField::PresetFor(Bwl.BowlerType), Batter.BatHand, Bwl.BowlHand);
	Ctx.bFreeHit = Match.bFreeHit;
	Ctx.Rules = Match.Rules;
	Ctx.BouncersBowled = Match.Cur().Bouncers;

	const float Off = OffSideSign(Batter.BatHand);
	const float Arm = Bwl.BowlHand == ECricketHand::Right ? 1.f : -1.f;
	const FLinearColor BatCol = Teams[Match.BattingTeam()].Colour, FieldCol = Teams[Match.BowlingTeam()].Colour;
	Striker->SetActorLocation(ToWorld(FVector(0.9f, -0.35f * Off, 0.9f)));
	NonStriker->SetActorLocation(ToWorld(FVector(CricketGeo::PitchLength - 1.3f, -1.1f * Arm, 0.9f)));
	Bowler->SetActorLocation(ToWorld(FVector(CricketGeo::PitchLength + 14.f, 0.5f * Arm, 0.925f)));
	Paint(Striker, BatCol);
	Paint(NonStriker, BatCol);
	Paint(Bowler, FieldCol);
	for (int32 I = 0; I < Fielders.Num(); ++I)
	{
		const bool bUsed = Ctx.Field.IsValidIndex(I) && !Ctx.Field[I].bBowler;
		Fielders[I]->SetActorHiddenInGame(!bUsed);
		if (!bUsed) continue;
		Fielders[I]->SetActorLocation(ToWorld(FVector(Ctx.Field[I].Home.X, Ctx.Field[I].Home.Y, 0.9f)));
		Paint(Fielders[I], Ctx.Field[I].bKeeper ? FieldCol * 0.5f : FieldCol);
	}
	Ball->SetActorLocation(ToWorld(FVector(CricketGeo::PitchLength + 14.f, 0.5f * Arm, 1.2f)));
	BatInput = FBatInput();
	Result = FDeliveryResult();
	BowlerIntent.Reset();
}

void ASuperOverGameMode::Tick(float Dt)
{
	Super::Tick(Dt);
	Dt = FMath::Min(Dt, 0.1f);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && !bViewSet)
	{
		PC->SetViewTarget(Camera);
		bViewSet = true;
	}
	if (PC) HandleInput(PC, Dt);
	PhaseTime += Dt;

	switch (DPhase)
	{
	case EDeliveryPhase::Waiting:
		if (Match.Phase == EMatchPhase::ReadyForDelivery && !HumanBowls() && PhaseTime > 1.2f) BeginRunUp();
		else if (bAutoPlay && PhaseTime > 2.5f)
		{
			if (Match.Phase == EMatchPhase::InningsBreak) Match.StartSecondInnings();
			else if (Match.Phase == EMatchPhase::MatchComplete) { if (!Match.StartNextSuperOver()) Match.Start(HumanTeam); }
			PlaceForDelivery();
			PhaseTime = 0.f;
		}
		break;
	case EDeliveryPhase::RunUp:
		Meter = FMath::Min(1.f, -1.f + 2.f * PhaseTime / RunUpSeconds);
		if (!HumanBowls() && Meter >= ReleaseTiming) DoRelease(ReleaseTiming);
		else if (PhaseTime >= RunUpSeconds) DoRelease(0.95f); // never let go: overstepped
		break;
	case EDeliveryPhase::BallInPlay:
		if (PhaseTime >= Result.DeadTime) FinishDelivery();
		break;
	case EDeliveryPhase::DeadBall:
		if (PhaseTime > 1.8f)
		{
			DPhase = EDeliveryPhase::Waiting;
			PhaseTime = 0.f;
			if (Match.Phase == EMatchPhase::ReadyForDelivery) PlaceForDelivery();
		}
		break;
	}
	UpdatePresentation(Dt);
}

void ASuperOverGameMode::HandleInput(APlayerController* PC, float Dt)
{
	auto Pressed = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };
	auto Down = [PC](const FKey& K) { return PC->IsInputKeyDown(K); };

	if (Pressed(EKeys::F1)) bDebug = !bDebug;
	if (Pressed(EKeys::F4)) bTrajectory = !bTrajectory;
	if (Pressed(EKeys::F5)) bForceWicket = true;
	if (Pressed(EKeys::F8)) bAutoPlay = !bAutoPlay;
	if (DPhase == EDeliveryPhase::Waiting && Match.Phase == EMatchPhase::ReadyForDelivery)
	{
		if (Pressed(EKeys::F2))
		{
			FCricketPlayer& P = Teams[Match.BattingTeam()].Batters[Match.Cur().Striker];
			P.BatHand = P.BatHand == ECricketHand::Right ? ECricketHand::Left : ECricketHand::Right;
			PlaceForDelivery();
		}
		if (Pressed(EKeys::F3))
		{
			FCricketPlayer& B = Teams[Match.BowlingTeam()].Bowler;
			B.BowlerType = EBowlerType((uint8(B.BowlerType) + 1) % 3);
			B.PaceKph = B.BowlerType == EBowlerType::Pace ? 140.f : 86.f;
			HumanPlan.Type = CricketBowling::Repertoire(B.BowlerType)[0];
			PlaceForDelivery();
		}
	}
	if (Pressed(EKeys::Enter) && DPhase == EDeliveryPhase::Waiting)
	{
		if (Match.Phase == EMatchPhase::InningsBreak) Match.StartSecondInnings();
		else if (Match.Phase == EMatchPhase::MatchComplete) { if (!Match.StartNextSuperOver()) Match.Start(HumanTeam); }
		PlaceForDelivery();
	}

	const bool bLeft = Down(EKeys::A) || Down(EKeys::Left), bRight = Down(EKeys::D) || Down(EKeys::Right);
	const bool bUp = Down(EKeys::W) || Down(EKeys::Up), bDown = Down(EKeys::S) || Down(EKeys::Down);

	if (HumanBowls())
	{
		if (DPhase == EDeliveryPhase::Waiting)
		{
			const TArray<EDeliveryType> Rep = CricketBowling::Repertoire(BowlerPlayer().BowlerType);
			const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven };
			for (int32 I = 0; I < Rep.Num() && I < UE_ARRAY_COUNT(Numbers); ++I) if (Pressed(Numbers[I])) HumanPlan.Type = Rep[I];
			if (!Rep.Contains(HumanPlan.Type)) HumanPlan.Type = Rep[0];
			// The camera looks down the pitch from behind the bowler: screen left is world +Y.
			const float Off = OffSideSign(StrikerPlayer().BatHand);
			HumanPlan.Length = FMath::Clamp(HumanPlan.Length + ((bDown ? 1.f : 0.f) - (bUp ? 1.f : 0.f)) * 4.f * Dt, 0.5f, 13.f);
			HumanPlan.Line = FMath::Clamp(HumanPlan.Line + ((bLeft ? 1.f : 0.f) - (bRight ? 1.f : 0.f)) * Off * 0.8f * Dt, -1.f, 1.6f);
			if (Pressed(EKeys::SpaceBar) && Match.Phase == EMatchPhase::ReadyForDelivery) BeginRunUp();
		}
		else if (DPhase == EDeliveryPhase::RunUp && Pressed(EKeys::SpaceBar))
		{
			DoRelease(FMath::Min(1.f, -1.f + 2.f * CricketMath::PressTime(PhaseTime, Dt) / RunUpSeconds));
		}
	}
	else if (HumanBats())
	{
		if (Pressed(EKeys::R)) HumanRunMargin = HumanRunMargin > 0.5f ? -0.1f : HumanRunMargin + 0.35f;
		const float Side = (bLeft ? 1.f : 0.f) - (bRight ? 1.f : 0.f);
		const float Base = bUp ? 40.f : bDown ? 130.f : 85.f;
		ShotDirection = Side == 0.f ? 0.f : Base * Side * OffSideSign(StrikerPlayer().BatHand);
		if (DPhase == EDeliveryPhase::BallInPlay && !BatInput.IsShot() && !Result.bTooLate)
		{
			EBatIntent Intent = EBatIntent::Leave;
			if (Pressed(EKeys::J)) Intent = EBatIntent::Ground;
			if (Pressed(EKeys::K)) Intent = EBatIntent::Loft;
			if (Pressed(EKeys::L)) Intent = EBatIntent::Defend;
			if (Intent != EBatIntent::Leave)
			{
				BatInput.Intent = Intent;
				BatInput.DirectionDeg = Intent == EBatIntent::Defend ? 0.f : ShotDirection;
				BatInput.PressTime = CricketMath::PressTime(PhaseTime, Dt);
				// Deterministic re-resolve: everything already shown is identical.
				Ctx.RunMargin = HumanRunMargin;
				Result = CricketDelivery::Resolve(Release, BatInput, Ctx);
			}
		}
	}
}

void ASuperOverGameMode::BeginRunUp()
{
	if (!Match.BeginDelivery()) return;
	PlaceForDelivery();
	Ctx.Seed = Rng.RandHelper(1 << 30);
	const float Aggr = CricketAI::Aggression(Match);
	Ctx.RunMargin = HumanBats() ? HumanRunMargin : CricketAI::RunMargin(Match, Aggr);
	if (!HumanBowls())
	{
		const FBowlingChoice Choice = CricketAI::ChooseDelivery(BowlerPlayer(), StrikerPlayer().BatHand, Match, RecentPlans, Rng);
		RecentPlans.Add(Choice.PlanId);
		HumanPlan = Choice.Plan; // the plan being executed, whoever chose it
		ReleaseTiming = Choice.ReleaseTiming;
		BowlerIntent = Choice.Label;
	}
	DPhase = EDeliveryPhase::RunUp;
	PhaseTime = 0.f;
	Meter = -1.f;
}

void ASuperOverGameMode::DoRelease(float Timing)
{
	const FCricketPlayer& Batter = StrikerPlayer();
	Release = CricketBowling::Execute(BowlerPlayer(), Batter.BatHand, HumanPlan, Timing, Ctx.Seed, Ctx.Conditions);
	ReleaseTiming = Timing;
	BatInput = FBatInput();
	if (!HumanBats())
	{
		FRandomStream AiRng(Ctx.Seed + 1);
		BatInput = CricketAI::ChooseShot(Release, Batter, BowlerPlayer().BowlerType, CricketAI::Aggression(Match), Ctx.Field, Ctx.Conditions, AiRng);
	}
	Result = CricketDelivery::Resolve(Release, BatInput, Ctx);
	DPhase = EDeliveryPhase::BallInPlay;
	PhaseTime = 0.f;
}

void ASuperOverGameMode::FinishDelivery()
{
	FDeliveryOutcome Outcome = Result.ToOutcome();
	if (bForceWicket && !Match.bFreeHit && !Outcome.bNoBall && !Outcome.bWide)
	{
		// Debug tool (F5): the next legal delivery is given out bowled regardless of the simulation.
		Outcome = FDeliveryOutcome();
		Outcome.Dismissal = EDismissal::Bowled;
		Result.Summary = TEXT("[DEBUG] Forced wicket  >  BOWLED!");
		bForceWicket = false;
	}
	TArray<ECricketEvent> Events;
	if (!Match.CompleteDelivery(Outcome, Events))
	{
		UE_LOG(LogCRICKET26, Error, TEXT("Rules rejected the simulated outcome (%s); delivery voided."), *Result.Summary);
		// Recover by voiding as a dot ball so the slice cannot soft-lock.
		Match.CompleteDelivery(FDeliveryOutcome(), Events);
	}
	FString Err;
	if (!Match.CheckInvariants(Err)) UE_LOG(LogCRICKET26, Error, TEXT("Match invariant broken: %s"), *Err);
	LastSummary = Result.Summary;
	UE_LOG(LogCRICKET26, Display, TEXT("%s %d/%d (%d.%d): %s"), *Teams[Match.BattingTeam()].Short, Match.Cur().Runs,
		Match.Cur().Wickets, Match.Cur().LegalBalls / 6, Match.Cur().LegalBalls % 6, *LastSummary);
	Emit(Events);
	DPhase = EDeliveryPhase::DeadBall;
	PhaseTime = 0.f;
}

void ASuperOverGameMode::Emit(const TArray<ECricketEvent>& Events)
{
	for (ECricketEvent E : Events) OnCricketEvent.Broadcast(E);
}

void ASuperOverGameMode::UpdatePresentation(float Dt)
{
	using namespace CricketGeo;
	const float Arm = BowlerPlayer().BowlHand == ECricketHand::Right ? 1.f : -1.f;
	const float Off = OffSideSign(StrikerPlayer().BatHand);
	const bool bLive = DPhase == EDeliveryPhase::BallInPlay || DPhase == EDeliveryPhase::DeadBall;
	const float T = DPhase == EDeliveryPhase::DeadBall ? Result.DeadTime : PhaseTime;
	const float Post = T - Result.ContactTime; // seconds after contact (or after passing the batter)

	// Bowler: run-up, delivery stride, follow-through.
	FVector BowlerPos(PitchLength + 14.f, 0.5f * Arm, 0.925f);
	if (DPhase == EDeliveryPhase::RunUp) BowlerPos.X = FMath::Lerp(PitchLength + 14.f, PitchLength - 1.f, FMath::Clamp(PhaseTime / RunUpSeconds, 0.f, 1.f));
	// Follow-through ends at the bowler's fielding mark, where the fielding solver has them.
	const int32 BowlerSlot = Ctx.Field.IndexOfByPredicate([](const FFielder& F) { return F.bBowler; });
	const FVector2D FollowThrough = BowlerSlot >= 0 ? Ctx.Field[BowlerSlot].Home : FVector2D(PitchLength - 4.f, 0.5f * Arm);
	if (bLive)
	{
		const float A = FMath::Clamp(T, 0.f, 1.f);
		BowlerPos = FVector(FMath::Lerp(PitchLength - 1.f, FollowThrough.X, A), FMath::Lerp(0.5f * Arm, FollowThrough.Y, A), BowlerPos.Z);
	}
	Bowler->SetActorLocation(ToWorld(BowlerPos));

	TargetMarker->SetActorHiddenInGame(!(HumanBowls() && (DPhase == EDeliveryPhase::Waiting || DPhase == EDeliveryPhase::RunUp)));
	TargetMarker->SetActorLocation(ToWorld(FVector(HumanPlan.Length, HumanPlan.Line * Off, 0.003f)));

	// Ball.
	FVector BallPos = ToWorld(FVector(BowlerPos.X - 0.3f, 0.4f * Arm, 1.1f));
	if (bLive) BallPos = ToWorld(Result.BallAt(T));
	Ball->SetActorLocation(BallPos);

	// Fielders run where the coordinator sends them (chase, back up, cover the stumps) at the speed the
	// solver assumed, so nobody arrives sooner than they physically could.
	if (bLive)
	{
		for (const FFielderMove& Move : Result.Fielding.Moves)
		{
			if (!Ctx.Field.IsValidIndex(Move.Fielder) || Post < Move.Start) continue;
			const FFielder& Who = Ctx.Field[Move.Fielder];
			const FVector2D P = CricketField::PositionOf(Move, Who, Post, Ctx.Fielding.RunSpeed);
			if (Who.bBowler) Bowler->SetActorLocation(ToWorld(FVector(P.X, P.Y, 0.925f)));
			else if (Fielders.IsValidIndex(Move.Fielder)) Fielders[Move.Fielder]->SetActorLocation(ToWorld(FVector(P.X, P.Y, 0.9f)));
		}
	}

	// Batters running between the wickets.
	const FRunningOutcome& Run = Result.Running;
	if (bLive && Run.Attempted > 0 && Post > 0.f)
	{
		float Along = 0.f; // 0 = original ends, 1 = swapped
		float Start = 0.35f;
		for (int32 N = 0; N < Run.RunTimes.Num(); ++N)
		{
			const float A = FMath::Clamp((Post - Start) / FMath::Max(Run.RunTimes[N] - Start, 0.1f), 0.f, 1.f);
			if (Post < Start) break;
			Along = (N % 2 == 0) ? A : 1.f - A;
			Start = Run.RunTimes[N] + 0.3f;
		}
		if (Run.bSentBack && Post >= Start)
		{
			// Out towards the next run, then sent back to the end they left.
			const float Out = Run.SentBackFrom * FMath::Clamp((Post - Start) / FMath::Max(Run.SentBackAt - Start, 0.05f), 0.f, 1.f);
			const float A = Post < Run.SentBackAt ? Out : Run.SentBackFrom * (1.f - FMath::Clamp((Post - Run.SentBackAt) / FMath::Max(Run.BackIn - Run.SentBackAt, 0.1f), 0.f, 1.f));
			Along = (Run.RunTimes.Num() % 2 == 0) ? A : 1.f - A;
		}
		const float SX = FMath::Lerp(0.9f, PitchLength - 1.3f, Along), NX = FMath::Lerp(PitchLength - 1.3f, 0.9f, Along);
		Striker->SetActorLocation(ToWorld(FVector(SX, -0.9f * Off, 0.9f)));
		NonStriker->SetActorLocation(ToWorld(FVector(NX, 0.9f * Off, 0.9f)));
	}
	else if (bLive && BatInput.IsShot())
	{
		// Footwork: the striker steps to where the stroke is played (down the track to a spinner), then back.
		const float S = FMath::Clamp((T - BatInput.PressTime) / FMath::Max(Result.Shot.SwingTime, 0.05f), 0.f, 1.f);
		const float Back = FMath::Clamp((T - Result.ContactTime - 0.4f) / 0.8f, 0.f, 1.f);
		const float X = FMath::Lerp(FMath::Lerp(0.9f, Result.Shot.ContactX() - 0.45f, FMath::SmoothStep(0.f, 1.f, S)), 0.9f, Back);
		Striker->SetActorLocation(ToWorld(FVector(X, -0.35f * Off, 0.9f)));
	}

	// Bat: backlift while the ball is coming, swing over the shot's swing time, then follow-through.
	float Angle = -25.f;
	if (DPhase == EDeliveryPhase::RunUp || (bLive && !BatInput.IsShot() && T < 1.5f)) Angle = -110.f;
	if (bLive && BatInput.IsShot())
	{
		const float S = FMath::Clamp((T - BatInput.PressTime) / FMath::Max(Result.Shot.SwingTime, 0.05f), 0.f, 1.f);
		Angle = FMath::Lerp(-110.f, 140.f, S);
	}
	const FVector Hands = Striker->GetActorLocation() + ToWorld(FVector(0.25f, 0.3f * Off, 0.1f));
	const float R = FMath::DegreesToRadians(Angle);
	const FVector Along(FMath::Sin(R), 0.f, -FMath::Cos(R));
	Bat->SetActorLocationAndRotation(Hands + Along * 45.f, FRotationMatrix::MakeFromZ(-Along).Rotator());

	// Camera: broadcast telephoto from behind the bowler, then pull wide and follow the ball after the shot.
	const bool bFollow = bLive && T > Result.ContactTime + 0.15f && (Result.Contact.HasContact() || Result.Fielding.Fielder >= 0);
	const FVector WantLoc = bFollow ? ToWorld(FVector(PitchLength + 32.f, 0.f, 20.f)) : ToWorld(FVector(PitchLength + 42.f, 0.f, 6.5f));
	const FVector LookAt = bFollow ? BallPos : ToWorld(FVector(1.5f, 0.f, 1.2f));
	const float WantFov = bFollow ? 42.f : 15.f;
	const float K = FMath::Clamp(Dt * 3.f, 0.f, 1.f);
	UCameraComponent* Cam = Camera->GetCameraComponent();
	const FVector Loc = FMath::Lerp(Camera->GetActorLocation(), WantLoc, bViewSet ? K : 1.f);
	const FRotator Want = (LookAt - Loc).Rotation();
	Camera->SetActorLocationAndRotation(Loc, FMath::RInterpTo(Camera->GetActorRotation(), Want, Dt, bFollow ? 5.f : 8.f));
	Cam->SetFieldOfView(FMath::Lerp(Cam->FieldOfView, WantFov, K));

	if (bTrajectory && Result.BallPath.Num() > 1)
	{
		const int32 Step = 6;
		for (int32 I = Step; I < Result.BallPath.Num(); I += Step)
		{
			DrawDebugLine(GetWorld(), ToWorld(Result.BallPath[I - Step]), ToWorld(Result.BallPath[I]),
				I * Result.SampleDt < Result.ContactTime ? FColor::Yellow : FColor::Cyan, false, -1.f, 0, 1.5f);
		}
		if (Result.PitchTime >= 0.f) DrawDebugSphere(GetWorld(), ToWorld(Result.PitchPos), 8.f, 8, FColor::Red);
	}
}
