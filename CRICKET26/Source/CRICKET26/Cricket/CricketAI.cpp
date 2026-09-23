#include "CricketAI.h"

namespace
{
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

float CricketAI::RunMargin(const FSuperOverMatch& M, float Aggression)
{
	if (M.IsChase() && M.BallsRemaining() == 1 && M.RunsRequired() <= 2) return -0.25f; // must run
	return FMath::Lerp(0.6f, 0.f, Aggression);
}

FBowlingChoice CricketAI::ChooseDelivery(const FCricketPlayer& Bowler, ECricketHand BatHand, const FSuperOverMatch& M,
	const TArray<int32>& Recent, FRandomStream& Rng)
{
	static const FPlanOption Pace[] = {
		{ TEXT("Yorker"), EDeliveryType::Stock, 1.1f, 0.05f, 3.f },
		{ TEXT("Wide yorker"), EDeliveryType::Stock, 1.4f, 0.65f, 2.f },
		{ TEXT("Slower ball"), EDeliveryType::Slower, 6.5f, 0.15f, 1.5f },
		{ TEXT("Bouncer"), EDeliveryType::Stock, 11.5f, 0.f, 1.f },
		{ TEXT("Hard length"), EDeliveryType::Cutter, 8.f, 0.2f, 1.f },
		{ TEXT("Full outswinger"), EDeliveryType::Outswing, 4.f, 0.25f, 1.f },
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
	};
	TArrayView<const FPlanOption> Options = Bowler.BowlerType == EBowlerType::Pace ? TArrayView<const FPlanOption>(Pace)
		: Bowler.BowlerType == EBowlerType::OffSpin ? TArrayView<const FPlanOption>(OffSpin) : TArrayView<const FPlanOption>(LegSpin);

	// Defending a small total late: squeeze with yorkers; otherwise mix it up.
	const bool bSqueeze = M.IsChase() && M.RunsRequired() <= M.BallsRemaining() * 1.5f;
	TArray<float> W;
	float Sum = 0.f;
	for (int32 I = 0; I < Options.Num(); ++I)
	{
		float Wt = Options[I].Weight * (bSqueeze && I == 0 ? 1.5f : 1.f);
		if (Recent.Num() > 0 && Recent.Last() == I) Wt *= 0.3f;
		if (Recent.Num() > 1 && Recent[Recent.Num() - 2] == I) Wt *= 0.6f;
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
	C.Plan.Line = O.Line;
	C.ReleaseTiming = FMath::Clamp(CricketMath::Gauss(Rng) * (0.05f + 0.25f * (1.f - Bowler.Accuracy)), -0.8f, 0.8f);
	return C;
}

FBatInput CricketAI::ChooseShot(const FDeliveryRelease& Rel, const FCricketPlayer& Batter, EBowlerType BowlerType,
	float Aggr, const TArray<FFielder>& Field, const FPitchConditions& C, FRandomStream& Rng)
{
	const float Off = OffSideSign(Batter.BatHand);
	// The batter decides roughly 0.35 s before the ball reaches them, on what they can see then.
	const FBallRead First = CricketDelivery::Read(Rel.Ball, 0.f, C);
	const float DecideAt = FMath::Max(0.05f, First.ArrivalTime - 0.35f);
	const FBallRead Seen = CricketDelivery::Read(Rel.Ball, DecideAt, C);
	const float Line = Seen.PitchLine * Off;
	const bool bShort = Seen.PitchX > 7.f || Seen.HeightAtBat > 0.95f;
	const bool bYorker = Seen.PitchX < 2.3f;
	Aggr = FMath::Clamp(Aggr + 0.1f * CricketMath::Gauss(Rng), 0.f, 1.f);

	FBatInput In;
	In.Intent = EBatIntent::Defend;
	if (Line > 0.8f && Aggr < 0.6f) In.Intent = EBatIntent::Leave;
	else if (bShort) In.Intent = Aggr > 0.5f ? EBatIntent::Loft : (Aggr > 0.3f ? EBatIntent::Ground : EBatIntent::Defend);
	else if (bYorker) In.Intent = Aggr > 0.75f ? EBatIntent::Loft : EBatIntent::Defend;
	else In.Intent = Aggr > 0.6f ? EBatIntent::Loft : (Aggr > 0.3f ? EBatIntent::Ground : EBatIntent::Defend);
	if (In.Intent == EBatIntent::Leave) return In;

	// Play with the line: off side for balls outside off, leg side for straight/leg, then find the gap.
	float Lo = -60.f, Hi = 60.f;
	if (bShort) { Lo = Line > 0.2f ? 70.f : -130.f; Hi = Line > 0.2f ? 130.f : -40.f; }
	else if (Line > 0.3f) { Lo = 10.f; Hi = 80.f; }
	else if (Line < 0.f) { Lo = -80.f; Hi = -5.f; }
	float BestDir = 0.f, BestGap = -1.f;
	for (float D = Lo; D <= Hi; D += 5.f)
	{
		float Gap = 180.f;
		for (const FFielder& F : Field)
		{
			if (F.bKeeper || F.bBowler) continue;
			Gap = FMath::Min(Gap, FMath::Abs(FMath::FindDeltaAngleDegrees(D, AngleOf(F, Off))));
		}
		Gap += 4.f * Rng.GetFraction();
		if (Gap > BestGap) { BestGap = Gap; BestDir = D; }
	}
	In.DirectionDeg = In.Intent == EBatIntent::Defend ? 0.f : BestDir;

	const FShotProfile Shot = CricketBatting::ChooseShot(In.Intent, In.DirectionDeg, Seen.PitchX, Seen.HeightAtBat, BowlerType);
	FBallState Probe = Rel.Ball;
	if (!CricketBall::SimulateToPlane(Probe, Shot.ContactX(), C)) { In.Intent = EBatIntent::Leave; return In; }
	const float Sigma = 0.015f + 0.035f * (1.f - Batter.Timing) + (Rel.SpeedKph > 138.f ? 0.01f : 0.f);
	In.PressTime = FMath::Max(DecideAt, Probe.Time - Shot.SwingTime + CricketMath::Gauss(Rng) * Sigma);
	return In;
}
