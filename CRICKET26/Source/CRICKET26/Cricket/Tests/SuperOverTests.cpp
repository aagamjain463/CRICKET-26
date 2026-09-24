// Gate 1 automation tests: rules, strike, scoring, state, ball physics, batting contact, fielding.
// Run: UnrealEditor-Cmd CRICKET26.uproject -ExecCmds="Automation RunTests CRICKET26.; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "CricketAI.h"
#include "SuperOverGameMode.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesPartnership, "CRICKET26.Rules.PartnershipResetsOnWicket", CricketTestFlags)
bool FSORulesPartnership::RunTest(const FString&)
{
	FSuperOverMatch M;
	M.Start(0);
	Bowl(M, Four());
	Bowl(M, Wide());
	Bowl(M, Runs(2));
	TestEqual(TEXT("partnership runs, extras included"), M.Cur().PartnershipRuns, 7);
	TestEqual(TEXT("partnership balls, legal only"), M.Cur().PartnershipBalls, 2);
	Bowl(M, Out(EDismissal::Bowled));
	TestEqual(TEXT("reset by the wicket"), M.Cur().PartnershipRuns, 0);
	TestEqual(TEXT("balls reset too"), M.Cur().PartnershipBalls, 0);
	Bowl(M, Runs(1));
	TestEqual(TEXT("the new pair's first run"), M.Cur().PartnershipRuns, 1);
	TestEqual(TEXT("the new pair's first ball"), M.Cur().PartnershipBalls, 1);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallPaceVariations, "CRICKET26.Ball.PaceVariationsAndExecution", CricketTestFlags)
bool FSOBallPaceVariations::RunTest(const FString&)
{
	const FPitchConditions C;
	auto Bowl = [&C](EDeliveryType Type, float Accuracy, float Timing, int32 Seed)
	{
		FCricketPlayer Bowler;
		Bowler.Accuracy = Accuracy;
		FDeliveryPlan Plan;
		Plan.Type = Type;
		Plan.Length = 6.5f;
		return CricketBowling::Execute(Bowler, ECricketHand::Right, Plan, Timing, Seed, C);
	};
	// Sideways change in velocity where the ball pitches (m/s; + toward off for the right-hander).
	auto OffThePitch = [&C](const FDeliveryRelease& R)
	{
		FBallState B = R.Ball;
		float Before = 0.f;
		while (B.Bounces == 0 && B.Time < 3.f) { Before = B.Vel.Y; CricketBall::Step(B, C); }
		return float(B.Vel.Y) - Before;
	};
	auto Spread = [](TFunctionRef<float(int32)> Sample)
	{
		float Sum = 0.f, Sq = 0.f;
		for (int32 Seed = 0; Seed < 60; ++Seed) { const float V = Sample(Seed); Sum += V; Sq += V * V; }
		return FMath::Sqrt(FMath::Max(0.f, Sq / 60.f - FMath::Square(Sum / 60.f)));
	};
	// Friction at the bounce also checks the ball's angle across the pitch, the same for every seed; the
	// seam's unpredictable part is the spread.
	auto SeamSpread = [&](EDeliveryType Type) { return Spread([&](int32 Seed) { return OffThePitch(Bowl(Type, 1.f, 0.f, Seed)); }); };
	auto LengthSpread = [&](EDeliveryType Type, float Accuracy, float Timing)
	{
		return Spread([&](int32 Seed) { return float(Bowl(Type, Accuracy, Timing, Seed).AimedPitch.X); });
	};

	const float Stock = SeamSpread(EDeliveryType::Stock), Seam = SeamSpread(EDeliveryType::Seam), Cross = SeamSpread(EDeliveryType::CrossSeam);
	TestTrue(*FString::Printf(TEXT("seam up moves more off the pitch (%.2f vs stock %.2f m/s)"), Seam, Stock), Seam > 1.5f * Stock);
	TestTrue(*FString::Printf(TEXT("cross-seam barely moves (%.3f m/s)"), Cross), Cross < 0.03f);
	float Cut = 0.f;
	for (int32 Seed = 0; Seed < 20; ++Seed) Cut += OffThePitch(Bowl(EDeliveryType::Cutter, 1.f, 0.f, Seed)) / 20.f;
	TestTrue(*FString::Printf(TEXT("off-cutter comes back into the right-hander (%.2f m/s)"), Cut), Cut < -0.1f);
	const FBallState CrossAtBat = AtCrease(Bowl(EDeliveryType::CrossSeam, 1.f, 0.f, 1)), StockAtBat = AtCrease(Bowl(EDeliveryType::Stock, 1.f, 0.f, 1));
	TestTrue(TEXT("cross-seam a touch slower"), CrossAtBat.Time > StockAtBat.Time);

	// Execution: skill and release quality set the scatter; cross-seam is the easiest to land.
	const float Good = LengthSpread(EDeliveryType::Stock, 0.9f, 0.f), Poor = LengthSpread(EDeliveryType::Stock, 0.3f, 0.f);
	const float Mistimed = LengthSpread(EDeliveryType::Stock, 0.9f, 0.6f), Control = LengthSpread(EDeliveryType::CrossSeam, 0.9f, 0.f);
	TestTrue(*FString::Printf(TEXT("accurate bowler tighter (%.2f vs %.2f m)"), Good, Poor), Good < 0.6f * Poor);
	TestTrue(*FString::Printf(TEXT("poor release wider (%.2f vs %.2f m)"), Mistimed, Good), Mistimed > 1.3f * Good);
	TestTrue(*FString::Printf(TEXT("cross-seam tighter than stock (%.2f vs %.2f m)"), Control, Good), Control < Good);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallSpinVariations, "CRICKET26.Ball.SpinVariationsDiffer", CricketTestFlags)
bool FSOBallSpinVariations::RunTest(const FString&)
{
	const FPitchConditions C;
	struct FFlight { float Dip, Turn, Bounce; };
	auto Fly = [&C](EDeliveryType Type, float Movement = 0.6f, float Timing = 0.f)
	{
		FCricketPlayer Bowler;
		Bowler.BowlerType = EBowlerType::LegSpin;
		Bowler.PaceKph = 88.f;
		Bowler.Accuracy = 1.f;
		Bowler.Movement = Movement;
		FDeliveryPlan Plan;
		Plan.Type = Type;
		Plan.Length = 4.6f;
		const FDeliveryRelease R = CricketBowling::Execute(Bowler, ECricketHand::Right, Plan, Timing, 3, C);
		FBallState B = R.Ball;
		FFlight F{};
		FVector Pitch = FVector::ZeroVector, In = FVector::ZeroVector;
		while (B.Pos.X > CricketGeo::PoppingCrease && B.Time < 3.f)
		{
			const FVector V = B.Vel;
			if (CricketBall::Step(B, C) == CricketBall::EStep::Bounce && B.Bounces == 1)
			{
				F.Dip = FMath::RadiansToDegrees(FMath::Atan2(-V.Z, -V.X)); // descent angle onto the pitch
				Pitch = B.Pos;
				In = V;
			}
		}
		// Deviation off the pitch: angle out minus angle in, + away from the right-hander (an angle, so neither
		// length nor the line across from the bowling arm confounds it).
		F.Turn = FMath::RadiansToDegrees(FMath::Atan2(B.Pos.Y - Pitch.Y, Pitch.X - B.Pos.X) - FMath::Atan2(In.Y, -In.X));
		F.Bounce = B.Pos.Z;
		return F;
	};
	const FFlight Leg = Fly(EDeliveryType::LegBreak), Top = Fly(EDeliveryType::TopSpinner), Slide = Fly(EDeliveryType::Slider), Goog = Fly(EDeliveryType::Googly);
	UE_LOG(LogTemp, Display, TEXT("Leg spin (dip deg / turn deg / height at crease m): leg break %.1f/%.2f/%.2f, googly %.1f/%.2f/%.2f, top-spinner %.1f/%.2f/%.2f, slider %.1f/%.2f/%.2f"),
		Leg.Dip, Leg.Turn, Leg.Bounce, Goog.Dip, Goog.Turn, Goog.Bounce, Top.Dip, Top.Turn, Top.Bounce, Slide.Dip, Slide.Turn, Slide.Bounce);

	TestTrue(TEXT("top-spinner dips more than the leg break"), Top.Dip > Leg.Dip + 0.5f);
	// The extra dip is only ~0.5 deg at 88 kph (Magnus lift ~1 m/s^2), so it bounces a touch higher, not a lot.
	TestTrue(TEXT("top-spinner bounces at least as high as the leg break"), Top.Bounce >= Leg.Bounce);
	TestTrue(TEXT("top-spinner goes straight on"), FMath::Abs(Top.Turn) < 0.3f * Leg.Turn);
	TestTrue(TEXT("slider is flatter than the leg break"), Slide.Dip < Leg.Dip - 0.5f);
	TestTrue(TEXT("slider keeps lower than the leg break"), Slide.Bounce < Leg.Bounce - 0.03f);
	TestTrue(TEXT("slider turns a little, the leg-break way"), Slide.Turn > 0.f && Slide.Turn < 0.5f * Leg.Turn);
	TestTrue(TEXT("googly turns the other way, less than the leg break"), Goog.Turn < 0.f && -Goog.Turn < Leg.Turn);

	// More revs, more turn; a poor release loses revs.
	const float Big = Fly(EDeliveryType::LegBreak, 0.95f).Turn, Small = Fly(EDeliveryType::LegBreak, 0.3f).Turn, Poor = Fly(EDeliveryType::LegBreak, 0.6f, 0.7f).Turn;
	TestTrue(*FString::Printf(TEXT("big spinner turns it more (%.1f vs %.1f deg)"), Big, Small), Big > 1.3f * Small);
	TestTrue(*FString::Printf(TEXT("poor release turns less (%.1f vs %.1f deg)"), Poor, Leg.Turn), Poor < 0.85f * Leg.Turn);
	return true;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallTracking, "CRICKET26.Umpire.BallTrackingMatchesLBW", CricketTestFlags)
