#include "SuperOverGameMode.h"
#include "SuperOverHUD.h"
#include "MatchHUDWidget.h"
#include "CricketAnimInstance.h"
#include "CricketBatter.h"
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
#include "Components/LODSyncComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#if WITH_EDITOR
#include "TextureCompiler.h"
#endif
#include "Animation/AnimSequence.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Components/AudioComponent.h"
#include "CricketCommentary.h"
#include "CricketKeeper.h"
#include "Frontend/FrontendStatics.h"
#include "Frontend/FrontendStyle.h"
#include "IPLSeason.h"
#include "IPLSeasonSave.h"
#include "IPLPending.h"
#include "IPLMatchAdapter.h"
#include "AuctionEngine.h"
#include "AuctionTypes.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "DrawDebugHelpers.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Scalability.h"
#include "Engine/SkyLight.h"
#include "Engine/SpotLight.h"
#include "Components/SpotLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"

namespace
{
	FVector ToWorld(const FVector& M) { return M * 100.f; } // simulation metres -> Unreal cm

	FCricketPlayer MakePlayer(const TCHAR* Name, ECricketHand Bat, float Timing, float Power, EBatterStyle Style = EBatterStyle::Classical)
	{
		FCricketPlayer P;
		P.Name = Name;
		P.BatHand = Bat;
		P.Timing = Timing;
		P.Technique = Timing;
		P.Power = Power;
		P.BatterStyle = Style;
		return P;
	}

	const FLinearColor Grass(0.09f, 0.28f, 0.07f), Strip(0.55f, 0.47f, 0.3f), White(0.9f, 0.9f, 0.9f), Wood(0.8f, 0.65f, 0.4f);

	struct FBoneChainKey
	{
		const USkeleton* Skeleton;
		FName Bone;
		bool operator==(const FBoneChainKey& O) const { return Skeleton == O.Skeleton && Bone == O.Bone; }
	};
	inline uint32 GetTypeHash(const FBoneChainKey& K) { return HashCombine(GetTypeHash(K.Skeleton), GetTypeHash(K.Bone)); }

	/** A bone's component-space location (cm) in a clip at a time, composed up its parents. */
	FVector ClipBoneAt(const UAnimSequence* Seq, FName Bone, float Time)
	{
		if (!Seq) return FVector::ZeroVector;
		const USkeleton* Skel = Seq->GetSkeleton();
		if (!Skel) return FVector::ZeroVector;

		static TMap<FBoneChainKey, TArray<int32>> BoneChainCache;
		const TArray<int32>* Chain = BoneChainCache.Find({ Skel, Bone });
		if (!Chain)
		{
			TArray<int32> Indices;
			const FReferenceSkeleton& Ref = Skel->GetReferenceSkeleton();
			for (int32 B = Ref.FindBoneIndex(Bone); B != INDEX_NONE; B = Ref.GetParentIndex(B))
			{
				Indices.Add(B);
			}
			Chain = &BoneChainCache.Add({ Skel, Bone }, MoveTemp(Indices));
		}

		FTransform T = FTransform::Identity;
		const FAnimExtractContext ExtractCtx(static_cast<double>(Time));
		for (int32 B : *Chain)
		{
			FTransform Local;
			Seq->GetBoneTransform(Local, FSkeletonPoseBoneIndex(B), ExtractCtx, false);
			T = T * Local;
		}
		return T.GetLocation();
	}
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
	Home.Accent = FLinearColor(1.f, 0.62f, 0.08f); // gold sleeves, collar and number trim on royal blue
	Home.Sponsor = TEXT("SAFFRON BANK");
	Home.Batters = { MakePlayer(TEXT("Opener"), ECricketHand::Right, 0.7f, 0.6f, EBatterStyle::Classical),
		MakePlayer(TEXT("Finisher"), ECricketHand::Left, 0.65f, 0.8f, EBatterStyle::PowerHitter),
		MakePlayer(TEXT("Allrounder"), ECricketHand::Right, 0.55f, 0.65f, EBatterStyle::ExpressPuller) };
	Home.Batters[0].Number = 18;
	Home.Batters[1].Number = 7;
	Home.Batters[2].Number = 45;
	Home.Bowler = MakePlayer(TEXT("Quick"), ECricketHand::Right, 0.3f, 0.3f);
	Home.Bowler.Number = 93;
	Home.Bowler.PaceKph = 142.f;
	Home.Bowler.Accuracy = 0.9f;
	Home.Bowler.Movement = 0.8f;

	FCricketTeam Away;
	Away.Name = TEXT("Away XI");
	Away.Short = TEXT("AWY");
	Away.Colour = FLinearColor(0.6f, 0.08f, 0.1f);
	Away.Accent = FLinearColor(0.95f, 0.88f, 0.7f); // cream sleeves and trim on maroon
	Away.Sponsor = TEXT("ZEPHYRA");
	Away.Batters = { MakePlayer(TEXT("Hitter"), ECricketHand::Left, 0.6f, 0.8f, EBatterStyle::PowerHitter),
		MakePlayer(TEXT("Anchor"), ECricketHand::Right, 0.75f, 0.5f, EBatterStyle::Unorthodox),
		MakePlayer(TEXT("Keeper-bat"), ECricketHand::Right, 0.6f, 0.65f, EBatterStyle::Classical) };
	Away.Batters[0].Number = 63;
	Away.Batters[1].Number = 10;
	Away.Batters[2].Number = 27;
	Away.Bowler = MakePlayer(TEXT("Wrist spinner"), ECricketHand::Right, 0.3f, 0.3f);
	Away.Bowler.Number = 29;
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
	bDebug = FParse::Param(FCommandLine::Get(), TEXT("CricketDebug")); // F1 overlay on: camera role/state/lens + replay + buffer, for captures
	FParse::Value(FCommandLine::Get(), TEXT("CricketShotBall="), ShotBall);
	QuitAfter = ShotBall;
	FParse::Value(FCommandLine::Get(), TEXT("CricketShotEvery="), ShotEvery);
	FParse::Value(FCommandLine::Get(), TEXT("CricketUIShotEvery="), UIShotEvery);
	FParse::Value(FCommandLine::Get(), TEXT("CricketSlowMo="), SlowMo);
	// -CricketDevCam=X,Y,Z,LookX,LookY,LookZ,Fov (simulation metres): a fixed camera for inspecting bodies;
	// -CricketDevCam=fielder: 7 m from whoever fields the ball (their body), on the pitch side; or a named review shot of the
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
	// -CricketDevLook=Role,Bone,Distance,Yaw,Fov (metres, degrees): follows one body's bone from Yaw degrees off its
	// facing (0 = in front), to inspect faces, hands and gear up close in match light. Role: striker, nonstriker,
	// bowler, keeper, umpire0, umpire1 or fielderN.
	FString Look;
	if (FParse::Value(FCommandLine::Get(), TEXT("CricketDevLook="), Look, false))
	{
		TArray<FString> V;
		Look.ParseIntoArray(V, TEXT(","));
		if (V.Num() == 5)
		{
			DevLookRole = V[0];
			DevLookBone = V[1];
			DevLookDist = FCString::Atof(*V[2]);
			DevLookYaw = FCString::Atof(*V[3]);
			DevLookFov = FCString::Atof(*V[4]);
		}
	}

	// -CricketDeliveryCam=Distance,Height,Lateral,Fov (metres, degrees): development-only calibration of the
	// standard delivery camera against the Cricket 24 broadcast reference. With Scripts/capture.sh, each
	// delivery's run-up/release/contact frames can be compared and the composition dialled in directly.
	FString Tune;
	if (FParse::Value(FCommandLine::Get(), TEXT("CricketDeliveryCam="), Tune, false))
	{
		TArray<FString> V;
		Tune.ParseIntoArray(V, TEXT(","));
		if (V.Num() >= 4)
		{
			BroadcastTuning.Delivery.Distance = FCString::Atof(*V[0]);
			BroadcastTuning.Delivery.Height = FCString::Atof(*V[1]);
			BroadcastTuning.Delivery.LateralOffset = FCString::Atof(*V[2]);
			BroadcastTuning.Delivery.FOV = FCString::Atof(*V[3]);
			UE_LOG(LogCRICKET26, Display, TEXT("Delivery camera calibration override: D=%.1f H=%.1f Lat=%.1f FOV=%.1f"),
				BroadcastTuning.Delivery.Distance, BroadcastTuning.Delivery.Height,
				BroadcastTuning.Delivery.LateralOffset, BroadcastTuning.Delivery.FOV);
		}
	}
	FParse::Value(FCommandLine::Get(), TEXT("CricketQuitAfter="), QuitAfter);
	int32 Level = int32(Difficulty);
	// Frontend hook (additive): match setup chosen in the menu arrives as travel URL options
	// (UFrontendSettingsSave::MatchOptions); the Cricket* command-line switches still override them.
	Level = UGameplayStatics::GetIntOption(OptionsString, TEXT("Difficulty"), Level);
	Quality = UGameplayStatics::GetIntOption(OptionsString, TEXT("Quality"), Quality);
	bTimingFeedback = UGameplayStatics::GetIntOption(OptionsString, TEXT("TimingBar"), bTimingFeedback ? 1 : 0) != 0;
	bCoaching = UGameplayStatics::GetIntOption(OptionsString, TEXT("Coaching"), 1) != 0;
	const int32 Overs = UGameplayStatics::GetIntOption(OptionsString, TEXT("Overs"), 1);
	SelectedMatchOvers = Overs == 3 || Overs == 5 || Overs == 10 || Overs == 20 ? Overs : 1;
	Match.Rules.MaxLegalBalls = SelectedMatchOvers * 6;
	Match.Rules.MaxWickets = SelectedMatchOvers == 1 ? 2 : 10;
	if (SelectedMatchOvers > 1)
	{
		// ponytail: use reserve batters until full named squads exist for longer matches.
		for (FCricketTeam& Team : Teams)
		{
			for (int32 I = Team.Batters.Num(); I < 10; ++I)
			{
				FCricketPlayer Batter = Team.Batters.Last();
				Batter.Name = FString::Printf(TEXT("Batter %d"), I + 1);
				Batter.Number = I + 1;
				Team.Batters.Add(Batter);
			}
			Team.Batters.Add(Team.Bowler);
		}
	}
	// IPL hook (additive): a staged season fixture replaces the placeholder squads with the two
	// franchises' actual auction-built XIs. Standalone matches never stage a fixture, so they
	// reach Match.Start with exactly the squads above, as before.
	SetupIPLMatch();
	for (int32 M = 0; M < UE_ARRAY_COUNT(CoachUses); ++M)
		GConfig->GetInt(CoachSection, *FString::Printf(TEXT("Mode%d"), M), CoachUses[M], GGameUserSettingsIni);
	FParse::Value(FCommandLine::Get(), TEXT("CricketDifficulty="), Level);
	Difficulty = CricketAI::EDifficulty(FMath::Clamp(Level, 0, 3));
	FParse::Value(FCommandLine::Get(), TEXT("CricketQuality="), Quality);
	bForceWicket = FParse::Param(FCommandLine::Get(), TEXT("CricketForceWicket")); // F5 from the start, for captures
	Quality = FMath::Clamp(Quality, 0, 3);
	ApplyQuality();
	bRecordAudio = FParse::Param(FCommandLine::Get(), TEXT("CricketRecordAudio")) && ShotBall > 0;
	VenueIndex = bAutoPlay ? 0 : FMath::RandRange(0, CricketStadium::NumVenues - 1);
	VenueIndex = UGameplayStatics::GetIntOption(OptionsString, TEXT("Venue"), VenueIndex);
	FParse::Value(FCommandLine::Get(), TEXT("CricketVenue="), VenueIndex);
	VenueIndex = FMath::Clamp(VenueIndex, 0, CricketStadium::NumVenues - 1);
	UE_LOG(LogCRICKET26, Display, TEXT("Venue: %s"), CricketStadium::Venue(VenueIndex).Name);
	BuildScene();
	SetupAudio();
	CommRng.Initialize(MatchSeed * 31 + 7);
	AudioDir.Reset();
	CommDir.Reset();
	// IPL: the toss (staged with the fixture) decides who bats first; standalone keeps HumanTeam.
	Match.Start(IsIPLMatch() ? (IPLBatFirst == 1 ? 1 : 0) : HumanTeam);
	PlaceForDelivery();
	// IPL: opening bowlers (or the user's opening-bowler pick when their side bowls first).
	if (IsIPLMatch()) IPLNewInningsSetup();
}

void ASuperOverGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	// The match owns four procedural channels (outer = this GameMode). OpenLevel tears the world
	// down while the mixer thread is still pulling buffers; if the waves die first the mixer
	// hits SoundGenerator.IsValid() || bProcedural and the process exits (looks like REMATCH /
	// CONTINUE "quits"). Stop and detach first, so the mixer releases its sources synchronously.
	for (UAudioComponent* Comp : { FieldAudio, CrowdAudio, VoiceAudio, VocalAudio })
	{
		if (Comp)
		{
			Comp->Stop();
			Comp->SetSound(nullptr);
			Comp->DestroyComponent();
		}
	}
	for (USoundWaveProcedural* Wave : { FieldWave, CrowdWave, VoiceWave, VocalWave })
	{
		if (Wave) Wave->ResetAudio();
	}
	FieldAudio = CrowdAudio = VoiceAudio = VocalAudio = nullptr;
	FieldWave = CrowdWave = VoiceWave = VocalWave = nullptr;
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
	if (ElbowChecks > 0) UE_LOG(LogCRICKET26, Display, TEXT("Pose: striker arm inside torso %d of %d samples"), ElbowInside, ElbowChecks);
	if (RaisedChecks > 0) UE_LOG(LogCRICKET26, Display, TEXT("Pose: striker raised-arm elbow flared out %d of %d samples"), ElbowFlared, RaisedChecks);
	if (ArmChecks > 0) UE_LOG(LogCRICKET26, Display, TEXT("Pose: striker elbow bowed up (upper arm up, forearm down) %d of %d samples"), ElbowUp, ArmChecks);
	if (FootSlide.Num() > 0)
	{
		FootSlide.Sort();
		UE_LOG(LogCRICKET26, Display, TEXT("Pose: striker planted-foot slide per frame p95 %.2f cm, max %.2f cm over %d samples"),
			FootSlide[FootSlide.Num() * 95 / 100], FootSlide.Last(), FootSlide.Num());
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

// ---------- Broadcast camera + replay runtime (isolated modules, fed here) ----------

bool ASuperOverGameMode::IsReplaying() const
{
	return bReplayThis && DPhase == EDeliveryPhase::DeadBall && PhaseTime >= ReplayDelay && PhaseTime < ReplayDelay + ReplayTotalTime();
}

int32 ASuperOverGameMode::ReplayAngle() const
{
	if (!ActivePackage.IsValid() || ActivePackage.Angles.Num() == 0) return PhaseTime - ReplayDelay < ReplayAngleTime ? 0 : 1;
	float Accum = 0.f;
	for (int32 I = 0; I < ActivePackage.Angles.Num(); ++I)
	{
		Accum += ActivePackage.Angles[I].WallTime;
		if (PhaseTime - ReplayDelay < Accum) return I;
	}
	return ActivePackage.Angles.Num() - 1;
}

float ASuperOverGameMode::ReplayAngleDuration(int32 Angle) const
{
	if (ActivePackage.Angles.IsValidIndex(Angle)) return ActivePackage.Angles[Angle].WallTime;
	return ReplayAngleTime;
}

float ASuperOverGameMode::ReplayTotalTime() const
{
	if (ActivePackage.IsValid()) return ActivePackage.TotalWallTime();
	return ReplayTime;
}

float ASuperOverGameMode::ReplayAngleBallSpan(int32 Angle) const
{
	if (ActiveRemaps.IsValidIndex(Angle)) return ActiveRemaps[Angle].BallSpan();
	// Legacy span of the super-slow angle (kept for the HUD overlay when no package exists).
	return Angle == 1 ? ReplayAngleTime * SuperSlowSpeed : ReplayAngleTime * ReplaySpeed;
}

float ASuperOverGameMode::ReplayAngleStartTp(int32 Angle) const
{
	if (ActivePackage.Angles.IsValidIndex(Angle)) return ActivePackage.Angles[Angle].StartTp;
	return Angle == 1 ? FMath::Max(0.f, Result.ContactTime - SuperSlowLead) : FMath::Max(0.f, Result.ContactTime - ReplayLead);
}

bool ASuperOverGameMode::IsReplaySlowAngle() const
{
	if (!IsReplaying()) return false;
	const int32 Angle = ReplayAngle();
	if (ActivePackage.Angles.IsValidIndex(Angle))
		return ActivePackage.Angles[Angle].Shot == EBroadcastShot::ReplaySlowMo;
	return Angle == 1;
}

float ASuperOverGameMode::ReplayBallTime() const
{
	const float Into = PhaseTime - ReplayDelay;
	const int32 Angle = ReplayAngle();
	if (ActiveRemaps.IsValidIndex(Angle))
	{
		float Start = 0.f;
		for (int32 I = 0; I < Angle; ++I) Start += ActivePackage.Angles[I].WallTime;
		return ActiveRemaps[Angle].Sample(Into - Start);
	}
	// Legacy two-angle replay (no package): side-on at half speed, then super slow-mo down the pitch.
	return Angle == 0 ? FMath::Max(0.f, Result.ContactTime - ReplayLead) + Into * ReplaySpeed
		: FMath::Max(0.f, Result.ContactTime - SuperSlowLead) + (Into - ReplayAngleTime) * SuperSlowSpeed;
}

void ASuperOverGameMode::ClearBroadcastReplay()
{
	ActivePackage = CricketBroadcast::FReplayPackage();
	ActiveRemaps.Reset();
	bBufferPose = false;
}

void ASuperOverGameMode::BuildReplayPackageForResult(const FDeliveryOutcome& Outcome, bool bMilestone)
{
	using namespace CricketBroadcast;
	ClearBroadcastReplay();
	const FReplayTrigger Trigger = ClassifyReplayEvent(Result, Outcome, bMilestone);
	if (Trigger.Priority == EReplayPriority::None) return;
	// Geometry inputs are resolved at replay time from live actors; the package only needs the
	// ball story (contact, boundary crossing) plus the event.
	FBroadcastFrame Frame;
	Frame.ContactPos = Result.Contact.ContactPos;
	Frame.ExitVel = Result.Contact.ExitVel;
	// Byes have no stroke: the reverse angle takes its station from where the ball ran instead.
	if (!Result.Contact.HasContact() && Result.BallPath.Num() > 1)
		Frame.ExitVel = Result.BallAt(Result.DeadTime) - Result.BallAt(Result.ContactTime);
	Frame.OffSign = OffSideSign(StrikerPlayer().BatHand);
	Frame.ArmSign = BowlerPlayer().BowlHand == ECricketHand::Right ? 1.f : -1.f;
	if (Result.Fielding.Boundary > 0)
	{
		Frame.bHasBoundaryCross = true;
		Frame.BoundaryCrossPos = ToWorld(Result.BallAt(Result.ContactTime + Result.Fielding.BoundaryTime));
	}
	ActivePackage = BuildReplayPackage(Trigger, Result, Frame, BroadcastTuning, RecentReplayShots);
	ActiveRemaps.Reserve(ActivePackage.Angles.Num());
	for (const FReplayAnglePlay& A : ActivePackage.Angles) ActiveRemaps.Add(BuildTimeRemap(A));
}

void ASuperOverGameMode::RebuildReplayCast()
{
	// Stable record order: the buffer maps index -> actor, so this order must never shuffle.
	ReplayCast.Reset();
	auto Add = [&](AStaticMeshActor* A) { if (A && ReplayCast.Num() < ReplayBuffer.MaxActors) ReplayCast.Add(A); };
	Add(Ball); Add(Striker); Add(NonStriker); Add(Bowler); Add(Bat); Add(NonStrikerBat);
	for (TObjectPtr<AStaticMeshActor> H : BatHandles) Add(H.Get());
	for (TObjectPtr<AStaticMeshActor> U : Umpires) Add(U.Get());
	for (TObjectPtr<AStaticMeshActor> F : Fielders) Add(F.Get());
}

void ASuperOverGameMode::RecordReplayFrame(float BallT, const FVector& BallPos, const FVector& BallVel)
{
	if (ReplayCast.Num() == 0) RebuildReplayCast();
	ReplayScratch.Reset(ReplayCast.Num());
	for (const TObjectPtr<AStaticMeshActor>& A : ReplayCast)
	{
		FReplayActorPose P;
		P.Position = A->GetActorLocation();
		P.Rotation = A->GetActorQuat();
		P.Scale = A->GetActorScale3D();
		ReplayScratch.Add(P);
	}
	ReplayBuffer.Record(BallT, BallPos, BallVel, ReplayScratch);
}

FString ASuperOverGameMode::CameraDebugString() const
{
	using namespace CricketBroadcast;
	FString S = FString::Printf(TEXT("CAM %s | %s"), ShotName(LastSolvedShot), *LastCameraDebug);
	if (IsReplaying())
	{
		const int32 A = ReplayAngle();
		S += FString::Printf(TEXT(" | REPLAY %s P%d A%d/%d Tbal %.2f x%.2f"), ReplayEventName(ActivePackage.Event),
			int32(ActivePackage.Priority), A + 1, ActivePackage.Angles.Num(), ReplayBallTime(),
			ActivePackage.Angles.IsValidIndex(A) ? ReplaySpeedAt(ReplayBallTime(), ActivePackage.Angles[A].DecisiveTp, ActivePackage.Angles[A].SlowFactor) : 1.f);
	}
	else if (bReplayThis)
	{
		S += FString::Printf(TEXT(" | REPLAY-COMING %s P%d %d angles"), ReplayEventName(ActivePackage.Event),
			int32(ActivePackage.Priority), ActivePackage.Angles.Num());
	}
	S += FString::Printf(TEXT(" | BUF %d frames %.1f-%.1f s %dkB %s"), ReplayBuffer.NumFrames(), ReplayBuffer.EarliestT(),
		ReplayBuffer.LatestT(), int32(ReplayBuffer.FootprintBytes() / 1024), bBufferPose ? TEXT("POSE-FROM-BUFFER") : TEXT("analytic"));
	return S;
}

void ASuperOverGameMode::Paint(AStaticMeshActor* A, const FLinearColor& Colour, const FCricketTeam* Team, const FString& Name, int32 Number)
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
					// M_Kit: woven cloth but for the helmet's shell and grille, smooth leather shoes, ribbed pads.
					Cloth->SetScalarParameterValue(TEXT("Fabric"), Slot == TEXT("Gear_Helmet") || Slot == TEXT("Gear_Grille") ? 0.f : Slot == TEXT("Kit_Shoes") ? 0.3f : 1.f);
					Cloth->SetScalarParameterValue(TEXT("Ribs"), Slot == TEXT("Gear_Pads") ? 1.f : 0.f);
					// The sporty kit: the team's second colour on the sleeves, collar, side panels and
					// trouser stripes (M_Kit, kit_ue.py), and the name, number and sponsor on the shirt.
					// Harmless until the material is rebuilt with those parameters.
					Cloth->SetVectorParameterValue(TEXT("Accent"), Team ? Team->Accent : Pale);
					if (Team && Slot == TEXT("Kit_Shirt"))
					{
						Cloth->SetTextureParameterValue(TEXT("KitPrint"), KitPrint(Name, Number, Team->Sponsor));
						Cloth->SetScalarParameterValue(TEXT("PrintStrength"), 1.f);
					}
					else
					{
						Cloth->SetScalarParameterValue(TEXT("PrintStrength"), 0.f);
					}
					continue;
				}
				TintOutfit(Cloth, Colour, Team ? Team->Accent : Colour * 0.45f);
				const FString Source = Cloth->Parent ? Cloth->Parent->GetName() : FString();
				// The crew-neck jersey's print, switched on by make_players.py: the sleeves, side panels and
				// pinstripes in the team's second colour with a gold collar, pure-white name and number rimmed in
				// gold, and the sponsor gold across the chest with a small number under it.
				if (Team && Source.Contains(TEXT("Crewneckt")))
				{
					Cloth->SetTextureParameterValue(TEXT("PrintGraphicMap"), ShirtPrint(Name, Number, Team->Sponsor, Source.Contains(TEXT("ovw"))));
					Cloth->SetVectorParameterValue(TEXT("PrintGraphicColorA"), FLinearColor::White);
					Cloth->SetVectorParameterValue(TEXT("PrintGraphicColorB"), FLinearColor(1.f, 0.68f, 0.15f));
					Cloth->SetVectorParameterValue(TEXT("PrintGraphicColorC"), Team->Accent);
					Cloth->SetScalarParameterValue(TEXT("PrintGraphicStrength"), 1.f);
				}
			}
		}
	}
}

void ASuperOverGameMode::TintOutfit(UMaterialInstanceDynamic* Cloth, const FLinearColor& Colour, TOptional<FLinearColor> Piping)
{
	// The parametric outfit (outfit_ue.py): trousers darker, shoes white, like the Blender kit.
	const FString Source = Cloth->Parent ? Cloth->Parent->GetName() : FString();
	const bool bTrousers = Source.Contains(TEXT("jeans"));
	const FLinearColor Shade = bTrousers ? Colour * 0.45f : Source.Contains(TEXT("shoe")) ? FLinearColor(0.75f, 0.75f, 0.75f) : Colour;
	// The only free trousers are jeans: the tint multiplies a faded-denim colour map (divided by its average,
	// div_fabric) and a denim twill overlay. Plain white under the tint, divided by one, and no overlay leave
	// smooth cloth, its seams and folds still in the normal and AO maps.
	if (bTrousers)
	{
		Cloth->SetTextureParameterValue(TEXT("Diffuse"), LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")));
		Cloth->SetVectorParameterValue(TEXT("div_fabric"), FLinearColor::White);
		Cloth->SetScalarParameterValue(TEXT("detail_diffuse_strength"), 0.f);
		// Cricket trousers: the jeans' leather back-pocket label, tinted by "Leather Tint", gives way to piping down
		// the outside of each leg (trouser_stripe.py repaints the mask), in the given colour and matt like the cloth.
		if (Piping)
		{
			const TCHAR* Mask = Source.Contains(TEXT("ovw"))
				? TEXT("/Game/MetaHumans/Outfits/T_TrouserMask_Broad.T_TrouserMask_Broad") : TEXT("/Game/MetaHumans/Outfits/T_TrouserMask.T_TrouserMask");
			if (UTexture* Stripes = LoadObject<UTexture>(nullptr, Mask)) Cloth->SetTextureParameterValue(TEXT("Mask"), Stripes);
			Cloth->SetVectorParameterValue(TEXT("Leather Tint"), *Piping);
			Cloth->SetScalarParameterValue(TEXT("Leather roughness"), 0.8f);
			Cloth->SetVectorParameterValue(TEXT("Metal Tint"), Shade);    // the brass rivets and buttons, in the cloth's colour
		}
	}
	for (const TCHAR* Param : { TEXT("Paint Tint"), TEXT("LogoTint"), TEXT("diffuse_color_1"), TEXT("diffuse_color_2"), TEXT("B_diffuse_color_1") })
		Cloth->SetVectorParameterValue(Param, Shade);
}

UTexture* ASuperOverGameMode::ShirtPrint(const FString& Name, int32 Number, const FString& Sponsor, bool bBroad)
{
	const FString Key = FString::Printf(TEXT("%s|%d|%s|%d"), *Name, Number, *Sponsor, bBroad);
	if (const TObjectPtr<UTextureRenderTarget2D>* Drawn = ShirtPrints.Find(Key)) return *Drawn;
	// Mipmapped, or the letters shimmer on distant fielders; named players, seen close, get the full sheet.
	const int32 Res = Name.IsEmpty() ? 1024 : 2048;
	UTextureRenderTarget2D* Target = UKismetRenderingLibrary::CreateRenderTarget2D(this, Res, Res, RTF_RGBA8, FLinearColor::Transparent, true);
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Target, Canvas, Size, Context);
	UFont* Font = GEngine->GetLargeFont();
	// Laid out on the shirt's first UVs, measured by jersey_panels.py (make_jersey.sh prints them). MetaHuman Creator
	// cuts the shirt two ways with different UVs, slim (nrw) and broad (ovw), so each has its own panels and places:
	// the middle of the back and of the front in u, and in v (counted up from the bottom of the sheet) the tops of the
	// name 6 cm below the shoulders, the back number 13 cm, the sponsor 13 cm and the small number 21 cm. Text is
	// Height tall (in v) from Top down, but no wider than Width (in u). The letters write their coverage into the alpha
	// channel, which is how much of the print shows; text drawn the usual way leaves the alpha alone. Red marks take
	// the print's first colour (white), green its second (gold): every face is white rimmed in gold, every sponsor
	// gold rimmed in white, so the print reads on any team colour.
	Canvas->Canvas->SetWriteDestinationAlpha(true);
	// The jersey's panels (Scripts/metahuman/jersey_panels.py) go down first, copied as they are: blue is the
	// print's third colour, the team's second, green the gold of the collar, and alpha how strongly it shows.
	struct FLayout { const TCHAR* Panels; float BackU, NameTop, NumberTop, FrontU, SponsorTop, SmallTop; };
	static const FLayout Slim{ TEXT("/Game/MetaHumans/Outfits/T_JerseyPanels.T_JerseyPanels"), 0.216f, 0.462f, 0.404f, 0.640f, 0.535f, 0.462f };
	static const FLayout Broad{ TEXT("/Game/MetaHumans/Outfits/T_JerseyPanels_Broad.T_JerseyPanels_Broad"), 0.745f, 0.727f, 0.667f, 0.279f, 0.389f, 0.307f };
	const FLayout& L = bBroad ? Broad : Slim;
	UTexture* Panels = LoadObject<UTexture>(nullptr, L.Panels, nullptr, LOAD_Quiet | LOAD_NoWarn);
#if WITH_EDITOR
	// Played from the editor the texture compiles in the background, a blank stand-in until it is done, and the print
	// is drawn only once; so wait for it.
	if (Panels) FTextureCompilingManager::Get().FinishCompilation({ Panels });
#endif
	if (Panels && Panels->GetResource())
	{
		FCanvasTileItem Tile(FVector2D::ZeroVector, Panels->GetResource(), Size, FLinearColor::White);
		Tile.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(Tile);
	}
	auto Stamp = [&](const FString& S, float U, float Top, float Height, float Width, const FLinearColor& Face, const FLinearColor& Rim)
	{
		if (S.IsEmpty()) return;
		float W, H;
		Canvas->TextSize(Font, S, W, H);
		if (W <= 0.f || H <= 0.f) return;
		const float Scale = FMath::Min(Height * Size.Y / H, Width * Size.X / W);
		const FVector2D At(U * Size.X - W * Scale / 2.f, (1.f - Top) * Size.Y);
		// The rim: the same face shifted round a ring in the second colour, so it outlines the letters.
		const float R = FMath::Max(2.f, Height * Size.Y * 0.055f);
		for (int32 K = 0; K < 8; ++K)
		{
			const float A = K * PI / 4.f;
			FCanvasTextItem Item(At + FVector2D(FMath::Cos(A) * R, FMath::Sin(A) * R), FText::FromString(S), Font, Rim);
			Item.Scale = FVector2D(Scale, Scale);
			Canvas->DrawItem(Item);
		}
		// Faux-bold: the face drawn five times, each offset a pixel, so thin type fills its coverage solid.
		const float B = FMath::Max(1.5f, Size.X / 1024.f * 1.5f);
		for (const FVector2D& O : { FVector2D::ZeroVector, FVector2D(B, 0.f), FVector2D(-B, 0.f), FVector2D(0.f, B), FVector2D(0.f, -B) })
		{
			FCanvasTextItem Item(At + O, FText::FromString(S), Font, Face);
			Item.Scale = FVector2D(Scale, Scale);
			Canvas->DrawItem(Item);
		}
	};
	Stamp(Name.ToUpper(), L.BackU, L.NameTop, 0.040f, 0.20f, FLinearColor::Red, FLinearColor::Green);
	Stamp(FString::FromInt(Number), L.BackU, L.NumberTop, 0.14f, 0.15f, FLinearColor::Red, FLinearColor::Green);
	Stamp(Sponsor, L.FrontU, L.SponsorTop, 0.045f, 0.22f, FLinearColor::Green, FLinearColor::Red);
	Stamp(FString::FromInt(Number), L.FrontU, L.SmallTop, 0.035f, 0.08f, FLinearColor::Red, FLinearColor::Green);
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	// The canvas draws only the top mip; this builds the rest from it (without it they stay clear, and the print
	// fades out as the shirt gets smaller on screen).
	Target->UpdateResourceImmediate(false);
	ShirtPrints.Add(Key, Target);
	return Target;
}

