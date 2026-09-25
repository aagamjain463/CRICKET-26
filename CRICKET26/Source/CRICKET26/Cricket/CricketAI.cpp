#include "CricketAI.h"

namespace
{
	/** How long before the ball arrives a premeditated charge is decided (s): early enough to go and meet it. */
	constexpr float ChargeLead = 0.85f;

	struct FPlanOption
	{
		const TCHAR* Label;
		EDeliveryType Type;
		float Length, Line, Weight;
	};

	/** Batter-relative angle (deg) of a field position as seen from the striker. */
	float AngleOf(const FFielder& F, float Off)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(F.Home.Y * Off, F.Home.X));
	}
}

float CricketAI::SkillOf(EDifficulty D)
{
	static const float Skill[] = { 0.f, 0.4f, DefaultSkill, 1.f };
	return Skill[uint8(D)];
}

const TCHAR* CricketAI::DifficultyName(EDifficulty D)
{
	static const TCHAR* Names[] = { TEXT("Easy"), TEXT("Medium"), TEXT("Hard"), TEXT("Legend") };
	return Names[uint8(D)];
}

float CricketAI::Aggression(const FSuperOverMatch& M)
{
	const FInningsState& In = M.Cur();
	const bool bLastPair = In.Wickets == M.Rules.MaxWickets - 1;
	if (!M.IsChase())
	{
		// Setting a Super Over total: attack, a little less once a wicket has fallen early.
		return FMath::Clamp(0.72f - (bLastPair && M.BallsRemaining() > 2 ? 0.12f : 0.f), 0.f, 1.f);
	}
	const float Balls = FMath::Max(1, M.BallsRemaining());
	const float PerBall = M.RunsRequired() / Balls;
	float A = 0.2f + 0.33f * PerBall;
	if (bLastPair && M.BallsRemaining() > 2) A -= 0.1f;
	return FMath::Clamp(A, 0.1f, 1.f);
}

float CricketAI::RunMargin(const FSuperOverMatch& M, float Aggression, float Skill)
{
	if (M.IsChase() && M.BallsRemaining() == 1 && M.RunsRequired() <= 2) return -0.25f; // must run
	// Below Hard the batters misjudge runs: they take on throws they cannot beat.
	return FMath::Lerp(0.6f, -0.2f, Aggression) - 0.6f * FMath::Max(0.f, DefaultSkill - Skill);
}

