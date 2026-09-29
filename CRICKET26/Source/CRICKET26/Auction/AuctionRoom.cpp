#include "AuctionRoom.h"
#include "AuctionEngine.h"
#include "FrontendStyle.h"
#include "CricketStadium.h"
#include "CricketAnimInstance.h"
#include "SuperOverGameMode.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CanvasItem.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

using CricketStadium::FColouredMesh;
using namespace AuctionRoomLayout;

namespace
{
	constexpr float Cm = 100.f;
	const FLinearColor Gold(1.f, 0.66f, 0.18f), Teal(0.02f, 0.45f, 0.4f), Ink(0.004f, 0.022f, 0.022f);
	const FVector Up = FVector::UpVector;
	// About 450 lux on the tables and 1500 on the auctioneer (E = I / d^2), the exposure set for the tables.
	constexpr float TableLux = 450.f;

	/** A box with all six faces, its half extents along any three axes (m). */
	void Solid(FColouredMesh& M, const FVector& C, const FVector& X, const FVector& Y, const FVector& Z, const FLinearColor& Col)
	{
		// Each face: its outward half-axis and the two half-axes across it.
		const FVector Faces[3][3] = { { X, Y, Z }, { Y, X, Z }, { Z, X, Y } };
		for (const auto& F : Faces)
			for (float S : { 1.f, -1.f })
			{
				const FVector N = F[0] * S, U = F[1], V = F[2];
				M.AddQuad(C + N - U - V, C + N + U - V, C + N + U + V, C + N - U + V, Col, N);
			}
	}
	void Box(FColouredMesh& M, const FVector& C, const FVector& Half, const FLinearColor& Col)
	{
		Solid(M, C, FVector(Half.X, 0, 0), FVector(0, Half.Y, 0), FVector(0, 0, Half.Z), Col);
	}
	/** A flat disc of N sides in the plane across Normal, seen from the Normal side. */
	void Disc(FColouredMesh& M, const FVector& C, const FVector& Normal, float R, const FLinearColor& Col, int32 N = 20)
	{
		const FVector A = FVector::CrossProduct(Normal, FMath::Abs(Normal.Z) > 0.9f ? FVector::ForwardVector : Up).GetSafeNormal();
		const FVector B = FVector::CrossProduct(Normal, A);
		for (int32 I = 0; I < N; ++I)
		{
			const float T0 = 2.f * PI * I / N, T1 = 2.f * PI * (I + 1) / N;
			const FVector P0 = C + R * (A * FMath::Cos(T0) + B * FMath::Sin(T0)), P1 = C + R * (A * FMath::Cos(T1) + B * FMath::Sin(T1));
			M.AddQuad(C, P0, P1, P1, Col, Normal);
		}
	}

	FVector Across(const FVector& D) { return FVector::CrossProduct(Up, D); }
	const FVector AuctioneerFloor(-0.55f, 0.f, StageHeight);
	FVector AuctioneerHead() { return AuctioneerFloor + FVector(0.05f, 0.f, 1.62f); }

	/** 0 to 1 and back over a gesture: up in Rise seconds, held, and down over the last Fall seconds of Length. */
	float Envelope(float T, float Length, float Rise = 0.25f, float Fall = 0.35f)
	{
		if (T < 0.f || T > Length) return 0.f;
		return FMath::SmoothStep(0.f, 1.f, FMath::Min(T / Rise, (Length - T) / Fall));
	}
}

namespace AuctionRoomLayout
{
	FVector TableFacing(int32)
	{
		return -FVector::ForwardVector;
	}
	FVector TableFront(int32 Team)
	{
		// Front three per bank, then two staggered on a low riser. PBKS is ahead of SRH in the reference shot.
		static const FVector Fronts[] = {
			{ 8.3f, -4.6f, 0.f }, { 8.3f, 4.6f, 0.f }, { 8.3f, 8.2f, 0.f },
			{ 8.3f, -8.2f, 0.f }, { 13.5f, -10.f, RearRowHeight }, { 13.5f, 6.4f, RearRowHeight },
			{ 8.3f, -11.8f, 0.f }, { 13.5f, 10.f, RearRowHeight },
			{ 13.5f, -6.4f, RearRowHeight }, { 8.3f, 11.8f, 0.f }
		};
		check(Team >= 0 && Team < UE_ARRAY_COUNT(Fronts));
		return Fronts[Team];
	}
	FVector SeatAt(int32 Team, int32 Seat)
	{
		const FVector D = -TableFacing(Team);
		// The staff face the lectern (-D), so their right is -Across(D): seat 0 there.
		const float Side = Seat == 2 ? 0.f : Seat == 0 ? -1.f : 1.f;
		return TableFront(Team) + D * (2.f * TableHalfDepth + 0.25f) + Across(D) * Side * (SeatsPerTable > 2 ? 0.9f : 0.62f);
	}
	int32 StaffFor(int32 Team, int32 Seat, int32 NumStaff)
	{
		if (NumStaff <= 0) return INDEX_NONE;
		const int32 First = Team % NumStaff;
		if (Seat == 0 || NumStaff == 1) return First;
		// 3t+5 runs through every index once (mod 10) and never lands on t, so each face sits at two different tables.
		const int32 Second = (3 * Team + 5) % NumStaff != First ? (3 * Team + 5) % NumStaff : (First + 1) % NumStaff;
		if (Seat == 1 || NumStaff == 2) return Second;
		// The middle seat: the first face from 7t+2 on that is neither of the other two.
		for (int32 K = 7 * Team + 2;; ++K)
			if (K % NumStaff != First && K % NumStaff != Second) return K % NumStaff;
	}
}

AAuctionRoom::AAuctionRoom()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Accent = AccentGoal = Teal;
}

double AAuctionRoom::Now() const { return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0; }

void AAuctionRoom::BeginPlay()
{
	Super::BeginPlay();
	BuildHall();
	SpawnPeople();
	Camera = GetWorld()->SpawnActor<ACameraActor>();
	Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
#if !PLATFORM_ANDROID && !PLATFORM_IOS
	// The split screen's two cameras. Each is a second render of the room, which a phone cannot spare: there the
	// director stays on one camera.
	for (int32 I = 0; I < 2; ++I)
	{
		SplitTargets[I] = UKismetRenderingLibrary::CreateRenderTarget2D(this, 1024, 640, RTF_RGBA8);
		SplitCaptures[I] = NewObject<USceneCaptureComponent2D>(this);
		SplitCaptures[I]->CaptureSource = SCS_FinalColorLDR;
		SplitCaptures[I]->TextureTarget = SplitTargets[I];
		SplitCaptures[I]->bCaptureEveryFrame = false;
		SplitCaptures[I]->bCaptureOnMovement = false;
		SplitCaptures[I]->bAlwaysPersistRenderingState = true;
		// The hall's vignette and exposure crush a small inset: none, and half a stop brighter.
		SplitCaptures[I]->PostProcessSettings.bOverride_VignetteIntensity = true;
		SplitCaptures[I]->PostProcessSettings.VignetteIntensity = 0.f;
		SplitCaptures[I]->PostProcessSettings.bOverride_AutoExposureBias = true;
		SplitCaptures[I]->PostProcessSettings.AutoExposureBias = 0.5f - FMath::Log2(TableLux / 2.5f);
		SplitCaptures[I]->SetupAttachment(RootComponent);
		SplitCaptures[I]->RegisterComponent();
	}
#endif
	DrawWall(nullptr, nullptr);
	DrawSideScreens(nullptr);
	Cut(Wide());
}