UTexture* ASuperOverGameMode::KitPrint(const FString& Name, int32 Number, const FString& Sponsor)
{
	const FString Key = FString::Printf(TEXT("kit|%s|%d|%s"), *Name, Number, *Sponsor);
	if (const TObjectPtr<UTextureRenderTarget2D>* Drawn = KitPrints.Find(Key)) return *Drawn;
	const int32 Res = 2048;
	UTextureRenderTarget2D* Target = UKismetRenderingLibrary::CreateRenderTarget2D(this, Res, Res, RTF_RGBA8, FLinearColor::Transparent, true);
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Target, Canvas, Size, Context);
	Canvas->Canvas->SetWriteDestinationAlpha(true);
	const FLinearColor Ink(0.94f, 0.96f, 1.f), Gold(1.f, 0.66f, 0.18f);
	// Rasterize the installed Barlow typeface at print resolution. Scaling the engine's
	// tiny bitmap font made even a 2K texture look blurred on player close-ups.
	auto Block = [&](const FString& Text, float X, float Y, float Height, float Width, const FLinearColor& Colour)
	{
		if (Text.IsEmpty()) return;
		FSlateFontInfo Font = FrontendStyle::Font(FMath::RoundToInt(Height * 2.f), FrontendStyle::EWeight::Condensed);
		const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		FVector2D Extent = Measure->Measure(Text, Font);
		if (Extent.X <= 0.f || Extent.Y <= 0.f) return;
		Font.Size *= FMath::Min(Height * 2.f / Extent.Y, Width * 2.f / Extent.X);
		Extent = Measure->Measure(Text, Font);
		FCanvasTextItem Item(FVector2D(X * 2.f - Extent.X * 0.5f, Y * 2.f), FText::FromString(Text), Font, Colour);
		Canvas->DrawItem(Item);
	};
	Block(Name.ToUpper(), 512.f, 64.f, 78.f, 760.f, Ink);
	Block(FString::FromInt(Number), 512.f, 163.f, 285.f, 570.f, Ink);
	Block(TEXT("CRICKET / 26"), 512.f, 446.f, 20.f, 340.f, Gold);
	Block(TEXT("C26"), 260.f, 594.f, 62.f, 160.f, Gold);
	Block(TEXT("PERFORMANCE"), 747.f, 601.f, 26.f, 255.f, Ink);
	Block(Sponsor.ToUpper(), 512.f, 708.f, 95.f, 830.f, Ink);
	Block(TEXT("CRICKET CLUB"), 512.f, 815.f, 27.f, 370.f, Gold);
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	Target->UpdateResourceImmediate(false);
	KitPrints.Add(Key, Target);
	return Target;
}


AStaticMeshActor* ASuperOverGameMode::DevLookFigure() const
{
	// "all" steps through everyone, one figure per saved frame.
	TArray<AStaticMeshActor*> All = { Striker.Get(), NonStriker.Get(), Bowler.Get() };
	for (const TObjectPtr<AStaticMeshActor>& U : Umpires) All.Add(U.Get());
	for (const TObjectPtr<AStaticMeshActor>& F : Fielders) All.Add(F.Get());
	if (DevLookRole == TEXT("all")) return All[DevLookIndex % All.Num()];
	if (DevLookRole == TEXT("striker")) return Striker;
	if (DevLookRole == TEXT("nonstriker")) return NonStriker;
	if (DevLookRole == TEXT("bowler")) return Bowler;
	if (DevLookRole == TEXT("keeper")) return Fielders.IsEmpty() ? nullptr : Fielders[0].Get();
	if (DevLookRole.StartsWith(TEXT("umpire"))) { const int32 I = FCString::Atoi(*DevLookRole.Mid(6)); return Umpires.IsValidIndex(I) ? Umpires[I].Get() : nullptr; }
	if (DevLookRole.StartsWith(TEXT("fielder"))) { const int32 I = FCString::Atoi(*DevLookRole.Mid(7)); return Fielders.IsValidIndex(I) ? Fielders[I].Get() : nullptr; }
	return nullptr;
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
	const bool bPitchActor = Marker == Striker || Marker == NonStriker || Marker == Bowler || (!Fielders.IsEmpty() && Marker == Fielders[0]);
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
		// The pitch actors (batters, bowler, keeper) maintain continuous face updates; distant fielders
		// and umpires tick pose only when rendered to save massive CPU animation cost. On camera cuts,
		// RefreshCutFigures refreshes any cut close-ups immediately to avoid stale bone artifacts.
		for (USkeletalMeshComponent* Part : Parts)
			if (Part->GetFName() == TEXT("Face"))
				Part->VisibilityBasedAnimTickOption = bPitchActor
					? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
					: EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		// The cricket kit made by Scripts/metahuman/make_kit.sh, when there is one, in place of the preset T-shirt and
		// shorts, and the batting gear (pads, gloves, helmet) on the two batters. Both are skinned to this body's
		// skeleton and take its pose.
		auto Wear = [&](const TCHAR* Part)
		{
			const FString MeshPath = FString::Printf(TEXT("/Game/MetaHumans/%s/Kit/SKM_%s_%s.SKM_%s_%s"), MetaHuman, MetaHuman, Part, MetaHuman, Part);
			if (!Body) return false;
			USkeletalMesh* Mesh = nullptr;
			if (FCString::Strcmp(Part, TEXT("Kit")) == 0)
				Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/MetaHumans/CricketKit/SKM_%s_Kit"), MetaHuman), nullptr, LOAD_Quiet | LOAD_NoWarn);
			if (!Mesh) Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
			if (!Mesh) return false;
			UMaterialInterface* Fabric = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaHumans/Kit/M_Kit.M_Kit"), nullptr, LOAD_Quiet | LOAD_NoWarn);
			USkeletalMeshComponent* Worn = NewObject<USkeletalMeshComponent>(Player, Part);
			Worn->SetSkeletalMesh(Mesh);
			for (int32 I = 0; Fabric && I < Worn->GetNumMaterials(); ++I) Worn->SetMaterial(I, Fabric);
			Worn->SetupAttachment(Body);
			Worn->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Worn->SetLeaderPoseComponent(Body);
			Worn->SetCastContactShadow(bPitchActor);
			Worn->RegisterComponent();
			Player->AddInstanceComponent(Worn);
			return true;
		};
		TArray<USkinnedMeshComponent*> Garment;
		Player->GetComponents(Garment);
		// A player built in Epic's parametric outfit (Scripts/metahuman/outfit_ue.py) wears it; the rest wear the Blender
		// kit over the preset garment.
		const bool bOutfit = Garment.ContainsByPredicate([](USkinnedMeshComponent* Part) {
			return Part->GetMaterials().ContainsByPredicate([](UMaterialInterface* M) { return M && M->GetName().Contains(TEXT("WI_OA_")); }); });
		if (bOutfit || Wear(TEXT("Kit")))
		{
			if (!bOutfit) for (USkinnedMeshComponent* Part : Garment) if (Part != Body && Part->GetFName() != TEXT("Face")) Part->SetVisibility(false);
			const bool bEquippedBatter = (Marker == Striker || Marker == NonStriker) && Wear(TEXT("Gear"));
			// Every field preset puts the keeper first (CricketField::Make).
			const bool bEquippedKeeper = !Fielders.IsEmpty() && Marker == Fielders[0] && Wear(TEXT("Keeper"));
			// The one-LOD gloves use finger bones stripped from optimized body LODs 1/2.
			// Keep full body/face pose only for the three equipped actors; other fielders retain mobile LODs.
			if (ULODSyncComponent* LOD = Player->FindComponentByClass<ULODSyncComponent>())
			{
				// Dense cards on close-ups for every face; screen size still selects cheaper distant LODs.
				for (const FName Part : { FName(TEXT("Hair")), FName(TEXT("Beard")), FName(TEXT("Mustache")), FName(TEXT("Eyebrows")) })
					if (FLODMappingData* Mapping = LOD->CustomLODMapping.Find(Part); Mapping && Mapping->Mapping.Num() > 0 && Mapping->Mapping[0] == 3)
						Mapping->Mapping[0] = 2;
				if (bEquippedBatter || bEquippedKeeper)
				{
					LOD->ForcedLOD = 0;
				}
				else if (Marker == Bowler)
				{
					// Bowler body at LOD 0; dense hair cards
					LOD->ForcedLOD = 0;
					for (const FName Part : { FName(TEXT("Hair")), FName(TEXT("Beard")) })
						if (FLODMappingData* Mapping = LOD->CustomLODMapping.Find(Part); Mapping && Mapping->Mapping.Num() > 0 && Mapping->Mapping[0] == 3)
							Mapping->Mapping[0] = 2;
					LOD->RefreshSyncComponents();
				}
				else
				{
					// Let celebration and umpire close-ups recover facial detail instead of pinning distant geometry.
					LOD->ForcedLOD = -1;
				}
				LOD->RefreshSyncComponents();
			}
			const bool bHattedUmpire = Umpires.Contains(Marker) && Wear(TEXT("Hat"));
			if (bEquippedBatter || bHattedUmpire)
			{
				TArray<UPrimitiveComponent*> Visuals;
				Player->GetComponents(Visuals);
				for (UPrimitiveComponent* Visual : Visuals)
					if (Visual->GetName().StartsWith(TEXT("Hair"))) Visual->SetVisibility(false);
			}
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
	// Pitch actors update pose/bones continuously; distant fielders and umpires tick pose when rendered.
	// Camera cuts invoke RefreshCutFigures() to eliminate any 1-frame stale bone latency.
	Body->VisibilityBasedAnimTickOption = bPitchActor
		? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
		: EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Body->SetCastContactShadow(bPitchActor);
	if (UCricketAnimInstance* Anim = Cast<UCricketAnimInstance>(Body->GetAnimInstance()))
	{
		Anim->Idle = IdleAnim;
		Anim->Jog = JogAnim;
		Anim->Walk = WalkAnim;
		Anim->Sprint = SprintAnim;
	}
	Marker->GetStaticMeshComponent()->SetVisibility(false);
}

USkeletalMeshComponent* ASuperOverGameMode::BodyOf(const AActor* Figure) const
{
	const TObjectPtr<USkeletalMeshComponent>* Body = Bodies.Find(Figure);
	return Body ? Body->Get() : nullptr;
}

void ASuperOverGameMode::RefreshCutFigures()
{
	for (AStaticMeshActor* Fig : Figures)
	{
		if (USkeletalMeshComponent* Body = BodyOf(Fig))
		{
			if (AActor* Owner = Body->GetOwner())
			{
				TArray<USkeletalMeshComponent*> Comps;
				Owner->GetComponents(Comps);
				for (USkeletalMeshComponent* C : Comps)
				{
					if (C && C->IsVisible())
					{
						C->RefreshBoneTransforms();
					}
				}
			}
		}
	}
}

void ASuperOverGameMode::SetupAudio()
{
	// Recorded cues (Content/Audio/Sfx/<Cue>.pcm) replace the synthesized ones one by one.
	static const TCHAR* CueNames[] = { TEXT("BatCrack"), TEXT("EdgeTick"), TEXT("Bounce"), TEXT("Stumps"), TEXT("Crowd"), TEXT("BatMiddle"),
		TEXT("BatToe"), TEXT("PadThud"), TEXT("KeeperGlove"), TEXT("CatchPop"), TEXT("ThrowRelease"), TEXT("Footstep") };
	static_assert(UE_ARRAY_COUNT(CueNames) == int32(CricketAudio::ECue::Count), "a name per cue");
	for (int32 I = 0; I < int32(CricketAudio::ECue::Count); ++I)
	{
		CuePcm[I] = CricketAudio::LoadClip(FString::Printf(TEXT("Sfx/%s.pcm"), CueNames[I]));
		if (CuePcm[I].IsEmpty()) CuePcm[I] = CricketAudio::Synthesize(CricketAudio::ECue(I));
	}
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
	Channel(VoiceWave, VoiceAudio);
	Channel(VocalWave, VocalAudio);
	// Same make-up gain as the field channel (at unity the voice sat level with the crowd bed, -26 dB RMS), lifted a
	// little more so the commentary reads over the crowd.
	if (VoiceAudio) VoiceAudio->SetVolumeMultiplier(CricketAudio::BusTrim(CricketAudio::EMixBus::Commentary) * CricketAudio::MixGain * CricketAudio::CommentaryLift);
	if (VocalAudio) VocalAudio->SetVolumeMultiplier(CricketAudio::BusTrim(CricketAudio::EMixBus::PlayerVocal) * CricketAudio::MixGain);
}

void ASuperOverGameMode::Say(const FString& Body, const FString& Suffix, float At)
{
	VoicePending = CricketAudio::LoadClip(TEXT("Commentary/") + CricketCommentary::ClipKey(Body) + TEXT(".pcm"));
	if (VoicePending.IsEmpty())
	{
		CricketAudio::EnsureClip(Body);
		VoicePending = CricketAudio::LoadClip(TEXT("Commentary/") + CricketCommentary::ClipKey(Body) + TEXT(".pcm"));
	}
	VoiceAt = VoicePending.IsEmpty() ? -1.f : At;
	UE_LOG(LogCRICKET26, Display, TEXT("Voice: %s (%s)"), *CricketCommentary::ClipKey(Body), VoicePending.IsEmpty() ? TEXT("no clip, caption only") : TEXT("clip"));
	if (VoicePending.IsEmpty()) return;

	if (!Suffix.IsEmpty())
	{
		TArray<int16> Tail = CricketAudio::LoadClip(TEXT("Commentary/") + CricketCommentary::ClipKey(Suffix) + TEXT(".pcm"));
		if (Tail.IsEmpty())
		{
			CricketAudio::EnsureClip(Suffix);
			Tail = CricketAudio::LoadClip(TEXT("Commentary/") + CricketCommentary::ClipKey(Suffix) + TEXT(".pcm"));
		}
		if (!Tail.IsEmpty())
		{
			VoicePending.AddZeroed(CricketAudio::SampleRate * 12 / 100); // breath between the call and the situation
			VoicePending.Append(Tail);
		}
	}
}

void ASuperOverGameMode::PlayCue(CricketAudio::ECue Cue, float Volume)
{
	if (!FieldAudio) return;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	// Concurrency: a new cue never stacks audibly on itself (footsteps, throws, takes).
	if (!CricketAudioDirector::ShouldPlay(AudioDir, Cue, Now)) return;
	CricketAudioDirector::MarkPlayed(AudioDir, Cue, Now);
	// ponytail: one ball channel, a new cue cuts the tail of the last; mix on separate channels if cues overlap audibly.
	FieldWave->ResetAudio();
	FieldAudio->SetVolumeMultiplier(Volume * CricketAudio::MixGain);
	const TArray<int16>& Pcm = CuePcm[int32(Cue)];
	FieldWave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.GetData()), Pcm.Num() * sizeof(int16));
}

