#include "CricketCommentaryDirector.h"

namespace CricketCommentaryDirector
{
namespace
{
	bool IsEdge(EContactZone Z)
	{
		return Z == EContactZone::InsideEdge || Z == EContactZone::OutsideEdge || Z == EContactZone::TopEdge || Z == EContactZone::BottomEdge;
	}

	FString FillLine(const CricketCommentary::FLine& L, const CricketCommentary::FNames& N, const FString& Where)
	{
		return FString(L.Text).Replace(TEXT("{S}"), *N.Striker).Replace(TEXT("{NS}"), *N.NonStriker).Replace(TEXT("{R}"), *Where).Replace(TEXT("{B}"), *N.BattingTeam);
	}

	// First two whitespace-separated tokens: catches "That's huge!" / "Clean bowled." repeats.
	FString Opening(const FString& Text)
	{
		FString A, B, Rest;
		if (!Text.Split(TEXT(" "), &A, &Rest)) return Text;
		if (!Rest.Split(TEXT(" "), &B, &Rest)) return A;
		return A + TEXT(" ") + B;
	}

	bool HasTag(const CricketCommentary::FLine& L, const TCHAR* Tag)
	{
		TArray<FString> Tags;
		L.Meta.Tags.ParseIntoArray(Tags, TEXT(","));
		for (FString& T : Tags) if (T.TrimStartAndEnd().Equals(Tag, ESearchCase::IgnoreCase)) return true;
		return false;
	}

	// The lines Happened() would consider, with authority filtering: no edge claim without an edge,
	// no dive praise without a dive, no middled/mistimed claim without the quality to back it.
	void CollectCandidates(const FContext& C, TArray<const CricketCommentary::FLine*>& Out)
	{
		using namespace CricketCommentary;
		const bool bEdge = IsEdge(C.Zone);
		const bool bMiddled = C.Zone == EContactZone::Middle && C.ContactQuality >= 0.8f;
		const bool bMistimed = C.bBatContact && C.ContactQuality <= 0.35f;
		auto AddPool = [&](const TArray<FLine>& Pool, auto Keep)
		{
			for (const FLine& L : Pool) if (Keep(L)) Out.Add(&L);
		};
		switch (C.Dismissal)
		{
		case EDismissal::Bowled:
			AddPool(BowledLines(), [&](const FLine& L) {
				const bool bPlayedOn = HasTag(L, TEXT("played-on"));
				const bool bWasPlayedOn = bEdge || C.Zone == EContactZone::BottomEdge;
				return bPlayedOn == bWasPlayedOn || (!bPlayedOn && !bWasPlayedOn); });
			return;
		case EDismissal::Caught:
			if (C.bCaughtBehind) { AddPool(CaughtBehindLines(), [](const FLine&) { return true; }); return; }
			AddPool(CaughtLines(), [&](const FLine& L) {
				if (HasTag(L, TEXT("diving"))) return C.bDivingCatch;
				if (HasTag(L, TEXT("edge"))) return bEdge && !C.bDivingCatch;
				return !C.bDivingCatch; });
			return;
		case EDismissal::LBW: AddPool(LbwLines(), [](const FLine&) { return true; }); return;
		case EDismissal::RunOut:
			AddPool(RunOutLines(), [&](const FLine& L) {
				if (HasTag(L, TEXT("direct-hit"))) return C.bDirectHit;
				if (HasTag(L, TEXT("close"))) return C.bCloseRunOut && !C.bDirectHit;
				return !C.bDirectHit && !C.bCloseRunOut; });
			return;
		case EDismissal::Stumped: AddPool(StumpedLines(), [](const FLine&) { return true; }); return;
		case EDismissal::HitWicket: AddPool(HitWicketLines(), [](const FLine&) { return true; }); return;
		default: break;
		}
		if (C.bWide) { AddPool(WideLines(), [](const FLine&) { return true; }); return; }
		if (C.bOverthrow) { AddPool(OverthrowLines(), [](const FLine&) { return true; }); return; }
		if (C.Boundary == 6)
		{
			AddPool(SixLines(), [&](const FLine& L) {
				if (HasTag(L, TEXT("mistimed"))) return bMistimed;
				if (HasTag(L, TEXT("middled"))) return bMiddled;
				return !bMistimed; });
			return;
		}
		if (C.Boundary == 4)
		{
			if (bEdge)
			{
				AddPool(EdgeFourLines(), [&](const FLine& L) {
					if (HasTag(L, TEXT("inside"))) return C.Zone == EContactZone::InsideEdge;
					if (HasTag(L, TEXT("thick"))) return C.Zone != EContactZone::InsideEdge;
					return true; });
				return;
			}
			AddPool(FourLines(), [](const FLine&) { return true; });
			return;
		}
		if (C.RunsRun > 1) { AddPool(MultiRunLines(), [](const FLine&) { return true; }); return; }
		if (C.RunsRun == 1) { AddPool(SingleLines(), [](const FLine&) { return true; }); return; }
		if (!C.bBatContact)
		{
			if (C.Shot == EShotType::Leave) { AddPool(LeaveLines(), [](const FLine&) { return true; }); return; }
			AddPool(BeatenLines(), [&](const FLine& L) {
				if (HasTag(L, TEXT("bouncer"))) return C.bBouncer;
				return !C.bBouncer; });
			return;
		}
		if (C.Shot == EShotType::Defend) { AddPool(DefendLines(), [](const FLine&) { return true; }); return; }
		AddPool(DotLines(), [](const FLine&) { return true; });
	}

