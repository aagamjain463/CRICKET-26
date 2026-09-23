#include "SuperOverGameMode.h"
#include "SuperOverHUD.h"
#include "CRICKET26.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Components/AudioComponent.h"
#include "CricketCommentary.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
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
#include "UnrealClient.h"

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
	bTouchScript = FParse::Param(FCommandLine::Get(), TEXT("CricketTouchScript")); // a human side, played by injected touches
	bAutoPlay = FParse::Param(FCommandLine::Get(), TEXT("CricketAutoPlay")) && !bTouchScript; // soak/smoke runs
	FParse::Value(FCommandLine::Get(), TEXT("CricketShotBall="), ShotBall);
	int32 Level = int32(Difficulty);
	FParse::Value(FCommandLine::Get(), TEXT("CricketDifficulty="), Level);
	Difficulty = CricketAI::EDifficulty(FMath::Clamp(Level, 0, 3));
	bTouchUI = PLATFORM_IOS || PLATFORM_ANDROID || bTouchScript || FParse::Param(FCommandLine::Get(), TEXT("CricketTouch"));
	bRecordAudio = FParse::Param(FCommandLine::Get(), TEXT("CricketRecordAudio")) && ShotBall > 0;
	BuildScene();
	SetupAudio();
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
	// A player's mannequin is painted in the team colour (its own material's tint).
	if (USkeletalMeshComponent* Body = A->FindComponentByClass<USkeletalMeshComponent>())
	{
		for (int32 I = 0; I < Body->GetNumMaterials(); ++I)
		{
			if (UMaterialInstanceDynamic* Kit = Body->CreateDynamicMaterialInstance(I))
			{
				Kit->SetVectorParameterValue(TEXT("Paint Tint"), Colour);
				Kit->SetVectorParameterValue(TEXT("LogoTint"), Colour);
			}
		}
	}
}

void ASuperOverGameMode::AddBody(AStaticMeshActor* Marker)
{
	Figures.Add(Marker);
	if (!BodyMesh) return;
	// The marker cylinder stays the authoritative position (centred 0.9 m up); the mannequin stands in it.
	USkeletalMeshComponent* Body = NewObject<USkeletalMeshComponent>(Marker);
	Body->SetSkeletalMesh(BodyMesh);
	Body->SetUsingAbsoluteScale(true);
	Body->SetupAttachment(Marker->GetRootComponent());
	Body->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -0.5f), FRotator(0.f, -90.f, 0.f)); // cylinder is 1.8 units tall: -0.5 = feet
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->RegisterComponent();
	Body->PlayAnimation(IdleAnim, true);
	Marker->GetStaticMeshComponent()->SetVisibility(false);
}

void ASuperOverGameMode::SetupAudio()
{
	for (int32 I = 0; I < int32(CricketAudio::ECue::Count); ++I) CuePcm[I] = CricketAudio::Synthesize(CricketAudio::ECue(I));
	auto Channel = [this](TObjectPtr<USoundWaveProcedural>& Wave, TObjectPtr<UAudioComponent>& Comp)
	{
		Wave = NewObject<USoundWaveProcedural>(this);
		Wave->SetSampleRate(CricketAudio::SampleRate);
		Wave->NumChannels = 1;
		Wave->bLooping = false;
		Comp = UGameplayStatics::CreateSound2D(this, Wave, 1.f, 1.f, 0.f, nullptr, true); // null with -nosound
		if (Comp) Comp->Play();
	};
	Channel(FieldWave, FieldAudio);
	Channel(CrowdWave, CrowdAudio);
}

void ASuperOverGameMode::PlayCue(CricketAudio::ECue Cue, float Volume)
{
	if (!FieldAudio) return;
	// ponytail: one ball channel, a new cue cuts the tail of the last; mix on separate channels if cues overlap audibly.
	FieldWave->ResetAudio();
	FieldAudio->SetVolumeMultiplier(Volume);
	const TArray<int16>& Pcm = CuePcm[int32(Cue)];
	FieldWave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.GetData()), Pcm.Num() * sizeof(int16));
}

