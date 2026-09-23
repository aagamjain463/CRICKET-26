#include "FieldingModel.h"

namespace
{
	constexpr float RunLength = 17.68f; // popping crease to popping crease
	constexpr float Accel = 6.f;

	// Batter-relative polar placement. Ring fielders are measured from the striker's stumps,
	// boundary riders from the pitch centre so they sit just inside the rope.
	FFielder Ring(const TCHAR* Name, float Deg, float Dist, float Off)
	{
		const float T = FMath::DegreesToRadians(Deg);
		return { Name, FVector2D(Dist * FMath::Cos(T), Off * Dist * FMath::Sin(T)) };
	}
	FFielder Deep(const TCHAR* Name, float Deg, float Dist, float Off)
	{
		FFielder F = Ring(Name, Deg, Dist, Off);
		F.Home.X += CricketGeo::PitchLength * 0.5f;
		return F;
	}
}

EFieldPreset CricketField::PresetFor(EBowlerType Type)
{
	return Type == EBowlerType::Pace ? EFieldPreset::PaceDeath : EFieldPreset::SpinDefensive;
}

TArray<FFielder> CricketField::Make(EFieldPreset Preset, ECricketHand BatHand, ECricketHand BowlHand)
{
	const float Off = OffSideSign(BatHand);
	const float Arm = BowlHand == ECricketHand::Right ? 1.f : -1.f;
	TArray<FFielder> F;
	// Super Over playing conditions: at most five fielders outside the 30-yard circle.
	if (Preset == EFieldPreset::PaceDeath)
	{
		F.Add({ TEXT("Wicketkeeper"), FVector2D(-16.f, 0.5f * Off), true });
		F.Add({ TEXT("Bowler"), FVector2D(15.f, 1.2f * Arm), false, true });
		F.Add(Deep(TEXT("Third man"), 140.f, 58.f, Off));
		F.Add(Deep(TEXT("Long off"), 12.f, 60.f, Off));
		F.Add(Deep(TEXT("Long on"), -12.f, 60.f, Off));
		F.Add(Deep(TEXT("Deep midwicket"), -58.f, 60.f, Off));
		F.Add(Deep(TEXT("Deep square leg"), -100.f, 58.f, Off));
		F.Add(Ring(TEXT("Point"), 95.f, 22.f, Off));
		F.Add(Ring(TEXT("Extra cover"), 48.f, 26.f, Off));
		F.Add(Ring(TEXT("Midwicket"), -50.f, 26.f, Off));
		F.Add(Ring(TEXT("Short fine leg"), -145.f, 22.f, Off));
	}
	else
	{
		F.Add({ TEXT("Wicketkeeper"), FVector2D(-0.8f, 0.3f * Off), true });
		F.Add({ TEXT("Bowler"), FVector2D(17.5f, 0.8f * Arm), false, true });
		F.Add(Deep(TEXT("Deep cover"), 58.f, 60.f, Off));
		F.Add(Deep(TEXT("Long off"), 12.f, 60.f, Off));
		F.Add(Deep(TEXT("Long on"), -12.f, 60.f, Off));
		F.Add(Deep(TEXT("Deep midwicket"), -58.f, 60.f, Off));
		F.Add(Deep(TEXT("Deep square leg"), -100.f, 58.f, Off));
		F.Add(Ring(TEXT("Point"), 95.f, 22.f, Off));
		F.Add(Ring(TEXT("Mid off"), 20.f, 24.f, Off));
		F.Add(Ring(TEXT("Midwicket"), -55.f, 24.f, Off));
		F.Add(Ring(TEXT("Short fine leg"), -150.f, 22.f, Off));
	}
	return F;
}

float CricketField::TimeToCover(float Dist, float Top)
{
	const float AccelDist = Top * Top / (2.f * Accel);
	return Dist < AccelDist ? FMath::Sqrt(2.f * Dist / Accel) : Dist / Top + Top / (2.f * Accel);
}