void ASuperOverGameMode::UpdateFigures(float Dt)
{
	// Everyone faces the ball when standing and their direction of travel when moving, and plays idle or a
	// jog paced to their speed. A player tipped over for a dive keeps the dive, and one in a captured dive or
	// throw the facing it set.
	const FVector BallAt = Ball->GetActorLocation();
	const bool bBallInHand = (DPhase == EDeliveryPhase::Waiting || DPhase == EDeliveryPhase::RunUp) && !IsReplaying();
	const FVector StrikerHead = Striker->GetActorLocation() + FVector(0.f, 0.f, 70.f);
	const float Off = OffSideSign(StrikerPlayer().BatHand);
	for (AStaticMeshActor* A : Figures)
	{
		FFigureState& S = FigureStates.FindOrAdd(A);
		const FVector Pos = A->GetActorLocation();
		FVector Vel = Dt > 0.f && !S.Last.IsZero() ? (Pos - S.Last) / Dt : FVector::ZeroVector;
		const bool bJumped = Vel.Size() > 1500.f; // faster than anyone runs: a reset or replay jump
		if (bJumped) { Vel = FVector::ZeroVector; S.Speed = 0.f; }
		if (S.bCarried) Vel = FVector::ZeroVector; // the dive fading out: the actor takes up the clip's travel, the body is still
		S.bCarried = false;
		S.Last = Pos;
		const bool bHeld = S.bHeld; // facing already set this frame by a dive or throw
		S.bHeld = false;
		S.Speed = FMath::Lerp(S.Speed, FVector2D(Vel).Size() / 100.f, FMath::Clamp(Dt * 8.f, 0.f, 1.f));
		if (A->IsHidden()) continue;
		UCricketAnimInstance* Anim = AnimOf(A);
		const USkeletalMeshComponent* Body = BodyOf(A);
		// The striker against last frame's plan (UpdatePoses replaces it before the body poses): how far each
		// hand's knuckles are from where they grip the handle, whether an elbow or forearm is inside the torso,
		// and how far a planted foot slid. Not if the striker was moved since (a new ball, or setting off for a
		// run): the bones then sit on the old transform until the next evaluation, which the screen never shows.
		if (Anim && A == Striker && !bJumped && Anim->Pose.Batter.Weight >= 1.f && A->GetActorTransform().Equals(StrikerPosed, 0.01f))
		{
			const FCricketBatterPose& Plan = Anim->Pose.Batter;
			const FVector Axis = Plan.BatAxis.GetSafeNormal();
			const FVector Pelvis = Body->GetSocketLocation(TEXT("pelvis")), Neck = Body->GetSocketLocation(TEXT("neck_01"));
			const FVector Spine = (Neck - Pelvis).GetSafeNormal();
			const FVector Deep = (Plan.Chest - Spine * (Plan.Chest | Spine)).GetSafeNormal(), Wide = FVector::CrossProduct(Spine, Deep);
			// A distant LOD drops the fingers (and may drop the toes): their sockets are left stale, so skip them.
			auto Posed = [&](const TCHAR* Bone) { return Body->RequiredBones.Contains(FBoneIndexType(Body->GetBoneIndex(Bone))); };
			const bool bFingers = Posed(TEXT("pinky_01_l")) && Posed(TEXT("pinky_01_r")), bToes = Posed(TEXT("ball_l")) && Posed(TEXT("ball_r"));
			static const FName IndexBoneNames[2] = { TEXT("index_01_l"), TEXT("index_01_r") };
			static const FName PinkyBoneNames[2] = { TEXT("pinky_01_l"), TEXT("pinky_01_r") };
			static const FName LowerArmBoneNames[2] = { TEXT("lowerarm_l"), TEXT("lowerarm_r") };
			static const FName HandBoneNames[2] = { TEXT("hand_l"), TEXT("hand_r") };
			static const FName UpperArmBoneNames[2] = { TEXT("upperarm_l"), TEXT("upperarm_r") };
			static const FName BallBoneNames[2] = { TEXT("ball_l"), TEXT("ball_r") };
			for (int32 H = 0; H < 2; ++H)
			{
				// The solve puts the handle's centre line 3.2 cm out from the knuckles' midpoint, level with it.
				const FVector Handle = Plan.Grip + Axis * (H == Plan.TopHand ? -4.5f : 4.5f);
				const FVector Knuckles = 0.5f * (Body->GetSocketLocation(IndexBoneNames[H]) + Body->GetSocketLocation(PinkyBoneNames[H])) - Handle;
				const float Along = Knuckles | Axis;
				const float Miss = FMath::Sqrt(FMath::Square(Along) + FMath::Square((Knuckles - Axis * Along).Size() - 3.2f));
				if (bFingers) HandMiss.Add(Miss);
				if (bFingers && Miss > 6.f && bDebug) UE_LOG(LogTemp, Display, TEXT("Hand miss %.0f cm: hand %d phase %d time %.2f dt %.3f"), Miss, H, int32(DPhase), PhaseTime, Dt);
				const FVector Elbow = Body->GetSocketLocation(LowerArmBoneNames[H]), Wrist = Body->GetSocketLocation(HandBoneNames[H]);
				for (const FVector& P : { Elbow, FMath::Lerp(Elbow, Wrist, 0.5f) })
				{
					const FVector V = P - Pelvis - Spine * FMath::Clamp((P - Pelvis) | Spine, 0.f, FVector::Dist(Neck, Pelvis));
					const bool bIn = FMath::Square((V | Wide) / 16.f) + FMath::Square((V | Deep) / 12.f) < 1.f;
					++ElbowChecks;
					ElbowInside += bIn;
					if (bIn && bDebug) UE_LOG(LogTemp, Display, TEXT("Arm inside torso: hand %d phase %d time %.2f"), H, int32(DPhase), PhaseTime);
				}
				// A raised arm's elbow winged out sideways, beyond the shoulder, rather than forward under the hands.
				const FVector Shoulder = Body->GetSocketLocation(UpperArmBoneNames[H]);
				// The elbow bowed up off the shoulder-to-wrist line: the upper arm lifting and the forearm dropping
				// to the hands, the reverse of a real stroke, where the upper arm hangs and the forearm rises.
				const FVector Chord = (Wrist - Shoulder).GetSafeNormal();
				const FVector Bow = Elbow - Shoulder - Chord * ((Elbow - Shoulder) | Chord);
				++ArmChecks;
				ElbowUp += Bow.Z > 3.f;
				if (Bow.Z > 3.f && bDebug) UE_LOG(LogTemp, Display, TEXT("Elbow bowed up %.0f cm: hand %d phase %d time %.2f"), Bow.Z, H, int32(DPhase), PhaseTime);
				if (Wrist.Z > Shoulder.Z)
				{
					const bool bFlared = FMath::Abs((Elbow - Pelvis) | Wide) - FMath::Abs((Shoulder - Pelvis) | Wide) > 12.f;
					++RaisedChecks;
					ElbowFlared += bFlared;
					if (bFlared && bDebug) UE_LOG(LogTemp, Display, TEXT("Elbow flared: hand %d phase %d time %.2f"), H, int32(DPhase), PhaseTime);
				}
				const FVector Toes = Body->GetSocketLocation(BallBoneNames[H]);
				const bool bPlanted = Plan.Lift[H] <= 0.f && bToes;
				if (bPlanted && bWasPlanted[H] && !bStrikerCut)
				{
					FootSlide.Add(FVector2D(Toes - LastBall[H]).Size());
					if (FootSlide.Last() > 1.f && bDebug) UE_LOG(LogTemp, Display, TEXT("Foot slide %.1f cm: foot %d phase %d time %.2f dt %.3f"), FootSlide.Last(), H, int32(DPhase), PhaseTime, Dt);
				}
				LastBall[H] = Toes;
				bWasPlanted[H] = bPlanted;
			}
		}
		else if (A == Striker) bWasPlanted[0] = bWasPlanted[1] = false;
		if (A->GetActorUpVector().Z > 0.95f && !bHeld)
		{
			const bool bStriker = (A == Striker);
			const bool bNonStriker = (A == NonStriker);
			const bool bBowler = (A == Bowler);
			const bool bKeeper = (!Fielders.IsEmpty() && A == Fielders[0]);
			const bool bUmpire0 = (!Umpires.IsEmpty() && A == Umpires[0]);
			const bool bUmpire1 = (Umpires.Num() > 1 && A == Umpires[1]);
			const bool bLiveRunning = (DPhase == EDeliveryPhase::BallInPlay || DPhase == EDeliveryPhase::DeadBall) && Result.Running.Attempted > 0 && (PhaseTime - Result.ContactTime > 0.f);

			FVector Look;
			if (bKeeper)
			{
				Look = (S.Speed > 2.f) ? Vel : FVector(1.f, 0.f, 0.f);
			}
			else if (bStriker)
			{
				// Striker holds a side-on batting stance until actually running between wickets
				Look = (bLiveRunning && S.Speed > 0.8f) ? Vel : FVector(0.f, Off, 0.f);
			}
			else if (bNonStriker)
			{
				// Non-striker holds a normal stance facing down the pitch until running between wickets
				Look = (bLiveRunning && S.Speed > 0.8f) ? Vel : FVector(-1.f, 0.f, 0.f);
			}
			else if (bBowler)
			{
				// Bowler faces down the pitch towards the striker when delivering or standing
				Look = (S.Speed > 0.8f && DPhase != EDeliveryPhase::Waiting && DPhase != EDeliveryPhase::RunUp) ? Vel : FVector(-1.f, 0.f, 0.f);
			}
			else if (bUmpire0)
			{
				Look = FVector(-1.f, 0.f, 0.f);
			}
			else if (bUmpire1)
			{
				Look = Striker ? (Striker->GetActorLocation() - Pos) : FVector::ZeroVector;
			}
			else // Fielders
			{
				// Fielders face their travel direction when moving to field, and hold a normal ready fielding stance facing the striker when standing
				Look = (S.Speed > 0.8f) ? Vel : (Striker ? (Striker->GetActorLocation() - Pos) : FVector(-1.f, 0.f, 0.f));
			}

			if (!FVector2D(Look).IsNearlyZero())
				A->SetActorRotation(FMath::RInterpConstantTo(A->GetActorRotation(), FRotator(0.f, Look.Rotation().Yaw, 0.f), Dt, bKeeper ? 240.f : 540.f));
		}
		if (!Anim) continue;
		// ponytail: two cycles time-scaled to speed (template jog ~4 m/s, sprint ~SprintSpeed) and cross-faded, feet
		// not phase-matched through the short fade; a synced blend space if the fade shows.
		// A walk under the jog when the template walk is there (players walking off, in, or to a word), else the slowed jog.
		// The keeper's own solve (UpdatePoses) steps its feet up to a jog; the clips take over running.
		const bool bKeeper = !Fielders.IsEmpty() && A == Fielders[0];
		Anim->Walk = WalkAnim.Get();
		Anim->Pose.WalkWeight = Anim->Walk && !bKeeper ? FMath::Clamp((S.Speed - 0.3f) / 0.4f, 0.f, 1.f) : 0.f;
		Anim->Pose.WalkRate = FMath::Clamp(S.Speed / 1.4f, 0.6f, 1.6f);
		Anim->Pose.JogWeight = bKeeper ? FMath::Clamp((S.Speed - 2.2f) / 0.6f, 0.f, 1.f) : WalkAnim ? FMath::Clamp((S.Speed - 1.8f) / 0.6f, 0.f, 1.f) : FMath::Clamp((S.Speed - 0.4f) / 0.6f, 0.f, 1.f);
		Anim->Pose.JogRate = FMath::Clamp(S.Speed / 4.f, 0.6f, 2.f);
		Anim->Pose.SprintWeight = FMath::Clamp((S.Speed - 4.5f) / 1.5f, 0.f, 1.f);
		Anim->Pose.SprintRate = FMath::Clamp(S.Speed / SprintSpeed, 0.6f, 1.5f);
		// Actions are set afresh each frame by UpdatePoses; everyone watches the ball, except the bowler holding it,
		// who watches the batter (following the ball in their own hand turned the head with every swing of the arm).
		// Umpires do the same while the ball is in hand: down the pitch at the striker, never turned
		// around watching the run-up start behind them; they track the ball once it is live.
		Anim->Pose.ClearActions();
		Anim->Pose.LookWeight = 1.f;
		const bool bUmpireLook = (!Umpires.IsEmpty() && (A == Umpires[0] || (Umpires.Num() > 1 && A == Umpires[1])));
		const FVector DesiredLook = (A == Bowler || bUmpireLook) && bBallInHand ? StrikerHead : BallAt;
		if (!Fielders.IsEmpty() && A == Fielders[0])
		{
			S.Look = S.Look.IsZero() || bJumped ? DesiredLook : FMath::VInterpTo(S.Look, DesiredLook, Dt, 14.f);
			Anim->Pose.LookAt = S.Look;
		}
		else Anim->Pose.LookAt = DesiredLook;
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
	// Each MetaHuman is whole and dressed for its role.
	for (AStaticMeshActor* A : Figures)
	{
		const USkeletalMeshComponent* Body = BodyOf(A);
		const AActor* Player = Body ? Body->GetOwner() : nullptr;
		if (!Player || Player == A) continue;  // the mannequin stand-in
		const EFigureRole Role = A == Striker || A == NonStriker ? EFigureRole::Batter
			: !Fielders.IsEmpty() && A == Fielders[0] ? EFigureRole::Keeper
			: Umpires.Contains(A) ? EFigureRole::Umpire : EFigureRole::Other;
		for (const FString& Problem : FigureProblems(Player, Role))
		{
			UE_LOG(LogTemp, Error, TEXT("Figure check FAILED: %s: %s"), *Player->GetName(), *Problem);
		}
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

TArray<FString> ASuperOverGameMode::FigureProblems(const AActor* Player, EFigureRole Role)
{
	TArray<FString> Problems;
	TArray<USkinnedMeshComponent*> Parts;
	Player->GetComponents(Parts);
	auto Find = [&](const TCHAR* Name) -> USkinnedMeshComponent*
	{
		USkinnedMeshComponent* const* Part = Parts.FindByPredicate([Name](const USkinnedMeshComponent* P) { return P->GetFName() == Name; });
		return Part && (*Part)->GetSkinnedAsset() ? *Part : nullptr;
	};
	const USkeletalMeshComponent* Body = Cast<USkeletalMeshComponent>(Find(TEXT("Body")));
	if (!Body) Problems.Add(TEXT("no body mesh"));
	else if (!Cast<UCricketAnimInstance>(Body->GetAnimInstance())) Problems.Add(TEXT("body is not driven by the cricket animation"));
	if (const USkinnedMeshComponent* Face = Find(TEXT("Face")); !Face) Problems.Add(TEXT("no face mesh"));
	else if (Face->VisibilityBasedAnimTickOption != EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
		Problems.Add(TEXT("face bones go stale off screen"));
	for (const USkinnedMeshComponent* Part : Parts)
	{
		if (!Part->GetSkinnedAsset()) continue;
		for (int32 I = 0; I < Part->GetNumMaterials(); ++I)
		{
			if (!Part->GetMaterial(I)) Problems.Add(FString::Printf(TEXT("%s material slot %d is empty"), *Part->GetName(), I));
		}
	}
	// Pads, gloves and helmet only on the batters, keeping gear only on the keeper, the sun hat only on umpires.
	const struct { const TCHAR* Gear; EFigureRole Wearer; } Roles[] = { { TEXT("Gear"), EFigureRole::Batter }, { TEXT("Keeper"), EFigureRole::Keeper }, { TEXT("Hat"), EFigureRole::Umpire } };
	for (const auto& R : Roles)
	{
		const USkinnedMeshComponent* Gear = Find(R.Gear);
		if (Gear && Gear->IsVisible() && Role != R.Wearer) Problems.Add(FString::Printf(TEXT("wears %s gear not meant for its role"), R.Gear));
		if (!Gear && Role == R.Wearer) Problems.Add(FString::Printf(TEXT("missing its %s gear"), R.Gear));
	}
	if (const IConsoleVariable* Groom = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HairStrands.Enable")); Groom && Groom->GetInt() == 0)
	{
		Problems.Add(TEXT("grooms are switched off: no hair, eyebrows, lashes or beard"));
	}
	return Problems;
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

UAnimSequence* ASuperOverGameMode::BowlAnim() const
{
	const FCricketPlayer& B = BowlerPlayer();
	return BowlAnims[2 * FMath::Clamp(int32(B.BowlerType), 0, 2) + (B.BowlHand == ECricketHand::Right ? 0 : 1)];
}

CricketPose::FClipPlay ASuperOverGameMode::BowlPlay(float T, bool bLive) const
{
	if (!BowlAnim()) return {};
	// Set off on the run-up this long before the release (a human's is assumed ideal until they let go).
	const float At = 0.5f * ((HumanBowls() && !bLive ? IdealRelease : ReleaseTiming) + 1.f) * RunUpSeconds;
	// The clip is only the last strides: before them the run-in is the sprint (or jog) paced to the approach.
	return CricketPose::BowlClip(TimeToRelease(T, bLive), -FMath::Min(At, CricketPose::BowlClipRelease));
}

float ASuperOverGameMode::RunUpX(float Time) const
{
	using namespace CricketGeo;
	const float Ideal = 0.5f * (IdealRelease + 1.f) * RunUpSeconds;
	// ponytail: constant run-up speed; an accelerating approach when the bowler animation has strides to match.
	return FMath::Max(PitchLength - 1.f + RunUpLength() * (1.f - Time / Ideal), PitchLength - 2.2f);
}

ASkyLight* ASuperOverGameMode::SpawnSky(UWorld* World, bool bNight)
{
	// The sky light is made Movable before it registers: there is no baked capture.
	ASkyLight* Sky = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity);
	Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	// The sky is captured once, after BuildScene has put the sky up, never in real time: phones skip a real-time capture,
	// and on desktop it lights nothing without Lumen, so below the Epic tier every shadow and every player's shaded
	// side went black. The sun and sky never move during a match, so one capture is all there is to see.
	Sky->GetLightComponent()->bRealTimeCapture = false;
	Sky->GetLightComponent()->SetIntensity(bNight ? 0.35f : 1.75f);
	Sky->GetLightComponent()->SetLightColor(bNight ? FLinearColor(0.55f, 0.65f, 0.95f) : FLinearColor(0.92f, 0.95f, 1.0f));
	// Project natural turf/pitch bounce light upward to lift deep helmet shadows physically without fake lights.
	// Warm neutral, never green-dominant: a green lower hemisphere dyed eye whites and hat-brim undersides
	// green in shade (premium face pass). Same total energy as the old turf bounce.
	Sky->GetLightComponent()->bLowerHemisphereIsBlack = false;
	Sky->GetLightComponent()->SetLowerHemisphereColor(bNight ? FLinearColor(0.08f, 0.09f, 0.12f) : FLinearColor(0.27f, 0.245f, 0.185f));
	Sky->FinishSpawning(FTransform::Identity);
	return Sky;
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
	Sun->GetComponent()->SetCastShadows(true);
	// Screen-space contact shadows ground boots on turf, the ball, bat, stumps, and crisp facial contours under the helmet
	Sun->GetComponent()->ContactShadowLength = 0.04f;
	Sun->GetComponent()->ContactShadowLengthInWS = true;
	Sun->GetComponent()->ContactShadowCastingIntensity = 1.0f;
	Sun->GetComponent()->LightSourceAngle = 1.2f; // natural sun disc penumbra
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

	ADirectionalLight* Sun = SpawnSun(W);
	const CricketStadium::FVenue& V = CricketStadium::Venue(VenueIndex);
	float Exposure = ExposureEV100;
	if (V.bNight)
	{
		// Night: a faint, cool moon for the sky and the fill; the floodlights (BuildStadium) light the ground to
		// about 2000 lux, which the exposure is set for.
		Sun->GetComponent()->SetIntensity(80.f);
		Sun->GetComponent()->SetLightColor(FLinearColor(0.6f, 0.7f, 1.f));
		Exposure = FMath::Log2(NightLux / 2.5f);
	}
	else if (V.Cloud > 0.5f)
	{
		// Overcast: the cloud takes most of the direct sun and softens its shadows. The exposure opens up a stop, as a
		// broadcast camera's would, so the ground reads a little darker than on a sunny day but not gloomy.
		const float Through = 1.f - 0.7f * V.Cloud;
		Sun->GetComponent()->SetLightSourceAngle(6.f);
		Exposure -= 1.f;
		Sun->GetComponent()->SetIntensity(SunLux * Through);
		// Cloud to see, from Epic's sky content (UE EULA), thickened into a grey blanket; phones keep the plain sky.
		UMaterialInterface* CloudMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst"));
		if (Quality >= 2 && CloudMaterial)
		{
			UMaterialInstanceDynamic* Cover = UMaterialInstanceDynamic::Create(CloudMaterial, this);
			Cover->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), -0.2f + 0.6f * V.Cloud);
			Cover->SetScalarParameterValue(TEXT("StormClouds"), 0.4f * V.Cloud);
			W->SpawnActor<AVolumetricCloud>()->FindComponentByClass<UVolumetricCloudComponent>()->SetMaterial(Cover);
		}
	}
	ASkyLight* Sky = SpawnSky(W, V.bNight);
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
	Sky->GetLightComponent()->RecaptureSky();
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
	PP->Settings.AutoExposureBias = -Exposure;

	// Broadcast bloom: restrained, clean glare without blowing out highlights
	PP->Settings.bOverride_BloomIntensity = true;
	PP->Settings.BloomIntensity = V.bNight ? 0.38f : 0.22f;
	PP->Settings.bOverride_BloomThreshold = true;
	PP->Settings.BloomThreshold = 1.25f;

	SetAmbientOcclusion(PP->Settings);

	// Local Exposure: rescues helmet-shadowed faces and deep chin/neck shadows naturally
	PP->Settings.bOverride_LocalExposureHighlightContrastScale = true;
	PP->Settings.LocalExposureHighlightContrastScale = 0.85f;
	PP->Settings.bOverride_LocalExposureShadowContrastScale = true;
	PP->Settings.LocalExposureShadowContrastScale = 0.50f;
	PP->Settings.bOverride_LocalExposureDetailStrength = true;
	PP->Settings.LocalExposureDetailStrength = 0.45f;

	// Film tonemapper curve: preserves highlights, prevents crushed blacks
	PP->Settings.bOverride_FilmSlope = true;
	PP->Settings.FilmSlope = 0.88f;
	PP->Settings.bOverride_FilmToe = true;
	PP->Settings.FilmToe = 0.55f;
	PP->Settings.bOverride_FilmShoulder = true;
	PP->Settings.FilmShoulder = 0.92f;
	PP->Settings.bOverride_FilmBlackClip = true;
	PP->Settings.FilmBlackClip = 0.0005f;
	PP->Settings.bOverride_FilmWhiteClip = true;
	PP->Settings.FilmWhiteClip = 0.04f;

	// Balanced broadcast color grading
	PP->Settings.bOverride_ColorSaturation = true;
	PP->Settings.ColorSaturation = FVector4(1.03f, 1.01f, 1.04f, 1.0f);
	PP->Settings.bOverride_ColorContrast = true;
	PP->Settings.ColorContrast = FVector4(1.02f, 1.02f, 1.02f, 1.0f);

	// Restrained motion blur for crisp cricket ball and bat readability
	PP->Settings.bOverride_MotionBlurAmount = true;
	PP->Settings.MotionBlurAmount = 0.12f;
	PP->Settings.bOverride_MotionBlurMax = true;
	PP->Settings.MotionBlurMax = 1.8f;

	const FVector C = PitchCentre();
	// The ground's own materials (Scripts/stadium/make_stadium.sh) draw the grass, the square, the wear, the painted
	// logos and the 30-yard circle, and the pitch's clay, cracks and footmarks; without them, flat colours.
	GrassMaterial = LoadStadiumMaterial(TEXT("M_Grass"));
	UMaterialInterface* PitchMaterial = LoadStadiumMaterial(TEXT("M_Pitch"));
	AStaticMeshActor* Ground = Spawn(CylinderMesh, FVector(C.X, 0.f, -0.06f), FVector(800.f, 800.f, 0.1f), Grass);
	if (GrassMaterial) Ground->GetStaticMeshComponent()->SetMaterial(0, GrassMaterial);
	AStaticMeshActor* Pitch = Spawn(CubeMesh, FVector(C.X, 0.f, -0.005f), FVector(PitchLength + 2.4f, 2.f * PitchHalfWidth, 0.01f), Strip);
	if (PitchMaterial)
	{
		// The venue's kind of pitch and its wear, and a canvas for the marks this match leaves on it
		// (DrawPitchMarks), which the material spreads over the strip: 23 m by 3.2 m round its middle.
		PitchFace = UMaterialInstanceDynamic::Create(PitchMaterial, this);
		PitchFace->SetScalarParameterValue(TEXT("Green"), V.Pitch == EPitchType::Green ? 1.f : 0.f);
		PitchFace->SetScalarParameterValue(TEXT("Dust"), V.Pitch == EPitchType::Dusty ? 1.f : 0.f);
		PitchFace->SetScalarParameterValue(TEXT("Wear"), V.Wear);
		PitchFace->SetScalarParameterValue(TEXT("RoughSpots"), CricketBall::Conditions(V.Pitch, V.Wear).Rough);
		MarksTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, 1024, 128, RTF_RGBA8);
		UKismetRenderingLibrary::ClearRenderTarget2D(this, MarksTarget, FLinearColor::Black);
		PitchFace->SetTextureParameterValue(TEXT("Marks"), MarksTarget);
		PitchFace->SetScalarParameterValue(TEXT("MarksOn"), 1.f);
		Pitch->GetStaticMeshComponent()->SetMaterial(0, PitchFace);
	}
	for (float X : { 0.f, PitchLength })
	{
		const float Dir = X == 0.f ? 1.f : -1.f;
		Spawn(CubeMesh, FVector(X + Dir * PoppingCrease, 0.f, 0.001f), FVector(0.05f, 3.66f, 0.004f), White);
		Spawn(CubeMesh, FVector(X, 0.f, 0.001f), FVector(0.05f, 2.64f, 0.004f), White);
		for (float Y : { -1.32f, 1.32f }) Spawn(CubeMesh, FVector(X + Dir * 0.6f, Y, 0.001f), FVector(2.44f, 0.05f, 0.004f), White);
		for (float Y : { -StumpsHalfWidth + 0.018f, 0.f, StumpsHalfWidth - 0.018f })
		{
			AStaticMeshActor* Stump = Spawn(CylinderMesh, FVector(X, Y, StumpHeight * 0.5f), FVector(0.036f, 0.036f, StumpHeight), Wood);
			if (Stump) Stump->GetStaticMeshComponent()->SetCastContactShadow(true);
		}
	}
	BuildStadium(); // with the rope
	for (int32 I = 0; I < 72 && !GrassMaterial; ++I) // the 30-yard circle
	{
		const float A = 2.f * PI * I / 72.f;
		Spawn(CylinderMesh, FVector(C.X + 27.4f * FMath::Cos(A), 27.4f * FMath::Sin(A), 0.01f), FVector(0.25f, 0.25f, 0.02f), White);
	}

	// Sightscreens behind both ends on the line of the pitch: black, so the batter (and the viewer) sees the white
	// ball against them, in a grey frame on legs.
	for (const float Dir : { -1.f, 1.f })
	{
		const float X = C.X + Dir * (BoundaryRadius + 4.f);
		Spawn(CubeMesh, FVector(X, 0.f, 3.4f), FVector(0.6f, 16.4f, 5.6f), FLinearColor(0.2f, 0.2f, 0.21f));
		Spawn(CubeMesh, FVector(X - Dir * 0.32f, 0.f, 3.4f), FVector(0.05f, 15.6f, 4.9f), FLinearColor(0.015f, 0.015f, 0.018f));
		for (const float Y : { -7.f, 0.f, 7.f }) Spawn(CubeMesh, FVector(X, Y, 0.3f), FVector(0.3f, 0.3f, 0.6f), FLinearColor(0.2f, 0.2f, 0.21f));
	}

	Ball = Spawn(SphereMesh, FVector(0.f, 0.f, -5.f), FVector(2.f * BallRadius), FLinearColor(0.92f, 0.92f, 0.88f));
	if (Ball)
	{
		Ball->GetStaticMeshComponent()->SetCastShadow(true);
		Ball->GetStaticMeshComponent()->SetCastContactShadow(true);
	}
	// Bats: a blade and a taped handle, the two parts placed together each frame. The willow bat made by
	// Scripts/metahuman/make_kit.sh, when there is one, is the whole bat in one mesh (grain, grip and stickers),
	// with its origin at the middle of the blade like the box's, and the handle part is hidden.
	UStaticMesh* BatMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/MetaHumans/Kit/SM_Bat.SM_Bat"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UMaterialInterface* Willow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaHumans/Kit/M_Bat.M_Bat"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	for (TObjectPtr<AStaticMeshActor>* B : { &Bat, &NonStrikerBat })
	{
		*B = Spawn(CubeMesh, FVector::ZeroVector, FVector(0.108f, 0.045f, CricketPose::BatLength - CricketPose::HandleLength), Wood);
		if (*B) (*B)->GetStaticMeshComponent()->SetCastContactShadow(true);
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
	WalkAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd.MF_Unarmed_Walk_Fwd"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	// A sprint from the Mixamo library (Scripts/anim), when imported; the jog is sped up otherwise.
	SprintAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Anims/Mocap/Run_Sprint.Run_Sprint"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	// The fielders' overarm throw and dives, from the same library; the IK throw and a tilt play otherwise.
	ThrowAnim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Anims/Mocap/Field_Throw.Field_Throw"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	DiveAnims[0] = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Anims/Mocap/Field_Dive.Field_Dive"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	DiveAnims[1] = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Anims/Mocap/Field_Dive_Left.Field_Dive_Left"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	// The bowling actions, authored from the broadcast reference (Scripts/anim/author_bowl.py).
	static const TCHAR* Kinds[] = { TEXT("Pace"), TEXT("OffSpin"), TEXT("LegSpin") };
	for (int32 K = 0; K < 3; ++K)
		for (int32 H = 0; H < 2; ++H)
		{
			const FString Name = FString::Printf(TEXT("Bowl_%s_%s"), Kinds[K], H == 0 ? TEXT("R") : TEXT("L"));
			BowlAnims[2 * K + H] = LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Anims/Bowling/%s.%s"), *Name, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
		}
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
	// Fielders cycle the five faces not batting or bowling, so the striker, non-striker and bowler never meet their
	// own double on the field; the keeper (fielder 0) stays the Anchor, whose keeper gear is fitted.
	for (int32 I = 0; I < Fielders.Num(); ++I) AddBody(Fielders[I], Players[3 + I % (UE_ARRAY_COUNT(Players) - 3)]);

	Camera = W->SpawnActor<ACameraActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
}

void ASuperOverGameMode::ApplyQuality()
{
	Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
	Levels.SetFromSingleQualityLevel(Quality);
	// Maintain native 100% internal resolution for Quality >= 2 (High & Epic); 90% for Balanced (1); 75% for Perf (0)
	Levels.ResolutionQuality = Quality >= 2 ? 100.f : (Quality == 1 ? 90.f : 75.f);
	Scalability::SetQualityLevels(Levels);
	const bool bMobile = PLATFORM_IOS || PLATFORM_ANDROID;
	// Lumen: Epic only (desktop preview). High/Medium/Low use directional sun + sky light + screen-space reflections,
	// which preserves the realistic broadcast look without GPU stalls or frame drops.
	const bool bLumen = !bMobile && Quality >= 3;
	if (IConsoleVariable* GI = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod")))
		GI->Set(bLumen ? 1 : 0, ECVF_SetByCode);
	if (IConsoleVariable* Refl = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ReflectionMethod")))
		Refl->Set(bLumen ? 1 : (Quality >= 1 ? 2 : 0), ECVF_SetByCode);
	// TSR (3) for Epic (Quality 3); responsive, crisp TAA (2) for High/Medium with restrained sharpening
	if (IConsoleVariable* AA = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod")))
		AA->Set(Quality >= 3 ? 3 : 2, ECVF_SetByCode);
	if (IConsoleVariable* Shp = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Tonemapper.Sharpen")))
		Shp->Set(0.35f, ECVF_SetByCode);
	if (IConsoleVariable* TaaWeight = IConsoleManager::Get().FindConsoleVariable(TEXT("r.TemporalAACurrentFrameWeight")))
		TaaWeight->Set(0.25f, ECVF_SetByCode);
	// Virtual Shadow Maps (VSM): Epic only (Quality >= 3). Medium/High use standard CSM with caching for smooth 60fps.
	if (IConsoleVariable* VSM = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable")))
		VSM->Set(!bMobile && Quality >= 3 ? 1 : 0, ECVF_SetByCode);
	if (IConsoleVariable* CSMCache = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.CSMCaching")))
		CSMCache->Set(1, ECVF_SetByCode);
	if (IConsoleVariable* ShadowRes = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.MaxResolution")))
		ShadowRes->Set(Quality >= 2 ? 2048 : (Quality == 1 ? 1024 : 512), ECVF_SetByCode);
	if (IConsoleVariable* CS = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ContactShadows")))
		CS->Set(Quality >= 2 ? 1 : 0, ECVF_SetByCode);
	if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
		MaxFps->Set(Quality == 0 ? 30 : 60, ECVF_SetByCode);
	ApplyHairQuality();
	UE_LOG(LogCRICKET26, Display, TEXT("Quality %d (ResolutionQuality=%.0f%%)"), Quality, Levels.ResolutionQuality);
}

void ASuperOverGameMode::SetAmbientOcclusion(FPostProcessSettings& Settings)
{
	// Grounds players, equipment, pitch and boundary boards. Screen-space AO only draws below Quality 2 (Lumen
	// replaces it); at 120 cm it took the head and hat brim as occluders over the whole face and turned every
	// shaded face black.
	Settings.bOverride_AmbientOcclusionIntensity = true;
	Settings.AmbientOcclusionIntensity = 0.65f;
	Settings.bOverride_AmbientOcclusionRadius = true;
	Settings.AmbientOcclusionRadius = 30.f;
}

void ASuperOverGameMode::ApplyHairQuality()
{
	// Strands stay off on every tier (the cost). r.HairStrands.Enable is the whole groom system, cards and meshes
	// too: switching it off once left every player bald, without eyebrows, lashes or beards.
	if (IConsoleVariable* Groom = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HairStrands.Enable"))) Groom->Set(1, ECVF_SetByCode);
	if (IConsoleVariable* Strands = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HairStrands.Strands"))) Strands->Set(0, ECVF_SetByCode);
	if (IConsoleVariable* Cards = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HairStrands.Cards"))) Cards->Set(1, ECVF_SetByCode);
	if (IConsoleVariable* Meshes = IConsoleManager::Get().FindConsoleVariable(TEXT("r.HairStrands.Meshes"))) Meshes->Set(1, ECVF_SetByCode);
}

UMaterialInterface* ASuperOverGameMode::LoadStadiumMaterial(const TCHAR* Name)
{
	return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Stadium/%s.%s"), Name, Name), nullptr, LOAD_Quiet | LOAD_NoWarn);
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
	Spec.Scheme = VenueIndex;
	UMaterialInterface* LedMaterial = LoadStadiumMaterial(TEXT("M_LED"));
	Spec.bLedBoards = LedMaterial != nullptr;
	// The 3D crowd when its assets are built (Scripts/stadium/make_stadium.sh), otherwise blocks.
	UStaticMesh* FanMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Stadium/SM_Fan.SM_Fan"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UMaterialInterface* FanMaterial = LoadStadiumMaterial(TEXT("M_Crowd"));
	Spec.bFanCrowd = FanMesh && FanMaterial;
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
	// The floodlights: at night their lamps glare and each tower throws a spot light over the whole field.
	UMaterialInterface* LampMaterial = VertexColourMaterial;
	if (CricketStadium::Venue(VenueIndex).bNight)
	{
		UMaterialInterface* Glow = LoadStadiumMaterial(TEXT("M_Screen"));
		UTexture* Blank = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
		if (Glow && Blank)
		{
			UMaterialInstanceDynamic* Lit = UMaterialInstanceDynamic::Create(Glow, this);
			Lit->SetTextureParameterValue(TEXT("Screen"), Blank);
			Lit->SetScalarParameterValue(TEXT("Glow"), 40000.f);
			LampMaterial = Lit;
		}
		const FVector Middle = CricketGeo::PitchCentre();
		for (int32 I = 0; I < Stadium.Floodlights.Num(); ++I)
		{
			const FVector& Bank = Stadium.Floodlights[I];
			// Aimed a little short of the middle, so the near side of the field is as bright as the far side.
			const FVector Aim = Middle + (Bank - Middle).GetSafeNormal2D() * 12.f;
			ASpotLight* Flood = GetWorld()->SpawnActorDeferred<ASpotLight>(ASpotLight::StaticClass(), FTransform::Identity);
			USpotLightComponent* L = Flood->SpotLightComponent;
			L->SetMobility(EComponentMobility::Movable);
			L->SetIntensityUnits(ELightUnits::Candelas);
			// NightLux over the four towers at the middle: E = I / d^2.
			L->SetIntensity(NightLux / 4.f * FVector::DistSquared(Bank, Middle));
			L->SetLightColor(FLinearColor(1.f, 0.96f, 0.9f));
			L->SetAttenuationRadius(30000.f);
			L->SetOuterConeAngle(40.f);
			L->SetInnerConeAngle(28.f);
			// Every tower's shadow on Epic; High keeps two opposite towers' (a shadowed light costs about 3 ms of GPU).
			L->SetCastShadows(Quality >= 3 || (Quality == 2 && I % 2 == 0));
			L->ContactShadowLength = 0.05f;
			L->ContactShadowLengthInWS = true;
			L->ContactShadowCastingIntensity = 1.0f;
			Flood->FinishSpawning(FTransform::Identity);
			// Set after spawning, as with the sun: a spawn rotation is added to the light's own built-in tilt.
			Flood->SetActorLocationAndRotation(ToWorld(Bank), (Aim - Bank).Rotation());
		}
	}
	Place(Stadium.Lamps, 0.f, LampMaterial)->GetStaticMeshComponent()->SetCastShadow(false);
	Place(Stadium.Boards, 0.f, LedMaterial ? LedMaterial : VertexColourMaterial.Get());
	// Without the grass material the stripes are meshes, in the shape material: the vertex colour material's sheen
	// washes out a flat field seen at a grazing angle.
	for (int32 I = 0; I < 2 && !GrassMaterial; ++I) Paint(Place(Stadium.Outfield[I], -0.8f, ShapeMaterial), CricketStadium::StripeColours[I]); // under the strip's top and the creases
	if (Spec.bFanCrowd)
	{
		// One instanced mesh for the whole crowd; each fan's look and rhythm go in its custom data, and the material
		// moves them all (Excite, from UpdateCrowd).
		AActor* Holder = GetWorld()->SpawnActor<AActor>();
		UHierarchicalInstancedStaticMeshComponent* Fans = NewObject<UHierarchicalInstancedStaticMeshComponent>(Holder);
		Holder->SetRootComponent(Fans);
		Fans->SetStaticMesh(FanMesh);
		CrowdMaterial = UMaterialInstanceDynamic::Create(FanMaterial, this);
		Fans->SetMaterial(0, CrowdMaterial);
		// Fill light on the crowd, a few percent of the venue's light (see M_Crowd).
		const CricketStadium::FVenue& V = CricketStadium::Venue(VenueIndex);
		const float Fill = 0.06f / PI * (V.bNight ? NightLux : V.Cloud > 0.5f ? SunLux * (1.f - 0.7f * V.Cloud) : SunLux);
		CrowdMaterial->SetScalarParameterValue(TEXT("Fill"), Fill);
		Fans->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Fans->SetCastShadow(false);
		Fans->NumCustomDataFloats = 5;
		Fans->RegisterComponent();
		TArray<FTransform> Seats;
		Seats.Reserve(Stadium.Fans.Num());
		for (const CricketStadium::FFan& F : Stadium.Fans) Seats.Add(FTransform(FRotator(0.f, F.Yaw, 0.f), ToWorld(F.Pos), FVector(F.Scale)));
		Fans->AddInstances(Seats, false);
		for (int32 I = 0; I < Stadium.Fans.Num(); ++I)
		{
			const CricketStadium::FFan& F = Stadium.Fans[I];
			Fans->SetCustomData(I, { F.Shirt.R, F.Shirt.G, F.Shirt.B, F.Skin, F.Phase });
		}
		// Their flags, one more instanced mesh: colour and rhythm per flag, and the material waves them.
		UMaterialInterface* FlagBase = LoadStadiumMaterial(TEXT("M_Flag"));
		if (FlagBase && !Stadium.Flags.IsEmpty())
		{
			UHierarchicalInstancedStaticMeshComponent* Flags = NewObject<UHierarchicalInstancedStaticMeshComponent>(Holder);
			FlagMaterial = UMaterialInstanceDynamic::Create(FlagBase, this);
			FlagMaterial->SetScalarParameterValue(TEXT("Fill"), Fill);
			Flags->SetStaticMesh(CricketStadium::ToStaticMesh(Holder, CricketStadium::Flag(), FlagMaterial));
			Flags->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Flags->SetCastShadow(false);
			Flags->NumCustomDataFloats = 4;
			Flags->SetupAttachment(Fans);
			Flags->RegisterComponent();
			TArray<FTransform> Poles;
			for (const CricketStadium::FFan& F : Stadium.Flags) Poles.Add(FTransform(FRotator(0.f, F.Yaw, 0.f), ToWorld(F.Pos), FVector(F.Scale)));
			Flags->AddInstances(Poles, false);
			for (int32 I = 0; I < Stadium.Flags.Num(); ++I)
			{
				const CricketStadium::FFan& F = Stadium.Flags[I];
				Flags->SetCustomData(I, { F.Shirt.R, F.Shirt.G, F.Shirt.B, F.Phase });
			}
		}
	}
	for (const CricketStadium::FColouredMesh& Section : Stadium.Crowd)
	{
		if (Section.Tri.IsEmpty()) continue;
		AStaticMeshActor* A = Place(Section, 0.f, VertexColourMaterial);
		A->GetStaticMeshComponent()->SetCastShadow(false); // ponytail: the stand's own shadow reads fine; a crowd's is thousands of tiny casters
		CrowdSections.Add(A);
	}
	// The big screens: a plane on each face showing one canvas the game draws the score into (UpdateBigScreens).
	UMaterialInterface* ScreenMaterial = LoadStadiumMaterial(TEXT("M_Screen"));
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (ScreenMaterial && Plane)
	{
		ScreenTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, 512, 256, RTF_RGBA8_SRGB);
		UMaterialInstanceDynamic* Face = UMaterialInstanceDynamic::Create(ScreenMaterial, this);
		Face->SetTextureParameterValue(TEXT("Screen"), ScreenTarget);
		for (const CricketStadium::FScreen& Sc : Stadium.Screens)
		{
			// The plane is 1 m square facing up, its texture across X: turn its face to the field, its X to the right.
			const FVector Right = FVector::CrossProduct(FVector::UpVector, -Sc.Facing);
			const FRotator Orient = FRotationMatrix::MakeFromZX(Sc.Facing, Right).Rotator();
			AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(ToWorld(Sc.Centre + Sc.Facing * 0.05f), Orient);
			UStaticMeshComponent* C = A->GetStaticMeshComponent();
			C->SetMobility(EComponentMobility::Movable);
			C->SetStaticMesh(Plane);
			C->SetMaterial(0, Face);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCastShadow(false);
			A->SetActorScale3D(FVector(Sc.Width, Sc.Height, 1.f));
		}
	}
	int32 Tris = Stadium.Structure.NumTriangles() + Stadium.Boards.NumTriangles();
	if (!GrassMaterial) Tris += Stadium.Outfield[0].NumTriangles() + Stadium.Outfield[1].NumTriangles();
	for (const CricketStadium::FColouredMesh& Section : Stadium.Crowd) Tris += Section.NumTriangles();
	UE_LOG(LogCRICKET26, Display, TEXT("Stadium: %d spectators (%s), %d flags, %d triangles besides, %d meshes, %s materials"), Stadium.Spectators,
		Spec.bFanCrowd ? TEXT("3D") : TEXT("blocks"), FlagMaterial ? Stadium.Flags.Num() : 0, Tris, (GrassMaterial ? 2 : 4) + (Spec.bFanCrowd ? 1 : Stadium.Crowd.Num()),
		LedMaterial ? TEXT("stadium") : TEXT("flat"));
}

void ASuperOverGameMode::UpdateCrowd()
{
	// The crowd gets up and jumps as the noise swells on a boundary or wicket, and sits back down with it.
	const float Excitement = FMath::Clamp((CrowdLevel - 0.4f) / 0.4f, 0.f, 1.f);
	if (CrowdMaterial) CrowdMaterial->SetScalarParameterValue(TEXT("Excite"), Excitement);
	if (FlagMaterial) FlagMaterial->SetScalarParameterValue(TEXT("Excite"), Excitement);
	const float T = GetWorld()->GetTimeSeconds();
	for (int32 I = 0; I < CrowdSections.Num(); ++I)
	{
		const float Z = CricketStadium::JumpHeight(T, Excitement, I / CricketStadium::NumGroups, I % CricketStadium::NumGroups);
		CrowdSections[I]->SetActorLocation(FVector(0.f, 0.f, Z * 100.f));
	}
}

void ASuperOverGameMode::UpdateBigScreens()
{
	if (!ScreenTarget) return;
	// The batting side and its score, the overs, then the chase or the replay; redrawn only when that changes.
	const FInningsState& In = Match.Cur();
	const FCricketTeam& Batting = Teams[Match.BattingTeam()];
	const FString Score = FString::Printf(TEXT("%s  %d-%d"), *Batting.Short, In.Runs, In.Wickets);
	const FString Overs = FString::Printf(TEXT("OVERS  %d.%d"), In.LegalBalls / 6, In.LegalBalls % 6);
	const FString Foot = IsReplaying() ? FString(TEXT("REPLAY")) : Match.Target > 0 ? FString::Printf(TEXT("TARGET  %d"), Match.Target) : FString(CricketStadium::Venue(VenueIndex).Name);
	const FString Shown = Score + Overs + Foot;
	if (Shown == ScreenShown) return;
	ScreenShown = Shown;

	UKismetRenderingLibrary::ClearRenderTarget2D(this, ScreenTarget, FLinearColor(0.004f, 0.006f, 0.02f));
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, ScreenTarget, Canvas, Size, Context);
	UFont* Font = GEngine->GetLargeFont();
	auto Line = [&](const FString& S, float Y, float Scale, const FLinearColor& Colour)
	{
		float W, H;
		Canvas->TextSize(Font, S, W, H, Scale, Scale);
		FCanvasTextItem Item(FVector2D((Size.X - W) / 2.f, Y), FText::FromString(S), Font, Colour);
		Item.Scale = FVector2D(Scale, Scale);
		Canvas->DrawItem(Item);
	};
	FCanvasTileItem Band(FVector2D(0.f, 0.f), FVector2D(Size.X, Size.Y * 0.44f), Batting.Colour * 0.8f);
	Canvas->DrawItem(Band);
	Line(Score, Size.Y * 0.03f, 4.2f, FLinearColor::White);
	Line(Overs, Size.Y * 0.47f, 2.6f, FLinearColor::White);
	Line(Foot, Size.Y * 0.72f, 2.6f, FLinearColor(1.f, 0.78f, 0.2f));
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

FPitchConditions ASuperOverGameMode::Conditions() const
{
	const CricketStadium::FVenue& V = CricketStadium::Venue(VenueIndex);
	return CricketBall::Conditions(V.Pitch, V.Wear + 0.02f * Marks.Num(), V.Cloud, V.bNight);
}

void ASuperOverGameMode::DrawPitchMarks(const FBallMark& Mark)
{
	UTexture2D* Stamp = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Stadium/T_Mark.T_Mark"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	if (!MarksTarget || !Stamp) return;
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, MarksTarget, Canvas, Size, Context);
	// A stamp Length by Width metres at a point on the pitch (simulation metres), its length turned by Angle degrees.
	auto Stamp2 = [&](const FVector2D& At, float Length, float Width, float Angle, float Strength)
	{
		const FVector2D Px((At.X - CricketGeo::PitchCentre().X) / 23.f + 0.5f, At.Y / 3.2f + 0.5f);
		const FVector2D Extent(Length / 23.f * Size.X, Width / 3.2f * Size.Y);
		FCanvasTileItem Item(Px * Size - Extent / 2.f, Stamp->GetResource(), Extent, FLinearColor(1.f, 1.f, 1.f, Strength));
		Item.BlendMode = SE_BLEND_Translucent;
		Item.Rotation = FRotator(0.f, Angle, 0.f);
		Item.PivotPoint = FVector2D(0.5f, 0.5f);
		Canvas->DrawItem(Item);
	};
	if (Mark.bPitched && FMath::Abs(Mark.Pitch.Y) < 1.6f) Stamp2(Mark.Pitch, 0.09f, 0.08f, 0.f, 0.7f);
	// The bowler's front foot lands just behind the popping crease on the bowling arm's side, then the follow-through
	// strides away toward the off side of the pitch.
	FRandomStream Feet(Ctx.Seed);
	const float Arm = BowlerPlayer().BowlHand == ECricketHand::Right ? 1.f : -1.f;
	const float FrontX = CricketGeo::PitchLength - CricketGeo::PoppingCrease + 0.15f + Feet.FRandRange(-0.12f, 0.2f);
	Stamp2(FVector2D(FrontX, Arm * Feet.FRandRange(0.05f, 0.3f)), 0.3f, 0.12f, Feet.FRandRange(-15.f, 15.f), 0.5f);
	for (int32 Stride = 1; Stride <= 3; ++Stride)
		Stamp2(FVector2D(FrontX - 1.5f * Stride, Arm * (0.25f + 0.35f * Stride + Feet.FRandRange(-0.15f, 0.15f))), 0.28f, 0.11f,
			Arm * 25.f + Feet.FRandRange(-10.f, 10.f), 0.35f);
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
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
	// Keeper depth by bowler type AND stock pace (isolated to the keeper slot):
	// express pace stands deepest, medium/slower pace an intermediate step up,
	// spin stands up. Aligned behind the striker's wicket for this handedness.
	if (Ctx.Field.Num() > 0 && Ctx.Field[0].bKeeper)
		Ctx.Field[0].Home = CricketKeeper::KeeperHome(Bwl.BowlerType, Bwl.PaceKph, Batter.BatHand);
	if (HumanBowls()) ApplyHumanField(Ctx.Field, Batter.BatHand);
	Ctx.bFreeHit = Match.bFreeHit;
	Ctx.Rules = Match.Rules;
	Ctx.BouncersBowled = Match.Cur().Bouncers;
	Ctx.Conditions = Conditions();
	// The guard persists across balls; the stance (and the simulation's pads/reach) moves with it.
	// Only a human striker uses it - the AI holds middle.
	StrikerGuard = CricketControl::ClampGuard(StrikerGuard, ControlTuning);
	Ctx.StrikerGuard = HumanBats() ? StrikerGuard : 0.f;

	const float Off = OffSideSign(Batter.BatHand);
	const float Arm = Bwl.BowlHand == ECricketHand::Right ? 1.f : -1.f;
	const FLinearColor BatCol = Teams[Match.BattingTeam()].Colour, FieldCol = Teams[Match.BowlingTeam()].Colour;
	const FVector Home = StrikerHome();
	Striker->SetActorLocation(ToWorld(FVector(Home.X, Home.Y, 0.9f)));
	Striker->SetActorRotation(FRotator(0.f, Off > 0.f ? 90.f : -90.f, 0.f));
	// Non-striker backs up wide of the pitch, clear of the stumps and the umpire's eyeline,
	// like a real game: a full stride behind the popping crease and well to the side.
	NonStriker->SetActorLocation(ToWorld(FVector(CricketGeo::PitchLength - 2.0f, -1.9f * Arm, 0.9f)));
	NonStriker->SetActorRotation(FRotator(0.f, 180.f, 0.f));
	Bowler->SetActorLocation(ToWorld(FVector(RunUpX(0.f), 0.5f * Arm, 0.925f)));
	Bowler->SetActorRotation(FRotator(0.f, 180.f, 0.f));
	// Umpires like a real match: the bowler's-end umpire stands behind the stumps (UmpireBack) almost in line with
	// them, a little to the side away from the bowler's arm, so the run-up passes clear on one side and the
	// non-striker backs up well wide on the other; square leg level with the popping crease on the leg side.
	Umpires[0]->SetActorLocation(ToWorld(FVector(CricketGeo::PitchLength + UmpireBack, -UmpireSide * Arm, 0.9f)));
	Umpires[0]->SetActorRotation(FRotator(0.f, 180.f, 0.f));
	Umpires[1]->SetActorLocation(ToWorld(FVector(CricketGeo::PoppingCrease, -15.f * Off, 0.9f)));
	{
		const FVector ToPitch = Striker->GetActorLocation() - Umpires[1]->GetActorLocation();
		if (!FVector2D(ToPitch).IsNearlyZero())
			Umpires[1]->SetActorRotation(FRotator(0.f, ToPitch.Rotation().Yaw, 0.f));
	}
	for (AStaticMeshActor* U : Umpires) Paint(U, FLinearColor(0.14f, 0.16f, 0.18f)); // charcoal slate keeps officiating cloth readable under match light
	// Shirt numbers: the squad's own, falling back to a stable hash of the name for sides without one.
	auto Shirt = [](const FCricketPlayer& P) { return P.Number > 0 ? P.Number : 1 + static_cast<int32>(GetTypeHash(P.Name) % 99); };
	Paint(Striker, BatCol, &Teams[Match.BattingTeam()], Batter.Name, Shirt(Batter));
	Paint(NonStriker, BatCol, &Teams[Match.BattingTeam()], Ctx.NonStriker.Name, Shirt(Ctx.NonStriker));
	Paint(Bowler, FieldCol, &Teams[Match.BowlingTeam()], Bwl.Name, Shirt(Bwl));
	for (int32 I = 0; I < Fielders.Num(); ++I)
	{
		const bool bUsed = Ctx.Field.IsValidIndex(I) && !Ctx.Field[I].bBowler;
		Fielders[I]->SetActorHiddenInGame(!bUsed);
		if (USkeletalMeshComponent* Body = BodyOf(Fielders[I]); Body && Body->GetOwner() != Fielders[I]) Body->GetOwner()->SetActorHiddenInGame(!bUsed);
		if (!bUsed) continue;
		Fielders[I]->SetActorLocation(ToWorld(FVector(Ctx.Field[I].Home.X, Ctx.Field[I].Home.Y, 0.9f)));
		const FVector FielderLook = Ctx.Field[I].bKeeper ? FVector(1.f, 0.f, 0.f) : (Striker->GetActorLocation() - Fielders[I]->GetActorLocation());
		if (!FVector2D(FielderLook).IsNearlyZero())
			Fielders[I]->SetActorRotation(FRotator(0.f, FielderLook.Rotation().Yaw, 0.f));
		Paint(Fielders[I], Ctx.Field[I].bKeeper ? FieldCol * 0.5f : FieldCol, &Teams[Match.BowlingTeam()], FString(), 10 + I);
	}
	Ball->SetActorLocation(ToWorld(FVector(RunUpX(0.f), 0.5f * Arm, 1.2f)));
	BatInput = FBatInput();
	Result = FDeliveryResult();
	BowlerIntent.Reset();
	// A new delivery is a new control state: no pending run calls, no held pull, no stale gesture.
	HumanRunCalls = FRunCalls();
	LiveAim = FShotAim();
	bBatPullActive = false;
	LastTimingGrade = ETimingGrade::None;
	LastRunState = ERunState::AtCrease;
	ShotDirection = 0.f;
	TouchGesture.Finger = INDEX_NONE;
}

bool ASuperOverGameMode::ApplyHumanField(TArray<FFielder>& Field, ECricketHand BatHand) const
{
	if (HumanFieldRH.Num() != Field.Num()) return false;
	TArray<FFielder> Set = Field;
	const float Off = OffSideSign(BatHand);
	for (int32 I = 0; I < Set.Num(); ++I)
	{
		if (Set[I].bKeeper || Set[I].bBowler) continue;
		Set[I].Home = FVector2D(HumanFieldRH[I].X, HumanFieldRH[I].Y * Off);
		Set[I].Position = CricketField::PositionName(Set[I].Home, BatHand);
	}
	if (!CricketField::Validate(Set, BatHand).IsEmpty()) return false;
	Field = MoveTemp(Set);
	return true;
}

FVector ASuperOverGameMode::StrikerHome() const
{
	const float Off = OffSideSign(StrikerPlayer().BatHand);
	// Only a human striker shuffles; the AI holds middle guard.
	const float Guard = HumanBats() ? CricketControl::ClampGuard(StrikerGuard, ControlTuning) : 0.f;
	return FVector(0.9f, (-0.35f + Guard) * Off, 0.f);
}

FVector ASuperOverGameMode::TargetMarkerLocation() const
{
	return TargetMarker ? TargetMarker->GetActorLocation() : FVector::ZeroVector;
}

ASuperOverGameMode::FLiveField ASuperOverGameMode::LiveField() const
{
	auto At = [](const AActor* A) { return FVector2D(A->GetActorLocation() / 100.f); };
	FLiveField L;
	for (int32 I = 0; I < Ctx.Field.Num(); ++I)
	{
		const AActor* Who = Ctx.Field[I].bBowler ? Bowler.Get() : Fielders.IsValidIndex(I) ? Fielders[I].Get() : nullptr;
		L.Field.Add(Who ? At(Who) : Ctx.Field[I].Home);
	}
	if (Striker) L.Striker = At(Striker);
	if (NonStriker) L.NonStriker = At(NonStriker);
	L.bBall = Ball && !Ball->IsHidden() && DPhase == EDeliveryPhase::BallInPlay;
	if (L.bBall) L.Ball = At(Ball);
	return L;
}

void ASuperOverGameMode::UpdateFieldEdit(const FCricketControls& C)
{
	auto Buzz = [this](float Strength) { Haptic(Strength); };
	if (!bFieldEdit)
	{
		if (C.bFieldOpen && CanEditField())
		{
			bFieldEdit = true;
			EditField = Ctx.Field;
			EditPick = INDEX_NONE;
			EditPreset = INDEX_NONE;
			EditError.Reset();
			Buzz(0.2f);
		}
		return;
	}
	// Locked the moment the ball can no longer be set for: the run-up, a replay, the end of the innings.
	if (!CanEditField()) { bFieldEdit = false; EditPick = INDEX_NONE; return; }

	const ECricketHand Hand = StrikerPlayer().BatHand;
	auto LoadOutfield = [&](EFieldPreset Preset)
	{
		const TArray<FFielder> From = CricketField::Make(Preset, Hand, BowlerPlayer().BowlHand);
		for (int32 I = 0; I < EditField.Num() && I < From.Num(); ++I)
			if (!EditField[I].bKeeper && !EditField[I].bBowler) EditField[I] = From[I];
		EditPick = INDEX_NONE;
		EditPreset = int32(Preset);
		Buzz(0.2f);
	};
	if (C.FieldPreset >= 0 && C.FieldPreset < CricketTouch::NumFieldPresets) LoadOutfield(EFieldPreset(C.FieldPreset));
	if (C.bFieldReset) LoadOutfield(CricketField::PresetFor(BowlerPlayer().BowlerType));
	if (C.bFieldCancel) { bFieldEdit = false; EditPick = INDEX_NONE; return; }
	if (C.bFieldApply)
	{
		const FString Why = CricketField::Validate(EditField, Hand);
		if (!Why.IsEmpty()) { EditError = Why; EditErrorAt = GetWorld()->GetTimeSeconds(); Buzz(0.6f); return; }
		const float Off = OffSideSign(Hand);
		HumanFieldRH.SetNum(EditField.Num());
		for (int32 I = 0; I < EditField.Num(); ++I) HumanFieldRH[I] = FVector2D(EditField[I].Home.X, EditField[I].Home.Y * Off);
		bFieldEdit = false;
		EditPick = INDEX_NONE;
		PlaceForDelivery();
		Buzz(0.3f);
		UE_LOG(LogCRICKET26, Display, TEXT("Field set by the captain: %s"), *FString::JoinBy(Ctx.Field, TEXT(", "), [](const FFielder& F) { return F.Position; }));
		return;
	}

	// Pick up the nearest outfielder under the finger, carry them, and put them down where the rules allow.
	const FBox2D Map = CricketTouch::FieldMap(ViewAspect, TouchSafe);
	const FVector2D At = CricketTouch::MapToField(Map, C.FieldAt);
	constexpr float Reach = 8.f; // m on the ground: a fingertip on the map
	if (C.bFieldGrab)
	{
		EditPick = INDEX_NONE;
		float Best = Reach;
		for (int32 I = 0; I < EditField.Num(); ++I)
		{
			const float D = FVector2D::Distance(EditField[I].Home, At);
			if (!EditField[I].bKeeper && !EditField[I].bBowler && D < Best) { Best = D; EditPick = I; }
		}
		if (EditPick != INDEX_NONE) Buzz(0.15f);
	}
	if (!EditField.IsValidIndex(EditPick)) return;
	TArray<FFielder> Moved = EditField;
	Moved[EditPick].Home = At;
	EditDrag = At;
	EditDragWhy = CricketField::Validate(Moved, Hand);
	if (C.bFieldDrop)
	{
		if (EditDragWhy.IsEmpty())
		{
			Moved[EditPick].Position = CricketField::PositionName(At, Hand);
			EditField = MoveTemp(Moved);
			EditPreset = INDEX_NONE;
			Buzz(0.25f);
			CoachUsed(CricketTouch::EMode::FieldEdit);
		}
		else
		{
			EditError = EditDragWhy;
			EditErrorAt = GetWorld()->GetTimeSeconds();
			Buzz(0.6f);
		}
		EditPick = INDEX_NONE;
		EditDragWhy.Reset();
	}
}

void ASuperOverGameMode::Tick(float Dt)
{
	Super::Tick(Dt);
	Dt = FMath::Min(Dt, 0.1f);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && !bViewSet)
	{
		PC->SetViewTarget(Camera);
		PC->bShowMouseCursor = true; // desktop plays with the touch controls, the mouse as the finger
		// A drag never hides, locks or recaptures the cursor, so the pull follows the mouse exactly.
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		// The match HUD rides the viewport (the HUD actor's canvas only carries the F1 debug overlay).
		SCricketMatchHUD::AddToGameViewport(this, Cast<ASuperOverHUD>(PC->GetHUD()));
		bViewSet = true;
	}
	if (PC)
	{
		int32 VX = 0, VY = 0;
		PC->GetViewportSize(VX, VY);
		if (VY > 0) ViewAspect = float(VX) / VY;
		// The notch, rounded corners and home bar, in the layout's screen-height units.
		if (VY > 0 && FSlateApplication::IsInitialized())
		{
			FMargin Safe;
			FSlateApplication::Get().GetSafeZoneSize(Safe, FVector2D(VX, VY));
			TouchSafe = { Safe.Left / VY, Safe.Top / VY, Safe.Right / VY, Safe.Bottom / VY };
		}
	}
	if (PC && bTouchScript) RunTouchScript(PC);
	if (PC) HandleInput(PC, Dt);
	if (SlowMo < 1.f) GetWorldSettings()->SetTimeDilation(DPhase == EDeliveryPhase::BallInPlay || DPhase == EDeliveryPhase::RunUp ? SlowMo : 1.f);
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
		// The frame's phase and time, so Scripts/anim/stroke_qa.sh can pick the frames round the stroke.
		UE_LOG(LogCRICKET26, Display, TEXT("Frame %d phase %d time %.3f contact %.3f cam %s at %s"), Shot, int32(DPhase), PhaseTime, Result.ContactTime, *LastCameraDebug,
			*(Camera ? Camera->GetActorLocation() / 100.f : FVector::ZeroVector).ToString());
		FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / FString::Printf(TEXT("Ball%d_%03d.png"), ShotBall, Shot++), true, false);
		++DevLookIndex;
		// -CricketRecordAudio: also write the mixed game audio of the delivery to Saved/BallN.wav.
		if (bRecordAudio && !bRecording) { UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 30.f); bRecording = true; }
	}
	if (UIShotEvery > 0.f && (UIShotClock += Dt) >= UIShotEvery)
	{
		// Named by innings, ball and phase so Scripts/ui_capture.sh can pick each screen out.
		UIShotClock = 0.f;
		static int32 UIShot = 0;
		FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / FString::Printf(TEXT("UI_%04d_i%d_b%d_p%d_m%d.png"), UIShot++,
			Match.CurrentInnings, BallsPlayed, int32(DPhase), int32(Match.Phase)), true, false);
	}
	if (QuitAfter > 0 && BallsPlayed >= QuitAfter && DPhase == EDeliveryPhase::Waiting)
	{
		if (bRecording) UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile, FString::Printf(TEXT("Ball%d"), ShotBall), FPaths::ProjectSavedDir());
		FPlatformMisc::RequestExit(false); // capture done
	}

	switch (DPhase)
	{
	case EDeliveryPhase::Waiting:
		// A human batter calls for the ball with PLAY; a human bowler runs in with BOWL.
		if (Match.Phase == EMatchPhase::ReadyForDelivery && !HumanBowls() && !HumanBats() && PhaseTime > BetweenBalls) BeginRunUp();
		else if (bAutoPlay && PhaseTime > BetweenBalls + 1.3f)
		{
			if (Match.Phase == EMatchPhase::InningsBreak) { Match.StartSecondInnings(); if (IsIPLMatch()) IPLNewInningsSetup(); }
			else if (Match.Phase == EMatchPhase::MatchComplete) { if (!Match.StartNextSuperOver()) { Match.Rules.MaxLegalBalls = SelectedMatchOvers * 6; Match.Rules.MaxWickets = SelectedMatchOvers == 1 ? 2 : 10; Match.Start(HumanTeam); } }
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
		if (InReel())
		{
			if (PhaseTime >= ReplayDelay + ReplayAngleDuration(0)) PlayClip(ReelClip + 1);
		}
		else if (PhaseTime > (bReviewThis ? ReviewFrom() + ReviewTime + 0.5f : AfterDeadBall + ReplayDelay - ReplayDelayMin + (bReplayThis ? ReplayTotalTime() : 0.f)))
		{
			DPhase = EDeliveryPhase::Waiting;
			PhaseTime = 0.f;
			// Return to live (§37): the replay is over, so its state dies here and the camera snaps
			// back to the delivery shot instead of gliding from the close-up. Match state is untouched.
			bCutCamera = true;
			BroadcastDirector.Reset(EBroadcastShot::StandardDelivery);
			if (Match.Phase == EMatchPhase::ReadyForDelivery) PlaceForDelivery();
			else if (Highlights.Num())
			{
				LiveClip = { Result, Ctx, BatInput, ReleaseTiming, Commentary };
				PlayClip(0);
			}
		}
		break;
	}
	UpdatePresentation(Dt);
}

void ASuperOverGameMode::CoachUsed(CricketTouch::EMode Mode)
{
	int32& Uses = CoachUses[uint8(Mode)];
	if (Uses >= CoachRepeats) return;
	GConfig->SetInt(CoachSection, *FString::Printf(TEXT("Mode%d"), uint8(Mode)), ++Uses, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void ASuperOverGameMode::Haptic(float Strength) const
{
	const float S = FMath::Clamp(Strength * ControlTuning.HapticScale, 0.f, 1.f);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && S > 0.f)
		PC->PlayDynamicForceFeedback(S, 0.06f, true, true, true, true);
}

void ASuperOverGameMode::HandleInput(APlayerController* PC, float Dt)
{
	auto Pressed = [PC](const FKey& K) { return PC->WasInputKeyJustPressed(K); };

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
	// Frontend hook (additive): Esc / Android back returns to the menu shell, on the Play screen.
	// IPL: back to the season hub instead; the unplayed fixture stays Upcoming, never half-committed.
	if (Pressed(EKeys::Escape) || Pressed(EKeys::Android_Back))
	{
		if (IsIPLMatch()) { ReturnToIPLHub(); return; }
		UFrontendStatics::OpenFrontend(this, EFrontendTab::Play);
		return;
	}
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

	// The touch UI is the only gameplay input, on every platform: on desktop the mouse is the finger.
	FCricketControls C = ReadTouch(PC);
	UpdateFieldEdit(C);

	// IPL selector: a tapped candidate (touch or mouse) or a number key (desktop) makes the pick.
	if (C.PickIndex >= 0)
	{
		if (bAwaitingBatter) ChooseNextBatter(C.PickIndex);
		else if (bAwaitingBowler) ChooseNextBowler(C.PickIndex);
	}
	if (IsAwaitingPick())
	{
		const FKey Nums[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
			EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero };
		for (int32 I = 0; I < 10; ++I)
			if (Pressed(Nums[I]) && IPLAwaitingCandidates.IsValidIndex(I))
			{
				if (bAwaitingBatter) ChooseNextBatter(I);
				else ChooseNextBowler(I);
				break;
			}
	}

	if (HumanReviews() && (C.bReview || C.bProgress)) SettleReview(C.bReview);
	else if (C.bProgress && InReel())
	{
		PlayClip(INDEX_NONE); // skip the whole reel, back to the scorecard
		C.bProgress = false;
	}
	else if (C.bProgress && IsReplaying()) PhaseTime = ReplayDelay + ReplayTotalTime(); // skip the replay
	if (C.bProgress && DPhase == EDeliveryPhase::Waiting)
	{
		if (Match.Phase == EMatchPhase::InningsBreak)
		{
			Match.StartSecondInnings();
			if (IsIPLMatch()) IPLNewInningsSetup();
		}
		else if (Match.Phase == EMatchPhase::MatchComplete)
		{
			if (IsIPLMatch())
			{
				// Level scores already went to the Super Over decider in AfterIPLDelivery; a
				// decided match with its result committed returns to the season hub.
				if (bIPLCommitted) { ReturnToIPLHub(); return; }
			}
			else if (!Match.StartNextSuperOver()) { Match.Rules.MaxLegalBalls = SelectedMatchOvers * 6; Match.Rules.MaxWickets = SelectedMatchOvers == 1 ? 2 : 10; Match.Start(HumanTeam); }
		}
		PlaceForDelivery();
	}

	// One helper: playing a shot re-resolves deterministically, so everything shown stays identical.
	auto PlayShot = [&](const FBatInput& In, const TCHAR* Via)
	{
		BatInput = In;
		Ctx.RunCalls = HumanRunCalls;
		Result = CricketDelivery::Resolve(Release, BatInput, Ctx);
		LastTimingGrade = CricketControl::GradeShot(Result, BatInput, ControlTuning);
		UE_LOG(LogCRICKET26, Display, TEXT("Shot input (%s): intent %d, direction %.0f, power %.2f, press %.3f s, timing %s"),
			Via, int32(In.Intent), In.DirectionDeg, In.Power, In.PressTime, CricketControl::TimingGradeName(LastTimingGrade));
	};

	if (HumanBowls())
	{
		if (DPhase == EDeliveryPhase::Waiting)
		{
			const TArray<EDeliveryType> Rep = CricketBowling::Repertoire(BowlerPlayer().BowlerType);
			if (Rep.IsValidIndex(C.DeliveryPick) && HumanPlan.Type != Rep[C.DeliveryPick]) { HumanPlan.Type = Rep[C.DeliveryPick]; Haptic(0.15f); }
			if (!Rep.Contains(HumanPlan.Type)) HumanPlan.Type = Rep[0];
			if (!C.TargetDrag.IsNearlyZero())
				CricketControl::DragTarget(HumanPlan, C.TargetDrag, StrikerPlayer().BatHand, ControlTuning);
			if (C.Effort > -1.5f) HumanPlan.Effort = C.Effort;
			if (C.Dial > -1.5f) CricketControl::ApplyDial(HumanPlan, C.Dial, StrikerPlayer().BatHand, ControlTuning);
			CricketControl::ClampTarget(HumanPlan);
			if (C.bAction && Match.Phase == EMatchPhase::ReadyForDelivery) { BeginRunUp(); Haptic(0.2f); CoachUsed(CricketTouch::EMode::Bowling); }
		}
		else if (DPhase == EDeliveryPhase::RunUp && C.bAction)
		{
			const float At = FMath::Min(1.f, -1.f + 2.f * CricketMath::PressTime(PhaseTime, Dt) / RunUpSeconds);
			DoRelease(At);
			Haptic(CricketControl::GradeRelease(At, ControlTuning) == EReleaseGrade::Perfect ? 0.6f : 0.25f);
			CoachUsed(CricketTouch::EMode::Release);
		}
	}
	else if (HumanBats())
	{
		if (C.bAction && DPhase == EDeliveryPhase::Waiting && Match.Phase == EMatchPhase::ReadyForDelivery)
		{
			BeginRunUp();
			Haptic(0.2f);
			CoachUsed(CricketTouch::EMode::Ready);
		}
		// Shot mode around the pull-and-release: the GROUND / LOFT / DEFEND buttons latch it.
		if (C.bDefend) BatMode = EBatIntent::Defend;
		else if (C.bLoft) BatMode = EBatIntent::Loft;
		else if (C.bGround) BatMode = EBatIntent::Ground;

		// Crease guard: shuffle along the crease before the stroke. Touch arrows step, A/D or
		// Left/Right slide. Locked once the shot is played; stumps and wides stay where they are,
		// pads and reach travel with the body.
		if (!BatInput.IsShot() && (DPhase == EDeliveryPhase::Waiting || DPhase == EDeliveryPhase::RunUp || DPhase == EDeliveryPhase::BallInPlay))
		{
			const ECricketHand Hand = StrikerPlayer().BatHand;
			const float Before = StrikerGuard;
			if (C.bGuardLeft || C.bGuardRight)
			{
				StrikerGuard = CricketControl::MoveGuard(StrikerGuard, C.bGuardLeft, C.bGuardRight, Hand, ControlTuning);
				if (!FMath::IsNearlyEqual(Before, StrikerGuard)) Haptic(0.12f);
			}
			const bool bKeyLeft = PC && (PC->IsInputKeyDown(EKeys::Left) || PC->IsInputKeyDown(EKeys::A));
			const bool bKeyRight = PC && (PC->IsInputKeyDown(EKeys::Right) || PC->IsInputKeyDown(EKeys::D));
			if (bKeyLeft != bKeyRight)
			{
				const float Off = OffSideSign(Hand);
				const float Rate = FMath::Max(ControlTuning.GuardStep, 0.01f) * 8.f;
				StrikerGuard = CricketControl::ClampGuard(StrikerGuard + (bKeyLeft ? Rate * Dt * Off : -Rate * Dt * Off), ControlTuning);
			}
			if (!FMath::IsNearlyEqual(Before, StrikerGuard))
			{
				Ctx.StrikerGuard = StrikerGuard;
				const FVector Home = StrikerHome();
				if (Striker) Striker->SetActorLocation(ToWorld(FVector(Home.X, Home.Y, 0.9f)));
				// Still pre-shot: re-resolve so pads and the previewed outcome follow the feet.
				// Flight is guard-independent, so the played path does not jump.
				if (DPhase == EDeliveryPhase::BallInPlay && !Result.bTooLate)
				{
					Ctx.RunCalls = HumanRunCalls;
					Result = CricketDelivery::Resolve(Release, BatInput, Ctx);
				}
			}
		}

		// The pull: holding aims, letting go swings with the release as the timing.
		if (C.bPullHeld || C.bPullReleased)
		{
			const float Was = LiveAim.Magnitude;
			LiveAim = CricketControl::Aim(C.Pull, StrikerPlayer().BatHand, ControlTuning);
			if (C.bPullHeld && Was < 1.f && LiveAim.Magnitude >= 1.f) Haptic(0.2f); // full stretch
			ShotDirection = LiveAim.DirectionDeg;
		}
		bBatPullActive = C.bPullHeld;
		if (C.bPullReleased && DPhase == EDeliveryPhase::BallInPlay && !BatInput.IsShot() && !Result.bTooLate)
		{
			PlayShot(CricketControl::ShotFor(LiveAim, BatMode, CricketMath::PressTime(PhaseTime, Dt)), TEXT("touch"));
			Haptic(LastTimingGrade == ETimingGrade::Perfect ? 0.7f : 0.3f);
			CoachUsed(CricketTouch::EMode::Batting);
		}

		// Manual running: RUN queues another run, CANCEL drops a pending call or sends the batters back.
		// Pressing RUN never scores by itself; the match awards only runs the runners complete.
		const float Post = Result.ContactTime > 0.f ? PhaseTime - Result.ContactTime : -0.1f;
		if (C.bRun && DPhase == EDeliveryPhase::BallInPlay)
		{
			if (CricketControl::CallRun(HumanRunCalls, Result.Running, Post, ControlTuning))
			{
				Haptic(0.2f);
				CoachUsed(CricketTouch::EMode::Running);
				Ctx.RunCalls = HumanRunCalls;
				Result = CricketDelivery::Resolve(Release, BatInput, Ctx);
			}
		}
		if (C.bCancel && DPhase == EDeliveryPhase::BallInPlay)
		{
			if (CricketControl::CancelRun(HumanRunCalls, Result.Running, Post) != ECancelResult::Nothing)
			{
				Ctx.RunCalls = HumanRunCalls;
				Result = CricketDelivery::Resolve(Release, BatInput, Ctx);
			}
		}
		if (DPhase == EDeliveryPhase::BallInPlay || DPhase == EDeliveryPhase::DeadBall)
			LastRunState = CricketControl::RunState(Result.Running, HumanRunCalls, Post);
	}
}

CricketTouch::EMode ASuperOverGameMode::TouchMode() const
{
	using CricketTouch::EMode;
	// IPL: a pending batter/bowler pick owns the touch layer until it is made.
	if (IsAwaitingPick()) return EMode::Pick;
	if (HumanReviews()) return EMode::Review;
	if (IsReplaying() || (DPhase == EDeliveryPhase::Waiting && Match.Phase != EMatchPhase::ReadyForDelivery)) return EMode::Progress;
	if (HumanBats())
	{
		// Running takes over once the stroke is in (or the ball has passed the bat): no second shot, only calls.
		if (DPhase == EDeliveryPhase::DeadBall) return EMode::None;
		if (DPhase == EDeliveryPhase::Waiting) return EMode::Ready;
		const bool bPastBat = BatInput.IsShot() || Result.bTooLate || (Result.ContactTime > 0.f && PhaseTime > Result.ContactTime);
		return DPhase == EDeliveryPhase::BallInPlay && bPastBat ? EMode::Running : EMode::Batting;
	}
	if (bFieldEdit && CanEditField()) return EMode::FieldEdit;
	if (HumanBowls() && DPhase == EDeliveryPhase::Waiting) return EMode::Bowling;
	if (HumanBowls() && DPhase == EDeliveryPhase::RunUp) return EMode::Release;
	return EMode::None;
}

FCricketControls ASuperOverGameMode::ReadTouch(APlayerController* PC)
{
	int32 VX = 0, VY = 0;
	PC->GetViewportSize(VX, VY);
	if (VY <= 0) return FCricketControls();
	// Positions in screen-height units, as the layout uses. One gesture finger owns the
	// batting pull / bowling target drag across frames; buttons act on touch-down.
	TArray<CricketTouch::FFinger> Fingers;
	for (int32 I = 0; I < UE_ARRAY_COUNT(bTouchWasDown); ++I)
	{
		float X = 0.f, Y = 0.f;
		bool bDown = false;
		PC->GetInputTouchState(ETouchIndex::Type(I), X, Y, bDown);
		if (bDown) Fingers.Add({ I, FVector2D(X, Y) / VY, !bTouchWasDown[I] });
		bTouchWasDown[I] = bDown;
	}
	// On desktop the mouse stands in for a finger (id 10, clear of the touch ids).
	// Down-ness is tracked here, as for touches: WasInputKeyJustPressed only updates when the controller
	// processes input, which can tick after this, costing the pull a frame.
	float MX = 0.f, MY = 0.f;
	const bool bMouseDown = PC->IsInputKeyDown(EKeys::LeftMouseButton) && PC->GetMousePosition(MX, MY);
	if (bMouseDown) Fingers.Add({ 10, FVector2D(MX, MY) / VY, !bMouseWasDown });
	bMouseWasDown = bMouseDown;
	TouchFingers = Fingers;
	// IPL: the Pick layout sizes itself by the candidate count, not the bowling repertoire.
	const CricketTouch::EMode ActiveMode = TouchMode();
	const int32 NumOptions = ActiveMode == CricketTouch::EMode::Pick
		? IPLAwaitingCandidates.Num()
		: CricketBowling::Repertoire(BowlerPlayer().BowlerType).Num();
	return CricketTouch::Read(ActiveMode, NumOptions, ViewAspect, Fingers, TouchGesture, TouchSafe);
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
		for (const FButton& B : Layout(Mode, NumTypes, ViewAspect, TouchSafe))
			if (B.Button == Button && B.Index == Index)
			{
				ScriptTapAt = B.Rect.GetCenter();
				InjectTouch(PC, 0, ETouchType::Began, ScriptTapAt);
				bScriptTapDown = true;
				UE_LOG(LogCRICKET26, Display, TEXT("Touch script: tap %s at (%.2f, %.2f)"), What, ScriptTapAt.X, ScriptTapAt.Y);
			}
	};

	// IPL soak: a scripted match makes the first selector pick so it never stalls waiting.
	if (Mode == EMode::Pick) Tap(EButton::Pick, 0, TEXT("PICK"));
	else if (Mode == EMode::Progress && PhaseTime > 1.5f)
	{
		if (Match.Phase == EMatchPhase::MatchComplete && DPhase == EDeliveryPhase::Waiting) { FPlatformMisc::RequestExit(false); return; } // script done
		ScriptTapAt = FVector2D(ViewAspect * 0.5f, 0.4f);
		InjectTouch(PC, 0, ETouchType::Began, ScriptTapAt);
		bScriptTapDown = true;
		UE_LOG(LogCRICKET26, Display, TEXT("Touch script: tap to continue"));
	}
	else if (Mode == EMode::Review && PhaseTime > 1.5f) Tap(EButton::Accept, 0, TEXT("ACCEPT"));
	else if (HumanBats()) // Batting, Running, and the dead ball after (to tidy up)
	{
		if (Mode == EMode::Ready && PhaseTime > BetweenBalls) Tap(EButton::Play, 0, TEXT("PLAY"));
		// Bat with the AI's choice for this ball through the same pull-and-release the player uses:
		// tap its mode, pull for its direction and power, let go on time.
		if (DPhase == EDeliveryPhase::BallInPlay && !ScriptShot.IsShot() && !bScriptStickDown)
		{
			FRandomStream AiRng(Ctx.Seed + 7);
			ScriptShot = CricketAI::ChooseShot(Release, StrikerPlayer(), BowlerPlayer().BowlerType, CricketAI::Aggression(Match), Ctx.Field, Ctx.Conditions, AiRng, AiSkill());
			UE_LOG(LogCRICKET26, Display, TEXT("Touch script: AI would play intent %d, direction %.0f, press %.3f s"), int32(ScriptShot.Intent), ScriptShot.DirectionDeg, ScriptShot.PressTime);
			if (ScriptShot.IsShot())
			{
				const EButton B = ScriptShot.Intent == EBatIntent::Loft ? EButton::Loft : ScriptShot.Intent == EBatIntent::Defend ? EButton::Defend : EButton::Ground;
				Tap(B, 0, B == EButton::Loft ? TEXT("LOFT") : B == EButton::Defend ? TEXT("DEFEND") : TEXT("GROUND"));
			}
		}
		if (DPhase == EDeliveryPhase::BallInPlay && ScriptShot.IsShot() && !BatInput.IsShot())
		{
			const float Lead = 0.30f;
			if (!bScriptStickDown && PhaseTime >= ScriptShot.PressTime - Lead)
			{
				// The pull this shot stands for: length for power, angle for direction (down is straight).
				ScriptPullOrigin = GestureZone(EMode::Batting, ViewAspect, TouchSafe).GetCenter();
				ScriptPullTarget = ScriptPullOrigin;
				if (ScriptShot.Intent != EBatIntent::Defend)
				{
					const float Shape = FMath::Clamp((ScriptShot.Power - ControlTuning.MinPower)
						/ FMath::Max(ControlTuning.MaxPower - ControlTuning.MinPower, 1e-4f), 0.f, 1.f);
					const float Len = ControlTuning.PullDeadZone
						+ FMath::Pow(Shape, 1.f / 1.3f) * (ControlTuning.PullMax - ControlTuning.PullDeadZone);
					const float Raw = FMath::DegreesToRadians(-ScriptShot.DirectionDeg * OffSideSign(StrikerPlayer().BatHand));
					ScriptPullTarget = ScriptPullOrigin + Len * FVector2D(FMath::Sin(Raw), FMath::Cos(Raw));
				}
				InjectTouch(PC, 1, ETouchType::Began, ScriptPullOrigin);
				bScriptStickDown = true;
				UE_LOG(LogCRICKET26, Display, TEXT("Touch script: pull from (%.2f, %.2f)"), ScriptPullOrigin.X, ScriptPullOrigin.Y);
			}
			else if (bScriptStickDown && PhaseTime < ScriptShot.PressTime)
			{
				const float U = FMath::Clamp((PhaseTime - (ScriptShot.PressTime - Lead)) / Lead, 0.f, 1.f);
				InjectTouch(PC, 1, ETouchType::Moved, FMath::Lerp(ScriptPullOrigin, ScriptPullTarget, U));
			}
			else if (bScriptStickDown && PhaseTime >= ScriptShot.PressTime)
			{
				InjectTouch(PC, 1, ETouchType::Ended, ScriptPullTarget);
				bScriptStickDown = false;
				UE_LOG(LogCRICKET26, Display, TEXT("Touch script: release at (%.2f, %.2f)"), ScriptPullTarget.X, ScriptPullTarget.Y);
			}
		}
		if (DPhase == EDeliveryPhase::DeadBall && bScriptStickDown)
		{
			InjectTouch(PC, 1, ETouchType::Ended, ScriptPullTarget);
			bScriptStickDown = false;
			ScriptShot = FBatInput();
		}
		// After contact with no dismissal, call the run through the same RUN button the player uses.
		if (DPhase == EDeliveryPhase::BallInPlay && BatInput.IsShot() && Result.Contact.HasContact()
			&& Result.Dismissal == EDismissal::None && !bScriptRunTapped && PhaseTime >= Result.ContactTime + 0.4f)
		{
			Tap(EButton::Run, 0, TEXT("RUN"));
			bScriptRunTapped = true;
		}
		if (DPhase == EDeliveryPhase::DeadBall)
			bScriptRunTapped = false; // next ball calls its own runs
	}
	else if (Mode == EMode::FieldEdit)
	{
		// The field editor, once: carry a ring fielder to the deep (refused - a sixth outside the circle), then to
		// cover, then apply. The same finger on the same map the player uses.
		const FBox2D Map = FieldMap(ViewAspect, TouchSafe);
		const float Off = OffSideSign(StrikerPlayer().BatHand), T = PhaseTime - ScriptFieldFrom;
		auto Spot = [&](float Deg, float Dist) { const float A = FMath::DegreesToRadians(Deg); return FieldToMap(Map, FVector2D(Dist * FMath::Cos(A), Off * Dist * FMath::Sin(A))); };
		constexpr int32 Who = 7;
		auto Carry = [&](float From, const FVector2D& To)
		{
			if (!bScriptStickDown && T >= From && EditField.IsValidIndex(Who))
			{
				ScriptPullOrigin = FieldToMap(Map, EditField[Who].Home);
				ScriptPullTarget = To;
				InjectTouch(PC, 1, ETouchType::Began, ScriptPullOrigin);
				bScriptStickDown = true;
				UE_LOG(LogCRICKET26, Display, TEXT("Touch script: pick up %s"), *EditField[Who].Position);
			}
			else if (bScriptStickDown)
			{
				const float U = FMath::Clamp((T - From) / 0.8f, 0.f, 1.f);
				InjectTouch(PC, 1, U < 1.f ? ETouchType::Moved : ETouchType::Ended, FMath::Lerp(ScriptPullOrigin, ScriptPullTarget, U));
				if (U >= 1.f) { bScriptStickDown = false; ++ScriptFieldStep; }
			}
		};
		if (ScriptFieldStep == 0) Carry(1.f, Spot(70.f, 55.f));
		else if (ScriptFieldStep == 1) Carry(2.6f, Spot(70.f, 20.f));
		else if (T > 4.2f) { Tap(EButton::FieldApply, 0, TEXT("APPLY")); bScriptFieldDone = true; }
	}
	else if (Mode == EMode::Bowling || Mode == EMode::Release)
	{
		// Bowl the second delivery type in the repertoire, releasing near the perfect point of the meter.
		const EDeliveryType Want = CricketBowling::Repertoire(BowlerPlayer().BowlerType)[1];
		if (!bScriptFieldDone && DPhase == EDeliveryPhase::Waiting && PhaseTime > 0.5f)
		{
			Tap(EButton::Field, 0, TEXT("FIELD"));
			ScriptFieldFrom = PhaseTime;
		}
		else if (DPhase == EDeliveryPhase::Waiting && PhaseTime > 0.5f)
			HumanPlan.Type != Want ? Tap(EButton::Delivery, 1, TEXT("delivery 2")) : Tap(EButton::Bowl, 0, TEXT("BOWL (run-up)"));
		else if (DPhase == EDeliveryPhase::RunUp && -1.f + 2.f * PhaseTime / RunUpSeconds >= -0.05f)
			Tap(EButton::Bowl, 0, TEXT("BOWL (release)"));
	}
}

void ASuperOverGameMode::BeginRunUp()
{
	// IPL: the next ball waits on the user's batter/bowler pick; nothing starts early.
	if (IsAwaitingPick()) return;
	if (!Match.BeginDelivery()) return;
	// -CricketLeftHanded: every striker bats left-handed, to inspect the mirrored strokes.
	if (FParse::Param(FCommandLine::Get(), TEXT("CricketLeftHanded")))
		Teams[Match.BattingTeam()].Batters[Match.Cur().Striker].BatHand = ECricketHand::Left;
	// -CricketPace: the bowler bowls quick (as F3 does), so a forced pull or hook meets a ball that gets up.
	if (FParse::Param(FCommandLine::Get(), TEXT("CricketPace")))
	{
		FCricketPlayer& B = Teams[Match.BowlingTeam()].Bowler;
		B.BowlerType = EBowlerType::Pace;
		B.PaceKph = 140.f;
	}
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
		// -CricketLength=M: the AI bowler pitches every ball this far from the striker's stumps (short for the cut
		// and pull, full for the drives and sweep).
		FParse::Value(FCommandLine::Get(), TEXT("CricketLength="), HumanPlan.Length);
		// -CricketLine=M: and this far toward the striker's off side (wide ones for the keeper's shuffle and dive).
		FParse::Value(FCommandLine::Get(), TEXT("CricketLine="), HumanPlan.Line);
		// The release is fixed now (plan, timing, seed), so the batter can be shown where it will pitch from the start
		// of the run-up. DoRelease resolves the same ball.
		const FDeliveryResult Preview = CricketDelivery::Resolve(
			CricketBowling::Execute(BowlerPlayer(), StrikerPlayer().BatHand, HumanPlan, ReleaseTiming, Ctx.Seed, Ctx.Conditions), FBatInput(), Ctx);
		PitchPreview = Preview.PitchPos;
		PitchPreviewAt = Preview.PitchTime > 0.f ? ReleaseAt() + Preview.PitchTime : -1.f;
	}
	DPhase = EDeliveryPhase::RunUp;
	PhaseTime = 0.f;
	Meter = -1.f;
	NextFootstepAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	// A new delivery is a new shot: cut back to the bowler's-end camera instead of easing from the last
	// ball's follow, and retire the last ball's result from the HUD. The broadcast director, replay
	// package and rolling buffer all restart with the delivery.
	bCutCamera = true;
	BroadcastDirector.Reset(EBroadcastShot::StandardDelivery);
	ClearBroadcastReplay();
	ReplayBuffer.Reset();
	LastSummary.Reset();
	Commentary.Reset();
}

void ASuperOverGameMode::DoRelease(float Timing)
{
	const FCricketPlayer& Batter = StrikerPlayer();
	Release = CricketBowling::Execute(BowlerPlayer(), Batter.BatHand, HumanPlan, Timing, Ctx.Seed, Ctx.Conditions);
	ReleaseTiming = Timing;
	LastReleaseGrade = CricketControl::GradeRelease(Timing, ControlTuning);
	BatInput = FBatInput();
	// -CricketAiLeaves: the AI batter lets every ball go (to inspect pad impacts and their ball tracking).
	static const bool bAiLeaves = FParse::Param(FCommandLine::Get(), TEXT("CricketAiLeaves"));
	// -CricketAiShot=Intent,Dir: the AI batter plays that intent (1 defend, 2 ground, 3 loft) toward Dir (degrees, + off
	// side) to every ball, timed as it would, to show each stroke. The ball's length still picks the stroke.
	static FString AiShot;
	static const bool bAiShot = FParse::Value(FCommandLine::Get(), TEXT("CricketAiShot="), AiShot, false);
	if (!HumanBats() && !bAiLeaves)
	{
		FRandomStream AiRng(Ctx.Seed + 1);
		FString ShotIntent, ShotDir;
		if (bAiShot && AiShot.Split(TEXT(","), &ShotIntent, &ShotDir))
			BatInput = CricketAI::PlayIntent(EBatIntent(FCString::Atoi(*ShotIntent)), Release, Batter, BowlerPlayer().BowlerType, Ctx.Field,
				Ctx.Conditions, AiRng, AiSkill(), FCString::Atof(*ShotDir));
		else
			BatInput = CricketAI::ChooseShot(Release, Batter, BowlerPlayer().BowlerType, CricketAI::Aggression(Match), Ctx.Field, Ctx.Conditions, AiRng, AiSkill());
	}
	Ctx.RunCalls = HumanRunCalls; // pre-contact run calls buffered during the run-up count from contact
	Result = CricketDelivery::Resolve(Release, BatInput, Ctx);
	if (HumanBats() && PitchPreviewAt > 0.f && !Result.PitchPos.Equals(PitchPreview, 0.01f))
		UE_LOG(LogTemp, Warning, TEXT("Pitch preview off by %.2f m"), FVector::Dist(Result.PitchPos, PitchPreview));
	DPhase = EDeliveryPhase::BallInPlay;
	PhaseTime = 0.f;
	// Micro-drop around the release so the contact lands; the landing foot is the last run-up step.
	if (GetWorld())
	{
		CricketAudioDirector::OnRelease(AudioDir, GetWorld()->GetTimeSeconds());
		PlayCue(CricketAudio::ECue::Footstep, 0.25f);
	}
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
			// The appeal goes up as the ball dies: HOWZAT now, umpire's decision after.
			if (GetWorld()) CricketAudioDirector::QueueVocal(AudioDir, CricketAudioDirector::EVocal::Howzat, GetWorld()->GetTimeSeconds() + 0.3f);
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
	const CricketCommentary::FNames Names{ StrikerPlayer().Name, Teams[Match.BattingTeam()].Batters[Match.Cur().NonStriker].Name, Teams[Match.BattingTeam()].Name,
		Teams[Match.BowlingTeam()].Name };
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
	DrawPitchMarks(Mark);
	TArray<ECricketEvent> Events;
	// IPL: the batting-order slot the engine is about to consume for the incoming batter, so the
	// user's pick can reorder the tail before he faces.
	const int32 IPLPreNext = (IsIPLMatch() && Match.Innings.IsValidIndex(Match.CurrentInnings)) ? Match.Cur().NextBatter : INDEX_NONE;
	if (!Match.CompleteDelivery(Outcome, Events))
	{
		UE_LOG(LogCRICKET26, Error, TEXT("Rules rejected the simulated outcome (%s); delivery voided."), *Result.Summary);
		// Recover by voiding as a dot ball so the slice cannot soft-lock.
		Match.CompleteDelivery(FDeliveryOutcome(), Events);
	}
	FString Err;
	if (!Match.CheckInvariants(Err)) UE_LOG(LogCRICKET26, Error, TEXT("Match invariant broken: %s"), *Err);
	LastSummary = Result.Summary;
	// Broadcast audio: the directors consume the finished delivery (they never decide it).
	const float NowAudio = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	CricketAudioDirector::OnDelivery(AudioDir, Result, Outcome, Match, NowAudio);
	CricketCommentaryDirector::FContext CommCtx = CricketCommentaryDirector::FContext::Build(Result, Outcome, Match, Names, OffSign, Release.Type);
	CricketCommentaryDirector::FSelection Sel = CricketCommentaryDirector::SelectLine(CommDir, CommCtx, Names, NowAudio, BallsPlayed, CommRng);
	Commentary = Sel.bSilent ? FString() : Sel.Text;
	if (!Sel.bSilent) Say(Sel.Body, Sel.Suffix, NowAudio + Sel.DelaySeconds);
	if (bReviewTaken && ReviewResult == CricketUmpire::EReview::Overturned) Commentary = TEXT("Overturned on review! ") + Commentary;
	if (bReferredThis) Commentary = (Outcome.Dismissal == EDismissal::None ? TEXT("Not out, says the third umpire. ") : TEXT("Given out by the third umpire. ")) + Commentary;
	UE_LOG(LogCRICKET26, Display, TEXT("Commentary: %s"), *Commentary);
	++BallsPlayed;
	UE_LOG(LogCRICKET26, Display, TEXT("%s %d/%d (%d.%d): %s"), *Teams[Match.BattingTeam()].Short, Match.Cur().Runs,
		Match.Cur().Wickets, Match.Cur().LegalBalls / 6, Match.Cur().LegalBalls % 6, *LastSummary);
	Emit(Events);
	// Broadcast replay: the trigger classifies the ACTUAL result + umpire outcome (wickets, boundaries,
	// edges, drops, close run-outs, milestones), not just wicket/boundary events. The third umpire's
	// frames were the replay, so a referred delivery never replays again.
	const bool bMilestone = Events.Contains(ECricketEvent::MatchWon);
	BuildReplayPackageForResult(Outcome, bMilestone);
	bReplayThis = ActivePackage.IsValid() && Result.BallPath.Num() > 1 && !bReferredThis;
	if (!bReplayThis) ClearBroadcastReplay();
	bWicketThis = Events.Contains(ECricketEvent::Wicket);
	if (bReplayThis)
	{
		Highlights.Add({ Result, Ctx, BatInput, ReleaseTiming, Commentary });
		ReplayBuffer.MarkEvent(Result.ContactTime, TEXT("contact"));
		if (Result.bStumpsHit) ReplayBuffer.MarkEvent(Result.StumpsTime, TEXT("stumps"));
		if (Result.BrokenTime >= 0.f) ReplayBuffer.MarkEvent(Result.BrokenTime, TEXT("broken"));
		if (Result.Fielding.Boundary > 0) ReplayBuffer.MarkEvent(Result.ContactTime + Result.Fielding.BoundaryTime, TEXT("boundary"));
	}
	bReviewThis = Result.bPadImpact && Result.Tracking.Projected.Num() > 1 && Result.BallPath.Num() > 1;
	DPhase = EDeliveryPhase::DeadBall;
	PhaseTime = 0.f;
	// IPL: wicket/over/match-complete handling (batter and bowler picks, super-over decider, commit).
	if (IsIPLMatch()) AfterIPLDelivery(Outcome, Events, IPLPreNext);
}

void ASuperOverGameMode::PlayClip(int32 Clip)
{
	// ponytail: the fielders who do not move in a clip stay where the live field put them.
	const FHighlight& H = Highlights.IsValidIndex(Clip) ? Highlights[Clip] : LiveClip;
	Result = H.Result;
	Ctx = H.Ctx;
	BatInput = H.BatInput;
	ReleaseTiming = H.ReleaseTiming;
	Commentary = H.Commentary;
	bReviewThis = false;
	if (Highlights.IsValidIndex(Clip))
	{
		// The clip's own multi-angle replay package, from its start. The buffer belongs to the live
		// delivery, not the clip, so reel replays pose analytically from the stored result (same data).
		ReelClip = Clip;
		bReplayThis = true;
		BuildReplayPackageForResult(Result.ToOutcome(), false);
		if (!ActivePackage.IsValid()) ClearBroadcastReplay(); // legacy fallback timings still replay it
		DPhase = EDeliveryPhase::DeadBall;
		PhaseTime = ReplayDelay;
		return;
	}
	ReelClip = -1;
	bReplayThis = false;
	ClearBroadcastReplay();
	bCutCamera = true;
	Highlights.Empty();
	DPhase = EDeliveryPhase::Waiting;
	PhaseTime = 0.f;
}

void ASuperOverGameMode::Emit(const TArray<ECricketEvent>& Events)
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	for (ECricketEvent E : Events)
	{
		// The crowd lifts for boundaries and wickets through the audio director, then settles
		// back to the bed gradually (no instant quiet->screaming cuts).
		CricketAudioDirector::OnEvent(AudioDir, E, Match, Now);
		OnCricketEvent.Broadcast(E);
	}
}

CricketPose::FClipPlay ASuperOverGameMode::ThrowPlay(int32 I, float Post) const
{
	const FRunningOutcome& Run = Result.Running;
	const FFieldingOutcome& Fd = Result.Fielding;
	if (!ThrowAnim) return {};
	if (I == Run.RelayMove.Fielder && Run.RelayRelease > 0.f) return CricketPose::ThrowClip(Post, Run.RelayCatch, Run.RelayRelease);
	if (I != Fd.Fielder || Run.ThrowRelease <= 0.f || Run.ThrowType == EThrowType::Underarm) return {};
	// A diver has the ball once down, and throws on the way up.
	const bool bDived = (Fd.bDive || Fd.Action == EFieldAction::CatchDiving) && DiveAnims[0] && DiveAnims[1];
	return CricketPose::ThrowClip(Post, Fd.FieldTime + (bDived ? CricketPose::DiveClipLanded - CricketPose::DiveClipStretch : 0.f), Run.ThrowRelease);
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

	// Hands on a bat: the wrists just off the handle toward the chest, the top hand above the bottom. The elbows
	// point the way the body faces them, whichever way it runs: the top one out ahead, the bottom one back, out
	// and down, as an arm hangs.
	auto Hold = [](FCricketBodyPose& P, const FBat& B, const FVector& Chest, const AActor* Who, int32 TopHand, float TopWeight, float BottomWeight)
	{
		const FVector F = Who->GetActorForwardVector(), R = Who->GetActorRightVector();
		for (int32 H = 0; H < 2; ++H)
		{
			const bool bTop = H == TopHand;
			const float Out = H == 1 ? 1.f : -1.f; // toward this arm's side
			const FVector OnHandle = B.Grip + B.Axis * (bTop ? -0.05f : 0.05f);
			P.Hand[H] = ToWorld(OnHandle + (Chest - OnHandle).GetSafeNormal() * 0.06f);
			P.HandWeight[H] = bTop ? TopWeight : BottomWeight;
			P.Elbow[H] = ToWorld(Chest + (bTop ? F * 0.6f + R * (0.2f * Out) : -F * 0.3f + R * (0.3f * Out) - FVector::UpVector * 0.6f));
		}
	};
	auto PlaceBat = [this](AActor* Blade, const FBat& B)
	{
		const FRotator R = FRotationMatrix::MakeFromZY(B.Axis, B.Face).Rotator();
		Blade->SetActorLocationAndRotation(ToWorld(B.Top() + B.Axis * (HandleLength + 0.5f * (BatLength - HandleLength))), R);
		BatHandles[Blade == Bat ? 0 : 1]->SetActorLocationAndRotation(ToWorld(B.Top() + B.Axis * (0.5f * HandleLength)), R);
	};
	// A bat carried in the bottom hand, running or waiting at the non-striker's end: it goes where the idle or the
	// jog swings that hand (its last pose, moved with the body), so the arm keeps its natural swing rather than
	// being pinned to a fixed spot beside the hip.
	auto Carry = [this](const AActor* Who, float Side)
	{
		const FVector F = Who->GetActorForwardVector(), R = Who->GetActorRightVector();
		const USkeletalMeshComponent* Body = BodyOf(Who);
		const TCHAR* Suffix = Side > 0.f ? TEXT("_r") : TEXT("_l");
		const int32 WristBone = Body ? Body->GetBoneIndex(FName(FString(TEXT("hand")) + Suffix)) : INDEX_NONE;
		const int32 ElbowBone = Body ? Body->GetBoneIndex(FName(FString(TEXT("lowerarm")) + Suffix)) : INDEX_NONE;
		const TArray<FTransform>& Pose = Body ? Body->GetComponentSpaceTransforms() : TArray<FTransform>();
		if (WristBone != INDEX_NONE && ElbowBone != INDEX_NONE && Pose.IsValidIndex(WristBone) && Pose.IsValidIndex(ElbowBone)
			&& !Pose[WristBone].GetLocation().IsNearlyZero())
		{
			const FTransform& To = Body->GetComponentTransform();
			return CarriedBat(To.TransformPosition(Pose[ElbowBone].GetLocation()) / 100.f, To.TransformPosition(Pose[WristBone].GetLocation()) / 100.f, F, R);
		}
		// Before the first pose: the hand hanging at the side.
		const FVector Shoulder = Who->GetActorLocation() / 100.f + R * (0.2f * Side) + FVector(0.f, 0.f, 0.55f);
		return CarriedBat(Shoulder - FVector::UpVector * 0.3f, Shoulder - FVector::UpVector * 0.55f, F, R);
	};
	const FRunningOutcome& Run = Result.Running;
	// Running between the wickets.
	const float RunW = bLive && Run.Attempted > 0 ? Ramp(Post, 0.35f, 0.6f) : 0.f;

	// Striker: the whole body planned through the delivery (CricketBatter), the stroke built backwards from
	// where the simulation put the bat so the sweet spot is on that point at the moment of contact. Setting
	// off for a run, the bat goes to the bottom hand and the body to the jog.
	{
		const EShotType Shot = Result.Shot.Shot;
		const bool bPlayed = bLive && BatInput.IsShot();
		CricketBatter::FInput In;
		In.Off = Off;
		In.Home = StrikerHome();
		In.Time = Ttr;
		In.Clock = GetWorld()->GetTimeSeconds();
		In.bStroke = bPlayed && Shot != EShotType::Leave;
		In.bLeave = bPlayed && Shot == EShotType::Leave;
		In.Shot = Shot;
		In.Foot = Result.Shot.Foot;
		In.DirectionDeg = BatInput.DirectionDeg;
		In.Press = BatInput.PressTime;
		// A hit meets the ball when the simulation says; a miss swings through the aim when the timing says.
		In.Impact = Result.Contact.HasContact() ? Result.ContactTime : BatInput.PressTime + Result.Shot.SwingTime;
		In.Contact = !Result.Contact.BatPos.IsZero() ? Result.Contact.BatPos : FVector(Result.Shot.ContactX(), 0.f, 0.5f);
		FVector Dir = CricketBatting::DirectionToWorld(BatInput.DirectionDeg, StrikerPlayer().BatHand);
		Dir.Z = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(Result.Shot.LoftDeg, 0.f, 45.f)));
		In.ShotDir = Dir.GetSafeNormal();
		In.Style = StrikerPlayer().BatterStyle;
		In.bMiss = bPlayed && !Result.Contact.HasContact() && Shot != EShotType::Leave;
		In.ContactQuality = Result.Contact.HasContact() ? (Result.Contact.Zone == EContactZone::Middle ? 1.f : 0.6f) : 0.f;
		// Back into the stance after the ball; from down the track, back in their ground when the simulation says.
		// Otherwise the finish (reached 0.5 s after the contact) is held a second, watching the ball, as in the
		// broadcast reference, before the hands come down.
		In.Settle = In.Foot == EFootwork::Advance && Result.BrokenTime >= 0.f ? Result.BrokenTime - Result.HomeMargin - 0.8f
			: In.Foot == EFootwork::Advance ? Result.ContactTime + 0.4f : In.Impact + 1.5f;
		const CricketBatter::FBody Body = CricketBatter::Plan(In);
		if (!(bLive && Run.Attempted > 0 && Post > 0.f))
		{
			Striker->SetActorLocation(ToWorld(FVector(Body.Pelvis.X, Body.Pelvis.Y, MarkerHeight)));
			StrikerFrom = FVector2D(Body.Pelvis);
		}
		const FBat B = Blend(Body.Bat, Carry(Striker, Off), RunW);
		PlaceBat(Bat, B);
		if (UCricketAnimInstance* Anim = AnimOf(Striker))
		{
			FCricketBodyPose& P = Anim->Pose;
			FCricketBatterPose& S = P.Batter;
			S.Weight = 1.f - RunW;
			S.Pelvis = ToWorld(Body.Pelvis);
			S.Drop = Body.Drop * 100.f;
			S.Hips = Body.Hips;
			S.Chest = Body.Chest;
			S.ChestUp = Body.ChestUp;
			for (int32 F = 0; F < 2; ++F)
			{
				S.Ball[F] = ToWorld(Body.Foot[F].Ball);
				S.Toe[F] = Body.Foot[F].Toe;
				S.Heel[F] = Body.Foot[F].Heel;
				S.Lift[F] = Body.Foot[F].Lift * 100.f;
			}
			S.Grip = ToWorld(B.Grip);
			S.BatAxis = B.Axis;
			S.BatFace = B.Face;
			S.TopHand = Body.TopHand;
			// Setting off: the jog's body with the bat in the bottom hand, the top hand letting go.
			if (RunW > 0.f)
			{
				Hold(P, B, SimAt(Striker) + FVector(0.f, 0.f, ShoulderHeight - MarkerHeight), Striker, Body.TopHand, 1.f - RunW, 1.f - RunW);
				P.ChestFacing = Striker->GetActorForwardVector();
			}
		}
	}

	// Non-striker: carries the bat in the hand the idle and the jog move.
	PlaceBat(NonStrikerBat, Carry(NonStriker, OffSideSign(Ctx.NonStriker.BatHand)));

	// Bowler: the authored action plays the whole body, its head on the batter throughout. Without it the arm
	// windmills over the top to the release, the front arm pulls down through it, and the chest turns from side-on
	// to the batter and bends over the front leg.
	const FClipPlay Bowl = BowlPlay(T, bLive);
	if (UCricketAnimInstance* Anim = AnimOf(Bowler); Anim && Bowl.Weight > 0.f)
	{
		FCricketBodyPose& P = Anim->Pose;
		P.Clip[0] = BowlAnim();
		P.ClipTime[0] = Bowl.Time;
		P.ClipWeight[0] = Bowl.Weight;
		P.LookWeight *= 1.f - Bowl.Weight;
	}
	const float BowlW = BowlAnim() ? 0.f : BowlingArmWeight(Ttr);
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

	// Fielders and keeper. The keeper runs its own isolated pipeline below
	// (CricketKeeper: stance, pre-delivery rhythm, trajectory-aware takes,
	// footwork, dives, spin, stumping, run-out reception, recovery); outfielders
	// keep the previous generic reach/throw behaviour untouched.
	for (int32 I = 0; I < Ctx.Field.Num(); ++I)
	{
		const FFielder& F = Ctx.Field[I];
		AStaticMeshActor* Who = F.bBowler ? Bowler.Get() : Fielders.IsValidIndex(I) ? Fielders[I].Get() : nullptr;
		UCricketAnimInstance* Anim = Who && !Who->IsHidden() ? AnimOf(Who) : nullptr;
		if (!Anim || (F.bBowler && (BowlW > 0.f || Bowl.Weight > 0.f))) continue;
		FCricketBodyPose& P = Anim->Pose;
		const FVector At = SimAt(Who);
		const FVector Fwd = Who->GetActorForwardVector();
		if (F.bKeeper)
		{
			P.bKeeper = true;
			// ---- WICKETKEEPER PIPELINE (isolated; simulation stays authoritative) ----
			const bool bStandingUp = CricketKeeper::IsStandingUp(F.Home);
			const CricketKeeper::FKeeperReady Ready = CricketKeeper::ReadyFor(bStandingUp);
			const float WorldT = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			// Set as the bowler arrives, stay low through the take, then recover.
			const float Breathe = 0.008f * FMath::Sin(WorldT * 1.4f + 0.7f * I);
			float Set = Ramp(Ttr, -0.9f, -0.15f);
			// Down into the squat as the bowler gathers, up with the bounce (on the full, just before the bat).
			const float RiseAt = !bLive ? -1.f : Result.PitchTime >= 0.f && Result.PitchTime < Result.ContactTime ? Result.PitchTime : Result.ContactTime - 0.12f;
			float Drop = CricketKeeper::StanceDrop(bStandingUp, Ttr, RiseAt) + Breathe;
			const FFieldingOutcome& Fd = Result.Fielding;
			const bool bKeeperTakes = bLive && Fd.Fielder == I;
			if (bLive)
			{
				const float RecoverAt = bKeeperTakes ? Fd.FieldTime : 0.55f;
				const float Recovery = Ramp(Post, RecoverAt + 0.15f, RecoverAt + 0.85f);
				Drop = FMath::Lerp(Drop, 0.12f + Breathe, Recovery);
				Set *= 1.f - Recovery;
			}
			// The side-step into a wide take on the feet runs ~4 m/s, over the keeper's jog threshold: the jog clip
			// faded in over the crouch while the feet IK faded out, and the keeper folded to the grass for a frame.
			if (bKeeperTakes && !CricketKeeper::UseSideDive(F.Home, Fd.FieldPos, Fd.bDive))
			{
				const FFielderMove* StepMove = Fd.Moves.FindByPredicate([I](const FFielderMove& M) { return M.Fielder == I; });
				const FVector2D Body = CricketKeeper::TakeBodyAt(StepMove, F, Ctx.Fielding.RunSpeed, Fd.FieldPos, Fd.FieldTime, Fd.FieldTime, Fd.FieldTime);
				if (CricketKeeper::ShufflesToTake(F.Home, Body, Fd.FieldTime, Post))
					P.WalkWeight = P.JogWeight = P.SprintWeight = 0.f;
			}
			Drop = FMath::Lerp(Drop, 0.12f + Breathe, FMath::Clamp(Anim->Pose.WalkWeight + Anim->Pose.JogWeight, 0.f, 1.f));
			const FVector Chest0 = At + FVector(0.f, 0.f, ShoulderHeight - MarkerHeight - 0.1f);
			const FVector Chest = Chest0 - FVector(0.f, 0.f, Drop);
			// Gloves presented naturally by the knees, head stable to the ball.
			FVector Reach = Chest + Fwd * FMath::Lerp(0.25f, Ready.GloveForward, Set)
				- FVector(0.f, 0.f, Chest.Z - FMath::Lerp(0.72f, Ready.GloveHeight - 0.9f * FMath::Max(Drop - Ready.Drop, 0.f), Set));
			Reach += Fwd * (0.012f * FMath::Sin(WorldT * 1.1f)) + FVector(0.f, 0.f, 0.01f * FMath::Sin(WorldT * 1.7f));
			float Near = 0.f;
			float TakeWeight = 0.f;
			CricketKeeper::FKeeperSelection Sel;
			bool bHaveSel = false;
			const bool bContact = bLive && Result.Contact.HasContact();
			if (bLive)
			{
				const FVector Soon = Result.BallAt(T + 0.12f);
				Near = 1.f - Ramp(FVector::Dist(Soon, Chest0), 0.8f, 1.8f);
				// Trajectory-aware selection: the REAL interception, height,
				// lateral displacement and time to arrival. Never a generic event.
				const bool bBounced = !bContact && Result.PitchTime >= 0.f;
				const float TimeToArr = FMath::Max(0.f, Fd.FieldTime - Post);
				Sel = CricketKeeper::Classify(F.Home, Fd.FieldPos, bKeeperTakes, CricketKeeper::UseSideDive(F.Home, Fd.FieldPos, Fd.bDive),
					bContact, bBounced, bStandingUp, TimeToArr, Off);
				bHaveSel = true;
				// Log once per delivery as the ball comes through (dev/QA).
				if (FMath::Abs(Post) < 0.02f && bDebug)
					UE_LOG(LogTemp, Display, TEXT("%s"), *CricketKeeper::SelectionLog(Sel, Fd.FieldPos, FMath::Abs(F.Home.X), Ctx.Bowler.BowlerType));
				// Semantic markers (dev/QA; gameplay never reads these).
				auto Mark = [&](float AtPost, const TCHAR* Name)
				{
					if (FMath::Abs(Post - AtPost) < 0.025f && bDebug)
						UE_LOG(LogTemp, Display, TEXT("Keeper event %s post %.2f take %s"), Name, Post, CricketKeeper::TakeName(Sel.Family));
				};
				if (bKeeperTakes)
				{
					if (Sel.Footwork == CricketKeeper::EKeeperFootwork::PushDive) Mark(Fd.FieldTime - 0.4f, TEXT("KeeperPushOff"));
					Mark(Fd.FieldTime, TEXT("KeeperCatchContact"));
					Mark(Fd.FieldTime + 0.25f, TEXT("KeeperSecureBall"));
					Mark(Fd.FieldTime + 1.2f, TEXT("KeeperRecoveryComplete"));
				}
				if (bKeeperTakes && Sel.Reach != CricketKeeper::EKeeperReach::Unreachable)
				{
					const float Approach = Ramp(Post, bContact ? FMath::Max(0.f, Fd.FieldTime - 0.3f) : Fd.FieldTime - 0.45f, Fd.FieldTime);
					FVector TakePoint = Fd.FieldPos;
					// Stumping transfer: possession first, then quick economical
					// gloves-to-wicket, synced to the sim's BrokenTime.
					float StampP = 0.f;
					if (Result.BrokenTime >= 0.f && Result.bBrokenAtStrikerEnd && Post >= Fd.FieldTime)
					{
						const float BreakPost = Result.BrokenTime - Result.ContactTime;
						StampP = CricketKeeper::StumpingProgress(Post, Fd.FieldTime, BreakPost);
						if (StampP > 0.f)
						{
							Mark(BreakPost, TEXT("KeeperStumpBreak"));
							const FVector StumpTop(0.f, 0.f, CricketGeo::StumpHeight);
							TakePoint = FMath::Lerp(Fd.FieldPos, StumpTop, StampP);
						}
					}
					const float HoldUntil = FMath::Max(Fd.FieldTime + 0.35f,
						StampP > 0.f ? Result.BrokenTime - Result.ContactTime + 0.15f : 0.f);
					const float Receive = Approach * (1.f - Ramp(Post, HoldUntil, HoldUntil + 0.45f));
					Drop = CricketKeeper::TakeDrop(Drop, Ready.Drop, Sel.Height, Receive);
					const FVector ChestD = Chest0 - FVector(0.f, 0.f, Drop);
					// Gloves track the real ball, converge onto the true take.
					FVector Gloves;
					TakeWeight = Receive;
					const FVector Tracked = TakePoint;
					const float ClampM = CricketKeeper::GloveTarget(Sel, ChestD, Tracked, Gloves);
					// Only while the take drives the gloves: before the step across the clamp is measured from home.
					if (ClampM > Sel.MaxIKCorrection + 0.05f && Receive > 0.5f && bDebug)
						UE_LOG(LogTemp, Display, TEXT("Keeper WRONG-ANIM take %s clamp %.2f m (lat %+.2f h %.2f) post %.2f"), CricketKeeper::TakeName(Sel.Family), ClampM, Sel.Lateral, Sel.Height, Post);
					// Absorb the ball: gloves give, elbows absorb, body
					// stabilizes. Pace gives more than spin.
					float Give = 0.f;
					if (Post > Fd.FieldTime)
					{
						const float Rate = bStandingUp ? 0.07f : 0.16f;
						Give = Rate * FMath::Clamp((Post - Fd.FieldTime) / 0.18f, 0.f, 1.f);
						Gloves += (ChestD - Gloves) * Give;
					}
					Reach = FMath::Lerp(Reach, Gloves, Receive);
					// Body behind the line: upper body travels with the take
					// over planted feet (small, constrained; feet re-plant by IK).
					const FVector ToTake = TakePoint - ChestD;
					const FVector LatDir = FVector(0.f, ToTake.Y, 0.f).GetSafeNormal(UE_SMALL_NUMBER, Who->GetActorRightVector());
					const float LatShift = FMath::Clamp(FMath::Abs(ToTake.Y) * 0.3f, 0.f, bStandingUp ? 0.25f : 0.35f);
					P.ShouldersAt = ToWorld(ChestD + LatDir * LatShift);
					P.ShouldersWeight = Receive * (Sel.Footwork == CricketKeeper::EKeeperFootwork::PushDive ? 0.3f : 0.5f);
					// Restrained appeal shape on caught-behind/stumping: gloves
					// lift briefly, no theatrical scream.
					if ((Result.Dismissal == EDismissal::Caught || Result.Dismissal == EDismissal::Stumped)
						&& Post > Fd.FieldTime + 0.2f && Post < Fd.FieldTime + 1.0f)
						Reach.Z += 0.22f * FMath::Sin(PI * (Post - Fd.FieldTime - 0.2f) / 0.8f);
				}
				else if (bKeeperTakes)
				{
					// Honest miss: capped stretch toward the ball, body holds,
					// then turn and chase via the existing locomotion.
					FVector Gloves;
					CricketKeeper::GloveTarget(Sel, Chest, Soon, Gloves);
					Reach = FMath::Lerp(Reach, Gloves, Near * 0.6f);
				}
				// Run-out reception at the keeper's end: orient to the incoming
				// throw, position near the wicket, receive, stay balanced.
				const FRunningOutcome& RunK = Result.Running;
				if (!bKeeperTakes && RunK.ThrowRelease > 0.f && RunK.bThrowToStrikerEnd && Post > RunK.ThrowRelease - 0.5f && Post < RunK.ThrowArrive + 0.4f)
				{
					const int32 Thrower = Fd.Fielder;
					FVector ThrowFrom = FVector(12.f, 0.f, 1.f);
					if (Ctx.Field.IsValidIndex(Thrower))
					{
						if (Thrower == I) ThrowFrom = At;
						else if (Fielders.IsValidIndex(Thrower)) ThrowFrom = Fielders[Thrower]->GetActorLocation() / 100.f;
						else if (Thrower >= 0 && Ctx.Field[Thrower].bBowler && Bowler) ThrowFrom = Bowler->GetActorLocation() / 100.f;
					}
					const FVector Aim = FVector(-At.X, -At.Y, 0.f).GetSafeNormal(UE_SMALL_NUMBER, Fwd); // toward the striker's stumps
					const float H = RunK.ThrowType == EThrowType::Underarm ? 0.6f : 1.2f;
					const FVector Reception(0.f, 0.f, H);
					const float Rw = Ramp(Post, RunK.ThrowRelease - 0.4f, RunK.ThrowArrive);
					Reach = FMath::Lerp(Reach, Reception, Rw);
					Near = FMath::Max(Near, Rw);
					P.ChestFacing = FMath::Lerp(Fwd, (ThrowFrom - At).GetSafeNormal(UE_SMALL_NUMBER, Fwd), Rw * 0.7f);
					if (FMath::Abs(Post - RunK.ThrowRelease) < 0.025f && bDebug)
						UE_LOG(LogTemp, Display, TEXT("Keeper event KeeperThrowReceive post %.2f h %.2f"), Post, H);
				}
			}
			const FVector ChestF = Chest0 - FVector(0.f, 0.f, Drop);
			FVector Across = FVector::CrossProduct(Reach - ChestF, FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER, Who->GetActorRightVector());
			// Variation without quality loss: millimetre glove separation by
			// delivery, never a different body shape.
			const float Var = 0.005f * FMath::Sin(float(BallsPlayed * 3 + I) * 1.7f);
			for (int32 H = 0; H < 2; ++H)
			{
				const float S = H == 0 ? 1.f : -1.f;
				// Palm centres either side of the ball, the gloves closing round it at the take.
				const float Spread = (0.06f + Var) * S * (1.f - 0.15f * TakeWeight);
				P.Hand[H] = ToWorld(Reach + Across * Spread);
				// Elbows stay valid per take height: low takes wide and low,
				// high takes below the hands, never collapsed or inverted.
				const float Hgt = bHaveSel ? Sel.Height : 0.7f;
				const float ElbOut = Hgt > 1.1f ? 0.42f : 0.5f;
				const float ElbDown = Hgt > 1.1f ? 0.28f : Hgt < 0.35f ? 0.58f : 0.5f;
				P.Elbow[H] = ToWorld(ChestF + Across * (ElbOut * S) - FVector(0.f, 0.f, ElbDown));
				P.HandWeight[H] = FMath::Max(Near, FMath::Lerp(0.55f, 1.f, Set));
			}
			P.PelvisOffset = FVector(0.f, 0.f, -100.f * Drop);
			P.ChestBend = FMath::Min(32.f, Ready.ChestBend * Drop / Ready.Drop);
			// A take off the grass goes down over the ball, not just the knees: the arms reach from the shoulders.
			if (bHaveSel) P.ChestBend += 24.f * (1.f - Ramp(Sel.Height, 0.1f, 0.5f)) * TakeWeight;
			if (P.ChestFacing.IsNearlyZero()) P.ChestFacing = Fwd;
			// The whole body solved, as the striker's: the pelvis over feet that only move by stepping, the squat on
			// the toes, the chest over the knees leaning with the take, the palms on the gloves' targets. The clips
			// take over for a run, a dive or a throw.
			{
				FCricketBatterPose& S = P.Batter;
				const FVector2D Face = FVector2D(Fwd).GetSafeNormal();
				const FVector Right = Who->GetActorRightVector();
				FVector2D Ideal[2];
				float Lift[2];
				CricketKeeper::StanceFeet(FVector2D(At), Face, Drop, Ideal);
				CricketKeeper::StepFeet(KeeperFeet, Ideal, GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f, Lift);
				// The hips lag half the feet's lag behind the body's travel: carried by the steps, not floating.
				const FVector2D Pelvis = FVector2D(At) + 0.25f * (KeeperFeet.Ball[0] + KeeperFeet.Ball[1] - Ideal[0] - Ideal[1]);
				S.Weight = 1.f - P.JogWeight;
				S.bGloves = true;
				S.Pelvis = ToWorld(FVector(Pelvis.X, Pelvis.Y, 0.f));
				S.Drop = Drop * 100.f;
				S.Chest = P.ChestFacing;
				S.Hips = FMath::Lerp(Fwd, P.ChestFacing.GetSafeNormal2D(), 0.4f);
				const float Bend = FMath::DegreesToRadians(P.ChestBend);
				const float Lean = P.ShouldersWeight * FMath::Clamp(float((P.ShouldersAt - ToWorld(ChestF)) | Right) / 100.f, -0.35f, 0.35f);
				S.ChestUp = (FVector::UpVector * FMath::Cos(Bend) + P.ChestFacing.GetSafeNormal2D() * FMath::Sin(Bend) + Right * (0.6f * Lean)).GetSafeNormal();
				const float Low = FMath::Clamp((Drop - CricketKeeper::RelaxedDrop) / 0.3f, 0.f, 1.f);
				const float Splay = FMath::DegreesToRadians(FMath::Lerp(10.f, 25.f, Low));
				for (int32 Leg = 0; Leg < 2; ++Leg)
				{
					S.Ball[Leg] = ToWorld(FVector(KeeperFeet.Ball[Leg].X, KeeperFeet.Ball[Leg].Y, 0.f));
					S.Toe[Leg] = Fwd * FMath::Cos(Splay) + Right * ((Leg == 0 ? -1.f : 1.f) * FMath::Sin(Splay));
					// On the toes in the squat; the heel comes up further through a step.
					S.Heel[Leg] = FMath::Lerp(4.f, 32.f, FMath::Clamp((Drop - 0.2f) / 0.25f, 0.f, 1.f)) + 25.f * Lift[Leg] / CricketKeeper::StepLift;
					S.Lift[Leg] = Lift[Leg] * 100.f;
				}
			}
		}
		else
		{
			// Premium outfield pipeline (original): ready crouch with split-step, ball tracking,
			// cupped take per EFieldAction (flat/high/low/rope/relay/ground), absorb-and-secure to
			// the chest, then a crow-hop into the throw. Routine catches stay on their feet; the
			// captured dive clip (below) only owns genuine full-stretch takes via UseDiveClip.
			const FFieldingOutcome& Fd = Result.Fielding;
			const bool bTaker = bLive && Fd.Fielder == I && Fd.Action != EFieldAction::None && Fd.Boundary == 0;
			const int32 Act = bTaker ? int32(Fd.Action) : 0;
			const FVector Chest0 = At + FVector(0.f, 0.f, ShoulderHeight - MarkerHeight - 0.1f);
			const FVector Right = Who->GetActorRightVector();
			const float WorldT = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			const float Breathe = 0.008f * FMath::Sin(WorldT * 1.4f + 0.7f * I);
			// Ready depth: walking in vs the taker's crouch per action.
			// The taker runs tall and crouches into the take, not through the chase.
			const float Crouch = bTaker ? CricketPose::TakeCrouchWeight(Post, Fd.FieldTime) : 0.f;
			float ReadyDrop = (!F.bBowler && (DPhase == EDeliveryPhase::RunUp || (bLive && T < Result.ContactTime + 0.4f))) ? 0.12f : 0.f;
			if (bTaker) ReadyDrop = FMath::Lerp(ReadyDrop, CricketPose::CatchReadyDrop(Act, true), Crouch);
			ReadyDrop += Breathe;
			// Take geometry in actor space.
			const FVector TakePoint = bTaker ? FVector(Fd.FieldPos.X, Fd.FieldPos.Y, Fd.FieldPos.Z) : Chest0;
			const FVector2D ToTake2D = bTaker ? FVector2D(TakePoint.X - At.X, TakePoint.Y - At.Y) : FVector2D::ZeroVector;
			const FVector Fwd2D3(Fwd.X, Fwd.Y, 0.f);
			const FVector2D Fwd2D = FVector2D(Fwd2D3).GetSafeNormal();
			const float Cross = Fwd2D.X * ToTake2D.Y - Fwd2D.Y * ToTake2D.X;
			const float LateralM = bTaker ? FMath::Clamp(Cross, -1.5f, 1.5f) : 0.f;
			const float HeightM = bTaker ? TakePoint.Z : 1.0f;
			const float Approach = bTaker ? Ramp(Post, Fd.FieldTime - 0.45f, Fd.FieldTime) : 0.f;
			const float HoldUntil = bTaker ? Fd.FieldTime + 0.35f : 0.f;
			const float Secure = bTaker && Post >= Fd.FieldTime ? CricketPose::CatchSecure(Post, Fd.FieldTime, HoldUntil) : 0.f;
			const float TakeW = bTaker ? (Post < Fd.FieldTime ? Approach : Secure) : 0.f;
			// Gloves: ready cup low and forward; converge onto the cup offset at the take; give
			// toward the chest after; back to ready as the secure releases.
			const FVector ReadyHands = Chest0 + Fwd * 0.35f - FVector(0.f, 0.f, 0.45f);
			FVector Gloves = ReadyHands;
			float Near = 0.f;
			if (bLive && !bTaker)
			{
				const FVector Soon = Result.BallAt(T + 0.12f);
				Near = 1.f - Ramp(FVector::Dist(Soon, Chest0), 0.8f, 1.8f);
				const FVector ChestR = Chest0 - FVector(0.f, 0.f, ReadyDrop);
				Gloves = FMath::Lerp(ReadyHands, ChestR + (Soon - ChestR).GetClampedToMaxSize(0.7f), Near);
			}
			if (bTaker)
			{
				const FVector ChestR = Chest0 - FVector(0.f, 0.f, ReadyDrop);
				const FVector GloveOff = CricketPose::CatchGloveOffset(Act, HeightM, LateralM);
				const FVector Cup = ChestR + Fwd * GloveOff.X + Right * GloveOff.Y + FVector(0.f, 0.f, GloveOff.Z);
				const FVector Tracked = ChestR + (Result.BallAt(FMath::Clamp(Post + Result.ContactTime, 0.f, Result.DeadTime)) - ChestR).GetClampedToMaxSize(0.75f);
				const FVector PreTake = FMath::Lerp(FMath::Lerp(ReadyHands, Tracked, FMath::Min(Near + 0.4f, 1.f)), Cup, Approach);
				const float Give = CricketPose::CatchGive(Post, Fd.FieldTime);
				const FVector Held = CricketPose::CatchSecureOffset(GloveOff, Give);
				const FVector Given = ChestR + Fwd * Held.X + Right * Held.Y + FVector(0.f, 0.f, Held.Z);
				const FVector PostTake = FMath::Lerp(Given, ReadyHands, 1.f - Secure);
				Gloves = Post < Fd.FieldTime ? PreTake : PostTake;
				Near = TakeW;
			}
			// Drop dips into the take, then recovers with the secure.
			float Dip = 0.f;
			if (bTaker)
			{
				if (Fd.Action == EFieldAction::CatchLow) Dip = 0.06f;
				else if (Fd.Action == EFieldAction::CatchFlat) Dip = 0.05f;
				else if (Fd.Action == EFieldAction::PickupClean) Dip = 0.05f;
				else if (Fd.Action == EFieldAction::LongBarrier) Dip = 0.04f;
			}
			const float Drop = FMath::Lerp(ReadyDrop, ReadyDrop + Dip, TakeW);
			const FVector Chest = Chest0 - FVector(0.f, 0.f, Drop);
			// Cup spread: open tracking, wide at the take, together once secured.
			float Spread = FMath::Lerp(0.07f, Fd.Action == EFieldAction::CatchHigh ? 0.10f : 0.14f, FMath::Clamp(TakeW, 0.f, 1.f));
			if (bTaker && Post >= Fd.FieldTime)
				Spread = FMath::Lerp(Spread, 0.03f, Secure * 0.85f);
			const FVector Across = FVector::CrossProduct((Gloves - Chest).GetSafeNormal(UE_SMALL_NUMBER, Fwd), FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER, Right);
			for (int32 H = 0; H < 2; ++H)
			{
				const float S = H == 0 ? 1.f : -1.f;
				P.Hand[H] = ToWorld(Gloves + Across * (Spread * S));
				const float ElbOut = HeightM > 1.1f ? 0.42f : 0.5f;
				const float ElbDown = HeightM > 1.1f ? 0.28f : HeightM < 0.45f ? 0.58f : 0.5f;
				P.Elbow[H] = ToWorld(Chest + Across * (ElbOut * S) - FVector(0.f, 0.f, ElbDown));
				P.HandWeight[H] = FMath::Max(Near, bTaker ? TakeW : 0.f);
			}
			// Body behind the line: shoulders travel with the take over planted feet.
			if (bTaker && TakeW > 0.f)
			{
				const FVector LatDir = Right * (LateralM >= 0.f ? 1.f : -1.f);
				const float LatShift = FMath::Clamp(FMath::Abs(LateralM) * 0.3f, 0.f, 0.30f);
				P.ShouldersAt = ToWorld(Chest + LatDir * LatShift);
				const bool bFullStretch = CricketPose::UseDiveClip(Act, Fd.bDive, FMath::Abs(LateralM), HeightM);
				P.ShouldersWeight = TakeW * (bFullStretch ? 0.3f : 0.5f);
			}
			// Crow-hop gather into an overarm throw: shift toward the target just before release.
			if (bTaker && Run.ThrowRelease > 0.f && Run.ThrowType == EThrowType::Overarm)
			{
				const float Gather = CricketPose::ThrowGatherWeight(Post, Run.ThrowRelease);
				if (Gather > 0.f)
				{
					const FVector2D To = FVector2D(Run.bThrowToStrikerEnd ? 0.f : PitchLength, 0.f) - FVector2D(At.X, At.Y);
					const FVector2D Aim = To.GetSafeNormal();
					P.PelvisOffset = FVector(float(Aim.X) * 12.f * Gather, float(Aim.Y) * 12.f * Gather, -100.f * Drop);
				}
				else
					P.PelvisOffset = FVector(0.f, 0.f, -100.f * Drop);
			}
			else
				P.PelvisOffset = FVector(0.f, 0.f, -100.f * Drop);
			float Bend = FMath::Min(38.f, 6.f + 70.f * Drop); // near upright standing or running, folded into a low take
			if (bTaker && Fd.Action == EFieldAction::CatchHigh) Bend *= 0.5f;
			if (bTaker && Fd.Action == EFieldAction::LongBarrier) Bend = FMath::Lerp(Bend, FMath::Max(Bend, 30.f), Crouch);
			// Stands tall again with the ball held in front, rather than staying folded over it.
			if (bTaker) Bend = FMath::Lerp(Bend, 12.f, FMath::SmoothStep(Fd.FieldTime, Fd.FieldTime + 0.4f, Post));
			P.ChestBend = Bend;
			// Gaze: track the ball in, watch it into the hands through the take.
			if (bLive)
			{
				const FVector Gaze = bTaker && Post > Fd.FieldTime - 0.5f ? TakePoint : Result.BallAt(T + 0.12f);
				P.LookAt = ToWorld(Gaze);
				P.LookWeight = FMath::Max(Near, TakeW);
				if (P.ChestFacing.IsNearlyZero()) P.ChestFacing = Fwd;
			}
		}

		// A captured dive or throw moves the whole body, so the reach, crouch and gaze give way to it.
		// Shared by keeper and outfielders: keeper dives use the same clip path, selected by the solver.
		{
			const FVector ChestT = At + FVector(0.f, 0.f, ShoulderHeight - MarkerHeight - 0.1f) + P.PelvisOffset / 100.f;
		const FClipPlay Dive = bLive && Diving && I == Result.Fielding.Fielder && (!F.bKeeper || CricketKeeper::UseSideDive(F.Home, Result.Fielding.FieldPos, Result.Fielding.bDive)) ? DiveClip(Post, Result.Fielding.FieldTime) : FClipPlay();
		const FClipPlay Throw = bLive ? ThrowPlay(I, Post) : FClipPlay();
		P.Clip[0] = Diving;
		P.ClipTime[0] = Dive.Time;
		P.ClipWeight[0] = Dive.Weight;
		P.Clip[1] = ThrowAnim;
		P.ClipTime[1] = Throw.Time;
		P.ClipWeight[1] = Throw.Weight;
		const float Own = (1.f - Dive.Weight) * (1.f - Throw.Weight);
		for (int32 H = 0; H < 2; ++H) P.HandWeight[H] *= Own;
			P.PelvisOffset *= Own;
			P.ChestBend *= Own;
			P.ShouldersWeight *= Own;
			P.LookWeight *= Own;
			P.Batter.Weight *= Own;

		// Otherwise (no captured throw, or an underarm flick) the arm windmills over by IK.
		const bool bThrower = I == Result.Fielding.Fielder && Run.ThrowRelease > 0.f;
		const bool bRelay = I == Run.RelayMove.Fielder && Run.RelayRelease > 0.f;
		const bool bClipThrow = ThrowAnim && (bRelay || Run.ThrowType != EThrowType::Underarm);
		const float Tt = Post - (bRelay ? Run.RelayRelease : Run.ThrowRelease);
		const float ThrowW = bLive && (bThrower || bRelay) && !bClipThrow ? BowlingArmWeight(Tt) : 0.f;
		if (ThrowW > 0.f)
		{
			const FVector Stumps(Run.bThrowToStrikerEnd ? 0.f : PitchLength, 0.f, 0.f);
			const bool bToRelay = !bRelay && Fielders.IsValidIndex(Run.RelayMove.Fielder);
			const FVector To = bToRelay ? SimAt(Fielders[Run.RelayMove.Fielder]) : Stumps;
			const FVector Aim = FVector(To.X - At.X, To.Y - At.Y, 0.f).GetSafeNormal(UE_SMALL_NUMBER, Fwd);
			const FVector Right(-Aim.Y, Aim.X, 0.f);
			const FVector Shoulder = ChestT + Right * ShoulderHalfWidth;
			const bool bUnderarm = !bRelay && Run.ThrowType == EThrowType::Underarm;
			P.Hand[1] = ToWorld(ArmCircle(Shoulder, Aim, bUnderarm ? UnderarmArmAngle(Tt) : BowlingArmAngle(Tt), 0.8f));
			P.Elbow[1] = ToWorld(Shoulder + Right);
			P.HandWeight[1] = ThrowW;
			P.HandWeight[0] *= 1.f - ThrowW;
			P.ChestFacing = FMath::Lerp(Fwd, Aim, ThrowW);
		}
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
	const bool bFullPass = bReplay && ActivePackage.Angles.IsValidIndex(ReplayAngle()) && ActivePackage.Angles[ReplayAngle()].bFullPass;
	const bool bReview = IsReviewing();
	const bool bScorecard = ShowingScorecard();
	const float T = bAwaitingThirdUmpire ? ThirdUmpireBallTime() : bReplay ? ReplayBallTime() : DPhase == EDeliveryPhase::DeadBall ? Result.DeadTime : PhaseTime;

	// Ball sounds when the presented ball passes each moment, so a replay plays them again.
	// Every pick is contact-driven (middle/toe/edge/pad/keeper/catch), never one sample rescaled.
	if (T < PrevCueT) PrevCueT = T; // a new ball or a replay rewinds the clock
	const float NowSfx = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (bLive)
	{
		using CricketAudio::ECue;
		auto Crossed = [&](float At) { return At > PrevCueT && At <= T; };
		if (Result.PitchTime > 0.f && Crossed(Result.PitchTime))
			PlayCue(ECue::Bounce, CricketAudio::PitchVolume(Result.SpeedKph));
		if (Result.Contact.HasContact() && Crossed(Result.ContactTime))
		{
			CricketAudioDirector::FSfxPick Pick = CricketAudioDirector::ContactSfx(Result);
			if (bReplay) Pick.Volume = FMath::Max(Pick.Volume, 0.7f); // replay: impact emphasised, bed reduced
			PlayCue(Pick.Cue, Pick.Volume);
			// No camera punch (CRICKET26.mp4): TV holds its breath through contact; impact is sound + ball.
		}
		if (Result.bPadImpact && Crossed(Result.Tracking.ImpactTime)
			&& (!Result.Contact.HasContact() || FMath::Abs(Result.Tracking.ImpactTime - Result.ContactTime) > 0.05f))
			PlayCue(ECue::PadThud, 0.5f);
		const FFieldingOutcome& Fd = Result.Fielding;
		if (Fd.Fielder >= 0 && Crossed(Fd.FieldTime))
		{
			const bool bKeeper = Ctx.Field.IsValidIndex(Fd.Fielder) && Ctx.Field[Fd.Fielder].bKeeper;
			if (bKeeper && Fd.Action == EFieldAction::KeeperTake) PlayCue(CricketAudioDirector::KeeperSfx(Result).Cue, CricketAudioDirector::KeeperSfx(Result).Volume);
			else if (Fd.bCaught) PlayCue(CricketAudioDirector::CatchSfx(Result).Cue, CricketAudioDirector::CatchSfx(Result).Volume);
			else if (!bKeeper && Fd.Action != EFieldAction::None && Fd.Boundary == 0) PlayCue(ECue::CatchPop, 0.3f); // ground-fielding take
		}
		const FRunningOutcome& Run = Result.Running;
		if (Run.ThrowRelease > 0.f && Crossed(Result.ContactTime + Run.ThrowRelease))
			PlayCue(ECue::ThrowRelease, CricketAudioDirector::ThrowVolume(Result));
		if (Run.RelayRelease > 0.f && Crossed(Result.ContactTime + Run.RelayRelease))
			PlayCue(ECue::ThrowRelease, CricketAudioDirector::ThrowVolume(Result));
		if (Result.bStumpsHit && Crossed(Result.StumpsTime)) PlayCue(ECue::Stumps, 1.f);
		if (Result.BrokenTime >= 0.f && Crossed(Result.BrokenTime)) PlayCue(ECue::Stumps, 0.9f);
	}
	// Bowler's run-up footsteps: subtle, strictly under the cricket (never louder than contact).
	if (DPhase == EDeliveryPhase::RunUp && NowSfx >= NextFootstepAt)
	{
		PlayCue(CricketAudio::ECue::Footstep, CricketAudioDirector::FootstepVolume());
		NextFootstepAt = NowSfx + CricketAudioDirector::RunUpStride;
	}
	PrevCueT = T;
	// Crowd: layered energy model (reaction + pre-delivery tension + micro-drop), ducked slightly
	// under active commentary, reduced bed inside replays. Excitement mirrors the energy for the stands.
	if (DPhase == EDeliveryPhase::Waiting || DPhase == EDeliveryPhase::RunUp)
		CricketAudioDirector::PreDelivery(AudioDir, Match);
	const bool bCommActive = CricketCommentaryDirector::IsSpeaking(CommDir, NowSfx);
	if (CrowdAudio) CrowdAudio->SetVolumeMultiplier(CricketAudioDirector::TickCrowd(AudioDir, Dt, NowSfx, bCommActive, bReplay)
		* CricketAudio::BusTrim(CricketAudio::EMixBus::Crowd));
	CrowdLevel = AudioDir.CrowdEnergy;
	// Voiced commentary: the picked line once its moment has breathed, then the analyst's handoff.
	CricketCommentaryDirector::Update(CommDir, NowSfx, BallsPlayed);
	if (const FString Handoff = CricketCommentaryDirector::TakeHandoff(CommDir, NowSfx); !Handoff.IsEmpty())
	{
		Commentary = Handoff;
		Say(Handoff, FString(), NowSfx);
	}
	if (VoiceWave && VoiceAt >= 0.f && NowSfx >= VoiceAt)
	{
		VoiceWave->ResetAudio(); // a new call cuts the last one off
		VoiceWave->QueueAudio(reinterpret_cast<const uint8*>(VoicePending.GetData()), VoicePending.Num() * sizeof(int16));
		VoiceAt = -1.f;
	}
	// Player vocals: the fielders' and batters' shouts on the director's timing hooks.
	if (CricketAudioDirector::EVocal V = CricketAudioDirector::PollVocal(AudioDir, NowSfx); V != CricketAudioDirector::EVocal::None && VocalWave)
	{
		static const TCHAR* VocalNames[] = { TEXT("None"), TEXT("Howzat"), TEXT("Run"), TEXT("No"), TEXT("Wait"), TEXT("CatchCall"), TEXT("Celebrate"), TEXT("Frustrated") };
		const TArray<int16> Pcm = CricketAudio::LoadClip(FString::Printf(TEXT("Vocal/%s.pcm"), VocalNames[int32(V)]));
		VocalWave->ResetAudio();
		VocalWave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.GetData()), Pcm.Num() * sizeof(int16));
	}
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
	// The authored action carries the body itself, from the run-in to the end of the follow-through, facing the
	// striker: the actor holds where the clip's bowling hand lets go at the simulation's release point and carries
	// only the share of the travel the clip does not (fading in and out). Then they walk on to their mark.
	const CricketPose::FClipPlay Bowl = BowlPlay(T, bLive);
	USkeletalMeshComponent* BowlerBody = BodyOf(Bowler);
	FVector BowlerShift = FVector::ZeroVector; // the clip's pelvis off the actor
	if (UAnimSequence* Bowling = BowlAnim(); Bowling && BowlerBody)
	{
		const FQuat Facing(FRotator(0.f, 180.f, 0.f));
		const FQuat Turn = Facing * Bowler->GetActorQuat().Inverse() * BowlerBody->GetComponentQuat();
		auto Flat = [&](const FVector& C) { const FVector V = Turn.RotateVector(C * BowlerBody->GetComponentScale()) / 100.f; return FVector(V.X, V.Y, 0.f); }; // clip cm to sim m
		static const UAnimSequence* CachedReleaseSeq = nullptr;
		static float CachedReleaseArm = 0.f;
		static FVector CachedHandAtRelease = FVector::ZeroVector;
		static FVector CachedPelvisAtRelease = FVector::ZeroVector;
		if (Bowling != CachedReleaseSeq || Arm != CachedReleaseArm)
		{
			CachedReleaseSeq = Bowling;
			CachedReleaseArm = Arm;
			CachedHandAtRelease = ClipBoneAt(Bowling, Arm > 0.f ? TEXT("hand_r") : TEXT("hand_l"), CricketPose::BowlClipRelease);
			CachedPelvisAtRelease = ClipBoneAt(Bowling, TEXT("pelvis"), CricketPose::BowlClipRelease);
		}
		const FVector Pelvis = Flat(ClipBoneAt(Bowling, TEXT("pelvis"), Bowl.Time));
		const FVector Anchor = FVector(PitchLength - 1.5f, 0.26f * Arm, 0.925f) - Flat(CachedHandAtRelease);
		BowlerShift = Bowl.Weight * Pelvis;
		BowlerPos = Anchor + (1.f - Bowl.Weight) * Pelvis;
		// Run in beside the stumps, not through them: the whole body moves out while it passes them.
		const float Scale = BowlerBody->GetComponentScale().Z;
		if (Bowling != StumpPushClip || Scale != StumpPushScale)
		{
			StumpPushClip = Bowling;
			StumpPushScale = Scale;
			StumpPush = CricketPose::StumpPush([&](int32 I, float At)
			{
				const FVector C = ClipBoneAt(Bowling, CricketPose::StumpBones[I].Name, At);
				return Anchor + Flat(C) + FVector(0.f, 0.f, C.Z * Scale / 100.f - Anchor.Z);
			}, Arm);
		}
		BowlerPos.Y += Arm * StumpPush * CricketPose::StumpPushWeight(Anchor.X + Pelvis.X, Anchor.X + Flat(CachedPelvisAtRelease).X);
		const float After = T - (CricketPose::BowlClipEnd - CricketPose::BowlClipRelease);
		if (bLive && After > 0.f) BowlerPos = FMath::Lerp(BowlerPos, FVector(FollowThrough.X, FollowThrough.Y, BowlerPos.Z), FMath::Min(After, 1.f));
		if (!bLive || Bowl.Weight > 0.f)
		{
			Bowler->SetActorRotation(Facing);
			FigureStates.FindOrAdd(Bowler).bHeld = true;
		}
	}
	Bowler->SetActorLocation(ToWorld(BowlerPos));

	TargetMarker->SetActorHiddenInGame(!(HumanBowls() && (DPhase == EDeliveryPhase::Waiting || DPhase == EDeliveryPhase::RunUp)));
	TargetMarker->SetActorLocation(ToWorld(FVector(HumanPlan.Length, HumanPlan.Line * Off, 0.003f)));

	// Ball.
	// In the bowling hand's fingers until release (last frame's hand: the arm is posed after this); a distant LOD
	// without fingers leaves their sockets stale, so the palm then.
	static const FName MidR(TEXT("middle_02_r")), MidL(TEXT("middle_02_l"));
	static const FName IdxR(TEXT("index_02_r")), IdxL(TEXT("index_02_l"));
	static const FName RngR(TEXT("ring_02_r")), RngL(TEXT("ring_02_l"));
	static const FName HndR(TEXT("hand_r")), HndL(TEXT("hand_l"));
	const FName MidBone = Arm > 0.f ? MidR : MidL;
	const FName IdxBone = Arm > 0.f ? IdxR : IdxL;
	const FName RngBone = Arm > 0.f ? RngR : RngL;
	const FName HndBone = Arm > 0.f ? HndR : HndL;
	FVector BallPos = ToWorld(FVector(BowlerPos.X - 0.3f, 0.4f * Arm, 1.1f));
	if (BowlerBody && BowlerBody->RequiredBones.Contains(FBoneIndexType(BowlerBody->GetBoneIndex(MidBone))))
		BallPos = (BowlerBody->GetSocketLocation(IdxBone) + BowlerBody->GetSocketLocation(MidBone) + BowlerBody->GetSocketLocation(RngBone)) / 3.f;
	else if (BowlerBody) BallPos = BowlerBody->GetSocketLocation(HndBone);
	if (bLive && T >= 0.f) BallPos = ToWorld(Result.BallAt(T)); // a full replay opens before release: still in hand
	// In a fielder's hands from the take until they let it go. The simulation holds it where it was taken, but a
	// captured dive or throw carries the body up to a metre from there. In the right hand: the midpoint of two hands
	// floats in the air whenever they part.
	// ponytail: the thrown path starts from the take, so the ball jumps from the hand on the release frame; start the
	// throw at the hand if it shows.
	const int32 HolderIndex = bLive ? Result.HolderAt(Post) : -1;
	const AActor* Holder = !Ctx.Field.IsValidIndex(HolderIndex) ? nullptr : Ctx.Field[HolderIndex].bBowler ? Bowler.Get() : Fielders.IsValidIndex(HolderIndex) ? Fielders[HolderIndex].Get() : nullptr;
	// Held, it rides the hand bone: the bodies are posed after this tick, so a socket read here is last frame's hand
	// and the ball trailed a diver getting up by a metre.
	USkeletalMeshComponent* HeldIn = Holder ? BodyOf(Holder) : nullptr;
	if (HeldIn)
	{
		if (Ball->GetRootComponent()->GetAttachParent() != HeldIn)
			Ball->AttachToComponent(HeldIn, FAttachmentTransformRules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false), TEXT("hand_r"));
		BallPos = HeldIn->GetSocketLocation(TEXT("hand_r"));
	}
	else
	{
		if (Ball->GetAttachParentActor()) Ball->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Ball->SetActorLocation(BallPos);
	}
	// Drawn stretched along its path by the distance it covers in a 1/60 s shutter, as a broadcast camera
	// blurs it, so a fast ball reads as a streak rather than strobing dots. A jump (new ball, replay) is not motion.
	const float Size = 2.f * BallRadius * BallDisplayScale(FVector::Dist(BallPos, Camera->GetActorLocation()) / 100.f, Camera->GetCameraComponent()->FieldOfView, ViewAspect);
	const FVector BallVel = Dt > 0.f ? (BallPos - LastBallPos) / 100.f / Dt : FVector::ZeroVector;
	LastBallPos = BallPos;
	const float Streak = !HeldIn && BallVel.Size() < 60.f ? FMath::Min(BallVel.Size() / 60.f, 6.f * Size) : 0.f;
	Ball->SetActorRotation(Streak > Size ? BallVel.Rotation() : FRotator::ZeroRotator);
	Ball->GetRootComponent()->SetWorldScale3D(FVector(FMath::Max(Size, Streak), Size, Size));

	// Fielders run where the coordinator sends them (chase, back up, cover the stumps) at the speed the
	// solver assumed, so nobody arrives sooner than they physically could.
	Diving = nullptr;
	if (bLive)
	{
		for (const FFielderMove& Move : Result.Fielding.Moves)
		{
			if (!Ctx.Field.IsValidIndex(Move.Fielder) || Post < Move.Start) continue;
			const FFielder& Who = Ctx.Field[Move.Fielder];
			const FVector2D P = CricketField::PositionOf(Move, Who, Post, Ctx.Fielding.RunSpeed);
			if (Who.bBowler) Bowler->SetActorLocation(ToWorld(FVector(P.X, P.Y, 0.925f) - BowlerShift)); // the body, not the actor, where sent
			else if (Fielders.IsValidIndex(Move.Fielder)) Fielders[Move.Fielder]->SetActorLocation(ToWorld(FVector(P.X, P.Y, 0.9f)));
		}
		const FFielderMove& Relay = Result.Running.RelayMove;
		if (Fielders.IsValidIndex(Relay.Fielder) && Post >= Relay.Start)
		{
			const FVector2D P = CricketField::PositionOf(Relay, Ctx.Field[Relay.Fielder], Post, Ctx.Fielding.RunSpeed);
			Fielders[Relay.Fielder]->SetActorLocation(ToWorld(FVector(P.X, P.Y, 0.9f)));
		}
		const FFieldingOutcome& Fd = Result.Fielding;
		const EFieldAction A = Fd.Action;
		if (Fd.Fielder == 0 && Ctx.Field.IsValidIndex(0) && Fielders.IsValidIndex(0)
			&& !CricketKeeper::UseSideDive(Ctx.Field[0].Home, Fd.FieldPos, Fd.bDive))
		{
			// The solver stops within reach; the keeper still has to get across to the take. The run is sent at the
			// ball itself, so it stops with the ball in the gloves rather than carrying on through the take point.
			const FFielderMove* StepMove = Fd.Moves.FindByPredicate([](const FFielderMove& M) { return M.Fielder == 0; });
			const float Begin = Result.Contact.HasContact() ? FMath::Max(0.f, Fd.FieldTime - 0.3f) : Fd.FieldTime - 0.45f;
			const FVector2D Body = CricketKeeper::TakeBodyAt(StepMove, Ctx.Field[0], Ctx.Fielding.RunSpeed, Fd.FieldPos, Fd.FieldTime, Begin, Post);
			Fielders[0]->SetActorLocation(ToWorld(FVector(Body.X, Body.Y, 0.9f)));
		}
		const FFielderMove* Chase = Fd.Moves.FindByPredicate([&Fd](const FFielderMove& M) { return M.Fielder == Fd.Fielder; });
		// Premium fix: the captured keeper-dive clip only owns genuine full-stretch takes.
		// Routine flat/high/low catches stay on their feet with cupped hands (UpdatePoses), so the
		// catch never pops into a soccer-keeper dive. Slide stops lean procedurally.
		float DiveLateral = 1.5f;
		{
			const FVector2D Take2D(Fd.FieldPos.X, Fd.FieldPos.Y);
			if (Chase && Ctx.Field.IsValidIndex(Fd.Fielder))
			{
				// How far the take is from where the fielder launches: a genuine full-stretch
				// take is metres from the launch, a routine take is already there.
				const FVector2D Launch = CricketField::PositionOf(*Chase, Ctx.Field[Fd.Fielder], Fd.FieldTime - CricketPose::DiveClipStretch, Ctx.Fielding.RunSpeed);
				DiveLateral = FVector2D::Distance(Launch, Take2D);
			}
		}
		const bool bDiveClip = CricketPose::UseDiveClip(int32(A), Fd.bDive, DiveLateral, Fd.FieldPos.Z);
		const bool bDiving = (Fd.bDive || A == EFieldAction::CatchDiving) && bDiveClip && (!Ctx.Field.IsValidIndex(Fd.Fielder) || !Ctx.Field[Fd.Fielder].bKeeper || CricketKeeper::UseSideDive(Ctx.Field[Fd.Fielder].Home, Fd.FieldPos, true));
		const bool bGround = bDiving || A == EFieldAction::SlideStop;
		USkeletalMeshComponent* DiverBody = Fielders.IsValidIndex(Fd.Fielder) ? BodyOf(Fielders[Fd.Fielder]) : nullptr;
		const CricketPose::FClipPlay Dive = CricketPose::DiveClip(Post, Fd.FieldTime);
		if (bDiving && DiveAnims[0] && DiveAnims[1] && DiverBody && Chase && !Ctx.Field[Fd.Fielder].bBowler && Post >= Fd.FieldTime - CricketPose::DiveClipStretch)
		{
			// The captured dive carries the body across on its own. Stand the fielder where it launches so its hands
			// reach the take on time, turned so it dives from where they were running, to whichever side leaves them
			// facing where the ball came from. After it they stay where they got up.
			AActor* Who = Fielders[Fd.Fielder];
			const FVector2D Take(Fd.FieldPos), Launch = CricketField::PositionOf(*Chase, Ctx.Field[Fd.Fielder], Fd.FieldTime - CricketPose::DiveClipStretch + CricketPose::DiveClipLaunch, Ctx.Fielding.RunSpeed);
			const FVector2D Out = Launch.Equals(Take, 0.01f) ? FVector2D(-1.f, 0.f) : (Launch - Take).GetSafeNormal(); // from the take back to the launch
			const FVector2D Came = -FVector2D(Result.BallAt(Fd.FieldTime) - Result.BallAt(Fd.FieldTime - 0.1f)).GetSafeNormal();
			const FQuat ActorQuat = Who->GetActorQuat(), BodyQuat = DiverBody->GetComponentQuat();
			const FVector BodyScale = DiverBody->GetComponentScale();
			auto Flat = [&](const FVector& C) { return FVector2D(CricketPose::ClipInActor(ActorQuat, BodyQuat, BodyScale, C)); }; // clip cm to actor m
			static const UAnimSequence* CachedDiveSeq[2] = { nullptr, nullptr };
			static FVector CachedDiveHandsRaw[2] = { FVector::ZeroVector, FVector::ZeroVector };
			static FVector CachedDivePelvisZero[2] = { FVector::ZeroVector, FVector::ZeroVector };
			for (int32 Side = 0; Side < 2; ++Side)
			{
				if (DiveAnims[Side] != CachedDiveSeq[Side])
				{
					CachedDiveSeq[Side] = DiveAnims[Side];
					if (DiveAnims[Side])
					{
						CachedDiveHandsRaw[Side] = 0.5f * (ClipBoneAt(DiveAnims[Side], TEXT("hand_l"), CricketPose::DiveClipStretch) + ClipBoneAt(DiveAnims[Side], TEXT("hand_r"), CricketPose::DiveClipStretch)) - ClipBoneAt(DiveAnims[Side], TEXT("pelvis"), 0.f);
						CachedDivePelvisZero[Side] = ClipBoneAt(DiveAnims[Side], TEXT("pelvis"), 0.f);
					}
				}
			}
			float Yaw = 0.f, Best = -2.f;
			FVector2D Reach;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const FVector2D Hands = Flat(CachedDiveHandsRaw[Side]);
				const float Y = FMath::RadiansToDegrees(FMath::Atan2(-Out.Y, -Out.X) - FMath::Atan2(Hands.Y, Hands.X));
				const float Facing = FVector2D::DotProduct(FVector2D(FMath::Cos(FMath::DegreesToRadians(Y)), FMath::Sin(FMath::DegreesToRadians(Y))), Came);
				if (Facing > Best) { Best = Facing; Yaw = Y; Reach = Hands; Diving = DiveAnims[Side]; }
			}
			// The clip's full-length dive (~3.6 m to the hands) is longer than the solver's DiveReach (2.3 m). The
			// fielder pushes off where they ran to and the flight takes up the difference, so the dive is as long as
			// the take needs, the feet never slide on the grass and the hands still meet the ball on the take.
			const FVector2D From = Take + Out * Reach.Size();
			const float Flight = 1.f - FMath::SmoothStep(CricketPose::DiveClipLaunch, CricketPose::DiveClipStretch, Dive.Time);
			const FRotator Turn(0.f, Yaw, 0.f);
			const FVector PelvisZero = (Diving == DiveAnims[0]) ? CachedDivePelvisZero[0] : (Diving == DiveAnims[1]) ? CachedDivePelvisZero[1] : ClipBoneAt(Diving, TEXT("pelvis"), 0.f);
			const FVector Travel = Turn.RotateVector(FVector(Flat(ClipBoneAt(Diving, TEXT("pelvis"), FMath::Min(Dive.Time, CricketPose::DiveClipUp)) - PelvisZero), 0.f));
			// The actor carries whatever share of the travel the clip no longer does (fading out, or under the throw).
			const float Held = Dive.Weight * (1.f - ThrowPlay(Fd.Fielder, Post).Weight);
			const float In = CricketPose::DiveClipIn(Dive.Time);
			const FVector2D Base = From + (Launch - From) * Flight;
			const FVector There = FVector(Base.X, Base.Y, 0.9f) + (1.f - Held) * FVector(Travel.X, Travel.Y, 0.f);
			Who->SetActorLocation(ToWorld(FMath::Lerp(Who->GetActorLocation() / 100.f, There, In)));
			if (Dive.Weight > 0.f)
			{
				Who->SetActorRotation(FMath::Lerp(FRotator(0.f, Who->GetActorRotation().Yaw, 0.f), Turn, In));
				FFigureState& Carried = FigureStates.FindOrAdd(Fielders[Fd.Fielder]);
				Carried.bHeld = Carried.bCarried = true;
			}
		}
		else if (Fielders.IsValidIndex(Fd.Fielder) && !Ctx.Field[Fd.Fielder].bBowler)
		{
			// Going to ground for a slide (or a dive with no captured clip): the primary lies toward the ball from
			// just before the take until they are back up (the same time the solver charges before the throw).
			const float Down = FMath::Clamp((Post - Fd.FieldTime + 0.2f) / 0.2f, 0.f, 1.f) * FMath::Clamp((Fd.FieldTime + 0.8f - Post) / 0.3f, 0.f, 1.f);
			AActor* Who = Fielders[Fd.Fielder];
			const FVector2D Lean = (FVector2D(Fd.FieldPos) - FVector2D(Who->GetActorLocation() / 100.f)).GetSafeNormal();
			const FVector Up = FMath::Lerp(FVector::UpVector, FVector(Lean.X, Lean.Y, 0.25f).GetSafeNormal(), bGround ? 0.85f * Down : 0.f);
			Who->SetActorRotation(CricketPose::LeanRotation(Who->GetActorQuat(), Up));
		}

		// A captured throw is thrown along the thrower's forward: turn them to the target through the wind-up.
		for (const int32 I : { Fd.Fielder, Result.Running.RelayMove.Fielder })
		{
			const float W = Ctx.Field.IsValidIndex(I) ? ThrowPlay(I, Post).Weight : 0.f;
			AStaticMeshActor* Who = W <= 0.f ? nullptr : Ctx.Field[I].bBowler ? Bowler.Get() : Fielders.IsValidIndex(I) ? Fielders[I].Get() : nullptr;
			if (!Who) continue;
			const bool bRelay = I == Result.Running.RelayMove.Fielder;
			const FVector2D To = !bRelay && Fielders.IsValidIndex(Result.Running.RelayMove.Fielder) && Result.Running.RelayRelease > 0.f
				? FVector2D(Fielders[Result.Running.RelayMove.Fielder]->GetActorLocation() / 100.f)
				: FVector2D(Result.Running.bThrowToStrikerEnd ? 0.f : PitchLength, 0.f);
			const FVector2D Aim = To - FVector2D(Who->GetActorLocation() / 100.f);
			if (Aim.IsNearlyZero()) continue;
			Who->SetActorRotation(FMath::Lerp(FRotator(0.f, Who->GetActorRotation().Yaw, 0.f), FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(Aim.Y, Aim.X)), 0.f), W));
			FigureStates.FindOrAdd(Who).bHeld = true;
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
		// The striker sets off from where the stroke left them (a stride down the pitch, say), not their guard.
		const FVector2D GuardHome = FVector2D(StrikerHome());
		const FVector2D From = Run.RunTimes.Num() > 0 && Post < Run.RunTimes[0] + 0.3f ? StrikerFrom : GuardHome;
		const float SX = FMath::Lerp(0.9f, PitchLength - 2.0f, Along) + (From.X - 0.9f) * (1.f - Along), NX = FMath::Lerp(PitchLength - 2.0f, 0.9f, Along);
		// Each batter eases from where they stood into their own lane, either side of the pitch: the
		// non-striker's on the side they backed up (away from the bowler's arm), the striker's the other.
		const float Lane = FMath::SmoothStep(0.35f, 0.9f, Post);
		Striker->SetActorLocation(ToWorld(FVector(SX, FMath::Lerp(From.Y, 1.1f * Arm, Lane), 0.9f)));
		NonStriker->SetActorLocation(ToWorld(FVector(NX, FMath::Lerp(-1.9f * Arm, -1.1f * Arm, Lane), 0.9f)));
	}
	// Otherwise the striker's own footwork (UpdatePoses) moves them.

	// Buffer-posed replay: when the rolling buffer covers this replay angle, the actors take their
	// RECORDED presented transforms (the actual event), over the analytic re-pose. Otherwise the
	// analytic path below stands (same stored result data). The ball look-target follows the pose.
	bBufferPose = false;
	if (bReplay && !InReel() && ActivePackage.IsValid())
	{
		const int32 RA = ReplayAngle();
		// The buffer starts at release: a full replay's delivery stride is posed analytically, then the buffer
		// takes over from the same ball time the live pass recorded from.
		if (T >= 0.f && ActivePackage.Angles.IsValidIndex(RA)
			&& ReplayBuffer.HasCoverage(FMath::Max(ActivePackage.Angles[RA].StartTp, 0.f), ActivePackage.Angles[RA].EndTp))
		{
			FVector BufBall;
			TArray<FReplayActorPose> BufActors;
			if (ReplayBuffer.SampleAt(T, BufBall, BufActors) && BufActors.Num() == ReplayCast.Num())
			{
				if (Ball->GetAttachParentActor()) Ball->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
				Ball->SetActorLocation(BufBall);
				for (int32 I = 0; I < BufActors.Num(); ++I)
					if (ReplayCast.IsValidIndex(I) && ReplayCast[I])
						ReplayCast[I]->SetActorLocationAndRotation(BufActors[I].Position, BufActors[I].Rotation);
				BallPos = BufBall;
				bBufferPose = true;
			}
		}
	}

	// Camera: the broadcast director observes gameplay and chooses the shot. Geometry and timing come
	// from CricketBroadcast (isolated, tested); this block only feeds it the current frame.
	using namespace CricketBroadcast;
	const FFieldingOutcome& Fld = Result.Fielding;
	const float After = T - Result.ContactTime;
	FBroadcastFrame Frame;
	Frame.BallPos = BallPos;
	Frame.BallVel = BallVel;
	Frame.BowlerPos = Bowler->GetActorLocation() + ToWorld(BowlerShift);
	Frame.StrikerPos = Striker->GetActorLocation();
	Frame.NonStrikerPos = NonStriker->GetActorLocation();
	Frame.OffSign = Off;
	Frame.ArmSign = Arm;
	Frame.AfterContact = After;
	Frame.BallT = T;
	Frame.DeadTime = Result.DeadTime;
	Frame.Dismissal = DPhase == EDeliveryPhase::DeadBall ? Result.Dismissal : EDismissal::None; // the full replay knows the outcome from the first frame
	Frame.Boundary = Result.Fielding.Boundary;
	Frame.bCaught = Result.Fielding.bCaught;
	Frame.bCatchChance = Result.Fielding.bCatchChance;
	Frame.bWicketThis = bWicketThis;
	Frame.FieldTime = Result.Fielding.FieldTime;
	Frame.bRunOutChance = Result.Running.Attempted > 0 && (Result.Running.bRunOut || Result.Running.Margin < 0.3f);
	Frame.ThrowTime = Result.Running.ThrowRelease;
	Frame.bHasThrowEnd = Result.Running.Attempted > 0;
	Frame.bThrowToStrikerEnd = Result.Running.bThrowToStrikerEnd;
	Frame.BoundaryTime = Fld.Boundary > 0 ? Fld.BoundaryTime : -1.f;
	if (Fielders.IsValidIndex(0)) { Frame.KeeperPos = Fielders[0]->GetActorLocation(); Frame.bHasKeeper = true; }
	if (Fielders.IsValidIndex(Fld.Fielder) && Fld.Fielder >= 0 && !Ctx.Field[Fld.Fielder].bBowler)
	{
		Frame.FielderPos = Fielders[Fld.Fielder]->GetActorLocation();
		Frame.bHasFielder = true;
	}
	Frame.ContactPos = Result.Contact.ContactPos;
	Frame.ExitVel = Result.Contact.ExitVel;
	// Byes have no stroke: the reverse angle takes its station from where the ball ran instead.
	if (!Result.Contact.HasContact() && Result.BallPath.Num() > 1)
		Frame.ExitVel = Result.BallAt(Result.DeadTime) - Result.BallAt(Result.ContactTime);
	if (Fld.Boundary > 0)
	{
		Frame.bHasBoundaryCross = true;
		Frame.BoundaryCrossPos = ToWorld(Result.BallAt(Result.ContactTime + Fld.BoundaryTime));
	}
	// Look-ahead from the resolver's stored path (deterministic, spike-free): the camera frames where
	// the ball is GOING, never by chasing it.
	if (bLive && Result.BallPath.Num() > 1)
	{
		Frame.PredictedPos = ToWorld(Result.BallAt(FMath::Clamp(T + 0.3f, 0.f, Result.DeadTime)));
		Frame.bHasPrediction = true;
	}
	// Post-contact class from the actual result; a boundary still travelling holds its follow shot
	// until the rope is near, and a take holds wide until the ball is nearly there.
	Frame.ShotClass = ClassifyShot(Result, Frame.Dismissal);
	if (Frame.ShotClass == EShotClass::BoundaryTrajectory && After < Fld.BoundaryTime - 0.8f)
	{
		const float Loft = Frame.ExitVel.Z;
		Frame.ShotClass = Loft > 6.f ? EShotClass::LoftedOutfield : EShotClass::GroundOutfield;
	}

	// Broadcast phase: the dead ball keeps showing the ball story until it settles, the replay takes
	// over, or a referral/review flow owns the screen.
	const bool bSettledDead = DPhase == EDeliveryPhase::DeadBall && !bAwaitingThirdUmpire && !bReviewThis
		&& PhaseTime > (bReplayThis ? ReplayDelay + ReplayTotalTime() : 0.7f);
	FPhaseInput PhaseIn;
	PhaseIn.bRunUp = DPhase == EDeliveryPhase::RunUp;
	PhaseIn.bBallLive = DPhase == EDeliveryPhase::BallInPlay || (DPhase == EDeliveryPhase::DeadBall && !bSettledDead && !bReplay);
	PhaseIn.bDead = bSettledDead;
	PhaseIn.bReplaying = bReplay;
	PhaseIn.BallT = T;
	PhaseIn.ContactTime = Result.ContactTime;
	PhaseIn.bHasContact = Result.Contact.HasContact();
	PhaseIn.AfterContact = After;
	PhaseIn.Dismissal = Frame.Dismissal;
	PhaseIn.Boundary = Frame.Boundary;
	PhaseIn.bCaught = false;
	PhaseIn.bRunning = Result.Running.Attempted > 0;
	PhaseIn.bWicketFallen = bWicketThis && bSettledDead;
	// The full replay is directed as the live pass was: the same phases on the replayed ball clock.
	if (bFullPass)
	{
		PhaseIn.bRunUp = T < 0.f;
		PhaseIn.bBallLive = T >= 0.f;
		PhaseIn.bDead = false;
		PhaseIn.bReplaying = false;
		PhaseIn.bWicketFallen = false;
	}
	const EBroadcastPhase BPhase = ResolvePhase(PhaseIn, BroadcastTuning);

	EBroadcastShot Desired = SelectLiveShot(BPhase, Frame);
	bool bForce = false;
	// A ball that hits the stumps (or the pad in front of them) is replayed on the delivery lens through the
	// wicket, as TV does: that lens frames the striker and the stumps; a close-up cut at contact would hide them.
	if (bFullPass && (Result.Dismissal == EDismissal::Bowled || Result.Dismissal == EDismissal::LBW || Result.Dismissal == EDismissal::HitWicket))
		Desired = EBroadcastShot::StandardDelivery;
	if (bReplay && !bFullPass)
	{
		// Replay direction: the package's angle, or the legacy two-angle fallback.
		const int32 RA = ReplayAngle();
		Desired = ActivePackage.Angles.IsValidIndex(RA) ? ActivePackage.Angles[RA].Shot
			: (RA == 1 ? EBroadcastShot::ReplaySlowMo : EBroadcastShot::ReplayBeauty);
		bForce = true; // every replay angle is an intentional cut
	}
	if (BPhase == EBroadcastPhase::Wicket || Desired == EBroadcastShot::Boundary) bForce = true;

	// Flows that own the screen keep their exact established geometry (director only snaps to them).
	EBroadcastShot FlowShot = Desired;
	FVector FlowLoc = FVector::ZeroVector, FlowLook = FVector::ZeroVector;
	float FlowFov = 0.f;
	bool bFlow = false;
	if (bAwaitingThirdUmpire)
	{
		// Square-on to the popping crease at the broken wicket, then from down the pitch back at it (behind the
		// stumps, the umpire and the fielder taking the ball stand in the way).
		FlowShot = ThirdUmpireAngle() == 0 ? EBroadcastShot::ReplayBeauty : EBroadcastShot::ReplaySlowMo;
		const bool bNear = Result.bBrokenAtStrikerEnd;
		const float StumpsX = bNear ? 0.f : PitchLength, Toward = bNear ? 1.f : -1.f, CreaseX = StumpsX + Toward * PoppingCrease;
		FlowLoc = ToWorld(ThirdUmpireAngle() == 0 ? FVector(CreaseX, 22.f * Off, 1.2f) : FVector(CreaseX + Toward * 14.f, 0.f, 2.5f));
		FlowLook = ToWorld(FVector(ThirdUmpireAngle() == 0 ? CreaseX : CreaseX - Toward * 0.5f, 0.f, 0.5f));
		FlowFov = ThirdUmpireAngle() == 0 ? 14.f : 20.f;
		bFlow = true;
		bForce = true;
	}
	if (bReview)
	{
		// Ball tracking: from above the bowler's stumps while the path comes down the pitch, then round to the
		// off side of the striker's stumps, to see from the pad on to them.
		FlowShot = EBroadcastShot::Review;
		const bool bClose = ReviewProgress() > 0.4f;
		FlowLoc = ToWorld(bClose ? FVector(4.5f, 2.5f * Off, 1.6f) : FVector(PitchLength + 5.f, 0.f, 3.f));
		FlowLook = ToWorld(bClose ? FVector(0.6f, 0.f, 0.35f) : FVector(2.f, 0.f, 0.5f));
		FlowFov = bClose ? 32.f : 24.f;
		bFlow = true;
		bForce = true;
	}
	if (bScorecard)
	{
		// High in the square-leg stand, across the square to the far stands.
		FlowShot = EBroadcastShot::Scorecard;
		FlowLoc = ToWorld(FVector(0.5f * PitchLength, -80.f, 26.f));
		FlowLook = ToWorld(FVector(0.5f * PitchLength, 30.f, 4.f));
		FlowFov = 70.f;
		bFlow = true;
		bForce = true;
	}
	if (bFlow) Desired = FlowShot;

	bool bDirectorCut = false;
	const EBroadcastShot ActiveShot = BroadcastDirector.Update(Dt, Desired, bForce, BroadcastTuning, bDirectorCut, bReplay ? ReplayAngle() : INDEX_NONE);
	LastSolvedShot = ActiveShot;

	FLiveCameraSolution Sol;
	if (bFlow)
	{
		Sol.Shot = ActiveShot;
		Sol.Location = FlowLoc;
		Sol.LookAt = FlowLook;
		Sol.FOV = FlowFov;
		Sol.Transition = ECameraTransition::Cut;
	}
	else
	{
		Sol = SolveShotGeometry(ActiveShot, Frame, BroadcastTuning);
	}
	FVector WantLoc = Sol.Location;
	FVector LookAt = Sol.LookAt;
	float WantFov = Sol.FOV;
	float DeliveryU = -1.f; // delivery clock, shown in the camera debug overlay for calibration
	if (!bFlow && ActiveShot == EBroadcastShot::StandardDelivery && BPhase != EBroadcastPhase::BallDead)
	{
		// The delivery shot is a lock-off with an operator's zoom: the position never chases; the lens
		// and tilt ride the Cricket 24 curve on the delivery clock (run-up to release to the batter),
		// and the weighted bowler/release/ball/batter target only nudges the pan.
		const bool bPreRelease = DPhase == EDeliveryPhase::Waiting || DPhase == EDeliveryPhase::RunUp;
		const float RunUpU = DPhase == EDeliveryPhase::RunUp ? PhaseTime / FMath::Max(PhaseTime - TimeToRelease(T, false), 0.05f)
			: bReplay ? 1.f + T / FMath::Max(RunUpSeconds, 0.4f) : 0.f;
		const float GameU = DeliveryClock(RunUpU, bPreRelease ? -1.f : T, FMath::Max(Result.ContactTime, 0.05f));
		// The lens rides the camera's own clock: the game's clock jumps when a human lets go off the ideal.
		if (bCutCamera || bDirectorCut || DPhase == EDeliveryPhase::Waiting) DeliveryCamClock.Reset(GameU);
		const float U = DeliveryCamClock.Update(GameU, Dt);
		const FVector Pan = WeightedDeliveryTarget(Frame, BroadcastTuning.Delivery, FMath::Clamp(U - 1.f, 0.f, 1.f));
		const FLiveCameraSolution D = SolveDeliveryShot(BroadcastTuning.Delivery, U, 16.f / 9.f, Arm, Pan);
		DeliveryU = U;
		WantLoc = D.Location;
		LookAt = D.LookAt;
		WantFov = D.FOV;
	}
	if (!bFlow && ActiveShot == EBroadcastShot::ReplayBeauty && T > Result.ContactTime + 0.3f)
	{
		// The beauty angle opens up to the flight once the stroke has read.
		LookAt = BallPos;
		WantFov = BroadcastTuning.ReplayWideFOV;
	}
	if (bDevCamFielder && bLive && Fielders.IsValidIndex(Result.Fielding.Fielder) && !Ctx.Field[Result.Fielding.Fielder].bBowler)
	{
		// On the body's pelvis rather than the actor: a captured dive carries the body metres from its actor.
		const USkeletalMeshComponent* Body = BodyOf(Fielders[Result.Fielding.Fielder]);
		const FVector Pelvis = Body ? Body->GetBoneLocation(TEXT("pelvis")) : FVector::ZeroVector;
		const FVector At = Body && !Pelvis.IsZero() ? FVector(Pelvis.X, Pelvis.Y, Fielders[Result.Fielding.Fielder]->GetActorLocation().Z) : Fielders[Result.Fielding.Fielder]->GetActorLocation();
		const FVector In = FVector(FVector2D(PitchCentre() * 100.f - At), 0.f).GetSafeNormal();
		WantLoc = At + In * 700.f + FVector(0.f, 0.f, 80.f);
		LookAt = At;
		WantFov = 30.f;
		bCutCamera = true;
	}
	else if (AStaticMeshActor* Who = DevLookFigure(); Who && BodyOf(Who))
	{
		// The mesh faces its +Y axis (the body is turned -90 degrees in its marker).
		const USkeletalMeshComponent* Body = BodyOf(Who);
		const FVector At = Body->GetBoneLocation(*DevLookBone);
		const FVector Ahead = FVector(FVector2D(Body->GetComponentTransform().TransformVectorNoScale(FVector(0.f, 1.f, 0.f))), 0.f).GetSafeNormal();
		WantLoc = At + Ahead.RotateAngleAxis(DevLookYaw, FVector::UpVector) * DevLookDist * 100.f;
		LookAt = At;
		WantFov = DevLookFov;
		bCutCamera = true;
	}
	else if (DevCam.Num() == 7)
	{
		WantLoc = ToWorld(FVector(DevCam[0], DevCam[1], DevCam[2]));
		LookAt = ToWorld(FVector(DevCam[3], DevCam[4], DevCam[5]));
		WantFov = DevCam[6];
		bCutCamera = true;
	}
	// Tuned lenses are 16:9: any other screen keeps the same vertical view (a phone sees wider, not closer).
	WantFov = AspectFOV(WantFov, ViewAspect);
	// Cut or blend (§18): a cut snaps (with a one-off occlusion pull-in); a blend rides the smoother.
	// The director already decided cut-vs-blend from broadcast grammar; bCutCamera forces it.
	const bool bCut = bCutCamera || bDirectorCut;
	UCameraComponent* Cam = Camera->GetCameraComponent();
	if (bCut)
	{
		RefreshCutFigures();
		WantLoc = ApplyOcclusion(GetWorld(), LookAt, WantLoc);
		const FQuat WantRot = (LookAt - WantLoc).GetSafeNormal().Rotation().Quaternion();
		BroadcastSmoother.Snap(WantLoc, WantRot, WantFov);
		Camera->SetActorLocationAndRotation(WantLoc, WantRot.Rotator());
		Cam->SetFieldOfView(WantFov);
	}
	else
	{
		// CRICKET26.mp4 premium tuning: the delivery is a lock-off (tight head, zoom glued to its
		// curve, 4 cm dead zone so the long lens never shimmers); every follow is one smooth head
		// move (6/s rotation, 5/s zoom so the lens opens as fast as the ball leaves the bat, tuned
		// dead zone so micro-jitter dies but the ball never sticks). Positions are static stands:
		// only the head and the lens move, exactly like the reference truck.
		const bool bDeliveryHold = ActiveShot == EBroadcastShot::StandardDelivery;
		const float PosLambda = bDeliveryHold ? 1.f / FMath::Max(BroadcastTuning.Delivery.TrackingLag, 0.03f) : BroadcastTuning.Delivery.PositionDamping;
		const float RotLambda = bDeliveryHold ? BroadcastTuning.Delivery.RotationDamping : 6.f;
		const float FovLambda = bDeliveryHold ? 3.f * RotLambda : 5.f;
		const float DeadCm = bDeliveryHold ? 4.f : BroadcastTuning.LookDeadZoneCm;
		BroadcastSmoother.Update(WantLoc, LookAt, WantFov, Dt, PosLambda, RotLambda,
			BroadcastTuning.MaxAngularVelocity, DeadCm, FovLambda);
		Camera->SetActorLocationAndRotation(BroadcastSmoother.Location, BroadcastSmoother.Rotation.Rotator());
		Cam->SetFieldOfView(BroadcastSmoother.FOV);
	}
	// CRICKET26.mp4 reference: no contact punch. The delivery holds its breath through contact
	// (0.3 s ContactHold) and the lens never pops: a 2% FOV punch is a 10% jump on the 5 deg lens
	// and reads as cheap game-feel, never TV. Impact is carried by sound + ball, not the head.
	// (ContactImpulseTime/Power retained on the header for save compat; never driven.)
	ContactImpulseTime = 0.f;
	bCutCamera = false;
	LastShot = int32(ActiveShot);
	LastCameraDebug = FString::Printf(TEXT("%s %s %.2fs fov %.1f"), PhaseName(BPhase), ShotName(ActiveShot), BroadcastDirector.ShotTime, Cam->FieldOfView);
	if (DeliveryU >= 0.f) LastCameraDebug += FString::Printf(TEXT(" U %.2f"), DeliveryU);
	UpdateFigures(Dt);
	UpdatePoses(T, bLive, Post, Off, Arm);
	StrikerPosed = Striker->GetActorTransform();
	bStrikerCut = T < StrikerPosedT || T > StrikerPosedT + 0.25f;
	StrikerPosedT = T;
	UpdateCrowd();
	UpdateBigScreens();
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

	// Rolling replay capture (§22): every presented live frame, keyed by ball time. Bounded and
	// fixed-rate (see FCricketReplayBuffer): no per-frame allocation once full. Never recorded during
	// replays, referrals or reviews: the buffer must hold the one true live pass.
	const bool bRecordReplay = bLive && !bReplay && !bAwaitingReview && !bAwaitingThirdUmpire && !bReviewThis && !InReel();
	if (bRecordReplay) RecordReplayFrame(T, Ball->GetActorLocation(), BallVel);
}

