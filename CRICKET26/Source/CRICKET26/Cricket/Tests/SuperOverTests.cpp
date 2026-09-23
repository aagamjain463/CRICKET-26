// Gate 1 automation tests: rules, strike, scoring, state, ball physics, batting contact, fielding.
// Run: UnrealEditor-Cmd CRICKET26.uproject -ExecCmds="Automation RunTests CRICKET26.; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "CricketAI.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags CricketTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FDeliveryOutcome Runs(int32 N) { FDeliveryOutcome O; O.bBatContact = true; O.RunsRun = N; return O; }
	FDeliveryOutcome Four() { FDeliveryOutcome O; O.bBatContact = true; O.Boundary = 4; return O; }
	FDeliveryOutcome Six() { FDeliveryOutcome O; O.bBatContact = true; O.Boundary = 6; return O; }
	FDeliveryOutcome Out(EDismissal D) { FDeliveryOutcome O; O.Dismissal = D; O.bBatContact = D == EDismissal::Caught; return O; }
	FDeliveryOutcome Wide() { FDeliveryOutcome O; O.bWide = true; return O; }
	FDeliveryOutcome NoBall(int32 R = 0) { FDeliveryOutcome O; O.bNoBall = true; O.bBatContact = R > 0; O.RunsRun = R; return O; }

	bool Bowl(FSuperOverMatch& M, const FDeliveryOutcome& O, TArray<ECricketEvent>* Ev = nullptr)
	{
		TArray<ECricketEvent> Local;
		return M.BeginDelivery() && M.CompleteDelivery(O, Ev ? *Ev : Local);
	}

	FResolveContext Ctx(EBowlerType Type = EBowlerType::Pace, int32 Seed = 1)
	{
		FResolveContext C;
		C.Bowler.BowlerType = Type;
		C.Field = CricketField::Make(CricketField::PresetFor(Type), C.Striker.BatHand, C.Bowler.BowlHand);
		C.Seed = Seed;
		return C;
	}

	FDeliveryRelease Release(EDeliveryType Type, float Length, float Line = 0.f, float Timing = 0.f,
		EBowlerType Style = EBowlerType::Pace, int32 Seed = 3)
	{
		FCricketPlayer Bowler;
		Bowler.BowlerType = Style;
		Bowler.Accuracy = 1.f; // no scatter: tests measure the physics, not execution error
		if (Style != EBowlerType::Pace) Bowler.PaceKph = 88.f;
		FDeliveryPlan Plan;
		Plan.Type = Type;
		Plan.Length = Length;
		Plan.Line = Line;
		return CricketBowling::Execute(Bowler, ECricketHand::Right, Plan, Timing, Seed, FPitchConditions());
	}

	/** Ball state at the striker's popping crease, and where it pitched. */
	FBallState AtCrease(const FDeliveryRelease& R, FVector* OutPitch = nullptr)
	{
		FBallState B = R.Ball;
		const FPitchConditions C;
		while (B.Pos.X > CricketGeo::PoppingCrease && B.Time < 3.f)
		{
			if (CricketBall::Step(B, C) == CricketBall::EStep::Bounce && B.Bounces == 1 && OutPitch) *OutPitch = B.Pos;
		}
		return B;
	}

	/** Perfectly timed input for a given intent and direction. */
	FBatInput Perfect(const FDeliveryRelease& R, EBatIntent Intent, float Dir, EBowlerType Style = EBowlerType::Pace)
	{
		const FPitchConditions C;
		const FBallRead Read = CricketDelivery::Read(R.Ball, 0.f, C);
		const FShotProfile Shot = CricketBatting::ChooseShot(Intent, Dir, Read.PitchX, Read.HeightAtBat, Style);
		FBallState P = R.Ball;
		CricketBall::SimulateToPlane(P, Shot.ContactX(), C);
		FBatInput In;
		In.Intent = Intent;
		In.DirectionDeg = Dir;
		In.PressTime = P.Time - Shot.SwingTime;
		return In;
	}
}

// ---------------------------------------------------------------- Rules

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesSixLegalBalls, "CRICKET26.Rules.SixLegalBallsEndInnings", CricketTestFlags)
bool FSORulesSixLegalBalls::RunTest(const FString&)
{
	FSuperOverMatch M;
	M.Start(0);
	for (int32 I = 0; I < 5; ++I) TestTrue(TEXT("ball accepted"), Bowl(M, Runs(0)));
	TestEqual(TEXT("still ready"), M.Phase, EMatchPhase::ReadyForDelivery);
	TArray<ECricketEvent> Ev;
	Bowl(M, Runs(1), &Ev);
	TestEqual(TEXT("innings break after 6"), M.Phase, EMatchPhase::InningsBreak);
	TestEqual(TEXT("target = runs + 1"), M.Target, 2);
	TestTrue(TEXT("target event"), Ev.Contains(ECricketEvent::TargetSet));
	TestFalse(TEXT("no delivery in break"), M.BeginDelivery());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesExtras, "CRICKET26.Rules.WidesAndNoBallsAreNotLegal", CricketTestFlags)
bool FSORulesExtras::RunTest(const FString&)
{
	FSuperOverMatch M;
	M.Start(0);
	TArray<ECricketEvent> Ev;
	Bowl(M, Wide(), &Ev);
	TestEqual(TEXT("wide: 1 run"), M.Cur().Runs, 1);
	TestEqual(TEXT("wide: not legal"), M.Cur().LegalBalls, 0);
	TestTrue(TEXT("wide event"), Ev.Contains(ECricketEvent::Wide));
	Ev.Reset();
	Bowl(M, NoBall(2), &Ev);
	TestEqual(TEXT("no-ball: 1 + 2 off the bat"), M.Cur().Runs, 4);
	TestEqual(TEXT("no-ball: not legal"), M.Cur().LegalBalls, 0);
	TestTrue(TEXT("free hit next"), M.bFreeHit && Ev.Contains(ECricketEvent::FreeHitNext));
	TestEqual(TEXT("bowler charged all"), M.Cur().Bowler.Runs, 4);
	TestEqual(TEXT("extras"), M.Cur().Extras, 2);
	// Free hit: bowled is illegal input, the state must not change.
	const FInningsState Before = M.Cur();
	M.BeginDelivery();
	TArray<ECricketEvent> Unused;
	TestFalse(TEXT("bowled rejected on free hit"), M.CompleteDelivery(Out(EDismissal::Bowled), Unused));
	TestEqual(TEXT("state untouched"), M.Cur().Runs, Before.Runs);
	TestTrue(TEXT("run out allowed on free hit"), M.CompleteDelivery(Out(EDismissal::RunOut), Unused));
	TestFalse(TEXT("free hit consumed by legal ball"), M.bFreeHit);
	TestEqual(TEXT("one legal ball"), M.Cur().LegalBalls, 1);
	FString Err;
	TestTrue(TEXT("invariants"), M.CheckInvariants(Err));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesFreeHitCarries, "CRICKET26.Rules.FreeHitCarriesOverWide", CricketTestFlags)
