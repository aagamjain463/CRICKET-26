// Broadcast camera + replay automation tests: phases, classification, cuts, smoothing,
// replay triggers/packages/slow-motion and the rolling buffer. Pure logic: no world needed.

#include "Misc/AutomationTest.h"
#include "CricketBroadcast.h"
#include "CricketReplayBuffer.h"
#include "DeliveryResolver.h"
#include "SuperOverMatch.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CricketBroadcastTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FBroadcastTuning DefaultTune() { return FBroadcastTuning(); }

	/** A synthetic resolved delivery: contact + ball path + fielding, with explicit knobs. */
	FDeliveryResult MockResult(EContactZone Zone, const FVector& ExitVel, int32 Boundary, int32 Fielder,
		bool bCatchChance, bool bCaught, float FieldTime, const FVector& FieldPos, int32 RunsAttempted,
		bool bStumpsHit = false)
	{
		FDeliveryResult R;
		R.Contact.Zone = Zone;
		R.Contact.Shot = EShotType::Drive;
		R.Contact.ExitVel = ExitVel;
		R.Contact.ContactPos = FVector(2.f, 0.f, 0.8f);
		R.ContactTime = 0.55f;
		R.Contact.ContactTime = 0.55f;
		for (int32 I = 0; I <= 60; ++I) R.BallPath.Add(FVector(20.f - 0.3f * I, 0.02f * I, 2.f - 0.02f * I * I * 0.05f));
		R.DeadTime = 4.f;
		R.bStumpsHit = bStumpsHit;
		R.StumpsTime = 0.7f;
		R.Fielding.Boundary = Boundary;
		R.Fielding.BoundaryTime = 2.5f;
		R.Fielding.Fielder = Fielder;
		R.Fielding.FieldTime = FieldTime;
		R.Fielding.FieldPos = FieldPos;
		R.Fielding.bCatchChance = bCatchChance;
		R.Fielding.bCaught = bCaught;
		R.Running.Attempted = RunsAttempted;
		R.Running.Margin = 1.f;
		return R;
	}

	FDeliveryOutcome MockOutcome(int32 Boundary, EDismissal Dismissal)
	{
		FDeliveryOutcome O;
		O.Boundary = Boundary;
		O.Dismissal = Dismissal;
		O.bBatContact = true;
		return O;
	}

	FBroadcastFrame MockFrame()
	{
		FBroadcastFrame F;
		F.BallPos = FVector(1000.f, 0.f, 150.f);
		F.BowlerPos = FVector(2000.f, 50.f, 90.f);
		F.StrikerPos = FVector(90.f, -35.f, 90.f);
		F.ContactPos = FVector(2.f, 0.f, 0.8f);
		return F;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryCameraDefaults, "CRICKET26.Broadcast.DeliveryDefaults", CricketBroadcastTests::Flags)
bool FDeliveryCameraDefaults::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	using namespace CricketBroadcastTests;
	const FBroadcastTuning Tune = DefaultTune();
	// The Cricket-24-style delivery view: high behind the bowler, long lens, above the ground.
	const FVector Loc = DeliveryLocation(Tune.Delivery, 1.f);
	TestTrue(TEXT("behind the bowler's stumps"), Loc.X > CricketGeo::PitchLength * 100.f);
	TestTrue(TEXT("high in the stand"), Loc.Z > 800.f && Loc.Z < 2000.f);
	TestTrue(TEXT("narrow broadcast lens"), Tune.Delivery.FOV <= 10.f && Tune.Delivery.FOV >= 4.f);
	const FVector Look = DeliveryLookAt(Tune.Delivery);
	TestTrue(TEXT("looks at the striker's end"), Look.X < CricketGeo::PitchLength * 100.f * 0.25f);
	TestTrue(TEXT("safeguards keep it out of the turf"), ApplyCameraSafeguards(FVector(0.f, 0.f, -500.f), Tune).Z >= 60.f);
	TestEqual(TEXT("null world skips occlusion"), ApplyOcclusion(nullptr, Look, Loc), Loc);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryWeightBlends, "CRICKET26.Broadcast.DeliveryWeights", CricketBroadcastTests::Flags)