// ================= IPL tournament integration =================
// The match engine stays untouched: the season stages a fixture (pending context or ?IPLFixture=),
// the game mode swaps in the two franchises' auction-built XIs, gates the next ball on the user's
// batter/bowler picks, and commits the finished match back to the season exactly once.

void ASuperOverGameMode::SetupIPLMatch()
{
	UIPLPendingMatch* Pending = UIPLPendingMatch::Get();
	if (Pending && Pending->bActive && Pending->bQuick)
	{
		// A quick match between two real teams; staging that cannot be played falls back to the placeholder squads.
		SetupQuickMatch(*Pending);
		Pending->Clear(); // consumed once: never leaks into a later match
		return;
	}
	int32 FixtureId = INDEX_NONE;
	FIPLPlayingXI HomeXI, AwayXI;
	bool bFromPending = false;
	if (Pending && Pending->bActive)
	{
		FixtureId = Pending->FixtureId;
		HomeXI = Pending->HomeXI;
		AwayXI = Pending->AwayXI;
		bFromPending = true;
	}
	else
	{
		FixtureId = UGameplayStatics::GetIntOption(OptionsString, TEXT("IPLFixture"), INDEX_NONE);
	}
	if (FixtureId == INDEX_NONE) return; // standalone: everything below is skipped
	UIPLSeasonSave* Save = UIPLSeasonSave::Get();
	if (!Save || !Save->bHasSeason)
	{
		UE_LOG(LogCRICKET26, Error, TEXT("IPL fixture %d staged without a season; playing standalone."), FixtureId);
		return;
	}
	FIPLSeason& Season = Save->Season;
	const int32 Fi = IPLSeason::FixtureIndex(Season, FixtureId);
	if (Fi == INDEX_NONE)
	{
		UE_LOG(LogCRICKET26, Error, TEXT("IPL fixture %d is not in the season; playing standalone."), FixtureId);
		return;
	}
	FIPLFixture& Fx = Season.Fixtures[Fi];
	if (Fx.Status != EIPLFixtureStatus::Upcoming)
	{
		UE_LOG(LogCRICKET26, Error, TEXT("IPL fixture %d is already %s; playing standalone."),
			FixtureId, Fx.Status == EIPLFixtureStatus::Completed ? TEXT("completed") : TEXT("unknown"));
		return;
	}
	const TArray<FAuctionPlayer>& Players = AuctionData::Players();
	const TArray<FAuctionFranchise>& Franchises = AuctionData::Franchises();
	if (!bFromPending)
	{
		// Re-travel fallback (no pending context): the season's remembered XIs.
		if (Season.LastXI.IsValidIndex(Fx.Home)) HomeXI = Season.LastXI[Fx.Home];
		if (Season.LastXI.IsValidIndex(Fx.Away)) AwayXI = Season.LastXI[Fx.Away];
	}
	FString Why;
	if (!IPLMatchAdapter::ValidateXI(Season, Fx.Home, HomeXI, &Why))
	{
		UE_LOG(LogCRICKET26, Error, TEXT("IPL home XI invalid (%s); using the default XI."), *Why);
		HomeXI = IPLSeason::MakeDefaultXI(Season.Squads[Fx.Home].Players, Players);
	}
	if (!IPLMatchAdapter::ValidateXI(Season, Fx.Away, AwayXI, &Why))
	{
		UE_LOG(LogCRICKET26, Error, TEXT("IPL away XI invalid (%s); using the default XI."), *Why);
		AwayXI = IPLSeason::MakeDefaultXI(Season.Squads[Fx.Away].Players, Players);
	}
	if (Fx.BatFirst == INDEX_NONE)
	{
		// The toss, seeded by the fixture so it can never change under a fixture.
		FRandomStream Toss(FixtureId * 9176 + 3);
		Fx.BatFirst = Toss.RandRange(0, 1);
		Save->Persist();
	}
	Teams = IPLMatchAdapter::BuildMatchTeams(Season, Fx, HomeXI, AwayXI, Players, Franchises);
	SelectedMatchOvers = IPLSeason::MatchOvers;
	Match.Rules.MaxLegalBalls = IPLSeason::MatchBalls;
	Match.Rules.MaxWickets = 10;
	IPLFixtureId = FixtureId;
	IPLUserTeam = Season.UserTeam;
	IPLHomeTeam = Fx.Home;
	IPLAwayTeam = Fx.Away;
	IPLHomeXI = HomeXI;
	IPLAwayXI = AwayXI;
	IPLBatFirst = Fx.BatFirst;
	HumanTeam = (Fx.Home == Season.UserTeam) ? 0 : 1;
	IPLBowlerBalls[0].SetNumZeroed(HomeXI.BattingOrder.Num());
	IPLBowlerBalls[1].SetNumZeroed(AwayXI.BattingOrder.Num());
	IPLBowlerSlot[0] = IPLBowlerSlot[1] = INDEX_NONE;
	IPLLastBowlerSlot[0] = IPLLastBowlerSlot[1] = INDEX_NONE;
	bAwaitingBatter = bAwaitingBowler = bIPLCommitted = false;
	if (bFromPending) Pending->Clear(); // consumed once: never leaks into a later match
	const TArray<FAuctionFranchise>& Fr = Franchises;
	UE_LOG(LogCRICKET26, Display, TEXT("IPL fixture %d: %s vs %s (%s bat first), user controls %s"),
		FixtureId, Fr.IsValidIndex(Fx.Home) ? *Fr[Fx.Home].Code : TEXT("?"),
		Fr.IsValidIndex(Fx.Away) ? *Fr[Fx.Away].Code : TEXT("?"),
		Fx.BatFirst == 0 ? (Fr.IsValidIndex(Fx.Home) ? *Fr[Fx.Home].Code : TEXT("home")) : (Fr.IsValidIndex(Fx.Away) ? *Fr[Fx.Away].Code : TEXT("away")),
		Fr.IsValidIndex(Season.UserTeam) ? *Fr[Season.UserTeam].Code : TEXT("?"));
}