FBowlingChoice CricketAI::ChooseDelivery(const FCricketPlayer& Bowler, ECricketHand BatHand, const FSuperOverMatch& M,
	const TArray<int32>& Recent, FRandomStream& Rng, float Skill)
{
	static const FPlanOption Pace[] = {
		// Yorkers go for the block hole under a front-foot bat (~2 m); fuller is a low full toss.
		{ TEXT("Yorker"), EDeliveryType::Stock, 1.9f, 0.05f, 3.f },
		{ TEXT("Wide yorker"), EDeliveryType::Stock, 2.0f, 0.65f, 2.f },
		{ TEXT("Slower ball"), EDeliveryType::Slower, 6.5f, 0.15f, 1.5f },
		{ TEXT("Bouncer"), EDeliveryType::Stock, 11.5f, 0.f, 1.f },
		{ TEXT("Hard length"), EDeliveryType::Cutter, 8.f, 0.2f, 1.f },
		{ TEXT("Full outswinger"), EDeliveryType::Outswing, 4.f, 0.25f, 1.f },
		{ TEXT("Seam up, good length"), EDeliveryType::Seam, 6.5f, 0.2f, 0.8f },
		{ TEXT("Cross-seam, back of a length"), EDeliveryType::CrossSeam, 8.5f, 0.1f, 0.6f },
	};
	static const FPlanOption OffSpin[] = {
		{ TEXT("Stump to stump"), EDeliveryType::OffBreak, 4.8f, 0.1f, 3.f },
		{ TEXT("Arm ball"), EDeliveryType::ArmBall, 4.3f, 0.2f, 1.5f },
		{ TEXT("Quick top-spinner"), EDeliveryType::TopSpinner, 3.5f, 0.05f, 1.f },
		{ TEXT("Wide of off"), EDeliveryType::OffBreak, 5.2f, 0.6f, 1.f },
	};
	static const FPlanOption LegSpin[] = {
		{ TEXT("Leg break"), EDeliveryType::LegBreak, 4.8f, 0.05f, 3.f },
		{ TEXT("Googly"), EDeliveryType::Googly, 4.6f, 0.15f, 1.5f },
		{ TEXT("Quick top-spinner"), EDeliveryType::TopSpinner, 3.6f, 0.0f, 1.f },
		{ TEXT("Wide leg break"), EDeliveryType::LegBreak, 5.0f, 0.55f, 1.f },
		{ TEXT("Slider"), EDeliveryType::Slider, 4.2f, 0.1f, 1.f },
	};
	TArrayView<const FPlanOption> Options = Bowler.BowlerType == EBowlerType::Pace ? TArrayView<const FPlanOption>(Pace)
		: Bowler.BowlerType == EBowlerType::OffSpin ? TArrayView<const FPlanOption>(OffSpin) : TArrayView<const FPlanOption>(LegSpin);

	// Defending a small total late: squeeze with yorkers; otherwise mix it up. A better bowler leans
	// harder on the plans that work, squeezes harder and is less predictable.
	const bool bSqueeze = M.IsChase() && M.RunsRequired() <= M.BallsRemaining() * 1.5f;
	const float Focus = FMath::Lerp(0.4f, 1.2f, Skill);
	TArray<float> W;
	float Sum = 0.f;
	for (int32 I = 0; I < Options.Num(); ++I)
	{
		float Wt = FMath::Pow(Options[I].Weight, Focus) * (bSqueeze && I == 0 ? FMath::Lerp(1.f, 1.67f, Skill) : 1.f);
		if (Recent.Num() > 0 && Recent.Last() == I) Wt *= FMath::Lerp(0.9f, 0.1f, Skill);
		if (Recent.Num() > 1 && Recent[Recent.Num() - 2] == I) Wt *= FMath::Lerp(1.f, 0.47f, Skill);
		W.Add(Wt);
		Sum += Wt;
	}
	float Pick = Rng.GetFraction() * Sum;
	int32 Chosen = 0;
	for (; Chosen < W.Num() - 1; ++Chosen)
	{
		if ((Pick -= W[Chosen]) <= 0.f) break;
	}
	const FPlanOption& O = Options[Chosen];
	FBowlingChoice C;
	C.PlanId = Chosen;
	C.Label = O.Label;
	C.Plan.Type = O.Type;
	C.Plan.Length = O.Length;
	// Below Hard the bowler also aims at the wrong length sometimes (a yorker plan that is really a
	// half-volley). This is the target, not the execution: Accuracy still scatters around it.
	if (Skill < DefaultSkill) C.Plan.Length = FMath::Max(0.5f, C.Plan.Length + CricketMath::Gauss(Rng) * 3.2f * (DefaultSkill - Skill));
	C.Plan.Line = O.Line;
	C.ReleaseTiming = FMath::Clamp(CricketMath::Gauss(Rng) * (0.05f + 0.25f * (1.f - Bowler.Accuracy)), -0.8f, 0.8f);
	return C;
}

CricketAI::EBallClass CricketAI::ClassOf(const FBallRead& Seen, bool bSpin)
{
	if (Seen.PitchX > 7.f || Seen.HeightAtBat > 0.95f) return EBallClass::Short;
	if (Seen.HeightAtBat < 0.12f) return EBallClass::BlockHole; // at the base of the bat, pitched or not
	if (Seen.PitchX < 2.f) return EBallClass::FullToss;
	return Seen.PitchX < (bSpin ? 3.8f : 4.5f) ? EBallClass::Slot : EBallClass::Length;
}