bool FDeliveryWeightBlends::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	using namespace CricketBroadcastTests;
	const FBroadcastTuning Tune = DefaultTune();
	// Weights always sum to one and move smoothly: no abrupt reframing mid-delivery.
	bool bFirst = true;
	float PrevBowler = 0.f;
	for (int32 I = 0; I <= 20; ++I)
	{
		const FDeliveryWeights W = CricketBroadcast::DeliveryWeights(I / 20.f, Tune.Delivery);
		TestTrue(TEXT("weights sum to one"), FMath::IsNearlyEqual(W.Bowler + W.Release + W.Ball + W.Batter, 1.f, 1e-4f));
		if (!bFirst) TestTrue(TEXT("no weight jumps"), FMath::Abs(W.Bowler - PrevBowler) < 0.2f);
		bFirst = false;
		PrevBowler = W.Bowler;
	}
	const FDeliveryWeights Early = CricketBroadcast::DeliveryWeights(0.f, Tune.Delivery), Late = CricketBroadcast::DeliveryWeights(1.f, Tune.Delivery);
	TestTrue(TEXT("run-up is bowler-dominant"), Early.Bowler > Late.Bowler);
	TestTrue(TEXT("contact is ball/batter-led"), Late.Ball + Late.Batter > Early.Ball + Early.Batter);
	// The target never dives into the turf, and the operator's pan is slew-limited: consecutive
	// targets centimetres apart, never a snap. (It rightly passes near the ball mid-flight: the
	// corridor target and the ball share the delivery line, which is the point of the weighting.)
	const FBroadcastFrame F = MockFrame();
	FVector PrevT = WeightedDeliveryTarget(F, Tune.Delivery, 0.f);
	for (int32 I = 1; I <= 10; ++I)
	{
		const FVector T = WeightedDeliveryTarget(F, Tune.Delivery, I / 10.f);
		TestTrue(TEXT("target above the turf"), T.Z > 30.f);
		TestTrue(TEXT("target pans, never snaps"), FVector::Dist(T, PrevT) < 150.f);
		PrevT = T;
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryLensCurve, "CRICKET26.Broadcast.DeliveryLensCurve", CricketBroadcastTests::Flags)
bool FDeliveryLensCurve::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	using namespace CricketBroadcastTests;
	const FBroadcastTuning Tune = DefaultTune();
	const FVector Crease = CricketGeo::PoppingCrease * FVector(100.f, 0.f, 0.f);
	// Where the striker's crease lands on screen (0 top .. 1 bottom) for a solved shot, by projection.
	auto CreaseScreenY = [&](const FLiveCameraSolution& S, float Aspect)
	{
		const FRotationMatrix M((S.LookAt - S.Location).Rotation());
		const FVector Rel = Crease - S.Location;
		const float Fwd = FVector::DotProduct(Rel, M.GetUnitAxis(EAxis::X)), Up = FVector::DotProduct(Rel, M.GetUnitAxis(EAxis::Z));
		const float VTan = FMath::Tan(FMath::DegreesToRadians(0.5f * S.FOV)) / Aspect;
		return 0.5f - 0.5f * (Up / Fwd) / VTan;
	};
	// Every key of the Cricket 24 curve is reproduced exactly: its lens, and its crease height on screen.
	for (const FDeliveryLensKey& K : Tune.Delivery.LensCurve)
	{
		const FLiveCameraSolution S = SolveDeliveryShot(Tune.Delivery, K.U, 16.f / 9.f, 1.f, Crease);
		TestTrue(FString::Printf(TEXT("lens at U=%.2f"), K.U), FMath::IsNearlyEqual(S.FOV, K.FOV, 0.01f));
		TestTrue(FString::Printf(TEXT("crease height at U=%.2f (%.3f)"), K.U, CreaseScreenY(S, 16.f / 9.f)), FMath::IsNearlyEqual(CreaseScreenY(S, 16.f / 9.f), K.CreaseY, 0.003f));
	}
	// A 19.5:9 phone sees the same vertical composition, only wider.
	const FLiveCameraSolution Phone = SolveDeliveryShot(Tune.Delivery, 1.5f, 19.5f / 9.f, 1.f, Crease);
	const FLiveCameraSolution Tv = SolveDeliveryShot(Tune.Delivery, 1.5f, 16.f / 9.f, 1.f, Crease);
	TestTrue(TEXT("wider screen, wider lens"), Phone.FOV > Tv.FOV);
	TestTrue(TEXT("same crease height on any aspect"), FMath::IsNearlyEqual(CreaseScreenY(Phone, 19.5f / 9.f), CreaseScreenY(Tv, 16.f / 9.f), 0.002f));
	// The clock never jumps through release, and the zoom only ever tightens.
	TestTrue(TEXT("clock continuous at release"), FMath::IsNearlyEqual(DeliveryClock(1.f, -1.f, 0.6f), DeliveryClock(0.f, 0.f, 0.6f), 1e-4f));
	float PrevFov = 1e9f;
	for (int32 I = 0; I <= 100; ++I)
	{
		float Fov = 0.f, Y = 0.f;
		SampleLensCurve(Tune.Delivery, I / 50.f, Fov, Y);
		TestTrue(TEXT("zoom only tightens"), Fov <= PrevFov + 1e-4f);
		PrevFov = Fov;
	}
	// The zoom speed has no kinks (regression: straight segments between keys jolted the lens at every key):
	// the log-zoom rate either side of each key agrees, and the push starts and lands at rest.
	auto LogFov = [&](float U) { float Fov = 0.f, Y = 0.f; SampleLensCurve(Tune.Delivery, U, Fov, Y); return FMath::Loge(Fov); };
	constexpr float E = 1e-3f;
	for (const FDeliveryLensKey& K : Tune.Delivery.LensCurve)
	{
		if (K.U <= 0.f || K.U >= 2.f || FMath::IsNearlyEqual(K.U, 1.f)) continue;
		// Extrapolate one-sided rates to the key: finite intervals also measure curve acceleration.
		const float Before = 2.f * (LogFov(K.U) - LogFov(K.U - E * 0.5f)) / (E * 0.5f) - (LogFov(K.U) - LogFov(K.U - E)) / E;
		const float After = 2.f * (LogFov(K.U + E * 0.5f) - LogFov(K.U)) / (E * 0.5f) - (LogFov(K.U + E) - LogFov(K.U)) / E;
		TestTrue(FString::Printf(TEXT("extrapolated zoom speed at U=%.3f (%.6f vs %.6f)"), K.U, Before, After), FMath::Abs(Before - After) < 0.05f * FMath::Max(FMath::Abs(Before), 0.2f));
	}
	// Through the release the clock changes speed (a 4 s run-up, then a 0.5 s flight): the zoom speed in real time holds.
	const float RunUpRate = 0.25f * (LogFov(1.f) - LogFov(1.f - E)) / E, FlightRate = 2.f * (LogFov(1.f + E) - LogFov(1.f)) / E;
	TestTrue(FString::Printf(TEXT("zoom speed holds through release (%.2f vs %.2f per s)"), RunUpRate, FlightRate), FlightRate / RunUpRate > 0.7f && FlightRate / RunUpRate < 1.4f);
	TestTrue(TEXT("push eases out of the wide"), FMath::Abs(LogFov(E) - LogFov(0.f)) / E < 0.05f);
	// The camera's clock tracks a steady game clock with no lag, and absorbs a jump (a human letting go early)
	// without a step, at any frame rate.
	for (const float Hz : { 30.f, 60.f, 120.f })
	{
		FDeliveryClockFollow Clock;
		const float Dt = 1.f / Hz;
		float Game = 0.f, Worst = 0.f;
		for (int32 I = 0; I < int32(2.f * Hz); ++I) { Game += 0.25f * Dt; Clock.Update(Game, Dt); }
		TestTrue(FString::Printf(TEXT("steady clock tracked at %.0f Hz"), Hz), FMath::IsNearlyEqual(Clock.U, Game, 0.01f));
		Game += 0.3f; // released early: the game clock leaps to release
		float Was = Clock.U;
		for (int32 I = 0; I < int32(1.5f * Hz); ++I)
		{
			Game += 2.f * Dt;
			Clock.Update(FMath::Min(Game, 2.f), Dt);
			Worst = FMath::Max(Worst, (Clock.U - Was) / Dt);
			Was = Clock.U;
		}
		TestTrue(FString::Printf(TEXT("jump absorbed smoothly at %.0f Hz (peak %.1f/s)"), Hz, Worst), Worst < 4.f);
		TestTrue(FString::Printf(TEXT("caught up at %.0f Hz"), Hz), FMath::IsNearlyEqual(Clock.U, 2.f, 0.02f));
		Clock.Update(0.f, Dt);
		TestTrue(TEXT("a new pass snaps"), Clock.U == 0.f);
	}
	// The camera stands where the reference measured it: 51.7 m behind the stumps, 8.1 m up, on the line.
	const FVector Loc = DeliveryLocation(Tune.Delivery, 1.f);
	TestTrue(TEXT("measured stand position"), Loc.Equals(FVector((CricketGeo::PitchLength + 51.7f) * 100.f, 0.f, 810.f), 1.f));
	// The pan drifts a share toward a target off the line, never all the way.
	const FVector Off = Crease + FVector(0.f, 400.f, 0.f);
	const FLiveCameraSolution Panned = SolveDeliveryShot(Tune.Delivery, 1.5f, 16.f / 9.f, 1.f, Off);
	const float PanYaw = (Panned.LookAt - Panned.Location).Rotation().Yaw - (Tv.LookAt - Tv.Location).Rotation().Yaw;
	const float FullYaw = (Off - Tv.Location).Rotation().Yaw - (Crease - Tv.Location).Rotation().Yaw;
	TestTrue(TEXT("pan follows a share of the target"), FMath::Abs(PanYaw - Tune.Delivery.LateralFollow * FullYaw) < 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmootherLongLens, "CRICKET26.Broadcast.SmootherLongLens", CricketBroadcastTests::Flags)