bool ASuperOverGameMode::SetupQuickMatch(const UIPLPendingMatch& Pending)
{
	const ECompetition Comp = Pending.QuickCompetition < uint8(ECompetition::Count) ? ECompetition(Pending.QuickCompetition) : ECompetition::IPL;
	const TArray<FRealTeam>& All = RealTeams::Teams(Comp);
	const int32 Home = Pending.QuickHome, Away = Pending.QuickAway;
	if (!All.IsValidIndex(Home) || !All.IsValidIndex(Away) || Home == Away)
	{
		UE_LOG(LogCRICKET26, Error, TEXT("Quick match staged with teams %d and %d; playing the placeholder squads."), Home, Away);
		return false;
	}
	FIPLPlayingXI HomeXI = Pending.HomeXI, AwayXI = Pending.AwayXI;
	FString Why;
	if (!RealTeams::ValidXI(All[Home], HomeXI, &Why))
	{
		UE_LOG(LogCRICKET26, Error, TEXT("Quick match: %s XI invalid (%s); using their real XI."), *All[Home].Code, *Why);
		HomeXI = RealTeams::DefaultXI(Comp, Home);
	}
	if (!RealTeams::ValidXI(All[Away], AwayXI, &Why))
	{
		UE_LOG(LogCRICKET26, Error, TEXT("Quick match: %s XI invalid (%s); using their real XI."), *All[Away].Code, *Why);
		AwayXI = RealTeams::DefaultXI(Comp, Away);
	}
	const int32 Overs = Pending.QuickOvers == 3 || Pending.QuickOvers == 5 || Pending.QuickOvers == 10 || Pending.QuickOvers == 20 ? Pending.QuickOvers : 1;
	bQuickMatch = true;
	QuickCompetition = Comp;
	QuickTeam[0] = Home;
	QuickTeam[1] = Away;
	const TArray<FAuctionPlayer>& Players = RealTeams::Players(Comp);
	Teams = { RealTeams::MatchSide(All[Home], HomeXI, Players), RealTeams::MatchSide(All[Away], AwayXI, Players) };
	SelectedMatchOvers = Overs;
	Match.Rules.MaxLegalBalls = Overs * 6;
	Match.Rules.MaxWickets = Overs == 1 ? 2 : 10;
	IPLMaxBowlerBalls = RealTeams::MaxBallsPerBowler(Overs);
	IPLHomeXI = HomeXI;
	IPLAwayXI = AwayXI;
	IPLBatFirst = Pending.QuickBatFirst == 1 ? 1 : 0; // the toss, called in the menu
	HumanTeam = 0; // the user's team is always side 0
	IPLBowlerBalls[0].SetNumZeroed(HomeXI.BattingOrder.Num());
	IPLBowlerBalls[1].SetNumZeroed(AwayXI.BattingOrder.Num());
	IPLBowlerSlot[0] = IPLBowlerSlot[1] = INDEX_NONE;
	IPLLastBowlerSlot[0] = IPLLastBowlerSlot[1] = INDEX_NONE;
	bAwaitingBatter = bAwaitingBowler = bIPLCommitted = false;
	UE_LOG(LogCRICKET26, Display, TEXT("Quick match (%s, %d over%s): %s vs %s, %s bat first"), RealTeams::CompetitionName(Comp), Overs,
		Overs == 1 ? TEXT("") : TEXT("s"), *All[Home].Code, *All[Away].Code, IPLBatFirst == 0 ? *All[Home].Code : *All[Away].Code);
	return true;
}

