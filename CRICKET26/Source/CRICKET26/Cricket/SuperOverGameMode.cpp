#include "SuperOverGameMode.h"
#include "SuperOverHUD.h"
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
#include "Components/StaticMeshComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
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

	/** A bone's component-space location (cm) in a clip at a time, composed up its parents. */
	FVector ClipBoneAt(const UAnimSequence* Seq, FName Bone, float Time)
	{
		const FReferenceSkeleton& Ref = Seq->GetSkeleton()->GetReferenceSkeleton();
		FTransform T = FTransform::Identity;
		for (int32 B = Ref.FindBoneIndex(Bone); B != INDEX_NONE; B = Ref.GetParentIndex(B))
		{
			FTransform Local;
			Seq->GetBoneTransform(Local, FSkeletonPoseBoneIndex(B), FAnimExtractContext(double(Time)), false);
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
	Home.Sponsor = TEXT("SAFFRON BANK");
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
	Away.Sponsor = TEXT("ZEPHYRA");
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
	FParse::Value(FCommandLine::Get(), TEXT("CricketQuitAfter="), QuitAfter);
	int32 Level = int32(Difficulty);
	FParse::Value(FCommandLine::Get(), TEXT("CricketDifficulty="), Level);
	Difficulty = CricketAI::EDifficulty(FMath::Clamp(Level, 0, 3));
	FParse::Value(FCommandLine::Get(), TEXT("CricketQuality="), Quality);
	Quality = FMath::Clamp(Quality, 0, 3);
	ApplyQuality();
	bTouchUI = PLATFORM_IOS || PLATFORM_ANDROID || bTouchScript || FParse::Param(FCommandLine::Get(), TEXT("CricketTouch"));
	bRecordAudio = FParse::Param(FCommandLine::Get(), TEXT("CricketRecordAudio")) && ShotBall > 0;
	VenueIndex = bAutoPlay ? 0 : FMath::RandRange(0, CricketStadium::NumVenues - 1);
	FParse::Value(FCommandLine::Get(), TEXT("CricketVenue="), VenueIndex);
	VenueIndex = FMath::Clamp(VenueIndex, 0, CricketStadium::NumVenues - 1);
	UE_LOG(LogCRICKET26, Display, TEXT("Venue: %s"), CricketStadium::Venue(VenueIndex).Name);
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
	if (ElbowChecks > 0) UE_LOG(LogCRICKET26, Display, TEXT("Pose: striker arm inside torso %d of %d samples"), ElbowInside, ElbowChecks);
	if (RaisedChecks > 0) UE_LOG(LogCRICKET26, Display, TEXT("Pose: striker raised-arm elbow flared out %d of %d samples"), ElbowFlared, RaisedChecks);
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
					continue;
				}
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
				}
				for (const TCHAR* Param : { TEXT("Paint Tint"), TEXT("LogoTint"), TEXT("diffuse_color_1"), TEXT("diffuse_color_2"), TEXT("B_diffuse_color_1") })
					Cloth->SetVectorParameterValue(Param, Shade);
				// The shirt's print, switched on by outfit_ue.py: white name and number, a gold sponsor.
				if (Team && Source.Contains(TEXT("Tshirt")))
				{
					Cloth->SetTextureParameterValue(TEXT("PrintGraphicMap"), ShirtPrint(Name, Number, Team->Sponsor));
					Cloth->SetVectorParameterValue(TEXT("PrintGraphicColorA"), FLinearColor(0.8f, 0.8f, 0.8f));
					Cloth->SetVectorParameterValue(TEXT("PrintGraphicColorB"), FLinearColor(1.f, 0.55f, 0.05f));
					Cloth->SetScalarParameterValue(TEXT("PrintGraphicStrength"), 1.f);
				}
			}
		}
	}
}