void ASuperOverGameMode::UpdateFigures(float Dt)
{
	// Everyone faces the ball when standing and their direction of travel when moving, and plays idle or a
	// jog paced to their speed. A player tipped over for a dive keeps the dive.
	const FVector BallAt = Ball->GetActorLocation();
	const float Off = OffSideSign(StrikerPlayer().BatHand);
	for (AStaticMeshActor* A : Figures)
	{
		FFigureState& S = FigureStates.FindOrAdd(A);
		const FVector Pos = A->GetActorLocation();
		FVector Vel = Dt > 0.f && !S.Last.IsZero() ? (Pos - S.Last) / Dt : FVector::ZeroVector;
		if (Vel.Size() > 1500.f) { Vel = FVector::ZeroVector; S.Speed = 0.f; } // faster than anyone runs: a reset or replay jump
		S.Last = Pos;
		S.Speed = FMath::Lerp(S.Speed, FVector2D(Vel).Size() / 100.f, FMath::Clamp(Dt * 8.f, 0.f, 1.f));
		if (A->IsHidden()) continue;
		if (A->GetActorUpVector().Z > 0.95f)
		{
			// The striker holds a side-on stance, chest to the off side, until they set off for a run.
			const FVector Look = S.Speed > 0.8f ? Vel : A == Striker ? FVector(0.f, Off, 0.f) : BallAt - Pos;
			if (!FVector2D(Look).IsNearlyZero()) A->SetActorRotation(FRotator(0.f, Look.Rotation().Yaw, 0.f));
		}
		USkeletalMeshComponent* Body = A->FindComponentByClass<USkeletalMeshComponent>();
		if (!Body || !JogAnim) continue;
		const bool bJog = S.Speed > 0.8f;
		if (bJog != S.bJogging)
		{
			S.bJogging = bJog;
			Body->PlayAnimation(bJog ? JogAnim : IdleAnim, true);
		}
		// ponytail: one jog cycle time-scaled to speed (template jog ~4 m/s); a run/sprint blend when real locomotion lands.
		Body->SetPlayRate(bJog ? FMath::Clamp(S.Speed / 4.f, 0.6f, 2.f) : 1.f);
	}
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

	// Sightscreens behind both ends on the line of the pitch, so the batter (and the viewer) sees the ball
	// against white.
	for (const float Dir : { -1.f, 1.f })
	{
		Spawn(CubeMesh, FVector(C.X + Dir * (BoundaryRadius + 4.f), 0.f, 2.5f), FVector(1.f, 16.f, 5.f), FLinearColor(0.95f, 0.95f, 0.95f));
	}

	// ponytail: ball drawn at 2x size so it reads on a telephoto view; replace with a streak/trail when real assets land.
	Ball = Spawn(SphereMesh, FVector(0.f, 0.f, -5.f), FVector(2.f * 2.f * BallRadius), FLinearColor(0.85f, 0.85f, 0.8f));
	Bat = Spawn(CubeMesh, FVector::ZeroVector, FVector(0.11f, 0.05f, 0.85f), Wood);
	// Epic's template mannequin (UE EULA, ships with the project template) when present; plain markers otherwise.
	BodyMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	IdleAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	JogAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd.MF_Unarmed_Jog_Fwd"));
	Striker = Spawn(CylinderMesh, FVector::ZeroVector, FVector(0.45f, 0.45f, 1.8f), FLinearColor::White);
	NonStriker = Spawn(CylinderMesh, FVector::ZeroVector, FVector(0.45f, 0.45f, 1.8f), FLinearColor::White);
	Bowler = Spawn(CylinderMesh, FVector::ZeroVector, FVector(0.45f, 0.45f, 1.85f), FLinearColor::White);
	TargetMarker = Spawn(CylinderMesh, FVector::ZeroVector, FVector(0.3f, 0.3f, 0.004f), FLinearColor(1.f, 0.85f, 0.f));
	for (int32 I = 0; I < 11; ++I) Fielders.Add(Spawn(CylinderMesh, FVector::ZeroVector, FVector(0.45f, 0.45f, 1.8f), FLinearColor::White));
	for (int32 I = 0; I < 2; ++I) Umpires.Add(Spawn(CylinderMesh, FVector::ZeroVector, FVector(0.45f, 0.45f, 1.8f), FLinearColor::White));
	for (AStaticMeshActor* P : { Striker.Get(), NonStriker.Get(), Bowler.Get(), Umpires[0].Get(), Umpires[1].Get() }) AddBody(P);
	for (AStaticMeshActor* P : Fielders) AddBody(P);

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
	// Umpires: behind the bowler's stumps on the side away from the bowling arm, and at square leg.
	Umpires[0]->SetActorLocation(ToWorld(FVector(CricketGeo::PitchLength + 1.8f, -0.9f * Arm, 0.9f)));
	Umpires[1]->SetActorLocation(ToWorld(FVector(0.5f, -26.f * Off, 0.9f)));
	for (AStaticMeshActor* U : Umpires) Paint(U, FLinearColor(0.85f, 0.85f, 0.8f));
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
	if (PC && bTouchScript) RunTouchScript(PC);
	if (PC) HandleInput(PC, Dt);
	PhaseTime += Dt;
	// Dev capture of the game view alone, 5 times a second (the desktop is never recorded).
	const int32 LiveBall = DPhase == EDeliveryPhase::DeadBall ? BallsPlayed : BallsPlayed + 1; // dead ball: the one just finished
	if (ShotBall == LiveBall && DPhase != EDeliveryPhase::Waiting && (ShotClock += Dt) >= 0.2f)
	{
		ShotClock = 0.f;
		static int32 Shot = 0;
		FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / FString::Printf(TEXT("Ball%d_%03d.png"), ShotBall, Shot++), true, false);
		// -CricketRecordAudio: also write the mixed game audio of the delivery to Saved/BallN.wav.
		if (bRecordAudio && !bRecording) { UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 30.f); bRecording = true; }
	}
	if (ShotBall > 0 && BallsPlayed >= ShotBall && DPhase == EDeliveryPhase::Waiting)
	{
		if (bRecording) UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile, FString::Printf(TEXT("Ball%d"), ShotBall), FPaths::ProjectSavedDir());
		FPlatformMisc::RequestExit(false); // capture done
	}

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
		if (PhaseTime > 1.8f + (bReplayThis ? ReplayAction / ReplaySpeed : 0.f))
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
	if (Pressed(EKeys::F6)) Difficulty = CricketAI::EDifficulty((uint8(Difficulty) + 1) % 4);
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

	// Keyboard and touch fill the same controls; everything below reads only the controls.
	FCricketControls C;
	C.bLeft = Down(EKeys::A) || Down(EKeys::Left);
	C.bRight = Down(EKeys::D) || Down(EKeys::Right);
	C.bUp = Down(EKeys::W) || Down(EKeys::Up);
	C.bDown = Down(EKeys::S) || Down(EKeys::Down);
	C.bGround = Pressed(EKeys::J);
	C.bLoft = Pressed(EKeys::K);
	C.bDefend = Pressed(EKeys::L);
	C.bRun = Pressed(EKeys::R);
	C.bAction = Pressed(EKeys::SpaceBar);
	C.bProgress = Pressed(EKeys::Enter);
	const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven };
	for (int32 I = 0; I < UE_ARRAY_COUNT(Numbers); ++I) if (Pressed(Numbers[I])) C.DeliveryPick = I;
	const FCricketControls Touch = ReadTouch(PC);
	C.Merge(Touch);

	if (C.bProgress && IsReplaying()) PhaseTime = ReplayDelay + ReplayAction / ReplaySpeed; // skip the replay
	if (C.bProgress && DPhase == EDeliveryPhase::Waiting)
	{
		if (Match.Phase == EMatchPhase::InningsBreak) Match.StartSecondInnings();
		else if (Match.Phase == EMatchPhase::MatchComplete) { if (!Match.StartNextSuperOver()) Match.Start(HumanTeam); }
		PlaceForDelivery();
	}

	if (HumanBowls())
	{
		if (DPhase == EDeliveryPhase::Waiting)
		{
			const TArray<EDeliveryType> Rep = CricketBowling::Repertoire(BowlerPlayer().BowlerType);
			if (Rep.IsValidIndex(C.DeliveryPick)) HumanPlan.Type = Rep[C.DeliveryPick];
			if (!Rep.Contains(HumanPlan.Type)) HumanPlan.Type = Rep[0];
			// The camera looks down the pitch from behind the bowler: screen left is world +Y.
			const float Off = OffSideSign(StrikerPlayer().BatHand);
			HumanPlan.Length = FMath::Clamp(HumanPlan.Length + ((C.bDown ? 1.f : 0.f) - (C.bUp ? 1.f : 0.f)) * 4.f * Dt, 0.5f, 13.f);
			HumanPlan.Line = FMath::Clamp(HumanPlan.Line + ((C.bLeft ? 1.f : 0.f) - (C.bRight ? 1.f : 0.f)) * Off * 0.8f * Dt, -1.f, 1.6f);
			if (C.bAction && Match.Phase == EMatchPhase::ReadyForDelivery) BeginRunUp();
		}
		else if (DPhase == EDeliveryPhase::RunUp && C.bAction)
		{
			DoRelease(FMath::Min(1.f, -1.f + 2.f * CricketMath::PressTime(PhaseTime, Dt) / RunUpSeconds));
		}
	}
	else if (HumanBats())
	{
		if (C.bRun) HumanRunMargin = HumanRunMargin > 0.5f ? -0.1f : HumanRunMargin + 0.35f;
		const float Side = (C.bLeft ? 1.f : 0.f) - (C.bRight ? 1.f : 0.f);
		const float Base = C.bUp ? 40.f : C.bDown ? 130.f : 85.f;
		ShotDirection = Side == 0.f ? 0.f : Base * Side * OffSideSign(StrikerPlayer().BatHand);
		if (DPhase == EDeliveryPhase::BallInPlay && !BatInput.IsShot() && !Result.bTooLate)
		{
			EBatIntent Intent = EBatIntent::Leave;
			if (C.bGround) Intent = EBatIntent::Ground;
			if (C.bLoft) Intent = EBatIntent::Loft;
			if (C.bDefend) Intent = EBatIntent::Defend;
			if (Intent != EBatIntent::Leave)
			{
				BatInput.Intent = Intent;
				BatInput.DirectionDeg = Intent == EBatIntent::Defend ? 0.f : ShotDirection;
				BatInput.PressTime = CricketMath::PressTime(PhaseTime, Dt);
				// Deterministic re-resolve: everything already shown is identical.
				Ctx.RunMargin = HumanRunMargin;
				Result = CricketDelivery::Resolve(Release, BatInput, Ctx);
				UE_LOG(LogCRICKET26, Display, TEXT("Shot input (%s): intent %d, direction %.0f, press %.3f s"),
					Touch.bGround || Touch.bLoft || Touch.bDefend ? TEXT("touch") : TEXT("keys"), int32(Intent), BatInput.DirectionDeg, BatInput.PressTime);
			}
		}
	}
}