bool FSmootherLongLens::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	// Regression: the look dead zone was a fixed 15 cm at the target, which on a 4.6 deg lens 70 m away
	// is ~30 px of 1280: the long delivery lens sat off its mark, then stepped. It now narrows with the lens.
	FSmoother S;
	S.Snap(FVector::ZeroVector, FQuat::Identity, 4.6f);
	S.Update(FVector::ZeroVector, FVector(7000.f, 10.f, 0.f), 4.6f, 1.f / 60.f, 3.f, 20.f, 120.f, 15.f);
	TestFalse(TEXT("long lens tracks a 10 cm error at 70 m"), FQuat::Identity.Equals(S.Rotation));
	// FovLambda lets the zoom ride its curve tighter than the position damping.
	FSmoother A, B;
	A.Snap(FVector::ZeroVector, FQuat::Identity, 20.f);
	B.Snap(FVector::ZeroVector, FQuat::Identity, 20.f);
	A.Update(FVector::ZeroVector, FVector(1000.f, 0.f, 0.f), 5.f, 0.1f, 3.f, 20.f, 120.f, 0.f);
	B.Update(FVector::ZeroVector, FVector(1000.f, 0.f, 0.f), 5.f, 0.1f, 3.f, 20.f, 120.f, 0.f, 20.f);
	TestTrue(TEXT("fov lambda defaults to position lambda, and overrides it"), B.FOV < A.FOV);
	// Regression: the slew cap was a floor, so any error under MaxDegPerSec * Dt was closed in one frame and the
	// long lens jumped with every nudge of its target. It eases now, and the cap holds a big turn back.
	auto TurnDeg = [](const FSmoother& Sm) { return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Sm.Rotation.Vector().X, -1.f, 1.f))); };
	FSmoother Nudge, Whip;
	Nudge.Snap(FVector::ZeroVector, FQuat::Identity, 5.f);
	Whip.Snap(FVector::ZeroVector, FQuat::Identity, 40.f);
	Nudge.Update(FVector::ZeroVector, FVector(7000.f, 7000.f * FMath::Tan(FMath::DegreesToRadians(1.f)), 0.f), 5.f, 1.f / 60.f, 3.f, 6.f, 120.f, 0.f);
	Whip.Update(FVector::ZeroVector, FVector(0.f, 7000.f, 0.f), 40.f, 1.f / 60.f, 3.f, 1000.f, 60.f, 0.f);
	TestTrue(TEXT("a 1 degree nudge eases rather than snapping"), TurnDeg(Nudge) > 0.05f && TurnDeg(Nudge) < 0.2f);
	TestTrue(TEXT("a 90 degree turn is held to the cap"), FMath::IsNearlyEqual(TurnDeg(Whip), 1.f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBroadcastPhases, "CRICKET26.Broadcast.Phases", CricketBroadcastTests::Flags)
bool FBroadcastPhases::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	using namespace CricketBroadcastTests;
	const FBroadcastTuning Tune = DefaultTune();
	FPhaseInput In;
	TestEqual(TEXT("pre-delivery"), int32(ResolvePhase(In, Tune)), int32(EBroadcastPhase::PreDelivery));
	In.bRunUp = true;
	TestEqual(TEXT("run-up"), int32(ResolvePhase(In, Tune)), int32(EBroadcastPhase::RunUp));
	In.bRunUp = false; In.bBallLive = true; In.BallT = 0.2f; In.ContactTime = 0.55f;
	TestEqual(TEXT("delivery flight"), int32(ResolvePhase(In, Tune)), int32(EBroadcastPhase::Delivery));
	// Regression: a ball that will be hit read CONTACT for its whole flight (negative AfterContact).
	In.bHasContact = true; In.AfterContact = In.BallT - In.ContactTime;
	TestEqual(TEXT("hit ball still in delivery before contact"), int32(ResolvePhase(In, Tune)), int32(EBroadcastPhase::Delivery));
	In.BallT = 0.6f; In.AfterContact = 0.05f;
	TestEqual(TEXT("contact breath"), int32(ResolvePhase(In, Tune)), int32(EBroadcastPhase::Contact));
	In.BallT = 1.55f; In.AfterContact = 1.f;
	TestEqual(TEXT("ball in play"), int32(ResolvePhase(In, Tune)), int32(EBroadcastPhase::BallInPlay));
	In.bBallLive = false; In.bDead = true;
	TestEqual(TEXT("ball dead"), int32(ResolvePhase(In, Tune)), int32(EBroadcastPhase::BallDead));
	In.Boundary = 4;
	TestEqual(TEXT("boundary"), int32(ResolvePhase(In, Tune)), int32(EBroadcastPhase::Boundary));
	In.Boundary = 0; In.bWicketFallen = true; In.Dismissal = EDismissal::Bowled;
	TestEqual(TEXT("wicket"), int32(ResolvePhase(In, Tune)), int32(EBroadcastPhase::Wicket));
	In.bDead = false; In.bReplaying = true;
	TestEqual(TEXT("replay"), int32(ResolvePhase(In, Tune)), int32(EBroadcastPhase::Replay));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotClassification, "CRICKET26.Broadcast.ShotClassification", CricketBroadcastTests::Flags)
