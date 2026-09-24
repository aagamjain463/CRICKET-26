#include "SuperOverGameMode.h"
#include "SuperOverHUD.h"
#include "CricketAnimInstance.h"
#include "CricketPose.h"
#include "CricketStadium.h"
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
#include "Scalability.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"

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

	Teams = DefaultSquads();
	HumanPlan.Length = 5.f;
}

TArray<FCricketTeam> ASuperOverGameMode::DefaultSquads()
{
	// Placeholder squads (fictional). Super Over: three batters and one bowler per side. Each side bowls its best
	// death bowler, as real sides do; CRICKET26.AI.DefaultSquadsScoreLikeASuperOver holds the match these make.
	FCricketTeam Home;
	Home.Name = TEXT("Home XI");
	Home.Short = TEXT("HOM");
	Home.Colour = FLinearColor(0.05f, 0.2f, 0.75f);
	Home.Batters = { MakePlayer(TEXT("Opener"), ECricketHand::Right, 0.7f, 0.6f),
		MakePlayer(TEXT("Finisher"), ECricketHand::Left, 0.65f, 0.8f), MakePlayer(TEXT("Allrounder"), ECricketHand::Right, 0.55f, 0.65f) };
	Home.Bowler = MakePlayer(TEXT("Quick"), ECricketHand::Right, 0.3f, 0.3f);
	Home.Bowler.PaceKph = 142.f;
	Home.Bowler.Accuracy = 0.9f;
	Home.Bowler.Movement = 0.8f;

	FCricketTeam Away;
	Away.Name = TEXT("Away XI");
	Away.Short = TEXT("AWY");
	Away.Colour = FLinearColor(0.6f, 0.08f, 0.1f);
	Away.Batters = { MakePlayer(TEXT("Hitter"), ECricketHand::Left, 0.6f, 0.8f),
		MakePlayer(TEXT("Anchor"), ECricketHand::Right, 0.75f, 0.5f), MakePlayer(TEXT("Keeper-bat"), ECricketHand::Right, 0.6f, 0.65f) };
	Away.Bowler = MakePlayer(TEXT("Wrist spinner"), ECricketHand::Right, 0.3f, 0.3f);
	Away.Bowler.BowlerType = EBowlerType::LegSpin;
	Away.Bowler.PaceKph = 86.f;
	Away.Bowler.Accuracy = 0.9f;
	Away.Bowler.Movement = 0.8f;
	return { Home, Away };
}

void ASuperOverGameMode::StartPlay()
{
	Super::StartPlay();
	Rng.Initialize(MatchSeed);
	bTouchScript = FParse::Param(FCommandLine::Get(), TEXT("CricketTouchScript")); // a human side, played by injected touches
	bAutoPlay = FParse::Param(FCommandLine::Get(), TEXT("CricketAutoPlay")) && !bTouchScript; // soak/smoke runs
	FParse::Value(FCommandLine::Get(), TEXT("CricketShotBall="), ShotBall);
	QuitAfter = ShotBall;
	FParse::Value(FCommandLine::Get(), TEXT("CricketShotEvery="), ShotEvery);
	// -CricketDevCam=X,Y,Z,LookX,LookY,LookZ,Fov (simulation metres): a fixed camera for inspecting bodies;
	// -CricketDevCam=fielder: 7 m from whoever fields the ball, on the pitch side; or a named review shot of the
	// striker: face (head and shoulders) or kit (head to toe).
	FString Cam;
	if (FParse::Value(FCommandLine::Get(), TEXT("CricketDevCam="), Cam, false))
	{
		if (Cam == TEXT("face")) Cam = TEXT("4.5,-1.5,1.5,0.6,0,1.2,22");
		else if (Cam == TEXT("kit")) Cam = TEXT("3.5,3,1,0.9,-0.35,0.85,40");
		bDevCamFielder = Cam == TEXT("fielder");
		TArray<FString> V;
		Cam.ParseIntoArray(V, TEXT(","));
		if (V.Num() == 7) DevCam = { FCString::Atof(*V[0]), FCString::Atof(*V[1]), FCString::Atof(*V[2]), FCString::Atof(*V[3]), FCString::Atof(*V[4]), FCString::Atof(*V[5]), FCString::Atof(*V[6]) };
	}
	FParse::Value(FCommandLine::Get(), TEXT("CricketQuitAfter="), QuitAfter);
	int32 Level = int32(Difficulty);
	FParse::Value(FCommandLine::Get(), TEXT("CricketDifficulty="), Level);
	Difficulty = CricketAI::EDifficulty(FMath::Clamp(Level, 0, 3));
	FParse::Value(FCommandLine::Get(), TEXT("CricketQuality="), Quality);
	Quality = FMath::Clamp(Quality, 0, 3);
	ApplyQuality();
	bTouchUI = PLATFORM_IOS || PLATFORM_ANDROID || bTouchScript || FParse::Param(FCommandLine::Get(), TEXT("CricketTouch"));
	bRecordAudio = FParse::Param(FCommandLine::Get(), TEXT("CricketRecordAudio")) && ShotBall > 0;
	BuildScene();
	SetupAudio();
	Match.Start(HumanTeam);
	PlaceForDelivery();
}

void ASuperOverGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	// Performance summary: averages and the 99th percentile of each measure over the session.
	if (Perf.Num() > 30)
	{
		auto Stat = [&](auto Get)
		{
			TArray<float> V;
			for (const FPerfSample& S : Perf) V.Add(float(Get(S)));
			V.Sort();
			double Sum = 0.0;
			for (float X : V) Sum += X;
			return FString::Printf(TEXT("%.1f/%.1f"), Sum / V.Num(), V[V.Num() * 99 / 100]);
		};
		UE_LOG(LogCRICKET26, Display, TEXT("Perf (avg/p99 over %d frames, RHI %s): frame %s ms, game %s ms, render %s ms, GPU %s ms"),
			Perf.Num(), GDynamicRHI ? GDynamicRHI->GetName() : TEXT("?"),
			*Stat([](const FPerfSample& S) { return S.Frame; }), *Stat([](const FPerfSample& S) { return S.Game; }),
			*Stat([](const FPerfSample& S) { return S.Render; }), *Stat([](const FPerfSample& S) { return S.Gpu; }));
	}
	if (HandMiss.Num() > 0)
	{
		HandMiss.Sort();
		UE_LOG(LogCRICKET26, Display, TEXT("Pose: striker hands from bat grip median %.1f cm, p95 %.1f cm, max %.1f cm over %d samples"),
			HandMiss[HandMiss.Num() / 2], HandMiss[HandMiss.Num() * 95 / 100], HandMiss.Last(), HandMiss.Num());
	}
	Super::EndPlay(Reason);
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

void ASuperOverGameMode::UpdateTracking()
{
	constexpr int32 Delivered = 40, Projected = 20; // trail segments before and after the pad
	const bool bShow = IsReviewing();
	if (!bShow && TrackSegments.IsEmpty()) return;
	if (TrackSegments.IsEmpty())
	{
		for (int32 I = 0; I < Delivered + Projected; ++I)
			TrackSegments.Add(Spawn(CylinderMesh, FVector(0.f, 0.f, -5.f), FVector(0.07f), I < Delivered ? FLinearColor(0.85f, 0.05f, 0.05f) : FLinearColor(0.1f, 0.4f, 0.95f)));
		TrackSegments.Add(Spawn(CylinderMesh, FVector(0.f, 0.f, -5.f), FVector(0.22f, 0.22f, 0.004f), FLinearColor(0.95f, 0.95f, 0.95f))); // where it pitched
		for (AStaticMeshActor* A : TrackSegments) A->GetStaticMeshComponent()->SetCastShadow(false);
	}

	// The players and their bats are hidden from the view, so the trail is drawn on the bare pitch.
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->HiddenActors.Reset();
		if (bShow)
		{
			for (AStaticMeshActor* A : Figures) PC->HiddenActors.Add(A);
			for (const auto& B : Bodies) PC->HiddenActors.Add(B.Value->GetOwner());
			for (AStaticMeshActor* A : BatHandles) PC->HiddenActors.Add(A);
			PC->HiddenActors.Add(Bat);
			PC->HiddenActors.Add(NonStrikerBat);
		}
	}

	// The delivery as bowled up to the pad, then its projection, each drawn as the review reveals it.
	const FBallTracking& Tr = Result.Tracking;
	TArray<FVector> Before;
	for (const FVector& P : Result.BallPath)
	{
		if (P.X <= Tr.Impact.X) break;
		Before.Add(P);
	}
	Before.Add(Tr.Impact);
	auto Along = [](const TArray<FVector>& Path, float U)
	{
		const float F = FMath::Clamp(U, 0.f, 1.f) * (Path.Num() - 1);
		const int32 K = FMath::Min(int32(F), Path.Num() - 2);
		return FMath::Lerp(Path[K], Path[K + 1], F - K);
	};
	auto Lay = [&](const TArray<FVector>& Path, int32 First, int32 Count, float Shown)
	{
		for (int32 I = 0; I < Count; ++I)
		{
			AStaticMeshActor* Seg = TrackSegments[First + I];
			const float U0 = float(I) / Count, U1 = FMath::Min(float(I + 1) / Count, Shown);
			const bool bOn = bShow && Path.Num() > 1 && U1 > U0;
			Seg->SetActorHiddenInGame(!bOn);
			if (!bOn) continue;
			const FVector A = ToWorld(Along(Path, U0)), B = ToWorld(Along(Path, U1));
			Seg->SetActorLocationAndRotation(0.5f * (A + B), FRotationMatrix::MakeFromZ(B - A).Rotator());
			Seg->SetActorScale3D(FVector(0.07f, 0.07f, FMath::Max(FVector::Dist(A, B) / 100.f, 0.001f)));
		}
	};
	const float Progress = ReviewProgress(), ToPad = Progress / 0.4f, OnFromPad = (Progress - 0.4f) / 0.3f;
	Lay(Before, 0, Delivered, ToPad);
	Lay(Tr.Projected, Delivered, Projected, OnFromPad);

	AStaticMeshActor* Pitched = TrackSegments.Last();
	const float PitchU = Result.PitchTime / Result.SampleDt / FMath::Max(1, Before.Num() - 1);
	Pitched->SetActorHiddenInGame(!bShow || Result.PitchTime < 0.f || ToPad < PitchU);
	Pitched->SetActorLocation(ToWorld(FVector(Result.PitchPos.X, Result.PitchPos.Y, 0.003f)));
	if (bShow) Ball->SetActorLocation(ToWorld(ToPad < 1.f ? Along(Before, ToPad) : Along(Tr.Projected, OnFromPad))); // the ball leads the trail
}