void AAuctionRoom::BuildHall()
{
	UWorld* W = GetWorld();
	UMaterialInterface* VertexColour = LoadObject<UMaterialInterface>(nullptr, TEXT("/MeshModelingToolset/Materials/M_DynamicMeshComponentVtxColor.M_DynamicMeshComponentVtxColor"));
	UMaterialInterface* ScreenMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Stadium/M_Screen.M_Screen"), nullptr, LOAD_Quiet | LOAD_NoWarn);
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (!VertexColour) return;

	auto Place = [&](const FColouredMesh& Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(CricketStadium::ToStaticMesh(this, Mesh, Material));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		return C;
	};
	auto Glowing = [&](UTexture* Texture, float Glow)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(ScreenMaterial, this);
		M->SetTextureParameterValue(TEXT("Screen"), Texture);
		M->SetScalarParameterValue(TEXT("Glow"), Glow);
		return M;
	};
	// A flat panel showing a canvas: centre and size in metres, facing Facing, its texture's X to the viewer's right.
	auto Panel = [&](const FVector& Centre, const FVector& Facing, float Width, float Height, UMaterialInterface* Material)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(Plane);
		C->SetMaterial(0, Material);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		const FVector Right = FVector::CrossProduct(Up, -Facing);
		C->SetWorldLocationAndRotation((Centre + Facing * 0.01f) * Cm, FRotationMatrix::MakeFromZX(Facing, Right).Rotator());
		C->SetWorldScale3D(FVector(Width, Height, 1.f));
	};

	// ---- The hall ---------------------------------------------------------------------------------------------
	FColouredMesh Hall, Lit;
	const FLinearColor Carpet(0.016f, 0.02f, 0.04f), Wall(0.01f, 0.012f, 0.02f), Black(0.012f, 0.012f, 0.015f), White(0.78f, 0.78f, 0.76f);
	Hall.AddQuad({ -8, -26, 0 }, { 34, -26, 0 }, { 34, 26, 0 }, { -8, 26, 0 }, Carpet, Up);
	for (float Side : { -1.f, 1.f })
		Box(Hall, FVector(15.f, Side * 8.2f, RearRowHeight * 0.5f), FVector(2.2f, 4.4f, RearRowHeight * 0.5f), Black);
	Hall.AddQuad({ -8, -26, 13 }, { 34, -26, 13 }, { 34, 26, 13 }, { -8, 26, 13 }, Wall * 0.5f, -Up);
	Hall.AddQuad({ -8, -26, 0 }, { -8, 26, 0 }, { -8, 26, 13 }, { -8, -26, 13 }, Wall, FVector::ForwardVector);
	Hall.AddQuad({ 34, -26, 0 }, { 34, 26, 0 }, { 34, 26, 13 }, { 34, -26, 13 }, Wall, -FVector::ForwardVector);
	for (float Y : { -26.f, 26.f })
	{
		Hall.AddQuad({ -8, Y, 0 }, { 34, Y, 0 }, { 34, Y, 13 }, { -8, Y, 13 }, Wall, FVector(0, -FMath::Sign(Y), 0));
		// Tall light slots down the side walls, in the accent colour.
		for (float X = -2.f; X < 32.f; X += 4.f) Box(Lit, FVector(X, Y - FMath::Sign(Y) * 0.05f, 6.f), FVector(0.08f, 0.04f, 4.5f), FLinearColor::White);
	}
	// The stage, its front a band of light, and the lectern with a gold rail.
	Box(Hall, FVector(-3.5f, 0.f, StageHeight / 2.f), FVector(4.5f, 11.f, StageHeight / 2.f), Black);
	Box(Lit, FVector(1.005f, 0.f, StageHeight * 0.5f), FVector(0.01f, 11.f, StageHeight * 0.2f), FLinearColor::White);
	Box(Hall, FVector(-0.05f, 0.f, StageHeight + 0.55f), FVector(0.25f, 0.45f, 0.55f), FLinearColor(0.03f, 0.026f, 0.026f));
	Box(Hall, FVector(-0.08f, 0.f, StageHeight + 1.12f), FVector(0.3f, 0.5f, 0.02f), FLinearColor(0.1f, 0.05f, 0.025f));
	Box(Hall, FVector(0.21f, 0.f, StageHeight + 0.95f), FVector(0.01f, 0.45f, 0.03f), Gold);
	Box(Lit, FVector(0.21f, 0.f, StageHeight + 0.35f), FVector(0.01f, 0.4f, 0.015f), FLinearColor::White);
	// The trophy on its plinth, stage left of the lectern. ponytail: stacked gold boxes; a modelled cup when there is one.
	const FVector Plinth(-1.2f, 2.4f, StageHeight);
	Box(Hall, Plinth + FVector(0, 0, 0.5f), FVector(0.3f, 0.3f, 0.5f), Black);
	const FVector Cup = Plinth + FVector(0, 0, 1.f);
	Box(Hall, Cup + FVector(0, 0, 0.05f), FVector(0.14f, 0.14f, 0.05f), FLinearColor(0.08f, 0.04f, 0.02f));
	Box(Hall, Cup + FVector(0, 0, 0.22f), FVector(0.03f, 0.03f, 0.12f), Gold);
	for (int32 I = 0; I < 4; ++I) { const float R = 0.06f + 0.028f * I; Box(Hall, Cup + FVector(0, 0, 0.37f + 0.07f * I), FVector(R, R, 0.035f), Gold); }
	for (float S : { -1.f, 1.f }) Box(Hall, Cup + FVector(0, S * 0.19f, 0.5f), FVector(0.015f, 0.04f, 0.08f), Gold);

	// The LED wall's frame and pillars, and the arches of light over the stage.
	const float WallZ = StageHeight + 0.4f + 3.4f;
	Box(Hall, FVector(-6.7f, 0.f, WallZ), FVector(0.15f, 8.8f, 3.6f), Black);
	for (float S : { -1.f, 1.f }) Box(Lit, FVector(-6.5f, S * 8.95f, WallZ), FVector(0.06f, 0.08f, 3.6f), FLinearColor::White);
	for (float X : { -5.5f, -3.5f, -1.5f, 0.5f })
	{
		const float R = 9.8f - 0.3f * (X + 5.5f) / 2.f;
		constexpr int32 N = 36;
		for (int32 I = 0; I < N; ++I)
		{
			const float A0 = PI * I / N, A1 = PI * (I + 1) / N;
			const FVector P0(X, R * FMath::Cos(A0), StageHeight + R * FMath::Sin(A0)), P1(X, R * FMath::Cos(A1), StageHeight + R * FMath::Sin(A1));
			const FVector Along = (P1 - P0) * 0.5f, Side = FVector::CrossProduct(Along.GetSafeNormal(), FVector::ForwardVector);
			Solid(Lit, (P0 + P1) * 0.5f, Along, Side * 0.035f, FVector(0.035f, 0, 0), FLinearColor::White);
		}
	}

	// ---- The franchise tables ------------------------------------------------------------------------------------
	const TArray<FAuctionFranchise>& Teams = AuctionData::Franchises();
	for (int32 T = 0; T < Teams.Num() && T < 10; ++T)
	{
		const FAuctionFranchise& F = Teams[T];
		const FVector D = -TableFacing(T), A = Across(D), Front = TableFront(T);
		const FVector Top = Front + D * TableHalfDepth;
		Solid(Hall, Top + Up * (TableTop - 0.02f), D * TableHalfDepth, A * TableHalfLength, Up * 0.02f, White);
		Solid(Hall, Top + Up * 0.37f, D * (TableHalfDepth - 0.05f), A * (TableHalfLength - 0.03f), Up * 0.37f, F.Primary * 0.35f);
		// Keep the team-colour backdrop below shoulder height so neither rank hides the people behind it.
		const FVector Back = Front + D * (2.f * TableHalfDepth + 1.3f);
		Solid(Hall, Back + Up * 0.42f, D * 0.05f, A * 1.6f, Up * 0.42f, F.Primary * 0.6f);
		Solid(Hall, Back - D * 0.01f + Up * 0.8f, D * 0.05f, A * 1.6f, Up * 0.04f, F.Secondary);
		Solid(Hall, Back - D * 0.02f + Up * 0.85f, D * 0.05f, A * 1.6f, Up * 0.01f, Gold);
		// The team's placard in the middle of the table, and a line of light on the floor in front.
		Solid(Hall, Front + D * 0.18f + Up * (TableTop + 0.09f), D * 0.01f, A * 0.24f, Up * 0.09f, F.Primary);
		Solid(Lit, Front - D * 0.35f + Up * 0.005f, D * 0.03f, A * (TableHalfLength + 0.2f), Up * 0.005f, FLinearColor::White);
		for (int32 S = 0; S < SeatsPerTable; ++S)
		{
			const FVector P = SeatAt(T, S);
			// Chair: seat and back.
			Solid(Hall, P + Up * 0.45f, D * 0.24f, A * 0.24f, Up * 0.03f, Black);
			Solid(Hall, P + D * 0.24f + Up * 0.78f, D * 0.03f, A * 0.24f, Up * 0.3f, Black);
			// Laptop open toward its user, a water bottle, a name tent.
			const FVector Laptop = P - D * 0.6f + Up * (TableTop + 0.008f);
			Solid(Hall, Laptop, D * 0.12f, A * 0.17f, Up * 0.008f, FLinearColor(0.35f, 0.36f, 0.38f));
			const float Tilt = FMath::DegreesToRadians(15.f);
			const FVector LidUp = Up * FMath::Cos(Tilt) + D * FMath::Sin(Tilt), LidOut = FVector::CrossProduct(A, LidUp).GetSafeNormal();
			Solid(Hall, Laptop - D * 0.12f + LidUp * 0.12f, LidOut * 0.006f, A * 0.17f, LidUp * 0.12f, FLinearColor(0.35f, 0.36f, 0.38f));
			Solid(Hall, Laptop - D * 0.12f + LidUp * 0.12f + D * 0.008f, LidOut * 0.001f, A * 0.155f, LidUp * 0.105f, FLinearColor(0.02f, 0.035f, 0.06f));
			const float Side = S == 0 ? -1.f : 1.f;
			Solid(Hall, P - D * 0.5f + A * Side * 0.34f + Up * (TableTop + 0.11f), D * 0.03f, A * 0.03f, Up * 0.11f, FLinearColor(0.45f, 0.62f, 0.8f));
			Solid(Hall, P - D * 0.5f + A * Side * 0.34f + Up * (TableTop + 0.235f), D * 0.015f, A * 0.015f, Up * 0.015f, White);
			const FVector Tent = Front + D * 0.12f + A * Side * 0.62f + Up * TableTop;
			Hall.AddQuad(Tent - D * 0.05f - A * 0.12f, Tent - D * 0.05f + A * 0.12f, Tent + Up * 0.1f + A * 0.12f, Tent + Up * 0.1f - A * 0.12f, White, -D + Up * 0.5f);
			Hall.AddQuad(Tent + D * 0.05f - A * 0.12f, Tent + D * 0.05f + A * 0.12f, Tent + Up * 0.1f + A * 0.12f, Tent + Up * 0.1f - A * 0.12f, White, D + Up * 0.5f);
		}
		// The skirt across the table's front: the franchise's colour and name, drawn once.
		if (ScreenMaterial && Plane)
		{
			UTextureRenderTarget2D* Skirt = UKismetRenderingLibrary::CreateRenderTarget2D(this, 1024, 272, RTF_RGBA8_SRGB);
			SkirtTargets.Add(Skirt);
			DrawSkirt(T);
			UMaterialInstanceDynamic* Brand = Glowing(Skirt, 60.f);
			Panel(Front + Up * 0.38f, -D, 2.f * TableHalfLength, 0.72f, Brand);
			Panel(Back + D * 0.07f + Up * 0.4f, D, 2.8f, 0.5f, Brand);
		}
		// The paddle: a disc in the team's colours, rimmed in its second colour, on a black handle along +Z.
		FColouredMesh PaddleMesh;
		Box(PaddleMesh, FVector(0, 0, 0.11f), FVector(0.012f, 0.012f, 0.11f), Black);
		for (float S : { 1.f, -1.f })
		{
			Disc(PaddleMesh, FVector(S * 0.004f, 0, 0.33f), FVector(S, 0, 0), 0.13f, F.Secondary);
			Disc(PaddleMesh, FVector(S * 0.006f, 0, 0.33f), FVector(S, 0, 0), 0.11f, F.Primary);
		}
		UStaticMeshComponent* Paddle = Place(PaddleMesh, VertexColour);
		Paddle->SetCastShadow(false);
		Paddles.Add(Paddle);
	}
	Place(Hall, VertexColour);

	// The gavel, on the lectern until she picks it up.
	FColouredMesh GavelMesh;
	Box(GavelMesh, FVector(0.12f, 0, 0), FVector(0.12f, 0.01f, 0.01f), FLinearColor(0.12f, 0.06f, 0.03f));
	Box(GavelMesh, FVector::ZeroVector, FVector(0.03f, 0.065f, 0.03f), FLinearColor(0.1f, 0.05f, 0.025f));
	Gavel = Place(GavelMesh, VertexColour);

	if (!ScreenMaterial || !Plane)
	{
		Place(Lit, VertexColour);
		return;
	}
	// Everything in Lit glows in the accent colour, one texel the director recolours (Update).
	AccentTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, 4, 4, RTF_RGBA8_SRGB);
	UStaticMeshComponent* Glow = Place(Lit, Glowing(AccentTarget, 160.f));
	Glow->SetCastShadow(false);
	// The LED wall and the two side screens.
	WallTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, 2048, 820, RTF_RGBA8_SRGB);
	Panel(FVector(-6.54f, 0.f, WallZ), FVector::ForwardVector, 17.f, 6.8f, Glowing(WallTarget, 380.f));
	SideTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, 1280, 720, RTF_RGBA8_SRGB);
	for (float S : { -1.f, 1.f })
	{
		const FVector At(-1.f, S * 13.f, 4.0f);
		Box(Hall, At, FVector(0.1f, 0.1f, 0.1f), Black);
		Panel(At, (FVector(10.f, 0.f, At.Z) - At).GetSafeNormal(), 6.4f, 3.6f, Glowing(SideTarget, 320.f));
	}

	// ---- Light -----------------------------------------------------------------------------------------------------
	auto Spot = [&](const FVector& From, const FVector& To, float Candela, float Cone, bool bShadow)
	{
		USpotLightComponent* L = NewObject<USpotLightComponent>(this);
		L->SetMobility(EComponentMobility::Movable);
		L->SetupAttachment(RootComponent);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(Candela);
		L->SetLightColor(FLinearColor(1.f, 0.95f, 0.88f));
		L->SetAttenuationRadius(4000.f);
		L->SetOuterConeAngle(Cone);
		L->SetInnerConeAngle(Cone * 0.6f);
		L->SetCastShadows(bShadow);
		L->SetVolumetricScatteringIntensity(1.5f);
		L->RegisterComponent();
		L->SetWorldLocationAndRotation(From * Cm, (To - From).Rotation());
		return L;
	};
	Spot(FVector(5.f, -2.5f, 6.5f), AuctioneerHead() - FVector(0, 0, 0.3f), 1500.f * 55.f, 16.f, true);
	Spot(FVector(5.f, 3.f, 5.f), AuctioneerHead(), 500.f * 45.f, 18.f, false);
	Spot(FVector(3.f, -3.5f, 7.f), Plinth + FVector(0, 0, 1.4f), 900.f * 60.f, 8.f, false);
	for (int32 T = 0; T < 10; ++T)
	{
		const FVector Mid = TableFront(T) + (-TableFacing(T)) * 0.8f;
		Spot(Mid - TableFacing(T) * 1.5f + Up * 8.f, Mid + Up * 0.8f, TableLux * 55.f, 24.f, true);
		// The key is behind the staff, so their faces need a soft fill from the stage side, as TV lights a room. It
		// lights the people only (lighting channel 1, SpawnPeople): past their heads it would make a hot spot on the
		// backdrop and wash the franchise colour out of it.
		Spot(Mid + TableFacing(T) * 5.f + Up * 3.f, Mid + Up * 1.15f, TableLux * 16.f, 18.f, false)->SetLightingChannels(false, true, false);
	}
	// The LED wall lights the hall in its colour.
	WallLight = NewObject<URectLightComponent>(this);
	WallLight->SetMobility(EComponentMobility::Movable);
	WallLight->SetupAttachment(RootComponent);
	WallLight->SetIntensityUnits(ELightUnits::Candelas);
	WallLight->SetIntensity(9000.f);
	WallLight->SetSourceWidth(1700.f);
	WallLight->SetSourceHeight(680.f);
	WallLight->SetAttenuationRadius(5000.f);
	WallLight->SetCastShadows(false);
	WallLight->RegisterComponent();
	WallLight->SetWorldLocationAndRotation(FVector(-6.4f, 0.f, WallZ) * Cm, FRotator::ZeroRotator);

	// A little haze for the beams, and the exposure.
	AExponentialHeightFog* Fog = W->SpawnActor<AExponentialHeightFog>();
	UExponentialHeightFogComponent* FogC = Fog->GetComponent();
	FogC->SetFogDensity(0.02f);
	FogC->SetFogHeightFalloff(0.05f);
	FogC->SetFogInscatteringColor(FLinearColor(0.004f, 0.006f, 0.01f));
	FogC->SetVolumetricFog(W->GetFeatureLevel() >= ERHIFeatureLevel::SM5);
	FogC->SetVolumetricFogDistance(4000.f);
	APostProcessVolume* PP = W->SpawnActor<APostProcessVolume>();
	PP->bUnbound = true;
	PP->Settings.bOverride_AutoExposureMethod = true;
	PP->Settings.AutoExposureMethod = AEM_Manual;
	PP->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PP->Settings.AutoExposureApplyPhysicalCameraExposure = false;
	PP->Settings.bOverride_AutoExposureBias = true;
	PP->Settings.AutoExposureBias = -FMath::Log2(TableLux / 2.5f);
	PP->Settings.bOverride_BloomIntensity = true;
	PP->Settings.BloomIntensity = 0.6f;
	PP->Settings.bOverride_VignetteIntensity = true;
	PP->Settings.VignetteIntensity = 0.45f;
}