bool FShotClassification::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	using namespace CricketBroadcastTests;
	TestEqual(TEXT("ground infield"), int32(ClassifyShot(MockResult(EContactZone::Middle, FVector(12.f, 6.f, 1.f), 0, 3, false, false, 1.4f, FVector(12.f, 8.f, 0.f), 0), EDismissal::None)), int32(EShotClass::GroundInfield));
	TestEqual(TEXT("ground outfield"), int32(ClassifyShot(MockResult(EContactZone::Middle, FVector(20.f, 12.f, 1.f), 0, 7, false, false, 2.2f, FVector(45.f, 25.f, 0.f), 0), EDismissal::None)), int32(EShotClass::GroundOutfield));
	TestEqual(TEXT("boundary trajectory"), int32(ClassifyShot(MockResult(EContactZone::Middle, FVector(24.f, 14.f, 3.f), 4, -1, false, false, 0.f, FVector::ZeroVector, 0), EDismissal::None)), int32(EShotClass::BoundaryTrajectory));
	TestEqual(TEXT("lofted outfield"), int32(ClassifyShot(MockResult(EContactZone::Middle, FVector(16.f, 10.f, 12.f), 0, 7, false, false, 3.f, FVector(50.f, 30.f, 0.f), 0), EDismissal::None)), int32(EShotClass::LoftedOutfield));
	TestEqual(TEXT("hanging catch chance"), int32(ClassifyShot(MockResult(EContactZone::Middle, FVector(10.f, 6.f, 11.f), 0, 4, true, false, 2.6f, FVector(30.f, 18.f, 0.f), 0), EDismissal::None)), int32(EShotClass::HighCatchChance));
	TestEqual(TEXT("top edge to catch cover"), int32(ClassifyShot(MockResult(EContactZone::TopEdge, FVector(6.f, 4.f, 9.f), 0, 4, true, false, 2.8f, FVector(20.f, 15.f, 0.f), 0), EDismissal::None)), int32(EShotClass::LoftedInfield));
	TestEqual(TEXT("edge through"), int32(ClassifyShot(MockResult(EContactZone::OutsideEdge, FVector(8.f, -6.f, 1.f), 0, 0, false, false, 0.8f, FVector(0.5f, -1.f, 0.f), 0), EDismissal::None)), int32(EShotClass::KeeperEdge));
	TestEqual(TEXT("bowled is a wicket"), int32(ClassifyShot(MockResult(EContactZone::Miss, FVector::ZeroVector, 0, -1, false, false, 0.f, FVector::ZeroVector, 0, true), EDismissal::Bowled)), int32(EShotClass::WicketEvent));
	TestEqual(TEXT("caught is a wicket"), int32(ClassifyShot(MockResult(EContactZone::Middle, FVector(14.f, 8.f, 10.f), 0, 5, true, true, 2.6f, FVector(35.f, 20.f, 0.f), 0), EDismissal::Caught)), int32(EShotClass::WicketEvent));
	TestEqual(TEXT("run-out is running"), int32(ClassifyShot(MockResult(EContactZone::Middle, FVector(10.f, 5.f, 1.f), 0, 2, false, false, 1.2f, FVector(10.f, 6.f, 0.f), 1), EDismissal::RunOut)), int32(EShotClass::RunningPlay));
	TestEqual(TEXT("beaten: hold delivery"), int32(ClassifyShot(MockResult(EContactZone::Miss, FVector::ZeroVector, 0, -1, false, false, 0.f, FVector::ZeroVector, 0), EDismissal::None)), int32(EShotClass::NoContact));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCutRules, "CRICKET26.Broadcast.CutRules", CricketBroadcastTests::Flags)
bool FCutRules::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	using namespace CricketBroadcastTests;
	const FBroadcastTuning Tune = DefaultTune();
	TestEqual(TEXT("pulling out blends"), int32(TransitionFor(EBroadcastShot::StandardDelivery, EBroadcastShot::GroundFollow)), int32(ECameraTransition::Blend));
	TestEqual(TEXT("reverse angle is a new camera: cut"), int32(TransitionFor(EBroadcastShot::StandardDelivery, EBroadcastShot::OutfieldFollow)), int32(ECameraTransition::Cut));
	TestEqual(TEXT("role changes cut"), int32(TransitionFor(EBroadcastShot::GroundFollow, EBroadcastShot::Boundary)), int32(ECameraTransition::Cut));
	TestEqual(TEXT("same role blends"), int32(TransitionFor(EBroadcastShot::Catch, EBroadcastShot::Catch)), int32(ECameraTransition::Blend));
	TestEqual(TEXT("close-up cuts home to the wide"), int32(TransitionFor(EBroadcastShot::WicketClose, EBroadcastShot::StandardDelivery)), int32(ECameraTransition::Cut));

	// No thrashing: establish a young shot, then flip the desire rapidly: the young shot survives
	// every flip, and only a forced event cuts through. No A -> B -> A ping-pong.
	FDirectorState D;
	D.Reset(EBroadcastShot::StandardDelivery);
	bool bCut = false;
	TestEqual(TEXT("forced take"), int32(D.Update(0.1f, EBroadcastShot::GroundFollow, true, Tune, bCut)), int32(EBroadcastShot::GroundFollow));
	for (int32 I = 0; I < 6; ++I)
	{
		const EBroadcastShot Want = I % 2 == 0 ? EBroadcastShot::StandardDelivery : EBroadcastShot::GroundFollow;
		TestEqual(TEXT("no thrash under rapid flips"), int32(D.Update(0.1f, Want, false, Tune, bCut)), int32(EBroadcastShot::GroundFollow));
		TestFalse(TEXT("no cut while held"), bCut);
	}
	TestEqual(TEXT("forced event cuts"), int32(D.Update(0.1f, EBroadcastShot::Boundary, true, Tune, bCut)), int32(EBroadcastShot::Boundary));
	TestTrue(TEXT("forced cut flagged"), bCut);
	TestEqual(TEXT("no ping-pong back"), int32(D.Update(0.1f, EBroadcastShot::GroundFollow, false, Tune, bCut)), int32(EBroadcastShot::Boundary));

	// CRICKET26.mp4 reference: the close-up cuts home to the wide (bowler face 2-3 s, then a hard
	// cut to the delivery wide). A blend home would glide across the ground through the sightscreen.
	// Every replay angle cuts, including a repeat of the same shot kind.
	D.Reset(EBroadcastShot::WicketClose);
	D.Update(0.1f, EBroadcastShot::StandardDelivery, true, Tune, bCut);
	TestTrue(TEXT("live close-up cuts home to the wide"), bCut);
	D.Reset(EBroadcastShot::WicketClose);
	D.Update(0.1f, EBroadcastShot::StandardDelivery, true, Tune, bCut, 0);
	TestTrue(TEXT("into the replay cuts"), bCut);
	D.Update(0.1f, EBroadcastShot::StandardDelivery, true, Tune, bCut, 0);
	TestFalse(TEXT("holding an angle does not cut"), bCut);
	D.Update(0.1f, EBroadcastShot::StandardDelivery, true, Tune, bCut, 1);
	TestTrue(TEXT("the next angle cuts, same shot kind"), bCut);

	// Live selection never loses the plot: beaten balls hold delivery, boundaries go to the rope.
	FBroadcastFrame F = MockFrame();
	F.ShotClass = EShotClass::NoContact;
	TestEqual(TEXT("no contact holds"), int32(SelectLiveShot(EBroadcastPhase::BallInPlay, F)), int32(EBroadcastShot::StandardDelivery));
	F.ShotClass = EShotClass::BoundaryTrajectory;
	TestEqual(TEXT("boundary ball on the reverse angle"), int32(SelectLiveShot(EBroadcastPhase::BallInPlay, F)), int32(EBroadcastShot::OutfieldFollow));
	TestEqual(TEXT("reverse angle holds through the rope"), int32(SelectLiveShot(EBroadcastPhase::Boundary, F)), int32(EBroadcastShot::OutfieldFollow));
	F.ShotClass = EShotClass::HighCatchChance;
	TestEqual(TEXT("high ball held on the reverse angle"), int32(SelectLiveShot(EBroadcastPhase::BallInPlay, F)), int32(EBroadcastShot::OutfieldFollow));
	F.ShotClass = EShotClass::LoftedOutfield;
	TestEqual(TEXT("lofted ball on the reverse angle"), int32(SelectLiveShot(EBroadcastPhase::BallInPlay, F)), int32(EBroadcastShot::OutfieldFollow));
	TestEqual(TEXT("dead ball goes to the bowler close-up"), int32(SelectLiveShot(EBroadcastPhase::BallDead, F)), int32(EBroadcastShot::WicketClose));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFollowHoldsHorizon, "CRICKET26.Broadcast.FollowHoldsHorizon", CricketBroadcastTests::Flags)
