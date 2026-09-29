// Presentation automation tests: synthesised sound cues, text commentary and ball readability.

#include "Misc/AutomationTest.h"
#include "CricketAI.h"
#include "CricketAudio.h"
#include "CricketAudioDirector.h"
#include "CricketCommentary.h"
#include "CricketCommentaryDirector.h"
#include "SuperOverGameMode.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SkyLightComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/Scene.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CricketPresentationTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	double Energy(const TArray<int16>& S, int32 From, int32 To)
	{
		double E = 0.0;
		for (int32 I = From; I < To; ++I) E += double(S[I]) * S[I];
		return E;
	}

	void Bowl(FSuperOverMatch& M, const FDeliveryOutcome& O)
	{
		TArray<ECricketEvent> Ev;
		M.BeginDelivery();
		M.CompleteDelivery(O, Ev);
	}

	FDeliveryOutcome Hit(int32 Runs, int32 Boundary = 0)
	{
		FDeliveryOutcome O;
		O.bBatContact = true;
		O.RunsRun = Runs;
		O.Boundary = Boundary;
		return O;
	}

	FDeliveryResult Stroke(EShotType Shot, const FVector& ExitVel)
	{
		FDeliveryResult R;
		R.Contact.Shot = Shot;
		R.Contact.Zone = EContactZone::Middle;
		R.Contact.ExitVel = ExitVel;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioCueSynthesis, "CRICKET26.Audio.CueSynthesis", CricketPresentationTests::Flags)
bool FAudioCueSynthesis::RunTest(const FString&)
{
	using namespace CricketAudio;
	using CricketPresentationTests::Energy;
	for (const ECue Cue : { ECue::BatCrack, ECue::EdgeTick, ECue::Bounce, ECue::Stumps })
	{
		const TArray<int16> S = Synthesize(Cue);
		const FString Name = FString::Printf(TEXT("cue %d"), int32(Cue));
		if (!TestTrue(Name + TEXT(" has samples"), S.Num() > SampleRate / 50)) continue;
		int32 Peak = 0;
		for (int16 V : S) Peak = FMath::Max(Peak, FMath::Abs(int32(V)));
		TestTrue(Name + TEXT(" is audible without clipping"), Peak > 32767 * 0.4 && Peak <= 32767);
		// A strike: the sound is front-loaded and has died away by the end.
		const int32 Fifth = S.Num() / 5;
		TestTrue(Name + TEXT(" decays"), Energy(S, 0, Fifth) > 20.0 * Energy(S, S.Num() - Fifth, S.Num()));
		TestTrue(Name + TEXT(" is deterministic"), Synthesize(Cue) == S);
	}
	TestTrue(TEXT("bat crack is louder than an edge"), Energy(Synthesize(ECue::BatCrack), 0, SampleRate / 50) > Energy(Synthesize(ECue::EdgeTick), 0, SampleRate / 50));

	// The crowd bed loops without a click: the jump from the last sample back to the first is an ordinary
	// step, no bigger than 99% of the steps inside the loop.
	const TArray<int16> Crowd = Synthesize(ECue::Crowd);
	TestEqual(TEXT("crowd loop is 4 s"), Crowd.Num(), SampleRate * 4);
	TArray<int32> Steps;
	for (int32 I = 1; I < Crowd.Num(); ++I) Steps.Add(FMath::Abs(Crowd[I] - Crowd[I - 1]));
	Steps.Sort();
	const int32 Seam = FMath::Abs(Crowd[0] - Crowd.Last()), P99 = Steps[Steps.Num() * 99 / 100];
	AddInfo(FString::Printf(TEXT("crowd seam %d, 99th percentile step %d"), Seam, P99));
	TestTrue(TEXT("crowd loop seam is smooth"), Seam <= P99);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommentaryLines, "CRICKET26.Commentary.Lines", CricketPresentationTests::Flags)