CricketTouch::EMode ASuperOverGameMode::TouchMode() const
{
	using CricketTouch::EMode;
	if (!bTouchUI) return EMode::None;
	if (IsReplaying() || (DPhase == EDeliveryPhase::Waiting && Match.Phase != EMatchPhase::ReadyForDelivery)) return EMode::Progress;
	if (HumanBats()) return EMode::Batting;
	if (HumanBowls() && (DPhase == EDeliveryPhase::Waiting || DPhase == EDeliveryPhase::RunUp)) return EMode::Bowling;
	return EMode::None;
}

FCricketControls ASuperOverGameMode::ReadTouch(APlayerController* PC)
{
	if (!bTouchUI) return FCricketControls();
	int32 VX = 0, VY = 0;
	PC->GetViewportSize(VX, VY);
	if (VY <= 0) return FCricketControls();
	ViewAspect = float(VX) / VY;
	// Positions in screen-height units, as the layout uses.
	TArray<FVector2D> Held, New;
	for (int32 I = 0; I < UE_ARRAY_COUNT(bTouchWasDown); ++I)
	{
		float X = 0.f, Y = 0.f;
		bool bDown = false;
		PC->GetInputTouchState(ETouchIndex::Type(I), X, Y, bDown);
		if (bDown) Held.Add(FVector2D(X, Y) / VY);
		if (bDown && !bTouchWasDown[I]) New.Add(FVector2D(X, Y) / VY);
		bTouchWasDown[I] = bDown;
	}
	// On desktop the mouse stands in for a finger.
	float MX = 0.f, MY = 0.f;
	if (PC->IsInputKeyDown(EKeys::LeftMouseButton) && PC->GetMousePosition(MX, MY))
	{
		Held.Add(FVector2D(MX, MY) / VY);
		if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton)) New.Add(FVector2D(MX, MY) / VY);
	}
	return CricketTouch::Read(TouchMode(), CricketBowling::Repertoire(BowlerPlayer().BowlerType).Num(), ViewAspect, Held, New);
}