bool FFollowHoldsHorizon::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	using namespace CricketBroadcastTests;
	const FBroadcastTuning Tune = DefaultTune();
	// Regression: following a ball toward the far boundary, the stand camera levelled out on the
	// predicted landing point and put the horizon mid-frame, half the picture crowd. Each follow keeps
	// its horizon share (field-dominant) and the ball in shot.
	FBroadcastFrame F = MockFrame();
	F.BallPos = FVector(-1500.f, 2500.f, 60.f); // past the striker, heading fine on the off side
	F.PredictedPos = FVector(-4500.f, 5500.f, 20.f);
	F.bHasPrediction = true;
	F.ExitVel = FVector(-15.f, 15.f, 1.f);
	for (const EBroadcastShot Shot : { EBroadcastShot::GroundFollow, EBroadcastShot::OutfieldFollow })
	{
		const FLiveCameraSolution S = SolveShotGeometry(Shot, F, Tune);
		if (Shot == EBroadcastShot::OutfieldFollow)
		{
			// Cricket 24's reverse angle: across the ground from the ball, looking out over the pitch.
			const FVector Centre(50.f * CricketGeo::PitchLength, 0.f, 0.f); // pitch centre, cm
			TestTrue(TEXT("reverse angle opposite the ball"), FVector::DotProduct((S.Location - Centre).GetSafeNormal2D(), (F.PredictedPos - Centre).GetSafeNormal2D()) < -0.9f);
		}
		const FVector Dir = (S.LookAt - S.Location).GetSafeNormal();
		const float VTan = FMath::Tan(FMath::DegreesToRadians(S.FOV) * 0.5f) * 9.f / 16.f;
		const float Dip = FMath::Asin(-Dir.Z);
		const float HorizonY = Shot == EBroadcastShot::GroundFollow ? Tune.FollowHorizonY : Tune.OutfieldHorizonY;
		TestTrue(TEXT("horizon held high"), FMath::Tan(Dip) / VTan >= HorizonY - 0.01f);
		TestTrue(TEXT("look target on or above the turf"), S.LookAt.Z >= -1.f);
		// Ball's vertical screen position (pinhole, camera without roll).
		const FVector Fwd = Dir, Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal(), Up = FVector::CrossProduct(Fwd, Right);
		const FVector ToBall = F.BallPos - S.Location;
		const float Depth = FVector::DotProduct(ToBall, Fwd);
		TestTrue(TEXT("ball in front"), Depth > 0.f);
		TestTrue(TEXT("ball in frame vertically"), FMath::Abs(FVector::DotProduct(ToBall, Up) / Depth / VTan) < 1.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCameraRolesSane, "CRICKET26.Broadcast.CameraRolesSane", CricketBroadcastTests::Flags)
bool FCameraRolesSane::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	using namespace CricketBroadcastTests;
	const FBroadcastTuning Tune = DefaultTune();
	// Every role, in three representative situations, must stay out of the turf/geometry, keep a
	// sane lens, and look at something near the action: no underground cameras, no sky stares, no
	// fisheyes. (The aerial role once derived its station from the 3D ball direction and ended up
	// clamped at turf level staring at the sky.)
	FBroadcastFrame Delivery = MockFrame();
	Delivery.BallPos = FVector(1100.f, 20.f, 180.f);
	FBroadcastFrame HighBall = MockFrame();
	HighBall.BallPos = FVector(1500.f, -2500.f, 2000.f); // a six climbing over midwicket
	HighBall.PredictedPos = FVector(1400.f, -3500.f, 2400.f);
	HighBall.bHasPrediction = true;
	HighBall.ExitVel = FVector(-2.f, -18.f, 12.f);
	HighBall.ContactPos = FVector(2.f, 0.f, 0.8f);
	HighBall.bHasFielder = true;
	HighBall.FielderPos = FVector(1200.f, -4500.f, 90.f);
	FBroadcastFrame AtRope = HighBall;
	AtRope.BallPos = FVector(900.f, -6200.f, 60.f);
	AtRope.bHasBoundaryCross = true;
	AtRope.BoundaryCrossPos = FVector(850.f, -6500.f, 40.f);
	AtRope.BoundaryTime = 3.f;
	AtRope.AfterContact = 3.4f;
	for (int32 S = 0; S < int32(EBroadcastShot::Count); ++S)
	{
		const EBroadcastShot Shot = EBroadcastShot(S);
		if (Shot == EBroadcastShot::Review || Shot == EBroadcastShot::Scorecard || Shot == EBroadcastShot::Presentation) continue;
		for (const FBroadcastFrame& F : { Delivery, HighBall, AtRope })
		{
			const FLiveCameraSolution Sol = SolveShotGeometry(Shot, F, Tune);
			const FString What = FString::Printf(TEXT("role %d"), S);
			TestTrue(What + TEXT(" above the turf"), Sol.Location.Z >= Tune.MinCameraHeightM * 100.f - 1.f);
			TestTrue(What + TEXT(" sane lens"), Sol.FOV >= 2.f && Sol.FOV <= 75.f);
			TestTrue(What + TEXT(" look target above ground"), Sol.LookAt.Z >= -50.f);
			TestTrue(What + TEXT(" look target near the action"),
				FVector::Dist(Sol.LookAt, F.BallPos) < 8000.f || FVector::Dist(Sol.LookAt, F.StrikerPos) < 8000.f);
		}
	}
	// The fixed defect, pinned: the high-ball aerial station stays high, looking at the ball.
	const FLiveCameraSolution Aer = SolveShotGeometry(EBroadcastShot::AerialBall, HighBall, Tune);
	TestTrue(TEXT("aerial station stays high"), Aer.Location.Z > 1000.f);
	TestTrue(TEXT("aerial eye on the ball"), FVector::Dist(Aer.LookAt, HighBall.BallPos) < 3000.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSmootherFrameRate, "CRICKET26.Broadcast.SmootherFrameRate", CricketBroadcastTests::Flags)
bool FSmootherFrameRate::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	auto Run = [](float Dt)
	{
		FSmoother S;
		FVector Target(0.f, 0.f, 1000.f);
		for (int32 I = 0; I < int32(2.f / Dt); ++I)
		{
			Target += FVector(50.f * Dt, 20.f * Dt, 0.f); // a drifting operator target
			S.Update(Target, Target + FVector(-500.f, 0.f, -200.f), 30.f, Dt, 3.f, 6.f, 120.f, 0.f);
		}
		return S.Location;
	};
	const FVector At30 = Run(1.f / 30.f), At60 = Run(1.f / 60.f), At120 = Run(1.f / 120.f);
	AddInfo(FString::Printf(TEXT("30fps %s 60fps %s 120fps %s"), *At30.ToString(), *At60.ToString(), *At120.ToString()));
	TestTrue(TEXT("30 vs 120 fps converge"), FVector::Dist(At30, At120) < 8.f);
	TestTrue(TEXT("60 vs 120 fps converge"), FVector::Dist(At60, At120) < 4.f);
	// Micro-jitter inside the dead zone never moves the head.
	FSmoother S;
	S.Snap(FVector::ZeroVector, FQuat::Identity, 8.f);
	const FQuat Before = S.Rotation;
	S.Update(FVector::ZeroVector, FVector(10000.f, 1.f, 0.f), 8.f, 1.f / 60.f, 3.f, 6.f, 120.f, 15.f);
	TestTrue(TEXT("dead zone holds still"), Before.Equals(S.Rotation));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayTriggers, "CRICKET26.Broadcast.ReplayTriggers", CricketBroadcastTests::Flags)
