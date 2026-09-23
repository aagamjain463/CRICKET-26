#include "CricketCommentary.h"

namespace CricketCommentary
{
namespace
{
	const TCHAR* Pick(std::initializer_list<const TCHAR*> Lines, int32 Variant)
	{
		return Lines.begin()[((Variant % int32(Lines.size())) + int32(Lines.size())) % int32(Lines.size())];
	}

	FString Fill(const TCHAR* Line, const FNames& N, const FString& Where)
	{
		return FString(Line).Replace(TEXT("{S}"), *N.Striker).Replace(TEXT("{NS}"), *N.NonStriker).Replace(TEXT("{R}"), *Where);
	}

	const TCHAR* Verb(EShotType Shot)
	{
		switch (Shot)
		{
		case EShotType::Drive: return TEXT("driven");
		case EShotType::Loft: return TEXT("lofted");
		case EShotType::Punch: return TEXT("punched");
		case EShotType::Cut: return TEXT("cut");
		case EShotType::Pull: return TEXT("pulled");
		case EShotType::Sweep: return TEXT("swept");
		case EShotType::Flick: return TEXT("flicked");
		case EShotType::Hook: return TEXT("hooked");
		case EShotType::SlogSweep: return TEXT("slog-swept");
		case EShotType::ReverseSweep: return TEXT("reverse-swept");
		case EShotType::Scoop: return TEXT("scooped");
		default: return TEXT("pushed");
		}
	}

	bool IsEdge(EContactZone Z)
	{
		return Z == EContactZone::InsideEdge || Z == EContactZone::OutsideEdge || Z == EContactZone::TopEdge || Z == EContactZone::BottomEdge;
	}

	FString Happened(const FDeliveryResult& R, const FDeliveryOutcome& O, const FNames& N, const FString& Where, int32 V)
	{
		const bool bBat = R.Contact.HasContact();
		switch (O.Dismissal)
		{
		case EDismissal::Bowled:
			return Fill(bBat ? TEXT("Played on! {S} drags it back onto the stumps.")
				: Pick({ TEXT("Bowled him! {S} misses and the stumps are sent flying."), TEXT("Clean bowled. Through the gate, and {S} has to go.") }, V), N, Where);
		case EDismissal::Caught:
			return Fill(IsEdge(R.Contact.Zone) ? TEXT("Off the edge... and taken! {S} is caught.")
				: Pick({ TEXT("{S} goes for it towards {R}... and it's caught!"), TEXT("Up in the air towards {R}, and taken. {S} is gone.") }, V), N, Where);
		case EDismissal::LBW:
			return Fill(Pick({ TEXT("Struck on the pad, and the finger goes up. {S} is LBW."), TEXT("Plumb in front! {S} has to go, leg before.") }, V), N, Where);
		case EDismissal::RunOut:
		{
			FNames Out = N;
			Out.Striker = O.bRunOutStriker ? N.Striker : N.NonStriker;
			return Fill(Pick({ TEXT("Run out! {S} is short of the crease."), TEXT("They went for it, and {S} is run out.") }, V), Out, Where);
		}
		case EDismissal::Stumped: return Fill(TEXT("Down the track, beaten, and stumped! {S} is out."), N, Where);
		case EDismissal::HitWicket: return Fill(TEXT("{S} treads on the stumps. Hit wicket!"), N, Where);
		default: break;
		}
		if (O.bWide) return Pick({ TEXT("Wide. Too far from the batter."), TEXT("Called wide: an extra run and the ball again.") }, V);

		const FString Dropped = R.Fielding.bCatchChance && !R.Fielding.bCaught ? Fill(TEXT("Put down at {R}! "), N, Where) : FString();
		const FString V1 = Verb(R.Contact.Shot);
		if (O.bOverthrow) return Dropped + TEXT("Overthrows! The throw gets away and runs to the boundary.");
		if (O.Boundary == 6)
			return Dropped + Fill(Pick({ TEXT("{S} launches it over {R}. SIX!"), TEXT("That's huge! Six over {R}."), TEXT("Clean strike from {S}, all the way over {R} for six.") }, V), N, Where);
		if (O.Boundary == 4)
			return Dropped + (bBat && IsEdge(R.Contact.Zone) ? Fill(TEXT("Off the edge, and it races away past {R} for four."), N, Where)
				: Fill(Pick({ TEXT("Beautifully %s through {R}. FOUR."), TEXT("%s away past {R}, and nobody is stopping that. Four.") }, V), N, Where).Replace(TEXT("%s"), *V1));
		if (O.RunsRun > 0)
		{
			const FString Runs = O.RunsRun == 1 ? TEXT("a single") : O.RunsRun == 2 ? TEXT("two") : O.RunsRun == 3 ? TEXT("three") : FString::Printf(TEXT("%d"), O.RunsRun);
			if (!bBat) return Dropped + (O.bLegBye ? TEXT("Off the pad, and they run ") : TEXT("Past everyone, and they run ")) + Runs + (O.bLegBye ? TEXT(" leg bye.") : TEXT(" bye."));
			return Dropped + Fill(O.RunsRun == 1 ? TEXT("%s towards {R} for a single.") : Pick({ TEXT("%s to {R}, and they come back for ") , TEXT("%s into the gap at {R}; they push for ") }, V), N, Where).Replace(TEXT("%s"), *V1)
				+ (O.RunsRun == 1 ? FString() : Runs + TEXT("."));
		}
		if (!Dropped.IsEmpty()) return Dropped + TEXT("No run.");
		if (!bBat)
			return R.Shot.Shot == EShotType::Leave ? FString(TEXT("Left alone."))
				: Fill(Pick({ TEXT("Beaten! {S} swings and misses."), TEXT("Past the bat. Good ball.") }, V), N, Where);
		if (R.Contact.Shot == EShotType::Defend) return Fill(Pick({ TEXT("Solid defence from {S}."), TEXT("Blocked, and no run.") }, V), N, Where);
		return Fill(Pick({ TEXT("Straight to the fielder at {R}. No run."), TEXT("Well stopped at {R}. Dot ball.") }, V), N, Where);
	}