float ASuperOverGameMode::BallDisplayScale(float DistanceM, float HorizontalFovDeg, float Aspect)
{
	const float ViewHeight = 2.f * DistanceM * FMath::Tan(FMath::DegreesToRadians(HorizontalFovDeg) * 0.5f) / Aspect;
	if (ViewHeight <= 0.f) return 1.f;
	return FMath::Clamp(MinBallScreen * ViewHeight / (2.f * CricketGeo::BallRadius), 1.f, MaxBallScale);
}

void ASuperOverGameMode::Paint(AStaticMeshActor* A, const FLinearColor& Colour)
{
	UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ShapeMaterial, A);
	M->SetVectorParameterValue(TEXT("Color"), Colour);
	A->GetStaticMeshComponent()->SetMaterial(0, M);
	// A player's kit is painted in the team colour: the mannequin's own tint, or a MetaHuman's garment (every
	// mesh but the skin: its shirt and shorts colours). The cricket kit has a shirt in the team colour, trousers in
	// a darker shade of it, and white shoes; the batting gear white pads and gloves and a helmet in the darkest shade,
	// the keeper's gear white, and the umpire's hat white.
	TArray<USkeletalMeshComponent*> Kit;
	if (USkeletalMeshComponent* Body = BodyOf(A)) Body->GetOwner()->GetComponents(Kit);
	for (USkeletalMeshComponent* Part : Kit)
	{
		if (Part->GetOwner() != A && (Part->GetFName() == TEXT("Body") || Part->GetFName() == TEXT("Face"))) continue;
		const TArray<FName> Slots = Part->GetMaterialSlotNames();
		for (int32 I = 0; I < Part->GetNumMaterials(); ++I)
		{
			if (UMaterialInstanceDynamic* Cloth = Part->CreateDynamicMaterialInstance(I))
			{
				const FName Worn = Part->GetFName();
				if ((Worn == TEXT("Kit") || Worn == TEXT("Gear") || Worn == TEXT("Keeper") || Worn == TEXT("Hat")) && Slots.IsValidIndex(I))
				{
					const FName Slot = Slots[I];
					const FLinearColor Pale(0.75f, 0.75f, 0.75f);
					Cloth->SetVectorParameterValue(TEXT("Color"), Slot == TEXT("Kit_Shoes") || Slot == TEXT("Gear_Pads") || Slot == TEXT("Gear_Gloves") || Slot == TEXT("Gear_Hat") ? Pale
						: Slot == TEXT("Kit_Trousers") ? Colour * 0.45f : Slot == TEXT("Gear_Helmet") ? Colour * 0.3f
						: Slot == TEXT("Gear_Grille") ? FLinearColor(0.1f, 0.1f, 0.1f) : Colour);
					continue;
				}
				for (const TCHAR* Name : { TEXT("Paint Tint"), TEXT("LogoTint"), TEXT("diffuse_color_1"), TEXT("diffuse_color_2"), TEXT("B_diffuse_color_1") })
					Cloth->SetVectorParameterValue(Name, Colour);
			}
		}
	}
}

void ASuperOverGameMode::AddBody(AStaticMeshActor* Marker, const TCHAR* MetaHuman)
{
	Figures.Add(Marker);
	// The marker cylinder stays the authoritative position (centred 0.9 m up); the player stands in it. In the
	// marker's unscaled space the basic cylinder is 100 cm tall about its centre, so its base (the ground,
	// whatever the marker's height scale) is at -50.
	const FVector Feet(0.f, 0.f, -50.f);
	const FRotator Facing(0.f, -90.f, 0.f);
	USkeletalMeshComponent* Body = nullptr;
	// A MetaHuman built by Scripts/metahuman/make_players.sh when there is one: its blueprint brings the face,
	// hair and kit, which follow its body. -CricketMannequin keeps everyone the mannequin, to compare against.
	static const bool bMannequin = FParse::Param(FCommandLine::Get(), TEXT("CricketMannequin"));
	const FString Path = FString::Printf(TEXT("/Game/MetaHumans/%s/BP_%s.BP_%s_C"), MetaHuman, MetaHuman, MetaHuman);
	if (UClass* Look = bMannequin ? nullptr : LoadClass<AActor>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn))
	{
		AActor* Player = GetWorld()->SpawnActor<AActor>(Look, Marker->GetActorTransform());
		Player->SetActorEnableCollision(false);
		Player->AttachToComponent(Marker->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		Player->GetRootComponent()->SetUsingAbsoluteScale(true);
		Player->SetActorRelativeTransform(FTransform(Facing, Feet));
		TArray<USkeletalMeshComponent*> Parts;
		Player->GetComponents(Parts);
		for (USkeletalMeshComponent* Part : Parts) if (Part->GetFName() == TEXT("Body")) Body = Part;
		// The cricket kit made by Scripts/metahuman/make_kit.sh, when there is one, in place of the preset T-shirt and
		// shorts, and the batting gear (pads, gloves, helmet) on the two batters. Both are skinned to this body's
		// skeleton and take its pose.
		auto Wear = [&](const TCHAR* Part)
		{
			const FString MeshPath = FString::Printf(TEXT("/Game/MetaHumans/%s/Kit/SKM_%s_%s.SKM_%s_%s"), MetaHuman, MetaHuman, Part, MetaHuman, Part);
			USkeletalMesh* Mesh = Body ? LoadObject<USkeletalMesh>(nullptr, *MeshPath, nullptr, LOAD_Quiet | LOAD_NoWarn) : nullptr;
			if (!Mesh) return false;
			UMaterialInterface* Fabric = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaHumans/Kit/M_Kit.M_Kit"), nullptr, LOAD_Quiet | LOAD_NoWarn);
			USkeletalMeshComponent* Worn = NewObject<USkeletalMeshComponent>(Player, Part);
			Worn->SetSkeletalMesh(Mesh);
			for (int32 I = 0; Fabric && I < Worn->GetNumMaterials(); ++I) Worn->SetMaterial(I, Fabric);
			Worn->SetupAttachment(Body);
			Worn->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Worn->SetLeaderPoseComponent(Body);
			Worn->RegisterComponent();
			Player->AddInstanceComponent(Worn);
			return true;
		};
		TArray<USkinnedMeshComponent*> Garment;
		Player->GetComponents(Garment);
		if (Wear(TEXT("Kit")))
		{
			for (USkinnedMeshComponent* Part : Garment) if (Part != Body && Part->GetFName() != TEXT("Face")) Part->SetVisibility(false);
			if (Marker == Striker || Marker == NonStriker) Wear(TEXT("Gear"));
			// Every field preset puts the keeper first (CricketField::Make).
			if (!Fielders.IsEmpty() && Marker == Fielders[0]) Wear(TEXT("Keeper"));
			if (Umpires.Contains(Marker)) Wear(TEXT("Hat"));
		}
	}
	else if (BodyMesh)
	{
		Body = NewObject<USkeletalMeshComponent>(Marker);
		Body->SetSkeletalMesh(BodyMesh);
		Body->SetUsingAbsoluteScale(true);
		Body->SetupAttachment(Marker->GetRootComponent());
		Body->SetRelativeLocationAndRotation(Feet, Facing);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->RegisterComponent();
	}
	if (!Body) return;
	Bodies.Add(Marker, Body);
	Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Body->SetAnimInstanceClass(UCricketAnimInstance::StaticClass());
	// Pose the body after this game mode has set this frame's targets, so hands and bat move together.
	Body->AddTickPrerequisiteActor(this);
	// Posed and its bones refreshed even off screen: the broadcast cuts between cameras, and a body that only
	// updated when seen drew one stale frame after each cut (the striker's hands 45 cm off the bat as a replay began).
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (UCricketAnimInstance* Anim = Cast<UCricketAnimInstance>(Body->GetAnimInstance()))
	{
		Anim->Idle = IdleAnim;
		Anim->Jog = JogAnim;
	}
	Marker->GetStaticMeshComponent()->SetVisibility(false);
}