USkeletalMeshComponent* AAuctionRoom::PrepareParts(AActor* Person)
{
	TArray<USkeletalMeshComponent*> Parts;
	Person->GetComponents(Parts);
	USkeletalMeshComponent* Body = nullptr;
	for (USkeletalMeshComponent* Part : Parts)
	{
		const bool bBody = Part->GetFName() == TEXT("Body");
		if (bBody) Body = Part;
		// The director hard-cuts between tables, so the body and the face that copies its pose keep posing off
		// screen: a face posed only when seen shows its last pose, standing, on the first frame after a cut to a
		// seated table (the neck stretched up to it). The rest follow the body's pose and need no tick of their own.
		if (bBody || Part->GetFName() == TEXT("Face")) Part->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Part->SetCastContactShadow(false);
	}
	// The tables' face fill is on channel 1, so it lights people and not the backdrops behind them. Hair and
	// eyebrows are grooms, not skeletal meshes, so every part is put on it.
	TArray<UPrimitiveComponent*> Prims;
	Person->GetComponents(Prims);
	for (UPrimitiveComponent* Prim : Prims) Prim->SetLightingChannels(true, true, false);
	return Body;
}

void AAuctionRoom::SpawnPeople()
{
	// The MetaHumans from Scripts/metahuman/make_auction_people.sh; whoever has not been built is skipped.
	static const TCHAR* StaffNames[] = { TEXT("Mateo"), TEXT("Orlando"), TEXT("Trey"), TEXT("Jorge"), TEXT("Dominic"), TEXT("Lorenzo") };
	auto Load = [](const FString& Name) { return LoadClass<AActor>(nullptr, *FString::Printf(TEXT("/Game/MetaHumans/%s/BP_%s.BP_%s_C"), *Name, *Name, *Name), nullptr, LOAD_Quiet | LOAD_NoWarn); };
	TArray<UClass*> Staff;
	for (const TCHAR* N : StaffNames) if (UClass* C = Load(FString(TEXT("MH_Staff_")) + N)) Staff.Add(C);
	UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));

	auto Spawn = [&](UClass* Look, const FVector& Floor, const FVector& Facing, int32 Team, int32 Seat)
	{
		// A MetaHuman's body faces its actor's +Y.
		AActor* A = GetWorld()->SpawnActor<AActor>(Look, FTransform(FRotator(0.f, Facing.Rotation().Yaw - 90.f, 0.f), Floor * Cm));
		if (!A) return;
		A->SetActorEnableCollision(false);
		Spawned.Add(A);
		USkeletalMeshComponent* Body = PrepareParts(A);
		if (!Body) return;
		// Every garment mesh follows the body's bones exactly (leader pose): a sleeve playing its own
		// copy of the idle while the body solves the paddle, huddle and applause by IK leaves the upper
		// arm and the forearm visibly split at the elbow. Nothing else about them changes. The face
		// keeps its own rig.
		TArray<USkeletalMeshComponent*> Followers;
		A->GetComponents(Followers);
		for (USkeletalMeshComponent* Follower : Followers)
			if (Follower && Follower != Body && Follower->GetFName() != TEXT("Face"))
				Follower->SetLeaderPoseComponent(Body);
		// Franchise staff wear polos in the team colour, as in Jeddah; the auctioneer a deep red, as Mallika Sagar did.
		const FLinearColor Colour = Team == INDEX_NONE ? FLinearColor(0.3f, 0.015f, 0.03f) : AuctionData::Franchises()[Team].Primary;
		TArray<USkeletalMeshComponent*> Parts;
		A->GetComponents(Parts);
		// Everything but the skin is the outfit (as SuperOverGameMode dresses its players).
		for (USkeletalMeshComponent* Part : Parts)
			if (Part->GetFName() != TEXT("Body") && Part->GetFName() != TEXT("Face"))
				for (int32 I = 0; I < Part->GetNumMaterials(); ++I)
					if (UMaterialInstanceDynamic* Cloth = Part->CreateDynamicMaterialInstance(I)) ASuperOverGameMode::TintOutfit(Cloth, Colour);
		// Their textures stream in now, not grey on the first cut to a table nobody has looked at.
		A->PrestreamTextures(10.f, true);
		Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Body->SetAnimInstanceClass(UCricketAnimInstance::StaticClass());
		Body->AddTickPrerequisiteActor(this);
		if (UCricketAnimInstance* Anim = Cast<UCricketAnimInstance>(Body->GetAnimInstance())) Anim->Idle = Idle;
		People.Add({ A, Body, Team, Seat, Floor, Facing });
	};
	if (UClass* Auctioneer = Load(TEXT("MH_Auctioneer"))) Spawn(Auctioneer, AuctioneerFloor, FVector::ForwardVector, INDEX_NONE, 0);
	for (int32 T = 0; T < 10; ++T)
		for (int32 S = 0; S < SeatsPerTable; ++S)
		{
			const int32 Who = StaffFor(T, S, Staff.Num());
			if (Who != INDEX_NONE) Spawn(Staff[Who], SeatAt(T, S), TableFacing(T), T, S);
		}
	UE_LOG(LogTemp, Display, TEXT("Auction room: %d people (%d staff looks)"), People.Num(), Staff.Num());
}