bool FSOBallTracking::RunTest(const FString&)
{
	// Every pad hit across lengths and lines, left alone: the tracking runs from the impact to the stumps, and its
	// three calls give exactly the LBW decision.
	int32 Outs = 0, NotOuts = 0;
	for (float Length = 2.f; Length <= 8.f; Length += 1.f)
	{
		for (float Line = -0.4f; Line <= 0.4f; Line += 0.05f)
		{
			const FDeliveryResult R = CricketDelivery::Resolve(Release(EDeliveryType::Stock, Length, Line), FBatInput(), Ctx());
			if (!R.bPadImpact) continue;
			const FBallTracking& T = R.Tracking;
		TestTrue(TEXT("impact timed after the release"), T.ImpactTime > 0.f && T.ImpactTime < R.DeadTime);
			if (!TestTrue(*FString::Printf(TEXT("tracking starts at the impact (%s)"), *R.Summary), T.Projected.Num() > 1 && T.Projected[0] == T.Impact)) return false;
			if (T.bWouldHit) TestTrue(TEXT("a ball hitting the stumps is tracked to them"), FMath::IsNearlyZero(T.Projected.Last().X, 0.01f));
			TestEqual(*FString::Printf(TEXT("calls match the decision (%s)"), *R.Summary), R.Dismissal == EDismissal::LBW,
				!T.bPitchedOutsideLeg && T.bImpactInLine && T.bWouldHit);
			TestTrue(TEXT("umpire's call only on a hit"), !T.bUmpiresCall || T.bWouldHit);
			(R.Dismissal == EDismissal::LBW ? Outs : NotOuts)++;
		}
	}
	TestTrue(*FString::Printf(TEXT("both LBWs (%d) and not-outs (%d) checked"), Outs, NotOuts), Outs > 0 && NotOuts > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOEdgeDetector, "CRICKET26.Umpire.EdgeDetectorSpikesOnlyOnTheBat", CricketTestFlags)
bool FSOEdgeDetector::RunTest(const FString&)
{
	FDeliveryResult Edge;
	Edge.Contact.Zone = EContactZone::OutsideEdge;
	Edge.ContactTime = 0.5f;
	TestEqual(TEXT("full spike at the edge"), CricketDelivery::EdgeSignal(Edge, 0.5f), 1.f);
	TestEqual(TEXT("silent before it"), CricketDelivery::EdgeSignal(Edge, 0.45f), 0.f);
	TestTrue(TEXT("rung out soon after"), CricketDelivery::EdgeSignal(Edge, 0.6f) < 0.01f);

	FDeliveryResult Pad;
	Pad.ContactTime = 0.5f; // a miss: the time the ball passed the bat
	Pad.bPadImpact = true;
	Pad.Tracking.ImpactTime = 0.53f;
	TestEqual(TEXT("no spike where the bat missed"), CricketDelivery::EdgeSignal(Pad, 0.5f) < 0.2f, true);
	TestTrue(TEXT("a low thud at the pad"), FMath::IsNearlyEqual(CricketDelivery::EdgeSignal(Pad, 0.53f), 0.3f));
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
	// Out unless the keeper fumbles the take (the take's difficulty is seeded per delivery).
	int32 Stumpings = 0;
	for (int32 Seed = 1; Seed <= 50; ++Seed)
	{
		const FDeliveryResult S = CricketDelivery::Resolve(R, Charge, Ctx(EBowlerType::LegSpin, Seed));
		TestTrue(*FString::Printf(TEXT("went down the track (%s)"), *S.Summary), S.Shot.Foot == EFootwork::Advance);
		TestFalse(TEXT("beaten"), S.Contact.HasContact());
		TestTrue(*FString::Printf(TEXT("stumped or fumbled (%s)"), *S.Summary), S.Dismissal == EDismissal::Stumped || S.Fielding.Action == EFieldAction::Fumble);
		if (S.Dismissal != EDismissal::Stumped || Stumpings++) continue;
		FSuperOverMatch M;
		M.Start(0);
		TestTrue(TEXT("stumping outcome legal"), Bowl(M, S.ToOutcome()));
		TestEqual(TEXT("recorded"), M.Cur().Batters[0].HowOut, EDismissal::Stumped);
	}
	TestTrue(*FString::Printf(TEXT("stumped on %d of 50"), Stumpings), Stumpings >= 40);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOFieldCoordinator, "CRICKET26.Fielding.CoordinatorRolesAndMotion", CricketTestFlags)
bool FSOFieldCoordinator::RunTest(const FString&)
{
	const FPitchConditions C;
	const FCricketPlayer Skill;
	auto Fly = [&](const TArray<FFielder>& Field, FVector Vel)
	{
		TArray<FBallState> S;
		FBallState B;
		B.Pos = FVector(1.f, 0.f, 0.8f);
		B.Vel = Vel;
		S.Add(B);
		for (int32 I = 0; I < 240 * 12 && CricketBall::Step(B, C) != CricketBall::EStep::Stopped; ++I) S.Add(B);
		FRandomStream Rng(5);
		return CricketField::SolveFielding(S, CricketBall::FixedDt, Field, Skill, true, Rng);
	};
	auto Has = [](const FFieldingOutcome& O, EFieldRole Role) { return O.Moves.ContainsByPredicate([Role](const FFielderMove& M) { return M.Role == Role; }); };

	int32 Checked = 0;
	for (const EFieldPreset Preset : { EFieldPreset::PaceDeath, EFieldPreset::SpinDefensive })
	{
		const TArray<FFielder> Field = CricketField::Make(Preset, ECricketHand::Right, ECricketHand::Right);
		for (float Dir = -150.f; Dir <= 150.f; Dir += 15.f)
		{
			for (const float Speed : { 9.f, 18.f, 30.f })
			{
				const FVector V = CricketBatting::DirectionToWorld(Dir, ECricketHand::Right) * Speed;
				const FFieldingOutcome O = Fly(Field, FVector(V.X, V.Y, 0.f));
				const FString Case = FString::Printf(TEXT("%s dir %.0f speed %.0f"), Preset == EFieldPreset::PaceDeath ? TEXT("pace") : TEXT("spin"), Dir, Speed);
				++Checked;
				// One job per fielder: nobody is sent two places, so nobody runs into another's path.
				TSet<int32> Seen;
				for (const FFielderMove& M : O.Moves) { bool bDup = false; Seen.Add(M.Fielder, &bDup); TestFalse(*(Case + TEXT(": one job each")), bDup); }
				if (O.Boundary) { TestTrue(*(Case + TEXT(": a boundary is chased")), Has(O, EFieldRole::Chase)); continue; }
				if (O.Fielder < 0) continue;
				TestTrue(*(Case + TEXT(": primary set")), Has(O, EFieldRole::Primary));
				TestTrue(*(Case + TEXT(": stumps covered")), Field[O.Fielder].bKeeper || Field[O.Fielder].bBowler || (O.CoverTime[0] < 5.f && O.CoverTime[1] < 5.f));
				if (!Field[O.Fielder].bKeeper) TestTrue(*(Case + TEXT(": backed up")), Has(O, EFieldRole::Backup));
				// The presented primary reaches the ball within their dive at the time the solver says - no teleport.
				const FFielderMove& P = *O.Moves.FindByPredicate([](const FFielderMove& M) { return M.Role == EFieldRole::Primary; });
				const float Gap = FVector2D::Distance(CricketField::PositionOf(P, Field[O.Fielder], O.FieldTime, Skill.RunSpeed), FVector2D(O.FieldPos.X, O.FieldPos.Y));
				TestTrue(*FString::Printf(TEXT("%s: primary in reach at the take (%.2f m)"), *Case, Gap), Gap <= (Field[O.Fielder].bKeeper ? 2.8f : 2.3f) + 0.05f);
			}
		}
	}
	// Motion obeys the speed and acceleration limits.
	const FFielder Mark{ TEXT("Test"), FVector2D(10.f, 10.f) };
	const FFielderMove Run{ 0, EFieldRole::Backup, 0.4f, FVector2D(60.f, 10.f) };
	float MaxV = 0.f, MaxA = 0.f, PrevV = 0.f;
	for (float T = 0.f; T < 9.f; T += 0.05f)
	{
		const float V = FVector2D::Distance(CricketField::PositionOf(Run, Mark, T + 0.05f, 7.f), CricketField::PositionOf(Run, Mark, T, 7.f)) / 0.05f;
		MaxV = FMath::Max(MaxV, V);
		MaxA = FMath::Max(MaxA, (V - PrevV) / 0.05f);
		PrevV = V;
	}
	TestTrue(*FString::Printf(TEXT("top speed respected (%.2f m/s)"), MaxV), MaxV <= 7.01f);
	TestTrue(*FString::Printf(TEXT("acceleration respected (%.2f m/s^2)"), MaxA), MaxA <= 6.3f);
	TestEqual(TEXT("stops at the target"), CricketField::PositionOf(Run, Mark, 30.f, 7.f), Run.Target);

	// Keeper standing back needs time to reach the stumps; standing up to spin he is already there.
	const TArray<FFielder> PaceField = CricketField::Make(EFieldPreset::PaceDeath, ECricketHand::Right, ECricketHand::Right);
	const TArray<FFielder> SpinField = CricketField::Make(EFieldPreset::SpinDefensive, ECricketHand::Right, ECricketHand::Right);
	const FVector Cover = CricketBatting::DirectionToWorld(45.f, ECricketHand::Right) * 12.f;
	const FFieldingOutcome Back = Fly(PaceField, FVector(Cover.X, Cover.Y, 0.f)), Up = Fly(SpinField, FVector(Cover.X, Cover.Y, 0.f));
	TestTrue(*FString::Printf(TEXT("keeper standing back runs up (%.2f s)"), Back.CoverTime[0]), Back.CoverTime[0] > 2.f);
	TestEqual(TEXT("keeper standing up is at the stumps"), Up.CoverTime[0], 0.f);

	// Throw target: to the end where the stumps can be broken soonest.
	auto Throw = [](FVector2D At, FVector2D From, float CoverStriker, float CoverBowler)
	{
		FFieldingOutcome Fd;
		Fd.Fielder = 3;
		Fd.FieldTime = 1.5f;
		Fd.FieldPos = FVector(At.X, At.Y, 0.1f);
		Fd.FielderFrom = From;
		Fd.CoverTime[0] = CoverStriker;
		Fd.CoverTime[1] = CoverBowler;
		FRandomStream Rng(2);
		return CricketField::SolveRunning(Fd, FCricketPlayer(), FCricketPlayer(), FCricketPlayer(), false, 0.3f, Rng);
	};
	TestFalse(TEXT("gathered near the bowler's end: throw there"), Throw(FVector2D(19.f, 12.f), FVector2D(19.f, 25.f), 0.f, 0.f).bThrowToStrikerEnd);
	TestTrue(TEXT("nobody at the bowler's end: throw to the keeper"), Throw(FVector2D(12.f, 15.f), FVector2D(12.f, 25.f), 0.f, 30.f).bThrowToStrikerEnd);
	// Running away from the target, the fielder has to turn before throwing.
	const FRunningOutcome Toward = Throw(FVector2D(5.f, 20.f), FVector2D(5.f, 30.f), 0.f, 30.f), Away = Throw(FVector2D(5.f, 20.f), FVector2D(5.f, 10.f), 0.f, 30.f);
	TestTrue(*FString::Printf(TEXT("turning to throw costs time (%.2f vs %.2f s)"), Away.ThrowRelease, Toward.ThrowRelease), Away.ThrowRelease > Toward.ThrowRelease + 0.15f);
	UE_LOG(LogTemp, Display, TEXT("Coordinator: %d cases; keeper back covers at %.2f s"), Checked, Back.CoverTime[0]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORunningCalls,"CRICKET26.Running.CallsSendBacksAndCloseCalls", CricketTestFlags)
bool FSORunningCalls::RunTest(const FString&)
{
	// Sweep balls gathered at 8-55 m from the bat, at 0.6-6 s, some at full stretch, under the three human
	// running modes (R: safe 0.6, normal 0.25, aggressive -0.1 s margin).
	const float Margins[] = { 0.6f, 0.25f, -0.1f };
	int32 Balls = 0, RunOuts[3] = {}, Runs[3] = {}, SentBack = 0, SavedBySendBack = 0, Close = 0, Bad = 0;
	for (int32 Mode = 0; Mode < 3; ++Mode)
	{
		FRandomStream Pick(11);
		for (int32 I = 0; I < 1500; ++I)
		{
			FFieldingOutcome Fd;
			Fd.Fielder = 3;
			const float Dist = Pick.FRandRange(8.f, 55.f), Ang = Pick.FRandRange(-PI, PI);
			Fd.FieldPos = FVector(Dist * FMath::Cos(Ang), Dist * FMath::Sin(Ang), 0.1f);
			Fd.FieldTime = Pick.FRandRange(0.6f, 6.f);
			Fd.bDive = Pick.GetFraction() < 0.2f;
			FRandomStream Rng(I);
			const FRunningOutcome R = CricketField::SolveRunning(Fd, FCricketPlayer(), FCricketPlayer(), FCricketPlayer(), false, Margins[Mode], Rng);
			Balls += Mode == 0;
			RunOuts[Mode] += R.bRunOut;
			Runs[Mode] += R.Completed;
			// Consistency: a run-out is a lost race, every run scored beat the break, a send-back comes before halfway.
			const float Home = R.bSentBack ? R.BackIn : R.RunTimes.Num() ? R.RunTimes.Last() : 0.f;
			const bool bOk = (R.bRunOut == (R.Attempted > 0 && Home > R.BreakTime))
				&& R.Completed == R.Attempted - (R.bRunOut || R.bSentBack ? 1 : 0)
				&& (!R.bSentBack || (R.SentBackFrom < 0.5f && R.SentBackAt >= Fd.FieldTime && R.BackIn > R.SentBackAt));
			if (!bOk && Bad++ < 5)
			{
				AddError(FString::Printf(TEXT("mode %d case %d: attempted %d completed %d out %d sent back %d (%.2f at %.2f, in %.2f) break %.2f"),
					Mode, I, R.Attempted, R.Completed, R.bRunOut, R.bSentBack, R.SentBackFrom, R.SentBackAt, R.BackIn, R.BreakTime));
			}
			if (Mode == 1)
			{
				SentBack += R.bSentBack;
				SavedBySendBack += R.bSentBack && !R.bRunOut;
				Close += !R.bRunOut && R.Completed > 0 && R.BreakTime - Home < 0.3f;
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("Running sweep (%d balls): runs safe/normal/aggressive %d/%d/%d, run outs %d/%d/%d; normal: %d sent back (%d safe), %d close calls"),
		Balls, Runs[0], Runs[1], Runs[2], RunOuts[0], RunOuts[1], RunOuts[2], SentBack, SavedBySendBack, Close);
	TestEqual(TEXT("consistent outcomes"), Bad, 0);
	TestTrue(TEXT("aggressive running scores more"), Runs[2] > Runs[1] && Runs[1] > Runs[0]);
	TestTrue(TEXT("aggressive running risks more"), RunOuts[2] > RunOuts[1] && RunOuts[1] >= RunOuts[0]);
	TestTrue(TEXT("safe running is rarely run out"), RunOuts[0] <= Balls / 100);
	TestTrue(TEXT("misjudged calls are sent back, mostly in time"), SentBack > 0 && SavedBySendBack > SentBack / 2);
	TestTrue(TEXT("close calls happen"), Close > 0);
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
	// A value-driven batter lofts almost every ball it can reach in a Super Over, as real ones do.
	Band(TEXT("sixes"), Sixes, 0.08f, 0.30f);
	Band(TEXT("fours"), Fours, 0.07f, 0.22f);
	Band(TEXT("dots"), Dots, 0.10f, 0.40f); // Super Over batters swing at everything: 0 dots in 12 balls in the 2019 World Cup final
	Band(TEXT("singles"), Ran[1], 0.08f, 0.35f);
	Band(TEXT("twos"), Ran[2], 0.005f, 0.12f);
	Band(TEXT("wickets"), Wickets, 0.06f, 0.18f);
	Band(TEXT("run outs"), RunOuts, 0.f, 0.02f);
	TestTrue(*FString::Printf(TEXT("catching efficiency %d/%d"), Taken, Chances), Chances > 0 && Taken >= 0.65f * Chances && Taken <= 0.92f * Chances);
	TestTrue(TEXT("not everything is middled"), Middled < 0.8f * Contacts);
	TestTrue(*FString::Printf(TEXT("batters use their feet to spin (%d advances)"), Advances), Advances >= Deliveries / 50 && Advances <= Deliveries / 6);
	TestTrue(*FString::Printf(TEXT("keeper stops balls that beat the bat (%d byes to the boundary)"), ByeBoundaries), ByeBoundaries <= Deliveries / 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOFieldActions, "CRICKET26.Fielding.ActionsCatchesThrows", CricketTestFlags)
bool FSOFieldActions::RunTest(const FString&)
{
	const FPitchConditions C;
	const FCricketPlayer Skill;
	auto Fly = [&](const TArray<FFielder>& Field, FVector Vel, int32 Seed)
	{
		TArray<FBallState> S;
		FBallState B;
		B.Pos = FVector(1.f, 0.f, 0.8f);
		B.Vel = Vel;
		S.Add(B);
		for (int32 I = 0; I < 240 * 12 && CricketBall::Step(B, C) != CricketBall::EStep::Stopped; ++I) S.Add(B);
		FRandomStream Rng(Seed);
		return CricketField::SolveFielding(S, CricketBall::FixedDt, Field, Skill, true, Rng);
	};

	// Every kind of take turns up across a sweep of ground balls and lofted shots round the field.
	TMap<EFieldAction, int32> Seen;
	int32 Ground = 0, Fumbles = 0, RopeSixes = 0;
	for (const EFieldPreset Preset : { EFieldPreset::PaceDeath, EFieldPreset::SpinDefensive })
	{
		const TArray<FFielder> Field = CricketField::Make(Preset, ECricketHand::Right, ECricketHand::Right);
		for (float Dir = -150.f; Dir <= 150.f; Dir += 10.f)
		{
			for (const float Speed : { 6.f, 12.f, 20.f, 30.f })
			{
				for (const float Up : { 0.f, 3.f, 8.f, 11.f, 14.f, 17.f, 20.f })
				{
					const FVector V = CricketBatting::DirectionToWorld(Dir, ECricketHand::Right) * Speed;
					const FFieldingOutcome O = Fly(Field, FVector(V.X, V.Y, Up), int32(Dir) * 7 + int32(Speed) + int32(Up) * 13);
					if (O.Fielder < 0) continue;
					if (O.Boundary) { RopeSixes += O.Boundary == 6; continue; }
					Seen.FindOrAdd(O.Action)++;
					if (!O.bCatchChance && O.Action != EFieldAction::KeeperTake) { ++Ground; Fumbles += O.Action == EFieldAction::Fumble; }
					TestTrue(TEXT("every take has an action"), O.Action != EFieldAction::None);
					TestEqual(TEXT("catch kinds only in the air"), O.bCatchChance, O.Action >= EFieldAction::CatchFlat && O.Action <= EFieldAction::CatchRelay);
					if (O.Action == EFieldAction::CatchDiving || O.Action == EFieldAction::DiveStop) TestTrue(TEXT("dives are at full stretch"), O.bDive);
				}
			}
		}
	}
	FString Counts;
	for (const TPair<EFieldAction, int32>& P : Seen) Counts += FString::Printf(TEXT("%s %d, "), CricketField::ActionName(P.Key), P.Value);
	for (const EFieldAction A : { EFieldAction::PickupClean, EFieldAction::PickupOnRun, EFieldAction::LongBarrier, EFieldAction::SlideStop,
		EFieldAction::DiveStop, EFieldAction::Fumble, EFieldAction::CatchFlat, EFieldAction::CatchHigh, EFieldAction::CatchDiving, EFieldAction::CatchBoundary })
	{
		TestTrue(*FString::Printf(TEXT("%s seen"), CricketField::ActionName(A)), Seen.Contains(A));
	}
	const float FumbleRate = float(Fumbles) / FMath::Max(1, Ground);
	TestTrue(*FString::Printf(TEXT("misfields are occasional (%.1f%%)"), 100.f * FumbleRate), FumbleRate > 0.01f && FumbleRate < 0.12f);

	// The kind of take sets how soon the throw goes: attacking pickup, clean pickup, long barrier, slide, misfield.
	auto Release = [](EFieldAction A, FVector2D At)
	{
		FFieldingOutcome Fd;
		Fd.Fielder = 3;
		Fd.FieldTime = 2.f;
		Fd.FieldPos = FVector(At.X, At.Y, 0.1f);
		Fd.FielderFrom = At;
		Fd.Action = A;
		FRandomStream Rng(3);
		return CricketField::SolveRunning(Fd, FCricketPlayer(), FCricketPlayer(), FCricketPlayer(), false, 0.3f, Rng);
	};
	const FVector2D Ring(10.f, 25.f);
	const float OnRun = Release(EFieldAction::PickupOnRun, Ring).ThrowRelease, Clean = Release(EFieldAction::PickupClean, Ring).ThrowRelease,
		Barrier = Release(EFieldAction::LongBarrier, Ring).ThrowRelease, Slide = Release(EFieldAction::SlideStop, Ring).ThrowRelease,
		Fumble = Release(EFieldAction::Fumble, Ring).ThrowRelease;
	TestTrue(*FString::Printf(TEXT("release order %.2f < %.2f < %.2f < %.2f < %.2f"), OnRun, Clean, Barrier, Slide, Fumble),
		OnRun < Clean && Clean < Barrier && Barrier < Slide && Slide < Fumble);
	// Close in, a pickup on the run is flicked underarm; further out it is thrown overarm.
	TestEqual(TEXT("underarm from 8 m"), Release(EFieldAction::PickupOnRun, FVector2D(18.f, 6.f)).ThrowType, EThrowType::Underarm);
	TestEqual(TEXT("overarm from the ring"), Release(EFieldAction::PickupOnRun, Ring).ThrowType, EThrowType::Overarm);

	// Long throws have to be put up: 70 m takes well over twice as long as 35 m.
	TestTrue(TEXT("long throws lose pace"), CricketField::ThrowFlight(70.f, 0.6f) > 2.2f * CricketField::ThrowFlight(35.f, 0.6f));

	// From the rope a free fielder comes in to relay it, and it gets there sooner than the one long throw.
	const TArray<FFielder> Field = CricketField::Make(EFieldPreset::PaceDeath, ECricketHand::Right, ECricketHand::Right);
	FFieldingOutcome Deep;
	Deep.Fielder = Field.IndexOfByPredicate([](const FFielder& F) { return F.Position == TEXT("Deep midwicket"); });
	// Stopped at the rope behind deep midwicket, 59 m from the bowler's stumps and 70 m from the keeper's.
	const FVector Rope = FVector(0.5f * CricketGeo::PitchLength, 0.f, 0.1f) + FVector(CricketBatting::DirectionToWorld(-58.f, ECricketHand::Right).GetSafeNormal2D() * 64.f);
	Deep.FieldPos = Rope;
	Deep.FielderFrom = Field[Deep.Fielder].Home;
	Deep.FieldTime = 3.f;
	Deep.Action = EFieldAction::PickupClean;
	Deep.CoverTime[0] = Deep.CoverTime[1] = 0.f;
	FRandomStream R1(4), R2(4);
	const FRunningOutcome Relayed = CricketField::SolveRunning(Deep, FCricketPlayer(), FCricketPlayer(), FCricketPlayer(), false, 0.3f, R1, &Field);
	const FRunningOutcome Direct = CricketField::SolveRunning(Deep, FCricketPlayer(), FCricketPlayer(), FCricketPlayer(), false, 0.3f, R2);
	TestEqual(TEXT("relay from deep midwicket"), Relayed.ThrowType, EThrowType::Relay);
	const int32 Who = Relayed.RelayMove.Fielder;
	TestTrue(TEXT("relay fielder is free"), Field.IsValidIndex(Who) && Who != Deep.Fielder && !Field[FMath::Max(Who, 0)].bKeeper && !Field[FMath::Max(Who, 0)].bBowler);
	if (Field.IsValidIndex(Who))
	{
		const float There = Relayed.RelayMove.Start + CricketField::TimeToCover(FVector2D::Distance(Field[Who].Home, Relayed.RelayMove.Target), Skill.RunSpeed);
		TestTrue(*FString::Printf(TEXT("relay man in place (%.2f) for the take (%.2f)"), There, Relayed.RelayCatch), There <= Relayed.RelayCatch + 1e-3f);
	}
	TestTrue(*FString::Printf(TEXT("relay beats the long throw (%.2f vs %.2f s)"), Relayed.ThrowArrive, Direct.ThrowArrive), Relayed.ThrowArrive < Direct.ThrowArrive);
	UE_LOG(LogTemp, Display, TEXT("Fielding actions: %s%d ground, %d misfields, %d carried over the rope; relay %.2f vs direct %.2f s"),
		*Counts, Ground, Fumbles, RopeSixes, Relayed.ThrowArrive, Direct.ThrowArrive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOKeeper, "CRICKET26.Keeper.TakesAndStumping", CricketTestFlags)
bool FSOKeeper::RunTest(const FString&)
{
	// Keeper's takes of balls left alone: standing back to pace is routine, standing up to spin is harder,
	// and down the leg side (unsighted behind the batter) harder still.
	auto FumbleRate = [&](EBowlerType Style, EDeliveryType Type, float Line, int32& Takes)
	{
		const FDeliveryRelease R = Release(Type, Style == EBowlerType::Pace ? 7.f : 4.8f, Line, 0.f, Style);
		int32 Fumbles = 0;
		Takes = 0;
		for (int32 Seed = 1; Seed <= 400; ++Seed)
		{
			const FResolveContext C = Ctx(Style, Seed);
			const FDeliveryResult D = CricketDelivery::Resolve(R, FBatInput(), C);
			if (D.Fielding.Fielder < 0 || !C.Field[D.Fielding.Fielder].bKeeper || D.Dismissal != EDismissal::None) continue;
			++Takes;
			Fumbles += D.Fielding.Action == EFieldAction::Fumble;
		}
		return float(Fumbles) / FMath::Max(1, Takes);
	};
	int32 N1, N2, N3;
	const float Pace = FumbleRate(EBowlerType::Pace, EDeliveryType::Stock, 0.5f, N1);
	const float SpinOff = FumbleRate(EBowlerType::LegSpin, EDeliveryType::LegBreak, 0.6f, N2);
	const float SpinLeg = FumbleRate(EBowlerType::LegSpin, EDeliveryType::Googly, -0.9f, N3);
	TestTrue(*FString::Printf(TEXT("keeper took them (%d/%d/%d)"), N1, N2, N3), N1 > 300 && N2 > 300 && N3 > 300);
	TestTrue(*FString::Printf(TEXT("fumbles: pace back %.1f%% < spin up %.1f%% < down leg %.1f%%"), 100.f * Pace, 100.f * SpinOff, 100.f * SpinLeg),
		Pace < SpinOff && SpinOff < SpinLeg && Pace < 0.05f && SpinLeg < 0.25f);

	// Reaching forward in the crease for a spinner wide of off and missing: the back foot can come up and the
	// keeper standing up stumps them. Less often for a better technique, and never against pace (keeper back).
	auto Overbalanced = [&](EBowlerType Style, float Technique, int32& Beaten)
	{
		const FDeliveryRelease R = Release(Style == EBowlerType::Pace ? EDeliveryType::Outswing : EDeliveryType::LegBreak, 4.6f, 1.0f, 0.f, Style);
		const FBallRead Seen = CricketDelivery::Read(R.Ball, 0.f, FPitchConditions());
		int32 Stumped = 0;
		Beaten = 0;
		for (int32 Seed = 1; Seed <= 400; ++Seed)
		{
			FResolveContext C = Ctx(Style, Seed);
			C.Striker.Technique = Technique;
			const FDeliveryResult D = CricketDelivery::Resolve(R, FBatInput{ EBatIntent::Ground, 30.f, Seen.ArrivalTime - 0.3f }, C);
			if (D.Shot.Foot != EFootwork::Front || D.Contact.HasContact() || D.bPadImpact) continue;
			++Beaten;
			Stumped += D.Dismissal == EDismissal::Stumped;
			if (D.Dismissal == EDismissal::Stumped) TestTrue(*FString::Printf(TEXT("named (%s)"), *D.Summary), D.Summary.Contains(TEXT("overbalanced")));
		}
		return Stumped;
	};
	int32 B1, B2, B3;
	const int32 Poor = Overbalanced(EBowlerType::LegSpin, 0.f, B1), Sound = Overbalanced(EBowlerType::LegSpin, 1.f, B2), Quick = Overbalanced(EBowlerType::Pace, 0.f, B3);
	TestTrue(*FString::Printf(TEXT("poor technique stumped %d of %d beaten"), Poor, B1), B1 > 50 && Poor > 0 && Poor < B1 / 3);
	TestEqual(*FString::Printf(TEXT("sound technique never overbalances (%d beaten)"), B2), Sound, 0);
	TestEqual(*FString::Printf(TEXT("not against pace (%d beaten)"), B3), Quick, 0);
	UE_LOG(LogTemp, Display, TEXT("Keeper: fumbles pace back %.1f%%, spin up %.1f%%, down leg %.1f%%; overbalanced stumpings %d/%d"),
		100.f * Pace, 100.f * SpinOff, 100.f * SpinLeg, Poor, B1);
	return true;
}

// ---------------------------------------------------------------- AI difficulty

namespace
{
	struct FDifficultyStats { int32 Innings = 0, Runs = 0, Wickets = 0, RunOuts = 0, Balls = 0, Sixes = 0; };

	/** Whole Super Overs with the batting AI and the bowling AI at separate skills; same seeds, same players
	  * unless Setup changes them. */
	FDifficultyStats PlaySuperOvers(int32 Games, float BatSkill, float BowlSkill,
		TFunctionRef<void(FResolveContext&, const FSuperOverMatch&)> Setup = [](FResolveContext&, const FSuperOverMatch&) {})
	{
		FDifficultyStats S;
		const FPitchConditions Cond;
		for (int32 Game = 0; Game < Games; ++Game)
		{
			FSuperOverMatch M;
			M.Start(Game % 2);
			FRandomStream Rng(Game * 131 + 17);
			TArray<int32> Recent;
			for (int32 Safety = 0; Safety < 200 && M.Phase != EMatchPhase::MatchComplete; ++Safety)
			{
				if (M.Phase == EMatchPhase::InningsBreak) { M.StartSecondInnings(); continue; }
				FCricketPlayer Bowler;
				Bowler.BowlerType = EBowlerType(Game % 3);
				if (Bowler.BowlerType != EBowlerType::Pace) Bowler.PaceKph = 88.f;
				FResolveContext C;
				C.Bowler = Bowler;
				Setup(C, M);
				Bowler = C.Bowler;
				C.Field = CricketField::Make(CricketField::PresetFor(Bowler.BowlerType), C.Striker.BatHand, Bowler.BowlHand);
				C.bFreeHit = M.bFreeHit;
				C.Rules = M.Rules;
				C.BouncersBowled = M.Cur().Bouncers;
				// Separate streams per role so one side's skill cannot shift the other side's dice.
				C.Seed = Rng.RandHelper(1 << 20);
				FRandomStream BowlRng(C.Seed + 3), BatRng(C.Seed + 5);
				const float Aggr = CricketAI::Aggression(M);
				C.RunMargin = CricketAI::RunMargin(M, Aggr, BatSkill);
				const FBowlingChoice Choice = CricketAI::ChooseDelivery(Bowler, C.Striker.BatHand, M, Recent, BowlRng, BowlSkill);
				Recent.Add(Choice.PlanId);
				const FDeliveryRelease Rel = CricketBowling::Execute(Bowler, C.Striker.BatHand, Choice.Plan, Choice.ReleaseTiming, C.Seed, Cond);
				const FBatInput In = CricketAI::ChooseShot(Rel, C.Striker, Bowler.BowlerType, Aggr, C.Field, Cond, BatRng, BatSkill);
				const FDeliveryResult R = CricketDelivery::Resolve(Rel, In, C);
				TArray<ECricketEvent> Ev;
				if (!M.BeginDelivery() || !M.CompleteDelivery(R.ToOutcome(), Ev)) return S;
				++S.Balls;
				S.Wickets += Ev.Contains(ECricketEvent::Wicket);
				S.RunOuts += R.Dismissal == EDismissal::RunOut;
				S.Sixes += Ev.Contains(ECricketEvent::BoundarySix);
				if (M.Phase == EMatchPhase::MatchComplete && M.bTied)
				{
					S.Runs += M.Innings[0].Runs + M.Innings[1].Runs;
					S.Innings += 2;
					M.StartNextSuperOver();
				}
			}
			if (M.Phase == EMatchPhase::MatchComplete)
			{
				S.Runs += M.Innings[0].Runs + M.Innings[1].Runs;
				S.Innings += 2;
			}
		}
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOAIShotKnowledge, "CRICKET26.AI.ShotKnowledge", CricketTestFlags)
bool FSOAIShotKnowledge::RunTest(const FString&)
{
	// Re-measures the batting AI's stroke values against the physics: every stroke played to the same
	// deliveries, grouped by how the batter classes the ball. The AI's table must rank the strokes the
	// way the physics does at every wicket cost it uses, or its "experience" is stale.
	using namespace CricketAI;
	const FPitchConditions Cond;
	const TCHAR* ClassNames[] = { TEXT("full toss"), TEXT("block hole"), TEXT("slot"), TEXT("length"), TEXT("short") };
	for (int32 Spin = 0; Spin < 2; ++Spin)
	{
		float Runs[5][3] = {}, Outs[5][3] = {};
		int32 N[5] = {};
		for (int32 I = 0; I < 6000; ++I)
		{
			FSuperOverMatch M;
			M.Start(0);
			FCricketPlayer Bowler;
			Bowler.BowlerType = Spin ? EBowlerType(1 + I % 2) : EBowlerType::Pace;
			if (Spin) Bowler.PaceKph = 88.f;
			FResolveContext Ctx;
			Ctx.Bowler = Bowler;
			Ctx.Field = CricketField::Make(CricketField::PresetFor(Bowler.BowlerType), Ctx.Striker.BatHand, Bowler.BowlHand);
			Ctx.Rules = M.Rules;
			Ctx.Seed = I * 7 + 1;
			Ctx.RunMargin = 0.2f;
			FRandomStream PlanRng(I);
			TArray<int32> Recent;
			FBowlingChoice Ch = ChooseDelivery(Bowler, Ctx.Striker.BatHand, M, Recent, PlanRng);
			Ch.Plan.Length = 1.f + 10.f * PlanRng.GetFraction();
			const FDeliveryRelease Rel = CricketBowling::Execute(Bowler, Ctx.Striker.BatHand, Ch.Plan, Ch.ReleaseTiming, Ctx.Seed, Cond);
			const FBallRead First = CricketDelivery::Read(Rel.Ball, 0.f, Cond);
			const int32 K = int32(ClassOf(CricketDelivery::Read(Rel.Ball, FMath::Max(0.05f, First.ArrivalTime - 0.35f), Cond), Spin != 0));
			++N[K];
			for (int32 S = 0; S < 3; ++S)
			{
				FRandomStream BatRng(I + 99991);
				const FBatInput In = PlayIntent(EBatIntent(int32(EBatIntent::Defend) + S), Rel, Ctx.Striker, Bowler.BowlerType, Ctx.Field, Cond, BatRng);
				const FDeliveryOutcome O = CricketDelivery::Resolve(Rel, In, Ctx).ToOutcome();
				Runs[K][S] += O.RunsRun + O.Boundary + ((O.bWide || O.bNoBall) ? 1 : 0);
				Outs[K][S] += O.Dismissal != EDismissal::None;
			}
		}
		for (int32 K = 0; K < 5; ++K)
		{
			if (N[K] == 0) continue;
			FString Row;
			for (int32 S = 0; S < 3; ++S) Row += FString::Printf(TEXT("{ %.2ff, %.3ff }, "), Runs[K][S] / N[K], Outs[K][S] / N[K]);
			UE_LOG(LogTemp, Display, TEXT("Shot knowledge %s %s (n %d): { %s}"), Spin ? TEXT("spin") : TEXT("pace"), ClassNames[K], N[K], *Row);
			if (N[K] < 150) continue; // too rare to rank reliably
			for (const float Cost : { WicketCost(0.1f), WicketCost(0.5f), WicketCost(1.f) })
			{
				int32 Measured = 0, Known = 0;
				float BestM = -1e9f, BestK = -1e9f;
				for (int32 S = 0; S < 3; ++S)
				{
					float R, O;
					ShotValue(EBallClass(K), Spin != 0, EBatIntent(int32(EBatIntent::Defend) + S), R, O);
					const float VM = (Runs[K][S] - Cost * Outs[K][S]) / N[K], VK = R - Cost * O;
					if (VM > BestM) { BestM = VM; Measured = S; }
					if (VK > BestK) { BestK = VK; Known = S; }
				}
				const float Chosen = (Runs[K][Known] - Cost * Outs[K][Known]) / N[K];
				TestTrue(*FString::Printf(TEXT("%s %s at wicket cost %.1f: AI picks stroke %d, physics says %d (%.2f vs %.2f runs)"),
					Spin ? TEXT("spin") : TEXT("pace"), ClassNames[K], Cost, Known, Measured, Chosen, BestM), BestM - Chosen < 0.15f);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOAIDifficulty, "CRICKET26.AI.DifficultyIsDecisionQuality", CricketTestFlags)
bool FSOAIDifficulty::RunTest(const FString&)
{
	// The same players (identical timing, accuracy, pace, power) and the same physics at every level:
	// only the AI's decisions change, so any gap in results comes from how well it decides.
	constexpr int32 Games = 150;
	const float Hard = CricketAI::DefaultSkill;
	auto PerInnings = [](const FDifficultyStats& S) { return S.Innings ? float(S.Runs) / S.Innings : 0.f; };
	const FDifficultyStats BatEasy = PlaySuperOvers(Games, 0.f, Hard), BatHard = PlaySuperOvers(Games, Hard, Hard),
		BatLegend = PlaySuperOvers(Games, 1.f, Hard);
	const FDifficultyStats BowlEasy = PlaySuperOvers(Games, Hard, 0.f), BowlLegend = PlaySuperOvers(Games, Hard, 1.f);
	UE_LOG(LogTemp, Display, TEXT("Difficulty: batting AI easy/hard/legend %.1f/%.1f/%.1f runs per innings, %d/%d/%d wickets, %d/%d/%d run outs"),
		PerInnings(BatEasy), PerInnings(BatHard), PerInnings(BatLegend), BatEasy.Wickets, BatHard.Wickets, BatLegend.Wickets,
		BatEasy.RunOuts, BatHard.RunOuts, BatLegend.RunOuts);
	UE_LOG(LogTemp, Display, TEXT("Difficulty: bowling AI easy/hard/legend concedes %.1f/%.1f/%.1f runs per innings, %d/%d/%d wickets"),
		PerInnings(BowlEasy), PerInnings(BatHard), PerInnings(BowlLegend), BowlEasy.Wickets, BatHard.Wickets, BowlLegend.Wickets);
	TestTrue(TEXT("all levels complete their matches"), BatEasy.Innings >= 2 * Games && BowlLegend.Innings >= 2 * Games);
	TestTrue(TEXT("a better batting AI scores more"), PerInnings(BatEasy) < PerInnings(BatHard) && PerInnings(BatHard) < PerInnings(BatLegend));
	TestTrue(TEXT("an easy batting AI gives its wicket away more"), BatEasy.Wickets > BatHard.Wickets);
	TestTrue(TEXT("an easy batting AI runs itself out more"), BatEasy.RunOuts > BatHard.RunOuts);
	TestTrue(TEXT("a better bowling AI concedes less"), PerInnings(BowlLegend) < PerInnings(BatHard) && PerInnings(BatHard) < PerInnings(BowlEasy));
	TestTrue(TEXT("the easy gap is felt: at least 10% more runs off an easy bowler"), PerInnings(BowlEasy) > 1.1f * PerInnings(BatHard));
	for (const CricketAI::EDifficulty D : { CricketAI::EDifficulty::Easy, CricketAI::EDifficulty::Medium, CricketAI::EDifficulty::Hard })
		TestTrue(TEXT("levels ascend"), CricketAI::SkillOf(D) < CricketAI::SkillOf(CricketAI::EDifficulty(uint8(D) + 1)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOAttributeCurves, "CRICKET26.AI.AttributeCurves", CricketTestFlags)
bool FSOAttributeCurves::RunTest(const FString&)
{
	// Each attribute, weak (0.2) against strong (0.95) with everything else equal, over whole AI Super Overs:
	// it must move the result the way cricket says, by a noticeable amount, and a strong player must not
	// break the game.
	constexpr int32 Games = 400;
	const float Hard = CricketAI::DefaultSkill;
	struct FCase { const TCHAR* Name; TFunction<void(FResolveContext&, float)> Set; bool bBatting; };
	const FCase Cases[] = {
		{ TEXT("batting Timing"), [](FResolveContext& C, float V) { C.Striker.Timing = C.NonStriker.Timing = V; }, true },
		{ TEXT("batting Technique"), [](FResolveContext& C, float V) { C.Striker.Technique = C.NonStriker.Technique = V; }, true },
		{ TEXT("batting Power"), [](FResolveContext& C, float V) { C.Striker.Power = C.NonStriker.Power = V; }, true },
		{ TEXT("bowler Accuracy"), [](FResolveContext& C, float V) { C.Bowler.Accuracy = V; }, false },
		{ TEXT("bowler Movement"), [](FResolveContext& C, float V) { C.Bowler.Movement = V; }, false },
		{ TEXT("fielding Catching"), [](FResolveContext& C, float V) { C.Fielding.Catching = V; }, false },
		{ TEXT("fielders' RunSpeed"), [](FResolveContext& C, float V) { C.Fielding.RunSpeed = 5.5f + 3.f * V; }, false },
	};
	for (const FCase& Case : Cases)
	{
		const FDifficultyStats Lo = PlaySuperOvers(Games, Hard, Hard, [&](FResolveContext& C, const FSuperOverMatch&) { Case.Set(C, 0.2f); });
		const FDifficultyStats Hi = PlaySuperOvers(Games, Hard, Hard, [&](FResolveContext& C, const FSuperOverMatch&) { Case.Set(C, 0.95f); });
		const float RLo = float(Lo.Runs) / Lo.Innings, RHi = float(Hi.Runs) / Hi.Innings;
		const float WLo = float(Lo.Wickets) / Lo.Innings, WHi = float(Hi.Wickets) / Hi.Innings;
		UE_LOG(LogTemp, Display, TEXT("Attribute %s weak/strong: %.1f/%.1f runs, %.2f/%.2f wickets, %d/%d run outs per %d innings"),
			Case.Name, RLo, RHi, WLo, WHi, Lo.RunOuts, Hi.RunOuts, Lo.Innings);
		// Batting attributes are worth runs per wicket to the batting side; the rest take them away.
		// Movement is the exception: slogging Super Over batters are beaten by extra swing and turn more
		// than they are out to it (about 4% over 400 to 1200 games), so it only has to help.
		const float Good = (RHi + 1.f) / (WHi + 0.1f) / ((RLo + 1.f) / (WLo + 0.1f));
		const float Bar = FString(Case.Name).Contains(TEXT("Movement")) ? 0.02f : 0.05f;
		TestTrue(*FString::Printf(TEXT("%s: strong changes runs per wicket by %.0f%%"), Case.Name, 100.f * (Good - 1.f)),
			Case.bBatting ? Good > 1.f + Bar : Good < 1.f - Bar);
		TestTrue(*FString::Printf(TEXT("%s: strong side still plays cricket (%.1f runs per innings)"), Case.Name, RHi), RHi > 5.f && RHi < 25.f);
	}

	// Running between the wickets is a small share of Super Over runs, so the running attributes are
	// measured on the running sweep's balls (normal margin) where they are the only thing that differs.
	auto Sweep = [](float BatterSpeed, float Throwing, int32& Runs, int32& Attempts, int32& RunOuts)
	{
		Runs = Attempts = RunOuts = 0;
		FCricketPlayer Batter, Fielding;
		Batter.RunSpeed = BatterSpeed;
		Fielding.Throwing = Throwing;
		FRandomStream Pick(11);
		for (int32 I = 0; I < 3000; ++I)
		{
			FFieldingOutcome Fd;
			Fd.Fielder = 3;
			const float Dist = Pick.FRandRange(8.f, 55.f), Ang = Pick.FRandRange(-PI, PI);
			Fd.FieldPos = FVector(Dist * FMath::Cos(Ang), Dist * FMath::Sin(Ang), 0.1f);
			Fd.FieldTime = Pick.FRandRange(0.6f, 6.f);
			Fd.bDive = Pick.GetFraction() < 0.2f;
			FRandomStream Rng(I);
			const FRunningOutcome R = CricketField::SolveRunning(Fd, Batter, Batter, Fielding, false, 0.25f, Rng);
			Runs += R.Completed;
			Attempts += R.Attempted;
			RunOuts += R.bRunOut;
		}
	};
	int32 Runs[4], Attempts[4], Outs[4];
	Sweep(6.1f, 0.6f, Runs[0], Attempts[0], Outs[0]);
	Sweep(8.35f, 0.6f, Runs[1], Attempts[1], Outs[1]);
	Sweep(7.f, 0.2f, Runs[2], Attempts[2], Outs[2]);
	Sweep(7.f, 0.95f, Runs[3], Attempts[3], Outs[3]);
	UE_LOG(LogTemp, Display, TEXT("Attribute running (3000 balls): batters slow/fast %d/%d runs, %d/%d attempted, %d/%d run outs; throwing weak/strong %d/%d runs, %d/%d attempted, %d/%d run outs"),
		Runs[0], Runs[1], Attempts[0], Attempts[1], Outs[0], Outs[1], Runs[2], Runs[3], Attempts[2], Attempts[3], Outs[2], Outs[3]);
	TestTrue(TEXT("fast batters score more runs"), Runs[1] > Runs[0] * 1.1f);
	TestTrue(TEXT("fast batters are not run out more often per run attempted"), Outs[1] * Attempts[0] <= Outs[0] * Attempts[1] * 1.2f + Attempts[1] * 0.002f);
	TestTrue(TEXT("a strong arm stops runs"), Runs[3] < Runs[2] * 0.95f);
	return true;
}

// ---------------------------------------------------------------- Acceptance scenarios

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOAcceptance, "CRICKET26.Acceptance.ScenariosAToL", CricketTestFlags)
bool FSOAcceptance::RunTest(const FString&)
{
	// The slice's twelve acceptance scenarios, each a real delivery through the full resolver (ball flight,
	// the stroke, umpiring, fielding and running) scored by the match rules. Where a scenario needs a
	// particular field placing or call, the test searches deliveries for one that produces it, so a failure
	// means the simulation can no longer produce that piece of cricket at all.
	auto Legal = [this](const TCHAR* Name, const FDeliveryResult& R)
	{
		FSuperOverMatch M;
		M.Start(0);
		TestTrue(*FString::Printf(TEXT("%s: the match accepts it (%s)"), Name, *R.Summary), Bowl(M, R.ToOutcome()));
		UE_LOG(LogTemp, Display, TEXT("Scenario %s: %s"), Name, *R.Summary);
	};
	auto Angle = [](const FDeliveryResult& R) { return FMath::RadiansToDegrees(FMath::Atan2(R.Contact.ExitVel.Y, R.Contact.ExitVel.X)); };
	auto Rise = [](const FDeliveryResult& R) { return FMath::RadiansToDegrees(FMath::Atan2(R.Contact.ExitVel.Z, FVector2D(R.Contact.ExitVel).Size())); };

	// A: cover drive. Full outside off, driven along the ground into the covers.
	const FDeliveryRelease DriveBall = Release(EDeliveryType::Stock, 3.5f, 0.25f);
	const FDeliveryResult A = CricketDelivery::Resolve(DriveBall, Perfect(DriveBall, EBatIntent::Ground, 60.f), Ctx());
	TestTrue(*FString::Printf(TEXT("A: middled into the covers (%.0f deg, %.0f up, quality %.2f)"), Angle(A), Rise(A), A.Contact.Quality),
		A.Contact.HasContact() && A.Dismissal == EDismissal::None && Angle(A) > 35.f && Angle(A) < 90.f && Rise(A) < 15.f && A.Contact.Quality > 0.7f);
	Legal(TEXT("A"), A);

	// B: yorker dug out. On the stumps at the toes, blocked in time: bat on ball, not out, no power.
	const FDeliveryRelease Yorker = Release(EDeliveryType::Stock, 1.f, 0.f);
	const FDeliveryResult B = CricketDelivery::Resolve(Yorker, Perfect(Yorker, EBatIntent::Defend, 0.f), Ctx());
	TestTrue(*FString::Printf(TEXT("B: dug out (%s, %.1f m/s)"), *B.Summary, B.Contact.ExitVel.Size()),
		B.Contact.HasContact() && B.Dismissal == EDismissal::None && B.Contact.ExitVel.Size() < 25.f);
	Legal(TEXT("B"), B);

	// C: pull. Short, pulled in front of square on the leg side.
	const FDeliveryRelease Short = Release(EDeliveryType::Stock, 8.f, 0.f);
	const FDeliveryResult C = CricketDelivery::Resolve(Short, Perfect(Short, EBatIntent::Ground, -80.f), Ctx());
	TestTrue(*FString::Printf(TEXT("C: pulled to the leg side (%s, %.0f deg)"), *C.Summary, Angle(C)), // in the air, so it may be caught
		C.Shot.Shot == EShotType::Pull && C.Contact.HasContact() && Angle(C) < -35.f && Angle(C) > -135.f);
	Legal(TEXT("C"), C);

	// D: a late outswinger finds the outside edge that the same stock ball would not.
	{
		FResolveContext Tail = Ctx();
		Tail.Striker.Technique = 0.f;
		bool bFound = false;
		for (float Line = 0.05f; Line <= 0.3f && !bFound; Line += 0.05f)
		{
			FCricketPlayer Swinger;
			Swinger.Accuracy = 1.f;
			Swinger.Movement = 1.f;
			FDeliveryPlan Plan;
			Plan.Length = 4.5f;
			Plan.Line = Line;
			Plan.Type = EDeliveryType::Stock;
			const FDeliveryRelease Stock = CricketBowling::Execute(Swinger, ECricketHand::Right, Plan, 0.f, 3, Tail.Conditions);
			Plan.Type = EDeliveryType::Outswing;
			const FDeliveryRelease Away = CricketBowling::Execute(Swinger, ECricketHand::Right, Plan, 0.f, 3, Tail.Conditions);
			const FDeliveryResult S = CricketDelivery::Resolve(Stock, Perfect(Stock, EBatIntent::Defend, 0.f), Tail);
			const FDeliveryResult D = CricketDelivery::Resolve(Away, Perfect(Away, EBatIntent::Defend, 0.f), Tail);
			if (S.Contact.Zone == EContactZone::OutsideEdge || D.Contact.Zone != EContactZone::OutsideEdge) continue;
			bFound = true;
			Legal(TEXT("D"), D);
		}
		TestTrue(TEXT("D: some line finds the edge with late swing only"), bFound);
	}

	// E: a leg break beats the bat and the keeper, standing up, gathers it.
	{
		FResolveContext Spin = Ctx(EBowlerType::LegSpin);
		Spin.Striker.Technique = 0.f;
		bool bFound = false;
		for (float Line = 0.1f; Line <= 0.6f && !bFound; Line += 0.05f)
		{
			const FDeliveryRelease R = Release(EDeliveryType::LegBreak, 4.5f, Line, 0.f, EBowlerType::LegSpin);
			const FDeliveryResult E = CricketDelivery::Resolve(R, Perfect(R, EBatIntent::Defend, 0.f, EBowlerType::LegSpin), Spin);
			if (E.Contact.HasContact() || E.bWide || E.Dismissal != EDismissal::None) continue;
			bFound = true;
			TestTrue(*FString::Printf(TEXT("E: the keeper takes it (%s)"), *E.Summary), E.Fielding.Fielder >= 0 && Spin.Field[E.Fielding.Fielder].bKeeper);
			Legal(TEXT("E"), E);
		}
		TestTrue(TEXT("E: some line beats the bat"), bFound);
	}

	// F: stumped. The batter charges a wide leg break, is beaten, and the keeper breaks the wicket.
	{
		bool bFound = false;
		const FDeliveryRelease R = Release(EDeliveryType::LegBreak, 4.5f, 1.3f, 0.f, EBowlerType::LegSpin);
		FBatInput Charge;
		Charge.Intent = EBatIntent::Loft;
		Charge.PressTime = CricketDelivery::Read(R.Ball, 0.f, FPitchConditions()).ArrivalTime - 0.62f;
		for (int32 Seed = 1; Seed <= 5 && !bFound; ++Seed) // the keeper fumbles about one take in ten
		{
			const FDeliveryResult F = CricketDelivery::Resolve(R, Charge, Ctx(EBowlerType::LegSpin, Seed));
			if (F.Dismissal != EDismissal::Stumped) continue;
			bFound = true;
			Legal(TEXT("F"), F);
		}
		TestTrue(TEXT("F: stumped"), bFound);
	}

	// G, H: pushed and punched strokes round the ground, under normal and aggressive running. G wants a
	// quick single (completed with the throw close behind); H a second run that is lost by a whisker.
	TOptional<FDeliveryResult> G, H;
	for (float Dir = -150.f; Dir <= 150.f && !(G && H); Dir += 10.f)
	{
		for (const float Length : { 3.5f, 5.f, 7.f })
		{
			const FDeliveryRelease R = Release(EDeliveryType::Stock, Length, 0.1f);
			for (const EBatIntent Intent : { EBatIntent::Defend, EBatIntent::Ground })
			{
				for (int32 Seed = 1; Seed <= 6; ++Seed)
				{
					FResolveContext Run = Ctx(EBowlerType::Pace, Seed);
					Run.RunMargin = Seed % 2 ? 0.25f : -0.1f; // normal and aggressive running
					const FDeliveryResult X = CricketDelivery::Resolve(R, Perfect(R, Intent, Dir), Run);
					const FRunningOutcome& Rn = X.Running;
					if (X.Fielding.Boundary || X.Fielding.bCaught || !X.Contact.HasContact()) continue;
					if (!G && Rn.Completed == 1 && Rn.Attempted == 1 && !Rn.bRunOut && !Rn.bSentBack && Rn.RunTimes.Num() == 1
						&& Rn.BreakTime - Rn.RunTimes[0] < 0.5f && X.Fielding.FieldPos.Size() < 30.f) G = X;
					if (!H && Rn.Attempted == 2 && Rn.Completed == 1 && Rn.bRunOut && Rn.RunTimes.Num() == 2
						&& Rn.RunTimes[1] - Rn.BreakTime < 0.3f) H = X;
				}
			}
		}
	}
	if (TestTrue(TEXT("G: a quick single"), G.IsSet()))
	{
		TestEqual(TEXT("G: one run, nobody out"), G->ToOutcome().RunsRun, 1);
		Legal(TEXT("G"), *G);
	}
	if (TestTrue(TEXT("H: run out by a whisker going for two"), H.IsSet()))
	{
		TestEqual(TEXT("H: run out"), H->Dismissal, EDismissal::RunOut);
		TestEqual(TEXT("H: the first run counts"), H->ToOutcome().RunsRun, 1);
		Legal(TEXT("H"), *H);
	}

	// I: a mistimed loft goes up and gives a catch chance.
	{
		const FDeliveryRelease R = Release(EDeliveryType::Stock, 4.f, 0.f);
		TOptional<FDeliveryResult> I;
		for (float Off = 0.02f; Off <= 0.1f && !I; Off += 0.01f)
		{
			for (const float Sign : { 1.f, -1.f })
			{
				for (const float Dir : { 0.f, -40.f, 40.f })
				{
					FBatInput In = Perfect(R, EBatIntent::Loft, Dir);
					In.PressTime += Sign * Off;
					const FDeliveryResult X = CricketDelivery::Resolve(R, In, Ctx());
					if (!I && X.Fielding.bCatchChance && X.Contact.Quality < 0.8f) I = X;
				}
			}
		}
		if (TestTrue(TEXT("I: a mistimed loft offers a catch"), I.IsSet()))
		{
			TestTrue(*FString::Printf(TEXT("I: caught or put down (%s)"), *I->Summary), I->Dismissal == EDismissal::Caught || I->Dismissal == EDismissal::None);
			Legal(TEXT("I"), *I);
		}
	}

	// J: six. A half-volley lofted straight, perfectly timed, clears the rope.
	TOptional<FDeliveryResult> J;
	int32 Middled = 0, Sixes = 0;
	for (float Length = 3.f; Length <= 5.f; Length += 0.5f) // half-volley to good length
	{
		const FDeliveryRelease Slot = Release(EDeliveryType::Stock, Length, 0.f);
		for (int32 Seed = 1; Seed <= 4; ++Seed) // bat placement carries the batter's execution error, so not all are middled
		{
			const FDeliveryResult X = CricketDelivery::Resolve(Slot, Perfect(Slot, EBatIntent::Loft, 0.f), Ctx(EBowlerType::Pace, Seed));
			if (X.Contact.Zone != EContactZone::Middle) continue;
			++Middled;
			Sixes += X.Fielding.Boundary == 6;
			if (!J && X.Fielding.Boundary == 6) J = X;
		}
	}
	TestTrue(*FString::Printf(TEXT("J: every middled straight loft clears the rope (%d of %d)"), Sixes, Middled), Middled >= 5 && Sixes == Middled);
	if (J) Legal(TEXT("J"), *J);

	// K, L: last ball, two to win. The six wins it; the quick single ties it and a second Super
	// Over starts with the side that batted second going in first.
	auto LastBall = [this](const FDeliveryResult& R, TArray<ECricketEvent>& Ev)
	{
		FSuperOverMatch M;
		M.Start(0);
		for (const FDeliveryOutcome& O : { Four(), Four(), Runs(0), Runs(0), Runs(0), Runs(0) }) Bowl(M, O);
		M.StartSecondInnings(); // target 9
		for (const FDeliveryOutcome& O : { Four(), Runs(2), Runs(1), Runs(0), Runs(0) }) Bowl(M, O);
		TestTrue(TEXT("K: two needed off the last ball"), M.Target - M.Cur().Runs == 2 && M.Cur().LegalBalls == 5);
		Bowl(M, R.ToOutcome(), &Ev);
		return M;
	};
	TArray<ECricketEvent> Ev;
	if (J)
	{
		const FSuperOverMatch Won = LastBall(*J, Ev);
		TestTrue(TEXT("K: the six wins it"), Won.Phase == EMatchPhase::MatchComplete && Won.Winner == 1 && Ev.Contains(ECricketEvent::MatchWon));
	}
	if (G)
	{
		Ev.Reset();
		FSuperOverMatch Tied = LastBall(*G, Ev);
		TestTrue(TEXT("L: the single ties it"), Tied.Phase == EMatchPhase::MatchComplete && Tied.bTied && Ev.Contains(ECricketEvent::MatchTied));
		TestTrue(TEXT("L: another Super Over"), Tied.StartNextSuperOver() && Tied.SuperOverNumber == 2 && Tied.BattingTeam() == 1);
		FString Err;
		TestTrue(TEXT("L: invariants hold"), Tied.CheckInvariants(Err));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOSquads, "CRICKET26.AI.DefaultSquadsScoreLikeASuperOver", CricketTestFlags)
bool FSOSquads::RunTest(const FString&)
{
	// The squads the game ships with, each side's bowler to the other's batters in order, over whole AI
	// Super Overs: the default match has to score like the real thing, not like a net session.
	const TArray<FCricketTeam> Teams = ASuperOverGameMode::DefaultSquads();
	const FDifficultyStats S = PlaySuperOvers(150, CricketAI::DefaultSkill, CricketAI::DefaultSkill, [&](FResolveContext& C, const FSuperOverMatch& M)
	{
		const FCricketTeam& Bat = Teams[M.BattingTeam()];
		C.Striker = Bat.Batters[M.Cur().Striker];
		C.NonStriker = Bat.Batters[M.Cur().NonStriker];
		C.Bowler = Teams[M.BowlingTeam()].Bowler;
	});
	const float PerInnings = float(S.Runs) / FMath::Max(1, S.Innings), SixShare = float(S.Sixes) / FMath::Max(1, S.Balls);
	UE_LOG(LogTemp, Display, TEXT("Default squads: %.1f runs and %.2f wickets per innings, %.0f%% of balls hit for six, %d run outs over %d innings"),
		PerInnings, float(S.Wickets) / FMath::Max(1, S.Innings), 100.f * SixShare, S.RunOuts, S.Innings);
	// The two 2019 World Cup final Super Overs made 15 each. The squads first shipped as five hitters against two
	// average bowlers and made 17.9 with 40% of balls hit for six; the bowlers are now death specialists. The six
	// share is still high because the model's lofted shots are (see the reference doc), so this band only stops
	// the default match drifting further from real cricket.
	TestTrue(*FString::Printf(TEXT("runs per innings like a real Super Over (%.1f)"), PerInnings), PerInnings > 10.f && PerInnings < 17.f);
	TestTrue(*FString::Printf(TEXT("six share no higher than now (%.0f%%)"), 100.f * SixShare), SixShare < 0.36f);
	return true;
}

#endif
