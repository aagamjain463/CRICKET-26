// Gate 1 automation tests: rules, strike, scoring, state, ball physics, batting contact, fielding.
// Run: UnrealEditor-Cmd CRICKET26.uproject -ExecCmds="Automation RunTests CRICKET26.; Quit" -unattended -nullrhi

#include "Misc/AutomationTest.h"
#include "CricketAI.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesSixLegalBalls, "CRICKET26.Rules.SixLegalBallsEndInnings", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesExtras, "CRICKET26.Rules.WidesAndNoBallsAreNotLegal", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesFreeHitCarries, "CRICKET26.Rules.FreeHitCarriesOverWide", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesWickets, "CRICKET26.Rules.TwoWicketsEndInnings", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesStrike, "CRICKET26.Rules.StrikeRotation", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesChase, "CRICKET26.Rules.ChaseEndsWhenTargetReached", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesDefend, "CRICKET26.Rules.DefendingSideWins", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesTie, "CRICKET26.Rules.TieGoesToAnotherSuperOver", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSORulesIllegal, "CRICKET26.Rules.IllegalInputRejected", Flags)
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

// ---------------------------------------------------------------- Ball physics

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallSolver, "CRICKET26.Ball.PitchesWhereAimed", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallSwing, "CRICKET26.Ball.SwingAndTurnDirections", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallLengths, "CRICKET26.Ball.BouncerRisesYorkerDoesNot", Flags)
bool FSOBallLengths::RunTest(const FString&)
{
	const FBallState Bouncer = AtCrease(Release(EDeliveryType::Stock, 11.5f));
	const FBallState Yorker = AtCrease(Release(EDeliveryType::Stock, 1.1f));
	TestTrue(*FString::Printf(TEXT("bouncer high at crease (%.2f m)"), Bouncer.Pos.Z), Bouncer.Pos.Z > 1.2f);
	TestTrue(*FString::Printf(TEXT("yorker low at crease (%.2f m)"), Yorker.Pos.Z), Yorker.Pos.Z < 0.3f);
	const FDeliveryRelease Slow = Release(EDeliveryType::Slower, 5.f), Stock = Release(EDeliveryType::Stock, 5.f);
	TestTrue(TEXT("slower ball slower"), Slow.SpeedKph < Stock.SpeedKph * 0.85f);
	TestTrue(TEXT("slower ball takes longer"), AtCrease(Slow).Time > AtCrease(Stock).Time + 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallReleaseErrors, "CRICKET26.Ball.ReleaseErrorsStayPhysical", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBallDeterminism, "CRICKET26.Ball.DeterministicAndNoBall", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBatMiddle, "CRICKET26.Batting.PerfectDriveMiddlesAndGoesStraight", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBatTiming, "CRICKET26.Batting.LateGoesFinerAndVeryLateMisses", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBatEdge, "CRICKET26.Batting.LateMovementFindsTheEdge", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOBatWicket, "CRICKET26.Batting.BowledAndLBW", Flags)
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

// ---------------------------------------------------------------- Fielding

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOFieldKeeper, "CRICKET26.Fielding.KeeperTakesBeatenBall", Flags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOFieldBoundaries, "CRICKET26.Fielding.CatchFourSixRunning", Flags)
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
	FRandomStream Rng2(1);
	const FRunningOutcome Mad = CricketField::SolveRunning(Push, FCricketPlayer(), FCricketPlayer(), FCricketPlayer(), false, -3.f, Rng2);
	TestTrue(TEXT("reckless running gets run out"), Mad.bRunOut && Mad.Attempted > Run.Attempted);
	return true;
}

// ---------------------------------------------------------------- Full match with AI on both sides

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOAIMatch, "CRICKET26.Match.AIvsAICompletesLegally", Flags)
bool FSOAIMatch::RunTest(const FString&)
{
	int32 Fours = 0, Sixes = 0, Wickets = 0, Wides = 0, Edges = 0, Deliveries = 0, Runs1 = 0, Caught = 0, RunOuts = 0, Dots = 0, ByeBoundaries = 0;
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
	TestTrue(TEXT("some boundaries"), Fours + Sixes > 0);
	TestTrue(TEXT("some wickets"), Wickets > 0);
	TestTrue(*FString::Printf(TEXT("keeper stops balls that beat the bat (%d byes to the boundary)"), ByeBoundaries), ByeBoundaries <= Deliveries / 100);
	return true;
}

#endif
