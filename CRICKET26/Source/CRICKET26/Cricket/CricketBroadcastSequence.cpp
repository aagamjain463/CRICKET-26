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
	float Dot3(const FVector& A, const FVector& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
	FVector Cross3(const FVector& A, const FVector& B) { return FVector(A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X); }
	FVector Unit(const FVector& V, const FVector& Fallback)
	{
		const float Len = V.Size();
		return Len > 1e-4f ? V / Len : Fallback;
	}
	/** V with its share along the unit Axis taken out, normalised; Fallback if nothing is left. */
	FVector Square(const FVector& V, const FVector& Axis, const FVector& Fallback) { return Unit(V - Axis * Dot3(V, Axis), Fallback); }

	/**
	 * One arm's shape relative to the body: the way from the shoulder to the wrist, how far along the arm's full
	 * reach the wrist sits (under 1: the elbow keeps a little bend and the solve never snaps straight), the way the
	 * elbow points, the way the palm faces and how the fingers are held.
	 */
	struct FSigArm
	{
		FVector Dir = FVector(0.f, 0.f, -1.f);
		float Reach = 0.95f;
		FVector Pole = FVector(-1.f, 0.f, 0.f);
		FVector Palm = FVector(0.f, 1.f, 0.f);
		EHandShape Shape = EHandShape::Relaxed;
	};

	/** The idle's hanging arm: straight down by the thigh, the elbow back, the palm to the leg. */
	FSigArm SigRest(int32 Side, const FVector& F, const FVector& Rt)
	{
		const FVector Out = Side == 0 ? -Rt : Rt;
		FSigArm A;
		A.Dir = Unit(-WorldUp + F * 0.06f + Out * 0.12f, -WorldUp);
		A.Reach = 0.95f;
		A.Pole = Unit(-F + Out * 0.3f, -F);
		A.Palm = -Out;
		return A;
	}

	/** An arm reaching for a point: its direction and reach from the shoulder. */
	FSigArm SigToward(const FVector& Shoulder, const FVector& Point, float Full, const FVector& Pole, const FVector& Palm, EHandShape Shape)
	{
		FSigArm A;
		A.Dir = Unit(Point - Shoulder, -WorldUp);
		A.Reach = FMath::Clamp(float(FVector::Dist(Shoulder, Point)) / FMath::Max(Full, 1.f), 0.3f, 0.97f);
		A.Pole = Pole;
		A.Palm = Palm;
		A.Shape = Shape;
		return A;
	}

	/**
	 * From direction A to B along a curve bowed toward Via (a quadratic Bezier on the directions, normalised): a raise
	 * from hanging to overhead goes round the front or the side and never collapses through the shoulder.
	 */
	FVector SigArc(const FVector& A, const FVector& B, const FVector& Via, float S)
	{
		FVector C = Via.Size() > 1e-3f ? Unit(Via, A) : A + B;
		if (C.Size() < 1e-3f) C = Unit(Cross3(A, WorldUp), FVector(1.f, 0.f, 0.f));
		C = Unit(C, A);
		const float U = 1.f - S;
		return Unit(A * (U * U) + C * (2.f * S * U) + B * (S * S), B);
	}

	FSigArm SigBlend(const FSigArm& A, const FSigArm& B, const FVector& Via, float S)
	{
		FSigArm R;
		R.Dir = SigArc(A.Dir, B.Dir, Via, S);
		R.Reach = FMath::Lerp(A.Reach, B.Reach, S);
		R.Pole = SigArc(A.Pole, B.Pole, FVector::ZeroVector, S);
		R.Palm = SigArc(A.Palm, B.Palm, FVector::ZeroVector, S);
		R.Shape = S > 0.4f ? B.Shape : A.Shape;
		return R;
	}

	/** Writes one arm's targets: the wrist inside reach, a pole square off the middle of the arm, and the hand on the line of the forearm. */
	void SigPlace(FArms& Out, int32 Side, const FArmFrame& Body, const FSigArm& Arm, float Weight)
	{
		const FVector S = Body.Shoulder[Side];
		const float Full = Body.Upper + Body.Lower;
		const FVector Dir = Unit(Arm.Dir, -WorldUp);
		const FVector Wrist = S + Dir * (Full * FMath::Clamp(Arm.Reach, 0.3f, 0.975f));
		// Square to the arm, so the plane the elbow bends in never turns over however straight the arm is.
		const FVector Across = Square(Side == 0 ? -Body.Right : Body.Right, Dir, FVector(0.f, 0.f, 1.f));
		const FVector Bend = Square(Arm.Pole, Dir, Across);
		const FVector Pole = (S + Wrist) * 0.5f + Bend * 40.f;
		const FVector Elbow = ElbowAt(S, Wrist, Pole, Body.Upper, Body.Lower);
		const FVector Fingers = Unit(Wrist - Elbow, Dir);
		Out.Hand[Side] = Wrist;
		Out.Elbow[Side] = Pole;
		Out.Fingers[Side] = Fingers;
		Out.Palm[Side] = Square(Arm.Palm, Fingers, Square(Bend, Fingers, Across));
		Out.Shape[Side] = Arm.Shape;
		Out.Weight[Side] = FMath::Clamp(Weight, 0.f, 1.f);
	}

	/** The arm taking over from the idle's and handing back: quick, so the arm is on its own path before it moves far. */
	float SigEngage(float T, float Duration)
	{
		const float Edge = FMath::Min(0.15f, 0.25f * Duration);
		return FMath::SmoothStep(0.f, Edge, T) * (1.f - FMath::SmoothStep(Duration - Edge, Duration, T));
	}

	/** How far from the hanging arm to the signal: up over In, held, down over the last LowerTime. */
	float SigProgress(float T, float Duration, float In)
	{
		const float Up = FMath::SmoothStep(0.05f, 0.05f + In, T);
		const float DownFrom = FMath::Max(Duration - LowerTime, 0.05f + In);
		const float DownTo = FMath::Max(Duration - 0.1f, DownFrom + 0.05f);
		return Up * (1.f - FMath::SmoothStep(DownFrom, DownTo, T));
	}
}