bool FReplayTriggers::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	using namespace CricketBroadcastTests;
	auto Trigger = [&](const FDeliveryResult& R, const FDeliveryOutcome& O, bool bMile = false)
	{ return ClassifyReplayEvent(R, O, bMile); };

	FDeliveryResult Six = MockResult(EContactZone::Middle, FVector(20.f, -20.f, 14.f), 6, -1, false, false, 0.f, FVector::ZeroVector, 0);
	FReplayTrigger T = Trigger(Six, MockOutcome(6, EDismissal::None));
	TestEqual(TEXT("six is high"), int32(T.Priority), int32(EReplayPriority::High));

	FDeliveryResult Four = MockResult(EContactZone::Middle, FVector(22.f, 12.f, 2.f), 4, -1, false, false, 0.f, FVector::ZeroVector, 0);
	TestEqual(TEXT("four is medium"), int32(Trigger(Four, MockOutcome(4, EDismissal::None)).Priority), int32(EReplayPriority::Medium));

	FDeliveryResult Bowled = MockResult(EContactZone::Miss, FVector::ZeroVector, 0, -1, false, false, 0.f, FVector::ZeroVector, 0, true);
	TestEqual(TEXT("bowled is high"), int32(Trigger(Bowled, MockOutcome(0, EDismissal::Bowled)).Priority), int32(EReplayPriority::High));

	FDeliveryResult Caught = MockResult(EContactZone::Middle, FVector(14.f, 8.f, 10.f), 0, 5, true, true, 2.6f, FVector(35.f, 20.f, 0.f), 0);
	TestEqual(TEXT("caught is high"), int32(Trigger(Caught, MockOutcome(0, EDismissal::Caught)).Priority), int32(EReplayPriority::High));

	FDeliveryResult Dropped = MockResult(EContactZone::TopEdge, FVector(8.f, 5.f, 8.f), 0, 4, true, false, 2.4f, FVector(22.f, 14.f, 0.f), 1);
	TestEqual(TEXT("dropped catch replays"), int32(Trigger(Dropped, MockOutcome(1, EDismissal::None)).Priority), int32(EReplayPriority::Medium));

	FDeliveryResult Dot = MockResult(EContactZone::Miss, FVector::ZeroVector, 0, -1, false, false, 0.f, FVector::ZeroVector, 0);
	TestEqual(TEXT("dot ball never replays"), int32(Trigger(Dot, MockOutcome(0, EDismissal::None)).Priority), int32(EReplayPriority::None));

	FDeliveryResult Edge = MockResult(EContactZone::OutsideEdge, FVector(8.f, -6.f, 1.f), 0, 0, false, false, 0.8f, FVector(0.5f, -1.f, 0.f), 0);
	TestEqual(TEXT("survived edge is low"), int32(Trigger(Edge, MockOutcome(0, EDismissal::None)).Priority), int32(EReplayPriority::Low));

	TestEqual(TEXT("milestone is hero"), int32(Trigger(Four, MockOutcome(4, EDismissal::None), true).Priority), int32(EReplayPriority::Hero));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayPackages, "CRICKET26.Broadcast.ReplayPackages", CricketBroadcastTests::Flags)