UTexture* ASuperOverGameMode::ShirtPrint(const FString& Name, int32 Number, const FString& Sponsor)
{
	const FString Key = FString::Printf(TEXT("%s|%d|%s"), *Name, Number, *Sponsor);
	if (const TObjectPtr<UTextureRenderTarget2D>* Drawn = ShirtPrints.Find(Key)) return *Drawn;
	// Mipmapped, or the letters shimmer on distant fielders; those, unnamed and never close, get a smaller one.
	const int32 Res = Name.IsEmpty() ? 512 : 1024;
	UTextureRenderTarget2D* Target = UKismetRenderingLibrary::CreateRenderTarget2D(this, Res, Res, RTF_RGBA8, FLinearColor::Transparent, true);
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Target, Canvas, Size, Context);
	UFont* Font = GEngine->GetLargeFont();
	// Laid out on the shirt's first UVs, measured on the outfit exported to Blender: the back panel is centred on
	// u 0.218 and the front on u 0.661, and v climbs about 0.64 a metre up the body to the neckline near 0.5. Text is
	// Height tall (in v) from Top down, but no wider than Width (in u). The letters write their coverage into the alpha
	// channel, which is how much of the print shows; text drawn the usual way leaves the alpha alone.
	Canvas->Canvas->SetWriteDestinationAlpha(true);
	auto Text = [&](const FString& S, float U, float Top, float Height, float Width, const FLinearColor& Channel)
	{
		float W, H;
		Canvas->TextSize(Font, S, W, H);
		const float Scale = FMath::Min(Height * Size.Y / H, Width * Size.X / W);
		FCanvasTextItem Item(FVector2D(U * Size.X - W * Scale / 2.f, (1.f - Top) * Size.Y), FText::FromString(S), Font, Channel);
		Item.Scale = FVector2D(Scale, Scale);
		Canvas->DrawItem(Item);
	};
	if (!Name.IsEmpty()) Text(Name.ToUpper(), 0.218f, 0.455f, 0.035f, 0.2f, FLinearColor::Red);
	Text(FString::FromInt(Number), 0.218f, 0.415f, 0.14f, 0.18f, FLinearColor::Red);
	Text(Sponsor, 0.661f, 0.42f, 0.05f, 0.17f, FLinearColor::Green);
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	// The canvas draws only the top mip; this builds the rest from it (without it they stay clear, and the print
	// fades out as the shirt gets smaller on screen).
	Target->UpdateResourceImmediate(false);
	ShirtPrints.Add(Key, Target);
	return Target;
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
		// A player built in Epic's parametric outfit (Scripts/metahuman/outfit_ue.py) wears it; the rest wear the Blender
		// kit over the preset garment.
		const bool bOutfit = Garment.ContainsByPredicate([](USkinnedMeshComponent* Part) {
			return Part->GetMaterials().ContainsByPredicate([](UMaterialInterface* M) { return M && M->GetName().Contains(TEXT("WI_OA_")); }); });
		if (bOutfit || Wear(TEXT("Kit")))
		{
			if (!bOutfit) for (USkinnedMeshComponent* Part : Garment) if (Part != Body && Part->GetFName() != TEXT("Face")) Part->SetVisibility(false);
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
		Anim->Sprint = SprintAnim;
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
			for (int32 H = 0; H < 2; ++H)
			{
				const TCHAR* Side = H == 0 ? TEXT("l") : TEXT("r");
				auto Socket = [&](const TCHAR* Name) { return Body->GetSocketLocation(FName(FString::Printf(TEXT("%s_%s"), Name, Side))); };
				// The solve puts the handle's centre line 3.2 cm out from the knuckles' midpoint, level with it.
				const FVector Handle = Plan.Grip + Axis * (H == Plan.TopHand ? -4.5f : 4.5f);
				const FVector Knuckles = 0.5f * (Socket(TEXT("index_01")) + Socket(TEXT("pinky_01"))) - Handle;
				const float Along = Knuckles | Axis;
				const float Miss = FMath::Sqrt(FMath::Square(Along) + FMath::Square((Knuckles - Axis * Along).Size() - 3.2f));
				if (bFingers) HandMiss.Add(Miss);
				if (bFingers && Miss > 6.f) UE_LOG(LogTemp, Display, TEXT("Hand miss %.0f cm: hand %d phase %d time %.2f dt %.3f"), Miss, H, int32(DPhase), PhaseTime, Dt);
				const FVector Elbow = Socket(TEXT("lowerarm")), Wrist = Socket(TEXT("hand"));
				for (const FVector& P : { Elbow, FMath::Lerp(Elbow, Wrist, 0.5f) })
				{
					const FVector V = P - Pelvis - Spine * FMath::Clamp((P - Pelvis) | Spine, 0.f, FVector::Dist(Neck, Pelvis));
					const bool bIn = FMath::Square((V | Wide) / 16.f) + FMath::Square((V | Deep) / 12.f) < 1.f;
					++ElbowChecks;
					ElbowInside += bIn;
					if (bIn) UE_LOG(LogTemp, Display, TEXT("Arm inside torso: hand %d phase %d time %.2f"), H, int32(DPhase), PhaseTime);
				}
				// A raised arm's elbow winged out sideways, beyond the shoulder, rather than forward under the hands.
				const FVector Shoulder = Socket(TEXT("upperarm"));
				if (Wrist.Z > Shoulder.Z)
				{
					const bool bFlared = FMath::Abs((Elbow - Pelvis) | Wide) - FMath::Abs((Shoulder - Pelvis) | Wide) > 12.f;
					++RaisedChecks;
					ElbowFlared += bFlared;
					if (bFlared) UE_LOG(LogTemp, Display, TEXT("Elbow flared: hand %d phase %d time %.2f"), H, int32(DPhase), PhaseTime);
				}
				const FVector Toes = Socket(TEXT("ball"));
				const bool bPlanted = Plan.Lift[H] <= 0.f && bToes;
				if (bPlanted && bWasPlanted[H] && !bStrikerCut)
				{
					FootSlide.Add(FVector2D(Toes - LastBall[H]).Size());
					if (FootSlide.Last() > 1.f) UE_LOG(LogTemp, Display, TEXT("Foot slide %.1f cm: foot %d phase %d time %.2f dt %.3f"), FootSlide.Last(), H, int32(DPhase), PhaseTime, Dt);
				}
				LastBall[H] = Toes;
				bWasPlanted[H] = bPlanted;
			}
		}
		else if (A == Striker) bWasPlanted[0] = bWasPlanted[1] = false;
		if (A->GetActorUpVector().Z > 0.95f && !bHeld)
		{
			// The striker holds a side-on stance, chest to the off side, through the footwork of a stroke
			// until they set off for a run. Everyone turns at a human rate rather than snapping.
			const FVector Look = S.Speed > (A == Striker ? 2.f : 0.8f) ? Vel : A == Striker ? FVector(0.f, Off, 0.f) : BallAt - Pos;
			if (!FVector2D(Look).IsNearlyZero())
				A->SetActorRotation(FMath::RInterpConstantTo(A->GetActorRotation(), FRotator(0.f, Look.Rotation().Yaw, 0.f), Dt, 540.f));
		}
		if (!Anim) continue;
		// ponytail: two cycles time-scaled to speed (template jog ~4 m/s, sprint ~SprintSpeed) and cross-faded, feet
		// not phase-matched through the short fade; a synced blend space if the fade shows.
		Anim->Pose.JogWeight = FMath::Clamp((S.Speed - 0.4f) / 0.6f, 0.f, 1.f);
		Anim->Pose.JogRate = FMath::Clamp(S.Speed / 4.f, 0.6f, 2.f);
		Anim->Pose.SprintWeight = FMath::Clamp((S.Speed - 4.5f) / 1.5f, 0.f, 1.f);
		Anim->Pose.SprintRate = FMath::Clamp(S.Speed / SprintSpeed, 0.6f, 1.5f);
		// Actions are set afresh each frame by UpdatePoses; everyone watches the ball, except the bowler holding it,
		// who watches the batter (following the ball in their own hand turned the head with every swing of the arm).
		Anim->Pose.ClearActions();
		Anim->Pose.LookWeight = 1.f;
		Anim->Pose.LookAt = A == Bowler && bBallInHand ? StrikerHead : BallAt;
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
	return CricketPose::BowlClip(TimeToRelease(T, bLive), -At);
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
	PP->Settings.AutoExposureBias = -Exposure;
	// A soft broadcast glow on sunlit whites (ball, kit, sightscreen) and contact shadow under the players where Lumen is
	// off (below High), which otherwise leaves them floating on the grass.
	PP->Settings.bOverride_BloomIntensity = true;
	PP->Settings.BloomIntensity = V.bNight ? 0.6f : 0.3f; // and the floodlights' glare at night
	PP->Settings.bOverride_AmbientOcclusionIntensity = true;
	PP->Settings.AmbientOcclusionIntensity = 0.5f;

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
			Spawn(CylinderMesh, FVector(X, Y, StumpHeight * 0.5f), FVector(0.036f, 0.036f, StumpHeight), Wood);
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
	Ctx.bFreeHit = Match.bFreeHit;
	Ctx.Rules = Match.Rules;
	Ctx.BouncersBowled = Match.Cur().Bouncers;
	Ctx.Conditions = Conditions();

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
	// ponytail: shirt numbers come from the names' hashes; give players a Number field if a squad needs real ones.
	auto Shirt = [](const FCricketPlayer& P) { return 1 + static_cast<int32>(GetTypeHash(P.Name) % 99); };
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
		Paint(Fielders[I], Ctx.Field[I].bKeeper ? FieldCol * 0.5f : FieldCol, &Teams[Match.BowlingTeam()], FString(), 10 + I);
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
		UE_LOG(LogCRICKET26, Display, TEXT("Frame %d phase %d time %.3f contact %.3f"), Shot, int32(DPhase), PhaseTime, Result.ContactTime);
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
		if (InReel())
		{
			if (PhaseTime >= ReplayDelay + ReplayAngleTime) PlayClip(ReelClip + 1);
		}
		else if (PhaseTime > (bReviewThis ? ReviewFrom() + ReviewTime + 0.5f : 1.8f + (bReplayThis ? ReplayTime : 0.f)))
		{
			DPhase = EDeliveryPhase::Waiting;
			PhaseTime = 0.f;
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
	else if (C.bProgress && InReel())
	{
		PlayClip(INDEX_NONE); // skip the whole reel, back to the scorecard
		C.bProgress = false;
	}
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
	// -CricketLeftHanded: every striker bats left-handed, to inspect the mirrored strokes.
	if (FParse::Param(FCommandLine::Get(), TEXT("CricketLeftHanded")))
		Teams[Match.BattingTeam()].Batters[Match.Cur().Striker].BatHand = ECricketHand::Left;
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
	DrawPitchMarks(Mark);
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
	if (bReplayThis) Highlights.Add({ Result, Ctx, BatInput, ReleaseTiming, Commentary });
	bReviewThis = Result.bPadImpact && Result.Tracking.Projected.Num() > 1 && Result.BallPath.Num() > 1;
	DPhase = EDeliveryPhase::DeadBall;
	PhaseTime = 0.f;
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
		// The replay's side-on angle, from its start.
		ReelClip = Clip;
		bReplayThis = true;
		DPhase = EDeliveryPhase::DeadBall;
		PhaseTime = ReplayDelay;
		return;
	}
	ReelClip = -1;
	bReplayThis = false;
	Highlights.Empty();
	DPhase = EDeliveryPhase::Waiting;
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

	// Striker: the whole body planned through the delivery (CricketBatter), the stroke built backwards from
	// where the simulation put the bat so the sweet spot is on that point at the moment of contact. Setting
	// off for a run, the bat goes to the bottom hand and the body to the jog.
	{
		const EShotType Shot = Result.Shot.Shot;
		const bool bPlayed = bLive && BatInput.IsShot();
		CricketBatter::FInput In;
		In.Off = Off;
		In.Home = FVector(0.9f, -0.35f * Off, 0.f);
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
		// Back into the stance after the ball; from down the track, back in their ground when the simulation says.
		In.Settle = In.Foot == EFootwork::Advance && Result.BrokenTime >= 0.f ? Result.BrokenTime - Result.HomeMargin - 0.8f
			: In.Foot == EFootwork::Advance ? Result.ContactTime + 0.4f : In.Impact + 0.9f;
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
				Hold(P, B, SimAt(Striker) + FVector(0.f, 0.f, ShoulderHeight - MarkerHeight), Body.TopHand, 1.f - RunW, 1.f);
				P.ChestFacing = Striker->GetActorForwardVector();
			}
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

	// Fielders and keeper: the keeper squats until the ball is on its way; anyone the ball comes near gets
	// down to it and puts both hands where it will be a moment later; a thrower windmills the arm over.
	for (int32 I = 0; I < Ctx.Field.Num(); ++I)
	{
		const FFielder& F = Ctx.Field[I];
		AStaticMeshActor* Who = F.bBowler ? Bowler.Get() : Fielders.IsValidIndex(I) ? Fielders[I].Get() : nullptr;
		UCricketAnimInstance* Anim = Who && !Who->IsHidden() ? AnimOf(Who) : nullptr;
		if (!Anim || (F.bBowler && (BowlW > 0.f || Bowl.Weight > 0.f))) continue;
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

		// A captured dive or throw moves the whole body, so the reach, crouch and gaze give way to it.
		const FClipPlay Dive = bLive && Diving && I == Result.Fielding.Fielder ? DiveClip(Post, Result.Fielding.FieldTime) : FClipPlay();
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
		P.LookWeight *= Own;

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
		const FVector Pelvis = Flat(ClipBoneAt(Bowling, TEXT("pelvis"), Bowl.Time));
		const FVector Anchor = FVector(PitchLength - 1.5f, 0.26f * Arm, 0.925f) - Flat(ClipBoneAt(Bowling, Arm > 0.f ? TEXT("hand_r") : TEXT("hand_l"), CricketPose::BowlClipRelease));
		BowlerShift = Bowl.Weight * Pelvis;
		BowlerPos = Anchor + (1.f - Bowl.Weight) * Pelvis;
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
	const TCHAR* BowlSide = Arm > 0.f ? TEXT("r") : TEXT("l");
	auto BowlBone = [&](const TCHAR* Name) { return FName(FString::Printf(TEXT("%s_%s"), Name, BowlSide)); };
	FVector BallPos = ToWorld(FVector(BowlerPos.X - 0.3f, 0.4f * Arm, 1.1f));
	if (BowlerBody && BowlerBody->RequiredBones.Contains(FBoneIndexType(BowlerBody->GetBoneIndex(BowlBone(TEXT("middle_02"))))))
		BallPos = (BowlerBody->GetSocketLocation(BowlBone(TEXT("index_02"))) + BowlerBody->GetSocketLocation(BowlBone(TEXT("middle_02"))) + BowlerBody->GetSocketLocation(BowlBone(TEXT("ring_02")))) / 3.f;
	else if (BowlerBody) BallPos = BowlerBody->GetSocketLocation(BowlBone(TEXT("hand")));
	if (bLive) BallPos = ToWorld(Result.BallAt(T));
	// In a fielder's hands from the take until they let it go. The simulation holds it where it was taken, but a
	// captured dive or throw carries the body up to a metre from there. In the right hand: the midpoint of two hands
	// floats in the air whenever they part.
	// ponytail: the thrown path starts from the take, so the ball jumps from the hand on the release frame; start the
	// throw at the hand if it shows.
	const int32 HolderIndex = bLive ? Result.HolderAt(Post) : -1;
	const AActor* Holder = !Ctx.Field.IsValidIndex(HolderIndex) ? nullptr : Ctx.Field[HolderIndex].bBowler ? Bowler.Get() : Fielders.IsValidIndex(HolderIndex) ? Fielders[HolderIndex].Get() : nullptr;
	if (const USkeletalMeshComponent* Hands = Holder ? BodyOf(Holder) : nullptr) BallPos = Hands->GetSocketLocation(TEXT("hand_r"));
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
		const bool bDiving = Fd.bDive || A == EFieldAction::CatchDiving;
		const bool bGround = bDiving || A == EFieldAction::SlideStop;
		const FFielderMove* Chase = Fd.Moves.FindByPredicate([&Fd](const FFielderMove& M) { return M.Fielder == Fd.Fielder; });
		USkeletalMeshComponent* DiverBody = Fielders.IsValidIndex(Fd.Fielder) ? BodyOf(Fielders[Fd.Fielder]) : nullptr;
		const CricketPose::FClipPlay Dive = CricketPose::DiveClip(Post, Fd.FieldTime);
		if (bDiving && DiveAnims[0] && DiveAnims[1] && DiverBody && Chase && !Ctx.Field[Fd.Fielder].bBowler && Post >= Fd.FieldTime - CricketPose::DiveClipStretch)
		{
			// The captured dive carries the body across on its own. Stand the fielder where it launches so its hands
			// reach the take on time, turned so it dives from where they were running, to whichever side leaves them
			// facing where the ball came from. After it they stay where they got up.
			AActor* Who = Fielders[Fd.Fielder];
			const FVector2D Take(Fd.FieldPos), Launch = CricketField::PositionOf(*Chase, Ctx.Field[Fd.Fielder], Fd.FieldTime - CricketPose::DiveClipStretch, Ctx.Fielding.RunSpeed);
			const FVector2D Out = Launch.Equals(Take, 0.01f) ? FVector2D(-1.f, 0.f) : (Launch - Take).GetSafeNormal(); // from the take back to the launch
			const FVector2D Came = -FVector2D(Result.BallAt(Fd.FieldTime) - Result.BallAt(Fd.FieldTime - 0.1f)).GetSafeNormal();
			const FQuat BodyTurn = DiverBody->GetRelativeRotation().Quaternion();
			auto Flat = [&BodyTurn](const FVector& C) { const FVector V = BodyTurn.RotateVector(C) / 100.f; return FVector2D(V.X, V.Y); }; // clip cm to actor m
			float Yaw = 0.f, Best = -2.f;
			FVector2D Reach;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const UAnimSequence* Seq = DiveAnims[Side];
				const FVector2D Hands = Flat(0.5f * (ClipBoneAt(Seq, TEXT("hand_l"), CricketPose::DiveClipStretch) + ClipBoneAt(Seq, TEXT("hand_r"), CricketPose::DiveClipStretch)) - ClipBoneAt(Seq, TEXT("pelvis"), 0.f));
				const float Y = FMath::RadiansToDegrees(FMath::Atan2(-Out.Y, -Out.X) - FMath::Atan2(Hands.Y, Hands.X));
				const float Facing = FVector2D::DotProduct(FVector2D(FMath::Cos(FMath::DegreesToRadians(Y)), FMath::Sin(FMath::DegreesToRadians(Y))), Came);
				if (Facing > Best) { Best = Facing; Yaw = Y; Reach = Hands; Diving = DiveAnims[Side]; }
			}
			// ponytail: the clip's full-length dive (~3.6 m to the hands) is longer than the solver's DiveReach (2.3 m),
			// so the fielder slides that difference as the clip blends in; scale the take's reach if it shows.
			const FVector2D From = Take + Out * Reach.Size();
			const FRotator Turn(0.f, Yaw, 0.f);
			const FVector Travel = Turn.RotateVector(FVector(Flat(ClipBoneAt(Diving, TEXT("pelvis"), FMath::Min(Dive.Time, CricketPose::DiveClipUp)) - ClipBoneAt(Diving, TEXT("pelvis"), 0.f)), 0.f));
			// The actor carries whatever share of the travel the clip no longer does (fading out, or under the throw).
			const float Held = Dive.Weight * (1.f - ThrowPlay(Fd.Fielder, Post).Weight);
			const float In = FMath::SmoothStep(0.f, 0.3f, Dive.Time);
			const FVector There = FVector(From.X, From.Y, 0.9f) + (1.f - Held) * FVector(Travel.X, Travel.Y, 0.f);
			Who->SetActorLocation(ToWorld(FMath::Lerp(Who->GetActorLocation() / 100.f, There, In)));
			if (Dive.Weight > 0.f)
			{
				Who->SetActorRotation(FMath::Lerp(FRotator(0.f, Who->GetActorRotation().Yaw, 0.f), Turn, In));
				FigureStates.FindOrAdd(Fielders[Fd.Fielder]).bHeld = true;
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
			Who->SetActorRotation(FRotationMatrix::MakeFromZ(Up).Rotator());
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
		const FVector2D From = Run.RunTimes.Num() > 0 && Post < Run.RunTimes[0] + 0.3f ? StrikerFrom : FVector2D(0.9f, -0.35f * Off);
		const float SX = FMath::Lerp(0.9f, PitchLength - 1.3f, Along) + (From.X - 0.9f) * (1.f - Along), NX = FMath::Lerp(PitchLength - 1.3f, 0.9f, Along);
		// Each batter eases from where they stood into their own lane, either side of the pitch: the
		// non-striker's on the side they backed up (away from the bowler's arm), the striker's the other.
		const float Lane = FMath::SmoothStep(0.35f, 0.9f, Post);
		Striker->SetActorLocation(ToWorld(FVector(SX, FMath::Lerp(From.Y, 1.1f * Arm, Lane), 0.9f)));
		NonStriker->SetActorLocation(ToWorld(FVector(NX, -1.1f * Arm, 0.9f)));
	}
	// Otherwise the striker's own footwork (UpdatePoses) moves them.

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
}