USkeletalMeshComponent* ASuperOverGameMode::BodyOf(const AActor* Figure) const
{
	const TObjectPtr<USkeletalMeshComponent>* Body = Bodies.Find(Figure);
	return Body ? Body->Get() : nullptr;
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
	FieldAudio->SetVolumeMultiplier(Volume * CricketAudio::MixGain);
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
		const bool bJumped = Vel.Size() > 1500.f; // faster than anyone runs: a reset or replay jump
		if (bJumped) { Vel = FVector::ZeroVector; S.Speed = 0.f; }
		S.Last = Pos;
		S.Speed = FMath::Lerp(S.Speed, FVector2D(Vel).Size() / 100.f, FMath::Clamp(Dt * 8.f, 0.f, 1.f));
		if (A->IsHidden()) continue;
		UCricketAnimInstance* Anim = AnimOf(A);
		const USkeletalMeshComponent* Body = BodyOf(A);
		// How far the striker's hands ended up from last frame's bat targets (the IK's reach), for the log. After a
		// jump those targets are where the body was, not where it is; UpdatePoses replaces them before the body poses.
		for (int32 H = 0; H < 2 && Anim && A == Striker && !bJumped && Anim->Pose.HandWeight[0] >= 1.f && Anim->Pose.HandWeight[1] >= 1.f; ++H)
		{
			const float Miss = FVector::Dist(Body->GetSocketLocation(H == 0 ? TEXT("hand_l") : TEXT("hand_r")), Anim->Pose.Hand[H]);
			HandMiss.Add(Miss);
			if (Miss > 15.f) UE_LOG(LogTemp, Display, TEXT("Hand miss %.0f cm: hand %d phase %d time %.2f dt %.3f"), Miss, H, int32(DPhase), PhaseTime, Dt);
		}
		if (A->GetActorUpVector().Z > 0.95f)
		{
			// The striker holds a side-on stance, chest to the off side, through the footwork of a stroke
			// until they set off for a run. Everyone turns at a human rate rather than snapping.
			const FVector Look = S.Speed > (A == Striker ? 2.f : 0.8f) ? Vel : A == Striker ? FVector(0.f, Off, 0.f) : BallAt - Pos;
			if (!FVector2D(Look).IsNearlyZero())
				A->SetActorRotation(FMath::RInterpConstantTo(A->GetActorRotation(), FRotator(0.f, Look.Rotation().Yaw, 0.f), Dt, 540.f));
		}
		if (!Anim) continue;
		// ponytail: one jog cycle time-scaled to speed (template jog ~4 m/s); a run/sprint blend when real locomotion lands.
		Anim->Pose.JogWeight = FMath::Clamp((S.Speed - 0.4f) / 0.6f, 0.f, 1.f);
		Anim->Pose.JogRate = FMath::Clamp(S.Speed / 4.f, 0.6f, 2.f);
		// Actions are set afresh each frame by UpdatePoses; everyone watches the ball.
		Anim->Pose.ClearActions();
		Anim->Pose.LookWeight = 1.f;
		Anim->Pose.LookAt = BallAt;
	}
}

void ASuperOverGameMode::CheckFigures()
{
	// Self-check, once, standing between balls: every body's feet are on the ground (the ankle bones sit a
	// few centimetres up). Scripts grep the log for the error.
	bFiguresChecked = true;
	float Worst = 10.f;
	int32 Checked = 0;
	for (AStaticMeshActor* A : Figures)
	{
		const USkeletalMeshComponent* Body = BodyOf(A);
		if (!Body || A->IsHidden() || A->GetActorUpVector().Z < 0.95f) continue;
		const float Foot = FMath::Min(Body->GetSocketLocation(TEXT("foot_l")).Z, Body->GetSocketLocation(TEXT("foot_r")).Z);
		Worst = FMath::Abs(Foot - 10.f) > FMath::Abs(Worst - 10.f) ? Foot : Worst;
		++Checked;
	}
	if (Checked > 0 && FMath::Abs(Worst - 10.f) > 12.f)
	{
		UE_LOG(LogTemp, Error, TEXT("Figure check FAILED: a body's lower ankle is %.0f cm above the ground"), Worst);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("Figure check passed: %d bodies, worst ankle height %.0f cm"), Checked, Worst);
	}
}

UCricketAnimInstance* ASuperOverGameMode::AnimOf(AActor* Figure) const
{
	const USkeletalMeshComponent* Body = BodyOf(Figure);
	return Body ? Cast<UCricketAnimInstance>(Body->GetAnimInstance()) : nullptr;
}

float ASuperOverGameMode::TimeToRelease(float T, bool bLive) const
{
	if (bLive) return T;
	if (DPhase != EDeliveryPhase::RunUp) return -100.f;
	// The AI's release is known; a human's is assumed ideal until they press (the arm waits at the top).
	const float At = 0.5f * ((HumanBowls() ? IdealRelease : ReleaseTiming) + 1.f) * RunUpSeconds;
	return FMath::Min(PhaseTime - At, -0.02f);
}

float ASuperOverGameMode::RunUpX(float Time) const
{
	using namespace CricketGeo;
	const float Ideal = 0.5f * (IdealRelease + 1.f) * RunUpSeconds;
	// ponytail: constant run-up speed; an accelerating approach when the bowler animation has strides to match.
	return FMath::Max(PitchLength - 1.f + RunUpLength * (1.f - Time / Ideal), PitchLength - 2.2f);
}

ADirectionalLight* ASuperOverGameMode::SpawnSun(UWorld* W)
{
	// The sun shines from the bowler's end, over the delivery camera's shoulder, so the players it frames face
	// the light (from the striker's end it lit only their backs and left every face black). It is made Movable
	// before it registers, as there is no baked lighting.
	ADirectionalLight* Sun = W->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform::Identity);
	Sun->GetComponent()->SetMobility(EComponentMobility::Movable);
	Sun->GetComponent()->SetAtmosphereSunLight(true);
	Sun->GetComponent()->SetIntensity(SunLux);
	Sun->FinishSpawning(FTransform::Identity);
	// Set after spawning: the light's component carries a -46 degree pitch of its own, which a spawn
	// rotation is added to (it put the sun at -88 degrees, straight overhead, so nothing cast a visible shadow).
	Sun->SetActorRotation(SunRotation);
	return Sun;
}

void ASuperOverGameMode::BuildScene()
{
	using namespace CricketGeo;
	UWorld* W = GetWorld();

	SpawnSun(W);
	// The sky light is made Movable before it registers: there is no baked capture, and a real-time one needs it.
	ASkyLight* Sky = W->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity);
	Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sky->GetLightComponent()->bRealTimeCapture = true;
	Sky->FinishSpawning(FTransform::Identity);
	W->SpawnActor<ASkyAtmosphere>();
	// The mobile renderer draws no sky from the atmosphere alone; give it Epic's sky dome (UE EULA).
	if (W->GetFeatureLevel() < ERHIFeatureLevel::SM5)
	{
		UStaticMesh* Dome = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EngineSky/SM_SkySphere.SM_SkySphere"));
		UMaterialInterface* DomeMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/M_SimpleSkyDome.M_SimpleSkyDome"));
		if (Dome && DomeMat)
		{
			AStaticMeshActor* SkyDome = W->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
			SkyDome->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			SkyDome->GetStaticMeshComponent()->SetStaticMesh(Dome);
			SkyDome->GetStaticMeshComponent()->SetMaterial(0, DomeMat);
			SkyDome->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			SkyDome->GetStaticMeshComponent()->SetCastShadow(false);
			SkyDome->SetActorScale3D(FVector(400.f));
		}
	}
	// Daylight at real-world levels seen through a fixed sunny-day exposure, so the sky's fill, the atmosphere and
	// Lumen's bounce keep their natural balance with the sun (an 8 lux sun under a dimmed exposure left every
	// shadow black). The exposure is set as compensation, which phones honour too (they ignore the physical camera).
	APostProcessVolume* PP = W->SpawnActor<APostProcessVolume>();
	PP->bUnbound = true;
	PP->Settings.bOverride_AutoExposureMethod = true;
	PP->Settings.AutoExposureMethod = AEM_Manual;
	PP->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP->Settings.AutoExposureApplyPhysicalCameraExposure = false;
	PP->Settings.bOverride_AutoExposureBias = true;
	PP->Settings.AutoExposureBias = -ExposureEV100;
	// A soft broadcast glow on sunlit whites (ball, kit, sightscreen) and contact shadow under the players where Lumen is
	// off (below High), which otherwise leaves them floating on the grass.
	PP->Settings.bOverride_BloomIntensity = true;
	PP->Settings.BloomIntensity = 0.3f;
	PP->Settings.bOverride_AmbientOcclusionIntensity = true;
	PP->Settings.AmbientOcclusionIntensity = 0.5f;

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
	BuildStadium();
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

	Ball = Spawn(SphereMesh, FVector(0.f, 0.f, -5.f), FVector(2.f * BallRadius), FLinearColor(0.85f, 0.85f, 0.8f));
	// Bats: a blade and a taped handle, the two parts placed together each frame. The willow bat made by
	// Scripts/metahuman/make_kit.sh, when there is one, is the whole bat in one mesh (grain, grip and stickers),
	// with its origin at the middle of the blade like the box's, and the handle part is hidden.
	UStaticMesh* BatMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/MetaHumans/Kit/SM_Bat.SM_Bat"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UMaterialInterface* Willow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaHumans/Kit/M_Bat.M_Bat"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	for (TObjectPtr<AStaticMeshActor>* B : { &Bat, &NonStrikerBat })
	{
		*B = Spawn(CubeMesh, FVector::ZeroVector, FVector(0.108f, 0.045f, CricketPose::BatLength - CricketPose::HandleLength), Wood);
		BatHandles.Add(Spawn(CylinderMesh, FVector::ZeroVector, FVector(0.034f, 0.034f, CricketPose::HandleLength), FLinearColor(0.08f, 0.08f, 0.1f)));
		if (!BatMesh || !Willow) continue;
		UStaticMeshComponent* Blade = (*B)->GetStaticMeshComponent();
		Blade->SetStaticMesh(BatMesh);
		(*B)->SetActorScale3D(FVector::OneVector);
		const TArray<FName> Slots = Blade->GetMaterialSlotNames();
		for (int32 I = 0; I < Slots.Num(); ++I)
		{
			if (Slots[I] == TEXT("Bat_Willow")) { Blade->SetMaterial(I, Willow); continue; }
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ShapeMaterial, *B);
			M->SetVectorParameterValue(TEXT("Color"), Slots[I] == TEXT("Bat_Grip") ? FLinearColor(0.03f, 0.03f, 0.035f)
				: Slots[I] == TEXT("Bat_Letters") ? FLinearColor(0.45f, 0.02f, 0.03f) : FLinearColor(0.8f, 0.8f, 0.78f));
			Blade->SetMaterial(I, M);
		}
		BatHandles.Last()->SetActorHiddenInGame(true);
	}
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
	// The same bodies play for both sides (their kit changes colour with the innings); fielders reuse the faces.
	static const TCHAR* Players[] = { TEXT("MH_Home_Opener"), TEXT("MH_Away_Hitter"), TEXT("MH_Home_Quick"), TEXT("MH_Away_Anchor"),
		TEXT("MH_Home_Finisher"), TEXT("MH_Away_KeeperBat"), TEXT("MH_Home_Allrounder"), TEXT("MH_Away_WristSpinner") };
	AddBody(Striker, Players[0]);
	AddBody(NonStriker, Players[1]);
	AddBody(Bowler, Players[2]);
	AddBody(Umpires[0], TEXT("MH_Umpire_1"));
	AddBody(Umpires[1], TEXT("MH_Umpire_2"));
	for (int32 I = 0; I < Fielders.Num(); ++I) AddBody(Fielders[I], Players[(I + 3) % UE_ARRAY_COUNT(Players)]);

	Camera = W->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
}