void ASuperOverGameMode::ApplyIPLBowler(int32 Side, int32 Slot)
{
	if (!IsIPLMatch() || !Teams.IsValidIndex(Side)) return;
	if (!IPLXIForSide(Side).BattingOrder.IsValidIndex(Slot)) return;
	if (!Teams[Side].Batters.IsValidIndex(Slot)) return;
	// Both the engine identity (figures follow SetBowlerSlot) and the presentation identity move.
	if (Match.Phase == EMatchPhase::ReadyForDelivery || Match.Phase == EMatchPhase::InningsBreak)
		Match.SetBowlerSlot(Slot);
	Teams[Side].Bowler = Teams[Side].Batters[Slot];
	IPLBowlerSlot[Side] = Slot;
}

void ASuperOverGameMode::IPLNewInningsSetup()
{
	if (!IsIPLMatch() || !Match.Innings.IsValidIndex(Match.CurrentInnings)) return;
	IPLBowlerBalls[0].SetNumZeroed(IPLHomeXI.BattingOrder.Num());
	IPLBowlerBalls[1].SetNumZeroed(IPLAwayXI.BattingOrder.Num());
	IPLBowlerSlot[0] = IPLBowlerSlot[1] = INDEX_NONE;
	IPLLastBowlerSlot[0] = IPLLastBowlerSlot[1] = INDEX_NONE;
	bAwaitingBatter = false;
	IPLAwaitingSlot = INDEX_NONE;
	const int32 BowlSide = Match.BowlingTeam();
	const FIPLPlayingXI& XI = IPLXIForSide(BowlSide);
	const TArray<FAuctionPlayer>& Players = XIPlayers();
	if (BowlSide == HumanTeam && !bAutoPlay)
	{
		// The user picks the opening bowler too: same panel as every over change.
		IPLAwaitingCandidates = IPLMatchAdapter::EligibleBowlers(XI, IPLBowlerBalls[BowlSide], INDEX_NONE, Players, IPLMaxBowlerBalls);
		if (IPLAwaitingCandidates.Num() > 0) bAwaitingBowler = true;
		else ApplyIPLBowler(BowlSide, 0);
	}
	else
	{
		const int32 Slot = IPLMatchAdapter::ChooseAIBowler(XI, IPLBowlerBalls[BowlSide], INDEX_NONE, Players, IPLMaxBowlerBalls);
		ApplyIPLBowler(BowlSide, Slot != INDEX_NONE ? Slot : 0);
	}
}