void ASuperOverGameMode::InjectTouch(APlayerController* PC, int32 Finger, uint8 Type, const FVector2D& At)
{
	int32 VX = 0, VY = 0;
	PC->GetViewportSize(VX, VY);
	PC->InputTouch(FTouchId(IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), ETouchIndex::Type(Finger)), ETouchType::Type(Type), At * VY, 1.f, FPlatformTime::Cycles64());
}

void ASuperOverGameMode::RunTouchScript(APlayerController* PC)
{
	using namespace CricketTouch;
	// A tap is a finger down one frame and up the next.
	if (bScriptTapDown) { InjectTouch(PC, 0, ETouchType::Ended, ScriptTapAt); bScriptTapDown = false; return; }
	const EMode Mode = TouchMode();
	const int32 NumTypes = CricketBowling::Repertoire(BowlerPlayer().BowlerType).Num();
	auto Tap = [&](EButton Button, int32 Index, const TCHAR* What)
	{
		for (const FButton& B : Layout(Mode, NumTypes, ViewAspect))
			if (B.Button == Button && B.Index == Index)
			{
				ScriptTapAt = B.Rect.GetCenter();
				InjectTouch(PC, 0, ETouchType::Began, ScriptTapAt);
				bScriptTapDown = true;
				UE_LOG(LogCRICKET26, Display, TEXT("Touch script: tap %s at (%.2f, %.2f)"), What, ScriptTapAt.X, ScriptTapAt.Y);
			}
	};

	if (Mode == EMode::Progress && PhaseTime > 1.5f)
	{
		if (Match.Phase == EMatchPhase::MatchComplete && DPhase == EDeliveryPhase::Waiting) { FPlatformMisc::RequestExit(false); return; } // script done
		ScriptTapAt = FVector2D(ViewAspect * 0.5f, 0.4f);
		InjectTouch(PC, 0, ETouchType::Began, ScriptTapAt);
		bScriptTapDown = true;
		UE_LOG(LogCRICKET26, Display, TEXT("Touch script: tap to continue"));
	}
	else if (Mode == EMode::Batting)
	{
		// Bat with the AI's choice for this ball: hold the stick for its direction, tap its shot on time.
		if (DPhase == EDeliveryPhase::BallInPlay && !bScriptStickDown)
		{
			FRandomStream AiRng(Ctx.Seed + 7);
			ScriptShot = CricketAI::ChooseShot(Release, StrikerPlayer(), BowlerPlayer().BowlerType, CricketAI::Aggression(Match), Ctx.Field, Ctx.Conditions, AiRng, AiSkill());
			const float Side = FMath::Sign(ScriptShot.DirectionDeg) * OffSideSign(StrikerPlayer().BatHand); // +1: stick left
			const float Abs = FMath::Abs(ScriptShot.DirectionDeg);
			const FVector2D Stick = StickCentre() + StickRadius * FVector2D(-Side, Side == 0.f ? 0.f : Abs < 62.f ? -1.f : Abs > 107.f ? 1.f : 0.f);
			InjectTouch(PC, 1, ETouchType::Began, Stick);
			bScriptStickDown = true;
			UE_LOG(LogCRICKET26, Display, TEXT("Touch script: AI would play intent %d, direction %.0f, press %.3f s"), int32(ScriptShot.Intent), ScriptShot.DirectionDeg, ScriptShot.PressTime);
		}
		if (DPhase == EDeliveryPhase::BallInPlay && !BatInput.IsShot() && ScriptShot.IsShot() && PhaseTime >= ScriptShot.PressTime)
		{
			const EButton B = ScriptShot.Intent == EBatIntent::Loft ? EButton::Loft : ScriptShot.Intent == EBatIntent::Defend ? EButton::Defend : EButton::Ground;
			Tap(B, 0, B == EButton::Loft ? TEXT("LOFT") : B == EButton::Defend ? TEXT("DEFEND") : TEXT("GROUND"));
		}
		if (DPhase == EDeliveryPhase::DeadBall && bScriptStickDown)
		{
			InjectTouch(PC, 1, ETouchType::Ended, StickCentre());
			bScriptStickDown = false;
			ScriptShot = FBatInput();
		}
	}
	else if (Mode == EMode::Bowling)
	{
		// Bowl the second delivery type in the repertoire, releasing near the perfect point of the meter.
		const EDeliveryType Want = CricketBowling::Repertoire(BowlerPlayer().BowlerType)[1];
		if (DPhase == EDeliveryPhase::Waiting && PhaseTime > 0.5f)
			HumanPlan.Type != Want ? Tap(EButton::Delivery, 1, TEXT("delivery 2")) : Tap(EButton::Bowl, 0, TEXT("BOWL (run-up)"));
		else if (DPhase == EDeliveryPhase::RunUp && -1.f + 2.f * PhaseTime / RunUpSeconds >= -0.05f)
			Tap(EButton::Bowl, 0, TEXT("BOWL (release)"));
	}
}