FVector ElbowAt(const FVector& Shoulder, const FVector& Wrist, const FVector& Pole, float Upper, float Lower)
{
	// As the engine's two-bone solve places it: in the plane of the shoulder, the wrist and the pole, on the pole's side.
	const FVector U = Unit(Wrist - Shoulder, -WorldUp);
	const float D = FMath::Clamp(float(FVector::Dist(Shoulder, Wrist)), FMath::Abs(Upper - Lower) + 0.1f, 0.995f * (Upper + Lower));
	const float X = (Upper * Upper - Lower * Lower + D * D) / (2.f * D);
	const FVector Side = Square(Pole - Shoulder, U, Unit(Cross3(U, WorldUp), FVector(1.f, 0.f, 0.f)));
	return Shoulder + U * X + Side * FMath::Sqrt(FMath::Max(Upper * Upper - X * X, 0.f));
}

FArms SignalArms(ESignal Signal, float T, float Duration, const FArmFrame& Body)
{
	FArms A;
	const FVector F = FlatNormal(Body.Forward), Rt = FlatNormal(Body.Right, RightOf(F)), U = WorldUp;
	const float Engage = SigEngage(T, Duration);
	auto Pose = [&](int32 Side, const FSigArm& Want, const FVector& Via, float In)
	{
		SigPlace(A, Side, Body, SigBlend(SigRest(Side, F, Rt), Want, Via, SigProgress(T, Duration, In)), Engage);
	};
	auto Straight = [](const FVector& Dir, const FVector& Pole, const FVector& Palm, EHandShape Shape)
	{
		FSigArm W;
		W.Dir = Unit(Dir, WorldUp);
		W.Reach = 0.97f;
		W.Pole = Pole;
		W.Palm = Palm;
		W.Shape = Shape;
		return W;
	};
	switch (Signal)
	{
	case ESignal::Six:
		// Both arms up together, slowly, round the front: straight above the head, forefingers up, palms to the field.
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector Out = Side == 0 ? -Rt : Rt;
			Pose(Side, Straight(U + F * 0.12f + Out * 0.16f, Out - F * 0.3f, F, EHandShape::Point), F * 0.7f + Out * 0.7f, RaiseTime);
		}
		break;
	case ESignal::Out:
	case ESignal::Bye:
	{
		// The right arm straight up in front of the face: the forefinger for OUT, the open palm for a BYE.
		const bool bOut = Signal == ESignal::Out;
		Pose(1, Straight(U + F * 0.2f + Rt * 0.06f, Rt + F * 0.2f, bOut ? F - Rt * 0.5f : F, bOut ? EHandShape::Point : EHandShape::Open),
			F + Rt * 0.25f, RaiseTime);
		break;
	}
	case ESignal::Wide:
		// Both arms straight out level to the sides, palms down.
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector Out = Side == 0 ? -Rt : Rt;
			Pose(Side, Straight(Out + F * 0.1f - U * 0.03f, -U - F * 0.3f, -U + F * 0.35f, EHandShape::Open), FVector::ZeroVector, 0.5f);
		}
		break;
	case ESignal::NoBall:
		Pose(1, Straight(Rt + F * 0.1f - U * 0.03f, -U - F * 0.3f, -U + F * 0.35f, EHandShape::Open), FVector::ZeroVector, 0.5f);
		break;
	case ESignal::Four:
	{
		// The right arm swept to and fro across the front of the body at waist height, palm down: out to the right,
		// across to the middle, three or four times, the sweep growing in as the arm comes up.
		const float Sweep = FMath::Sin(2.f * PI * 0.95f * FMath::Max(T - 0.3f, 0.f)) * FMath::SmoothStep(0.2f, 0.6f, T);
		FSigArm W;
		W.Dir = Unit(F * 0.9f - U * 0.45f + Rt * (0.2f + 0.55f * Sweep), F);
		W.Reach = 0.9f;
		W.Pole = Rt - U * 0.6f;
		W.Palm = -U + F * 0.2f;
		W.Shape = EHandShape::Open;
		Pose(1, W, FVector::ZeroVector, 0.35f);
		break;
	}
	case ESignal::LegBye:
	{
		// The right hand to the front of the right thigh, patting it twice.
		FSigArm W;
		W.Dir = Unit(-U + F * 0.5f + Rt * 0.08f, -U);
		W.Reach = 0.9f + 0.03f * FMath::Sin(2.f * PI * 2.2f * FMath::Max(T - 0.5f, 0.f)) * FMath::SmoothStep(0.5f, 0.7f, T);
		W.Pole = Rt - F * 0.4f;
		W.Palm = -F * 0.8f - U * 0.6f;
		W.Shape = EHandShape::Open;
		Pose(1, W, FVector::ZeroVector, 0.5f);
		break;
	}
	default:
		break;
	}
	return A;
}