void ASuperOverGameMode::AfterIPLDelivery(const FDeliveryOutcome& Outcome, const TArray<ECricketEvent>& Events, int32 PreNext)
{
	if (!IsIPLMatch()) return;
	const int32 BatSide = Match.BattingTeam();
	const int32 BowlSide = Match.BowlingTeam();
	IPLBowlerBalls[BowlSide] = IPLMatchAdapter::BowlerBallsFromMatch(Match);
	const bool bLegal = !Outcome.bWide && !Outcome.bNoBall;

	// 1. The match is decided: commit once, or play the Super Over decider on level scores.
	if (Match.Phase == EMatchPhase::MatchComplete)
	{
		if (Match.bTied && !bIPLCommitted)
		{
			if (Match.StartNextSuperOver()) IPLNewInningsSetup();
		}
		else if (!Match.bTied && !bIPLCommitted)
		{
			CommitIPLResult();
		}
		return;
	}

	// 2. A wicket with the innings continuing: who walks out next.
	if (Events.Contains(ECricketEvent::Wicket) && Match.Phase == EMatchPhase::ReadyForDelivery)
	{
		// The engine consumed batting-order slot PreNext for the incoming batter.
		if (Match.Cur().Batters.IsValidIndex(PreNext) && BatSide == HumanTeam && !bAutoPlay)
		{
			IPLAwaitingSlot = PreNext;
			IPLAwaitingCandidates.Reset();
			IPLAwaitingCandidates.Add(PreNext); // the next man in stays pickable
			IPLAwaitingCandidates.Append(IPLMatchAdapter::EligibleBatters(Match));
			bAwaitingBatter = true;
		}
		// An AI side's next man is its lineup order: the engine already sent him out.
	}

	// 3. An over ends with the innings continuing: who bowls next.
	if (bLegal && Match.Phase == EMatchPhase::ReadyForDelivery && Match.Cur().LegalBalls > 0 && Match.Cur().LegalBalls % 6 == 0)
	{
		IPLLastBowlerSlot[BowlSide] = IPLBowlerSlot[BowlSide];
		const FIPLPlayingXI& XI = IPLXIForSide(BowlSide);
		const TArray<FAuctionPlayer>& Players = XIPlayers();
		if (BowlSide == HumanTeam && !bAutoPlay)
		{
			IPLAwaitingCandidates = IPLMatchAdapter::EligibleBowlers(XI, IPLBowlerBalls[BowlSide], IPLLastBowlerSlot[BowlSide], Players, IPLMaxBowlerBalls);
			if (IPLAwaitingCandidates.Num() > 0)
				bAwaitingBowler = true;
			else
				UE_LOG(LogCRICKET26, Error, TEXT("IPL: no eligible bowler for the user side; keeping the current one."));
		}
		else
		{
			const int32 Slot = IPLMatchAdapter::ChooseAIBowler(XI, IPLBowlerBalls[BowlSide], IPLLastBowlerSlot[BowlSide], Players, IPLMaxBowlerBalls);
			if (Slot != INDEX_NONE) ApplyIPLBowler(BowlSide, Slot);
		}
	}
}