void ASuperOverGameMode::ApplyQuality()
{
	Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
	Levels.SetFromSingleQualityLevel(Quality);
	Scalability::SetQualityLevels(Levels);
	// Engine Medium still runs Lumen at reduced quality; below High use the sky light and screen-space
	// reflections instead, which costs a third of the GPU time and looks nearly the same in daylight.
	const bool bLumen = Quality >= 2;
	IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod"))->Set(bLumen ? 1 : 0, ECVF_SetByCode);
	IConsoleManager::Get().FindConsoleVariable(TEXT("r.ReflectionMethod"))->Set(bLumen ? 1 : 2, ECVF_SetByCode);
	// TSR costs about 6 ms a frame at native resolution on an M-series Mac (60 fps missed); TAA costs about 2.
	IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->Set(Quality >= 3 ? 4 : 2, ECVF_SetByCode);
	UE_LOG(LogCRICKET26, Display, TEXT("Quality %d"), Quality);
}

void ASuperOverGameMode::BuildStadium()
{
	// A lit material that takes its base colour from the vertex colours (the modeling plugin's; Epic, UE EULA).
	VertexColourMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/MeshModelingToolset/Materials/M_DynamicMeshComponentVtxColor.M_DynamicMeshComponentVtxColor"));
	if (!VertexColourMaterial) return;
	CricketStadium::FStadiumSpec Spec;
	Spec.CrowdDensity = Quality == 0 ? 0.35f : Quality == 1 ? 0.5f : 0.7f;
	Spec.Home = Teams[0].Colour;
	Spec.Away = Teams[1].Colour;
	const CricketStadium::FStadium Stadium = CricketStadium::Build(Spec);
	auto Place = [&](const CricketStadium::FColouredMesh& Mesh, float Z, UMaterialInterface* Material)
	{
		AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(FVector(0.f, 0.f, Z), FRotator::ZeroRotator);
		UStaticMeshComponent* C = A->GetStaticMeshComponent();
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(CricketStadium::ToStaticMesh(A, Mesh, Material));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return A;
	};
	Place(Stadium.Structure, 0.f, VertexColourMaterial);
	// The grass keeps the shape material: the vertex colour material's sheen washes out a flat field seen
	// at a grazing angle.
	for (int32 I = 0; I < 2; ++I) Paint(Place(Stadium.Outfield[I], -0.8f, ShapeMaterial), CricketStadium::StripeColours[I]); // under the strip's top and the creases
	for (const CricketStadium::FColouredMesh& Section : Stadium.Crowd)
	{
		AStaticMeshActor* A = Place(Section, 0.f, VertexColourMaterial);
		A->GetStaticMeshComponent()->SetCastShadow(false); // ponytail: the stand's own shadow reads fine; a crowd's is thousands of tiny casters
		CrowdSections.Add(A);
	}
	int32 Tris = Stadium.Structure.NumTriangles() + Stadium.Outfield[0].NumTriangles() + Stadium.Outfield[1].NumTriangles();
	for (const CricketStadium::FColouredMesh& Section : Stadium.Crowd) Tris += Section.NumTriangles();
	UE_LOG(LogCRICKET26, Display, TEXT("Stadium: %d spectators, %d triangles, %d meshes"), Stadium.Spectators, Tris, 3 + Stadium.Crowd.Num());
}