void CricketAI::ShotValue(EBallClass Class, bool bSpin, EBatIntent Intent, float& Runs, float& Out)
{
	// Runs per ball (running included) and chance of dismissal for defend / ground / loft, per class.
	// This is the batter's experience, not a peek at the delivery: CRICKET26.AI.ShotKnowledge re-measures
	// it against the physics and fails if the ranking of strokes has drifted.
	struct FV { float Runs, Out; };
	static const FV Table[2][5][3] = {
		{ // pace
			{ { 0.23f, 0.050f }, { 1.96f, 0.031f }, { 3.88f, 0.046f } }, // full toss
			{ { 0.35f, 0.061f }, { 1.31f, 0.009f }, { 1.98f, 0.040f } }, // block hole
			{ { 0.60f, 0.028f }, { 1.80f, 0.014f }, { 3.70f, 0.047f } }, // slot
			{ { 0.58f, 0.043f }, { 1.64f, 0.027f }, { 3.61f, 0.064f } }, // length
			{ { 0.60f, 0.038f }, { 2.77f, 0.025f }, { 4.23f, 0.034f } }, // short
		},
		{ // spin
			{ { 0.02f, 0.021f }, { 1.89f, 0.009f }, { 2.95f, 0.111f } }, // full toss
			{ { 0.01f, 0.007f }, { 1.29f, 0.009f }, { 1.49f, 0.084f } }, // block hole
			{ { 0.06f, 0.078f }, { 1.19f, 0.035f }, { 1.85f, 0.163f } }, // slot
			{ { 0.08f, 0.080f }, { 1.30f, 0.058f }, { 2.29f, 0.180f } }, // length
			{ { 0.29f, 0.031f }, { 2.65f, 0.032f }, { 3.03f, 0.060f } }, // short
		},
	};
	const int32 I = FMath::Clamp(int32(Intent) - int32(EBatIntent::Defend), 0, 2);
	const FV& V = Table[bSpin ? 1 : 0][uint8(Class)][I];
	Runs = V.Runs;
	Out = V.Out;
}

float CricketAI::WicketCost(float Aggr)
{
	// Runs a wicket is worth giving up: plenty when preserving the last pair, almost none when
	// everything must be hit.
	return FMath::Lerp(6.f, 0.5f, FMath::Clamp(Aggr, 0.f, 1.f));
}

namespace
{
	/** Plays a chosen intent to the ball as read at DecideAt: picks the gap and times the swing. */
	FBatInput Play(EBatIntent Intent, bool bCharge, const FBallRead& First, float DecideAt, const FBallRead& Seen,
		const FDeliveryRelease& Rel, const FCricketPlayer& Batter, EBowlerType BowlerType, const TArray<FFielder>& Field,
		const FPitchConditions& C, FRandomStream& Rng, float Skill, TOptional<float> Direction = {})
	{
		FBatInput In;
		In.Intent = Intent;
		if (In.Intent == EBatIntent::Leave) return In;
		const float Off = OffSideSign(Batter.BatHand);
		const float Line = Seen.PitchLine * Off;
		const bool bShort = !bCharge && (Seen.PitchX > 7.f || Seen.HeightAtBat > 0.95f);

		// Play with the line: off side for balls outside off, leg side for straight/leg, then find the gap.
		float Lo = -60.f, Hi = 60.f;
		if (bShort) { Lo = Line > 0.2f ? 70.f : -130.f; Hi = Line > 0.2f ? 130.f : -40.f; }
		else if (bCharge) { Lo = -45.f; Hi = 45.f; }
		else if (Line > 0.3f) { Lo = 10.f; Hi = 80.f; }
		else if (Line < 0.f) { Lo = -130.f; Hi = -5.f; }
		float BestDir = 0.f, BestGap = -1.f;
		for (float D = Lo; D <= Hi; D += 5.f)
		{
			float Gap = 180.f;
			for (const FFielder& F : Field)
			{
				if (F.bKeeper || F.bBowler) continue;
				// A lofted shot clears the ring: only the fielders out deep can catch it or cut it off.
				if (Intent == EBatIntent::Loft && F.Home.Size() < 35.f) continue;
				Gap = FMath::Min(Gap, FMath::Abs(FMath::FindDeltaAngleDegrees(D, AngleOf(F, Off))));
			}
			Gap += FMath::Lerp(16.f, 0.f, Skill) * Rng.GetFraction(); // a weaker batter picks a gap less precisely
			if (Gap > BestGap) { BestGap = Gap; BestDir = D; }
		}
		In.DirectionDeg = In.Intent == EBatIntent::Defend ? 0.f : Direction.Get(BestDir);

		const FShotProfile Shot = CricketBatting::ChooseShot(In.Intent, In.DirectionDeg, Seen.PitchX, Seen.HeightAtBat, BowlerType,
			bCharge ? ChargeLead : 0.f);
		FBallState Probe = Rel.Ball;
		if (!CricketBall::SimulateToPlane(Probe, Shot.ContactX(), C)) { In.Intent = EBatIntent::Leave; return In; }
		const float Sigma = 0.015f + 0.035f * (1.f - Batter.Timing) + (Rel.SpeedKph > 138.f ? 0.01f : 0.f);
		In.PressTime = FMath::Max(DecideAt, Probe.Time - Shot.SwingTime + CricketMath::Gauss(Rng) * Sigma);
		// Committed to the charge: the feet go before AdvanceLead whatever the timing of the swing.
		if (bCharge) In.PressTime = FMath::Min(In.PressTime, First.ArrivalTime - CricketBatting::AdvanceLead - 0.02f);
		return In;
	}
}