void ASuperOverGameMode::BeginRunUp()
{
	if (!Match.BeginDelivery()) return;
	PlaceForDelivery();
	Ctx.Seed = Rng.RandHelper(1 << 30);
	const float Aggr = CricketAI::Aggression(Match);
	Ctx.RunMargin = HumanBats() ? HumanRunMargin : CricketAI::RunMargin(Match, Aggr, AiSkill());
	if (!HumanBowls())
	{
		const FBowlingChoice Choice = CricketAI::ChooseDelivery(BowlerPlayer(), StrikerPlayer().BatHand, Match, RecentPlans, Rng, AiSkill());
		RecentPlans.Add(Choice.PlanId);
		HumanPlan = Choice.Plan; // the plan being executed, whoever chose it
		ReleaseTiming = Choice.ReleaseTiming;
		BowlerIntent = Choice.Label;
	}
	DPhase = EDeliveryPhase::RunUp;
	PhaseTime = 0.f;
	Meter = -1.f;
	// A new delivery is a new shot: cut back to the bowler's-end camera instead of easing from the last
	// ball's follow, and retire the last ball's result from the HUD.
	bCutCamera = true;
	LastSummary.Reset();
	Commentary.Reset();
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
		BatInput = CricketAI::ChooseShot(Release, Batter, BowlerPlayer().BowlerType, CricketAI::Aggression(Match), Ctx.Field, Ctx.Conditions, AiRng, AiSkill());
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
	const CricketCommentary::FNames Names{ StrikerPlayer().Name, Teams[Match.BattingTeam()].Batters[Match.Cur().NonStriker].Name, Teams[Match.BattingTeam()].Name };
	const float OffSign = OffSideSign(StrikerPlayer().BatHand);
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
	Commentary = CricketCommentary::Describe(Result, Outcome, Match, Names, OffSign, BallsPlayed);
	UE_LOG(LogCRICKET26, Display, TEXT("Commentary: %s"), *Commentary);
	++BallsPlayed;
	UE_LOG(LogCRICKET26, Display, TEXT("%s %d/%d (%d.%d): %s"), *Teams[Match.BattingTeam()].Short, Match.Cur().Runs,
		Match.Cur().Wickets, Match.Cur().LegalBalls / 6, Match.Cur().LegalBalls % 6, *LastSummary);
	Emit(Events);
	bReplayThis = (Events.Contains(ECricketEvent::Wicket) || Events.Contains(ECricketEvent::BoundaryFour) || Events.Contains(ECricketEvent::BoundarySix))
		&& Result.BallPath.Num() > 1;
	DPhase = EDeliveryPhase::DeadBall;
	PhaseTime = 0.f;
}