void ASuperOverGameMode::UpdateCrowd()
{
	// The crowd gets up and jumps as the noise swells on a boundary or wicket, and sits back down with it.
	const float Excitement = FMath::Clamp((CrowdLevel - 0.4f) / 0.4f, 0.f, 1.f);
	const float T = GetWorld()->GetTimeSeconds();
	for (int32 I = 0; I < CrowdSections.Num(); ++I)
	{
		const float Z = CricketStadium::JumpHeight(T, Excitement, I / CricketStadium::NumGroups, I % CricketStadium::NumGroups);
		CrowdSections[I]->SetActorLocation(FVector(0.f, 0.f, Z * 100.f));
	}
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
	Bowler->SetActorLocation(ToWorld(FVector(RunUpX(0.f), 0.5f * Arm, 0.925f)));
	// Umpires: behind the bowler's stumps on the side away from the bowling arm, and at square leg.
	Umpires[0]->SetActorLocation(ToWorld(FVector(CricketGeo::PitchLength + 1.8f, -0.9f * Arm, 0.9f)));
	Umpires[1]->SetActorLocation(ToWorld(FVector(0.5f, -26.f * Off, 0.9f)));
	for (AStaticMeshActor* U : Umpires) Paint(U, FLinearColor(0.05f, 0.05f, 0.06f)); // umpires in black: white picked up the grass green
	Paint(Striker, BatCol);
	Paint(NonStriker, BatCol);
	Paint(Bowler, FieldCol);
	for (int32 I = 0; I < Fielders.Num(); ++I)
	{
		const bool bUsed = Ctx.Field.IsValidIndex(I) && !Ctx.Field[I].bBowler;
		Fielders[I]->SetActorHiddenInGame(!bUsed);
		if (USkeletalMeshComponent* Body = BodyOf(Fielders[I]); Body && Body->GetOwner() != Fielders[I]) Body->GetOwner()->SetActorHiddenInGame(!bUsed);
		if (!bUsed) continue;
		Fielders[I]->SetActorLocation(ToWorld(FVector(Ctx.Field[I].Home.X, Ctx.Field[I].Home.Y, 0.9f)));
		Paint(Fielders[I], Ctx.Field[I].bKeeper ? FieldCol * 0.5f : FieldCol);
	}
	Ball->SetActorLocation(ToWorld(FVector(RunUpX(0.f), 0.5f * Arm, 1.2f)));
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
	if (PC)
	{
		int32 VX = 0, VY = 0;
		PC->GetViewportSize(VX, VY);
		if (VY > 0) ViewAspect = float(VX) / VY;
	}
	if (PC && bTouchScript) RunTouchScript(PC);
	if (PC) HandleInput(PC, Dt);
	PhaseTime += Dt;
	if (GetWorld()->GetTimeSeconds() > 3.f) // after start-up hitches
		Perf.Add({ float(FApp::GetDeltaTime() * 1000.0), float(FPlatformTime::ToMilliseconds(GGameThreadTime)), float(FPlatformTime::ToMilliseconds(GRenderThreadTime)),
			float(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles())) });
	if (!bFiguresChecked && GetWorld()->GetTimeSeconds() > 2.f) CheckFigures();
	// Dev capture of the game view alone, 5 times a second (the desktop is never recorded).
	const int32 LiveBall = DPhase == EDeliveryPhase::DeadBall && !bAwaitingReview && !bAwaitingThirdUmpire ? BallsPlayed : BallsPlayed + 1; // dead ball: the one just finished
	if (ShotBall == LiveBall && (ShotClock += Dt) >= ShotEvery) // from the wait before the ball, which shows the player cards
	{
		ShotClock = 0.f;
		static int32 Shot = 0;
		FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / FString::Printf(TEXT("Ball%d_%03d.png"), ShotBall, Shot++), true, false);
		// -CricketRecordAudio: also write the mixed game audio of the delivery to Saved/BallN.wav.
		if (bRecordAudio && !bRecording) { UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 30.f); bRecording = true; }
	}
	if (QuitAfter > 0 && BallsPlayed >= QuitAfter && DPhase == EDeliveryPhase::Waiting)
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
		if (bAwaitingReview)
		{
			if (!HumanReviews() && PhaseTime > 1.5f) SettleReview(AiReviews());
			else if (PhaseTime > ReviewWindow) SettleReview(false);
			break;
		}
		if (bAwaitingThirdUmpire)
		{
			if (PhaseTime > ThirdUmpireTime)
			{
				bAwaitingThirdUmpire = false;
				bReferredThis = true;
				ScoreDelivery(PendingOutcome);
			}
			break;
		}
		if (PhaseTime > (bReviewThis ? ReviewFrom() + ReviewTime + 0.5f : 1.8f + (bReplayThis ? ReplayTime : 0.f)))
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
	if (Pressed(EKeys::F7))
	{
		Quality = (Quality + 1) % 4;
		ApplyQuality();
	}
	if (Pressed(EKeys::F8)) bAutoPlay = !bAutoPlay;
	if (Pressed(EKeys::F9)) bTimingFeedback = !bTimingFeedback;
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

	if (HumanReviews() && (Pressed(EKeys::V) || C.bProgress)) SettleReview(Pressed(EKeys::V));
	else if (C.bProgress && IsReplaying()) PhaseTime = ReplayDelay + ReplayTime; // skip the replay
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
	// ponytail: a touch player can only accept an umpire's decision; a review button needs its own touch mode.
	if (HumanReviews() || IsReplaying() || (DPhase == EDeliveryPhase::Waiting && Match.Phase != EMatchPhase::ReadyForDelivery)) return EMode::Progress;
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
	// -CricketRunMargin=S: the AI batters run with this margin (negative: suicidal), to inspect run outs and the third umpire.
	FParse::Value(FCommandLine::Get(), TEXT("CricketRunMargin="), Ctx.RunMargin);
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
	// -CricketAiLeaves: the AI batter lets every ball go (to inspect pad impacts and their ball tracking).
	static const bool bAiLeaves = FParse::Param(FCommandLine::Get(), TEXT("CricketAiLeaves"));
	if (!HumanBats() && !bAiLeaves)
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
	// Nothing of the last ball's presentation carries into this one's appeal or referral.
	bReplayThis = bWicketThis = bReviewThis = bReferredThis = false;
	// An LBW appeal: the umpire decides, and the ball is dead once given out, so no leg byes either way.
	bAwaitingReview = bReviewTaken = false;
	const FBallTracking& Tr = Result.Tracking;
	if (Result.bPadImpact && Tr.Projected.Num() > 1 && !Outcome.bNoBall && !Outcome.bWide && !Match.bFreeHit
		&& (Outcome.Dismissal == EDismissal::None || Outcome.Dismissal == EDismissal::LBW))
	{
		bOnFieldOut = CricketUmpire::GivesLBW(Tr, Result.PitchTime >= 0.f, Ctx.Seed);
		if (bOnFieldOut || Outcome.Dismissal == EDismissal::LBW)
		{
			Outcome.RunsRun = Outcome.Boundary = 0;
			Outcome.bLegBye = Outcome.bOverthrow = false;
		}
		Outcome.Dismissal = bOnFieldOut ? EDismissal::LBW : EDismissal::None;
		ReviewingTeam = bOnFieldOut ? Match.BattingTeam() : Match.BowlingTeam();
		UE_LOG(LogCRICKET26, Display, TEXT("LBW appeal: umpire says %s, tracking %s; %s have %d reviews"), bOnFieldOut ? TEXT("out") : TEXT("not out"),
			Result.Dismissal == EDismissal::LBW ? TEXT("out") : TEXT("not out"), *Teams[ReviewingTeam].Short, Match.ReviewsLeft[ReviewingTeam]);
		if (Match.ReviewsLeft[ReviewingTeam] > 0)
		{
			PendingOutcome = Outcome;
			bAwaitingReview = true;
			DPhase = EDeliveryPhase::DeadBall;
			PhaseTime = 0.f;
			return;
		}
	}
	if (CricketUmpire::RefersToThirdUmpire(Result) && Result.BallPath.Num() > 1
		&& (Outcome.Dismissal == EDismissal::None || Outcome.Dismissal == EDismissal::RunOut || Outcome.Dismissal == EDismissal::Stumped))
	{
		UE_LOG(LogCRICKET26, Display, TEXT("Referred to the third umpire: home %.2f s before the stumps were broken"), Result.HomeMargin);
		PendingOutcome = Outcome;
		bAwaitingThirdUmpire = true;
		DPhase = EDeliveryPhase::DeadBall;
		PhaseTime = 0.f;
		return;
	}
	ScoreDelivery(Outcome);
}

bool ASuperOverGameMode::AiReviews() const
{
	// The side feels how the ball struck: it reviews most decisions ball tracking will overturn (more often the
	// better it is), and a fair one now and then.
	FRandomStream Hunch(Ctx.Seed * 31 + 5);
	const bool bWrong = (Result.Dismissal == EDismissal::LBW) != bOnFieldOut;
	return Hunch.FRand() < (bWrong ? 0.3f + 0.6f * AiSkill() : 0.1f);
}

void ASuperOverGameMode::SettleReview(bool bReview)
{
	bAwaitingReview = false;
	bReviewTaken = bReview;
	FDeliveryOutcome Outcome = PendingOutcome;
	if (bReview)
	{
		ReviewResult = CricketUmpire::Review(bOnFieldOut, Result.Tracking);
		if (ReviewResult == CricketUmpire::EReview::Overturned) Outcome.Dismissal = bOnFieldOut ? EDismissal::None : EDismissal::LBW;
		if (ReviewResult == CricketUmpire::EReview::Upheld) --Match.ReviewsLeft[ReviewingTeam];
		UE_LOG(LogCRICKET26, Display, TEXT("Review by %s: on-field %s, %s"), *Teams[ReviewingTeam].Short, bOnFieldOut ? TEXT("out") : TEXT("not out"),
			ReviewResult == CricketUmpire::EReview::Overturned ? TEXT("overturned") : ReviewResult == CricketUmpire::EReview::UmpiresCall ? TEXT("umpire's call") : TEXT("upheld"));
	}
	ScoreDelivery(Outcome);
}