// ---- Screens ---------------------------------------------------------------------------------------------------

namespace
{
	/** Canvas text in a Slate font Px tall, placed at X by Align (0 left, 0.5 centre, 1 right), its middle at Y. */
	/**
	 * The broadcast face (Barlow Condensed) as a runtime-cached UFont. A canvas drawing into a render target shows
	 * nothing for a bare FSlateFontInfo (the HUD's fonts); a UFont draws, as the stadium scoreboard's Roboto does.
	 */
	UFont* ScreenFont(bool bBold)
	{
		static TStrongObjectPtr<UFont> Fonts[2];
		TStrongObjectPtr<UFont>& F = Fonts[bBold];
		if (!F)
		{
			const FString Path = FPaths::ProjectContentDir() / (bBold ? TEXT("UI/Fonts/BarlowCondensed-ExtraBold.ttf") : TEXT("UI/Fonts/BarlowCondensed-Medium.ttf"));
			if (!FPaths::FileExists(Path)) return GEngine->GetLargeFont();
			F.Reset(NewObject<UFont>());
			F->FontCacheType = EFontCacheType::Runtime;
			F->LegacyFontSize = 72;
			F->CompositeFont.DefaultTypeface.AppendFont(TEXT("Default"), Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
			FrontendStyle::AddRupee(F->CompositeFont);
		}
		return F.Get();
	}

	/** Canvas text Px tall, placed at X by Align (0 left, 0.5 centre, 1 right), its middle at Y. */
	void CanvasText(UCanvas* Canvas, const FString& S, float X, float Y, float Px, const FLinearColor& C, float Align = 0.f, bool bBold = true)
	{
		UFont* Font = ScreenFont(bBold);
		float W, H;
		Canvas->TextSize(Font, S, W, H);
		const float Scale = H > 0.f ? Px / H : 1.f;
		FCanvasTextItem Item(FVector2D(X - Align * W * Scale, Y - 0.5f * H * Scale), FText::FromString(S), Font, C);
		Item.Scale = FVector2D(Scale, Scale);
		Canvas->DrawItem(Item);
	}
	void Tile(UCanvas* Canvas, float X, float Y, float W, float H, const FLinearColor& C)
	{
		FCanvasTileItem Item(FVector2D(X, Y), FVector2D(W, H), C);
		Canvas->DrawItem(Item);
	}
	template <typename FDraw>
	void Draw(UObject* Ctx, UTextureRenderTarget2D* Target, const FLinearColor& Clear, FDraw&& Body)
	{
		if (!Target) return;
		UKismetRenderingLibrary::ClearRenderTarget2D(Ctx, Target, Clear);
		UCanvas* Canvas = nullptr;
		FVector2D Size;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(Ctx, Target, Canvas, Size, Context);
		Body(Canvas, float(Size.X), float(Size.Y));
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(Ctx, Context);
	}
}

void AAuctionRoom::DrawSkirt(int32 Team)
{
	const FAuctionFranchise& F = AuctionData::Franchises()[Team];
	Draw(this, SkirtTargets.IsValidIndex(Team) ? SkirtTargets[Team].Get() : nullptr, F.Primary, [&](UCanvas* C, float W, float H)
	{
		Tile(C, 0, H * 0.84f, W, H * 0.1f, F.Secondary);
		Tile(C, 0, H * 0.94f, W, H * 0.02f, Gold);
		CanvasText(C, F.Name.ToUpper(), W / 2, H * 0.4f, H * 0.34f, FLinearColor::White, 0.5f);
	});
}

void AAuctionRoom::DrawWall(const FAuction* A, const FAuctionEventRecord* E)
{
	const TArray<FAuctionFranchise>& Teams = AuctionData::Franchises();
	const bool bSold = A && A->Phase == EAuctionPhase::Hammer && Teams.IsValidIndex(A->LastSoldTo);
	const FLinearColor Back = bSold ? Teams[A->LastSoldTo].Primary * 0.8f : Ink;
	Draw(this, WallTarget, Back, [&](UCanvas* C, float W, float H)
	{
		// Gold rules top and bottom, like the broadcast's frames.
		Tile(C, 0, H * 0.04f, W, 4, Gold);
		Tile(C, 0, H * 0.96f - 4, W, 4, Gold);
		if (!A || A->Phase == EAuctionPhase::Retention)
		{
			const bool bMini = A && A->IsMini();
			CanvasText(C, bMini ? FString::Printf(TEXT("IPL %d AUCTION"), A->Config.Season) : FString(TEXT("IPL MEGA AUCTION")), W / 2, H * 0.42f, H * 0.2f, FLinearColor::White, 0.5f);
			CanvasText(C, bMini ? FString(TEXT("TEN FRANCHISES  ·  TOPPING UP TO 25")) : FString(TEXT("TEN FRANCHISES  ·  ₹120 CRORE EACH")), W / 2, H * 0.64f, H * 0.07f, Gold, 0.5f);
			return;
		}
		if (A->Phase == EAuctionPhase::Break)
		{
			CanvasText(C, TEXT("END OF DAY ONE"), W / 2, H * 0.42f, H * 0.18f, FLinearColor::White, 0.5f);
			CanvasText(C, TEXT("THE AUCTION RESUMES WITH THE UNCAPPED SETS"), W / 2, H * 0.62f, H * 0.06f, Gold, 0.5f);
			return;
		}
		if (A->Phase == EAuctionPhase::Finished)
		{
			CanvasText(C, TEXT("AUCTION COMPLETE"), W / 2, H * 0.45f, H * 0.18f, FLinearColor::White, 0.5f);
			return;
		}
		const FAuctionSet* Set = A->CurrentSet();
		if (A->Phase == EAuctionPhase::SetIntro && Set)
		{
			CanvasText(C, TEXT("NEXT SET"), W / 2, H * 0.3f, H * 0.07f, Gold, 0.5f);
			CanvasText(C, Set->Name, W / 2, H * 0.5f, H * 0.17f, FLinearColor::White, 0.5f);
			return;
		}
		if (A->Lot == INDEX_NONE) return;
		const FAuctionPlayer& P = FAuction::Player(A->Lot);
		if (A->Phase == EAuctionPhase::Hammer)
		{
			CanvasText(C, bSold ? TEXT("SOLD") : TEXT("UNSOLD"), W / 2, H * 0.2f, H * 0.12f, bSold ? Gold : FLinearColor(0.9f, 0.3f, 0.3f), 0.5f);
			CanvasText(C, P.Name.ToUpper(), W / 2, H * 0.44f, H * 0.16f, FLinearColor::White, 0.5f);
			if (bSold)
			{
				CanvasText(C, Teams[A->LastSoldTo].Name.ToUpper(), W / 2, H * 0.66f, H * 0.08f, FLinearColor::White, 0.5f);
				CanvasText(C, AuctionRules::Money(A->Price), W / 2, H * 0.83f, H * 0.12f, Gold, 0.5f);
			}
			return;
		}
		// The player card: set and lot, name, details, base price and the live bid with the holder's colour.
		CanvasText(C, FString::Printf(TEXT("%s  ·  LOT %d"), Set ? *Set->Name : TEXT(""), A->LotsHeld), W * 0.05f, H * 0.12f, H * 0.055f, Gold);
		CanvasText(C, P.IsOverseas() ? TEXT("OVERSEAS") : P.bCapped ? TEXT("CAPPED") : TEXT("UNCAPPED"), W * 0.95f, H * 0.12f, H * 0.055f, Gold, 1.f);
		CanvasText(C, P.Name.ToUpper(), W * 0.05f, H * 0.3f, H * 0.16f, FLinearColor::White);
		FString Detail = FString::Printf(TEXT("%s  ·  %s  ·  AGE %d"), *FString(AuctionRules::RoleName(P.Role)).ToUpper(), *P.Country.ToUpper(), P.Age);
		if (!P.Team2026.IsEmpty()) Detail += FString::Printf(TEXT("  ·  IPL 2026: %s"), *P.Team2026);
		CanvasText(C, Detail, W * 0.05f, H * 0.46f, H * 0.055f, FLinearColor(0.75f, 0.85f, 0.85f), 0.f, false);
		// Base price, left; current bid, right, on the holding side's colour.
		Tile(C, W * 0.05f, H * 0.58f, W * 0.3f, H * 0.3f, FLinearColor(0.01f, 0.06f, 0.06f));
		CanvasText(C, TEXT("BASE PRICE"), W * 0.2f, H * 0.65f, H * 0.05f, Gold, 0.5f);
		CanvasText(C, AuctionRules::Money(P.Base), W * 0.2f, H * 0.78f, H * 0.1f, FLinearColor::White, 0.5f);
		const bool bHeld = Teams.IsValidIndex(A->Holder) && A->Price > 0;
		Tile(C, W * 0.4f, H * 0.58f, W * 0.55f, H * 0.3f, bHeld ? Teams[A->Holder].Primary * 0.8f : FLinearColor(0.01f, 0.06f, 0.06f));
		CanvasText(C, bHeld ? FString::Printf(TEXT("CURRENT BID  ·  %s"), *Teams[A->Holder].Short) : FString(TEXT("AWAITING OPENING BID")), W * 0.675f, H * 0.65f, H * 0.05f, Gold, 0.5f);
		CanvasText(C, bHeld ? AuctionRules::Money(A->Price) : TEXT("—"), W * 0.675f, H * 0.78f, H * 0.12f, FLinearColor::White, 0.5f);
		if (Teams.IsValidIndex(A->RtmTeam))
			CanvasText(C, FString::Printf(TEXT("RTM  ·  %s"), *Teams[A->RtmTeam].Short), W * 0.95f, H * 0.3f, H * 0.07f, Gold, 1.f);
	});
}

void AAuctionRoom::DrawSideScreens(const FAuction* A)
{
	const TArray<FAuctionFranchise>& Teams = AuctionData::Franchises();
	Draw(this, SideTarget, Ink, [&](UCanvas* C, float W, float H)
	{
		CanvasText(C, TEXT("TEAMS"), W * 0.08f, H * 0.07f, H * 0.045f, Gold);
		CanvasText(C, TEXT("PURSE REMAINING"), W * 0.62f, H * 0.07f, H * 0.045f, Gold, 1.f);
		CanvasText(C, TEXT("PLAYERS"), W * 0.8f, H * 0.07f, H * 0.045f, Gold, 0.5f);
		CanvasText(C, TEXT("OS"), W * 0.93f, H * 0.07f, H * 0.045f, Gold, 0.5f);
		Tile(C, W * 0.04f, H * 0.11f, W * 0.92f, 3, Gold);
		for (int32 T = 0; T < Teams.Num() && T < 10; ++T)
		{
			const float Y = H * (0.18f + 0.082f * T);
			Tile(C, W * 0.04f, Y - H * 0.032f, W * 0.018f, H * 0.064f, Teams[T].Primary);
			CanvasText(C, Teams[T].Short, W * 0.08f, Y, H * 0.05f, FLinearColor::White);
			const FAuctionTeam* Team = A && A->Teams.IsValidIndex(T) ? &A->Teams[T] : nullptr;
			CanvasText(C, AuctionRules::Money(Team ? Team->Purse : AuctionRules::Purse), W * 0.62f, Y, H * 0.05f, FLinearColor::White, 1.f);
			CanvasText(C, FString::Printf(TEXT("%d/%d"), Team ? Team->Squad.Num() : 0, AuctionRules::SquadMax), W * 0.8f, Y, H * 0.05f, FLinearColor::White, 0.5f);
			CanvasText(C, FString::Printf(TEXT("%d/%d"), Team ? Team->Overseas() : 0, AuctionRules::OverseasMax), W * 0.93f, Y, H * 0.05f, FLinearColor::White, 0.5f);
		}
	});
}

// ---- The director -----------------------------------------------------------------------------------------------

void AAuctionRoom::Cut(const FShot& S, bool bForce)
{
	// Broadcast cuts, but never so fast they strobe: a quiet cut waits until the last shot has had a moment.
	if (!bForce && Now() - ShotAt < 2.2) return;
	Shot = S;
	ShotAt = Now();
	bShowSplit = false;
}

void AAuctionRoom::ResetDirector()
{
	Split[0] = Split[1] = INDEX_NONE;
	bShowSplit = false;
	LastBidder = INDEX_NONE;
	ShotBeat = 0;
	SaleWideAt = -1.0;
	PointAt = GavelAt = -100.0;
	PointTeam = LookTeam = INDEX_NONE;
	AccentUntil = -100.0;
	for (FTableAct& A : Acts) A = FTableAct();
	Cut(Wide());
}

void AAuctionRoom::NextBiddingShot()
{
	if (bShowSplit || Now() - ShotAt < 2.2) return;
	switch (ShotBeat++ % 5)
	{
	case 0:
		Cut(LastBidder != INDEX_NONE ? OnTable(LastBidder) : Wide());
		break;
	case 1:
		if (Split[0] != INDEX_NONE && SplitCaptures[0])
		{
			bShowSplit = true;
			ShotAt = Now();
		}
		else Cut(OnAuctioneer());
		break;
	case 2:
		Cut(Wide());
		break;
	case 3:
		Cut(LastBidder == INDEX_NONE ? OnAuctioneer() :
			OnTable(Split[0] != INDEX_NONE ? (Split[0] == LastBidder ? Split[1] : Split[0]) : LastBidder));
		break;
	default:
		Cut(OnAuctioneer());
		break;
	}
}

AAuctionRoom::FShot AAuctionRoom::Wide() const
{
	return { FVector(31.f, 0.f, 13.f), FVector(4.f, 0.f, 1.5f), FVector(0.f, 0.08f, 0.f), 72.f };
}

AAuctionRoom::FShot AAuctionRoom::OnAuctioneer() const
{
	return { FVector(5.5f, -1.6f, StageHeight + 1.75f), AuctioneerHead() - FVector(0, 0, 0.12f), FVector(-0.06f, 0.03f, 0.f), 22.f };
}

AAuctionRoom::FShot AAuctionRoom::OnWall() const
{
	// The whole wall with the auctioneer small at her lectern under it, easing in from the centre aisle.
	return { FVector(9.f, 0.f, 2.2f), FVector(-6.5f, 0.f, StageHeight + 3.f), FVector(-0.3f, 0.f, 0.f), 62.f };
}

AAuctionRoom::FShot AAuctionRoom::OnTable(int32 Team) const
{
	if (Team < 0 || Team >= 10) return Wide();
	const FVector D = -TableFacing(Team), A = Across(D);
	const FVector Heads = 0.5f * (SeatAt(Team, 0) + SeatAt(Team, 1)) + Up * 1.1f;
	// Rear-row shots come from the aisle gap, clear of the front-row desks.
	const bool bRear = TableFront(Team).Z > 0.f;
	const FVector From = bRear ? Heads - D * 4.f - A * FMath::Sign(Heads.Y) * 3.f + Up * 0.3f
		: Heads - D * 5.8f + A * 0.9f + Up * 0.3f;
	return { From, Heads - Up * 0.12f, A * 0.05f, 38.f };
}

void AAuctionRoom::Present(const FAuction& A, const FAuctionEventRecord& E)
{
	const double T = Now();
	const bool bTeam = E.Team >= 0 && E.Team < 10;
	const bool bFirstBid = E.Type == EAuctionEvent::Bid && bTeam && LastBidder == INDEX_NONE;
	const int32 Duel[2] = { Split[0], Split[1] }; // the two tables of the last duel, before this event changes them
	// Keep the two most recent opposing bidders available for brief split-screen shots.
	if (E.Type == EAuctionEvent::Bid && bTeam)
	{
		if (LastBidder != INDEX_NONE && LastBidder != E.Team && SplitCaptures[0])
		{
			if (Split[0] == INDEX_NONE) ShotBeat = 1;
			Split[0] = FMath::Min(LastBidder, E.Team);
			Split[1] = FMath::Max(LastBidder, E.Team);
		}
		if (bFirstBid) ShotBeat = 1;
		LastBidder = E.Team;
		if (bFirstBid) Cut(OnTable(E.Team));
	}
	else if (E.Type != EAuctionEvent::Huddle && E.Type != EAuctionEvent::Out) Split[0] = Split[1] = INDEX_NONE;
	if (Split[0] == INDEX_NONE) bShowSplit = false;
	if (E.Type == EAuctionEvent::LotOpened)
	{
		LastBidder = INDEX_NONE;
		ShotBeat = 0;
		SaleWideAt = -1.0;
	}
	switch (E.Type)
	{
	case EAuctionEvent::SetOpened:
	case EAuctionEvent::Accelerated:
		DrawWall(&A, &E);
		Cut(Wide());
		break;
	case EAuctionEvent::LotOpened:
		DrawWall(&A, &E);
		Cut(OnWall());
		break;
	case EAuctionEvent::OpeningCall:
	case EAuctionEvent::GoingOnce:
	case EAuctionEvent::GoingTwice:
		Cut(OnAuctioneer());
		break;
	case EAuctionEvent::Bid:
	case EAuctionEvent::FinalRaise:
		DrawWall(&A, &E);
		if (!bTeam) break;
		Acts[E.Team].RaiseAt = T;
		PointAt = T;
		PointTeam = LookTeam = E.Team;
		if (E.Type == EAuctionEvent::FinalRaise) Cut(OnTable(E.Team));
		else if (!bFirstBid) NextBiddingShot();
		break;
	case EAuctionEvent::Huddle:
		if (bTeam) { Acts[E.Team].HuddleAt = T; Cut(OnTable(E.Team), false); }
		break;
	case EAuctionEvent::Out:
		if (bTeam) Acts[E.Team].OutAt = T;
		break;
	case EAuctionEvent::Timeout:
		// The table asked for a moment: its people put their heads together, on camera.
		if (bTeam) { Acts[E.Team].HuddleAt = T; LookTeam = E.Team; Cut(OnTable(E.Team)); }
		break;
	case EAuctionEvent::DayEnded:
	case EAuctionEvent::DayStarted:
		DrawWall(&A, &E);
		DrawSideScreens(&A);
		Cut(Wide());
		break;
	case EAuctionEvent::Sold:
		GavelAt = T;
		DrawWall(&A, &E);
		DrawSideScreens(&A);
		SaleWideAt = T + 2.0;
		// The side that lost the duel sinks back as the other table applauds.
		if (bTeam && Duel[0] != INDEX_NONE)
		{
			const int32 Loser = Duel[0] == E.Team ? Duel[1] : Duel[0];
			if (Loser >= 0 && Loser < 10 && Loser != E.Team) Acts[Loser].OutAt = T + 0.2;
		}
		if (bTeam)
		{
			Acts[E.Team].CheerAt = T + 0.4;
			AccentGoal = AuctionData::Franchises()[E.Team].Primary;
			AccentUntil = T + 5.0;
			Cut(OnTable(E.Team));
		}
		break;
	case EAuctionEvent::Unsold:
		GavelAt = T;
		DrawWall(&A, &E);
		SaleWideAt = T + 2.0;
		Cut(OnAuctioneer());
		break;
	case EAuctionEvent::RtmOffered:
	case EAuctionEvent::RtmUsed:
	case EAuctionEvent::RtmDeclined:
	case EAuctionEvent::RtmMatched:
	case EAuctionEvent::RtmNotMatched:
		DrawWall(&A, &E);
		if (bTeam) { LookTeam = E.Team; Acts[E.Team].HuddleAt = E.Type == EAuctionEvent::RtmOffered ? T : Acts[E.Team].HuddleAt; Cut(OnTable(E.Team)); }
		break;
	case EAuctionEvent::Finished:
		DrawWall(&A, &E);
		DrawSideScreens(&A);
		Cut(Wide());
		break;
	default:
		break;
	}
}

// ---- People -----------------------------------------------------------------------------------------------------

void AAuctionRoom::PoseStaff(FPerson& P, double T)
{
	UCricketAnimInstance* Anim = Cast<UCricketAnimInstance>(P.Body->GetAnimInstance());
	if (!Anim) return;
	FCricketBodyPose& B = Anim->Pose;
	B.ClearActions();
	const FVector F = P.Facing, R = Across(F), Floor = P.Floor;
	const FTableAct& Act = Acts[P.Team];
	// Seated: the pelvis dropped onto the chair, feet forward, leaning on the table. ponytail: one pelvis drop for
	// every body; per-body heights if a tall or short MetaHuman sits visibly wrong.
	B.PelvisOffset = FVector(0.f, 0.f, -40.f);
	B.FootWeight = 1.f;
	B.Foot[0] = (Floor + F * 0.42f - R * 0.14f + Up * 0.09f) * Cm;
	B.Foot[1] = (Floor + F * 0.42f + R * 0.14f + Up * 0.09f) * Cm;
	B.ChestFacing = F;
	B.ChestBend = 8.f;
	B.LookWeight = 0.7f;
	B.LookAt = AuctioneerHead() * Cm;
	FVector Hand[2] = { Floor + F * 0.4f - R * 0.2f + Up * (TableTop + 0.04f), Floor + F * 0.4f + R * 0.2f + Up * (TableTop + 0.04f) };
	FVector Elbow[2] = { Floor - R * 0.6f + Up * 0.7f, Floor + R * 0.6f + Up * 0.7f };
	FVector Palm[2] = { -Up, -Up };
	FVector Finger[2] = { F, F };
	// Who they turn to: the one beside them (the middle seat, when there is one, confers with the paddle).
	const int32 Beside = SeatsPerTable > 2 ? (P.Seat == 2 ? 0 : 2) : 1 - P.Seat;
	const FVector Partner = SeatAt(P.Team, Beside) + Up * 1.15f;

	// A huddle: turned to the colleague, heads together.
	if (const float H = Envelope(float(T - Act.HuddleAt), 3.5f, 0.5f, 0.6f); H > 0.f)
	{
		B.ChestFacing = FMath::Lerp(F, (Partner - Floor).GetSafeNormal2D(), 0.6f * H).GetSafeNormal();
		B.ChestBend = 8.f + 12.f * H;
		B.LookAt = FMath::Lerp(AuctioneerHead(), Partner, H) * Cm;
	}
	// Out of the bidding: sits back, eyes on the laptop.
	if (const float O = Envelope(float(T - Act.OutAt), 2.5f, 0.4f, 0.6f); O > 0.f)
	{
		B.ChestBend = FMath::Lerp(B.ChestBend, -4.f, O);
		B.LookAt = FMath::Lerp(B.LookAt / Cm, Floor + F * 0.6f + Up * 0.8f, O) * Cm;
	}
	// The paddle up (seat 0 holds it), high and toward the lectern.
	const float Raise = P.Seat == 0 ? Envelope(float(T - Act.RaiseAt), 1.4f, 0.22f, 0.4f) : 0.f;
	Hand[1] = FMath::Lerp(Hand[1], Floor + F * 0.25f + R * 0.25f + Up * 1.5f, Raise);
	Elbow[1] = FMath::Lerp(Elbow[1], Floor + R * 0.7f + Up * 1.0f, Raise);
	Palm[1] = FMath::Lerp(Palm[1], -R, Raise).GetSafeNormal();
	Finger[1] = FMath::Lerp(Finger[1], Up, Raise).GetSafeNormal();
	// A buy: the table applauds.
	if (const float Cheer = Envelope(float(T - Act.CheerAt), 2.6f, 0.2f, 0.4f); Cheer > 0.f)
	{
		const FVector Clap = Floor + F * 0.36f + Up * 1.12f;
		const float Gap = 0.03f + 0.09f * FMath::Abs(FMath::Sin(float(T - Act.CheerAt) * 11.f));
		Hand[0] = FMath::Lerp(Hand[0], Clap - R * Gap, Cheer);
		Hand[1] = FMath::Lerp(Hand[1], Clap + R * Gap, Cheer);
		Palm[0] = FMath::Lerp(Palm[0], R, Cheer).GetSafeNormal();
		Palm[1] = FMath::Lerp(Palm[1], -R, Cheer).GetSafeNormal();
		Finger[0] = FMath::Lerp(Finger[0], Up, Cheer).GetSafeNormal();
		Finger[1] = FMath::Lerp(Finger[1], Up, Cheer).GetSafeNormal();
		B.LookAt = FMath::Lerp(B.LookAt / Cm, Partner, Cheer) * Cm;
	}
	for (int32 S = 0; S < 2; ++S)
	{
		B.HandWeight[S] = 1.f;
		B.Hand[S] = Hand[S] * Cm;
		B.Elbow[S] = Elbow[S] * Cm;
		B.PalmFacing[S] = Palm[S];
		B.FingerFacing[S] = Finger[S];
	}
	// The paddle: in the raised hand, face to the lectern; otherwise flat on the table by the hand.
	if (P.Seat == 0 && Paddles.IsValidIndex(P.Team))
	{
		if (Raise > 0.05f)
			Paddles[P.Team]->SetWorldLocationAndRotation(P.Body->GetSocketLocation(TEXT("hand_r")) - Up * 5.f, FRotationMatrix::MakeFromXZ(F, Up).Rotator());
		else
			Paddles[P.Team]->SetWorldLocationAndRotation((Floor + F * 0.45f + R * 0.42f + Up * (TableTop + 0.012f)) * Cm, FRotationMatrix::MakeFromXZ(Up, F).Rotator());
	}
}

void AAuctionRoom::PoseAuctioneer(FPerson& P, double T)
{
	UCricketAnimInstance* Anim = Cast<UCricketAnimInstance>(P.Body->GetAnimInstance());
	if (!Anim) return;
	FCricketBodyPose& B = Anim->Pose;
	B.ClearActions();
	const FVector F = P.Facing, R = Across(F), Floor = P.Floor;
	const FVector Look = LookTeam >= 0 ? 0.5f * (SeatAt(LookTeam, 0) + SeatAt(LookTeam, 1)) + Up * 1.1f : FVector(12.f, 0.f, 1.1f);
	B.LookWeight = 0.85f;
	B.LookAt = Look * Cm;
	B.ChestFacing = FMath::Lerp(F, (Look - Floor).GetSafeNormal2D(), 0.35f).GetSafeNormal();
	const float Top = StageHeight + 1.14f;
	FVector Hand[2] = { FVector(-0.22f, -0.22f, Top), FVector(-0.22f, 0.22f, Top) };
	FVector Elbow[2] = { Floor - R * 0.5f + Up * 0.9f, Floor + R * 0.5f + Up * 0.9f };
	FVector Finger[2] = { F, F };
	// Pointing to the paddle, with the hand on that side.
	if (const float Pt = Envelope(float(T - PointAt), 1.1f, 0.2f, 0.35f); Pt > 0.f && PointTeam >= 0)
	{
		const FVector Target = 0.5f * (SeatAt(PointTeam, 0) + SeatAt(PointTeam, 1)) + Up * 1.3f;
		const int32 S = FVector::DotProduct(Target - Floor, R) < 0.f ? 0 : 1;
		const FVector Shoulder = Floor + R * (S == 0 ? -0.2f : 0.2f) + Up * 1.42f;
		Hand[S] = FMath::Lerp(Hand[S], Shoulder + (Target - Shoulder).GetSafeNormal() * 0.58f, Pt);
		Elbow[S] = FMath::Lerp(Elbow[S], Shoulder + R * (S == 0 ? -0.5f : 0.5f) - Up * 0.2f, Pt);
		Finger[S] = FMath::Lerp(Finger[S], (Target - Shoulder).GetSafeNormal(), Pt).GetSafeNormal();
	}
	// The gavel: raised, brought down on the block, lifted away.
	const float G = float(T - GavelAt);
	const float Lift = G < 0.f || G > 1.f ? 0.f : G < 0.35f ? G / 0.35f : G < 0.45f ? 1.f - (G - 0.35f) / 0.1f : 0.f;
	const float Hold = Envelope(G, 1.f, 0.2f, 0.3f);
	Hand[1] = FMath::Lerp(Hand[1], FVector(-0.1f, 0.25f, Top + 0.02f + 0.3f * Lift), Hold);
	Finger[1] = FMath::Lerp(Finger[1], Up, Hold).GetSafeNormal();
	for (int32 S = 0; S < 2; ++S)
	{
		B.HandWeight[S] = 1.f;
		B.Hand[S] = Hand[S] * Cm;
		B.Elbow[S] = Elbow[S] * Cm;
		B.PalmFacing[S] = S == 1 ? FMath::Lerp(-Up, -R, Hold).GetSafeNormal() : -Up;
		B.FingerFacing[S] = Finger[S];
	}
	if (Gavel)
	{
		if (Hold > 0.05f) Gavel->SetWorldLocationAndRotation(P.Body->GetSocketLocation(TEXT("hand_r")), FRotator(0.f, 180.f, 0.f));
		else Gavel->SetWorldLocationAndRotation(FVector(-0.05f, 0.3f, Top) * Cm + Up * 3.f, FRotator(0.f, 160.f, 0.f));
	}
}

void AAuctionRoom::Update(const FAuction* A, float Dt, int32 HumanTeam)
{
	const double T = Now();
	// The arches and the wall's light: teal and gold at rest, the buyer's colour after a sale.
	const FLinearColor Goal = T < AccentUntil ? AccentGoal : FLinearColor::LerpUsingHSV(Teal, Gold, 0.5f + 0.5f * FMath::Sin(float(T) * 0.3f)) * 0.8f;
	Accent = FMath::Lerp(Accent, Goal, 1.f - FMath::Exp(-4.f * Dt));
	if (AccentTarget) UKismetRenderingLibrary::ClearRenderTarget2D(this, AccentTarget, Accent);
	if (WallLight) WallLight->SetLightColor(Accent);
	if (T >= ScreensAt)
	{
		ScreensAt = T + 0.5;
		DrawWall(A, nullptr);
		DrawSideScreens(A);
		if (SkirtDraws++ < 2) for (int32 Team = 0; Team < SkirtTargets.Num(); ++Team) DrawSkirt(Team);
	}

	if (SaleWideAt > 0.0 && T >= SaleWideAt)
	{
		SaleWideAt = -1.0;
		Cut(Wide());
	}
	else if (A && A->Phase == EAuctionPhase::Bidding)
	{
		if (bShowSplit && T - ShotAt >= 2.8) Cut(OnTable(LastBidder));
		else if (!bShowSplit && T - ShotAt >= 3.8) NextBiddingShot();
	}

	for (FPerson& P : People)
	{
		if (!P.Body) continue;
		if (P.Team == INDEX_NONE) PoseAuctioneer(P, T);
		else PoseStaff(P, T);
	}

	for (int32 I = 0; I < 2; ++I)
	{
		if (!SplitCaptures[I]) continue;
		SplitCaptures[I]->bCaptureEveryFrame = IsSplitShot();
		if (!IsSplitShot()) continue;
		const FShot S = OnTable(Split[I]);
		SplitCaptures[I]->SetWorldLocationAndRotation(S.From * Cm, (S.To - S.From).Rotation());
		SplitCaptures[I]->FOVAngle = S.Fov * 0.9f; // tighter than the full-screen cut: the inset is small
	}

	if (Camera)
	{
		const float Since = float(T - ShotAt);
		const FVector From = Shot.From + Shot.Drift * Since;
		Camera->SetActorLocationAndRotation(From * Cm, (Shot.To - From).Rotation());
		Camera->GetCameraComponent()->SetFieldOfView(Shot.Fov);
	}
}
