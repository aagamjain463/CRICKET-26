#include "CricketBroadcastSequence.h"

namespace CricketSequence
{
namespace
{
	FVector Flat(const FVector& V) { return FVector(V.X, V.Y, 0.f); }

	FVector FlatNormal(const FVector& V, const FVector& Fallback = FVector(1.f, 0.f, 0.f))
	{
		const FVector F = Flat(V);
		const float Len = F.Size();
		return Len > 1e-3f ? F / Len : Fallback;
	}

	/** Right of a flat facing in Unreal's left-handed frame (X forward, Y right, Z up). */
	FVector RightOf(const FVector& Facing) { return FVector(-Facing.Y, Facing.X, 0.f); }

	const FVector WorldUp(0.f, 0.f, 1.f);

	// Heights on a standing player (cm above the ground): the eyes, the chest, the waist.
	constexpr float Eye = 160.f, Chest = 135.f, Waist = 100.f;

	/** The share of the beat gone, 0..1, for the push-ins. */
	float Progress(const FShotFrame& F) { return FMath::Clamp(F.T / FMath::Max(F.Duration, 0.1f), 0.f, 1.f); }

	/** Appends a beat after the last one. A beat with no time in this pacing is left out. */
	struct FBuilder
	{
		FDirector& D;
		FSequence& S;
		FSegment& Add(ESegment Kind, EShot Shot, ESubject Who, ESubject Second = ESubject::None, ECard Card = ECard::None,
			ESignal Signal = ESignal::None, float Seconds = -1.f)
		{
			static FSegment Dropped;
			const float Len = Seconds >= 0.f ? Seconds : BeatSeconds(Kind, D.Pacing);
			if (Len <= 0.f) { Dropped = FSegment(); return Dropped; }
			FSegment G;
			G.Kind = Kind;
			G.Start = S.Duration();
			G.Duration = Len;
			G.Shot = Shot;
			G.Subject = Who;
			G.Second = Second;
			G.Card = Card;
			G.Signal = Signal;
			G.Side = D.Side();
			S.Segments.Add(G);
			return S.Segments.Last();
		}
	};

