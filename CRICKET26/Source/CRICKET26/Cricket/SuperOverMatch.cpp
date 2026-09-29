#include "SuperOverMatch.h"

void FSuperOverMatch::Start(int32 FirstBattingTeam)
{
	SuperOverNumber++;
	Innings.Reset();
	CurrentInnings = 0;
	Target = 0;
	Winner = -1;
	bTied = false;
	bFreeHit = false;
	ReviewsLeft[0] = ReviewsLeft[1] = Rules.ReviewsPerTeam;
	BeginInnings(FirstBattingTeam);
}

void FSuperOverMatch::BeginInnings(int32 BattingTeam)
{
	FInningsState& In = Innings.AddDefaulted_GetRef();
	In.BattingTeam = BattingTeam;
	In.Batters.SetNum(Rules.MaxWickets + 1);
	CurrentInnings = Innings.Num() - 1;
	bFreeHit = false;
	Phase = EMatchPhase::ReadyForDelivery;
}

bool FSuperOverMatch::BeginDelivery()
{
	if (Phase != EMatchPhase::ReadyForDelivery) return false;
	Phase = EMatchPhase::DeliveryInProgress;
	return true;
}

bool FSuperOverMatch::CompleteDelivery(const FDeliveryOutcome& O, TArray<ECricketEvent>& OutEvents)
{
	if (Phase != EMatchPhase::DeliveryInProgress) return false;
	if (O.RunsRun < 0 || O.RunsRun > 7) return false;
	if (O.Boundary != 0 && O.Boundary != 4 && O.Boundary != 6) return false;
	// Runs completed plus a boundary only happens when an overthrow reaches the rope.
	if (O.bOverthrow != (O.Boundary == 4 && O.RunsRun > 0)) return false;
	if (O.Boundary != 0 && O.Dismissal != EDismissal::None) return false;
	if (O.bWide && (O.bNoBall || O.bBatContact || O.bLegBye)) return false;
	if (O.bLegBye && O.bBatContact) return false;
	if (O.Boundary == 6 && (!O.bBatContact || O.bOverthrow)) return false;
	// The umpire calls a bouncer over the limit; an uncalled one means the simulation broke the rules.
	if (O.bBouncer && !BouncerAllowed() && !O.bNoBall && !O.bWide) return false;

	const EDismissal D = O.Dismissal;
	// Only a run out survives a no-ball or a free hit (Law 21.18). Off a wide the striker can still be
	// stumped, hit wicket or run out, but not bowled, caught or LBW.
	if ((O.bNoBall || bFreeHit) && D != EDismissal::None && D != EDismissal::RunOut) return false;
	if (O.bWide && (D == EDismissal::Bowled || D == EDismissal::Caught || D == EDismissal::LBW)) return false;
	if (D == EDismissal::Caught && !O.bBatContact) return false;
	if (D == EDismissal::LBW && O.bBatContact) return false;
	// These end the ball at once: no runs can be completed alongside them.
	if ((D == EDismissal::Bowled || D == EDismissal::LBW || D == EDismissal::Stumped || D == EDismissal::HitWicket) && O.RunsRun != 0) return false;

	FInningsState& In = Mut();
	const bool bLegal = !O.bWide && !O.bNoBall;
	// A caught batter's runs never count.
	const int32 Ran = D == EDismissal::Caught ? 0 : O.RunsRun;
	const int32 OffBat = O.bBatContact ? Ran + O.Boundary : 0;
	const int32 Byes = (O.bBatContact || O.bWide) ? 0 : Ran + O.Boundary; // byes or leg byes
	const int32 Wides = O.bWide ? 1 + Ran + O.Boundary : 0;
	const int32 NoBall = O.bNoBall ? 1 : 0;
	const int32 Total = OffBat + Byes + Wides + NoBall;

	In.Deliveries++;
	In.Runs += Total;
	In.PartnershipRuns += Total;
	In.PartnershipBalls += bLegal ? 1 : 0;
	In.Extras += Byes + Wides + NoBall;
	(O.bLegBye ? In.LegByes : In.Byes) += Byes;
	In.Bowler.Runs += OffBat + Wides + NoBall; // byes and leg byes are not the bowler's fault
	FBatterCard& S = In.Batters[In.Striker];
	S.Runs += OffBat;
	if (!O.bWide) S.Balls++;
	if (bLegal) { In.LegalBalls++; In.Bowler.Balls++; }
	if (O.bWide) In.Bowler.Wides++;
	if (O.bNoBall) In.Bowler.NoBalls++;
	if (O.bBouncer) In.Bouncers++;
	if (O.bBatContact && O.Boundary == 4 && !O.bOverthrow) S.Fours++;
	if (O.Boundary == 6) S.Sixes++;

	FString Log = Total == 0 ? TEXT(".") : FString::FromInt(Total);
	if (O.bWide) Log = FString::Printf(TEXT("%dwd"), Total);
	else if (O.bNoBall) Log = FString::Printf(TEXT("%dnb"), Total);
	else if (Byes > 0) Log = FString::FromInt(Total) + (O.bLegBye ? TEXT("lb") : TEXT("b"));
	if (D != EDismissal::None) Log = Total == 0 ? TEXT("W") : Log + TEXT("W");
	In.BallLog.Add(Log);

	OutEvents.Add(ECricketEvent::DeliveryCompleted);
	if (O.bWide) OutEvents.Add(ECricketEvent::Wide);
	if (O.bNoBall) OutEvents.Add(ECricketEvent::NoBall);
	if (O.Boundary == 4 && !O.bOverthrow) OutEvents.Add(ECricketEvent::BoundaryFour);
	if (O.Boundary == 6) OutEvents.Add(ECricketEvent::BoundarySix);
	if (Total == 0 && D == EDismissal::None) OutEvents.Add(ECricketEvent::DotBall);
	if (Total > 0 && O.Boundary == 0) OutEvents.Add(ECricketEvent::RunsScored);

	// Strike: completed runs rotate it; boundaries and penalty runs don't.
	if (Ran % 2 == 1) Swap(In.Striker, In.NonStriker);

	if (D != EDismissal::None)
	{
		In.Wickets++;
		In.PartnershipRuns = In.PartnershipBalls = 0;
		OutEvents.Add(ECricketEvent::Wicket);
		if (D != EDismissal::RunOut) In.Bowler.Wickets++;

		int32 OutSlot;          // batting-order index of the dismissed batter
		bool bNewAtStrikerEnd;  // which end the incoming batter takes
		if (D == EDismissal::RunOut)
		{
			// The out batter was short of the end they were running to on the attempted extra run.
			// Positions above already reflect completed runs, so the original striker is heading for
			// the striker's end exactly when an odd number of runs was completed.
			const bool bStrikerHeadingHome = (Ran % 2 == 1);
			const bool bOutHeadingHome = O.bRunOutStriker ? bStrikerHeadingHome : !bStrikerHeadingHome;
			// Striker/NonStriker already swapped for odd runs, so map the original striker's index.
			const int32 OrigStriker = (Ran % 2 == 1) ? In.NonStriker : In.Striker;
			const int32 OrigNonStriker = (Ran % 2 == 1) ? In.Striker : In.NonStriker;
			OutSlot = O.bRunOutStriker ? OrigStriker : OrigNonStriker;
			const int32 Survivor = O.bRunOutStriker ? OrigNonStriker : OrigStriker;
			bNewAtStrikerEnd = bOutHeadingHome;
			In.Striker = bNewAtStrikerEnd ? -1 : Survivor;
			In.NonStriker = bNewAtStrikerEnd ? Survivor : -1;
		}
		else
		{
			// Bowled / LBW / stumped / hit wicket / caught: striker out, new batter on strike (Law 18.11).
			OutSlot = In.Striker;
			bNewAtStrikerEnd = true;
			In.Striker = -1;
		}
		In.Batters[OutSlot].HowOut = D;

		if (In.Wickets < Rules.MaxWickets)
		{
			(bNewAtStrikerEnd ? In.Striker : In.NonStriker) = In.NextBatter;
			In.NextBatter++;
		}
		else
		{
			// Innings over: keep indices valid for display.
			if (In.Striker == -1) In.Striker = OutSlot;
			if (In.NonStriker == -1) In.NonStriker = OutSlot;
		}
	}

	const bool bWasFreeHitCarried = bFreeHit && !bLegal;
	bFreeHit = (O.bNoBall && Rules.bFreeHitAfterNoBall) || bWasFreeHitCarried;
	if (bFreeHit) OutEvents.Add(ECricketEvent::FreeHitNext);

	const bool bChaseDone = IsChase() && In.Runs >= Target;
	if (bChaseDone || In.Wickets >= Rules.MaxWickets || In.LegalBalls >= Rules.MaxLegalBalls)
	{
		FinishInnings(OutEvents);
	}
	else
	{
		if (bLegal && In.LegalBalls % 6 == 0)
		{
			Swap(In.Striker, In.NonStriker);
			In.Bouncers = 0;
			In.OverLogStart = In.BallLog.Num();
		}
		Phase = EMatchPhase::ReadyForDelivery;
		if (IsChase())
		{
			OutEvents.Add(ECricketEvent::RequiredRunsChanged);
			if (BallsRemaining() == 1) OutEvents.Add(ECricketEvent::LastBallSituation);
		}
	}
	return true;
}