FFieldingOutcome CricketField::SolveFielding(const TArray<FBallState>& Samples, float Dt, const TArray<FFielder>& Field,
	const FCricketPlayer& Skill, bool bContact, FRandomStream& Rng)
{
	FFieldingOutcome O;
	if (Samples.Num() == 0) return O;
	const FVector2D Centre(CricketGeo::PitchLength * 0.5f, 0.f);
	const int32 Extra = FMath::CeilToInt(10.f / Dt); // a stopped ball still has to be picked up
	for (int32 K = 0; K < Samples.Num() + Extra; ++K)
	{
		const FBallState& S = Samples[FMath::Min(K, Samples.Num() - 1)];
		const float T = K * Dt;
		const FVector2D P(S.Pos.X, S.Pos.Y);
		if (FVector2D::Distance(P, Centre) >= CricketGeo::BoundaryRadius)
		{
			O.Boundary = (bContact && S.Bounces == 0) ? 6 : 4;
			O.BoundaryTime = T;
			return O;
		}
		if (T < 0.06f) continue;

		const bool bAir = bContact && S.Bounces == 0;
		int32 Best = -1;
		float BestSlack = -1.f;
		bool bBestDive = false;
		for (int32 I = 0; I < Field.Num(); ++I)
		{
			const FFielder& F = Field[I];
			const float MaxZ = bAir ? (F.bKeeper ? 2.2f : 2.5f) : (!bContact && F.bKeeper ? 2.2f : 0.9f);
			if (S.Pos.Z > MaxZ || (bAir && S.Pos.Z < 0.05f)) continue;
			const float Reaction = F.bKeeper ? 0.15f : 0.25f;
			const float Reach = F.bKeeper ? 1.5f : 1.0f;
			const float DiveReach = F.bKeeper ? 2.8f : 2.3f;
			const float D = FVector2D::Distance(P, F.Home);
			const float Need = Reaction + TimeToCover(FMath::Max(0.f, D - DiveReach), Skill.RunSpeed);
			if (Need > T) continue;
			const float Slack = T - Need;
			if (Slack > BestSlack)
			{
				Best = I;
				BestSlack = Slack;
				bBestDive = Reaction + TimeToCover(FMath::Max(0.f, D - Reach), Skill.RunSpeed) > T;
			}
		}
		if (Best < 0) continue;

		O.Fielder = Best;
		O.ChaseStart = Field[Best].bKeeper ? 0.15f : 0.25f;
		O.FieldTime = T;
		O.FieldPos = S.Pos;
		if (bAir)
		{
			O.bCatchChance = true;
			const float Speed = S.Vel.Size();
			float Diff = 0.1f + 0.45f * FMath::Clamp((0.5f - BestSlack) / 0.5f, 0.f, 1.f)
				+ 0.3f * FMath::Clamp((Speed - 18.f) / 25.f, 0.f, 1.f) + (bBestDive ? 0.25f : 0.f);
			O.CatchDifficulty = FMath::Clamp(Diff, 0.f, 0.95f);
			const float Chance = FMath::Clamp(1.f - O.CatchDifficulty * (1.25f - Skill.Catching), 0.03f, 0.99f);
			O.bCaught = Rng.GetFraction() < Chance;
			if (!O.bCaught) O.FieldTime += 0.9f; // spilled: gathered again at the fielder's feet
		}
		return O;
	}
	return O;
}

FRunningOutcome CricketField::SolveRunning(const FFieldingOutcome& Fd, const FCricketPlayer& Striker, const FCricketPlayer& NonStriker,
	const FCricketPlayer& Skill, bool bKeeperFielded, float Margin, FRandomStream& Rng)
{
	FRunningOutcome R;
	if (Fd.bCaught || Fd.Boundary != 0 || Fd.Fielder < 0) return R;

	const float V = FMath::Max(3.f, FMath::Min(Striker.RunSpeed, NonStriker.RunSpeed));
	auto RunTime = [V](int32 N) { return 0.35f + TimeToCover(RunLength, V) + (N - 1) * (RunLength / V + 0.6f); };

	const FVector2D From(Fd.FieldPos.X, Fd.FieldPos.Y);
	const float ToStriker = From.Size();
	const float ToBowler = FVector2D::Distance(From, FVector2D(CricketGeo::PitchLength, 0.f));
	R.bThrowToStrikerEnd = ToStriker <= ToBowler;
	const float Dist = FMath::Min(ToStriker, ToBowler);
	const float Throw = FMath::Clamp(Skill.Throwing, 0.f, 1.f);
	R.ThrowRelease = Fd.FieldTime + (bKeeperFielded ? 0.25f : 0.5f - 0.2f * Throw);
	R.ThrowArrive = R.ThrowRelease + Dist / (24.f + 12.f * Throw);
	const float PDirect = Dist < 3.f ? 1.f : FMath::Clamp(0.15f + 0.35f * Throw - Dist / 150.f, 0.03f, 0.5f);
	const float Expected = R.ThrowArrive + 0.4f * (1.f - PDirect);

	while (R.Attempted < 4 && RunTime(R.Attempted + 1) + Margin <= Expected) R.Attempted++;
	R.bDirectHit = Rng.GetFraction() < PDirect;
	R.BreakTime = R.ThrowArrive + (R.bDirectHit ? 0.f : 0.4f);
	for (int32 N = 1; N <= R.Attempted; ++N) R.RunTimes.Add(RunTime(N));
	R.Completed = R.Attempted;
	if (R.Attempted > 0 && RunTime(R.Attempted) > R.BreakTime)
	{
		R.bRunOut = true;
		R.Completed = R.Attempted - 1;
		// On run N the original striker heads for the bowler's end when N is odd.
		const bool bStrikerHeadingToBowlerEnd = R.Attempted % 2 == 1;
		R.bRunOutStriker = R.bThrowToStrikerEnd != bStrikerHeadingToBowlerEnd;
	}
	return R;
}