	FString Situation(const FDeliveryOutcome& O, const FSuperOverMatch& M, const FNames& N)
	{
		if (M.Phase == EMatchPhase::MatchComplete)
			return M.Winner == M.BattingTeam() ? FString::Printf(TEXT(" And that wins it for %s!"), *N.BattingTeam)
				: FString::Printf(TEXT(" %s fall short, and the match is over."), *N.BattingTeam);
		if (M.bTied && M.Phase == EMatchPhase::InningsBreak) return TEXT(" Scores level! Another Super Over.");
		if (M.Phase == EMatchPhase::InningsBreak) return FString::Printf(TEXT(" %s set a target of %d."), *N.BattingTeam, M.Target);
		FString S = M.bFreeHit ? TEXT(" Free hit to come.") : TEXT("");
		if (M.IsChase() && M.Phase == EMatchPhase::ReadyForDelivery)
			S += M.BallsRemaining() == 1 ? FString::Printf(TEXT(" %d needed off the last ball."), M.RunsRequired())
				: FString::Printf(TEXT(" %d needed from %d."), M.RunsRequired(), M.BallsRemaining());
		return S;
	}
}

FString Region(const FVector& ExitVel, float OffSign)
{
	const float Deg = FMath::RadiansToDegrees(FMath::Atan2(ExitVel.Y * OffSign, ExitVel.X)); // + off side, 0 straight
	const float A = FMath::Abs(Deg);
	if (Deg >= 0.f) return A < 35.f ? TEXT("long-off") : A < 65.f ? TEXT("cover") : A < 100.f ? TEXT("point") : A < 130.f ? TEXT("backward point") : TEXT("third man");
	return A < 35.f ? TEXT("long-on") : A < 70.f ? TEXT("midwicket") : A < 105.f ? TEXT("square leg") : TEXT("fine leg");
}

FString Describe(const FDeliveryResult& R, const FDeliveryOutcome& Outcome, const FSuperOverMatch& After, const FNames& Names, float OffSign, int32 Variant)
{
	FString Line = Happened(R, Outcome, Names, Region(R.Contact.ExitVel, OffSign), Variant);
	if (!Line.IsEmpty()) Line[0] = FChar::ToUpper(Line[0]);
	return (Outcome.bNoBall ? TEXT("No ball! ") : TEXT("")) + Line + Situation(Outcome, After, Names);
}
}