	float Pace(EPacing P, float Broadcast, bool bOptional)
	{
		switch (P)
		{
		case EPacing::Full: return Broadcast * 1.25f;
		case EPacing::Quick: return bOptional ? 0.f : Broadcast * 0.7f;
		default: return Broadcast;
		}
	}
}

// ---------- Sequence ----------

int32 FSequence::IndexAt(float T) const
{
	for (int32 I = 0; I < Segments.Num(); ++I)
		if (T >= Segments[I].Start && T < Segments[I].End()) return I;
	return INDEX_NONE;
}

const FSegment* FSequence::At(float T) const
{
	const int32 I = IndexAt(T);
	return I == INDEX_NONE ? nullptr : &Segments[I];
}

const FSegment* FSequence::Find(ESegment Kind) const
{
	for (const FSegment& G : Segments)
		if (G.Kind == Kind) return &G;
	return nullptr;
}

float FSequence::ReplayStart() const
{
	const FSegment* R = Find(ESegment::Replay);
	return R ? R->Start : -1.f;
}

float FSequence::ReplayDuration() const
{
	const FSegment* R = Find(ESegment::Replay);
	return R ? R->Duration : 0.f;
}

void FSequence::SetReplayDuration(float Seconds)
{
	int32 At = INDEX_NONE;
	for (int32 I = 0; I < Segments.Num(); ++I)
		if (Segments[I].Kind == ESegment::Replay) { At = I; break; }
	if (At == INDEX_NONE) return;
	const float Delta = FMath::Max(Seconds, 0.1f) - Segments[At].Duration;
	Segments[At].Duration += Delta;
	for (int32 I = At + 1; I < Segments.Num(); ++I) Segments[I].Start += Delta;
}

float FSequence::AfterReplay() const
{
	const FSegment* R = Find(ESegment::Replay);
	return R ? R->End() : -1.f;
}

int32 FDirector::Pick(ESegment Beat, int32 Variants)
{
	if (Variants <= 1) return 0;
	const int32 Key = int32(Beat) * 16;
	// Fresh: not among this beat's last Variants-1 picks, so every variant plays before any repeats.
	TArray<int32> Fresh;
	for (int32 V = 0; V < Variants; ++V)
		if (!Recent.Contains(Key + V)) Fresh.Add(V);
	const int32 V = Fresh.Num() ? Fresh[Rng.RandRange(0, Fresh.Num() - 1)] : Rng.RandRange(0, Variants - 1);
	Recent.Add(Key + V);
	int32 Kept = 0;
	for (int32 I = Recent.Num() - 1; I >= 0; --I)
		if (Recent[I] / 16 == int32(Beat) && ++Kept > Variants - 1) Recent.RemoveAt(I);
	while (Recent.Num() > 64) Recent.RemoveAt(0);
	return V;
}

// ---------- Timing ----------

float BeatSeconds(ESegment Beat, EPacing P)
{
	// Measured off the reference at Broadcast pacing (Docs/BROADCAST_REFERENCE_GAME_MP4.md §2). Optional beats
	// (reactions, the crowd, the huddle) drop out at Quick; the stingers always keep their length.
	switch (Beat)
	{
	case ESegment::LiveHold: return Pace(P, 0.8f, false);
	case ESegment::UmpireSignal: return Pace(P, 2.8f, false);
	case ESegment::BatterReaction: return Pace(P, 3.2f, true);
	case ESegment::BowlerReaction: return Pace(P, 2.8f, true);
	case ESegment::FielderReaction: return Pace(P, 2.6f, true);
	case ESegment::Celebration: return Pace(P, 3.0f, false);
	case ESegment::TeamHuddle: return Pace(P, 2.6f, true);
	case ESegment::CrowdCutaway: return Pace(P, 1.4f, true);
	case ESegment::StumpsClose: return Pace(P, 1.8f, true);
	case ESegment::BattersConfer: return Pace(P, 3.0f, true);
	case ESegment::StingerIn: return EventCut;
	case ESegment::Replay: return 0.f; // sized from the replay package
	case ESegment::StingerOut: return LogoTotal - LogoCut;
	case ESegment::WalkOff: return Pace(P, 3.4f, false);
	case ESegment::ThisOver: return Pace(P, 3.0f, false);
	case ESegment::BowlerIntro: return Pace(P, 3.4f, false);
	case ESegment::BatterIntro: return Pace(P, 3.8f, false);
	case ESegment::Establishing: return Pace(P, 3.5f, true);
	case ESegment::TossCaptains: return Pace(P, 3.2f, false);
	case ESegment::TossCoin: return Pace(P, 4.6f, false);
	case ESegment::TossResult: return Pace(P, 2.8f, false);
	case ESegment::PairWalkOff: return Pace(P, 3.4f, false);
	case ESegment::WinCaptain: return Pace(P, 3.2f, false);
	case ESegment::Handshake: return Pace(P, 3.0f, false);
	default: return 0.f;
	}
}

ESignal SignalFor(const FBallFacts& Ball)
{
	if (Ball.Out != EOut::None) return ESignal::Out;
	if (Ball.Boundary == 6) return ESignal::Six;
	if (Ball.Boundary == 4 && Ball.bBatContact) return ESignal::Four;
	if (Ball.bNoBall) return ESignal::NoBall;
	if (Ball.bWide) return ESignal::Wide;
	if (Ball.bLegBye && Ball.RunsRun + Ball.Boundary > 0) return ESignal::LegBye;
	if (!Ball.bBatContact && Ball.RunsRun + Ball.Boundary > 0) return ESignal::Bye;
	return ESignal::None;
}

FSequence BuildDeadBall(FDirector& D, const FBallFacts& Ball)
{
	FSequence S;
	FBuilder B{ D, S };
	const ESignal Signal = SignalFor(Ball);
	const bool bWicket = Ball.Out != EOut::None;
	const bool bStumpsDown = Ball.Out == EOut::Bowled || Ball.Out == EOut::HitWicket;
	const bool bQuick = D.Pacing == EPacing::Quick;

	// 1. The live follow rolls on while the ball settles: a six carries into the stands, a bowled ball holds the wicket.
	float Hold = Ball.Boundary == 6 ? 1.6f : Ball.Boundary == 4 ? 0.9f : bWicket ? (bStumpsDown ? 1.8f : 1.4f) : 0.6f;
	if (bQuick) Hold *= 0.6f;
	B.Add(ESegment::LiveHold, EShot::Live, ESubject::None, ESubject::None, bWicket || Ball.Boundary > 0 ? ECard::EventStrip : ECard::None,
		ESignal::None, Hold);

	// 2. The event itself: signal, reactions, celebration.
	if (bWicket)
	{
		// Bowled and hit wicket need no finger: the broken stumps say it.
		if (!bStumpsDown)
			B.Add(ESegment::UmpireSignal, D.Pick(ESegment::UmpireSignal, 2) == 0 ? EShot::UmpireFront : EShot::UmpireLow, ESubject::Umpire,
				ESubject::None, ECard::None, ESignal::Out, BeatSeconds(ESegment::UmpireSignal, D.Pacing) + 0.6f);
		if (!bQuick && D.Pick(ESegment::CrowdCutaway, 2) == 0) B.Add(ESegment::CrowdCutaway, EShot::Crowd, ESubject::Crowd);
		const int32 V = D.Pick(ESegment::Celebration, 3);
		B.Add(ESegment::Celebration, V == 0 ? EShot::TwoShot : V == 1 ? EShot::CloseUp : EShot::LowWide, ESubject::Hero, ESubject::Fielder);
		B.Add(ESegment::TeamHuddle, EShot::Group, ESubject::Hero);
		if (bStumpsDown) B.Add(ESegment::StumpsClose, EShot::StumpsClose, ESubject::Striker);
		else if ((Ball.Out == EOut::Caught || Ball.Out == EOut::LBW) && !Ball.bInningsOver && !Ball.bMatchOver)
			B.Add(ESegment::BattersConfer, EShot::TwoShot, ESubject::DismissedBatter, ESubject::NonStriker);
		if (!bQuick && D.Pick(ESegment::CrowdCutaway, 2) == 0) B.Add(ESegment::CrowdCutaway, EShot::Crowd, ESubject::Crowd);
	}
	else if (Ball.Boundary == 6)
	{
		// Straight to the umpire, or the batter first (low and wide against the sky, or from above watching it go).
		const int32 V = D.Pick(ESegment::BatterReaction, 3);
		if (V > 0) B.Add(ESegment::BatterReaction, V == 1 ? EShot::LowWide : EShot::HighCrane, ESubject::Striker);
		B.Add(ESegment::UmpireSignal, D.Pick(ESegment::UmpireSignal, 2) == 0 ? EShot::UmpireLow : EShot::UmpireFront, ESubject::Umpire,
			ESubject::None, ECard::None, ESignal::Six, BeatSeconds(ESegment::UmpireSignal, D.Pacing) + (V > 0 ? -0.4f : 0.3f));
	}
	else if (Ball.Boundary == 4)
	{
		B.Add(ESegment::UmpireSignal, EShot::UmpireFront, ESubject::Umpire, ESubject::None, ECard::None, Signal);
		const int32 V = D.Pick(ESegment::BatterReaction, 3);
		if (V == 1) B.Add(ESegment::BatterReaction, D.Pick(ESegment::Count, 2) == 0 ? EShot::LowWide : EShot::CloseUp, ESubject::Striker);
		else if (V == 2) B.Add(ESegment::BowlerReaction, EShot::Medium, ESubject::Bowler);
	}
	else if (Signal != ESignal::None)
	{
		// Extras: the umpire's call is the story.
		B.Add(ESegment::UmpireSignal, EShot::UmpireFront, ESubject::Umpire, ESubject::None, ECard::None, Signal,
			BeatSeconds(ESegment::UmpireSignal, D.Pacing) * 0.75f);
	}
	else if (Ball.bDroppedCatch)
	{
		B.Add(ESegment::FielderReaction, EShot::Medium, ESubject::Fielder);
		B.Add(ESegment::BowlerReaction, EShot::CloseUp, ESubject::Bowler);
	}
	else if (Ball.bBeaten || Ball.bEdge)
	{
		B.Add(ESegment::BowlerReaction, D.Pick(ESegment::BowlerReaction, 2) == 0 ? EShot::CloseUp : EShot::Medium, ESubject::Bowler);
	}
	else if (!Ball.bMatchOver)
	{
		// A dot or a run: one short look at whoever the ball left with something to do.
		const int32 V = D.Pick(ESegment::Count, 3);
		const float Len = BeatSeconds(ESegment::BowlerReaction, D.Pacing) * 0.8f;
		if (V == 2 && Ball.bFieldedDeep) B.Add(ESegment::FielderReaction, EShot::Medium, ESubject::Fielder, ESubject::None, ECard::None, ESignal::None, Len);
		else if (V == 1) B.Add(ESegment::BatterReaction, EShot::LowWide, ESubject::Striker, ESubject::None, ECard::None, ESignal::None, Len);
		else B.Add(ESegment::BowlerReaction, EShot::Medium, ESubject::Bowler, ESubject::None, ECard::None, ESignal::None, Len);
	}

	// 3. The replay between its two stingers.
	if (Ball.bReplay && Ball.ReplaySeconds > 0.f)
	{
		B.Add(ESegment::StingerIn, EShot::Live, ESubject::None);
		B.Add(ESegment::Replay, EShot::Live, ESubject::None, ESubject::None, ECard::None, ESignal::None, Ball.ReplaySeconds);
		B.Add(ESegment::StingerOut, EShot::Live, ESubject::None);
	}

	// 4. What the ball leaves behind.
	if (Ball.bMatchOver)
	{
		if (Ball.bTied)
		{
			B.Add(ESegment::CrowdCutaway, EShot::Crowd, ESubject::Crowd, ESubject::None, ECard::Result, ESignal::None, 2.f);
			B.Add(ESegment::Handshake, EShot::TwoShot, ESubject::Bowler, ESubject::Striker, ECard::Result);
			return S;
		}
		B.Add(ESegment::WinCaptain, EShot::CloseUp, ESubject::Hero, ESubject::None, ECard::Result);
		B.Add(ESegment::TeamHuddle, EShot::Group, ESubject::Hero, ESubject::None, ECard::Result);
		B.Add(ESegment::CrowdCutaway, EShot::Crowd, ESubject::Crowd, ESubject::None, ECard::Result, ESignal::None, 2.f);
		B.Add(ESegment::Handshake, EShot::TwoShot, ESubject::Bowler, ESubject::Striker, ECard::Result);
		return S;
	}
	if (bWicket) B.Add(ESegment::WalkOff, EShot::WalkFront, ESubject::DismissedBatter, ESubject::None, ECard::Dismissal);
	if (Ball.bInningsOver)
	{
		if (!bWicket) B.Add(ESegment::PairWalkOff, EShot::WalkFront, ESubject::Striker, ESubject::NonStriker);
	}
	else if (Ball.bOverComplete)
	{
		B.Add(ESegment::ThisOver, EShot::ThisOver, ESubject::Ground, ESubject::None, ECard::ThisOver);
		if (!bQuick && D.Pick(ESegment::CrowdCutaway, 2) == 0) B.Add(ESegment::CrowdCutaway, EShot::Crowd, ESubject::Crowd);
	}
	return S;
}

FSequence BuildIntro(FDirector& D, const FIntroFacts& Intro)
{
	FSequence S;
	FBuilder B{ D, S };
	if (Intro.bToss)
	{
		B.Add(ESegment::Establishing, EShot::Establishing, ESubject::Ground);
		B.Add(ESegment::TossCaptains, EShot::TossTwoShot, ESubject::BattingCaptain, ESubject::BowlingCaptain, ECard::PitchConditions);
		B.Add(ESegment::TossCoin, EShot::TossCoin, ESubject::Coin, ESubject::None, ECard::TossCall);
		B.Add(ESegment::TossResult, EShot::TossTwoShot, ESubject::BattingCaptain, ESubject::BowlingCaptain, ECard::TossResult);
		B.Add(ESegment::CrowdCutaway, EShot::Crowd, ESubject::Crowd);
	}
	if (Intro.bInningsStart)
	{
		if (!Intro.bToss) B.Add(ESegment::Establishing, EShot::Establishing, ESubject::Ground, ESubject::None, ECard::None, ESignal::None,
			BeatSeconds(ESegment::Establishing, D.Pacing) * 0.7f);
		// The openers at the crease, then the bowler at the top of the mark.
		B.Add(ESegment::BatterIntro, EShot::CloseUp, ESubject::Striker, ESubject::None, ECard::Batter);
		B.Add(ESegment::BowlerIntro, EShot::LowWide, ESubject::Bowler, ESubject::None, ECard::Bowler);
		return S;
	}
	if (Intro.bNewBatter)
		B.Add(ESegment::BatterIntro, D.Pick(ESegment::BatterIntro, 2) == 0 ? EShot::WalkHigh : EShot::WalkFront, ESubject::Striker,
			ESubject::None, ECard::Batter);
	if (Intro.bNewBowler)
	{
		const int32 V = D.Pick(ESegment::BowlerIntro, 3);
		B.Add(ESegment::BowlerIntro, V == 0 ? EShot::Medium : V == 1 ? EShot::TrackBeside : EShot::LowWide, ESubject::Bowler, ESubject::None, ECard::Bowler);
	}
	return S;
}

// ---------- Stingers and cards ----------

FStingerLook StingerLook(EStinger Kind, float T)
{
	FStingerLook L;
	if (Kind == EStinger::Event)
	{
		if (T < 0.f || T >= EventTotal) return L;
		// The picture dips as the stinger begins, the bands close (0.1-0.4), the word comes up on the card; the
		// camera cuts under the card at EventCut, then the word grows as the card dissolves into the replay.
		if (T < EventCut)
		{
			L.Cover = FMath::SmoothStep(0.1f, 0.4f, T);
			L.Dim = 0.3f * FMath::SmoothStep(0.f, 0.12f, T) * (1.f - L.Cover);
		}
		else L.CardAlpha = 1.f - FMath::SmoothStep(0.7f, EventTotal, T);
		L.LabelAlpha = FMath::SmoothStep(0.38f, 0.55f, T) * (1.f - FMath::SmoothStep(0.82f, EventTotal, T));
		L.LabelScale = T < EventCut ? FMath::Lerp(0.82f, 1.f, FMath::SmoothStep(0.38f, EventCut, T)) : FMath::Lerp(1.f, 1.35f, FMath::SmoothStep(EventCut, EventTotal, T));
	}
	else if (Kind == EStinger::Logo)
	{
		if (T < 0.f || T >= LogoTotal) return L;
		// The bands close over the replay's last moment, the camera cuts to the next shot under the card at
		// LogoCut, the logo grows on the card, and the card dissolves into the live picture.
		if (T < LogoCut) L.Cover = FMath::SmoothStep(0.f, LogoCut - 0.02f, T);
		else L.CardAlpha = 1.f - FMath::SmoothStep(0.6f, LogoTotal, T);
		L.LabelAlpha = FMath::SmoothStep(LogoCut, 0.45f, T) * (1.f - FMath::SmoothStep(0.72f, LogoTotal, T));
		L.LabelScale = T < 0.55f ? FMath::Lerp(0.85f, 1.f, FMath::SmoothStep(LogoCut, 0.55f, T)) : FMath::Lerp(1.f, 1.3f, FMath::SmoothStep(0.55f, LogoTotal, T));
	}
	return L;
}

EStinger StingerAt(const FSequence& S, float T, float& OutLocal)
{
	OutLocal = 0.f;
	if (const FSegment* In = S.Find(ESegment::StingerIn))
	{
		const float L = T - In->Start;
		if (L >= 0.f && L < EventTotal) { OutLocal = L; return EStinger::Event; }
	}
	if (const FSegment* Out = S.Find(ESegment::StingerOut))
	{
		const float L = T - (Out->Start - LogoCut);
		if (L >= 0.f && L < LogoTotal) { OutLocal = L; return EStinger::Logo; }
	}
	return EStinger::None;
}

FCardLook CardLook(float T, float Duration)
{
	FCardLook C;
	if (T < 0.f || T >= Duration) return C;
	// The header bar grows in (0.3 s), the stats fade in after it, and the whole card fades over the beat's last 0.3 s.
	C.Bar = FMath::SmoothStep(0.f, 0.3f, T);
	C.Body = FMath::SmoothStep(0.25f, 0.5f, T);
	C.Alpha = 1.f - FMath::SmoothStep(FMath::Max(Duration - 0.3f, 0.5f), Duration, T);
	return C;
}

// ---------- Cameras ----------

bool IsTracking(EShot Shot)
{
	switch (Shot)
	{
	case EShot::WalkFront: case EShot::WalkHigh: case EShot::TrackBeside: case EShot::TossCoin: case EShot::Group: return true;
	default: return false;
	}
}

FShotSolution SolveShot(EShot Shot, const FShotFrame& F)
{
	FShotSolution Sol;
	const FVector S = F.Subject;
	const FVector Fw = FlatNormal(F.Facing);
	const FVector R = RightOf(Fw);
	const float K = Progress(F);
	const float Sd = F.Side >= 0.f ? 1.f : -1.f;
	// Every shot creeps in: about 9% of the lens over the beat (the reference never locks a close-up off).
	const float Creep = 1.f - 0.09f * K;
	auto Focus = [&](float Aperture)
	{
		Sol.FocusCm = FVector::Dist(Sol.Location, Sol.LookAt);
		Sol.Aperture = Aperture;
	};
	switch (Shot)
	{
	case EShot::UmpireFront:
		Sol.Location = S + Fw * 480.f + R * (Sd * 120.f) + WorldUp * 150.f;
		Sol.LookAt = S + WorldUp * (Chest + 5.f);
		Sol.FOV = 22.f * Creep;
		Focus(2.8f);
		break;
	case EShot::UmpireLow:
		// Low and three-quarter so the raised arms read against the sky and the stands.
		Sol.Location = S + Fw * 360.f + R * (Sd * 110.f) + WorldUp * 85.f;
		Sol.LookAt = S + WorldUp * (Eye - 5.f);
		Sol.FOV = 30.f * Creep;
		Focus(2.8f);
		break;
	case EShot::CloseUp:
		Sol.Location = S + Fw * 300.f + R * (Sd * 80.f) + WorldUp * (Eye + 2.f);
		Sol.LookAt = S + WorldUp * (Eye - 4.f);
		Sol.FOV = 15.f * Creep;
		Focus(2.f);
		break;
	case EShot::Medium:
		Sol.Location = S + Fw * 480.f + R * (Sd * 120.f) + WorldUp * 140.f;
		Sol.LookAt = S + WorldUp * (Chest - 10.f);
		Sol.FOV = 20.f * Creep;
		Focus(2.8f);
		break;
	case EShot::LowWide:
		// Close and low with a wide lens: the player tall against the sky, the stands and the big screen.
		Sol.Location = S + Fw * 230.f + R * (Sd * 75.f) + WorldUp * 60.f;
		Sol.LookAt = S + WorldUp * (Chest + 15.f);
		Sol.FOV = 52.f * (1.f - 0.05f * K);
		break;
	case EShot::HighCrane:
		// Above and behind, looking over the player's shoulder out at the field.
		Sol.Location = S - Fw * 520.f + R * (Sd * 150.f) + WorldUp * 650.f;
		Sol.LookAt = S + Fw * 200.f + WorldUp * 60.f;
		Sol.FOV = 34.f * Creep;
		break;
	case EShot::TwoShot:
	{
		// Over the second player's shoulder onto the subject.
		const FVector Across = FlatNormal(S - F.Second, Fw);
		const FVector Perp = RightOf(Across);
		Sol.Location = F.Second - Across * 130.f + Perp * (Sd * 55.f) + WorldUp * 170.f;
		Sol.LookAt = S + WorldUp * (Eye - 10.f);
		Sol.FOV = 24.f * Creep;
		Focus(2.2f);
		break;
	}
	case EShot::Group:
		Sol.Location = S + Fw * 720.f + R * (Sd * 260.f) + WorldUp * 230.f;
		Sol.LookAt = S + WorldUp * (Waist + 15.f);
		Sol.FOV = 30.f * Creep;
		Focus(4.f);
		break;
	case EShot::Crowd:
	{
		// A slow pan along the stand, focus on the fans.
		const FVector Toward = FlatNormal(F.Target - F.From);
		Sol.Location = F.From;
		Sol.LookAt = F.Target + RightOf(Toward) * (Sd * (K - 0.5f) * 250.f);
		Sol.FOV = 26.f * (1.f - 0.06f * K);
		Focus(2.8f);
		break;
	}
	case EShot::StumpsClose:
	{
		// The striker's stumps from knee height, a little in front on the off side.
		const FVector Stumps = F.Target;
		Sol.Location = Stumps + FVector(130.f, 110.f * F.OffSign, 0.f) + WorldUp * 45.f;
		Sol.LookAt = Stumps + WorldUp * 40.f;
		Sol.FOV = 26.f * Creep;
		Focus(2.f);
		break;
	}
	case EShot::ThisOver:
		// Long lens from well behind the bowler's stumps, low, down the pitch: the length bands compress in front of
		// the striker's stumps as the reference's do.
		Sol.Location = FVector(F.PitchLength + 2600.f, 0.f, 240.f);
		Sol.LookAt = FVector(150.f, 0.f, 25.f);
		Sol.FOV = 8.5f * (1.f - 0.04f * K);
		break;
	case EShot::TrackBeside:
		Sol.Location = S + R * (Sd * 520.f) + Fw * 150.f + WorldUp * 150.f;
		Sol.LookAt = S + WorldUp * (Chest - 5.f);
		Sol.FOV = 22.f * Creep;
		Focus(2.8f);
		break;
	case EShot::WalkFront:
		// Backing away in front of the walker (Facing is the way they walk).
		Sol.Location = S + Fw * 380.f + R * (Sd * 60.f) + WorldUp * 160.f;
		Sol.LookAt = S + WorldUp * (Eye - 10.f);
		Sol.FOV = 17.f * Creep;
		Focus(2.f);
		break;
	case EShot::WalkHigh:
		Sol.Location = S - Fw * 650.f + R * (Sd * 120.f) + WorldUp * 620.f;
		Sol.LookAt = S + Fw * 350.f + WorldUp * 50.f;
		Sol.FOV = 32.f * Creep;
		break;
	case EShot::Establishing:
	{
		const float A = (Sd > 0.f ? 0.6f : 2.4f) + 0.05f * F.T;
		Sol.Location = F.PitchCentre + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * 5600.f + WorldUp * 2600.f;
		Sol.LookAt = F.PitchCentre + WorldUp * 200.f;
		Sol.FOV = 58.f;
		break;
	}
	case EShot::TossCoin:
	{
		// Level with the coin from the side of the pitch, the stands soft behind it.
		const FVector Side = RightOf(Fw) * Sd;
		Sol.Location = FVector(F.Target.X, F.Target.Y, 0.f) + Side * 380.f + WorldUp * 160.f;
		Sol.LookAt = F.Target;
		Sol.FOV = 14.f;
		Focus(1.8f);
		break;
	}
	case EShot::TossTwoShot:
	{
		const FVector Mid = (S + F.Second) * 0.5f;
		const FVector Across = FlatNormal(F.Second - S, R);
		Sol.Location = Mid + RightOf(Across) * (Sd * 430.f) + WorldUp * 150.f;
		Sol.LookAt = Mid + WorldUp * (Chest + 3.f);
		Sol.FOV = 25.f * Creep;
		Focus(2.2f);
		break;
	}
	default:
		Sol.Location = S + Fw * 500.f + WorldUp * 160.f;
		Sol.LookAt = S + WorldUp * Chest;
		Sol.FOV = 22.f;
		break;
	}
	Sol.Location.Z = FMath::Max(Sol.Location.Z, 30.f);
	return Sol;
}

// ---------- Bodies ----------

namespace
{
	float ArmWeight(float T, float Duration, float In)
	{
		return FMath::SmoothStep(0.f, In, T) * (1.f - FMath::SmoothStep(FMath::Max(Duration - LowerTime, In), Duration, T));
	}
}

FArms SignalArms(ESignal Signal, float T, float Duration, const FVector ShoulderL, const FVector ShoulderR, const FVector& Forward, const FVector& Right)
{
	FArms A;
	const FVector F = FlatNormal(Forward), Rt = FlatNormal(Right, RightOf(F));
	const FVector L = ShoulderL, Rs = ShoulderR;
	switch (Signal)
	{
	case ESignal::Six:
		// Both arms up, slowly, held.
		A.Hand[0] = L + WorldUp * 58.f - Rt * 6.f + F * 6.f;
		A.Hand[1] = Rs + WorldUp * 58.f + Rt * 6.f + F * 6.f;
		A.Elbow[0] = L - Rt * 30.f;
		A.Elbow[1] = Rs + Rt * 30.f;
		A.Weight[0] = A.Weight[1] = ArmWeight(T, Duration, RaiseTime);
		break;
	case ESignal::Four:
	{
		// The right arm waved to and fro across the front of the body at chest height.
		const float Wave = FMath::Sin(2.f * PI * 1.1f * FMath::Max(T - 0.3f, 0.f));
		A.Hand[1] = Rs + F * 42.f - Rt * 10.f - WorldUp * 8.f + Rt * (30.f * Wave);
		A.Elbow[1] = Rs + Rt * 25.f - WorldUp * 25.f;
		A.Weight[1] = ArmWeight(T, Duration, 0.35f);
		break;
	}
	case ESignal::Out:
	case ESignal::Bye:
		A.Hand[1] = Rs + WorldUp * 52.f + F * 14.f + Rt * 4.f;
		A.Elbow[1] = Rs + Rt * 20.f + F * 10.f;
		A.Weight[1] = ArmWeight(T, Duration, RaiseTime);
		break;
	case ESignal::Wide:
		A.Hand[0] = L - Rt * 58.f - WorldUp * 3.f;
		A.Hand[1] = Rs + Rt * 58.f - WorldUp * 3.f;
		A.Elbow[0] = L - WorldUp * 20.f;
		A.Elbow[1] = Rs - WorldUp * 20.f;
		A.Weight[0] = A.Weight[1] = ArmWeight(T, Duration, 0.5f);
		break;
	case ESignal::NoBall:
		A.Hand[1] = Rs + Rt * 58.f - WorldUp * 2.f;
		A.Elbow[1] = Rs - WorldUp * 20.f;
		A.Weight[1] = ArmWeight(T, Duration, 0.5f);
		break;
	case ESignal::LegBye:
		A.Hand[1] = Rs - WorldUp * 62.f + F * 18.f - Rt * 2.f;
		A.Elbow[1] = Rs + Rt * 20.f - WorldUp * 30.f;
		A.Weight[1] = ArmWeight(T, Duration, 0.5f);
		break;
	default:
		break;
	}
	return A;
}

FArms GestureArms(EGesture Gesture, float T, float Duration, const FVector ShoulderL, const FVector ShoulderR, const FVector& Forward, const FVector& Right)
{
	FArms A;
	const FVector F = FlatNormal(Forward), Rt = FlatNormal(Right, RightOf(F));
	const FVector L = ShoulderL, Rs = ShoulderR;
	const FVector Mid = (L + Rs) * 0.5f;
	switch (Gesture)
	{
	case EGesture::ArmsUp:
	{
		// Both arms flung up and apart, pumping twice in the first second.
		const float Pump = 8.f * FMath::Max(0.f, FMath::Sin(2.f * PI * 2.f * T)) * (1.f - FMath::SmoothStep(0.f, 1.2f, T));
		A.Hand[0] = L + WorldUp * (52.f + Pump) - Rt * 18.f + F * 8.f;
		A.Hand[1] = Rs + WorldUp * (52.f + Pump) + Rt * 18.f + F * 8.f;
		A.Elbow[0] = L - Rt * 35.f;
		A.Elbow[1] = Rs + Rt * 35.f;
		A.Weight[0] = A.Weight[1] = ArmWeight(T, Duration, 0.25f);
		break;
	}
	case EGesture::FistPump:
	{
		const float Pump = 25.f * FMath::Max(0.f, FMath::Sin(2.f * PI * 1.6f * T)) * (1.f - FMath::SmoothStep(0.5f, 2.5f, T));
		A.Hand[1] = Rs + F * 25.f - WorldUp * 5.f + WorldUp * Pump;
		A.Elbow[1] = Rs + Rt * 18.f - WorldUp * 30.f;
		A.Hand[0] = L + F * 20.f - WorldUp * 28.f;
		A.Elbow[0] = L - Rt * 15.f - WorldUp * 25.f;
		A.Weight[1] = ArmWeight(T, Duration, 0.2f);
		A.Weight[0] = 0.6f * A.Weight[1];
		break;
	}
	case EGesture::HandsOnHips:
		A.Hand[0] = L - WorldUp * 50.f - Rt * 2.f;
		A.Hand[1] = Rs - WorldUp * 50.f + Rt * 2.f;
		A.Elbow[0] = L - Rt * 30.f - WorldUp * 25.f;
		A.Elbow[1] = Rs + Rt * 30.f - WorldUp * 25.f;
		A.Weight[0] = A.Weight[1] = ArmWeight(T, Duration, 0.45f);
		break;
	case EGesture::HandsOnHead:
		A.Hand[0] = L + WorldUp * 30.f + F * 8.f + Rt * 14.f;
		A.Hand[1] = Rs + WorldUp * 30.f + F * 8.f - Rt * 14.f;
		A.Elbow[0] = L - Rt * 30.f + WorldUp * 10.f;
		A.Elbow[1] = Rs + Rt * 30.f + WorldUp * 10.f;
		A.Weight[0] = A.Weight[1] = ArmWeight(T, Duration, 0.35f);
		break;
	case EGesture::Clap:
	{
		const FVector C = Mid + F * 30.f - WorldUp * 18.f;
		const float Open = 12.f + 10.f * FMath::Abs(FMath::Sin(PI * 1.8f * T));
		A.Hand[0] = C - Rt * Open;
		A.Hand[1] = C + Rt * Open;
		A.Elbow[0] = L - WorldUp * 25.f - Rt * 10.f;
		A.Elbow[1] = Rs - WorldUp * 25.f + Rt * 10.f;
		A.Weight[0] = A.Weight[1] = ArmWeight(T, Duration, 0.3f);
		break;
	}
	case EGesture::HighFive:
		A.Hand[1] = Rs + WorldUp * 40.f + F * 30.f;
		A.Elbow[1] = Rs + Rt * 20.f;
		A.Weight[1] = ArmWeight(T, Duration, 0.3f);
		break;
	case EGesture::Point:
		A.Hand[1] = Rs + F * 55.f + WorldUp * 10.f;
		A.Elbow[1] = Rs + Rt * 15.f - WorldUp * 10.f;
		A.Weight[1] = ArmWeight(T, Duration, 0.3f);
		break;
	default:
		break;
	}
	return A;
}

FVector WalkTo(const FVector& From, const FVector& To, float Speed, float T)
{
	const FVector D = Flat(To - From);
	const float Dist = D.Size();
	if (Dist < 1.f) return From;
	return From + D / Dist * FMath::Min(Dist, FMath::Max(Speed, 0.f) * FMath::Max(T, 0.f));
}

const TCHAR* SegmentName(ESegment Kind)
{
	switch (Kind)
	{
	case ESegment::LiveHold: return TEXT("LIVE-HOLD");
	case ESegment::UmpireSignal: return TEXT("UMPIRE-SIGNAL");
	case ESegment::BatterReaction: return TEXT("BATTER-REACTION");
	case ESegment::BowlerReaction: return TEXT("BOWLER-REACTION");
	case ESegment::FielderReaction: return TEXT("FIELDER-REACTION");
	case ESegment::Celebration: return TEXT("CELEBRATION");
	case ESegment::TeamHuddle: return TEXT("TEAM-HUDDLE");
	case ESegment::CrowdCutaway: return TEXT("CROWD");
	case ESegment::StumpsClose: return TEXT("STUMPS");
	case ESegment::BattersConfer: return TEXT("BATTERS-CONFER");
	case ESegment::StingerIn: return TEXT("STINGER-IN");
	case ESegment::Replay: return TEXT("REPLAY");
	case ESegment::StingerOut: return TEXT("STINGER-OUT");
	case ESegment::WalkOff: return TEXT("WALK-OFF");
	case ESegment::ThisOver: return TEXT("THIS-OVER");
	case ESegment::BowlerIntro: return TEXT("BOWLER-INTRO");
	case ESegment::BatterIntro: return TEXT("BATTER-INTRO");
	case ESegment::Establishing: return TEXT("ESTABLISHING");
	case ESegment::TossCaptains: return TEXT("TOSS-CAPTAINS");
	case ESegment::TossCoin: return TEXT("TOSS-COIN");
	case ESegment::TossResult: return TEXT("TOSS-RESULT");
	case ESegment::PairWalkOff: return TEXT("PAIR-WALK-OFF");
	case ESegment::WinCaptain: return TEXT("WIN-CAPTAIN");
	case ESegment::Handshake: return TEXT("HANDSHAKE");
	default: return TEXT("NONE");
	}
}
}