bool FReplayPackages::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	using namespace CricketBroadcastTests;
	const FBroadcastTuning Tune = DefaultTune();
	TArray<EBroadcastShot> Recent;
	const FBroadcastFrame F = MockFrame();

	FDeliveryResult Six = MockResult(EContactZone::Middle, FVector(20.f, -20.f, 14.f), 6, -1, false, false, 0.f, FVector::ZeroVector, 0);
	FReplayPackage P = BuildReplayPackage(ClassifyReplayEvent(Six, MockOutcome(6, EDismissal::None), false), Six, F, Tune, Recent);
	TestTrue(TEXT("six package valid"), P.IsValid());
	TestEqual(TEXT("six gets three angles"), P.Angles.Num(), 3);
	TestTrue(TEXT("never opens tight"), P.Angles[0].Shot != EBroadcastShot::ReplaySlowMo && P.Angles[0].Shot != EBroadcastShot::Keeper);
	// The full replay leads: the whole ball, from the delivery stride to the ball over the rope, never squeezed.
	const FReplayAnglePlay& Full = P.Angles[0];
	TestTrue(TEXT("full replay leads"), Full.bFullPass);
	TestTrue(TEXT("full replay opens before release"), Full.StartTp < 0.f && Full.StartTp >= -1.f);
	TestTrue(TEXT("full replay reaches the rope"), Full.EndTp >= Six.ContactTime + Six.Fielding.BoundaryTime && Full.EndTp <= Six.DeadTime);
	TestTrue(TEXT("full replay plays at no more than real speed"), Full.WallTime >= Full.EndTp - Full.StartTp);
	TestTrue(TEXT("full replay is near real speed"), Full.WallTime < 1.5f * (Full.EndTp - Full.StartTp));
	for (int32 I = 1; I < P.Angles.Num(); ++I) TestFalse(TEXT("one full replay only"), P.Angles[I].bFullPass);

	FDeliveryResult Four = MockResult(EContactZone::Middle, FVector(22.f, 12.f, 2.f), 4, -1, false, false, 0.f, FVector::ZeroVector, 0);
	FReplayPackage P4 = BuildReplayPackage(ClassifyReplayEvent(Four, MockOutcome(4, EDismissal::None), false), Four, F, Tune, Recent);
	TestEqual(TEXT("four gets two angles"), P4.Angles.Num(), 2);
	TestTrue(TEXT("four's second angle is the stroke"), P4.Angles.IsValidIndex(1) && !P4.Angles[1].bFullPass && P4.Angles[1].DecisiveTp == Four.ContactTime);

	FDeliveryResult Edge = MockResult(EContactZone::OutsideEdge, FVector(8.f, -6.f, 1.f), 0, 0, false, false, 0.8f, FVector(0.5f, -1.f, 0.f), 0);
	FReplayPackage PE = BuildReplayPackage(ClassifyReplayEvent(Edge, MockOutcome(0, EDismissal::None), false), Edge, F, Tune, Recent);
	TestEqual(TEXT("edge gets the full replay and a closer look"), PE.Angles.Num(), 2);

	// Anti-repetition: rebuilding over history varies the package without invalid shots.
	const int32 RecentBefore = Recent.Num();
	FReplayPackage P2 = BuildReplayPackage(ClassifyReplayEvent(Six, MockOutcome(6, EDismissal::None), false), Six, F, Tune, Recent);
	TestTrue(TEXT("history grows"), Recent.Num() >= RecentBefore);
	for (const FReplayAnglePlay& A : P2.Angles)
	{
		TestTrue(TEXT("angle window ordered"), A.EndTp > A.StartTp);
		TestTrue(TEXT("decisive inside window"), A.DecisiveTp >= A.StartTp && A.DecisiveTp <= A.EndTp);
		TestTrue(TEXT("angle is a replay role"), A.Shot == EBroadcastShot::ReplayBeauty || A.Shot == EBroadcastShot::ReplaySlowMo
			|| A.Shot == EBroadcastShot::SideOn || A.Shot == EBroadcastShot::StraightOn || A.Shot == EBroadcastShot::AerialBall
			|| A.Shot == EBroadcastShot::Boundary || A.Shot == EBroadcastShot::OutfieldFollow || A.Shot == EBroadcastShot::StandardDelivery
			|| A.Shot == EBroadcastShot::AlternateDelivery || A.Shot == EBroadcastShot::Catch || A.Shot == EBroadcastShot::RunOut
			|| A.Shot == EBroadcastShot::Keeper || A.Shot == EBroadcastShot::Slip || A.Shot == EBroadcastShot::BatterEnd
			|| A.Shot == EBroadcastShot::GroundFollow);
	}

	// Slow-motion curve: exact endpoints, monotonic, slow at the moment, quick away from it.
	const FTimeRemap R = BuildTimeRemap(P.Angles[0]);
	TestEqual(TEXT("remap starts exact"), R.Sample(0.f), P.Angles[0].StartTp);
	TestTrue(TEXT("remap ends exact"), FMath::IsNearlyEqual(R.Sample(R.WallTime), P.Angles[0].EndTp, 1e-3f));
	float Prev = -1.f;
	bool bMono = true;
	for (int32 I = 0; I <= 40; ++I) { const float V = R.Sample(R.WallTime * I / 40.f); bMono &= V >= Prev - 1e-4f; Prev = V; }
	TestTrue(TEXT("remap monotonic"), bMono);
	// Wall-rate near the decisive moment is far below the approach rate.
	float WallAtDec = 0.f;
	for (int32 I = 0; I <= 100; ++I)
		if (FMath::Abs(R.Sample(R.WallTime * I / 100.f) - P.Angles[0].DecisiveTp) < 0.05f) { WallAtDec = R.WallTime * I / 100.f; break; }
	const float RateSlow = (R.Sample(FMath::Min(WallAtDec + 0.05f, R.WallTime)) - R.Sample(FMath::Max(WallAtDec - 0.05f, 0.f))) / 0.1f;
	const float RateEarly = (R.Sample(0.2f) - R.Sample(0.f)) / 0.2f;
	AddInfo(FString::Printf(TEXT("rate at moment %.2f vs approach %.2f"), RateSlow, RateEarly));
	TestTrue(TEXT("slow through the moment"), RateSlow < RateEarly * 0.75f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReplayBuffer, "CRICKET26.Broadcast.ReplayBuffer", CricketBroadcastTests::Flags)
