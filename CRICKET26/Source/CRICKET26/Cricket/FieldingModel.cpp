#include "FieldingModel.h"

namespace
{
	constexpr float RunLength = 17.68f; // popping crease to popping crease
	constexpr float Accel = 6.f;
	constexpr float BatReach = 1.2f;    // a runner grounds the bat this far ahead of their body
	constexpr float LegLength = RunLength - BatReach;
	constexpr float SetOff = 0.3f;      // leaving the crease after the stroke (s)
	constexpr float Turn = 0.6f;        // ground the bat, stop, push back off (s)
	// ponytail: one misjudgement spread for every pair; per-batter running judgement belongs with the attribute curves.
	constexpr float JudgeSigma = 0.3f;  // error (s) in the batters' read of how soon the ball is gathered

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
		F.Add(Deep(TEXT("Deep point"), 100.f, 58.f, Off));
		F.Add(Deep(TEXT("Long off"), 12.f, 60.f, Off));
		F.Add(Deep(TEXT("Long on"), -12.f, 60.f, Off));
		F.Add(Deep(TEXT("Deep midwicket"), -58.f, 60.f, Off));
		F.Add(Deep(TEXT("Deep square leg"), -100.f, 58.f, Off));
		F.Add(Ring(TEXT("Short third man"), 128.f, 24.f, Off));
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

float CricketField::DistanceCovered(float T, float Top)
{
	if (T <= 0.f) return 0.f;
	const float AccelTime = Top / Accel;
	return T < AccelTime ? 0.5f * Accel * T * T : Top * (T - 0.5f * AccelTime);
}

FVector2D CricketField::PositionOf(const FFielderMove& Move, const FFielder& Who, float Post, float Top)
{
	const FVector2D Path = Move.Target - Who.Home;
	const float Len = Path.Size();
	if (Len < KINDA_SMALL_NUMBER) return Who.Home;
	return Who.Home + Path * (FMath::Min(Len, DistanceCovered(Post - Move.Start, Top)) / Len);
}

namespace
{
	constexpr float CoverReaction = 0.3f; // keeper and bowler see where the ball is going, then head for the stumps

	/** Fielder (not keeper or bowler, not Skip) with the soonest arrival at P, and that arrival time. */
	int32 Nearest(const TArray<FFielder>& Field, const FVector2D& P, int32 Skip, float Top, float Reaction, float& OutTime)
	{
		int32 Best = -1;
		OutTime = BIG_NUMBER;
		for (int32 I = 0; I < Field.Num(); ++I)
		{
			if (I == Skip || Field[I].bKeeper || Field[I].bBowler) continue;
			const float T = Reaction + CricketField::TimeToCover(FVector2D::Distance(Field[I].Home, P), Top);
			if (T < OutTime) { OutTime = T; Best = I; }
		}
		return Best;
	}