FArms GestureArms(EGesture Gesture, float T, float Duration, const FArmFrame& Body)
{
	FArms A;
	const FVector F = FlatNormal(Body.Forward), Rt = FlatNormal(Body.Right, RightOf(F)), U = WorldUp;
	const FVector Mid = (Body.Shoulder[0] + Body.Shoulder[1]) * 0.5f;
	const float Full = Body.Upper + Body.Lower;
	const float Engage = SigEngage(T, Duration);
	auto Pose = [&](int32 Side, const FSigArm& Want, const FVector& Via, float In, float Share = 1.f)
	{
		SigPlace(A, Side, Body, SigBlend(SigRest(Side, F, Rt), Want, Via, SigProgress(T, Duration, In)), Engage * Share);
	};
	switch (Gesture)
	{
	case EGesture::ArmsUp:
	{
		// Both fists flung up and apart, pumping twice in the first second.
		const float Pump = 0.06f * FMath::Max(0.f, FMath::Sin(2.f * PI * 2.f * T)) * (1.f - FMath::SmoothStep(0.f, 1.2f, T));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector Out = Side == 0 ? -Rt : Rt;
			FSigArm W;
			W.Dir = Unit(U + Out * 0.45f + F * 0.15f, U);
			W.Reach = 0.93f - Pump;
			W.Pole = Out - F * 0.2f;
			W.Palm = F * 0.5f - Out * 0.8f;
			W.Shape = EHandShape::Fist;
			Pose(Side, W, F * 0.6f + Out * 0.8f, 0.4f);
		}
		break;
	}
	case EGesture::FistPump:
	{
		// The right fist punched up in front of the shoulder, the elbow down; the left fist clenched low.
		const float Pump = FMath::Max(0.f, FMath::Sin(2.f * PI * 1.6f * T)) * (1.f - FMath::SmoothStep(0.5f, 2.5f, T));
		FSigArm W;
		W.Dir = Unit(F * 0.9f + U * (0.25f + 0.5f * Pump) + Rt * 0.15f, F);
		W.Reach = 0.55f + 0.12f * Pump;
		W.Pole = -U + Rt * 0.6f - F * 0.2f;
		W.Palm = -Rt;
		W.Shape = EHandShape::Fist;
		Pose(1, W, FVector::ZeroVector, 0.3f);
		FSigArm L;
		L.Dir = Unit(-U * 0.8f + F * 0.55f - Rt * 0.1f, -U);
		L.Reach = 0.7f;
		L.Pole = -F - Rt * 0.5f;
		L.Palm = Rt;
		L.Shape = EHandShape::Fist;
		Pose(0, L, FVector::ZeroVector, 0.3f, 0.6f);
		break;
	}
	case EGesture::HandsOnHips:
		// Hands on the hips, the elbows out and back.
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector Out = Side == 0 ? -Rt : Rt;
			const FVector S = Body.Shoulder[Side];
			Pose(Side, SigToward(S, S - U * 48.f + Out * 3.f - F * 2.f, Full, Out - F * 0.4f, -Out, EHandShape::Open), FVector::ZeroVector, 0.45f);
		}
		break;
	case EGesture::HandsOnHead:
		// Both hands on top of the head, palms down, the elbows up and out.
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector Out = Side == 0 ? -Rt : Rt;
			Pose(Side, SigToward(Body.Shoulder[Side], Mid + U * 26.f + F * 1.f + Out * 5.f, Full, Out + U * 0.5f + F * 0.2f, -U, EHandShape::Open),
				F + Out * 0.6f, 0.45f);
		}
		break;
	case EGesture::Clap:
	{
		// Palm to palm in front of the chest.
		const FVector C = Mid + F * 30.f - U * 16.f;
		const float Open = 3.5f + 9.f * FMath::Abs(FMath::Sin(PI * 1.8f * T));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector Out = Side == 0 ? -Rt : Rt;
			Pose(Side, SigToward(Body.Shoulder[Side], C + Out * Open, Full, -U + Out * 0.5f, -Out, EHandShape::Open), FVector::ZeroVector, 0.3f);
		}
		break;
	}
	case EGesture::HighFive:
	{
		FSigArm W;
		W.Dir = Unit(U * 0.8f + F * 0.6f + Rt * 0.1f, U);
		W.Reach = 0.93f;
		W.Pole = Rt - U * 0.3f;
		W.Palm = F;
		W.Shape = EHandShape::Open;
		Pose(1, W, F * 0.4f + Rt * 0.8f, 0.45f); // up round the side, so the palm never turns about the fingers' own line
		break;
	}
	case EGesture::Point:
	{
		FSigArm W;
		W.Dir = Unit(F + U * 0.15f + Rt * 0.1f, F);
		W.Reach = 0.97f;
		W.Pole = -U + Rt * 0.4f;
		W.Palm = -Rt - U * 0.4f;
		W.Shape = EHandShape::Point;
		Pose(1, W, FVector::ZeroVector, 0.3f);
		break;
	}
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