void FSuperOverMatch::FinishInnings(TArray<ECricketEvent>& OutEvents)
{
	FInningsState& In = Mut();
	In.bComplete = true;
	bFreeHit = false;
	OutEvents.Add(ECricketEvent::InningsCompleted);
	if (!IsChase())
	{
		Target = In.Runs + 1;
		OutEvents.Add(ECricketEvent::TargetSet);
		Phase = EMatchPhase::InningsBreak;
		return;
	}
	Phase = EMatchPhase::MatchComplete;
	if (In.Runs >= Target) Winner = In.BattingTeam;
	else if (In.Runs == Target - 1) bTied = true;
	else Winner = 1 - In.BattingTeam;
	OutEvents.Add(bTied ? ECricketEvent::MatchTied : ECricketEvent::MatchWon);
}

bool FSuperOverMatch::StartSecondInnings()
{
	if (Phase != EMatchPhase::InningsBreak) return false;
	BeginInnings(1 - Innings[0].BattingTeam);
	return true;
}

bool FSuperOverMatch::StartNextSuperOver()
{
	if (Phase != EMatchPhase::MatchComplete || !bTied) return false;
	Rules.MaxLegalBalls = 6;
	Rules.MaxWickets = 2;
	Start(Innings[1].BattingTeam);
	return true;
}

