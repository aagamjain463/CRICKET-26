// Keeper selection, positioning and synchronization tests. Pure logic: no
// animation blueprint, no rendering, no ball physics changes.

#include "Misc/AutomationTest.h"
#include "CricketKeeper.h"
#include "FieldingModel.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CricketKeeperTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKeeperDepth, "CRICKET26.Keeper.Depth", CricketKeeperTests::Flags)
bool FKeeperDepth::RunTest(const FString&)
{
	using namespace CricketKeeper;
	// Pace stands back, spin stands up: never one universal distance.
	const float PaceDeep = KeeperDepth(EBowlerType::Pace, 135.f);
	const float SpinUp = KeeperDepth(EBowlerType::OffSpin, 88.f);
	TestTrue(TEXT("pace stands back"), PaceDeep > 10.f);
	TestTrue(TEXT("spin stands up"), SpinUp < 2.f);
	TestTrue(TEXT("pace deeper than spin"), PaceDeep > SpinUp + 5.f);
	// Medium / slower pace reads as an intermediate depth, not identical.
	const float Medium = KeeperDepth(EBowlerType::Pace, 110.f);
	const float Express = KeeperDepth(EBowlerType::Pace, 145.f);
	TestTrue(TEXT("medium up from express"), Medium < Express);
	// Standing back is where the stock ball arrives at the gloves, knee to waist, off every good length. At 15.6 m a
	// 135 kph ball off 7 m had come down to 6 cm: every take went to the grass.
	for (const float Kph : { 120.f, 130.f, 135.f, 140.f, 145.f, 150.f })
		for (const float Length : { 5.f, 6.f, 7.f, 8.f })
		{
			FCricketPlayer Bowler;
			Bowler.BowlerType = EBowlerType::Pace;
			Bowler.Accuracy = 1.f;
			Bowler.PaceKph = Kph;
			FDeliveryPlan Plan;
			Plan.Length = Length;
			Plan.Line = 0.5f;
			FBallState B = CricketBowling::Execute(Bowler, ECricketHand::Right, Plan, 0.f, 3, FPitchConditions()).Ball;
			const float Gloves = -KeeperDepth(EBowlerType::Pace, Kph) + ReadyFor(false).GloveForward;
			const bool bCarried = CricketBall::SimulateToPlane(B, Gloves, FPitchConditions()) && B.Bounces == 1;
			TestTrue(*FString::Printf(TEXT("%.0f kph off %.0f m carries to the gloves at %.2f m"), Kph, Length, B.Pos.Z), bCarried && B.Pos.Z > 0.3f && B.Pos.Z < 1.1f);
		}
	// Homes align behind the stumps, off-side, without clipping them.
	const FVector2D HomeR = KeeperHome(EBowlerType::Pace, 135.f, ECricketHand::Right);
	const FVector2D HomeL = KeeperHome(EBowlerType::Pace, 135.f, ECricketHand::Left);
	TestTrue(TEXT("pace home behind"), HomeR.X < -5.f);
	TestTrue(TEXT("right-hander off side +"), HomeR.Y > 0.f);
	TestTrue(TEXT("left-hander mirrors"), HomeL.Y < 0.f && FMath::IsNearlyEqual(FMath::Abs(HomeL.Y), HomeR.Y, 1e-3f));
	const FVector2D SpinHome = KeeperHome(EBowlerType::LegSpin, 85.f, ECricketHand::Right);
	TestTrue(TEXT("spin home close"), SpinHome.Size() < 3.f && IsStandingUp(SpinHome));
	TestFalse(TEXT("pace not standing up"), IsStandingUp(HomeR));
	// Field presets agree: pace back, spin up.
	const TArray<FFielder> PaceField = CricketField::Make(EFieldPreset::PaceDeath, ECricketHand::Right, ECricketHand::Right);
	const TArray<FFielder> SpinField = CricketField::Make(EFieldPreset::SpinDefensive, ECricketHand::Right, ECricketHand::Right);
	TestTrue(TEXT("pace preset keeper back"), PaceField[0].bKeeper && PaceField[0].Home.X < -5.f);
	TestTrue(TEXT("spin preset keeper up"), SpinField[0].bKeeper && SpinField[0].Home.Size() < 3.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKeeperClassify, "CRICKET26.Keeper.Classify", CricketKeeperTests::Flags)
bool FKeeperClassify::RunTest(const FString&)
{
	using namespace CricketKeeper;
	const FVector2D Home(-15.5f, 0.45f); // pace, right-hander
	auto Sel = [&](FVector Take, bool bDive, bool bContact, bool bBounced, bool bUp, float T = 0.6f)
	{
		return Classify(Home, Take, true, bDive, bContact, bBounced, bUp, T, 1.f);
	};
	// Golden central pace take.
	{
		const FKeeperSelection S = Sel(FVector(-15.5f, 0.45f, 0.75f), false, false, false, false);
		TestEqual(TEXT("central chest"), S.Family, EKeeperTake::CentralChest);
		TestEqual(TEXT("reachable"), S.Reach, EKeeperReach::Reachable);
		TestEqual(TEXT("feet set"), S.Footwork, EKeeperFootwork::Set);
		TestFalse(TEXT("no miss"), S.bUnreachableMiss);
	}
	// Low central goes through the legs, not the spine.
	{
		const FKeeperSelection S = Sel(FVector(-15.5f, 0.45f, 0.2f), false, false, true, false);
		TestTrue(TEXT("low take"), S.Family == EKeeperTake::LowCentral || S.Family == EKeeperTake::BounceTake);
	}
	// High central.
	{
		const FKeeperSelection S = Sel(FVector(-15.5f, 0.45f, 1.55f), false, false, false, false);
		TestEqual(TEXT("high central"), S.Family, EKeeperTake::HighCentral);
	}
	// Small vs lateral vs dive tiers, off side.
	{
		const FKeeperSelection Small = Sel(FVector(-15.5f, 1.1f, 0.75f), false, false, false, false);
		TestTrue(TEXT("small off shuffles"), (Small.Family == EKeeperTake::SmallOff) && Small.Footwork == EKeeperFootwork::Shuffle);
		const FKeeperSelection Lat = Sel(FVector(-15.5f, 1.8f, 0.75f), false, false, false, false);
		TestTrue(TEXT("lateral off crosses"), (Lat.Family == EKeeperTake::LateralOff) && Lat.Footwork == EKeeperFootwork::Crossover);
		const FKeeperSelection Dive = Sel(FVector(-15.5f, 2.6f, 0.9f), true, false, false, false);
		TestTrue(TEXT("dive off"), (Dive.Family == EKeeperTake::DiveOff) && Dive.Footwork == EKeeperFootwork::PushDive);
	}
	// Leg side stays distinct (never blindly mirrored).
	{
		const FKeeperSelection S = Sel(FVector(-15.5f, -0.9f, 0.75f), false, false, false, false);
		TestTrue(TEXT("leg take distinct"), S.Family == EKeeperTake::SmallLeg || S.Family == EKeeperTake::LateralLeg);
		TestTrue(TEXT("leg lateral negative"), S.Lateral < 0.f);
	}
	// Solver owns reachability after the keeper's run; home distance only
	// selects footwork. An assigned long chase must not become a visual miss.
	{
		const FKeeperSelection Chase = Sel(FVector(-15.5f, 4.5f, 0.8f), false, false, false, false);
		TestEqual(TEXT("long chase still reachable"), Chase.Reach, EKeeperReach::Reachable);
		TestFalse(TEXT("long chase not miss"), Chase.bUnreachableMiss);
		const FKeeperSelection Miss = Classify(FVector2D(-15.5f, 0.45f), FVector(-15.5f, 4.5f, 0.8f), false, false, false, false, false, 0.5f, 1.f);
		TestEqual(TEXT("unassigned ball is miss"), Miss.Reach, EKeeperReach::Unreachable);
		FVector Gloves;
		const float Clamp = GloveTarget(Miss, FVector(-15.5f, 0.45f, 1.0f), FVector(-15.5f, 4.5f, 0.8f), Gloves);
		TestTrue(TEXT("miss gloves capped"), Clamp > 0.5f && FVector::Dist(Gloves, FVector(-15.5f, 0.45f, 1.0f)) <= KeeperGloveReach + 0.01f);
	}
	{
		const FKeeperSelection SpinDive = Sel(FVector(-0.9f, 2.6f, 0.9f), true, false, false, true);
		TestEqual(TEXT("spin dive keeps push-off"), SpinDive.Footwork, EKeeperFootwork::PushDive);
		TestFalse(TEXT("central take stays grounded"), UseSideDive(FVector2D(-15.5f, 0.45f), FVector(-15.0f, 0.7f, 0.2f), true));
		TestFalse(TEXT("low take stays below ball"), UseSideDive(FVector2D(-15.5f, 0.45f), FVector(-15.0f, 2.0f, 0.2f), true));
		TestTrue(TEXT("wide catch uses side dive"), UseSideDive(FVector2D(-15.5f, 0.45f), FVector(-15.0f, 2.0f, 0.9f), true));
	}
	// Reachable: gloves meet the REAL ball, zero clamp.
	{
		const FKeeperSelection S = Sel(FVector(-15.5f, 0.45f, 0.75f), false, false, false, false);
		FVector Gloves;
		TestEqual(TEXT("gloves on ball"), GloveTarget(S, FVector(-15.5f, 0.45f, 1.0f), FVector(-15.5f, 0.45f, 0.75f), Gloves), 0.f);
		TestTrue(TEXT("gloves meet ball"), Gloves.Equals(FVector(-15.5f, 0.45f, 0.75f), 1e-3f));
		const float Far = GloveTarget(S, FVector(-15.5f, 0.45f, 1.0f), FVector(-15.5f, 2.45f, 0.75f), Gloves);
		TestTrue(TEXT("far target cannot stretch arm"), Far > 1.f && FVector::Dist(Gloves, FVector(-15.5f, 0.45f, 1.0f)) <= KeeperGloveReach + 0.01f);
	}
	// Thin edge: late sharp reaction, no dive. Large deviation: dive.
	{
		const FKeeperSelection Thin = Sel(FVector(-15.5f, 1.0f, 1.0f), false, true, false, false);
		TestEqual(TEXT("thin edge catch"), Thin.Family, EKeeperTake::EdgeCatch);
		const FKeeperSelection Wide = Sel(FVector(-15.5f, 2.6f, 1.0f), true, true, false, false);
		TestEqual(TEXT("wide edge dive"), Wide.Family, EKeeperTake::EdgeDive);
	}
	// Spin standing up is its own family, leg turn distinct.
	{
		const FVector2D Up(-0.9f, 0.28f);
		const FKeeperSelection S = Classify(Up, FVector(-0.9f, 0.3f, 0.5f), true, false, false, false, true, 0.5f, 1.f);
		TestEqual(TEXT("spin take"), S.Family, EKeeperTake::SpinTake);
		const FKeeperSelection Leg = Classify(Up, FVector(-0.9f, -0.5f, 0.4f), true, false, false, false, true, 0.5f, 1.f);
		TestEqual(TEXT("spin leg take"), Leg.Family, EKeeperTake::SpinLegTake);
	}
	// Not the keeper's ball: hold, never stretch.
	{
		const FKeeperSelection S = Classify(Home, FVector(10.f, 5.f, 0.5f), false, false, false, false, false, 0.6f, 1.f);
		TestEqual(TEXT("not keeper holds"), S.Family, EKeeperTake::Miss);
		TestTrue(TEXT("hold footwork"), S.Footwork == EKeeperFootwork::Hold);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKeeperSync, "CRICKET26.Keeper.Sync", CricketKeeperTests::Flags)
bool FKeeperSync::RunTest(const FString&)
{
	using namespace CricketKeeper;
	// Stumping: possession first, then a quick economical transfer.
	TestEqual(TEXT("no transfer before take"), StumpingProgress(0.5f, 0.6f, 0.85f), 0.f);
	TestTrue(TEXT("transfer completes by break"), StumpingProgress(0.85f, 0.6f, 0.85f) >= 1.f);
	TestTrue(TEXT("mid transfer partial"), StumpingProgress(0.65f, 0.6f, 0.85f) > 0.f && StumpingProgress(0.65f, 0.6f, 0.85f) < 1.f);
	TestEqual(TEXT("no break no transfer"), StumpingProgress(0.9f, 0.6f, -1.f), 0.f);
	// Ready stance: standing up lower and closer than standing back.
	const FKeeperReady Back = ReadyFor(false), Up = ReadyFor(true);
	TestTrue(TEXT("up lower"), Up.Drop > Back.Drop);
	TestTrue(TEXT("up gloves lower"), Up.GloveHeight < Back.GloveHeight);
	// Stance: relaxed at the mark, a deep squat by release, up with the bounce, never a jump.
	for (const bool bUp : { false, true })
	{
		TestEqual(TEXT("relaxed at mark"), StanceDrop(bUp, -100.f, -1.f), RelaxedDrop);
		TestTrue(TEXT("squats for release"), StanceDrop(bUp, -0.2f, 0.4f) > ReadyFor(bUp).Drop + 0.15f);
		TestTrue(TEXT("rises with the bounce"), FMath::IsNearlyEqual(StanceDrop(bUp, 0.5f, 0.4f), ReadyFor(bUp).Drop, 1e-3f));
		float Last = StanceDrop(bUp, -2.f, 0.4f), Worst = 0.f;
		for (float T = -2.f; T < 1.f; T += 1.f / 120.f)
		{
			Worst = FMath::Max(Worst, FMath::Abs(StanceDrop(bUp, T, 0.4f) - Last));
			Last = StanceDrop(bUp, T, 0.4f);
		}
		TestTrue(TEXT("no snap"), Worst < 0.012f);
	}
	TestTrue(TEXT("spin squats lower"), StanceDrop(true, -0.2f, 0.4f) > StanceDrop(false, -0.2f, 0.4f));
	// Selection log carries trajectory, family, animation and IK.
	const FKeeperSelection S = Classify(FVector2D(-15.5f, 0.45f), FVector(-15.5f, 0.45f, 0.75f), true, false, false, false, false, 0.6f, 1.f);
	const FString Log = SelectionLog(S, FVector(-15.5f, 0.45f, 0.75f), 15.5f, EBowlerType::Pace);
	TestTrue(TEXT("log names family"), Log.Contains(TEXT("CentralChest")));
	TestTrue(TEXT("log names anim"), Log.Contains(TEXT("KeeperCentralTake")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKeeperFootwork, "CRICKET26.Keeper.Footwork", CricketKeeperTests::Flags)
bool FKeeperFootwork::RunTest(const FString&)
{
	using namespace CricketKeeper;
	// Regression: the shuffle played a walk clip under a crouched body, so the
	// feet slid and lost phase with the travel. Now a foot only moves while
	// lifted, one at a time, the lead foot first.
	const FVector2D Fwd(1.f, 0.f);
	FVector2D Ideal[2];
	float Lift[2];
	FKeeperFeet Feet;
	StanceFeet(FVector2D(-15.5f, 0.45f), Fwd, 0.4f, Ideal);
	TestTrue(TEXT("squat stance wide"), FVector2D::Distance(Ideal[0], Ideal[1]) > 0.5f);
	TestTrue(TEXT("left foot on the left"), Ideal[0].Y < Ideal[1].Y);
	StepFeet(Feet, Ideal, 1.f / 60.f, Lift);
	TestTrue(TEXT("first frame snaps"), Feet.Ball[0].Equals(Ideal[0]) && Lift[0] == 0.f);
	// Shuffle 0.55 m to the off side (+Y) over 0.4 s, the solver's small lateral take.
	const float Dt = 1.f / 120.f;
	int32 FirstStep = -1, Slides = 0, BothUp = 0;
	FVector2D Prev[2] = { Feet.Ball[0], Feet.Ball[1] };
	bool bWasPlanted[2] = { true, true };
	for (int32 K = 1; K <= 120; ++K)
	{
		const float Y = 0.45f + 0.55f * FMath::SmoothStep(0.f, 1.f, FMath::Min(K * Dt / 0.4f, 1.f));
		StanceFeet(FVector2D(-15.5f, Y), Fwd, 0.4f, Ideal);
		StepFeet(Feet, Ideal, Dt, Lift);
		for (int32 F = 0; F < 2; ++F)
		{
			if (FirstStep < 0 && Lift[F] > 0.f) FirstStep = F;
			Slides += bWasPlanted[F] && Feet.Phase[F] >= 1.f && FVector2D::Distance(Feet.Ball[F], Prev[F]) > 1e-4f;
			bWasPlanted[F] = Feet.Phase[F] >= 1.f;
			Prev[F] = Feet.Ball[F];
		}
		BothUp += Lift[0] > 0.f && Lift[1] > 0.f && FMath::Min(Lift[0], Lift[1]) > 0.5f * StepLift;
	}
	TestEqual(TEXT("lead (right) foot steps first"), FirstStep, 1);
	TestEqual(TEXT("planted feet never slide"), Slides, 0);
	TestEqual(TEXT("never both feet high off the ground"), BothUp, 0);
	TestTrue(TEXT("feet arrive"), Feet.Ball[0].Equals(Ideal[0], 1e-3f) && Feet.Ball[1].Equals(Ideal[1], 1e-3f));
	TestTrue(TEXT("planted at rest"), Feet.Phase[0] >= 1.f && Feet.Phase[1] >= 1.f);
	// Standing still: no steps at all (no fidget from the breathing).
	FKeeperFeet Still;
	StanceFeet(FVector2D(-0.9f, 0.28f), Fwd, 0.47f, Ideal);
	StepFeet(Still, Ideal, Dt, Lift);
	int32 Steps = 0;
	for (int32 K = 0; K < 240; ++K)
	{
		FVector2D Wobble[2] = { Ideal[0] + FVector2D(0.01f * FMath::Sin(K * 0.1f), 0.f), Ideal[1] };
		StepFeet(Still, Wobble, Dt, Lift);
		Steps += Lift[0] > 0.f || Lift[1] > 0.f;
	}
	TestEqual(TEXT("no fidget steps"), Steps, 0);
	// A cut or replay jump snaps rather than walking across the field.
	StanceFeet(FVector2D(-15.5f, 0.45f), Fwd, 0.4f, Ideal);
	StepFeet(Still, Ideal, Dt, Lift);
	TestTrue(TEXT("jump snaps"), Still.Ball[1].Equals(Ideal[1]) && Lift[1] == 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKeeperTakeBody, "CRICKET26.Keeper.TakeBody", CricketKeeperTests::Flags)
bool FKeeperTakeBody::RunTest(const FString&)
{
	using namespace CricketKeeper;
	// The solver hands the keeper a ball once within KeeperReach and sends the run at the ball itself. A wide one
	// on the feet (the -CricketLine=1.3 capture) had the body 1.2 m short at the take, the gloves clamped, and then
	// the keeper ran on 1.7 m through the take point in the crouch after the ball was already in the gloves.
	FFielder Keeper;
	Keeper.Home = FVector2D(-12.7f, 0.45f);
	Keeper.bKeeper = true;
	const float Speed = 7.f;
	for (const float Wide : { 0.2f, 0.5f, 0.9f, 1.3f, 1.91f })
		for (const float Sign : { 1.f, -1.f })
		{
			const FVector Take(-12.3f, Keeper.Home.Y + Sign * Wide, 0.43f);
			const FFielderMove Run{ 0, EFieldRole::Primary, 0.15f, FVector2D(Take) };
			const float D = FVector2D::Distance(FVector2D(Take), Keeper.Home);
			const float FieldTime = 0.15f + CricketField::TimeToCover(FMath::Max(0.f, D - KeeperReach), Speed);
			const FVector2D AtTake = TakeBodyAt(&Run, Keeper, Speed, Take, FieldTime, FieldTime - 0.45f, FieldTime);
			const float Left = (Take.Y - AtTake.Y) * Sign;
			TestTrue(*FString::Printf(TEXT("%+.2f m: gloves reach from the body (%.2f m left)"), Sign * Wide, Left), Left <= KeeperArmsLength + 1e-3f);
			TestTrue(*FString::Printf(TEXT("%+.2f m: never past the ball"), Sign * Wide), Left >= -1e-3f);
			// The +1.9 m side-step runs ~4 m/s; on the jog clip the crouching keeper folded to the grass for a frame.
			TestTrue(*FString::Printf(TEXT("%+.2f m: shuffled across, off the locomotion clips"), Sign * Wide),
				ShufflesToTake(Keeper.Home, AtTake, FieldTime, FieldTime + 0.5f));
			if (Wide <= BehindTheLine)
				TestTrue(*FString::Printf(TEXT("%+.2f m: stepped fully behind"), Sign * Wide), FMath::Abs(Left) < 1e-3f);
			for (const float After : { 0.1f, 0.5f, 1.5f })
			{
				const FVector2D Later = TakeBodyAt(&Run, Keeper, Speed, Take, FieldTime, FieldTime - 0.45f, FieldTime + After);
				TestTrue(*FString::Printf(TEXT("%+.2f m: stops with the ball in the gloves (+%.1f s moved %.2f m)"), Sign * Wide, After,
					FVector2D::Distance(Later, AtTake)), Later.Equals(AtTake, 1e-3f));
			}
		}
	// The run the solver hands over gets the keeper within reach by the take. A beaten ball is read off the pitch,
	// so the solver timed the keeper off KeeperLead early; the run started 0.15 s after the bat regardless and a
	// wide one was 1.9 m out at the take.
	for (const float Lead : { 0.f, 0.3f, 0.6f })
		for (const float Wide : { 0.f, 1.f, 1.9f, 2.4f })
		{
			const TArray<FFielder> Field = CricketField::Make(EFieldPreset::PaceDeath, ECricketHand::Right, ECricketHand::Right);
			TArray<FBallState> Samples;
			const float Dt = 0.01f;
			for (int32 K = 0; K < 60; ++K)
			{
				FBallState B;
				B.Pos = FVector(-38.f * K * Dt, Field[0].Home.Y + Wide, 0.6f);
				B.Vel = FVector(-38.f, 0.f, 0.f);
				B.Bounces = 1;
				Samples.Add(B);
			}
			FCricketPlayer Skill;
			FRandomStream Rng(7);
			const FFieldingOutcome O = CricketField::SolveFielding(Samples, Dt, Field, Skill, false, Rng, Lead);
			const FFielderMove* Run = O.Moves.FindByPredicate([](const FFielderMove& M) { return M.Fielder == 0; });
			if (O.Fielder != 0 || O.bDive || !Run) continue;
			const float Short = FVector2D::Distance(CricketField::PositionOf(*Run, Field[0], O.FieldTime, Skill.RunSpeed), FVector2D(O.FieldPos));
			TestTrue(*FString::Printf(TEXT("lead %.1f s, %.1f m wide: within reach at the take (%.2f m)"), Lead, Wide, Short), Short <= KeeperReach + 0.02f);
		}
	// Down with a low ball, back up once it is secured. The sink followed the approach, which stays at 1 after the
	// take, so a keeper who took one off the grass stayed squatting at the full 0.5 m drop until the next ball, a knee on the grass.
	for (const float Height : { 0.12f, 0.3f, 0.7f, 1.4f })
	{
		const float Ready = ReadyFor(false).Drop;
		const float In = TakeDrop(Ready, Ready, Height, 1.f);
		const float Out = TakeDrop(Ready, Ready, Height, 0.f);
		TestTrue(*FString::Printf(TEXT("%.2f m: back to the ready drop once secured (%.2f)"), Height, Out), FMath::IsNearlyEqual(Out, Ready));
		if (Height < 0.45f) TestTrue(*FString::Printf(TEXT("%.2f m: sinks with a low ball (%.2f)"), Height, In), In > Ready + 0.1f && In <= MaxTakeDrop);
		if (Height > 1.1f) TestTrue(*FString::Printf(TEXT("%.2f m: stands taller for a high one"), Height), In < Ready);
	}
	// No run: the keeper stays home, and only steps across over the window.
	const FVector Near(-12.3f, 0.75f, 0.7f);
	TestTrue(TEXT("home before the step"), TakeBodyAt(nullptr, Keeper, Speed, Near, 0.6f, 0.15f, 0.f).Equals(Keeper.Home));
	TestTrue(TEXT("behind the line at the take"), FMath::IsNearlyEqual(TakeBodyAt(nullptr, Keeper, Speed, Near, 0.6f, 0.15f, 0.6f).Y, 0.75f, 1e-3f));
	// Locomotion is back once the get-up is done, and a skier 10 m away is run to, not shuffled.
	TestFalse(TEXT("walks again after the get-up"), ShufflesToTake(Keeper.Home, Keeper.Home, 0.6f, 0.6f + 0.9f));
	TestFalse(TEXT("runs to a skier"), ShufflesToTake(Keeper.Home, Keeper.Home + FVector2D(8.f, 6.f), 1.8f, 1.f));
	return true;
}

#endif