bool FCommentaryLines::RunTest(const FString&)
{
	using namespace CricketCommentary;
	using namespace CricketPresentationTests;

	// Regions for a right-hander (off side +Y) and the mirror for a left-hander.
	TestEqual(TEXT("straight off side"), Region(FVector(1.f, 0.05f, 0.f), 1.f), FString(TEXT("long-off")));
	TestEqual(TEXT("square off side"), Region(FVector(0.f, 1.f, 0.f), 1.f), FString(TEXT("point")));
	TestEqual(TEXT("square leg side"), Region(FVector(0.f, -1.f, 0.f), 1.f), FString(TEXT("square leg")));
	TestEqual(TEXT("fine leg"), Region(FVector(-1.f, -0.2f, 0.f), 1.f), FString(TEXT("fine leg")));
	TestEqual(TEXT("left-hander mirrored"), Region(FVector(0.f, 1.f, 0.f), -1.f), FString(TEXT("square leg")));

	const FNames N{ TEXT("Asha"), TEXT("Bina"), TEXT("Home") };
	FSuperOverMatch First;
	First.Start(0);
	Bowl(First, Hit(0));
	const FDeliveryResult Six = Stroke(EShotType::Loft, FVector(20.f, -20.f, 15.f));
	const FString SixLine = Describe(Six, Hit(0, 6), First, N, 1.f, 0);
	AddInfo(SixLine);
	TestTrue(TEXT("six says six"), SixLine.Contains(TEXT("six")));
	TestTrue(TEXT("six names where it went"), SixLine.Contains(TEXT("midwicket")));
	TestNotEqual(TEXT("variants differ"), Describe(Six, Hit(0, 6), First, N, 1.f, 1), SixLine);
	TestTrue(TEXT("first innings: no chase line"), !SixLine.Contains(TEXT("needed")));

	// Every dismissal names the batter who is out.
	for (const EDismissal D : { EDismissal::Bowled, EDismissal::Caught, EDismissal::LBW, EDismissal::Stumped, EDismissal::HitWicket })
	{
		FDeliveryOutcome O;
		O.Dismissal = D;
		const FString Line = Describe(FDeliveryResult(), O, First, N, 1.f, 0);
		TestTrue(FString::Printf(TEXT("dismissal %d names the striker: %s"), int32(D), *Line), Line.Contains(N.Striker));
	}
	FDeliveryOutcome RunOut = Hit(0);
	RunOut.Dismissal = EDismissal::RunOut;
	RunOut.bRunOutStriker = false;
	const FString RunOutLine = Describe(Stroke(EShotType::Drive, FVector(10.f, 5.f, 0.f)), RunOut, First, N, 1.f, 0);
	TestTrue(TEXT("run out names the non-striker when they are out"), RunOutLine.Contains(N.NonStriker) && !RunOutLine.Contains(N.Striker));

	FDeliveryOutcome NoBall = Hit(1);
	NoBall.bNoBall = true;
	TestTrue(TEXT("no ball called"), Describe(Stroke(EShotType::Drive, FVector(10.f, 5.f, 0.f)), NoBall, First, N, 1.f, 0).StartsWith(TEXT("No ball!")));
	FDeliveryResult Dropped = Stroke(EShotType::Loft, FVector(10.f, 5.f, 8.f));
	Dropped.Fielding.bCatchChance = true;
	TestTrue(TEXT("a dropped catch is called"), Describe(Dropped, Hit(1), First, N, 1.f, 0).Contains(TEXT("Put down")));

	// The chase: the requirement after each ball, and the winning hit.
	FSuperOverMatch Chase;
	Chase.Start(0);
	Bowl(Chase, Hit(0, 4));
	for (int32 I = 0; I < 5; ++I) Bowl(Chase, Hit(0));
	const FString TargetLine = Describe(FDeliveryResult(), Hit(0), Chase, N, 1.f, 0);
	TestTrue(TEXT("target announced at the break: ") + TargetLine, TargetLine.Contains(TEXT("target of 5")));
	Chase.StartSecondInnings();
	Bowl(Chase, Hit(0, 4));
	const FString Need = Describe(Stroke(EShotType::Cut, FVector(5.f, 20.f, 0.f)), Hit(0, 4), Chase, N, 1.f, 0);
	TestTrue(TEXT("requirement after a four: ") + Need, Need.Contains(TEXT("1 needed from 5")));
	Bowl(Chase, Hit(1));
	const FString Won = Describe(Stroke(EShotType::Drive, FVector(10.f, 5.f, 0.f)), Hit(1), Chase, N, 1.f, 0);
	TestTrue(TEXT("winning run called: ") + Won, Won.Contains(TEXT("wins it for Home")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBallReadability, "CRICKET26.Presentation.BallScale", CricketPresentationTests::Flags)
bool FBallReadability::RunTest(const FString&)
{
	const float Aspect = 16.f / 9.f;
	auto Scale = [&](float Dist, float Fov) { return ASuperOverGameMode::BallDisplayScale(Dist, Fov, Aspect); };
	// Share of the screen height the drawn ball covers.
	auto Screen = [&](float Dist, float Fov)
	{
		const float ViewHeight = 2.f * Dist * FMath::Tan(FMath::DegreesToRadians(Fov) * 0.5f) / Aspect;
		return 2.f * CricketGeo::BallRadius * Scale(Dist, Fov) / ViewHeight;
	};
	// The broadcast delivery view (8 degrees from behind the bowler) shows the real ball, at both ends.
	TestEqual(TEXT("true size at the bowler's end"), Scale(62.f, 8.f), 1.f);
	TestEqual(TEXT("true size at the batter's end"), Scale(82.f, 8.f), 1.f);
	TestEqual(TEXT("true size close up"), Scale(3.f, 35.f), 1.f);
	// On the wide follow camera the ball is held at the minimum readable size, not blown up further.
	const float Far = Scale(60.f, 42.f);
	AddInfo(FString::Printf(TEXT("follow camera at 60 m: %.2fx"), Far));
	TestTrue(TEXT("enlarged on a wide shot"), Far > 1.f && Far < ASuperOverGameMode::MaxBallScale);
	TestTrue(TEXT("held at the minimum screen size"), FMath::IsNearlyEqual(Screen(60.f, 42.f), ASuperOverGameMode::MinBallScreen, 1e-4f));
	TestEqual(TEXT("capped far away"), Scale(500.f, 42.f), ASuperOverGameMode::MaxBallScale);
	TestTrue(TEXT("never smaller when further"), Scale(40.f, 42.f) <= Scale(50.f, 42.f) && Scale(50.f, 42.f) <= Scale(80.f, 42.f));
	TestEqual(TEXT("degenerate view"), Scale(0.f, 42.f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunLightsFaces, "CRICKET26.Presentation.SunLightsFaces", CricketPresentationTests::Flags)
bool FSunLightsFaces::RunTest(const FString&)
{
	// The striker faces the bowler (+X) and the delivery camera looks back from behind the bowler, so the sun's
	// light has to travel toward -X to fall on the faces the camera sees. It used to travel toward +X, which
	// lit only the players' backs and left every face black.
	// The spawned sun is checked, not the intended rotation: the light's component adds its own -46 degree
	// pitch to a spawn rotation, which once put the sun straight overhead so nothing cast a visible shadow.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	const FVector Light = ASuperOverGameMode::SpawnSun(World)->GetComponent()->GetDirection();
	World->DestroyWorld(false);
	AddInfo(FString::Printf(TEXT("sun light travels along %s"), *Light.ToString()));
	TestTrue(TEXT("light falls on the striker's front"), FVector::DotProduct(Light, FVector::ForwardVector) < -0.5f);
	TestTrue(TEXT("from one side, so faces keep some shape"), FMath::Abs(Light.Y) > 0.2f);
	TestTrue(TEXT("low enough to cast shadows the camera sees"), FMath::IsNearlyEqual(-Light.Z, FMath::Sin(FMath::DegreesToRadians(42.f)), 0.02f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkyLightsShade, "CRICKET26.Presentation.SkyLightsShade", CricketPresentationTests::Flags)
bool FSkyLightsShade::RunTest(const FString&)
{
	// A real-time sky capture lights nothing without Lumen: below the Epic tier every shadow and every player's shaded
	// side went black. The sky has to be captured once, on every tier.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	const USkyLightComponent* Sky = ASuperOverGameMode::SpawnSky(World, false)->GetLightComponent();
	TestFalse(TEXT("not captured in real time"), Sky->bRealTimeCapture);
	TestEqual(TEXT("movable"), Sky->Mobility.GetValue(), EComponentMobility::Movable);
	TestTrue(TEXT("lights the shade"), Sky->Intensity > 1.f);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFacePremiumFill, "CRICKET26.Presentation.FacePremiumFill", CricketPresentationTests::Flags)
bool FFacePremiumFill::RunTest(const FString&)
{
	// Premium faces: the sky's upward bounce must stay warm neutral, never green-dominant.
	// A green lower hemisphere dyed eye whites and hat-brim undersides green in shade.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	const USkyLightComponent* Sky = ASuperOverGameMode::SpawnSky(World, false)->GetLightComponent();
	const FLinearColor Fill = Sky->LowerHemisphereColor;
	AddInfo(FString::Printf(TEXT("day fill %s"), *Fill.ToString()));
	TestTrue(TEXT("fill lights the shade"), Sky->Intensity > 1.f);
	TestFalse(TEXT("fill is green-dominant (dyes eyes)"), Fill.G > Fill.R);
	TestTrue(TEXT("fill stays warm"), Fill.R >= Fill.B * 0.9f);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrouserPiping, "CRICKET26.Presentation.TrouserPiping", CricketPresentationTests::Flags)
bool FTrouserPiping::RunTest(const FString&)
{
	// The only free trousers are jeans: a player's pair must lose the leather back-pocket label and gain piping in the
	// team's second colour, while the auction staff's jeans stay jeans.
	UMaterialInterface* Jeans = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/MetaHumans/MH_Home_Opener/Clothing/MI_WI_OA_Jeans_slm_M_btm_jeans_slm_custom.MI_WI_OA_Jeans_slm_M_btm_jeans_slm_custom"));
	if (!Jeans) { AddInfo(TEXT("No built jeans; skipped")); return true; }
	UTexture* Label = nullptr;
	Jeans->GetTextureParameterValue(TEXT("Mask"), Label);
	const FLinearColor Accent(1.f, 0.7f, 0.1f);
	UMaterialInstanceDynamic* Player = UMaterialInstanceDynamic::Create(Jeans, nullptr);
	ASuperOverGameMode::TintOutfit(Player, FLinearColor::Blue, Accent);
	UTexture* Mask = nullptr;
	Player->GetTextureParameterValue(TEXT("Mask"), Mask);
	TestTrue(TEXT("player's trousers take the piping mask"), Mask && Mask->GetName() == TEXT("T_TrouserMask"));
	TestEqual(TEXT("piping in the accent"), Player->K2_GetVectorParameterValue(TEXT("Leather Tint")), Accent);
	UMaterialInstanceDynamic* Staff = UMaterialInstanceDynamic::Create(Jeans, nullptr);
	ASuperOverGameMode::TintOutfit(Staff, FLinearColor::Blue);
	Staff->GetTextureParameterValue(TEXT("Mask"), Mask);
	TestEqual(TEXT("staff jeans keep their mask"), Mask, Label);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHairStaysOn, "CRICKET26.Presentation.HairStaysOn", CricketPresentationTests::Flags)
bool FHairStaysOn::RunTest(const FString&)
{
	// The mobile guard once switched r.HairStrands.Enable off to drop strands, but that switch is the whole groom
	// system: every player lost hair, eyebrows, lashes and beard. Only strands may go.
	auto Get = [](const TCHAR* Name) { const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name); return V ? V->GetInt() : -1; };
	ASuperOverGameMode::ApplyHairQuality();
	TestEqual(TEXT("groom system on"), Get(TEXT("r.HairStrands.Enable")), 1);
	TestEqual(TEXT("cards on"), Get(TEXT("r.HairStrands.Cards")), 1);
	TestEqual(TEXT("helmet meshes on"), Get(TEXT("r.HairStrands.Meshes")), 1);
	TestEqual(TEXT("strands off"), Get(TEXT("r.HairStrands.Strands")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAmbientOcclusionHeadScale, "CRICKET26.Presentation.AmbientOcclusionHeadScale", CricketPresentationTests::Flags)
bool FAmbientOcclusionHeadScale::RunTest(const FString&)
{
	// A 120 cm radius took the head and hat brim as occluders over the whole face: every shaded face went black
	// below Quality 2. The radius must stay under a head's height so only contact creases darken.
	FPostProcessSettings Settings;
	ASuperOverGameMode::SetAmbientOcclusion(Settings);
	TestTrue(TEXT("radius overridden"), Settings.bOverride_AmbientOcclusionRadius);
	TestTrue(TEXT("radius at contact scale"), Settings.AmbientOcclusionRadius <= 40.f);
	TestTrue(TEXT("AO still on"), Settings.AmbientOcclusionIntensity > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigureRoleGear, "CRICKET26.Presentation.FigureRoleGear", CricketPresentationTests::Flags)
bool FFigureRoleGear::RunTest(const FString&)
{
	// The self-check fails loudly when role gear lands on the wrong figure or a face goes missing.
	USkeletalMesh* HatMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/MetaHumans/MH_Umpire_1/Kit/SKM_MH_Umpire_1_Hat.SKM_MH_Umpire_1_Hat"));
	if (!HatMesh) { AddInfo(TEXT("No built umpire hat; skipped")); return true; }
	AActor* Player = NewObject<AActor>();
	NewObject<USkeletalMeshComponent>(Player, TEXT("Hat"))->SetSkeletalMeshAsset(HatMesh);
	using ERole = ASuperOverGameMode::EFigureRole;
	auto Has = [](const TArray<FString>& Problems, const TCHAR* Text) { return Problems.ContainsByPredicate([Text](const FString& P) { return P.Contains(Text); }); };
	const TArray<FString> Fielder = ASuperOverGameMode::FigureProblems(Player, ERole::Other);
	TestTrue(TEXT("hat on a fielder is flagged"), Has(Fielder, TEXT("wears Hat")));
	TestTrue(TEXT("missing face is flagged"), Has(Fielder, TEXT("no face")));
	TestTrue(TEXT("missing body is flagged"), Has(Fielder, TEXT("no body")));
	const TArray<FString> Umpire = ASuperOverGameMode::FigureProblems(Player, ERole::Umpire);
	TestFalse(TEXT("hat on an umpire is fine"), Has(Umpire, TEXT("Hat")));
	TestTrue(TEXT("batter without pads is flagged"), Has(ASuperOverGameMode::FigureProblems(Player, ERole::Batter), TEXT("missing its Gear")));
	// A face left to tick only when seen draws stale bones on the first frame after a cut to it.
	USkeletalMeshComponent* Face = NewObject<USkeletalMeshComponent>(Player, TEXT("Face"));
	Face->SetSkeletalMeshAsset(HatMesh);
	Face->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	TestTrue(TEXT("face ticking only when seen is flagged"), Has(ASuperOverGameMode::FigureProblems(Player, ERole::Umpire), TEXT("stale")));
	Face->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	TestFalse(TEXT("face refreshed off screen is fine"), Has(ASuperOverGameMode::FigureProblems(Player, ERole::Umpire), TEXT("stale")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTimingGrades, "CRICKET26.Presentation.TimingGrades", CricketPresentationTests::Flags)
bool FTimingGrades::RunTest(const FString&)
{
	using CricketDelivery::TimingName;
	TestEqual(TEXT("on time"), TimingName(0.f), FString(TEXT("PERFECT")));
	TestEqual(TEXT("15 ms late is still perfect"), TimingName(0.015f), FString(TEXT("PERFECT")));
	TestEqual(TEXT("30 ms early is good"), TimingName(-0.03f), FString(TEXT("GOOD")));
	TestEqual(TEXT("50 ms early"), TimingName(-0.05f), FString(TEXT("EARLY")));
	TestEqual(TEXT("50 ms late"), TimingName(0.05f), FString(TEXT("LATE")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioAssetValidation, "CRICKET26.Audio.AssetValidation", CricketPresentationTests::Flags)
bool FAudioAssetValidation::RunTest(const FString&)
{
	using namespace CricketAudio;
	// Every cue: non-empty, audible, never clipped, deterministic.
	for (int32 I = 0; I < int32(ECue::Count); ++I)
	{
		const TArray<int16> S = Synthesize(ECue(I));
		TestTrue(FString::Printf(TEXT("cue %d validates"), I), ValidateCue(S));
		TestTrue(FString::Printf(TEXT("cue %d deterministic"), I), Synthesize(ECue(I), 1) == S);
	}
	// Contact families are genuinely different cues, not one sample rescaled.
	TestNotEqual(TEXT("middle differs from toe"), Synthesize(ECue::BatMiddle), Synthesize(ECue::BatToe));
	TestNotEqual(TEXT("crack differs from middle"), Synthesize(ECue::BatCrack), Synthesize(ECue::BatMiddle));
	TestNotEqual(TEXT("keeper differs from catch"), Synthesize(ECue::KeeperGlove), Synthesize(ECue::CatchPop));
	// Selection is contact-driven.
	TestEqual(TEXT("perfect middle"), SelectBatCue(EContactZone::Middle, 0.95f), ECue::BatMiddle);
	TestEqual(TEXT("ordinary middle"), SelectBatCue(EContactZone::Middle, 0.5f), ECue::BatCrack);
	TestEqual(TEXT("toe-ender"), SelectBatCue(EContactZone::Toe, 0.3f), ECue::BatToe);
	TestEqual(TEXT("edge"), SelectBatCue(EContactZone::OutsideEdge, 0.4f), ECue::EdgeTick);
	TestTrue(TEXT("middle louder than toe"), BatVolume(EContactZone::Middle, 0.9f) > BatVolume(EContactZone::Toe, 0.3f));
	TestTrue(TEXT("pitch volume in range"), PitchVolume(80.f) >= 0.3f && PitchVolume(150.f) <= 0.65f && PitchVolume(140.f) > PitchVolume(90.f));
	// Mix buses are sane: commentary and impacts on top, ambience below, ducking slight.
	for (int32 I = 0; I < int32(EMixBus::Count); ++I)
		TestTrue(FString::Printf(TEXT("bus %d trim in range"), I), BusTrim(EMixBus(I)) > 0.f && BusTrim(EMixBus(I)) <= 1.f);
	TestEqual(TEXT("commentary never muted"), CommentaryDuck, 0.8f);
	// Commentary library metadata: ids unique, ranges valid, pools non-empty.
	TSet<FString> Ids;
	int32 Pools = 0;
	auto CheckPool = [&](const TArray<CricketCommentary::FLine>& Pool, const TCHAR* Name)
	{
		TestTrue(FString(Name) + TEXT(" non-empty"), Pool.Num() > 0);
		++Pools;
		for (const CricketCommentary::FLine& L : Pool)
		{
			TestFalse(FString(Name) + TEXT(" duplicate id ") + L.Meta.Id, Ids.Contains(L.Meta.Id));
			Ids.Add(L.Meta.Id);
			TestTrue(L.Meta.Id + TEXT(" excitement in range"), L.Meta.Excitement >= 0.f && L.Meta.Excitement <= 1.f);
			TestTrue(L.Meta.Id + TEXT(" priority in range"), L.Meta.Priority >= 0 && L.Meta.Priority <= 4);
			TestTrue(L.Meta.Id + TEXT(" cooldown positive"), L.Meta.CooldownBalls > 0);
			TestTrue(L.Meta.Id + TEXT(" has duration"), L.Meta.EstSeconds > 0.f);
		}
	};
	using namespace CricketCommentary;
	CheckPool(SixLines(), TEXT("six"));
	CheckPool(FourLines(), TEXT("four"));
	CheckPool(DotLines(), TEXT("dot"));
	CheckPool(SingleLines(), TEXT("single"));
	CheckPool(BowledLines(), TEXT("bowled"));
	CheckPool(CaughtLines(), TEXT("caught"));
	CheckPool(CaughtBehindLines(), TEXT("caught-behind"));
	CheckPool(LbwLines(), TEXT("lbw"));
	CheckPool(RunOutLines(), TEXT("run-out"));
	CheckPool(StumpedLines(), TEXT("stumped"));
	CheckPool(AnalysisLines(), TEXT("analysis"));
	CheckPool(ResultLines(), TEXT("result"));
	TestTrue(TEXT("coverage across pools"), Pools >= 12 && Ids.Num() >= 60);
	// Pronunciation: every default-squad name resolves; unknown names fall back (flagged, never guessed).
	for (const TCHAR* N : { TEXT("Opener"), TEXT("Finisher"), TEXT("Allrounder"), TEXT("Quick"), TEXT("Hitter"), TEXT("Anchor"), TEXT("Keeper-bat"), TEXT("Wrist spinner") })
		TestTrue(FString(TEXT("pronounced: ")) + N, HasPronunciation(N));
	TestFalse(TEXT("unknown flagged"), HasPronunciation(TEXT("Xyzzy Nobody")));
	TestEqual(TEXT("unknown falls back to display"), SpokenFor(TEXT("Xyzzy Nobody")), FString(TEXT("Xyzzy Nobody")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommentaryCorrectness, "CRICKET26.Commentary.Correctness", CricketPresentationTests::Flags)
bool FCommentaryCorrectness::RunTest(const FString&)
{
	using namespace CricketCommentary;
	using namespace CricketPresentationTests;
	const FNames N{ TEXT("Asha"), TEXT("Bina"), TEXT("Home") };
	FSuperOverMatch First;
	First.Start(0);
	Bowl(First, Hit(0));
	auto Says = [](const FString& Line, const TCHAR* W) { return Line.Contains(W, ESearchCase::IgnoreCase); };
	// Six is never called a four, four never a six, wickets never boundaries.
	FDeliveryResult Bat = Stroke(EShotType::Loft, FVector(20.f, 10.f, 15.f));
	Bat.Contact.Quality = 0.6f;
	const FString Six = Describe(Bat, Hit(0, 6), First, N, 1.f, 0);
	TestTrue(TEXT("six says six, not four: ") + Six, Says(Six, TEXT("six")) && !Says(Six, TEXT("four")));
	Bat.Contact.Zone = EContactZone::OuterHalf;
	const FString Four = Describe(Bat, Hit(0, 4), First, N, 1.f, 0);
	TestTrue(TEXT("four says four, not six: ") + Four, Says(Four, TEXT("four")) && !Says(Four, TEXT("six")));
	FDeliveryOutcome Bowled;
	Bowled.Dismissal = EDismissal::Bowled;
	const FString Out = Describe(FDeliveryResult(), Bowled, First, N, 1.f, 0);
	TestTrue(TEXT("bowled claims no boundary: ") + Out, !Says(Out, TEXT("six")) && !Says(Out, TEXT("four")));
	const FString Dot = Describe(Bat, Hit(0), First, N, 1.f, 3);
	TestTrue(TEXT("dot claims nothing: ") + Dot, !Says(Dot, TEXT("six")) && !Says(Dot, TEXT("four")) && !Says(Dot, TEXT("wicket")));
	// Edges only when the contact system saw one.
	FDeliveryResult Edge = Stroke(EShotType::Drive, FVector(10.f, 20.f, 1.f));
	Edge.Contact.Zone = EContactZone::OutsideEdge;
	const FString EdgeFour = Describe(Edge, Hit(0, 4), First, N, 1.f, 0);
	TestTrue(TEXT("edge four recognised: ") + EdgeFour, Says(EdgeFour, TEXT("edge")));
	TestTrue(TEXT("clean four claims no edge: ") + Four, !Says(Four, TEXT("edge")));
	// Caught behind sounds like caught behind, not like bowled; dives praised only when diving.
	FDeliveryOutcome Caught;
	Caught.Dismissal = EDismissal::Caught;
	Caught.bBatContact = true;
	FDeliveryResult Behind = Stroke(EShotType::Defend, FVector(0.f, 1.f, 1.f));
	Behind.Contact.Zone = EContactZone::OutsideEdge;
	Behind.Fielding.Action = EFieldAction::CatchKeeper;
	const FString CB = Describe(Behind, Caught, First, N, 1.f, 0);
	TestTrue(TEXT("caught behind: ") + CB, Says(CB, TEXT("keeper")) || Says(CB, TEXT("behind")));
	TestTrue(TEXT("regulation catch claims no dive"), !Says(Describe(Bat, Caught, First, N, 1.f, 0), TEXT("diving")) && !Says(Describe(Bat, Caught, First, N, 1.f, 0), TEXT("stretch")));
	FDeliveryResult Dive = Bat;
	Dive.Fielding.bDive = true;
	Dive.Fielding.bCaught = true;
	Dive.Fielding.bCatchChance = true;
	const FString DC = Describe(Dive, Caught, First, N, 1.f, 0);
	TestTrue(TEXT("diving catch praised: ") + DC, Says(DC, TEXT("diving")) || Says(DC, TEXT("stretch")));
	// Run-out margin: direct hit says so.
	FDeliveryOutcome RO = Hit(1);
	RO.Dismissal = EDismissal::RunOut;
	FDeliveryResult ROD = Stroke(EShotType::Drive, FVector(10.f, 5.f, 0.f));
	ROD.Running.bDirectHit = true;
	const FString DH = Describe(ROD, RO, First, N, 1.f, 0);
	TestTrue(TEXT("direct hit called: ") + DH, Says(DH, TEXT("direct hit")));
	// Bouncer beaten (authoritative) may say short; ordinary beaten may not invent deliveries.
	FDeliveryResult Short = FDeliveryResult();
	Short.bBouncer = true;
	Short.Shot.Shot = EShotType::Pull;
	TestTrue(TEXT("short ball named"), Says(Describe(Short, Hit(0), First, N, 1.f, 0), TEXT("short")));
	TestTrue(TEXT("no invented slower ball"), !Says(Describe(FDeliveryResult(), Hit(0), First, N, 1.f, 0), TEXT("slower")));
	// Extras and match facts.
	TestTrue(TEXT("overthrow"), Says(Describe(Bat, [] { FDeliveryOutcome O = Hit(1); O.bOverthrow = true; O.Boundary = 4; return O; }(), First, N, 1.f, 0), TEXT("overthrow")));
	FSuperOverMatch Chase;
	Chase.Start(0);
	Bowl(Chase, Hit(0, 6));
	for (int32 I = 0; I < 5; ++I) Bowl(Chase, Hit(0));
	Chase.StartSecondInnings();
	Bowl(Chase, Hit(1));
	for (int32 I = 0; I < 4; ++I) Bowl(Chase, Hit(0));
	const FString Last = Describe(Stroke(EShotType::Cut, FVector(5.f, 20.f, 0.f)), Hit(0), Chase, N, 1.f, 0);
	TestTrue(TEXT("final ball named: ") + Last, Last.Contains(TEXT("needed off the last ball")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommentaryRepetition, "CRICKET26.Commentary.Repetition", CricketPresentationTests::Flags)
bool FCommentaryRepetition::RunTest(const FString&)
{
	using namespace CricketCommentary;
	using namespace CricketCommentaryDirector;
	using namespace CricketPresentationTests;
	const FNames N{ TEXT("Asha"), TEXT("Bina"), TEXT("Home") };
	FSuperOverMatch M;
	M.Start(0);
	Bowl(M, Hit(0));
	FState S;
	S.Reset();
	FRandomStream Rng(1234);
	TArray<FString> Spoken;
	// Twelve sixes with real variety (middled / working / mistimed): broadcast must not repeat itself.
	for (int32 Ball = 0; Ball < 12; ++Ball)
	{
		FDeliveryResult R = Stroke(EShotType::Loft, FVector(20.f, float(Ball % 3 - 1) * 10.f, 15.f));
		R.Contact.Zone = Ball % 3 == 0 ? EContactZone::Middle : Ball % 3 == 1 ? EContactZone::OuterHalf : EContactZone::Toe;
		R.Contact.Quality = Ball % 3 == 0 ? 0.9f : Ball % 3 == 1 ? 0.6f : 0.2f;
		const FContext C = FContext::Build(R, Hit(0, 6), M, N, 1.f, EDeliveryType::Stock);
		const FSelection Sel = SelectLine(S, C, N, float(Ball) * 20.f, Ball, Rng);
		TestFalse(TEXT("sixes always speak"), Sel.bSilent);
		if (!Sel.bSilent) Spoken.Add(Sel.Text);
	}
	TestEqual(TEXT("all sixes spoken"), Spoken.Num(), 12);
	for (int32 I = 1; I < Spoken.Num(); ++I)
		TestNotEqual(FString::Printf(TEXT("no back-to-back repeat at %d"), I), Spoken[I], Spoken[I - 1]);
	TSet<FString> Distinct(Spoken);
	TestTrue(FString::Printf(TEXT("distinct lines %d/12"), Distinct.Num()), Distinct.Num() >= 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommentaryStaleInterrupt, "CRICKET26.Commentary.StaleInterrupt", CricketPresentationTests::Flags)
bool FCommentaryStaleInterrupt::RunTest(const FString&)
{
	using namespace CricketCommentary;
	using namespace CricketCommentaryDirector;
	using namespace CricketPresentationTests;
	const FNames N{ TEXT("Asha"), TEXT("Bina"), TEXT("Home") };
	FSuperOverMatch M;
	M.Start(0);
	Bowl(M, Hit(0));
	// Stale queue entries are dropped, never played late.
	FState S;
	S.Reset();
	S.Queue.bHas = true;
	S.Queue.BallIndex = 0;
	S.Queue.ExpiresAt = 1e9f;
	Update(S, 0.f, 2);
	TestFalse(TEXT("stale queue dropped"), S.Queue.bHas);
	// A wicket interrupts a still-speaking boundary call.
	FRandomStream Rng(7);
	FDeliveryResult Six = Stroke(EShotType::Loft, FVector(20.f, 10.f, 15.f));
	Six.Contact.Quality = 0.6f;
	Six.Contact.Zone = EContactZone::OuterHalf;
	SelectLine(S, FContext::Build(Six, Hit(0, 6), M, N, 1.f, EDeliveryType::Stock), N, 10.f, 0, Rng);
	TestTrue(TEXT("six speaking"), IsSpeaking(S, 11.f));
	FDeliveryOutcome W = Hit(0);
	W.Dismissal = EDismissal::Bowled;
	const FSelection Sel = SelectLine(S, FContext::Build(FDeliveryResult(), W, M, N, 1.f, EDeliveryType::Stock), N, 11.f, 1, Rng);
	TestTrue(TEXT("wicket interrupts"), Sel.bInterrupted);
	TestTrue(TEXT("wicket speaks"), !Sel.bSilent && Sel.Text.Contains(TEXT("Asha")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommentaryVoicing, "CRICKET26.Commentary.Voicing", CricketPresentationTests::Flags)
bool FCommentaryVoicing::RunTest(const FString&)
{
	using namespace CricketCommentary;
	using namespace CricketCommentaryDirector;
	using namespace CricketPresentationTests;
	// Clip keys match Scripts/audio/eleven.py: lower-case words, '_' between, apostrophes dropped.
	TestEqual(TEXT("key"), ClipKey(TEXT("It's all over! Home XI win.")), FString(TEXT("its_all_over_home_xi_win")));
	TestEqual(TEXT("key hyphen"), ClipKey(TEXT("Slog-swept past point, 7 needed from 3.")), FString(TEXT("slog_swept_past_point_7_needed_from_3")));
	// Multi-run lines end "...for ": the director must finish them, and keep the situation call separate.
	const FNames N{ TEXT("Asha"), TEXT("Bina"), TEXT("Home") };
	FSuperOverMatch Chase;
	Chase.Start(0);
	Bowl(Chase, Hit(0, 6));
	for (int32 I = 0; I < 5; ++I) Bowl(Chase, Hit(0));
	Chase.StartSecondInnings();
	Bowl(Chase, Hit(2));
	int32 Spoken = 0;
	for (int32 Seed = 0; Seed < 20; ++Seed)
	{
		FState S;
		S.Reset();
		FRandomStream Rng(Seed);
		const FContext C = FContext::Build(Stroke(EShotType::Drive, FVector(10.f, 8.f, 0.f)), Hit(2), Chase, N, 1.f, EDeliveryType::Stock);
		const FSelection Sel = SelectLine(S, C, N, 0.f, 0, Rng);
		if (Sel.bSilent) continue;
		++Spoken;
		TestTrue(TEXT("two runs finished: ") + Sel.Body, Sel.Body.EndsWith(TEXT(" two.")));
		TestTrue(TEXT("chase call split off: ") + Sel.Suffix, Sel.Suffix.Contains(TEXT("needed")));
		TestEqual(TEXT("caption is body then call"), Sel.Text, Sel.Body + TEXT(" ") + Sel.Suffix);
	}
	TestTrue(TEXT("some twos spoken"), Spoken > 0);
	// A defended total: the result call names the fielding side as the winner, once.
	{
		const FNames Def{ TEXT("Asha"), TEXT("Bina"), TEXT("Chasers"), TEXT("Defenders") };
		FSuperOverMatch M = Chase;
		for (int32 I = 0; I < 5 && M.Phase != EMatchPhase::MatchComplete; ++I) Bowl(M, Hit(0));
		TestEqual(TEXT("defence won"), M.Winner, M.BowlingTeam());
		FState S;
		S.Reset();
		FRandomStream Rng(3);
		const FSelection Sel = SelectLine(S, FContext::Build(FDeliveryResult(), Hit(0), M, Def, 1.f, EDeliveryType::Stock), Def, 0.f, 6, Rng);
		TestTrue(TEXT("winner named: ") + Sel.Text, Sel.Text.Contains(TEXT("Defenders")) && !Sel.Text.Contains(TEXT("Chasers")));
		TestTrue(TEXT("no second winner call: ") + Sel.Suffix, Sel.Suffix.IsEmpty());
	}
	// The analyst handoff plays once its slot opens, and only once.
	FState S;
	S.Reset();
	S.Queue.bHas = true;
	S.Queue.Text = TEXT("Pressure does strange things.");
	S.Queue.Line.Meta.EstSeconds = 2.f;
	S.Queue.Line.Meta.Id = TEXT("An_Pressure_01");
	S.Queue.AvailableAt = 5.f;
	S.Queue.ExpiresAt = 20.f;
	TestTrue(TEXT("not before its slot"), TakeHandoff(S, 4.f).IsEmpty());
	TestEqual(TEXT("handoff taken"), TakeHandoff(S, 5.f), FString(TEXT("Pressure does strange things.")));
	TestTrue(TEXT("analyst now speaking"), IsSpeaking(S, 6.f));
	TestTrue(TEXT("taken once"), TakeHandoff(S, 8.f).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommentaryIntensity, "CRICKET26.Commentary.Intensity", CricketPresentationTests::Flags)
bool FCommentaryIntensity::RunTest(const FString&)
{
	using namespace CricketCommentary;
	using namespace CricketCommentaryDirector;
	using namespace CricketPresentationTests;
	const FNames N{ TEXT("Asha"), TEXT("Bina"), TEXT("Home") };
	FSuperOverMatch First;
	First.Start(0);
	Bowl(First, Hit(0));
	FDeliveryResult Bat = Stroke(EShotType::Loft, FVector(20.f, 10.f, 15.f));
	Bat.Contact.Quality = 0.6f;
	const float DotCalm = ComputeIntensity(FContext::Build(FDeliveryResult(), Hit(0), First, N, 1.f, EDeliveryType::Stock));
	const float SixCalm = ComputeIntensity(FContext::Build(Bat, Hit(0, 6), First, N, 1.f, EDeliveryType::Stock));
	TestTrue(TEXT("six outweighs dot early"), SixCalm > DotCalm);
	// 6 required from 1: maximum pressure before the ball.
	FSuperOverMatch Chase;
	Chase.Start(0);
	Bowl(Chase, Hit(0, 6));
	for (int32 I = 0; I < 5; ++I) Bowl(Chase, Hit(0));
	Chase.StartSecondInnings();
	Bowl(Chase, Hit(1));
	for (int32 I = 0; I < 4; ++I) Bowl(Chase, Hit(0));
	TestEqual(TEXT("6 needed from the last"), Chase.RunsRequired(), 6);
	TestEqual(TEXT("final ball"), Chase.BallsRemaining(), 1);
	TestEqual(TEXT("max pressure"), CricketAudioDirector::PressureLevel(Chase), 1.0f);
	TestTrue(TEXT("early calmer than final-ball pressure"), CricketAudioDirector::PressureLevel(First) < 1.0f);
	TestEqual(TEXT("final-ball wicket decisive"), PriorityOf(FContext::Build(FDeliveryResult(), [] { FDeliveryOutcome O = Hit(0); O.Dismissal = EDismissal::Bowled; return O; }(), Chase, N, 1.f, EDeliveryType::Stock)), 4);
	TestEqual(TEXT("routine dot low priority"), PriorityOf(FContext::Build(FDeliveryResult(), Hit(0), First, N, 1.f, EDeliveryType::Stock)), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorSoak, "CRICKET26.Commentary.DirectorSoak", CricketPresentationTests::Flags)
bool FDirectorSoak::RunTest(const FString&)
{
	using namespace CricketCommentary;
	using namespace CricketCommentaryDirector;
	using namespace CricketPresentationTests;
	auto Says = [](const FString& Line, const TCHAR* W) { return Line.Contains(W, ESearchCase::IgnoreCase); };
	int32 Balls = 0, Spoken = 0, Silent = 0;
	for (int32 Game = 0; Game < 6; ++Game)
	{
		FSuperOverMatch M;
		M.Start(Game % 2);
		FRandomStream Rng(Game * 131 + 17);
		FRandomStream CommRng(Game * 733 + 5);
		CricketCommentaryDirector::FState CommState;
		CommState.Reset();
		CricketAudioDirector::FState AudioState;
		AudioState.Reset();
		TArray<int32> Recent;
		TArray<FString> Trailing;
		float Now = 0.f;
		const FPitchConditions Cond;
		for (int32 Safety = 0; Safety < 200 && M.Phase != EMatchPhase::MatchComplete; ++Safety)
		{
			if (M.Phase == EMatchPhase::InningsBreak) { M.StartSecondInnings(); continue; }
			FCricketPlayer Bowler;
			Bowler.BowlerType = EBowlerType(Game % 3);
			if (Bowler.BowlerType != EBowlerType::Pace) Bowler.PaceKph = 88.f;
			FResolveContext C;
			C.Bowler = Bowler;
			C.Striker.Name = TEXT("Asha");
			C.NonStriker.Name = TEXT("Bina");
			C.Field = CricketField::Make(CricketField::PresetFor(Bowler.BowlerType), C.Striker.BatHand, Bowler.BowlHand);
			C.bFreeHit = M.bFreeHit;
			C.Rules = M.Rules;
			C.BouncersBowled = M.Cur().Bouncers;
			C.Seed = Rng.RandHelper(1 << 20);
			const float Aggr = CricketAI::Aggression(M);
			C.RunMargin = CricketAI::RunMargin(M, Aggr);
			const FBowlingChoice Choice = CricketAI::ChooseDelivery(Bowler, C.Striker.BatHand, M, Recent, Rng);
			Recent.Add(Choice.PlanId);
			const FDeliveryRelease Rel = CricketBowling::Execute(Bowler, C.Striker.BatHand, Choice.Plan, Choice.ReleaseTiming, C.Seed, Cond);
			const FBatInput In = CricketAI::ChooseShot(Rel, C.Striker, Bowler.BowlerType, Aggr, C.Field, Cond, Rng);
			const FDeliveryResult R = CricketDelivery::Resolve(Rel, In, C);
			const FDeliveryOutcome O = R.ToOutcome();
			if (!M.BeginDelivery()) return false;
			TArray<ECricketEvent> Ev;
			if (!M.CompleteDelivery(O, Ev)) return false;
			// Both directors consume the finished delivery, exactly like the game mode does.
			CricketAudioDirector::OnDelivery(AudioState, R, O, M, Now);
			for (ECricketEvent E : Ev) CricketAudioDirector::OnEvent(AudioState, E, M, Now);
			const FNames N{ C.Striker.Name, C.NonStriker.Name, TEXT("Home") };
			const FContext Ctx = FContext::Build(R, O, M, N, 1.f, Rel.Type);
			const float I = ComputeIntensity(Ctx);
			TestTrue(TEXT("intensity in range"), I >= 0.f && I <= 1.f);
			const FSelection Sel = SelectLine(CommState, Ctx, N, Now, Balls, CommRng);
			Now += 20.f;
			++Balls;
			const float Crowd = CricketAudioDirector::TickCrowd(AudioState, 1.f / 60.f, Now, IsSpeaking(CommState, Now), false);
			TestTrue(TEXT("crowd finite and unclipped"), FMath::IsFinite(Crowd) && Crowd >= 0.f && Crowd <= CricketAudio::MixGain);
			if (Sel.bSilent)
			{
				++Silent;
				// Silence only on routine balls: the same must-speak contract the director keeps.
				const bool bMust = O.Boundary != 0 || O.Dismissal != EDismissal::None || Ctx.bMatchComplete || Ctx.bTied
					|| Ctx.bInningsBreak || Ctx.bFinalBall || Ctx.bDroppedCatch || Ctx.bCloseRunOut;
				TestFalse(TEXT("silence only when routine"), bMust);
				continue;
			}
			++Spoken;
			// Factual: the words match the umpire's outcome and name the right people.
			if (M.Phase == EMatchPhase::MatchComplete || M.bTied)
				TestTrue(FString(TEXT("result recognised: ")) + Sel.Text, Says(Sel.Text, TEXT("win")) || Says(Sel.Text, TEXT(" home with")) || Says(Sel.Text, TEXT("defend")) || Says(Sel.Text, TEXT("tie")) || Says(Sel.Text, TEXT("level")));
			else if (O.Dismissal != EDismissal::None)
				TestTrue(FString(TEXT("wicket names victim: ")) + Sel.Text,
					Sel.Text.Contains(O.bRunOutStriker || O.Dismissal != EDismissal::RunOut ? C.Striker.Name : C.NonStriker.Name));
			else if (O.Boundary == 6)
				TestTrue(FString(TEXT("six called six: ")) + Sel.Text, Says(Sel.Text, TEXT("six")) && !Says(Sel.Text, TEXT("four")));
			else if (O.Boundary == 4)
				TestTrue(FString(TEXT("four called four: ")) + Sel.Text, Says(Sel.Text, TEXT("four")) && !Says(Sel.Text, TEXT("six")));
			// No stale repeats inside the trailing window.
			for (const FString& Prev : Trailing) TestNotEqual(TEXT("no recent repeat"), Sel.Text, Prev);
			Trailing.Add(Sel.Text);
			while (Trailing.Num() > 4) Trailing.RemoveAt(0);
			// Vocals stay single-shot and scheduled (a pending celebration rightly blocks a lesser re-queue).
			const CricketAudioDirector::FVocalPick V = CricketAudioDirector::VocalFor(R, O);
			if (V.Vocal != CricketAudioDirector::EVocal::None)
			{
				CricketAudioDirector::PollVocal(AudioState, Now + 30.f); // drain the delivery-scheduled call
				TestTrue(TEXT("vocal scheduled"), CricketAudioDirector::QueueVocal(AudioState, V.Vocal, Now));
				TestEqual(TEXT("vocal fires once"), CricketAudioDirector::PollVocal(AudioState, Now + V.AfterContact + 1.f), V.Vocal);
				TestEqual(TEXT("vocal single-shot"), CricketAudioDirector::PollVocal(AudioState, Now + V.AfterContact + 2.f), CricketAudioDirector::EVocal::None);
			}
			if (M.Phase == EMatchPhase::MatchComplete && M.bTied) M.StartNextSuperOver();
		}
		TestEqual(TEXT("match finished"), M.Phase, EMatchPhase::MatchComplete);
	}
	AddInfo(FString::Printf(TEXT("soak: %d balls, %d spoken, %d silent"), Balls, Spoken, Silent));
	TestTrue(TEXT("broadcast mostly speaks"), Spoken * 2 >= Balls); // silence is the exception
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAudioDirector, "CRICKET26.Audio.Director", CricketPresentationTests::Flags)
bool FAudioDirector::RunTest(const FString&)
{
	using namespace CricketAudioDirector;
	using namespace CricketPresentationTests;
	FSuperOverMatch M;
	M.Start(0);
	Bowl(M, Hit(0));
	FState S;
	S.Reset();
	// Crowd swells gradually: no instant quiet->screaming cuts.
	// (A four: huge moments above 0.85 rightly let the crowd dominate unducked.)
	OnEvent(S, ECricketEvent::BoundaryFour, M, 0.f);
	TestEqual(TEXT("four lifts the room"), S.CrowdTarget, 0.8f);
	const float Early = TickCrowd(S, 0.2f, 0.2f, false, false) / CricketAudio::MixGain;
	TestTrue(TEXT("swell, not switch"), Early > 0.3f && Early < 1.0f);
	// Ducking is slight, never a mute; replays reduce the bed and let impacts through.
	// Compared at equal energy: settle first, then one tick with and without commentary.
	S.Reset();
	OnEvent(S, ECricketEvent::BoundaryFour, M, 10.f);
	TickCrowd(S, 2.f, 11.f, false, false);
	const float OpenOut = TickCrowd(S, 0.01f, 11.1f, false, false) / CricketAudio::MixGain;
	const float DuckedOut = TickCrowd(S, 0.01f, 11.2f, true, false) / CricketAudio::MixGain;
	TestTrue(TEXT("ducking slight"), DuckedOut < OpenOut && DuckedOut > OpenOut * 0.5f);
	TickCrowd(S, 1.f, 12.f, true, false);
	TestTrue(TEXT("duck engaged"), S.Duck < 0.95f);
	TickCrowd(S, 2.f, 14.f, false, false);
	TestTrue(TEXT("duck released"), S.Duck > 0.95f);
	TestTrue(TEXT("replay bed reduced"), TickCrowd(S, 0.01f, 3.2f, false, true) < TickCrowd(S, 0.01f, 3.3f, false, false));
	// Concurrency: no stacking.
	TestTrue(TEXT("first footstep plays"), ShouldPlay(S, CricketAudio::ECue::Footstep, 1.f));
	MarkPlayed(S, CricketAudio::ECue::Footstep, 1.f);
	TestFalse(TEXT("second footstep held"), ShouldPlay(S, CricketAudio::ECue::Footstep, 1.1f));
	TestTrue(TEXT("footstep later plays"), ShouldPlay(S, CricketAudio::ECue::Footstep, 2.f));
	// Contact-driven picks.
	FDeliveryResult Mid = Stroke(EShotType::Loft, FVector(20.f, 10.f, 15.f));
	Mid.Contact.Zone = EContactZone::Middle;
	Mid.Contact.Quality = 0.95f;
	TestEqual(TEXT("middled pick"), ContactSfx(Mid).Cue, CricketAudio::ECue::BatMiddle);
	// Vocals: appeal on pad, call on running, silence otherwise.
	FDeliveryResult Pad;
	Pad.bPadImpact = true;
	TestEqual(TEXT("appeal"), VocalFor(Pad, Hit(0)).Vocal, EVocal::Howzat);
	FDeliveryResult Run = Stroke(EShotType::Drive, FVector(10.f, 5.f, 0.f));
	Run.Running.Attempted = 2;
	Run.Running.Leaves = { 0.3f, 1.5f };
	TestEqual(TEXT("running call"), VocalFor(Run, Hit(1)).Vocal, EVocal::Run);
	TestEqual(TEXT("routine dot silent"), VocalFor(FDeliveryResult(), Hit(0)).Vocal, EVocal::None);
	QueueVocal(S, EVocal::Howzat, 5.f);
	TestEqual(TEXT("not due yet"), PollVocal(S, 4.f), EVocal::None);
	TestEqual(TEXT("due"), PollVocal(S, 5.f), EVocal::Howzat);
	TestEqual(TEXT("single shot"), PollVocal(S, 6.f), EVocal::None);
	return true;
}

#endif