FBatInput CricketAI::PlayIntent(EBatIntent Intent, const FDeliveryRelease& Rel, const FCricketPlayer& Batter, EBowlerType BowlerType,
	const TArray<FFielder>& Field, const FPitchConditions& C, FRandomStream& Rng, float Skill, TOptional<float> Direction)
{
	const FBallRead First = CricketDelivery::Read(Rel.Ball, 0.f, C);
	const float DecideAt = FMath::Max(0.05f, First.ArrivalTime - 0.35f);
	return Play(Intent, false, First, DecideAt, CricketDelivery::Read(Rel.Ball, DecideAt, C), Rel, Batter, BowlerType, Field, C, Rng, Skill, Direction);
}

FBatInput CricketAI::ChooseShot(const FDeliveryRelease& Rel, const FCricketPlayer& Batter, EBowlerType BowlerType,
	float Aggr, const TArray<FFielder>& Field, const FPitchConditions& C, FRandomStream& Rng, float Skill)
{
	const float Off = OffSideSign(Batter.BatHand);
	const bool bSpin = BowlerType != EBowlerType::Pace;
	// Against spin an attacking batter sometimes premeditates the charge: committed in the flight,
	// before the ball pitches, so length is guessed and a ball that dips or turns past them risks a stumping.
	const bool bCharge = bSpin && Rng.GetFraction() < FMath::Clamp(0.3f * (Aggr - 0.35f), 0.f, 0.15f);
	// Otherwise the batter decides roughly 0.35 s before the ball reaches them, on what they can see then.
	const FBallRead First = CricketDelivery::Read(Rel.Ball, 0.f, C);
	const float DecideAt = FMath::Max(0.05f, First.ArrivalTime - (bCharge ? ChargeLead : 0.35f));
	const FBallRead Seen = CricketDelivery::Read(Rel.Ball, DecideAt, C);
	const float Line = Seen.PitchLine * Off;

	// Weigh each stroke's runs against the chance of getting out at what a wicket costs right now.
	// Charging makes the slot. A weaker batter misjudges the values; a Legend all but never does.
	const EBallClass Class = bCharge ? EBallClass::Slot : ClassOf(Seen, bSpin);
	const float Cost = WicketCost(Aggr);
	const float Noise = FMath::Lerp(1.2f, 0.f, Skill);
	EBatIntent Intent = EBatIntent::Defend;
	float Best = -1e9f;
	for (const EBatIntent I : { EBatIntent::Defend, EBatIntent::Ground, EBatIntent::Loft })
	{
		float Runs, Out;
		ShotValue(Class, bSpin, I, Runs, Out);
		const float V = Runs - Cost * Out + Noise * CricketMath::Gauss(Rng);
		if (V > Best) { Best = V; Intent = I; }
	}
	if (bCharge && Intent == EBatIntent::Defend) Intent = EBatIntent::Ground;
	if (Line > 0.8f && Intent == EBatIntent::Defend && !bCharge) Intent = EBatIntent::Leave;
	return Play(Intent, bCharge, First, DecideAt, Seen, Rel, Batter, BowlerType, Field, C, Rng, Skill);
}