bool FReplayBuffer::RunTest(const FString&)
{
	FCricketReplayBuffer Buf;
	Buf.Window = 2.f;
	Buf.Rate = 30.f;
	Buf.MaxActors = 4;
	TArray<FReplayActorPose> Poses;
	Poses.SetNum(4);
	// A ball flying straight at 20 m/s, actors parked: record at 60 Hz calls, buffer keeps 30 Hz.
	for (int32 I = 0; I <= 180; ++I)
	{
		const float T = I / 60.f;
		Poses[0].Position = FVector(T * 2000.f, 0.f, 100.f);
		Buf.Record(T, FVector(T * 2000.f, 0.f, 100.f), FVector(2000.f, 0.f, 0.f), Poses);
	}
	AddInfo(FString::Printf(TEXT("frames %d [%.2f, %.2f] footprint %d B"), Buf.NumFrames(), Buf.EarliestT(), Buf.LatestT(), int32(Buf.FootprintBytes())));
	TestTrue(TEXT("throttled to rate"), Buf.NumFrames() >= 60 && Buf.NumFrames() <= 64);
	TestTrue(TEXT("bounded window"), Buf.EarliestT() >= 0.9f && Buf.LatestT() <= 3.01f);
	TestTrue(TEXT("coverage inside"), Buf.HasCoverage(1.2f, 2.4f));
	TestFalse(TEXT("no coverage outside"), Buf.HasCoverage(2.9f, 3.5f));
	FVector Ball;
	TArray<FReplayActorPose> Out;
	TestTrue(TEXT("samples"), Buf.SampleAt(1.5f, Ball, Out));
	TestTrue(TEXT("interpolates linear flight"), FVector::Dist(Ball, FVector(3000.f, 0.f, 100.f)) < 40.f);
	TestEqual(TEXT("actor count preserved"), Out.Num(), 4);
	TestTrue(TEXT("mobile footprint"), Buf.FootprintBytes() < 512 * 1024);
	Buf.Reset();
	TestEqual(TEXT("reset clears"), Buf.NumFrames(), 0);
	TestFalse(TEXT("no coverage after reset"), Buf.HasCoverage(0.f, 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalBallCoverage, "CRICKET26.Broadcast.NormalBallCoverage", CricketBroadcastTests::Flags)
bool FNormalBallCoverage::RunTest(const FString&)
{
	using namespace CricketBroadcast;
	using namespace CricketBroadcastTests;
	const FBroadcastTuning Tune = DefaultTune();
	// Regression: a ball that was neither a boundary nor a wicket was punched in to the low fielder camera as the
	// stop came (a keeper's take after a miss put the lens at the batter looking back at the keeper), and every
	// run cut to the crease camera. Both sit at head height looking out at the boards: the frame fell to the turf
	// with no pitch or players in it. Cricket 24 holds the high reverse angle through the stop, throw and runs.

	// Classification: a low edge someone else fields is an outfield ball; an edged four is a boundary; byes run.
	TestEqual(TEXT("edge to third man is not the keeper's"), int32(ClassifyShot(MockResult(EContactZone::OutsideEdge, FVector(-12.f, 6.f, 1.f), 0, 6, false, false, 2.f, FVector(-30.f, 20.f, 0.f), 1), EDismissal::None)), int32(EShotClass::RunningPlay));
	TestEqual(TEXT("edged four"), int32(ClassifyShot(MockResult(EContactZone::OutsideEdge, FVector(-18.f, 8.f, 1.f), 4, -1, false, false, 0.f, FVector::ZeroVector, 0), EDismissal::None)), int32(EShotClass::BoundaryTrajectory));
	TestEqual(TEXT("byes run"), int32(ClassifyShot(MockResult(EContactZone::Miss, FVector::ZeroVector, 0, 6, false, false, 2.f, FVector(-30.f, 20.f, 0.f), 1), EDismissal::None)), int32(EShotClass::RunningPlay));
	TestEqual(TEXT("keeper takes the miss"), int32(ClassifyShot(MockResult(EContactZone::Miss, FVector::ZeroVector, 0, 0, false, false, 0.1f, FVector(-15.f, 0.f, 1.f), 0), EDismissal::None)), int32(EShotClass::NoContact));

	// Selection over the whole live ball: never the low fielder or crease camera without a wicket or a run-out on.
	FBroadcastFrame F = MockFrame();
	F.bHasFielder = true;
	F.FieldTime = 1.2f;
	F.ThrowTime = 1.6f;
	for (const EShotClass Class : { EShotClass::GroundInfield, EShotClass::GroundOutfield, EShotClass::LoftedInfield,
		EShotClass::HighCatchChance, EShotClass::LoftedOutfield, EShotClass::RunningPlay })
		for (float After = 0.3f; After < 6.f; After += 0.1f)
		{
			F.ShotClass = Class;
			F.AfterContact = After;
			TestEqual(FString::Printf(TEXT("class %d at %.1f s held on the reverse angle"), int32(Class), After),
				int32(SelectLiveShot(EBroadcastPhase::BallInPlay, F)), int32(EBroadcastShot::OutfieldFollow));
		}
	F.ShotClass = EShotClass::NoContact;
	F.FieldTime = 0.1f;
	for (float After = -0.2f; After < 2.f; After += 0.1f)
	{
		F.AfterContact = After;
		TestEqual(TEXT("a miss holds the delivery lens through the take"), int32(SelectLiveShot(EBroadcastPhase::BallInPlay, F)), int32(EBroadcastShot::StandardDelivery));
	}
	// A run-out that is really on: the reverse angle until the throw is away, then the crease at the throw's end.
	F.ShotClass = EShotClass::RunningPlay;
	F.bRunOutChance = true;
	F.AfterContact = 1.f;
	TestEqual(TEXT("chase on the reverse angle"), int32(SelectLiveShot(EBroadcastPhase::BallInPlay, F)), int32(EBroadcastShot::OutfieldFollow));
	F.AfterContact = 1.5f;
	TestEqual(TEXT("throw away: crease camera"), int32(SelectLiveShot(EBroadcastPhase::BallInPlay, F)), int32(EBroadcastShot::RunOut));
	F.bHasThrowEnd = true;
	F.bThrowToStrikerEnd = false;
	F.StrikerPos = FVector(200.f, 100.f, 90.f); // the runner still near the striker's end
	const FLiveCameraSolution RO = SolveShotGeometry(EBroadcastShot::RunOut, F, Tune);
	TestTrue(TEXT("crease camera at the end the throw goes to"), RO.LookAt.X > 50.f * CricketGeo::PitchLength);

	// Framing: an ordinary ball hit anywhere round the ring keeps pitch, striker and ball in shot from a high
	// camera looking over the field, never down at the turf.
	auto InFrame = [](const FLiveCameraSolution& S, const FVector& P, float Margin)
	{
		const FVector Fwd = (S.LookAt - S.Location).GetSafeNormal();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd).GetSafeNormal(), Up = FVector::CrossProduct(Fwd, Right);
		const FVector To = P - S.Location;
		const float Depth = FVector::DotProduct(To, Fwd);
		const float HTan = FMath::Tan(FMath::DegreesToRadians(S.FOV) * 0.5f), VTan = HTan * 9.f / 16.f;
		return Depth > 0.f && FMath::Abs(FVector::DotProduct(To, Right) / Depth / HTan) < Margin
			&& FMath::Abs(FVector::DotProduct(To, Up) / Depth / VTan) < Margin;
	};
	const FVector Centre(50.f * CricketGeo::PitchLength, 0.f, 0.f);
	for (int32 Deg = 0; Deg < 360; Deg += 30)
		for (const float Metres : { 12.f, 25.f, 45.f })
		{
			const FVector2D Dir(FMath::Cos(FMath::DegreesToRadians(float(Deg))), FMath::Sin(FMath::DegreesToRadians(float(Deg))));
			FBroadcastFrame G = MockFrame();
			G.ExitVel = FVector(Dir.X * 15.f, Dir.Y * 15.f, 0.5f);
			G.BallPos = FVector(120.f + Dir.X * Metres * 100.f, Dir.Y * Metres * 100.f, 10.f);
			G.PredictedPos = G.BallPos + FVector(Dir * 150.f, 0.f);
			G.bHasPrediction = true;
			const FLiveCameraSolution S = SolveShotGeometry(EBroadcastShot::OutfieldFollow, G, Tune);
			const FString What = FString::Printf(TEXT("%d deg, %.0f m"), Deg, Metres);
			TestTrue(What + TEXT(": high camera"), S.Location.Z > 1000.f);
			TestTrue(What + TEXT(": ball in shot"), InFrame(S, G.BallPos, 0.9f));
			TestTrue(What + TEXT(": striker in shot"), InFrame(S, G.StrikerPos, 1.f));
			TestTrue(What + TEXT(": pitch in shot"), InFrame(S, Centre, 1.f));
			const float Dip = FMath::RadiansToDegrees(FMath::Asin(-(S.LookAt - S.Location).GetSafeNormal().Z));
			TestTrue(What + TEXT(": looking over the field, not at the turf"), Dip > 0.f && Dip < 20.f);
		}
	return true;
}

#endif