bool ASuperOverGameMode::ChooseNextBatter(int32 Candidate)
{
	if (!IsIPLMatch() || !bAwaitingBatter || !IPLAwaitingCandidates.IsValidIndex(Candidate)) return false;
	if (Match.Phase != EMatchPhase::ReadyForDelivery) return false;
	const int32 Slot = IPLAwaitingCandidates[Candidate];
	const int32 BatSide = Match.BattingTeam();
	if (Slot != IPLAwaitingSlot && Teams.IsValidIndex(BatSide)
		&& Teams[BatSide].Batters.IsValidIndex(Slot) && Teams[BatSide].Batters.IsValidIndex(IPLAwaitingSlot))
	{
		// Both cards are empty (no balls faced, not dismissed): only the identities reorder, so the
		// scorecard, HUD, commentary and gameplay all agree on who walked out.
		Teams[BatSide].Batters.Swap(IPLAwaitingSlot, Slot);
		UE_LOG(LogCRICKET26, Display, TEXT("IPL: %s walks out next."), *Teams[BatSide].Batters[IPLAwaitingSlot].Name);
	}
	bAwaitingBatter = false;
	IPLAwaitingSlot = INDEX_NONE;
	IPLAwaitingCandidates.Reset();
	if (DPhase == EDeliveryPhase::Waiting) PlaceForDelivery();
	return true;
}

bool ASuperOverGameMode::ChooseNextBowler(int32 Candidate)
{
	if (!IsIPLMatch() || !bAwaitingBowler || !IPLAwaitingCandidates.IsValidIndex(Candidate)) return false;
	if (Match.Phase != EMatchPhase::ReadyForDelivery) return false;
	const int32 Slot = IPLAwaitingCandidates[Candidate];
	const int32 BowlSide = Match.BowlingTeam();
	// State-safe: the pick is re-validated against the live limits (quota, consecutive over).
	const TArray<int32> Legal = IPLMatchAdapter::EligibleBowlers(IPLXIForSide(BowlSide),
		IPLBowlerBalls[BowlSide], IPLLastBowlerSlot[BowlSide], XIPlayers(), IPLMaxBowlerBalls);
	if (!Legal.Contains(Slot)) return false;
	ApplyIPLBowler(BowlSide, Slot);
	bAwaitingBowler = false;
	IPLAwaitingCandidates.Reset();
	UE_LOG(LogCRICKET26, Display, TEXT("IPL: %s takes the next over."), *Teams[BowlSide].Bowler.Name);
	if (DPhase == EDeliveryPhase::Waiting) PlaceForDelivery();
	return true;
}

FString ASuperOverGameMode::IPLPickTitle() const
{
	if (bAwaitingBatter) return TEXT("SELECT NEXT BATTER");
	if (bAwaitingBowler) return TEXT("SELECT NEXT BOWLER");
	return FString();
}

TArray<FString> ASuperOverGameMode::IPLPickNames() const
{
	TArray<FString> Out;
	if (!IsIPLMatch() || !IsAwaitingPick() || !Teams.IsValidIndex(bAwaitingBatter ? Match.BattingTeam() : Match.BowlingTeam()))
		return Out;
	const int32 Side = bAwaitingBatter ? Match.BattingTeam() : Match.BowlingTeam();
	for (int32 Slot : IPLAwaitingCandidates)
	{
		if (!Teams[Side].Batters.IsValidIndex(Slot)) continue;
		if (bAwaitingBowler)
		{
			const int32 Bowled = IPLBowlerBalls[Side].IsValidIndex(Slot) ? IPLBowlerBalls[Side][Slot] : 0;
			Out.Add(FString::Printf(TEXT("%s  %s ov"), *Teams[Side].Batters[Slot].Name, *IPLSeason::OversText(Bowled)));
		}
		else
		{
			Out.Add(Teams[Side].Batters[Slot].Name);
		}
	}
	return Out;
}

void ASuperOverGameMode::CommitIPLResult()
{
	if (!IsIPLMatch() || bIPLCommitted) return;
	if (bQuickMatch)
	{
		bIPLCommitted = true; // a quick match is decided, with no season to tell
		return;
	}
	UIPLSeasonSave* Save = UIPLSeasonSave::Get();
	if (!Save || !Save->bHasSeason) return;
	const int32 Fi = IPLSeason::FixtureIndex(Save->Season, IPLFixtureId);
	if (Fi == INDEX_NONE) return;
	FIPLFixture& Fx = Save->Season.Fixtures[Fi];
	if (Fx.Status == EIPLFixtureStatus::Completed) { bIPLCommitted = true; return; } // already there
	FIPLResult R = IPLMatchAdapter::ResultFromMatch(Match, Fx);
	R.BatFirst = Fx.BatFirst;
	if (R.Winner == INDEX_NONE)
	{
		UE_LOG(LogCRICKET26, Error, TEXT("IPL fixture %d ended without a result; staying on the scorecard."), IPLFixtureId);
		return;
	}
	if (IPLSeason::CommitResult(Save->Season, R))
	{
		Save->Persist();
		bIPLCommitted = true;
		UE_LOG(LogCRICKET26, Display, TEXT("IPL fixture %d committed: %s"), IPLFixtureId, *R.Margin);
	}
	else
	{
		UE_LOG(LogCRICKET26, Error, TEXT("IPL fixture %d result refused by the season; staying on the scorecard."), IPLFixtureId);
	}
}

void ASuperOverGameMode::ReturnToIPLHub()
{
	UFrontendStatics::OpenFrontend(this, bQuickMatch ? EFrontendTab::Play : EFrontendTab::IPLSeason);
}

void ASuperOverGameMode::RestartIPLFixture()
{
	// A quick match replays with the same teams, XIs and toss.
	if (bQuickMatch)
	{
		UIPLPendingMatch::Get()->SetQuick(uint8(QuickCompetition), QuickTeam[0], QuickTeam[1], IPLHomeXI, IPLAwayXI, SelectedMatchOvers, IPLBatFirst);
		UFrontendStatics::OpenMatch(this, SelectedMatchOvers);
		return;
	}
	// Only before the result commits: after that the hub owns the fixture and a replay would
	// double-play it. The staged XIs are this match's own, so the replay is exact.
	if (!IsIPLMatch() || bIPLCommitted) { ReturnToIPLHub(); return; }
	UIPLPendingMatch::Get()->Set(IPLFixtureId, IPLHomeXI, IPLAwayXI);
	UFrontendStatics::OpenIPLMatch(this, IPLFixtureId);
}