void ASuperOverGameMode::ScoreDelivery(FDeliveryOutcome Outcome)
{
	Result.Dismissal = Outcome.Dismissal; // the decision that stood, for the HUD and replays
	const CricketCommentary::FNames Names{ StrikerPlayer().Name, Teams[Match.BattingTeam()].Batters[Match.Cur().NonStriker].Name, Teams[Match.BattingTeam()].Name };
	const float OffSign = OffSideSign(StrikerPlayer().BatHand);
	FBallMark& Mark = Marks.AddDefaulted_GetRef();
	Mark.SuperOver = Match.SuperOverNumber;
	Mark.Innings = Match.Innings.Num() - 1;
	Mark.bPitched = Result.PitchTime >= 0.f;
	Mark.Pitch = FVector2D(Result.PitchPos);
	Mark.bHit = Outcome.bBatContact;
	const FFieldingOutcome& Fld = Result.Fielding;
	Mark.End = FVector2D(Fld.Boundary > 0 ? Result.BallAt(Result.ContactTime + Fld.BoundaryTime) : Fld.Fielder >= 0 ? Fld.FieldPos : Result.BallAt(Result.DeadTime));
	Mark.Runs = Outcome.Boundary + Outcome.RunsRun;
	Mark.bWicket = Outcome.Dismissal != EDismissal::None;
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
	if (bReviewTaken && ReviewResult == CricketUmpire::EReview::Overturned) Commentary = TEXT("Overturned on review! ") + Commentary;
	if (bReferredThis) Commentary = (Outcome.Dismissal == EDismissal::None ? TEXT("Not out, says the third umpire. ") : TEXT("Given out by the third umpire. ")) + Commentary;
	UE_LOG(LogCRICKET26, Display, TEXT("Commentary: %s"), *Commentary);
	++BallsPlayed;
	UE_LOG(LogCRICKET26, Display, TEXT("%s %d/%d (%d.%d): %s"), *Teams[Match.BattingTeam()].Short, Match.Cur().Runs,
		Match.Cur().Wickets, Match.Cur().LegalBalls / 6, Match.Cur().LegalBalls % 6, *LastSummary);
	Emit(Events);
	bReplayThis = (Events.Contains(ECricketEvent::Wicket) || Events.Contains(ECricketEvent::BoundaryFour) || Events.Contains(ECricketEvent::BoundarySix))
		&& Result.BallPath.Num() > 1 && !bReferredThis; // the third umpire's frames were the replay
	bWicketThis = Events.Contains(ECricketEvent::Wicket);
	bReviewThis = Result.bPadImpact && Result.Tracking.Projected.Num() > 1 && Result.BallPath.Num() > 1;
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

void ASuperOverGameMode::UpdatePoses(float T, bool bLive, float Post, float Off, float Arm)
{
	using namespace CricketGeo;
	using namespace CricketPose;
	// The mannequin's shoulders are at CricketGeo::ShoulderHeight, this far either side of the spine; the
	// marker actors the bodies stand in are centred MarkerHeight up.
	constexpr float ShoulderHalfWidth = 0.18f, MarkerHeight = 0.9f;
	auto Ramp = [](float X, float A, float B) { return FMath::SmoothStep(A, B, X); };
	auto SimAt = [](const AActor* A) { return A->GetActorLocation() / 100.f; };
	const float Ttr = TimeToRelease(T, bLive);

	// Hands on a bat: the wrists just off the handle toward the chest, the top hand above the bottom; the
	// top elbow leads toward the bowler and the bottom one tucks down.
	auto Hold = [](FCricketBodyPose& P, const FBat& B, const FVector& Chest, int32 TopHand, float TopWeight, float BottomWeight)
	{
		for (int32 H = 0; H < 2; ++H)
		{
			const bool bTop = H == TopHand;
			const FVector OnHandle = B.Grip + B.Axis * (bTop ? -0.05f : 0.05f);
			P.Hand[H] = ToWorld(OnHandle + (Chest - OnHandle).GetSafeNormal() * 0.06f);
			P.HandWeight[H] = bTop ? TopWeight : BottomWeight;
			P.Elbow[H] = ToWorld(Chest + (bTop ? FVector(0.6f, 0.f, 0.1f) : FVector(-0.3f, 0.f, -0.6f)));
		}
	};
	auto PlaceBat = [this](AActor* Blade, const FBat& B)
	{
		const FRotator R = FRotationMatrix::MakeFromZY(B.Axis, B.Face).Rotator();
		Blade->SetActorLocationAndRotation(ToWorld(B.Top() + B.Axis * (HandleLength + 0.5f * (BatLength - HandleLength))), R);
		BatHandles[Blade == Bat ? 0 : 1]->SetActorLocationAndRotation(ToWorld(B.Top() + B.Axis * (0.5f * HandleLength)), R);
	};
	// A bat carried in the bottom hand, angled down ahead: running, or waiting at the non-striker's end.
	auto Carry = [](const AActor* Who, float Side)
	{
		const FVector F = Who->GetActorForwardVector(), R = Who->GetActorRightVector();
		FBat B;
		B.Grip = Who->GetActorLocation() / 100.f + F * 0.15f + R * (0.25f * Side) + FVector(0.f, 0.f, 0.05f);
		B.Axis = (F * 0.6f - FVector::UpVector * 0.8f).GetSafeNormal();
		B.Face = R;
		return B;
	};
	const FRunningOutcome& Run = Result.Running;
	const float RunW = bLive && Run.Attempted > 0 ? Ramp(Post, 0.35f, 0.6f) : 0.f;

	// Striker: stance, backlift as the bowler gathers, then the stroke, built backwards from where the
	// simulation put the bat so the sweet spot is on that point at the moment of contact.
	{
		const int32 TopHand = Off > 0.f ? 0 : 1; // a right-hander's top hand is the left
		const FVector At = SimAt(Striker);
		const FVector Feet(At.X, At.Y, 0.f);
		const EShotType Shot = Result.Shot.Shot;
		const bool bPlayed = bLive && BatInput.IsShot();
		const bool bStroke = bPlayed && Shot != EShotType::Leave;
		const float Press = BatInput.PressTime;
		const bool bKneel = bStroke && (Shot == EShotType::Sweep || Shot == EShotType::ReverseSweep || Shot == EShotType::SlogSweep);
		const float Back = 1.f - Ramp(Post, 0.9f, 1.5f); // settles back into the stance after the ball
		const float Crouch = 0.08f + (bKneel ? 0.35f * Ramp(T, Press, Press + 0.15f) * Back : 0.f);
		const FVector Pivot = Feet + FVector(0.f, 0.f, ShoulderHeight - Crouch);
		// Stance: bat grounded by the back toe, face to the bowler.
		const FSwing Stance = PlanSwing(EShotType::Defend, Pivot, Feet + FVector(0.2f, 0.4f * Off, 0.18f), FVector::ForwardVector);
		const float Lift = DPhase == EDeliveryPhase::RunUp ? Ramp(Ttr, -0.6f, -0.1f) : bLive ? Back : 0.f;
		FBat B = BatAt(Stance, -60.f * Lift);
		FVector Lean = Stance.Lean;
		float Open = 0.f, Into = 0.f;
		if (bPlayed && !bStroke) B = BatAt(Stance, -FMath::Max(60.f * Lift, 125.f * Ramp(T, Press, Press + 0.25f) * Back)); // leave: bat up
		if (bStroke)
		{
			const FVector Aim = !Result.Contact.BatPos.IsZero() ? Result.Contact.BatPos : FVector(Result.Shot.ContactX(), 0.f, 0.5f);
			FVector Dir = CricketBatting::DirectionToWorld(BatInput.DirectionDeg, StrikerPlayer().BatHand);
			Dir.Z = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Result.Shot.LoftDeg, 0.f, 45.f)));
			// A hit meets the ball when the simulation says; a miss swings through the aim when the timing says.
			const float Impact = Result.Contact.HasContact() ? Result.ContactTime : Press + Result.Shot.SwingTime;
			const FSwing Stroke = PlanSwing(Shot, Pivot, Aim, Dir);
			Into = Ramp(T, Press, Press + FMath::Max(0.08f, 0.5f * (Impact - Press))) * Back;
			B = Blend(B, BatAt(Stroke, StrokeAngle(Stroke, T, Press, Impact)), Into);
			Lean = FMath::Lerp(Stance.Lean, Stroke.Lean, Into);
			Open = Into * Ramp(T, Impact - 0.15f, Impact + FollowSeconds);
		}
		B = Blend(B, Carry(Striker, Off), RunW);
		PlaceBat(Bat, B);
		if (UCricketAnimInstance* Anim = AnimOf(Striker))
		{
			FCricketBodyPose& P = Anim->Pose;
			Hold(P, B, Pivot + Lean, TopHand, 1.f - RunW, 1.f);
			P.PelvisOffset = ToWorld(Lean * 0.8f - FVector(0.f, 0.f, Crouch)) * (1.f - RunW);
			// Side-on in the stance (a little open to watch the bowler), the chest opening toward the ball's
			// direction through the stroke.
			const FVector Stand(0.35f, Off, 0.f), Through(1.f, 0.3f * Off, 0.f);
			P.ChestFacing = FMath::Lerp(FMath::Lerp(Stand, Through, Open), Striker->GetActorForwardVector(), RunW);
			P.ChestBend = (12.f + 18.f * Into) * (1.f - RunW);
		}
	}

	// Non-striker: carries the bat.
	{
		const float Side = OffSideSign(Ctx.NonStriker.BatHand);
		const FBat B = Carry(NonStriker, Side);
		PlaceBat(NonStrikerBat, B);
		if (UCricketAnimInstance* Anim = AnimOf(NonStriker))
			Hold(Anim->Pose, B, SimAt(NonStriker) + FVector(0.f, 0.f, ShoulderHeight - MarkerHeight), Side > 0.f ? 0 : 1, 0.f, 1.f);
	}

	// Bowler: the arm windmills over the top to the release, the front arm pulls down through it, and the
	// chest turns from side-on to the batter and bends over the front leg.
	const float BowlW = BowlingArmWeight(Ttr);
	if (UCricketAnimInstance* Anim = AnimOf(Bowler); Anim && BowlW > 0.f)
	{
		FCricketBodyPose& P = Anim->Pose;
		const FVector Fwd(-1.f, 0.f, 0.f);
		const FVector Side(0.f, -Arm, 0.f); // toward the bowling arm: a right-armer's right, running at the striker
		const FVector Mid = SimAt(Bowler) + FVector(0.f, 0.f, ShoulderHeight - 0.925f);
		const FVector Bowling = Mid + Side * ShoulderHalfWidth, Front = Mid - Side * ShoulderHalfWidth;
		const int32 H = Arm > 0.f ? 1 : 0;
		P.Hand[H] = ToWorld(ArmCircle(Bowling, Fwd, BowlingArmAngle(Ttr), 0.8f));
		P.Elbow[H] = ToWorld(Bowling + Side);
		P.HandWeight[H] = BowlW;
		// The front arm reaches up at the batter, then pulls down and back past the hip.
		P.Hand[1 - H] = ToWorld(ArmCircle(Front, Fwd, FMath::Lerp(25.f, -150.f, Ramp(Ttr, -0.2f, 0.15f)), 0.8f));
		P.Elbow[1 - H] = ToWorld(Front - Side);
		P.HandWeight[1 - H] = BowlW;
		// Side-on at the gather (front shoulder at the batter, so the chest faces the bowling arm's side),
		// turning through square to open past the batter in the follow-through.
		const FVector Chest = FMath::Lerp(FMath::Lerp(Fwd, Side, 0.8f), Fwd - 0.6f * Side, Ramp(Ttr, -0.25f, 0.15f));
		P.ChestFacing = FMath::Lerp(Bowler->GetActorForwardVector(), Chest, BowlW);
		P.ChestBend = 30.f * BowlW * Ramp(Ttr, -0.1f, 0.15f);
	}

	// Fielders and keeper: the keeper squats until the ball is on its way; anyone the ball comes near gets
	// down to it and puts both hands where it will be a moment later; a thrower windmills the arm over.
	for (int32 I = 0; I < Ctx.Field.Num(); ++I)
	{
		const FFielder& F = Ctx.Field[I];
		AStaticMeshActor* Who = F.bBowler ? Bowler.Get() : Fielders.IsValidIndex(I) ? Fielders[I].Get() : nullptr;
		UCricketAnimInstance* Anim = Who && !Who->IsHidden() ? AnimOf(Who) : nullptr;
		if (!Anim || (F.bBowler && BowlW > 0.f)) continue;
		FCricketBodyPose& P = Anim->Pose;
		const FVector At = SimAt(Who);
		const FVector Fwd = Who->GetActorForwardVector();
		float Drop = 0.f, ReadyW = 0.f;
		if (F.bKeeper)
		{
			Drop = bLive ? FMath::Lerp(0.4f, 0.12f, Ramp(T, 0.f, Result.PitchTime > 0.f ? Result.PitchTime + 0.1f : 0.5f)) : 0.4f;
			ReadyW = 1.f;
		}
		else if (!F.bBowler && (DPhase == EDeliveryPhase::RunUp || (bLive && T < Result.ContactTime + 0.4f))) Drop = 0.08f; // walking in, ready
		const FVector Chest0 = At + FVector(0.f, 0.f, ShoulderHeight - MarkerHeight - 0.1f);
		FVector Reach = Chest0 + Fwd * 0.4f - FVector(0.f, 0.f, 0.55f); // keeper's gloves by the knees
		float Near = 0.f;
		if (bLive)
		{
			const FVector Soon = Result.BallAt(T + 0.12f);
			Near = 1.f - Ramp(FVector::Dist(Soon, Chest0), 0.8f, 1.8f);
			Drop = FMath::Max(Drop, Near * FMath::Clamp(Chest0.Z - 0.55f - Soon.Z, 0.f, 0.5f));
			const FVector Chest = Chest0 - FVector(0.f, 0.f, Drop);
			Reach = FMath::Lerp(Reach, Chest + (Soon - Chest).GetClampedToMaxSize(0.65f), Near);
		}
		const FVector Chest = Chest0 - FVector(0.f, 0.f, Drop);
		const FVector Across = FVector::CrossProduct(Reach - Chest, FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER, Who->GetActorRightVector());
		for (int32 H = 0; H < 2; ++H)
		{
			const float S = H == 0 ? 1.f : -1.f;
			P.Hand[H] = ToWorld(Reach + Across * (0.07f * S));
			P.Elbow[H] = ToWorld(Chest + Across * (0.5f * S) - FVector(0.f, 0.f, 0.5f));
			P.HandWeight[H] = FMath::Max(Near, ReadyW);
		}
		P.PelvisOffset = FVector(0.f, 0.f, -100.f * Drop);
		P.ChestBend = 45.f * Drop;

		const bool bThrower = I == Result.Fielding.Fielder && Run.ThrowRelease > 0.f;
		const bool bRelay = I == Run.RelayMove.Fielder && Run.RelayRelease > 0.f;
		const float Tt = Post - (bRelay ? Run.RelayRelease : Run.ThrowRelease);
		const float ThrowW = bLive && (bThrower || bRelay) ? BowlingArmWeight(Tt) : 0.f;
		if (ThrowW > 0.f)
		{
			const FVector Stumps(Run.bThrowToStrikerEnd ? 0.f : PitchLength, 0.f, 0.f);
			const bool bToRelay = !bRelay && Fielders.IsValidIndex(Run.RelayMove.Fielder);
			const FVector To = bToRelay ? SimAt(Fielders[Run.RelayMove.Fielder]) : Stumps;
			const FVector Aim = FVector(To.X - At.X, To.Y - At.Y, 0.f).GetSafeNormal(UE_SMALL_NUMBER, Fwd);
			const FVector Right(-Aim.Y, Aim.X, 0.f);
			const FVector Shoulder = Chest + Right * ShoulderHalfWidth;
			P.Hand[1] = ToWorld(ArmCircle(Shoulder, Aim, BowlingArmAngle(Tt), 0.8f));
			P.Elbow[1] = ToWorld(Shoulder + Right);
			P.HandWeight[1] = ThrowW;
			P.HandWeight[0] *= 1.f - ThrowW;
			P.ChestFacing = FMath::Lerp(Fwd, Aim, ThrowW);
		}
	}
}