bool FSuperOverMatch::SetBowlerSlot(int32 Slot)
{
	if (Slot < 0) return false;
	if (Phase != EMatchPhase::ReadyForDelivery && Phase != EMatchPhase::InningsBreak) return false;
	if (!Innings.IsValidIndex(CurrentInnings)) return false;
	FInningsState& In = Mut();
	if (Slot == In.BowlerSlot) return true;
	if (In.BowlerSlot != INDEX_NONE)
	{
		if (!In.BowlerCards.IsValidIndex(In.BowlerSlot)) In.BowlerCards.SetNumZeroed(In.BowlerSlot + 1);
		In.BowlerCards[In.BowlerSlot] = In.Bowler;
	}
	if (!In.BowlerCards.IsValidIndex(Slot)) In.BowlerCards.SetNumZeroed(Slot + 1);
	In.Bowler = In.BowlerCards[Slot];
	In.BowlerSlot = Slot;
	return true;
}

FString FSuperOverMatch::PressureText() const
{
	if (!IsChase() || Innings.Num() < 2) return FString();
	const int32 Need = RunsRequired();
	const int32 Balls = BallsRemaining();
	if (Phase == EMatchPhase::MatchComplete) return FString();
	if (Balls == 1) return FString::Printf(TEXT("%d TO WIN OFF THE LAST BALL"), Need);
	return FString::Printf(TEXT("%d REQUIRED FROM %d"), Need, Balls);
}

bool FSuperOverMatch::CheckInvariants(FString& OutError) const
{
	auto Fail = [&OutError](const TCHAR* Why) { OutError = Why; return false; };
	if (Phase == EMatchPhase::PreMatch) return Innings.Num() == 0 ? true : Fail(TEXT("innings before start"));
	if (!Innings.IsValidIndex(CurrentInnings) || Innings.Num() > 2) return Fail(TEXT("bad innings index"));
	const FInningsState& In = Cur();
	if (In.LegalBalls > Rules.MaxLegalBalls) return Fail(TEXT("too many legal balls"));
	if (In.Wickets > Rules.MaxWickets) return Fail(TEXT("too many wickets"));
	if (In.LegalBalls > In.Deliveries) return Fail(TEXT("legal balls exceed deliveries"));
	if (In.Striker == In.NonStriker && !In.bComplete) return Fail(TEXT("striker is non-striker"));
	if (!In.Batters.IsValidIndex(In.Striker) || !In.Batters.IsValidIndex(In.NonStriker)) return Fail(TEXT("bad batter index"));
	if (!In.bComplete && (In.Batters[In.Striker].HowOut != EDismissal::None || In.Batters[In.NonStriker].HowOut != EDismissal::None))
		return Fail(TEXT("dismissed batter at the crease"));
	const bool bShouldBeOver = In.LegalBalls >= Rules.MaxLegalBalls || In.Wickets >= Rules.MaxWickets || (IsChase() && In.Runs >= Target);
	if (bShouldBeOver != In.bComplete) return Fail(TEXT("innings completion mismatch"));
	if (In.bComplete && Phase != EMatchPhase::InningsBreak && Phase != EMatchPhase::MatchComplete) return Fail(TEXT("complete innings still live"));
	if (IsChase() && Target != Innings[0].Runs + 1) return Fail(TEXT("target mismatch"));
	if (Phase == EMatchPhase::MatchComplete && !bTied && Winner < 0) return Fail(TEXT("complete without result"));
	return true;
}