bool FSORulesFreeHitCarries::RunTest(const FString&)
{
	FSuperOverMatch M;
	M.Start(0);
	Bowl(M, NoBall());
	Bowl(M, Wide());
	TestTrue(TEXT("free hit still pending after wide"), M.bFreeHit);
	Bowl(M, Runs(0));
	TestFalse(TEXT("cleared by legal ball"), M.bFreeHit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesWickets, "CRICKET26.Rules.TwoWicketsEndInnings", CricketTestFlags)
bool FSORulesWickets::RunTest(const FString&)
{
	FSuperOverMatch M;
	M.Start(0);
	Bowl(M, Out(EDismissal::Caught));
	TestEqual(TEXT("new batter takes strike"), M.Cur().Striker, 2);
	TestEqual(TEXT("non-striker unchanged"), M.Cur().NonStriker, 1);
	TestEqual(TEXT("still in"), M.Phase, EMatchPhase::ReadyForDelivery);
	Bowl(M, Out(EDismissal::Bowled));
	TestEqual(TEXT("2 wickets"), M.Cur().Wickets, 2);
	TestEqual(TEXT("innings over"), M.Phase, EMatchPhase::InningsBreak);
	TestEqual(TEXT("only 2 legal balls"), M.Cur().LegalBalls, 2);
	TestEqual(TEXT("bowler wickets"), M.Cur().Bowler.Wickets, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesStrike, "CRICKET26.Rules.StrikeRotation", CricketTestFlags)
bool FSORulesStrike::RunTest(const FString&)
{
	FSuperOverMatch M;
	M.Start(0);
	Bowl(M, Runs(1));
	TestEqual(TEXT("single swaps"), M.Cur().Striker, 1);
	Bowl(M, Runs(2));
	TestEqual(TEXT("two keeps"), M.Cur().Striker, 1);
	Bowl(M, Four());
	TestEqual(TEXT("four keeps"), M.Cur().Striker, 1);
	Bowl(M, Runs(3));
	TestEqual(TEXT("three swaps"), M.Cur().Striker, 0);
	TestEqual(TEXT("runs"), M.Cur().Runs, 10);
	TestEqual(TEXT("batter 0 runs"), M.Cur().Batters[0].Runs, 1);
	TestEqual(TEXT("batter 1 runs"), M.Cur().Batters[1].Runs, 9);
	TestEqual(TEXT("batter 1 fours"), M.Cur().Batters[1].Fours, 1);
	// Non-striker run out going for the first run: the batters crossed, so the new batter takes the
	// striker's end (where the out batter was heading) and the survivor is at the bowler's end.
	FDeliveryOutcome RO = Out(EDismissal::RunOut);
	RO.bBatContact = true;
	RO.bRunOutStriker = false;
	Bowl(M, RO);
	TestEqual(TEXT("new batter on strike"), M.Cur().Striker, 2);
	TestEqual(TEXT("survivor at non-striker's end"), M.Cur().NonStriker, 0);
	TestEqual(TEXT("non-striker is the one out"), M.Cur().Batters[1].HowOut, EDismissal::RunOut);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesChase, "CRICKET26.Rules.ChaseEndsWhenTargetReached", CricketTestFlags)
bool FSORulesChase::RunTest(const FString&)
{
	FSuperOverMatch M;
	M.Start(0);
	Bowl(M, Six());
	Bowl(M, Four());
	for (int32 I = 0; I < 4; ++I) Bowl(M, Runs(0));
	TestEqual(TEXT("target"), M.Target, 11);
	TestTrue(TEXT("second innings"), M.StartSecondInnings());
	TestEqual(TEXT("team 1 bats"), M.BattingTeam(), 1);
	TestEqual(TEXT("pressure"), M.PressureText(), FString(TEXT("11 REQUIRED FROM 6")));
	Bowl(M, Six());
	TArray<ECricketEvent> Ev;
	Bowl(M, Four(), &Ev);
	TestEqual(TEXT("still going on 10"), M.Phase, EMatchPhase::ReadyForDelivery);
	Bowl(M, Runs(1), &Ev);
	TestEqual(TEXT("complete on reaching target"), M.Phase, EMatchPhase::MatchComplete);
	TestEqual(TEXT("chasing side wins"), M.Winner, 1);
	TestTrue(TEXT("won event"), Ev.Contains(ECricketEvent::MatchWon));
	TestFalse(TEXT("no more balls"), M.BeginDelivery());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesDefend, "CRICKET26.Rules.DefendingSideWins", CricketTestFlags)
bool FSORulesDefend::RunTest(const FString&)
{
	FSuperOverMatch M;
	M.Start(1);
	Bowl(M, Six());
	for (int32 I = 0; I < 5; ++I) Bowl(M, Runs(0));
	M.StartSecondInnings();
	for (int32 I = 0; I < 4; ++I) Bowl(M, Runs(1));
	Bowl(M, Runs(0));
	TestEqual(TEXT("last ball text"), M.PressureText(), FString(TEXT("3 TO WIN OFF THE LAST BALL")));
	Bowl(M, Runs(1));
	TestEqual(TEXT("complete"), M.Phase, EMatchPhase::MatchComplete);
	TestEqual(TEXT("team 1 defended"), M.Winner, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesTie, "CRICKET26.Rules.TieGoesToAnotherSuperOver", CricketTestFlags)
bool FSORulesTie::RunTest(const FString&)
{
	FSuperOverMatch M;
	M.Start(0);
	Bowl(M, Four());
	for (int32 I = 0; I < 5; ++I) Bowl(M, Runs(0));
	M.StartSecondInnings();
	TArray<ECricketEvent> Ev;
	Bowl(M, Out(EDismissal::Bowled));
	Bowl(M, Four());
	Bowl(M, Out(EDismissal::LBW), &Ev);
	TestEqual(TEXT("complete"), M.Phase, EMatchPhase::MatchComplete);
	TestTrue(TEXT("tied"), M.bTied && M.Winner == -1 && Ev.Contains(ECricketEvent::MatchTied));
	TestTrue(TEXT("next super over"), M.StartNextSuperOver());
	TestEqual(TEXT("number"), M.SuperOverNumber, 2);
	TestEqual(TEXT("side that batted second bats first"), M.BattingTeam(), 1);
	TestEqual(TEXT("fresh innings"), M.Cur().Runs, 0);
	FString Err;
	TestTrue(TEXT("invariants"), M.CheckInvariants(Err));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesIllegal, "CRICKET26.Rules.IllegalInputRejected", CricketTestFlags)
bool FSORulesIllegal::RunTest(const FString&)
{
	FSuperOverMatch M;
	TArray<ECricketEvent> Ev;
	TestFalse(TEXT("no delivery before start"), M.BeginDelivery());
	M.Start(0);
	TestFalse(TEXT("complete without begin"), M.CompleteDelivery(Runs(1), Ev));
	M.BeginDelivery();
	FDeliveryOutcome W = Wide();
	W.Dismissal = EDismissal::Caught;
	TestFalse(TEXT("caught off a wide"), M.CompleteDelivery(W, Ev));
	FDeliveryOutcome C = Out(EDismissal::Caught);
	C.RunsRun = 1;
	TestTrue(TEXT("caught accepted"), M.CompleteDelivery(C, Ev));
	TestEqual(TEXT("runs don't count when caught"), M.Cur().Runs, 0);
	TestFalse(TEXT("second innings not yet"), M.StartSecondInnings());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesByes, "CRICKET26.Rules.ByesLegByesAndWideRuns", CricketTestFlags)
bool FSORulesByes::RunTest(const FString&)
{
	FSuperOverMatch M;
	M.Start(0);
	FDeliveryOutcome LB;
	LB.bLegBye = true;
	LB.RunsRun = 1;
	TestTrue(TEXT("leg bye accepted"), Bowl(M, LB));
	const FInningsState& In = M.Cur();
	TestEqual(TEXT("leg bye to the total"), In.Runs, 1);
	TestEqual(TEXT("leg bye is an extra"), In.LegByes, 1);
	TestEqual(TEXT("not the batter's run"), In.Batters[0].Runs, 0);
	TestEqual(TEXT("but the batter faced it"), In.Batters[0].Balls, 1);
	TestEqual(TEXT("not against the bowler"), In.Bowler.Runs, 0);
	TestEqual(TEXT("legal ball"), In.LegalBalls, 1);
	TestEqual(TEXT("strike rotates on a leg bye"), In.Striker, 1);
	TestEqual(TEXT("scored as 1lb"), In.BallLog.Last(), FString(TEXT("1lb")));

	FDeliveryOutcome FourByes;
	FourByes.Boundary = 4;
	TestTrue(TEXT("four byes accepted"), Bowl(M, FourByes));
	TestEqual(TEXT("byes"), M.Cur().Byes, 4);
	TestEqual(TEXT("scored as 4b"), M.Cur().BallLog.Last(), FString(TEXT("4b")));
	TestEqual(TEXT("no four for the batter"), M.Cur().Batters[1].Fours, 0);

	FDeliveryOutcome WideRun = Wide();
	WideRun.RunsRun = 2;
	TestTrue(TEXT("wide with runs"), Bowl(M, WideRun));
	TestEqual(TEXT("1 + 2 wides"), M.Cur().Runs, 8);
	TestEqual(TEXT("wide runs charged to the bowler"), M.Cur().Bowler.Runs, 3);
	FDeliveryOutcome WideFour = Wide();
	WideFour.Boundary = 4;
	TestTrue(TEXT("wide to the rope"), Bowl(M, WideFour));
	TestEqual(TEXT("5 wides"), M.Cur().Runs, 13);
	TestEqual(TEXT("wides are not legal"), M.Cur().LegalBalls, 2);

	FDeliveryOutcome NbLb = NoBall();
	NbLb.bLegBye = true;
	NbLb.RunsRun = 1;
	TestTrue(TEXT("leg bye off a no-ball"), Bowl(M, NbLb));
	TestEqual(TEXT("1 nb + 1 lb"), M.Cur().Runs, 15);

	TArray<ECricketEvent> Ev;
	M.BeginDelivery();
	FDeliveryOutcome Bad = Runs(1);
	Bad.bLegBye = true;
	TestFalse(TEXT("leg bye with bat contact rejected"), M.CompleteDelivery(Bad, Ev));
	FDeliveryOutcome BadWide = Wide();
	BadWide.bLegBye = true;
	TestFalse(TEXT("leg bye off a wide rejected"), M.CompleteDelivery(BadWide, Ev));
	FDeliveryOutcome SixByes;
	SixByes.Boundary = 6;
	TestFalse(TEXT("six without the bat rejected"), M.CompleteDelivery(SixByes, Ev));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesDismissalLegality, "CRICKET26.Rules.StumpedHitWicketAndLegality", CricketTestFlags)
bool FSORulesDismissalLegality::RunTest(const FString&)
{
	TArray<ECricketEvent> Ev;
	FSuperOverMatch M;
	M.Start(0);

	FDeliveryOutcome StWide = Wide();
	StWide.Dismissal = EDismissal::Stumped;
	TestTrue(TEXT("stumped off a wide stands"), Bowl(M, StWide));
	TestEqual(TEXT("one wide scored"), M.Cur().Runs, 1);
	TestEqual(TEXT("wicket"), M.Cur().Wickets, 1);
	TestEqual(TEXT("bowler credited for a stumping"), M.Cur().Bowler.Wickets, 1);
	TestEqual(TEXT("how out"), M.Cur().Batters[0].HowOut, EDismissal::Stumped);
	TestEqual(TEXT("new batter on strike"), M.Cur().Striker, 2);

	M.BeginDelivery();
	FDeliveryOutcome StNb = NoBall();
	StNb.Dismissal = EDismissal::Stumped;
	TestFalse(TEXT("stumped off a no-ball rejected"), M.CompleteDelivery(StNb, Ev));
	FDeliveryOutcome HwNb = NoBall();
	HwNb.Dismissal = EDismissal::HitWicket;
	TestFalse(TEXT("hit wicket off a no-ball rejected"), M.CompleteDelivery(HwNb, Ev));
	FDeliveryOutcome StRun = Out(EDismissal::Stumped);
	StRun.RunsRun = 1;
	TestFalse(TEXT("stumped while completing a run rejected"), M.CompleteDelivery(StRun, Ev));
	FDeliveryOutcome LbwBat = Out(EDismissal::LBW);
	LbwBat.bBatContact = true;
	TestFalse(TEXT("LBW after hitting the bat rejected"), M.CompleteDelivery(LbwBat, Ev));
	FDeliveryOutcome BowledWide = Wide();
	BowledWide.Dismissal = EDismissal::Bowled;
	TestFalse(TEXT("bowled off a wide rejected"), M.CompleteDelivery(BowledWide, Ev));
	// Nothing was applied by the rejections.
	TestEqual(TEXT("state untouched"), M.Cur().Deliveries, 1);
	TestTrue(TEXT("still mid-delivery"), M.Phase == EMatchPhase::DeliveryInProgress);

	// Free hit: only a run out.
	TestTrue(TEXT("no-ball"), M.CompleteDelivery(NoBall(), Ev));
	TestTrue(TEXT("free hit"), M.bFreeHit);
	M.BeginDelivery();
	TestFalse(TEXT("hit wicket on a free hit rejected"), M.CompleteDelivery(Out(EDismissal::HitWicket), Ev));
	TestFalse(TEXT("stumped on a free hit rejected"), M.CompleteDelivery(Out(EDismissal::Stumped), Ev));
	FDeliveryOutcome Ro = Out(EDismissal::RunOut);
	Ro.bBatContact = true;
	TestTrue(TEXT("run out on a free hit stands"), M.CompleteDelivery(Ro, Ev));
	TestTrue(TEXT("second wicket ends the innings"), M.Cur().bComplete);

	FSuperOverMatch H;
	H.Start(0);
	FDeliveryOutcome HwWide = Wide();
	HwWide.Dismissal = EDismissal::HitWicket;
	TestTrue(TEXT("hit wicket off a wide stands"), Bowl(H, HwWide));
	TestEqual(TEXT("bowler credited for hit wicket"), H.Cur().Bowler.Wickets, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesOverthrows, "CRICKET26.Rules.OverthrowsToTheBoundary", CricketTestFlags)
bool FSORulesOverthrows::RunTest(const FString&)
{
	TArray<ECricketEvent> Ev;
	FSuperOverMatch M;
	M.Start(0);
	FDeliveryOutcome O = Runs(1);
	O.Boundary = 4;
	M.BeginDelivery();
	TestFalse(TEXT("runs + boundary without an overthrow rejected"), M.CompleteDelivery(O, Ev));
	O.bOverthrow = true;
	TestTrue(TEXT("overthrow accepted"), M.CompleteDelivery(O, Ev));
	TestEqual(TEXT("1 run + 4 overthrows to the batter"), M.Cur().Batters[0].Runs, 5);
	TestEqual(TEXT("not a hit four"), M.Cur().Batters[0].Fours, 0);
	TestFalse(TEXT("no four event"), Ev.Contains(ECricketEvent::BoundaryFour));
	TestEqual(TEXT("strike follows the completed run"), M.Cur().Striker, 1);
	FDeliveryOutcome Six = O;
	Six.Boundary = 6;
	M.BeginDelivery();
	TestFalse(TEXT("overthrows can't be six"), M.CompleteDelivery(Six, Ev));
	FDeliveryOutcome ByeOver;
	ByeOver.RunsRun = 2;
	ByeOver.Boundary = 4;
	ByeOver.bOverthrow = true;
	TestTrue(TEXT("overthrows off byes"), M.CompleteDelivery(ByeOver, Ev));
	TestEqual(TEXT("6 byes"), M.Cur().Byes, 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesBouncers, "CRICKET26.Rules.BouncerLimitAndConfigurableRules", CricketTestFlags)
bool FSORulesBouncers::RunTest(const FString&)
{
	TArray<ECricketEvent> Ev;
	FSuperOverMatch M;
	M.Rules.MaxBouncersPerOver = 1;
	M.Start(0);
	FDeliveryOutcome B;
	B.bBouncer = true;
	TestTrue(TEXT("first bouncer fine"), Bowl(M, B));
	TestFalse(TEXT("limit reached"), M.BouncerAllowed());
	M.BeginDelivery();
	TestFalse(TEXT("uncalled second bouncer rejected"), M.CompleteDelivery(B, Ev));
	B.bNoBall = true;
	TestTrue(TEXT("second bouncer called no-ball"), M.CompleteDelivery(B, Ev));
	TestTrue(TEXT("free hit follows"), M.bFreeHit);

	// Rules are data: a 3-ball, 1-wicket shoot-out.
	FSuperOverMatch S;
	S.Rules.MaxLegalBalls = 3;
	S.Rules.MaxWickets = 1;
	S.Start(0);
	Bowl(S, Runs(1));
	Bowl(S, Runs(1));
	Bowl(S, Runs(1));
	TestTrue(TEXT("3 legal balls end the innings"), S.Phase == EMatchPhase::InningsBreak);
	S.StartSecondInnings();
	Bowl(S, Out(EDismissal::Bowled));
	TestTrue(TEXT("1 wicket ends the chase"), S.Phase == EMatchPhase::MatchComplete && S.Winner == 0);
	FString Err;
	TestTrue(Err, S.CheckInvariants(Err));
	return true;
}

// ---------------------------------------------------------------- Ball physics

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallSolver, "CRICKET26.Ball.PitchesWhereAimed", CricketTestFlags)
bool FSOBallSolver::RunTest(const FString&)
{
	for (float Length : { 2.f, 5.f, 8.f })
	{
		const FDeliveryRelease R = Release(EDeliveryType::Stock, Length, 0.1f);
		FVector Pitch = FVector::ZeroVector;
		AtCrease(R, &Pitch);
		TestNearlyEqual(*FString::Printf(TEXT("length %.0f"), Length), float(Pitch.X), float(Length), 0.25f);
		TestNearlyEqual(TEXT("line"), float(Pitch.Y), 0.1f, 0.12f);
		TestTrue(TEXT("legal"), !R.bNoBall);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallSwing, "CRICKET26.Ball.SwingAndTurnDirections", CricketTestFlags)
bool FSOBallSwing::RunTest(const FString&)
{
	// Right-handed batter: off side is +Y. Outswing / leg break move toward off after release / pitching.
	auto Lateral = [](const FDeliveryRelease& R) { FVector P; const FBallState B = AtCrease(R, &P); return B.Pos.Y - P.Y; };
	// The aim solver compensates swing so both pitch on the target; the swing shows as lateral velocity.
	const FBallState BO = AtCrease(Release(EDeliveryType::Outswing, 4.f)), BI = AtCrease(Release(EDeliveryType::Inswing, 4.f));
	TestTrue(*FString::Printf(TEXT("outswinger moving away (%.2f vs %.2f m/s)"), BO.Vel.Y, BI.Vel.Y), BO.Vel.Y > BI.Vel.Y + 0.4f);

	const float Off = Lateral(Release(EDeliveryType::OffBreak, 4.5f, 0.3f, 0.f, EBowlerType::OffSpin));
	const float Leg = Lateral(Release(EDeliveryType::LegBreak, 4.5f, 0.f, 0.f, EBowlerType::LegSpin));
	const float Goog = Lateral(Release(EDeliveryType::Googly, 4.5f, 0.f, 0.f, EBowlerType::LegSpin));
	TestTrue(*FString::Printf(TEXT("off break turns into the batter (%.2f)"), Off), Off < -0.08f);
	TestTrue(*FString::Printf(TEXT("leg break turns away (%.2f)"), Leg), Leg > 0.08f);
	TestTrue(*FString::Printf(TEXT("googly turns in (%.2f)"), Goog), Goog < -0.02f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallLengths, "CRICKET26.Ball.BouncerRisesYorkerDoesNot", CricketTestFlags)
bool FSOBallLengths::RunTest(const FString&)
{
	const FBallState Bouncer = AtCrease(Release(EDeliveryType::Stock, 11.5f));
	const FBallState Yorker = AtCrease(Release(EDeliveryType::Stock, 1.1f));
	TestTrue(*FString::Printf(TEXT("bouncer high at crease (%.2f m)"), Bouncer.Pos.Z), Bouncer.Pos.Z > 1.2f);
	TestTrue(*FString::Printf(TEXT("yorker low at crease (%.2f m)"), Yorker.Pos.Z), Yorker.Pos.Z < 0.3f);
	const FDeliveryRelease Slow = Release(EDeliveryType::Slower, 5.f), Stock = Release(EDeliveryType::Stock, 5.f);
	TestTrue(TEXT("slower ball slower"), Slow.SpeedKph < Stock.SpeedKph * 0.85f);
	TestTrue(TEXT("slower ball takes longer"), AtCrease(Slow).Time > AtCrease(Stock).Time + 0.05f);
	FString Table;
	for (float L = 2.f; L <= 12.f; L += 2.f)
	{
		Table += FString::Printf(TEXT(" %.0fm:%.2f/%.2f"), L, AtCrease(Release(EDeliveryType::Stock, L)).Pos.Z,
			AtCrease(Release(EDeliveryType::TopSpinner, L * 0.6f, 0.f, 0.f, EBowlerType::OffSpin)).Pos.Z);
	}
	UE_LOG(LogTemp, Display, TEXT("Bounce table (pace length: height at crease / spin at 0.6 x length):%s"), *Table);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallReleaseErrors, "CRICKET26.Ball.ReleaseErrorsStayPhysical", CricketTestFlags)
bool FSOBallReleaseErrors::RunTest(const FString&)
{
	// Mistimed releases must still produce a flat, catchable trajectory that pitches where the
	// perturbed aim says (or is a full toss), never a lob looping over the batter.
	int32 Bad = 0, Cases = 0;
	for (EBowlerType Style : { EBowlerType::Pace, EBowlerType::OffSpin, EBowlerType::LegSpin })
	{
		FCricketPlayer Bowler;
		Bowler.BowlerType = Style;
		Bowler.Accuracy = 0.5f;
		if (Style != EBowlerType::Pace) Bowler.PaceKph = 86.f;
		for (EDeliveryType Type : CricketBowling::Repertoire(Style))
		{
			for (float Length = 1.f; Length <= 12.f; Length += 1.f)
			{
				for (float Timing = -1.f; Timing <= 0.81f; Timing += 0.2f)
				{
					for (int32 Seed = 0; Seed < 3; ++Seed)
					{
						FDeliveryPlan Plan;
						Plan.Type = Type;
						Plan.Length = Length;
						const FDeliveryRelease R = CricketBowling::Execute(Bowler, ECricketHand::Right, Plan, Timing, Seed, FPitchConditions());
						FVector Pitch = FVector::ZeroVector;
						const FBallState B = AtCrease(R, &Pitch);
						const bool bPitched = Pitch != FVector::ZeroVector;
						// Pitched: where aimed. Full toss: only when aimed at the crease or beyond, and below head height.
						const bool bOk = bPitched ? (R.AimedPitch.X < 0.3f || FMath::Abs(Pitch.X - R.AimedPitch.X) < 0.3f)
							: (R.AimedPitch.X < 1.5f && B.Pos.Z < 2.f);
						++Cases;
						if (!bOk && Bad++ < 5)
						{
							AddError(FString::Printf(TEXT("%s len %.0f timing %+.1f seed %d: aimed %.2f, pitched %s, at crease z=%.2f"),
								*UEnum::GetValueAsString(Type), Length, Timing, Seed, R.AimedPitch.X, bPitched ? *FString::SanitizeFloat(Pitch.X) : TEXT("never"), B.Pos.Z));
						}
					}
				}
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("Release sweep: %d bad of %d"), Bad, Cases);
	return Bad == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallDeterminism, "CRICKET26.Ball.DeterministicAndNoBall", CricketTestFlags)
bool FSOBallDeterminism::RunTest(const FString&)
{
	FCricketPlayer Bowler; // default accuracy: scatter on
	FDeliveryPlan Plan;
	const FPitchConditions C;
	const FDeliveryRelease A = CricketBowling::Execute(Bowler, ECricketHand::Right, Plan, 0.2f, 42, C);
	const FDeliveryRelease B = CricketBowling::Execute(Bowler, ECricketHand::Right, Plan, 0.2f, 42, C);
	TestTrue(TEXT("same seed same ball"), A.Ball.Vel == B.Ball.Vel && A.Ball.Pos == B.Ball.Pos);
	const FResolveContext Cx = Ctx();
	const FDeliveryResult RA = CricketDelivery::Resolve(A, Perfect(A, EBatIntent::Ground, 20.f), Cx);
	const FDeliveryResult RB = CricketDelivery::Resolve(B, Perfect(B, EBatIntent::Ground, 20.f), Cx);
	TestTrue(TEXT("same resolve"), RA.BallPath == RB.BallPath && RA.Summary == RB.Summary);
	TestTrue(TEXT("late release oversteps"), CricketBowling::Execute(Bowler, ECricketHand::Right, Plan, 0.95f, 1, C).bNoBall);
	TestFalse(TEXT("good release is legal"), CricketBowling::Execute(Bowler, ECricketHand::Right, Plan, 0.f, 1, C).bNoBall);
	// Early release overpitches, late release drags it short.
	FVector PE, PL;
	AtCrease(Release(EDeliveryType::Stock, 5.f, 0.f, -0.6f), &PE);
	AtCrease(Release(EDeliveryType::Stock, 5.f, 0.f, 0.6f), &PL);
	TestTrue(*FString::Printf(TEXT("early fuller (%.2f) than late (%.2f)"), PE.X, PL.X), PE.X < PL.X - 0.5f);
	return true;
}

// ---------------------------------------------------------------- Batting

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBatMiddle, "CRICKET26.Batting.PerfectDriveMiddlesAndGoesStraight", CricketTestFlags)
bool FSOBatMiddle::RunTest(const FString&)
{
	const FDeliveryRelease R = Release(EDeliveryType::Stock, 4.f, 0.1f);
	const FDeliveryResult Res = CricketDelivery::Resolve(R, Perfect(R, EBatIntent::Ground, 0.f), Ctx());
	TestEqual(TEXT("drive"), Res.Shot.Shot, EShotType::Drive);
	TestEqual(TEXT("middled"), Res.Contact.Zone, EContactZone::Middle);
	const FVector V = Res.Contact.ExitVel;
	TestTrue(*FString::Printf(TEXT("goes back past the bowler (%s)"), *V.ToString()), V.X > 15.f && FMath::Abs(V.Y) < 0.25f * V.X);
	TestTrue(TEXT("kept down"), V.Z < 0.2f * V.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBatTiming, "CRICKET26.Batting.LateGoesFinerAndVeryLateMisses", CricketTestFlags)
bool FSOBatTiming::RunTest(const FString&)
{
	const FDeliveryRelease R = Release(EDeliveryType::Stock, 4.f, 0.1f);
	FBatInput In = Perfect(R, EBatIntent::Ground, 30.f);
	const FDeliveryResult Good = CricketDelivery::Resolve(R, In, Ctx());
	In.PressTime += 0.04f;
	const FDeliveryResult Late = CricketDelivery::Resolve(R, In, Ctx());
	auto Angle = [](const FVector& V) { return FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X)); };
	TestTrue(TEXT("both hit"), Good.Contact.HasContact() && Late.Contact.HasContact());
	TestTrue(*FString::Printf(TEXT("late squarer toward off (%.0f vs %.0f)"), Angle(Late.Contact.ExitVel), Angle(Good.Contact.ExitVel)),
		Angle(Late.Contact.ExitVel) > Angle(Good.Contact.ExitVel) + 5.f);
	In.PressTime += 0.25f;
	const FDeliveryResult Miss = CricketDelivery::Resolve(R, In, Ctx());
	TestFalse(TEXT("very late misses"), Miss.Contact.HasContact());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBatEdge, "CRICKET26.Batting.LateMovementFindsTheEdge", CricketTestFlags)
bool FSOBatEdge::RunTest(const FString&)
{
	// Same timing, same batter, same intent: only the delivery's late movement differs.
	FCricketPlayer Tail;
	Tail.Technique = 0.f; // commits early, so it cannot adjust to movement after the read
	FResolveContext C = Ctx();
	C.Striker = Tail;
	auto Zone = [&](EDeliveryType Type)
	{
		FCricketPlayer Bowler;
		Bowler.Accuracy = 1.f;
		Bowler.Movement = 1.f;
		FDeliveryPlan Plan;
		Plan.Type = Type;
		Plan.Length = 4.5f;
		Plan.Line = 0.15f;
		const FDeliveryRelease R = CricketBowling::Execute(Bowler, ECricketHand::Right, Plan, 0.f, 3, C.Conditions);
		return CricketDelivery::Resolve(R, Perfect(R, EBatIntent::Defend, 0.f), C).Contact.Zone;
	};
	const EContactZone Stock = Zone(EDeliveryType::Stock);
	const EContactZone Away = Zone(EDeliveryType::Outswing);
	UE_LOG(LogTemp, Display, TEXT("Edge test: stock %s, outswinger %s"), *CricketDelivery::ZoneName(Stock), *CricketDelivery::ZoneName(Away));
	TestTrue(TEXT("stock ball meets the blade"), Stock != EContactZone::Miss && Stock != EContactZone::OutsideEdge);
	TestTrue(TEXT("outswinger beats the middle"), Away == EContactZone::OutsideEdge || Away == EContactZone::Miss);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBatContinuous, "CRICKET26.Batting.ContactQualityIsContinuous", CricketTestFlags)
bool FSOBatContinuous::RunTest(const FString&)
{
	// Slide the impact point down the blade and then across the face, 5 mm at a time: exit speed and
	// quality must fall off smoothly from the sweet spot, with no jumps at zone boundaries.
	FBallState Ball;
	Ball.Pos = FVector(2.f, 0.1f, 0.4f);
	Ball.Vel = FVector(-30.f, 0.f, 3.f);
	const FShotProfile Drive = CricketBatting::Profile(EShotType::Drive);
	const FCricketPlayer Batter;
	auto Hit = [&](float Along, float Across)
	{
		return CricketBatting::ResolveContact(Ball, Ball.Pos + FVector(0.f, -Across, Along), Drive, 0.f, 0.f, Batter, false);
	};
	const FContactResult Sweet = Hit(0.f, 0.f);
	TestEqual(TEXT("sweet spot middles"), Sweet.Zone, EContactZone::Middle);
	TestTrue(*FString::Printf(TEXT("perfect quality %.2f"), Sweet.Quality), Sweet.Quality > 0.95f);
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		float PrevSpeed = Sweet.ExitVel.Size(), PrevQ = Sweet.Quality;
		for (float D = 0.005f; D <= (Axis == 0 ? 0.16f : 0.05f); D += 0.005f)
		{
			const FContactResult C = Axis == 0 ? Hit(D, 0.f) : Hit(0.f, D);
			if (!C.HasContact()) break;
			const float Speed = C.ExitVel.Size();
			TestTrue(*FString::Printf(TEXT("axis %d at %.3f: speed %.1f after %.1f"), Axis, D, Speed, PrevSpeed), Speed <= PrevSpeed + 0.05f && Speed > PrevSpeed - 3.f);
			TestTrue(TEXT("quality never rises away from the sweet spot"), C.Quality <= PrevQ + 1e-3f);
			PrevSpeed = Speed;
			PrevQ = C.Quality;
		}
		TestTrue(*FString::Printf(TEXT("axis %d loses real power toward the edge/toe (%.2f)"), Axis, PrevQ), PrevQ < 0.8f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBatFamilies, "CRICKET26.Batting.ShotFamiliesGoWhereTheyShould", CricketTestFlags)
bool FSOBatFamilies::RunTest(const FString&)
{
	struct FCase { EBowlerType Style; EDeliveryType Type; float Length, Line; EBatIntent Intent; float Dir; EShotType Want; float Centre, Half; };
	const FCase Cases[] = {
		{ EBowlerType::Pace, EDeliveryType::Stock, 4.f, -0.1f, EBatIntent::Ground, -110.f, EShotType::Flick, -110.f, 45.f },
		{ EBowlerType::Pace, EDeliveryType::Stock, 10.5f, 0.f, EBatIntent::Loft, -100.f, EShotType::Hook, -110.f, 55.f },
		{ EBowlerType::OffSpin, EDeliveryType::OffBreak, 4.5f, 0.1f, EBatIntent::Loft, -70.f, EShotType::SlogSweep, -70.f, 45.f },
		{ EBowlerType::LegSpin, EDeliveryType::LegBreak, 4.5f, 0.2f, EBatIntent::Ground, 110.f, EShotType::ReverseSweep, 115.f, 50.f },
		{ EBowlerType::Pace, EDeliveryType::Stock, 2.5f, 0.f, EBatIntent::Loft, -165.f, EShotType::Scoop, -160.f, 30.f },
	};
	for (const FCase& K : Cases)
	{
		const FDeliveryRelease R = Release(K.Type, K.Length, K.Line, 0.f, K.Style);
		const FDeliveryResult Res = CricketDelivery::Resolve(R, Perfect(R, K.Intent, K.Dir, K.Style), Ctx(K.Style));
		const FVector V = Res.Contact.ExitVel;
		const float Angle = FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X)); // right-hander: + is the off side
		const FString What = FString::Printf(TEXT("%s: %s, exit %.1f m/s at %.0f deg, %.0f up"), *CricketDelivery::ShotName(K.Want),
			*Res.Summary, V.Size(), Angle, FMath::RadiansToDegrees(FMath::Atan2(V.Z, FVector2D(V.X, V.Y).Size())));
		UE_LOG(LogTemp, Display, TEXT("Shot family %s (read: pitch %.1f m, %.2f m high at the front plane)"), *What,
			CricketDelivery::Read(R.Ball, 0.f, FPitchConditions()).PitchX, CricketDelivery::Read(R.Ball, 0.f, FPitchConditions()).HeightAtBat);
		TestEqual(*What, Res.Shot.Shot, K.Want);
		TestTrue(*(What + TEXT(" - hit")), Res.Contact.HasContact());
		TestTrue(*(What + TEXT(" - direction")), FMath::Abs(FMath::FindDeltaAngleDegrees(K.Centre, Angle)) <= K.Half);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBatWicket, "CRICKET26.Batting.BowledAndLBW", CricketTestFlags)
bool FSOBatWicket::RunTest(const FString&)
{
	const FResolveContext C = Ctx();
	// Straight full ball, no shot, bat never comes: pad in line hitting.
	const FDeliveryRelease Straight = Release(EDeliveryType::Stock, 3.f, 0.f);
	const FDeliveryResult L = CricketDelivery::Resolve(Straight, FBatInput(), C);
	TestTrue(*FString::Printf(TEXT("pad or stumps (%s)"), *L.Summary), L.Dismissal == EDismissal::LBW || L.Dismissal == EDismissal::Bowled);
	// Very late swing at a straight yorker: bowled.
	const FDeliveryRelease York = Release(EDeliveryType::Stock, 1.0f, 0.0f);
	FBatInput Late = Perfect(York, EBatIntent::Ground, 0.f);
	Late.PressTime += 0.2f;
	const FDeliveryResult B = CricketDelivery::Resolve(York, Late, C);
	TestTrue(*FString::Printf(TEXT("late on yorker is out (%s)"), *B.Summary), B.Dismissal == EDismissal::Bowled || B.Dismissal == EDismissal::LBW);
	// Wide outside off, left alone: wide, not out.
	const FDeliveryResult W = CricketDelivery::Resolve(Release(EDeliveryType::Stock, 5.f, 1.2f), FBatInput(), C);
	TestTrue(TEXT("wide called"), W.bWide && W.Dismissal == EDismissal::None);
	// Free hit protects the batter.
	FResolveContext FH = C;
	FH.bFreeHit = true;
	TestEqual(TEXT("free hit not out"), CricketDelivery::Resolve(Straight, FBatInput(), FH).Dismissal, EDismissal::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOUmpireHeights, "CRICKET26.Umpire.BeamerBouncerAndOverHead", CricketTestFlags)
bool FSOUmpireHeights::RunTest(const FString&)
{
	// Waist-high full toss: no-ball, whatever the batter does.
	FDeliveryRelease Beamer;
	Beamer.Ball.Pos = FVector(CricketGeo::PitchLength - 1.5f, 0.2f, 2.1f);
	Beamer.Ball.Vel = FVector(-38.f, -0.2f, 1.f);
	Beamer.SpeedKph = 137.f;
	const FDeliveryResult Bm = CricketDelivery::Resolve(Beamer, FBatInput(), Ctx());
	TestTrue(*FString::Printf(TEXT("beamer is a no-ball (%s)"), *Bm.Summary), Bm.bBeamer && Bm.bNoBall);
	TestTrue(TEXT("beamer outcome legal"), [&] { FSuperOverMatch M; M.Start(0); return Bowl(M, Bm.ToOutcome()); }());

	// Find a bouncer (above shoulder) and one that flies over head height.
	float Shoulder = -1.f, OverHead = -1.f;
	for (float L = 6.f; L <= 13.f; L += 0.25f)
	{
		const float Z = AtCrease(Release(EDeliveryType::Stock, L)).Pos.Z;
		if (Shoulder < 0.f && Z > CricketGeo::ShoulderHeight + 0.05f && Z < CricketGeo::HeadHeight - 0.05f) Shoulder = L;
		if (OverHead < 0.f && Z > CricketGeo::HeadHeight + 0.05f) OverHead = L;
	}
	if (!TestTrue(TEXT("a bouncer length exists"), Shoulder > 0.f)) return false;
	FResolveContext C = Ctx();
	const FDeliveryRelease Bouncer = Release(EDeliveryType::Stock, Shoulder);
	const FDeliveryResult First = CricketDelivery::Resolve(Bouncer, FBatInput(), C);
	TestTrue(*FString::Printf(TEXT("first bouncer legal (%s)"), *First.Summary), First.bBouncer && !First.bNoBall && !First.bWide);
	C.BouncersBowled = 1;
	const FDeliveryResult Second = CricketDelivery::Resolve(Bouncer, FBatInput(), C);
	TestTrue(*FString::Printf(TEXT("second bouncer no-ball (%s)"), *Second.Summary), Second.bBouncer && Second.bNoBall);
	FSuperOverMatch M;
	M.Start(0);
	Bowl(M, First.ToOutcome());
	TestTrue(TEXT("match accepts the called second bouncer"), Bowl(M, Second.ToOutcome()));

	// A steep short ball that climbs over head height at the crease.
	FDeliveryRelease High;
	High.Ball.Pos = FVector(CricketGeo::PitchLength - 1.5f, 0.1f, 2.2f);
	High.Ball.Vel = FVector(-33.f, -0.1f, -13.f);
	High.SpeedKph = 128.f;
	FDeliveryRelease HighCopy = High;
	if (!TestTrue(*FString::Printf(TEXT("test ball is over head height (%.2f m)"), AtCrease(HighCopy).Pos.Z), AtCrease(HighCopy).Pos.Z > CricketGeo::HeadHeight)) return false;
	const FDeliveryResult W = CricketDelivery::Resolve(High, FBatInput(), Ctx());
	TestTrue(*FString::Printf(TEXT("over head height is wide by default (%s)"), *W.Summary), W.bWide && !W.bNoBall);
	FResolveContext NbRules = Ctx();
	NbRules.Rules.bOverHeadIsWide = false;
	TestTrue(TEXT("or a no-ball under other playing conditions"), CricketDelivery::Resolve(High, FBatInput(), NbRules).bNoBall);
	// Law 22.4: once struck it is not a wide; over the bouncer limit it is still a no-ball.
	FResolveContext Spent = Ctx();
	Spent.BouncersBowled = Spent.Rules.MaxBouncersPerOver;
	// A ball just over head height at the crease that a hook can still reach.
	FDeliveryRelease Hookable = High;
	for (float Vz = -13.f; Vz < -5.f; Vz += 0.1f) // shallower pitches further up, bounces lower
	{
		Hookable.Ball.Vel.Z = Vz;
		const float Z = AtCrease(Hookable).Pos.Z;
		if (Z > CricketGeo::HeadHeight && Z < 1.95f) break;
	}
	const FDeliveryResult Hit = CricketDelivery::Resolve(Hookable, Perfect(Hookable, EBatIntent::Loft, -60.f), Spent);
	if (Hit.Contact.HasContact())
	{
		TestTrue(*FString::Printf(TEXT("struck over-head bouncer past the limit is a no-ball, not a wide (%s)"), *Hit.Summary), Hit.bNoBall && !Hit.bWide);
	}
	else
	{
		AddError(FString::Printf(TEXT("perfect hook should reach the over-head ball (%s, %.2f m at the crease)"), *Hit.Summary, AtCrease(Hookable).Pos.Z));
	}
	UE_LOG(LogTemp, Display, TEXT("Highest stock-delivery length over head height: %.2f (-1 = none up to 13 m)"), OverHead);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLegByes, "CRICKET26.Umpire.LegByesNeedAStroke", CricketTestFlags)
bool FSOLegByes::RunTest(const FString&)
{
	// Down the leg side onto the pads: padded away with no stroke - no leg byes; the same ball with
	// a missed stroke - leg byes may be run.
	int32 Checked = 0;
	for (float Line = -0.1f; Line >= -0.3f && Checked < 2; Line -= 0.05f)
	{
		const FDeliveryRelease R = Release(EDeliveryType::Stock, 4.f, Line);
		const FDeliveryResult NoShot = CricketDelivery::Resolve(R, FBatInput(), Ctx());
		if (!NoShot.bPadImpact || NoShot.Dismissal != EDismissal::None) continue;
		++Checked;
		const FDeliveryOutcome O = NoShot.ToOutcome();
		TestTrue(*FString::Printf(TEXT("no stroke, no leg byes (%s)"), *NoShot.Summary), !NoShot.bRunsAllowed && O.RunsRun == 0 && O.Boundary == 0);
		TestTrue(TEXT("flagged as off the body"), O.bLegBye);
		FResolveContext Reckless = Ctx();
		Reckless.RunMargin = -3.f;
		const FDeliveryResult Stroke = CricketDelivery::Resolve(R, FBatInput{ EBatIntent::Defend, 0.f, 10.f }, Reckless);
		TestTrue(TEXT("a missed stroke allows leg byes"), Stroke.bTooLate || Stroke.bRunsAllowed);
		FSuperOverMatch M;
		M.Start(0);
		TestTrue(*FString::Printf(TEXT("pad outcome legal (%s)"), *NoShot.Summary), Bowl(M, O));
	}
	TestTrue(TEXT("found a pad hit to check"), Checked > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOStumping, "CRICKET26.Umpire.StumpedDownTheTrack", CricketTestFlags)
bool FSOStumping::RunTest(const FString&)
{
	// A leg-spinner's wide leg break: the batter charges, is beaten, the keeper standing up stumps
	// them - and stumped stands off a wide.
	const FDeliveryRelease R = Release(EDeliveryType::LegBreak, 4.5f, 1.3f, 0.f, EBowlerType::LegSpin);
	const FResolveContext C = Ctx(EBowlerType::LegSpin);
	const FBallRead Seen = CricketDelivery::Read(R.Ball, 0.f, FPitchConditions());
	FBatInput Charge;
	Charge.Intent = EBatIntent::Loft;
	Charge.PressTime = Seen.ArrivalTime - 0.62f;
	const FDeliveryResult S = CricketDelivery::Resolve(R, Charge, C);
	TestTrue(*FString::Printf(TEXT("went down the track (%s)"), *S.Summary), S.Shot.Foot == EFootwork::Advance);
	TestFalse(TEXT("beaten"), S.Contact.HasContact());
	TestEqual(*FString::Printf(TEXT("stumped (%s)"), *S.Summary), S.Dismissal, EDismissal::Stumped);
	FSuperOverMatch M;
	M.Start(0);
	TestTrue(TEXT("stumping outcome legal"), Bowl(M, S.ToOutcome()));
	TestEqual(TEXT("recorded"), M.Cur().Batters[0].HowOut, EDismissal::Stumped);

	// Same ball, batter stays in the crease: not stumped.
	FBatInput Stay = Charge;
	Stay.PressTime = Seen.ArrivalTime - 0.3f;
	const FDeliveryResult In = CricketDelivery::Resolve(R, Stay, C);
	TestTrue(*FString::Printf(TEXT("in the crease, not stumped (%s)"), *In.Summary), In.Shot.Foot != EFootwork::Advance && In.Dismissal != EDismissal::Stumped);

	// Same charge against pace: no advance (keeper back, nobody charges 140 kph).
	TestTrue(TEXT("no charging the quicks"), CricketBatting::ChooseShot(EBatIntent::Loft, 0.f, 4.f, 0.5f, EBowlerType::Pace, 0.7f).Foot != EFootwork::Advance);

	// A well-timed charge at a stock leg break on a good length goes to the pitch and smothers the turn.
	const FDeliveryRelease Stock = Release(EDeliveryType::LegBreak, 4.6f, 0.05f, 0.f, EBowlerType::LegSpin);
	const FBallRead Flight = CricketDelivery::Read(Stock.Ball, 0.f, FPitchConditions());
	const FShotProfile Go = CricketBatting::ChooseShot(EBatIntent::Loft, 0.f, Flight.PitchX, Flight.HeightAtBat, EBowlerType::LegSpin, 0.8f);
	FBallState AtBat = Stock.Ball;
	CricketBall::SimulateToPlane(AtBat, Go.ContactX(), FPitchConditions());
	FBatInput Meet;
	Meet.Intent = EBatIntent::Loft;
	Meet.PressTime = AtBat.Time - Go.SwingTime;
	const FDeliveryResult G = CricketDelivery::Resolve(Stock, Meet, C);
	TestTrue(*FString::Printf(TEXT("charged (%s)"), *G.Summary), G.Shot.Foot == EFootwork::Advance);
	TestTrue(*FString::Printf(TEXT("met within a metre of the pitch (pitched %.2f, met %.2f)"), G.PitchPos.X, G.Shot.ContactX()),
		G.Shot.ContactX() < G.PitchPos.X && G.Shot.ContactX() > G.PitchPos.X - 1.f);
	TestTrue(*FString::Printf(TEXT("hit (%s)"), *G.Summary), G.Contact.HasContact() && G.Dismissal == EDismissal::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOPressTimeFrameRate, "CRICKET26.Batting.PressTimeFrameRateIndependent", CricketTestFlags)
bool FSOPressTimeFrameRate::RunTest(const FString&)
{
	// The game mode samples keys at the start of a tick, before the clock advances by that tick's Dt, so a press
	// seen there happened somewhere in the Dt just gone. Stamping it must be unbiased at 30, 60 and 120 fps
	// (with frame-time jitter), or timing windows would drift with the device's frame rate.
	for (const float Fps : { 30.f, 60.f, 120.f })
	{
		FRandomStream Rng(FMath::RoundToInt(Fps));
		double SumErr = 0.0;
		float MaxErr = 0.f, MaxDt = 0.f;
		const int32 N = 2000;
		for (int32 I = 0; I < N; ++I)
		{
			const float TruePress = Rng.FRandRange(0.5f, 1.5f);
			float Clock = 0.f;
			for (;;)
			{
				const float Dt = (1.f / Fps) * Rng.FRandRange(0.8f, 1.2f);
				MaxDt = FMath::Max(MaxDt, Dt);
				if (Clock + Dt >= TruePress) // key went down during this tick's interval
				{
					const float Err = CricketMath::PressTime(Clock, Dt) - TruePress;
					SumErr += Err;
					MaxErr = FMath::Max(MaxErr, FMath::Abs(Err));
					break;
				}
				Clock += Dt;
			}
		}
		const float Bias = float(SumErr / N);
		TestTrue(*FString::Printf(TEXT("%.0f fps: mean bias %.2f ms under 1 ms"), Fps, Bias * 1000.f), FMath::Abs(Bias) < 0.001f);
		TestTrue(*FString::Printf(TEXT("%.0f fps: max error %.1f ms within half a frame"), Fps, MaxErr * 1000.f), MaxErr <= 0.5f * MaxDt + 1e-4f);
	}
	return true;
}

// ---------------------------------------------------------------- Fielding

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOFieldKeeper, "CRICKET26.Fielding.KeeperTakesBeatenBall", CricketTestFlags)
bool FSOFieldKeeper::RunTest(const FString&)
{
	// A ball left outside off must be gathered by the keeper, standing back to pace or up to spin.
	const TPair<EBowlerType, EDeliveryType> Cases[] = { { EBowlerType::Pace, EDeliveryType::Stock },
		{ EBowlerType::OffSpin, EDeliveryType::ArmBall }, { EBowlerType::LegSpin, EDeliveryType::LegBreak } };
	for (const auto& Case : Cases)
	{
		const FResolveContext C = Ctx(Case.Key);
		const FDeliveryResult R = CricketDelivery::Resolve(Release(Case.Value, 6.f, 0.45f, 0.f, Case.Key), FBatInput(), C);
		const FString Name = UEnum::GetValueAsString(Case.Key);
		TestFalse(*(Name + TEXT(" not on the pad or stumps")), R.bPadImpact || R.bStumpsHit || R.bWide);
		TestEqual(*(Name + TEXT(" no byes to the boundary")), R.Fielding.Boundary, 0);
		TestTrue(*(Name + TEXT(" keeper gathers")), R.Fielding.Fielder >= 0 && C.Field[R.Fielding.Fielder].bKeeper);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOFieldBoundaries, "CRICKET26.Fielding.CatchFourSixRunning", CricketTestFlags)
bool FSOFieldBoundaries::RunTest(const FString&)
{
	const FPitchConditions C;
	const TArray<FFielder> Field = CricketField::Make(EFieldPreset::PaceDeath, ECricketHand::Right, ECricketHand::Right);
	FCricketPlayer Skill;
	Skill.Catching = 1.f;
	auto Fly = [&](FVector Vel)
	{
		TArray<FBallState> S;
		FBallState B;
		B.Pos = FVector(2.f, 0.f, 0.8f);
		B.Vel = Vel;
		S.Add(B);
		for (int32 I = 0; I < 240 * 12; ++I)
		{
			if (CricketBall::Step(B, C) == CricketBall::EStep::Stopped) break;
			S.Add(B);
		}
		FRandomStream Rng(5);
		return CricketField::SolveFielding(S, CricketBall::FixedDt, Field, Skill, true, Rng);
	};
	const FFieldingOutcome Big = Fly(FVector(30.f, 3.f, 22.f));
	TestEqual(TEXT("clears the rope"), Big.Boundary, 6);
	// Hard along the ground through the gap between extra cover and point.
	const FVector Gap = CricketBatting::DirectionToWorld(72.f, ECricketHand::Right) * 32.f;
	const FFieldingOutcome Ground = Fly(FVector(Gap.X, Gap.Y, 0.f));
	TestEqual(TEXT("four along the ground"), Ground.Boundary, 4);
	// Gentle lob straight to midwicket's hands.
	const FVector2D MW = Field.FindByPredicate([](const FFielder& F) { return F.Position == TEXT("Midwicket"); })->Home;
	const FVector To = FVector(MW.X - 2.f, MW.Y, 0.f).GetSafeNormal();
	const FFieldingOutcome Lob = Fly(To * 13.f + FVector(0, 0, 8.f));
	TestTrue(*FString::Printf(TEXT("catch taken by %d"), Lob.Fielder), Lob.bCatchChance && Lob.bCaught);
	// Slow ball into the ring: a single with no risk.
	const FFieldingOutcome Push = Fly(FVector(Gap.X, Gap.Y, 0.f) * 0.3f);
	FRandomStream Rng(1);
	const FRunningOutcome Run = CricketField::SolveRunning(Push, FCricketPlayer(), FCricketPlayer(), FCricketPlayer(), false, 0.3f, Rng);
	TestTrue(*FString::Printf(TEXT("fielded (%d) and run (%d/%d)"), Push.Fielder, Run.Completed, Run.Attempted), Push.Fielder >= 0 && Push.Boundary == 0);
	TestFalse(TEXT("cautious running is safe"), Run.bRunOut);
	TestTrue(TEXT("a push into the ring is a single"), Run.Completed >= 1);
	// Crease to crease with the bat grounded at the far end: a single takes about 3.0-3.5 s, a two 6-6.5 s.
	TestTrue(*FString::Printf(TEXT("single in %.2f s"), Run.RunTimes.Num() ? Run.RunTimes[0] : 0.f), Run.RunTimes.Num() > 0 && Run.RunTimes[0] > 3.f && Run.RunTimes[0] < 3.5f);
	FRandomStream Rng2(1);
	const FRunningOutcome Mad = CricketField::SolveRunning(Push, FCricketPlayer(), FCricketPlayer(), FCricketPlayer(), false, -3.f, Rng2);
	TestTrue(TEXT("reckless running gets run out"), Mad.bRunOut && Mad.Attempted > Run.Attempted);
	return true;
}

// ---------------------------------------------------------------- Full match with AI on both sides

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOAIMatch, "CRICKET26.Match.AIvsAICompletesLegally", CricketTestFlags)
bool FSOAIMatch::RunTest(const FString&)
{
	int32 Fours = 0, Sixes = 0, Wickets = 0, Wides = 0, Edges = 0, Deliveries = 0, Runs1 = 0, Caught = 0, RunOuts = 0, Dots = 0, ByeBoundaries = 0;
	int32 Ran[4] = {}, Byes = 0, LegByes = 0, NoBalls = 0, Bouncers = 0, PadHits = 0, OtherOuts = 0;
	int32 Intents[4] = {}, Middled = 0, Contacts = 0, Advances = 0;
	float ExitSum = 0.f;
	int32 IntentFours[4] = {}, IntentSixes[4] = {}, IntentOuts[4] = {}, Chances = 0, Taken = 0;
	for (int32 Game = 0; Game < 40; ++Game)
	{
		FSuperOverMatch M;
		M.Start(Game % 2);
		FRandomStream Rng(Game * 31 + 7);
		TArray<int32> Recent;
		const FPitchConditions Cond;
		for (int32 Safety = 0; Safety < 200 && M.Phase != EMatchPhase::MatchComplete; ++Safety)
		{
			if (M.Phase == EMatchPhase::InningsBreak) { M.StartSecondInnings(); continue; }
			FCricketPlayer Bowler;
			Bowler.BowlerType = EBowlerType(Game % 3);
			if (Bowler.BowlerType != EBowlerType::Pace) Bowler.PaceKph = 88.f;
			FResolveContext C;
			C.Bowler = Bowler;
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
			TArray<ECricketEvent> Ev;
			if (!TestTrue(TEXT("begin"), M.BeginDelivery())) return false;
			if (!TestTrue(*FString::Printf(TEXT("outcome legal: %s"), *R.Summary), M.CompleteDelivery(R.ToOutcome(), Ev))) return false;
			FString Err;
			if (!TestTrue(Err, M.CheckInvariants(Err))) return false;
			++Deliveries;
			Fours += Ev.Contains(ECricketEvent::BoundaryFour);
			Sixes += Ev.Contains(ECricketEvent::BoundarySix);
			Wickets += Ev.Contains(ECricketEvent::Wicket);
			Wides += Ev.Contains(ECricketEvent::Wide);
			Caught += R.Dismissal == EDismissal::Caught;
			RunOuts += R.Dismissal == EDismissal::RunOut;
			Dots += Ev.Contains(ECricketEvent::DotBall);
			const FDeliveryOutcome O = R.ToOutcome();
			if (O.bBatContact && O.Boundary == 0 && O.RunsRun > 0 && O.Dismissal != EDismissal::Caught) Ran[FMath::Min(O.RunsRun, 3)]++;
			if (!O.bBatContact && !O.bWide && O.RunsRun + O.Boundary > 0) (O.bLegBye ? LegByes : Byes)++;
			NoBalls += O.bNoBall;
			Bouncers += O.bBouncer;
			PadHits += R.bPadImpact;
			OtherOuts += R.Dismissal == EDismissal::Stumped || R.Dismissal == EDismissal::HitWicket;
			Intents[int32(In.Intent)]++;
			Chances += R.Fielding.bCatchChance;
			Taken += R.Fielding.bCaught;
			IntentFours[int32(In.Intent)] += O.bBatContact && O.Boundary == 4;
			IntentSixes[int32(In.Intent)] += O.Boundary == 6;
			IntentOuts[int32(In.Intent)] += O.Dismissal != EDismissal::None;
			Advances += R.Shot.Foot == EFootwork::Advance;
			if (R.Contact.HasContact()) { ++Contacts; Middled += R.Contact.Zone == EContactZone::Middle; ExitSum += R.Contact.ExitVel.Size(); }
			if (!R.Contact.HasContact() && R.Fielding.Boundary && !R.bWide)
			{
				++ByeBoundaries;
				UE_LOG(LogTemp, Display, TEXT("Bye boundary: %s plan %s len %.1f line %+.2f, pitched (%.2f, %+.2f), keeper-side fielder %d, timing %.2f, intent %d, ball at X=0 %s, t_pitch %.2f"),
					*UEnum::GetValueAsString(Bowler.BowlerType), *Choice.Label, Choice.Plan.Length, Choice.Plan.Line, R.PitchPos.X, R.PitchPos.Y, R.Fielding.Fielder,
					Choice.ReleaseTiming, int32(In.Intent), *R.BallPath[FMath::Min(R.BallPath.Num() - 1, FMath::RoundToInt(R.ContactTime / R.SampleDt))].ToString(), R.PitchTime);
			}
			Edges += R.Contact.Zone == EContactZone::OutsideEdge || R.Contact.Zone == EContactZone::InsideEdge
				|| R.Contact.Zone == EContactZone::TopEdge || R.Contact.Zone == EContactZone::BottomEdge;
			if (M.Phase == EMatchPhase::MatchComplete && M.bTied) M.StartNextSuperOver();
		}
		TestEqual(TEXT("match finished"), M.Phase, EMatchPhase::MatchComplete);
		Runs1 += M.Innings[0].Runs;
	}
	UE_LOG(LogTemp, Display, TEXT("AI stats: 40 games, %d deliveries, avg first-innings %.1f, %d dots, %d fours, %d sixes, %d wickets (%d caught, %d run out), %d wides, %d edges"),
		Deliveries, Runs1 / 40.f, Dots, Fours, Sixes, Wickets, Caught, RunOuts, Wides, Edges);
	UE_LOG(LogTemp, Display, TEXT("AI stats: off the bat 1s %d, 2s %d, 3s+ %d; byes %d, leg byes %d, no-balls %d, bouncers %d, pad hits %d, stumped/hit wicket %d"),
		Ran[1], Ran[2], Ran[3], Byes, LegByes, NoBalls, Bouncers, PadHits, OtherOuts);
	UE_LOG(LogTemp, Display, TEXT("AI stats: intents leave %d defend %d ground %d loft %d (advance %d); contact %d, middled %d, mean exit %.1f m/s"),
		Intents[0], Intents[1], Intents[2], Intents[3], Advances, Contacts, Middled, Contacts ? ExitSum / Contacts : 0.f);
	UE_LOG(LogTemp, Display, TEXT("AI stats: 4/6/W by intent: defend %d/%d/%d, ground %d/%d/%d, loft %d/%d/%d; catches %d/%d"),
		IntentFours[1], IntentSixes[1], IntentOuts[1], IntentFours[2], IntentSixes[2], IntentOuts[2], IntentFours[3], IntentSixes[3], IntentOuts[3], Taken, Chances);
	// Scoring shape of a death over, per ball. Bands are wide on purpose: they catch a broken mechanic
	// (every ball a six, no running, no catches), not small calibration drift.
	auto Band = [this, Deliveries](const TCHAR* What, int32 N, float Lo, float Hi)
	{
		const float P = float(N) / Deliveries;
		TestTrue(*FString::Printf(TEXT("%s %.1f%% within %.0f-%.0f%%"), What, 100.f * P, 100.f * Lo, 100.f * Hi), P >= Lo && P <= Hi);
	};
	Band(TEXT("sixes"), Sixes, 0.08f, 0.25f);
	Band(TEXT("fours"), Fours, 0.07f, 0.22f);
	Band(TEXT("dots"), Dots, 0.15f, 0.40f);
	Band(TEXT("singles"), Ran[1], 0.08f, 0.35f);
	Band(TEXT("twos"), Ran[2], 0.005f, 0.12f);
	Band(TEXT("wickets"), Wickets, 0.06f, 0.18f);
	TestTrue(*FString::Printf(TEXT("catching efficiency %d/%d"), Taken, Chances), Chances > 0 && Taken >= 0.65f * Chances && Taken <= 0.92f * Chances);
	TestTrue(TEXT("not everything is middled"), Middled < 0.8f * Contacts);
	TestTrue(*FString::Printf(TEXT("batters use their feet to spin (%d advances)"), Advances), Advances >= Deliveries / 50 && Advances <= Deliveries / 6);
	TestTrue(*FString::Printf(TEXT("keeper stops balls that beat the bat (%d byes to the boundary)"), ByeBoundaries), ByeBoundaries <= Deliveries / 100);
	return true;
}

#endif