void ASuperOverGameMode::Emit(const TArray<ECricketEvent>& Events)
{
	for (ECricketEvent E : Events)
	{
		// The crowd lifts for boundaries and wickets, then settles back to the bed.
		if (E == ECricketEvent::BoundarySix) CrowdLevel = 1.f;
		else if (E == ECricketEvent::BoundaryFour || E == ECricketEvent::Wicket) CrowdLevel = FMath::Max(CrowdLevel, 0.8f);
		OnCricketEvent.Broadcast(E);
	}
}

void ASuperOverGameMode::UpdatePresentation(float Dt)
{
	using namespace CricketGeo;
	const float Arm = BowlerPlayer().BowlHand == ECricketHand::Right ? 1.f : -1.f;
	const float Off = OffSideSign(StrikerPlayer().BatHand);
	const bool bLive = DPhase == EDeliveryPhase::BallInPlay || DPhase == EDeliveryPhase::DeadBall;
	const bool bReplay = IsReplaying();
	if (bReplay != bWasReplaying) bCutCamera = true; // cut into and out of the replay
	bWasReplaying = bReplay;
	const float ReplayFrom = FMath::Max(0.f, Result.ContactTime - ReplayLead);
	const float T = bReplay ? ReplayFrom + (PhaseTime - ReplayDelay) * ReplaySpeed : DPhase == EDeliveryPhase::DeadBall ? Result.DeadTime : PhaseTime;

	// Ball sounds when the presented ball passes each moment, so a replay plays them again.
	if (T < PrevCueT) PrevCueT = T; // a new ball or a replay rewinds the clock
	if (bLive)
	{
		using CricketAudio::ECue;
		auto Crossed = [&](float At) { return At > PrevCueT && At <= T; };
		const EContactZone Z = Result.Contact.Zone;
		const bool bEdge = Z == EContactZone::InsideEdge || Z == EContactZone::OutsideEdge || Z == EContactZone::TopEdge || Z == EContactZone::BottomEdge;
		if (Result.PitchTime > 0.f && Crossed(Result.PitchTime)) PlayCue(ECue::Bounce, 0.5f);
		if (Result.Contact.HasContact() && Crossed(Result.ContactTime)) PlayCue(bEdge ? ECue::EdgeTick : ECue::BatCrack, 0.4f + 0.6f * Result.Contact.Quality);
		if (Result.bStumpsHit && Crossed(Result.StumpsTime)) PlayCue(ECue::Stumps, 1.f);
	}
	PrevCueT = T;
	CrowdLevel = FMath::FInterpTo(CrowdLevel, 0.3f, Dt, 0.4f);
	if (CrowdAudio) CrowdAudio->SetVolumeMultiplier(CrowdLevel);
	if (CrowdWave && CrowdWave->GetAvailableAudioByteCount() < CricketAudio::SampleRate * 2)
		CrowdWave->QueueAudio(reinterpret_cast<const uint8*>(CuePcm[int32(CricketAudio::ECue::Crowd)].GetData()), CuePcm[int32(CricketAudio::ECue::Crowd)].Num() * sizeof(int16));
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
		const FFielderMove& Relay = Result.Running.RelayMove;
		if (Fielders.IsValidIndex(Relay.Fielder) && Post >= Relay.Start)
		{
			const FVector2D P = CricketField::PositionOf(Relay, Ctx.Field[Relay.Fielder], Post, Ctx.Fielding.RunSpeed);
			Fielders[Relay.Fielder]->SetActorLocation(ToWorld(FVector(P.X, P.Y, 0.9f)));
		}
		// Going to ground for a dive or a slide: the primary lies toward the ball from just before the take
		// until they are back up (the same time the solver charges before the throw).
		const FFieldingOutcome& Fd = Result.Fielding;
		const EFieldAction A = Fd.Action;
		const bool bGround = Fd.bDive || A == EFieldAction::SlideStop || A == EFieldAction::CatchDiving;
		if (Fielders.IsValidIndex(Fd.Fielder) && !Ctx.Field[Fd.Fielder].bBowler)
		{
			const float Down = FMath::Clamp((Post - Fd.FieldTime + 0.2f) / 0.2f, 0.f, 1.f) * FMath::Clamp((Fd.FieldTime + 0.8f - Post) / 0.3f, 0.f, 1.f);
			AActor* Who = Fielders[Fd.Fielder];
			const FVector2D Lean = (FVector2D(Fd.FieldPos) - FVector2D(Who->GetActorLocation() / 100.f)).GetSafeNormal();
			const FVector Up = FMath::Lerp(FVector::UpVector, FVector(Lean.X, Lean.Y, 0.25f).GetSafeNormal(), bGround ? 0.85f * Down : 0.f);
			Who->SetActorRotation(FRotationMatrix::MakeFromZ(Up).Rotator());
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
	const bool bFollow = bLive && !bReplay && T > Result.ContactTime + 0.15f && (Result.Contact.HasContact() || Result.Fielding.Fielder >= 0);
	// Delivery shot: a long lens high in the stand behind the bowler, framing the striker and keeper about
	// 11 m across so the batter reads large and the flight is compressed, as on a broadcast.
	FVector WantLoc = bFollow ? ToWorld(FVector(PitchLength + 32.f, 0.f, 20.f)) : ToWorld(FVector(PitchLength + 62.f, 0.f, 12.f));
	FVector LookAt = bFollow ? BallPos : ToWorld(FVector(0.5f, 0.f, 1.3f));
	float WantFov = bFollow ? 42.f : 8.f;
	if (bReplay)
	{
		// Side-on from the off side at batter height (facing the stance, clear of the square-leg umpire):
		// the stroke, then the ball's flight on a wider lens.
		WantLoc = ToWorld(FVector(Result.Shot.ContactX() + 2.f, 38.f * Off, 2.2f));
		LookAt = T < Result.ContactTime + 0.3f ? ToWorld(FVector(Result.Shot.ContactX(), 0.f, 1.f)) : BallPos;
		WantFov = T < Result.ContactTime + 0.3f ? 12.f : 35.f;
	}
	const float K = FMath::Clamp(Dt * 3.f, 0.f, 1.f);
	UCameraComponent* Cam = Camera->GetCameraComponent();
	const FVector Loc = bCutCamera ? WantLoc : FMath::Lerp(Camera->GetActorLocation(), WantLoc, bViewSet ? K : 1.f);
	const FRotator Want = (LookAt - Loc).Rotation();
	Camera->SetActorLocationAndRotation(Loc, bCutCamera ? Want : FMath::RInterpTo(Camera->GetActorRotation(), Want, Dt, bFollow ? 5.f : 8.f));
	Cam->SetFieldOfView(bCutCamera ? WantFov : FMath::Lerp(Cam->FieldOfView, WantFov, K));
	bCutCamera = false;
	UpdateFigures(Dt);

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