	/**
	 * How a ground ball is taken. A ring fielder attacks a slow ball and picks up on the run; a deep fielder
	 * facing a hard-hit ball goes down in the long barrier; one chasing a ball toward the rope slides. Faster
	 * balls and less sure hands fumble more (the long barrier almost never does). Seen is how long the fielder
	 * has been able to watch the ball's final line (for the keeper, since it pitched or came off the bat).
	 */
	EFieldAction GroundAction(const FFielder& F, const FBallState& Ball, float Seen, float Ran, bool bDive,
		const FCricketPlayer& Skill, FRandomStream& Rng)
	{
		const float Hands = 1.2f - FMath::Clamp(Skill.Catching, 0.f, 1.f);
		const FVector2D P(Ball.Pos.X, Ball.Pos.Y);
		if (F.bKeeper)
		{
			// Hard takes: standing up, little time after the deviation, down the leg side (the batter blocks
			// the view), at the ankles or above the shoulders, or at full stretch.
			const bool bUp = F.Home.Size() < 3.f;
			const bool bLeg = P.Y * F.Home.Y < 0.f && FMath::Abs(P.Y) > 0.3f;
			const float Diff = 0.05f + (bUp ? 0.12f : 0.f) + 0.25f * FMath::Clamp((0.4f - Seen) / 0.4f, 0.f, 1.f)
				+ (bLeg ? 0.15f : 0.f) + (Ball.Pos.Z < 0.2f || Ball.Pos.Z > 1.5f ? 0.1f : 0.f) + (bDive ? 0.2f : 0.f);
			return Rng.GetFraction() < 0.5f * Diff * Hands ? EFieldAction::Fumble : EFieldAction::KeeperTake;
		}
		const FVector2D Centre(CricketGeo::PitchLength * 0.5f, 0.f);
		const FVector2D V(Ball.Vel.X, Ball.Vel.Y);
		const float Speed = V.Size();
		const bool bChasing = Ran > 3.f && FVector2D::DotProduct(V.GetSafeNormal(), (P - F.Home).GetSafeNormal()) > 0.5f;
		const bool bDeep = FVector2D::Distance(F.Home, Centre) > 45.f;
		EFieldAction A = EFieldAction::PickupClean;
		float Fumble = 0.03f + 0.004f * Speed;
		if (bChasing && Speed > 5.f && FVector2D::Distance(P, Centre) > CricketGeo::BoundaryRadius - 12.f) { A = EFieldAction::SlideStop; Fumble = 0.08f; }
		else if (bDive) { A = EFieldAction::DiveStop; Fumble = 0.2f; }
		else if (bDeep && !bChasing && Speed > 10.f) { A = EFieldAction::LongBarrier; Fumble = 0.01f; }
		else if (!bDeep && !bChasing && Speed < 15.f) { A = EFieldAction::PickupOnRun; Fumble = 0.05f + 0.006f * Speed; }
		return Rng.GetFraction() < Fumble * (Hands + 0.1f) ? EFieldAction::Fumble : A;
	}

	/** Seconds from reaching the ball to having it in hand, set to throw. */
	float GatherTime(const FFieldingOutcome& Fd, bool bKeeper, float Throw)
	{
		if (bKeeper) return Fd.Action == EFieldAction::Fumble ? 0.9f : 0.25f;
		switch (Fd.Action)
		{
		case EFieldAction::PickupOnRun: return 0.45f - 0.15f * Throw; // stride and set for an overarm throw
		case EFieldAction::LongBarrier: return 0.8f - 0.2f * Throw;
		case EFieldAction::SlideStop: return 1.f - 0.2f * Throw;  // back up off the ground
		case EFieldAction::Fumble: return 1.35f - 0.2f * Throw;    // chases the loose ball
		default: return 0.55f - 0.2f * Throw + (Fd.bDive ? 0.5f : 0.f);
		}
	}