	int32 EventKind(const FContext& C)
	{
		if (C.Dismissal != EDismissal::None) return 4;
		if (C.Boundary == 6) return 3;
		if (C.Boundary == 4) return 2;
		if (C.RunsRun > 0) return 1;
		if (C.bWide || C.bNoBall) return 5;
		return 0;
	}

	void NoteOutcome(FMemory& M, const FContext& C, int32 BallIndex)
	{
		const int32 K = EventKind(C);
		M.RecentEvents.Add(K);
		if (M.RecentEvents.Num() > 6) M.RecentEvents.RemoveAt(0);
		M.ConsecutiveSixes = (K == 3) ? M.ConsecutiveSixes + 1 : 0;
		M.ConsecutiveBoundaries = (K == 3 || K == 2) ? M.ConsecutiveBoundaries + 1 : 0;
		M.ConsecutiveDots = (K == 0) ? M.ConsecutiveDots + 1 : 0;
		M.ConsecutiveWickets = (K == 4) ? M.ConsecutiveWickets + 1 : 0;
		(void)BallIndex;
	}

	void NoteSpoken(FMemory& M, const CricketCommentary::FLine& L, const FString& Filled, int32 BallIndex)
	{
		M.RecentIds.Add(L.Meta.Id);
		if (M.RecentIds.Num() > 12) M.RecentIds.RemoveAt(0);
		M.RecentOpenings.Add(Opening(Filled));
		if (M.RecentOpenings.Num() > 8) M.RecentOpenings.RemoveAt(0);
		TArray<FString> Tags;
		L.Meta.Tags.ParseIntoArray(Tags, TEXT(","));
		for (FString& T : Tags)
		{
			T.TrimStartAndEndInline();
			if (!T.IsEmpty()) M.RecentTopics.Add(T);
		}
		while (M.RecentTopics.Num() > 6) M.RecentTopics.RemoveAt(0);
		M.CooldownUntil.Add(L.Meta.Id, BallIndex + L.Meta.CooldownBalls);
		M.LastSpeaker = L.Meta.Speaker == CricketCommentary::ESpeaker::Analyst ? TEXT("B") : TEXT("A");
		M.LastId = L.Meta.Id;
	}
}

FContext FContext::Build(const FDeliveryResult& R, const FDeliveryOutcome& O, const FSuperOverMatch& After,
	const CricketCommentary::FNames& Names, float OffSign, EDeliveryType DeliveryType)
{
	FContext C;
	(void)DeliveryType; // reserved: delivery-type claims stay off until ball-type tracking is authoritative
	C.Innings = After.CurrentInnings;
	C.SuperOverNumber = After.SuperOverNumber;
	C.BattingTeam = Names.BattingTeam;
	C.BattingTeamIndex = After.BattingTeam();
	C.Striker = Names.Striker;
	C.NonStriker = Names.NonStriker;
	C.BatterHand = ECricketHand::Right;
	C.Score = After.Cur().Runs;
	C.Wickets = After.Cur().Wickets;
	C.LegalBalls = After.Cur().LegalBalls;
	C.BallsRemaining = After.BallsRemaining();
	C.Target = After.Target;
	C.RunsRequired = After.RunsRequired();
	C.bChase = After.IsChase();
	C.bFinalBall = After.IsChase() && After.Phase == EMatchPhase::ReadyForDelivery && After.BallsRemaining() == 1;
	C.bFreeHit = After.bFreeHit;
	C.bMatchComplete = After.Phase == EMatchPhase::MatchComplete;
	C.bTied = After.bTied;
	C.Winner = After.Winner;
	C.bInningsBreak = After.Phase == EMatchPhase::InningsBreak;
	C.bBatContact = R.Contact.HasContact();
	C.Zone = R.Contact.Zone;
	C.Shot = R.Contact.Shot;
	C.ContactQuality = R.Contact.Quality;
	C.TimingError = R.Contact.TimingError;
	C.Boundary = O.Boundary;
	C.RunsRun = O.RunsRun;
	C.bWide = O.bWide;
	C.bNoBall = O.bNoBall;
	C.bLegBye = O.bLegBye;
	C.bOverthrow = O.bOverthrow;
	C.bBouncer = R.bBouncer;
	C.bBeamer = R.bBeamer;
	C.BallSpeedKph = R.SpeedKph;
	C.Dismissal = O.Dismissal;
	C.bRunOutStriker = O.bRunOutStriker;
	C.bDirectHit = R.Running.bDirectHit;
	C.bCaughtBehind = O.Dismissal == EDismissal::Caught && R.Fielding.Action == EFieldAction::CatchKeeper;
	C.bDivingCatch = O.Dismissal == EDismissal::Caught && (R.Fielding.bDive || R.Fielding.Action == EFieldAction::CatchDiving);
	C.CatchDifficulty = R.Fielding.CatchDifficulty;
	C.bDroppedCatch = R.Fielding.bCatchChance && !R.Fielding.bCaught;
	C.bCloseRunOut = R.BrokenTime >= 0.f && FMath::Abs(R.HomeMargin) < 0.25f;
	C.bPadImpact = R.bPadImpact;
	C.Region = CricketCommentary::Region(R.Contact.ExitVel, OffSign);
	C.PartnershipRuns = After.Cur().PartnershipRuns;
	C.PartnershipBalls = After.Cur().PartnershipBalls;
	return C;
}

float ComputeIntensity(const FContext& C)
{
	// Event base: most balls are calm by design; contrast makes big moments land.
	float I = 0.25f;
	if (C.bWide || C.bNoBall) I = 0.2f;
	if (C.RunsRun > 0 && C.Boundary == 0) I = C.RunsRun >= 3 ? 0.45f : 0.35f;
	if (C.Boundary == 4) I = 0.6f;
	if (C.Boundary == 6) I = 0.8f;
	if (C.Dismissal != EDismissal::None) I = 0.85f;
	if (C.bMatchComplete || C.bTied) I = 1.0f;
	if (C.bInningsBreak && !C.bTied) I = FMath::Max(I, 0.6f);

	// Chase pressure: 6 required from the final ball is the maximum; 1 from 1 is extreme but different.
	if (C.bChase && !C.bMatchComplete)
	{
		if (C.bFinalBall)
		{
			if (C.RunsRequired <= 0) I = FMath::Max(I, 0.7f);
			else if (C.RunsRequired == 1) I = FMath::Max(I, 0.95f);
			else if (C.RunsRequired <= 4) I = FMath::Max(I, 0.9f);
			else if (C.RunsRequired == 6) I = 1.0f;
			else I = FMath::Max(I, 0.85f);
		}
		else if (C.BallsRemaining > 0)
		{
			const float Ratio = float(C.RunsRequired) / float(C.BallsRemaining);
			if (Ratio >= 2.5f) I = FMath::Max(I, C.Boundary ? I : 0.55f);
			else if (Ratio >= 1.5f) I = FMath::Max(I, C.Boundary ? I : 0.45f);
		}
		// Dot under pressure matters: defending a small total late.
		if (C.RunsRun == 0 && C.Boundary == 0 && C.Dismissal == EDismissal::None && C.BallsRemaining <= 2 && C.RunsRequired > 1)
			I = FMath::Max(I, 0.6f);
	}
	// Momentum: consecutive boundaries lift the room a touch.
	if (C.Boundary == 6) I = FMath::Min(1.f, I + 0.05f);
	return FMath::Clamp(I, 0.f, 1.f);
}

int32 PriorityOf(const FContext& C)
{
	if (C.bMatchComplete || C.bTied) return 4;
	if (C.Dismissal != EDismissal::None)
		return (C.bFinalBall || C.BallsRemaining <= 1) ? 4 : 3;
	if (C.Boundary == 6 || C.Boundary == 4)
		return (C.bFinalBall && C.bChase) ? 4 : 2;
	return 1;
}

void FState::Reset()
{
	Mem = FMemory();
	Queue = FPending();
	CurrentId.Reset();
	CurrentEndsAt = -1.f;
	CurrentSpeaker.Reset();
}

void Update(FState& S, float NowSeconds, int32 BallIndex)
{
	if (S.Queue.bHas && (NowSeconds > S.Queue.ExpiresAt || S.Queue.BallIndex < BallIndex - 1))
		S.Queue.bHas = false; // stale analysis never plays late
	if (!S.CurrentId.IsEmpty() && NowSeconds >= S.CurrentEndsAt)
	{
		S.CurrentId.Reset();
		S.CurrentSpeaker.Reset();
	}
}

bool IsSpeaking(const FState& S, float NowSeconds)
{
	return !S.CurrentId.IsEmpty() && NowSeconds < S.CurrentEndsAt;
}

FString TakeHandoff(FState& S, float NowSeconds)
{
	if (!S.Queue.bHas || NowSeconds < S.Queue.AvailableAt || IsSpeaking(S, NowSeconds)) return FString();
	S.Queue.bHas = false;
	S.CurrentId = S.Queue.Line.Meta.Id;
	S.CurrentMeta = S.Queue.Line.Meta;
	S.CurrentSpeaker = TEXT("B");
	S.CurrentEndsAt = NowSeconds + S.Queue.Line.Meta.EstSeconds;
	return S.Queue.Text;
}

FSelection SelectLine(FState& S, const FContext& C, const CricketCommentary::FNames& Names,
	float NowSeconds, int32 BallIndex, FRandomStream& Rng)
{
	Update(S, NowSeconds, BallIndex);
	NoteOutcome(S.Mem, C, BallIndex);
	FSelection Out;
	const float Intensity = ComputeIntensity(C);
	const int32 Priority = PriorityOf(C);

	// Match-end + innings-break are always spoken (voiced result calls).
	const bool bMustSpeak = true;

	TArray<const CricketCommentary::FLine*> Cands;
	// Match-end uses the voiced result calls (winner/tie aware); everything else the event pools.
	if (C.bMatchComplete || C.bTied)
	{
		const bool bBattingWon = C.Winner == C.BattingTeamIndex;
		const bool bFinalish = C.Wickets >= 2 || C.BallsRemaining <= 1;
		for (const CricketCommentary::FLine& L : CricketCommentary::ResultLines())
		{
			if (!C.bTied && C.LegalBalls + C.BallsRemaining > 6 && L.Text.Contains(TEXT("Super Over"))) continue;
			if (HasTag(L, TEXT("tie")) != C.bTied) continue;
			if (C.bTied) { Cands.Add(&L); continue; }
			if (bBattingWon != (HasTag(L, TEXT("chase-win")) || HasTag(L, TEXT("final-ball")))) continue;
			// A defence only ever ends on the last ball or the last wicket: finalish applies to chases.
			if (bBattingWon && HasTag(L, TEXT("final-ball")) != bFinalish) continue;
			Cands.Add(&L);
		}
		if (Cands.Num() == 0) // fallback: never stay silent on the result
			for (const CricketCommentary::FLine& L : CricketCommentary::ResultLines()) Cands.Add(&L);
	}
	else
		CollectCandidates(C, Cands);

	if (Cands.Num() == 0)
	{
		Out.bSilent = true;
		return Out;
	}

	// Effective names: a run-out names the victim, not whoever holds the striker's end now.
	CricketCommentary::FNames Eff = Names;
	if (C.Dismissal == EDismissal::RunOut && !C.bRunOutStriker) Swap(Eff.Striker, Eff.NonStriker);
	// Result lines name the winner as {B}: after a defence that is the fielding side.
	if (C.bMatchComplete && C.Winner != C.BattingTeamIndex && !Names.BowlingTeam.IsEmpty()) Eff.BattingTeam = Names.BowlingTeam;

	// Scored selection: context + emotion + novelty + speaker fit, minus repetition penalties.
	const CricketCommentary::FLine* Best = nullptr;
	float BestScore = -1e9f;
	TArray<const CricketCommentary::FLine*> Top;
	for (const CricketCommentary::FLine* L : Cands)
	{
		float Score = 0.f;
		Score += 1.f - FMath::Abs(L->Meta.Excitement - Intensity); // emotional match
		if (HasTag(*L, TEXT("final-ball")) && C.bFinalBall) Score += 1.f;
		if (HasTag(*L, TEXT("chase")) && C.bChase) Score += 0.5f;
		if (HasTag(*L, TEXT("middled")) && C.Zone == EContactZone::Middle && C.ContactQuality >= 0.8f) Score += 0.5f;
		if (HasTag(*L, TEXT("mistimed")) && C.bBatContact && C.ContactQuality <= 0.35f) Score += 0.5f;
		if (HasTag(*L, TEXT("diving")) && C.bDivingCatch) Score += 0.5f;
		if (HasTag(*L, TEXT("direct-hit")) && C.bDirectHit) Score += 0.5f;
		if (S.Mem.LastSpeaker == TEXT("B") || S.Mem.LastSpeaker.IsEmpty()) Score += 0.15f; // lean play-by-play
		const int32* Cool = S.Mem.CooldownUntil.Find(L->Meta.Id);
		if (Cool && *Cool > BallIndex) Score -= 3.f; // cooling down: strong avoid, not a veto
		if (S.Mem.RecentIds.Contains(L->Meta.Id)) Score -= 2.f;
		if (S.Mem.RecentTopics.Contains(L->Meta.Tags)) Score -= 0.4f;
		Score += Rng.FRand() * 0.3f; // jitter so equal candidates rotate
		if (Score > BestScore + 1e-4f)
		{
			BestScore = Score;
			Best = L;
			Top.Reset();
			Top.Add(L);
		}
		else if (FMath::Abs(Score - BestScore) <= 0.35f && Top.Num() < 3)
			Top.Add(L);
	}
	const CricketCommentary::FLine* Picked = Top.Num() > 1 ? Top[Rng.RandHelper(Top.Num())] : Best;

	// Opening-phrase anti-repetition: step to a same-pool alternative sharing no opening.
	const FString FirstOpen = Opening(FillLine(*Picked, Eff, C.Region));
	if (S.Mem.RecentOpenings.Contains(FirstOpen))
	{
		for (const CricketCommentary::FLine* Alt : Cands)
		{
			if (Alt == Picked) continue;
			const int32* Cool = S.Mem.CooldownUntil.Find(Alt->Meta.Id);
			if (Cool && *Cool > BallIndex) continue;
			if (!S.Mem.RecentOpenings.Contains(Opening(FillLine(*Alt, Eff, C.Region)))) { Picked = Alt; break; }
		}
	}

	// Interruption: a high-priority moment cancels a pending analyst line or a breakable current one.
	if ((S.Queue.bHas && Priority > 0) || (IsSpeaking(S, NowSeconds) && Priority >= 3 && S.CurrentMeta.bCanBeInterrupted))
	{
		Out.bInterrupted = S.Queue.bHas || IsSpeaking(S, NowSeconds);
		S.Queue.bHas = false;
		if (IsSpeaking(S, NowSeconds) && Priority >= 3 && S.CurrentMeta.bCanBeInterrupted)
		{
			S.CurrentId.Reset();
			S.CurrentSpeaker.Reset();
		}
	}

	Out.Text = FillLine(*Picked, Eff, C.Region);
	// %s verb slot for stroke lines (drive/cut/pull/...): same verbs as the caption path.
	{
		FString Verb;
		switch (C.Shot)
		{
		case EShotType::Drive: Verb = TEXT("driven"); break;
		case EShotType::Loft: Verb = TEXT("lofted"); break;
		case EShotType::Punch: Verb = TEXT("punched"); break;
		case EShotType::Cut: Verb = TEXT("cut"); break;
		case EShotType::Pull: Verb = TEXT("pulled"); break;
		case EShotType::Sweep: Verb = TEXT("swept"); break;
		case EShotType::Flick: Verb = TEXT("flicked"); break;
		case EShotType::Hook: Verb = TEXT("hooked"); break;
		case EShotType::SlogSweep: Verb = TEXT("slog-swept"); break;
		case EShotType::ReverseSweep: Verb = TEXT("reverse-swept"); break;
		case EShotType::Scoop: Verb = TEXT("scooped"); break;
		default: Verb = TEXT("pushed"); break;
		}
		Out.Text = Out.Text.Replace(TEXT("%s"), *Verb);
	}
	// Multi-run lines end "...for ": finish them with the runs, as the caption path does.
	if (Picked->Meta.Tags.Contains(TEXT("multi")))
		Out.Text += (C.RunsRun == 2 ? FString(TEXT("two")) : C.RunsRun == 3 ? FString(TEXT("three")) : FString::FromInt(C.RunsRun)) + TEXT(".");
	if (!Out.Text.IsEmpty()) Out.Text[0] = FChar::ToUpper(Out.Text[0]);
	Out.Body = Out.Text;
	// Situation suffix: same chase/target/result awareness as the caption path.
	// Match complete: the result line already names the winner, so nothing is appended.
	if (C.bMatchComplete) {}
	else if (C.bTied && C.bInningsBreak) Out.Suffix = TEXT("Scores level! Another Super Over.");
	else if (C.bInningsBreak) Out.Suffix = FString::Printf(TEXT("Target %d."), C.Target);
	else if (C.bChase && !C.bMatchComplete)
		Out.Suffix = C.BallsRemaining == 1 ? FString::Printf(TEXT("%d needed off the last ball."), C.RunsRequired)
			: FString::Printf(TEXT("%d needed from %d."), C.RunsRequired, C.BallsRemaining);
	if (!Out.Suffix.IsEmpty()) Out.Text += TEXT(" ") + Out.Suffix;
	Out.Meta = Picked->Meta;

	// Timing: let the moment breathe first — impact, crowd, appeal — then react.
	if (C.Dismissal != EDismissal::None) Out.DelaySeconds = 0.8f;
	else if (C.Boundary == 6) Out.DelaySeconds = 1.0f;
	else if (C.Boundary == 4) Out.DelaySeconds = 0.6f;
	else if (C.bMatchComplete || C.bTied) Out.DelaySeconds = 1.2f;
	else Out.DelaySeconds = 0.25f;

	NoteSpoken(S.Mem, *Picked, Out.Text, BallIndex);
	S.CurrentId = Picked->Meta.Id;
	S.CurrentMeta = Picked->Meta;
	S.CurrentSpeaker = Picked->Meta.Speaker == CricketCommentary::ESpeaker::Analyst ? TEXT("B") : TEXT("A");
	S.CurrentEndsAt = NowSeconds + Out.DelaySeconds + Picked->Meta.EstSeconds;

	// Handoff: after a big moment, the analyst may follow once the call lands — never over it.
	if (Picked->Meta.Speaker == CricketCommentary::ESpeaker::PlayByPlay && Intensity >= 0.6f
		&& (C.Boundary == 6 || C.Boundary == 4 || C.Dismissal != EDismissal::None) && Rng.FRand() < 0.5f)
	{
		const TArray<CricketCommentary::FLine>& An = CricketCommentary::AnalysisLines();
		const CricketCommentary::FLine& A = An[Rng.RandHelper(An.Num())];
		const int32* Cool = S.Mem.CooldownUntil.Find(A.Meta.Id);
		if ((!Cool || *Cool <= BallIndex + 1) && !S.Mem.RecentIds.Contains(A.Meta.Id))
		{
			S.Queue.bHas = true;
			S.Queue.Line = A;
			S.Queue.Text = FillLine(A, Names, C.Region);
			S.Queue.BallIndex = BallIndex;
			S.Queue.AvailableAt = S.CurrentEndsAt + 0.4f;
			S.Queue.ExpiresAt = NowSeconds + 12.f; // never leaks into a later ball
		}
	}
	return Out;
}
}