void ASuperOverGameMode::UpdatePresentation(float Dt)
{
	using namespace CricketGeo;
	const float Arm = BowlerPlayer().BowlHand == ECricketHand::Right ? 1.f : -1.f;
	const float Off = OffSideSign(StrikerPlayer().BatHand);
	const bool bLive = DPhase == EDeliveryPhase::BallInPlay || DPhase == EDeliveryPhase::DeadBall;
	const bool bReplay = IsReplaying();
	const bool bReview = IsReviewing();
	const bool bScorecard = ShowingScorecard();
	const float T = bAwaitingThirdUmpire ? ThirdUmpireBallTime() : bReplay ? ReplayBallTime() : DPhase == EDeliveryPhase::DeadBall ? Result.DeadTime : PhaseTime;

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
	if (CrowdAudio) CrowdAudio->SetVolumeMultiplier(CrowdLevel * CricketAudio::MixGain);
	if (CrowdWave && CrowdWave->GetAvailableAudioByteCount() < CricketAudio::SampleRate * 2)
		CrowdWave->QueueAudio(reinterpret_cast<const uint8*>(CuePcm[int32(CricketAudio::ECue::Crowd)].GetData()), CuePcm[int32(CricketAudio::ECue::Crowd)].Num() * sizeof(int16));
	const float Post = T - Result.ContactTime; // seconds after contact (or after passing the batter)

	// Bowler: run-up, delivery stride, follow-through.
	// The run-up reaches the crease on an ideally timed release; an early release lets go short of it.
	FVector BowlerPos(RunUpX(DPhase == EDeliveryPhase::RunUp ? PhaseTime : 0.f), 0.5f * Arm, 0.925f);
	// Follow-through ends at the bowler's fielding mark, where the fielding solver has them.
	const int32 BowlerSlot = Ctx.Field.IndexOfByPredicate([](const FFielder& F) { return F.bBowler; });
	const FVector2D FollowThrough = BowlerSlot >= 0 ? Ctx.Field[BowlerSlot].Home : FVector2D(PitchLength - 4.f, 0.5f * Arm);
	if (bLive)
	{
		const float A = FMath::Clamp(T, 0.f, 1.f);
		const float From = RunUpX(0.5f * (ReleaseTiming + 1.f) * RunUpSeconds);
		BowlerPos = FVector(FMath::Lerp(From, FollowThrough.X, A), FMath::Lerp(0.5f * Arm, FollowThrough.Y, A), BowlerPos.Z);
	}
	Bowler->SetActorLocation(ToWorld(BowlerPos));

	TargetMarker->SetActorHiddenInGame(!(HumanBowls() && (DPhase == EDeliveryPhase::Waiting || DPhase == EDeliveryPhase::RunUp)));
	TargetMarker->SetActorLocation(ToWorld(FVector(HumanPlan.Length, HumanPlan.Line * Off, 0.003f)));

	// Ball.
	// In the bowling hand until release (last frame's hand: the arm is posed after this).
	const USkeletalMeshComponent* BowlerBody = BodyOf(Bowler);
	const FName BowlingHand = Arm > 0.f ? FName(TEXT("hand_r")) : FName(TEXT("hand_l"));
	FVector BallPos = BowlerBody ? BowlerBody->GetSocketLocation(BowlingHand) + FVector(0.f, 0.f, -8.f) : ToWorld(FVector(BowlerPos.X - 0.3f, 0.4f * Arm, 1.1f));
	if (bLive) BallPos = ToWorld(Result.BallAt(T));
	Ball->SetActorLocation(BallPos);
	// Drawn stretched along its path by the distance it covers in a 1/60 s shutter, as a broadcast camera
	// blurs it, so a fast ball reads as a streak rather than strobing dots. A jump (new ball, replay) is not motion.
	const float Size = 2.f * BallRadius * BallDisplayScale(FVector::Dist(BallPos, Camera->GetActorLocation()) / 100.f, Camera->GetCameraComponent()->FieldOfView, ViewAspect);
	const FVector BallVel = Dt > 0.f ? (BallPos - LastBallPos) / 100.f / Dt : FVector::ZeroVector;
	LastBallPos = BallPos;
	const float Streak = BallVel.Size() < 60.f ? FMath::Min(BallVel.Size() / 60.f, 6.f * Size) : 0.f;
	Ball->SetActorRotation(Streak > Size ? BallVel.Rotation() : FRotator::ZeroRotator);
	Ball->SetActorScale3D(FVector(FMath::Max(Size, Streak), Size, Size));

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
		// Each batter eases from where they stood into their own lane, either side of the pitch: the
		// non-striker's on the side they backed up (away from the bowler's arm), the striker's the other.
		const float Lane = FMath::SmoothStep(0.35f, 0.9f, Post);
		Striker->SetActorLocation(ToWorld(FVector(SX, FMath::Lerp(-0.35f * Off, 1.1f * Arm, Lane), 0.9f)));
		NonStriker->SetActorLocation(ToWorld(FVector(NX, -1.1f * Arm, 0.9f)));
	}
	else if (bLive && BatInput.IsShot())
	{
		// Footwork: the striker steps to where the stroke is played (down the track to a spinner), then back.
		const float S = FMath::Clamp((T - BatInput.PressTime) / FMath::Max(Result.Shot.SwingTime, 0.05f), 0.f, 1.f);
		// Beaten down the track, they are back in their ground when the simulation says they regained it.
		const float Home = Result.BrokenTime >= 0.f ? Result.BrokenTime - Result.HomeMargin : Result.ContactTime + 1.2f;
		const float Back = FMath::Clamp((T - Home + 0.8f) / 0.8f, 0.f, 1.f);
		const float X = FMath::Lerp(FMath::Lerp(0.9f, Result.Shot.ContactX() - 0.45f, FMath::SmoothStep(0.f, 1.f, S)), 0.9f, Back);
		Striker->SetActorLocation(ToWorld(FVector(X, -0.35f * Off, 0.9f)));
	}

	// Camera: broadcast telephoto from behind the bowler, then pull wide and follow the ball after the shot.
	const bool bFollow = bLive && !bReplay && T > Result.ContactTime + 0.15f && (Result.Contact.HasContact() || Result.Fielding.Fielder >= 0);
	// Delivery shot: a long lens high in the stand behind the bowler, framing the striker and keeper about
	// 11 m across so the batter reads large and the flight is compressed, as on a broadcast.
	FVector WantLoc = bFollow ? ToWorld(FVector(PitchLength + 32.f, 0.f, 20.f)) : ToWorld(FVector(PitchLength + 62.f, 0.f, 12.f));
	FVector LookAt = bFollow ? BallPos : ToWorld(FVector(0.5f, 0.f, 1.3f));
	float WantFov = bFollow ? 42.f : 8.f;
	// Director: cuts on the simulation's events. A camera beyond the rope watches a boundary come to it; a
	// fielder's pickup or catch is seen from in front of them; once the ball is dead, a close-up of the bowler
	// after a wicket, or of the striker otherwise, until the replay or the next ball. Any change of shot is a
	// cut, except the delivery shot pulling out to follow the ball.
	enum EShot { Delivery, Follow, Boundary, Fielding, CloseUp, Replay, SuperSlow, Review, Scorecard };
	EShot Shot = bFollow ? Follow : Delivery;
	const FFieldingOutcome& Fld = Result.Fielding;
	const float After = T - Result.ContactTime;
	if (bFollow && Fld.Boundary > 0 && After > Fld.BoundaryTime - 0.8f)
	{
		// Beyond the rope where the ball crosses it, a little to one side so it does not fly into the lens, wide
		// enough to take in the rope.
		const FVector2D Centre(PitchCentre());
		const FVector2D Out = (FVector2D(Result.BallAt(Result.ContactTime + Fld.BoundaryTime)) - Centre).GetSafeNormal();
		Shot = Boundary;
		WantLoc = ToWorld(FVector(Centre + Out * (BoundaryRadius + 6.f) + FVector2D(-Out.Y, Out.X) * 5.f, 2.5f));
		LookAt = BallPos;
		WantFov = 50.f;
	}
	else if (bFollow && Fld.Fielder > 0 && Fielders.IsValidIndex(Fld.Fielder) && After > Fld.FieldTime - 0.7f && After < Fld.FieldTime + 1.5f)
	{
		// Fielders[0] is the keeper, whose takes the delivery shot already frames.
		const FVector At = Fielders[Fld.Fielder]->GetActorLocation();
		const FVector In = FVector(FVector2D(PitchCentre() * 100.f - At), 0.f).GetSafeNormal();
		Shot = Fielding;
		WantLoc = At + In * 900.f + FVector(0.f, 0.f, 120.f);
		LookAt = At + FVector(0.f, 0.f, 40.f);
		WantFov = 30.f;
	}
	if (DPhase == EDeliveryPhase::DeadBall && PhaseTime > 0.7f)
	{
		AStaticMeshActor* Who = bWicketThis ? Bowler : Striker;
		const USkeletalMeshComponent* WhoBody = BodyOf(Who);
		const FVector Head = WhoBody ? WhoBody->GetSocketLocation(TEXT("head")) : Who->GetActorLocation() + FVector(0.f, 0.f, 70.f);
		// In front of them: the bowler faces back down the pitch, the striker toward the off side.
		const FVector Front = bWicketThis ? FVector(-1.f, 0.3f * Arm, 0.f) : FVector(0.3f, Off, 0.15f);
		Shot = CloseUp;
		WantLoc = Head + Front.GetSafeNormal() * 800.f;
		LookAt = Head - FVector(0.f, 0.f, 35.f); // the head above the banner
		WantFov = 20.f;
	}
	if (bReplay)
	{
		Shot = Replay;
		// Side-on from the off side at batter height (facing the stance, clear of the square-leg umpire):
		// the stroke, then the ball's flight on a wider lens.
		WantLoc = ToWorld(FVector(Result.Shot.ContactX() + 2.f, 38.f * Off, 2.2f));
		LookAt = T < Result.ContactTime + 0.3f ? ToWorld(FVector(Result.Shot.ContactX(), 0.f, 1.f)) : BallPos;
		WantFov = T < Result.ContactTime + 0.3f ? 12.f : 35.f;
		if (ReplayAngle() == 1)
		{
			// From the main camera's place high behind the bowler (above the bowler and umpire), in close.
			Shot = SuperSlow;
			WantLoc = ToWorld(FVector(PitchLength + 62.f, 0.f, 12.f));
			LookAt = ToWorld(FVector(Result.Shot.ContactX(), 0.f, 1.f));
			WantFov = 3.f;
		}
	}
	if (bAwaitingThirdUmpire)
	{
		// Square-on to the popping crease at the broken wicket, then from down the pitch back at it (behind the
		// stumps, the umpire and the fielder taking the ball stand in the way).
		Shot = ThirdUmpireAngle() == 0 ? Replay : SuperSlow;
		const bool bNear = Result.bBrokenAtStrikerEnd;
		const float StumpsX = bNear ? 0.f : PitchLength, Toward = bNear ? 1.f : -1.f, CreaseX = StumpsX + Toward * PoppingCrease;
		WantLoc = ToWorld(ThirdUmpireAngle() == 0 ? FVector(CreaseX, 22.f * Off, 1.2f) : FVector(CreaseX + Toward * 14.f, 0.f, 2.5f));
		LookAt = ToWorld(FVector(ThirdUmpireAngle() == 0 ? CreaseX : CreaseX - Toward * 0.5f, 0.f, 0.5f));
		WantFov = ThirdUmpireAngle() == 0 ? 14.f : 20.f;
	}
	if (bReview)
	{
		// Ball tracking: from above the bowler's stumps while the path comes down the pitch, then round to the
		// off side of the striker's stumps, to see from the pad on to them.
		Shot = Review;
		const bool bClose = ReviewProgress() > 0.4f;
		WantLoc = ToWorld(bClose ? FVector(4.5f, 2.5f * Off, 1.6f) : FVector(PitchLength + 5.f, 0.f, 3.f));
		LookAt = ToWorld(bClose ? FVector(0.6f, 0.f, 0.35f) : FVector(2.f, 0.f, 0.5f));
		WantFov = bClose ? 32.f : 24.f;
	}
	if (bScorecard)
	{
		// High in the square-leg stand, across the square to the far stands.
		Shot = Scorecard;
		WantLoc = ToWorld(FVector(0.5f * PitchLength, -80.f, 26.f));
		LookAt = ToWorld(FVector(0.5f * PitchLength, 30.f, 4.f));
		WantFov = 70.f;
	}
	if (bDevCamFielder && bLive && Fielders.IsValidIndex(Result.Fielding.Fielder) && !Ctx.Field[Result.Fielding.Fielder].bBowler)
	{
		const FVector At = Fielders[Result.Fielding.Fielder]->GetActorLocation();
		const FVector In = FVector(FVector2D(PitchCentre() * 100.f - At), 0.f).GetSafeNormal();
		WantLoc = At + In * 700.f + FVector(0.f, 0.f, 80.f);
		LookAt = At;
		WantFov = 30.f;
		bCutCamera = true;
	}
	else if (DevCam.Num() == 7)
	{
		WantLoc = ToWorld(FVector(DevCam[0], DevCam[1], DevCam[2]));
		LookAt = ToWorld(FVector(DevCam[3], DevCam[4], DevCam[5]));
		WantFov = DevCam[6];
		bCutCamera = true;
	}
	if (Shot != LastShot && !(LastShot == Delivery && Shot == Follow)) bCutCamera = true;
	LastShot = Shot;
	const float K = FMath::Clamp(Dt * 3.f, 0.f, 1.f);
	UCameraComponent* Cam = Camera->GetCameraComponent();
	const FVector Loc = bCutCamera ? WantLoc : FMath::Lerp(Camera->GetActorLocation(), WantLoc, bViewSet ? K : 1.f);
	const FRotator Want = (LookAt - Loc).Rotation();
	Camera->SetActorLocationAndRotation(Loc, bCutCamera ? Want : FMath::RInterpTo(Camera->GetActorRotation(), Want, Dt, bFollow ? 5.f : 8.f));
	Cam->SetFieldOfView(bCutCamera ? WantFov : FMath::Lerp(Cam->FieldOfView, WantFov, K));
	bCutCamera = false;
	UpdateFigures(Dt);
	UpdatePoses(T, bLive, Post, Off, Arm);
	UpdateCrowd();
	UpdateTracking();

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