	void Coordinate(FFieldingOutcome& O, const TArray<FBallState>& Samples, float Dt, const TArray<FFielder>& Field,
		const FCricketPlayer& Skill, bool bContact)
	{
		const float Top = Skill.RunSpeed;
		const FVector2D Centre(CricketGeo::PitchLength * 0.5f, 0.f);
		auto BallAt = [&](float T)
		{
			const FBallState& S = Samples[FMath::Clamp(FMath::RoundToInt(T / Dt), 0, Samples.Num() - 1)];
			FVector2D P(S.Pos.X, S.Pos.Y);
			const FVector2D R = P - Centre;
			if (R.Size() > CricketGeo::BoundaryRadius - 1.f) P = Centre + R.GetSafeNormal() * (CricketGeo::BoundaryRadius - 1.f);
			return P;
		};
		if (O.Boundary)
		{
			// Nobody can stop it, but the nearest rider still chases toward where it crosses the rope.
			float T;
			const int32 Chaser = Nearest(Field, BallAt(O.BoundaryTime), -1, Top, 0.4f, T);
			if (Chaser >= 0) O.Moves.Add({ Chaser, EFieldRole::Chase, 0.4f, BallAt(O.BoundaryTime) });
			return;
		}
		if (O.Fielder < 0) return;
		O.Moves.Add({ O.Fielder, EFieldRole::Primary, O.ChaseStart, FVector2D(O.FieldPos.X, O.FieldPos.Y) });
		if (!bContact || O.bCaught) return;

		// Backup: whoever gets soonest to where the ball goes if the primary misses it, a second on.
		if (!Field[O.Fielder].bKeeper)
		{
			float T;
			const FVector2D Behind = BallAt(O.FieldTime + 1.f);
			const int32 Backup = Nearest(Field, Behind, O.Fielder, Top, 0.4f, T);
			if (Backup >= 0) O.Moves.Add({ Backup, EFieldRole::Backup, 0.4f, Behind });
		}

		// Stumps: the keeper comes up to the striker's end, the bowler goes to the non-striker's. Whoever is
		// fielding the ball covers their own end only once they have it.
		const FVector2D Ends[2] = { FVector2D(-0.4f, 0.f), FVector2D(CricketGeo::PitchLength + 0.4f, 0.f) };
		for (int32 E = 0; E < 2; ++E)
		{
			const int32 Who = Field.IndexOfByPredicate([E](const FFielder& F) { return E == 0 ? F.bKeeper : F.bBowler; });
			if (Who < 0) { O.CoverTime[E] = BIG_NUMBER; continue; }
			const float Dist = FVector2D::Distance(Field[Who].Home, Ends[E]);
			if (Who == O.Fielder)
			{
				O.CoverTime[E] = O.FieldTime + CricketField::TimeToCover(FVector2D::Distance(FVector2D(O.FieldPos.X, O.FieldPos.Y), Ends[E]), Top);
				continue;
			}
			O.CoverTime[E] = Dist < 1.f ? 0.f : CoverReaction + CricketField::TimeToCover(Dist, Top);
			if (Dist >= 1.f) O.Moves.Add({ Who, EFieldRole::CoverStumps, CoverReaction, Ends[E] });
		}
	}
}

FFieldingOutcome CricketField::SolveFielding(const TArray<FBallState>& Samples, float Dt, const TArray<FFielder>& Field,
	const FCricketPlayer& Skill, bool bContact, FRandomStream& Rng, float KeeperLead)
{
	FFieldingOutcome O = Intercept(Samples, Dt, Field, Skill, bContact, Rng, KeeperLead);
	if (Samples.Num() > 0) Coordinate(O, Samples, Dt, Field, Skill, bContact);
	return O;
}

FFieldingOutcome CricketField::Intercept(const TArray<FBallState>& Samples, float Dt, const TArray<FFielder>& Field,
	const FCricketPlayer& Skill, bool bContact, FRandomStream& Rng, float KeeperLead)
{
	FFieldingOutcome O;
	if (Samples.Num() == 0) return O;
	const FVector2D Centre(CricketGeo::PitchLength * 0.5f, 0.f);
	const int32 Extra = FMath::CeilToInt(10.f / Dt); // a stopped ball still has to be picked up
	auto SampleAt = [&](int32 K) -> const FBallState& { return Samples[FMath::Min(K, Samples.Num() - 1)]; };
	auto Over = [&](const FBallState& S) { return FVector2D::Distance(FVector2D(S.Pos.X, S.Pos.Y), Centre) >= CricketGeo::BoundaryRadius; };
	struct FTake { int32 Who = -1; float Slack = -1.f, Run = 0.f; bool bDive = false; };
	// The fielder with the most time in hand to reach S at T, at full stretch if bDiveAllowed, else standing.
	auto TakeAt = [&](const FBallState& S, float T, bool bAir, bool bDiveAllowed)
	{
		FTake Take;
		const FVector2D P(S.Pos.X, S.Pos.Y);
		for (int32 I = 0; I < Field.Num(); ++I)
		{
			const FFielder& F = Field[I];
			const float MaxZ = bAir ? (F.bKeeper ? 2.2f : 2.5f) : (!bContact && F.bKeeper ? 2.2f : 0.9f);
			if (S.Pos.Z > MaxZ || (bAir && S.Pos.Z < 0.05f)) continue;
			// Judging a ball in the air off the bat takes longer than reacting to one along the ground.
			const float Reaction = F.bKeeper ? 0.15f : bAir ? 0.5f : 0.25f;
			const float Reach = F.bKeeper ? 1.5f : 1.0f;
			const float DiveReach = F.bKeeper ? 2.8f : 2.3f;
			const float D = FVector2D::Distance(P, F.Home);
			const float Lead = F.bKeeper ? KeeperLead : 0.f;
			const float Need = Reaction + TimeToCover(FMath::Max(0.f, D - (bDiveAllowed ? DiveReach : Reach)), Skill.RunSpeed) - Lead;
			if (Need > T || T - Need <= Take.Slack) continue;
			Take.Who = I;
			Take.Slack = T - Need;
			Take.bDive = Reaction + TimeToCover(FMath::Max(0.f, D - Reach), Skill.RunSpeed) - Lead > T;
			Take.Run = FMath::Max(0.f, D - Reach);
		}
		return Take;
	};
	for (int32 K = 0; K < Samples.Num() + Extra; ++K)
	{
		const FBallState* SP = &SampleAt(K);
		float T = K * Dt;
		if (Over(*SP))
		{
			O.Boundary = (bContact && SP->Bounces == 0) ? 6 : 4;
			O.BoundaryTime = T;
			return O;
		}
		if (T < 0.06f) continue;

		const bool bAir = bContact && SP->Bounces == 0;
		FTake Take = TakeAt(*SP, T, bAir, true);
		if (Take.Who < 0) continue;
		if (Take.bDive)
		{
			// Only reachable at full stretch this soon. A fielder who can instead get in line a little later takes it
			// on their feet: along the ground if that beats diving and getting up (0.5 s), in the air while it is still up.
			for (int32 K2 = K + 1; K2 < Samples.Num() + Extra && (K2 - K) * Dt <= (bAir ? 1.5f : 0.5f); ++K2)
			{
				const FBallState& S2 = SampleAt(K2);
				if (Over(S2) || (bContact && S2.Bounces == 0) != bAir) break;
				const FTake Standing = TakeAt(S2, K2 * Dt, bAir, false);
				if (Standing.Who >= 0) { Take = Standing; SP = &S2; T = K2 * Dt; break; }
			}
		}
		const FBallState& S = *SP;
		const FVector2D P(S.Pos.X, S.Pos.Y);
		const int32 Best = Take.Who;
		const float BestSlack = Take.Slack, BestRun = Take.Run;
		const bool bBestDive = Take.bDive;

		O.Fielder = Best;
		O.ChaseStart = Field[Best].bKeeper ? 0.15f : bAir ? 0.5f : 0.25f;
		O.FieldTime = T;
		O.FieldPos = S.Pos;
		O.FielderFrom = Field[Best].Home;
		O.bDive = bBestDive;
		if (!bAir)
		{
			// A beaten ball has been in view since it pitched; an edge since it came off the bat.
			O.Action = GroundAction(Field[Best], S, T + (Field[Best].bKeeper ? KeeperLead : 0.f), BestRun, bBestDive, Skill, Rng);
			return O;
		}
		O.bCatchChance = true;
		const float Speed = S.Vel.Size();
		const FVector2D Out = (P - Centre).GetSafeNormal();
		const float ToRope = CricketGeo::BoundaryRadius - FVector2D::Distance(P, Centre);
		// Running back toward the rope to take it. Pulling up from running pace takes v^2 / 2a metres: if the
		// rope is closer than that the catcher's momentum carries them over.
		const bool bOutward = BestRun > 3.f && FVector2D::DotProduct(P - Field[Best].Home, Out) > 0.f;
		const float Pace = FMath::Min(Skill.RunSpeed, FMath::Sqrt(2.f * Accel * BestRun));
		const bool bAtRope = bOutward && ToRope < 5.f;
		const bool bCarried = bOutward && ToRope < Pace * Pace / (2.f * Accel);
		O.Action = Field[Best].bKeeper ? EFieldAction::CatchKeeper : bBestDive ? EFieldAction::CatchDiving
			: bAtRope ? EFieldAction::CatchBoundary : S.Pos.Z < 0.5f ? EFieldAction::CatchLow
			: S.Vel.Z < -0.6f * Speed ? EFieldAction::CatchHigh : EFieldAction::CatchFlat;
		float Diff = 0.1f + 0.45f * FMath::Clamp((0.5f - BestSlack) / 0.5f, 0.f, 1.f)
			+ 0.3f * FMath::Clamp((Speed - 18.f) / 25.f, 0.f, 1.f) + (bBestDive ? 0.25f : 0.f)
			+ 0.2f * FMath::Clamp((BestRun - 10.f) / 15.f, 0.f, 1.f) // taken on the run
			+ (O.Action == EFieldAction::CatchLow ? 0.1f : O.Action == EFieldAction::CatchHigh ? 0.05f : 0.f);
		O.CatchDifficulty = FMath::Clamp(Diff, 0.f, 0.95f);
		const float Catching = FMath::Clamp(Skill.Catching, 0.f, 1.f);
		const float Chance = FMath::Clamp(1.f - O.CatchDifficulty * (1.25f - Catching), 0.03f, 0.99f);
		O.bCaught = Rng.GetFraction() < Chance;
		if (O.bCaught && bCarried)
		{
			// Carried over: flick it back up before stepping on the rope, to a team-mate close enough to take it,
			// or back to themselves once they are inside again. Fail and it is six.
			float PartnerTime;
			const int32 Partner = Nearest(Field, P - 4.f * Out, Best, Skill.RunSpeed, 0.5f, PartnerTime);
			const bool bPartner = Partner >= 0 && PartnerTime < T + 1.f;
			O.bCaught = Rng.GetFraction() < (bPartner ? 0.7f : 0.35f) + 0.25f * Catching;
			if (bPartner) O.CatchPartner = Partner;
			if (O.bCaught) O.Action = EFieldAction::CatchRelay;
			else { O.Boundary = 6; O.BoundaryTime = T + 0.4f; }
		}
		if (!O.bCaught && !O.Boundary) O.FieldTime += 0.9f; // spilled: gathered again at the fielder's feet
		return O;
	}
	return O;
}

FRunningOutcome CricketField::SolveRunning(const FFieldingOutcome& Fd, const FCricketPlayer& Striker, const FCricketPlayer& NonStriker,
	const FCricketPlayer& Skill, bool bKeeperFielded, float Margin, FRandomStream& Rng, const TArray<FFielder>* Field)
{
	FRunningOutcome R;
	if (Fd.bCaught || Fd.Boundary != 0 || Fd.Fielder < 0) return R;

	const float V = FMath::Max(3.f, FMath::Min(Striker.RunSpeed, NonStriker.RunSpeed));
	// Set off after the stroke, then each turn: ground the bat, stop, push back off.
	auto RunTime = [V](int32 N) { return N <= 0 ? 0.f : SetOff + TimeToCover(LegLength, V) + (N - 1) * (LegLength / V + Turn); };
	auto Leave = [&RunTime](int32 N) { return N == 1 ? SetOff : RunTime(N - 1) + 0.5f * Turn; };

	const FVector2D From(Fd.FieldPos.X, Fd.FieldPos.Y);
	const float Throw = FMath::Clamp(Skill.Throwing, 0.f, 1.f);
	// Gathered and set to throw: how soon depends on how the ball was taken.
	const float Gathered = Fd.FieldTime + GatherTime(Fd, bKeeperFielded, Throw);
	auto PDirectAt = [Throw](float Dist) { return Dist < 3.f ? 1.f : FMath::Clamp(0.15f + 0.35f * Throw - Dist / 150.f, 0.03f, 0.5f); };
	// Throw at the end where the stumps can be broken soonest: the throw has to arrive and, unless it hits,
	// someone has to be there to take it. Throwing back across the way they ran costs a turn.
	struct FEnd
	{
		EThrowType Type = EThrowType::Overarm;
		float Release = 0.f, Arrive = 0.f, PDirect = 0.f, Expected = 0.f, RelayCatch = 0.f, RelayRelease = 0.f;
		FFielderMove Relay;
	};
	auto AtEnd = [&](int32 E)
	{
		const FVector2D Stumps(E == 0 ? 0.f : CricketGeo::PitchLength, 0.f);
		const FVector2D ThrowDir = (Stumps - From).GetSafeNormal(), RanDir = (From - Fd.FielderFrom).GetSafeNormal();
		const float TurnCost = FVector2D::Distance(From, Fd.FielderFrom) > 2.f ? 0.3f * 0.5f * (1.f - FVector2D::DotProduct(ThrowDir, RanDir)) : 0.f;
		const float Dist = FVector2D::Distance(From, Stumps);
		// A missed direct hit is gathered by whoever is covering, once they are there.
		auto Score = [&](FEnd& End) { End.Expected = End.PDirect * End.Arrive + (1.f - End.PDirect) * (FMath::Max(End.Arrive, Fd.CoverTime[E]) + 0.4f); };
		FEnd Best;
		Best.Release = Gathered + (bKeeperFielded ? 0.f : TurnCost);
		Best.Arrive = Best.Release + ThrowFlight(Dist, Throw);
		Best.PDirect = PDirectAt(Dist);
		Score(Best);
		if (Fd.Action == EFieldAction::PickupOnRun && Dist <= 12.f)
		{
			// Flicked underarm in the same movement as the pickup: no set and no turn, slower but accurate close in.
			FEnd Under = Best;
			Under.Type = EThrowType::Underarm;
			Under.Release = Fd.FieldTime + 0.1f;
			Under.Arrive = Under.Release + Dist / 16.f;
			Under.PDirect = FMath::Clamp(0.45f + 0.3f * Throw - Dist / 25.f, 0.05f, 0.7f);
			Score(Under);
			if (Under.Expected < Best.Expected) Best = Under;
		}
		if (Field && Dist > 45.f)
		{
			// From the deep: a free fielder runs to the throw's line a little past halfway and relays it on.
			const FVector2D At = FMath::Lerp(Stumps, From, 0.45f);
			int32 Who = -1;
			float There = BIG_NUMBER;
			for (int32 I = 0; I < Field->Num(); ++I)
			{
				const FFielder& F = (*Field)[I];
				if (F.bKeeper || F.bBowler || I == Fd.Fielder || Fd.Moves.ContainsByPredicate([I](const FFielderMove& M) { return M.Fielder == I; })) continue;
				const float Time = CoverReaction + TimeToCover(FVector2D::Distance(F.Home, At), Skill.RunSpeed);
				if (Time < There) { There = Time; Who = I; }
			}
			if (Who >= 0)
			{
				FEnd Relay = Best;
				Relay.Type = EThrowType::Relay;
				Relay.Release = Gathered + TurnCost;
				Relay.RelayCatch = FMath::Max(Relay.Release + ThrowFlight(FVector2D::Distance(From, At), Throw), There);
				Relay.RelayRelease = Relay.RelayCatch + 0.3f;
				Relay.Arrive = Relay.RelayRelease + ThrowFlight(FVector2D::Distance(At, Stumps), Throw);
				Relay.PDirect = PDirectAt(FVector2D::Distance(At, Stumps));
				Relay.Relay = { Who, EFieldRole::Relay, CoverReaction, At };
				Score(Relay);
				if (Relay.Expected < Best.Expected) Best = Relay;
			}
		}
		return Best;
	};
	const FEnd Ends[2] = { AtEnd(0), AtEnd(1) };
	R.bThrowToStrikerEnd = Ends[0].Expected <= Ends[1].Expected;
	const FEnd& To = Ends[R.bThrowToStrikerEnd ? 0 : 1];
	R.ThrowType = To.Type;
	R.ThrowRelease = To.Release;
	R.ThrowArrive = To.Arrive;
	R.RelayMove = To.Relay;
	R.RelayCatch = To.RelayCatch;
	R.RelayRelease = To.RelayRelease;
	const float Expected = To.Expected;

	R.bDirectHit = Rng.GetFraction() < To.PDirect;
	R.BreakTime = R.bDirectHit ? R.ThrowArrive : FMath::Max(R.ThrowArrive, Fd.CoverTime[R.bThrowToStrikerEnd ? 0 : 1]) + 0.4f;

	// What the batters believe at time Now: the truth once the ball is in hand, else their read of it.
	const float Misjudge = CricketMath::Gauss(Rng) * JudgeSigma;
	auto Believed = [&](float Now)
	{
		return Now >= Fd.FieldTime ? Expected : FMath::Max(Fd.FieldTime + Misjudge, Now) + (Expected - Fd.FieldTime);
	};

	for (int32 N = 1; N <= 4; ++N)
	{
		const float CallAt = RunTime(N - 1); // at the stroke, then at each turn
		if (RunTime(N) + Margin > Believed(CallAt)) break;
		R.Attempted = N;
		// The ball is gathered with this run on and the run is lost: "No! Get back!" - if not yet halfway.
		const float Along = (Fd.FieldTime - Leave(N)) / (RunTime(N) - Leave(N));
		if (Fd.FieldTime > CallAt && Along < 0.5f && RunTime(N) + 0.5f * Margin > Expected)
		{
			R.bSentBack = true;
			R.SentBackAt = FMath::Max(Fd.FieldTime, Leave(N));
			R.SentBackFrom = FMath::Max(0.f, Along);
			R.BackIn = R.SentBackAt + 0.5f * Turn + R.SentBackFrom * LegLength / V;
			break;
		}
		R.RunTimes.Add(RunTime(N));
	}
	R.Completed = R.RunTimes.Num();
	const float Home = R.bSentBack ? R.BackIn : R.RunTimes.Num() ? R.RunTimes.Last() : 0.f;
	if (R.Attempted > 0 && Home > R.BreakTime)
	{
		R.bRunOut = true;
		R.Completed = R.Attempted - 1;
		// On run N the original striker heads for the bowler's end when N is odd; sent back, the other way.
		const bool bStrikerHeadingToBowlerEnd = (R.Attempted % 2 == 1) != R.bSentBack;
		R.bRunOutStriker = R.bThrowToStrikerEnd != bStrikerHeadingToBowlerEnd;
	}
	return R;
}

float CricketField::ThrowFlight(float Dist, float Throwing)
{
	// Flat out to about 35 m; beyond that the throw has to be put up and comes in slower.
	return Dist / (24.f + 12.f * FMath::Clamp(Throwing, 0.f, 1.f)) + FMath::Square(FMath::Max(0.f, Dist - 35.f)) / 1500.f;
}

const TCHAR* CricketField::ActionName(EFieldAction Action)
{
	switch (Action)
	{
	case EFieldAction::CatchFlat: return TEXT("flat catch");
	case EFieldAction::CatchHigh: return TEXT("skier held");
	case EFieldAction::CatchLow: return TEXT("low catch");
	case EFieldAction::CatchDiving: return TEXT("diving catch");
	case EFieldAction::CatchKeeper: return TEXT("keeper's catch");
	case EFieldAction::CatchBoundary: return TEXT("catch on the rope");
	case EFieldAction::CatchRelay: return TEXT("relay catch at the rope");
	case EFieldAction::KeeperTake: return TEXT("keeper takes");
	case EFieldAction::PickupClean: return TEXT("clean pickup");
	case EFieldAction::PickupOnRun: return TEXT("pickup on the run");
	case EFieldAction::LongBarrier: return TEXT("long barrier");
	case EFieldAction::SlideStop: return TEXT("sliding stop");
	case EFieldAction::DiveStop: return TEXT("diving stop");
	case EFieldAction::Fumble: return TEXT("misfield");
	default: return TEXT("");
	}
}
