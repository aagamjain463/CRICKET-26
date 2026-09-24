// Presentation automation tests: synthesised sound cues, text commentary and ball readability.

#include "Misc/AutomationTest.h"
#include "CricketAudio.h"
#include "CricketCommentary.h"
#include "SuperOverGameMode.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"

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

#endif
