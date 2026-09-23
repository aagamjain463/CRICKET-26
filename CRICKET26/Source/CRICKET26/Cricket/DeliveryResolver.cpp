#include "DeliveryResolver.h"

using namespace CricketBall;

namespace
{
	constexpr float FrontPlane = 2.0f;

	FBallState AtPlane(const FBallState& Prev, const FBallState& Cur, float X)
	{
		FBallState S = Cur;
		const float F = (Prev.Pos.X - X) / FMath::Max(Prev.Pos.X - Cur.Pos.X, KINDA_SMALL_NUMBER);
		S.Pos = FMath::Lerp(Prev.Pos, Cur.Pos, F);
		S.Time = FMath::Lerp(Prev.Time, Cur.Time, F);
		return S;
	}

	bool HitsStumps(const FVector& P)
	{
		return FMath::Abs(P.Y) <= CricketGeo::StumpsHalfWidth + CricketGeo::BallRadius
			&& P.Z <= CricketGeo::StumpHeight + CricketGeo::BallRadius;
	}

	void Hold(TArray<FVector>& Path, FVector At, float Seconds) // by value: callers pass Path.Last()
	{
		for (int32 I = 0, N = FMath::CeilToInt(Seconds / FDeliveryResult::SampleDt); I < N; ++I) Path.Add(At);
	}
}

FString CricketDelivery::ZoneName(EContactZone Z)
{
	switch (Z)
	{
	case EContactZone::Middle: return TEXT("MIDDLED");
	case EContactZone::InnerHalf: return TEXT("INNER HALF");
	case EContactZone::OuterHalf: return TEXT("OUTER HALF");
	case EContactZone::Toe: return TEXT("OFF THE TOE");
	case EContactZone::Upper: return TEXT("SPLICE");
	case EContactZone::InsideEdge: return TEXT("INSIDE EDGE");
	case EContactZone::OutsideEdge: return TEXT("OUTSIDE EDGE");
	case EContactZone::TopEdge: return TEXT("TOP EDGE");
	case EContactZone::BottomEdge: return TEXT("BOTTOM EDGE");
	default: return TEXT("MISSED");
	}
}

FString CricketDelivery::ShotName(EShotType S)
{
	switch (S)
	{
	case EShotType::Defend: return TEXT("Defence");
	case EShotType::Drive: return TEXT("Drive");
	case EShotType::Loft: return TEXT("Lofted drive");
	case EShotType::Punch: return TEXT("Back-foot punch");
	case EShotType::Cut: return TEXT("Cut");
	case EShotType::Pull: return TEXT("Pull");
	case EShotType::Sweep: return TEXT("Sweep");
	default: return TEXT("Leave");
	}
}

FBallRead CricketDelivery::Read(const FBallState& Release, float AtTime, const FPitchConditions& C)
{
	FBallRead R;
	FBallState B = Release;
	bool bPitched = false;
	while (B.Time < AtTime && B.Pos.X > FrontPlane)
	{
		if (Step(B, C) == EStep::Bounce && !bPitched)
		{
			bPitched = true;
			R.PitchX = B.Pos.X;
			R.PitchLine = B.Pos.Y;
		}
	}
	FBallState P = B;
	P.Spin = FVector::ZeroVector;
	P.SwingAccel = 0.f;
	P.SeamKick = 0.f;
	FVector At;
	float T;
	if (PredictAtPlane(P, FrontPlane, C, At, T))
	{
		R.HeightAtBat = At.Z;
		R.ArrivalTime = T;
	}
	if (!bPitched)
	{
		while (P.Bounces == 0 && P.Time < 4.f && P.Pos.X > -2.f) Step(P, C);
		R.PitchX = P.Pos.X;
		R.PitchLine = P.Pos.Y;
	}
	return R;
}

FVector FDeliveryResult::BallAt(float Time) const
{
	if (BallPath.Num() == 0) return FVector::ZeroVector;
	const float F = FMath::Max(0.f, Time) / SampleDt;
	const int32 I = FMath::FloorToInt(F);
	if (I >= BallPath.Num() - 1) return BallPath.Last();
	return FMath::Lerp(BallPath[I], BallPath[I + 1], F - I);
}

FDeliveryOutcome FDeliveryResult::ToOutcome() const
{
	FDeliveryOutcome O;
	O.bBatContact = Contact.HasContact();
	O.bWide = bWide;
	O.bNoBall = bNoBall;
	O.Dismissal = Dismissal;
	O.bRunOutStriker = Running.bRunOutStriker;
	O.Boundary = Fielding.Boundary;
	O.RunsRun = Fielding.Boundary ? 0 : Running.Completed;
	return O;
}

FDeliveryResult CricketDelivery::Resolve(const FDeliveryRelease& Release, const FBatInput& Input, const FResolveContext& Ctx)
{
	const FPitchConditions& C = Ctx.Conditions;
	const FCricketPlayer& Bat = Ctx.Striker;
	const float Off = OffSideSign(Bat.BatHand);
	FRandomStream Rng(Ctx.Seed * 7919 + 13);

	FDeliveryResult R;
	R.SpeedKph = Release.SpeedKph;
	R.bNoBall = Release.bNoBall;
	R.Shot = CricketBatting::Profile(EShotType::Leave);

	// Shot choice uses the batter's read at the moment the player commits; bat placement uses the
	// read ReadLead seconds before the ball arrives at the chosen contact plane.
	FVector Aim = FVector::ZeroVector;
	float Timing = 0.f;
	if (Input.IsShot())
	{
		const FBallRead Seen = Read(Release.Ball, Input.PressTime, C);
		R.Shot = CricketBatting::ChooseShot(Input.Intent, Input.DirectionDeg, Seen.PitchX, Seen.HeightAtBat, Ctx.Bowler.BowlerType);
		FBallState Probe = Release.Ball;
		if (SimulateToPlane(Probe, R.Shot.ContactX(), C) && Input.PressTime <= Probe.Time)
		{
			Timing = Input.PressTime + R.Shot.SwingTime - Probe.Time;
			FBallState Early = Release.Ball;
			const float ReadAt = Probe.Time - CricketBatting::ReadLead(Bat);
			while (Early.Time < ReadAt) Step(Early, C);
			float Unused;
			if (!PredictAtPlane(Early, R.Shot.ContactX(), C, Aim, Unused)) Aim = Probe.Pos;
		}
		else
		{
			R.bTooLate = true;
			R.Shot = CricketBatting::Profile(EShotType::Leave);
		}
	}

	const bool bShot = R.Shot.Shot != EShotType::Leave;
	const bool bFront = bShot && R.Shot.Foot == EFootwork::Front;
	const float PadX = bFront ? 1.75f : 0.8f;
	const float PadMin = bFront ? -0.22f : -0.32f, PadMax = bFront ? 0.10f : 0.0f;
	const float InLine = CricketGeo::StumpsHalfWidth + CricketGeo::BallRadius;

	// Phase 1: release until bat, pad, stumps, or past the batter.
	FBallState B = Release.Ball;
	R.BallPath.Add(B.Pos);
	bool bContact = false, bWideLine = false, bDead = false;
	while (B.Time < 4.f)
	{
		const FBallState Prev = B;
		if (Step(B, C) == EStep::Bounce && R.PitchTime < 0.f)
		{
			R.PitchTime = B.Time;
			R.PitchPos = B.Pos;
		}
		auto Crossed = [&](float X) { return Prev.Pos.X > X && B.Pos.X <= X; };

		if (bShot && Crossed(R.Shot.ContactX()))
		{
			const FBallState AtBat = AtPlane(Prev, B, R.Shot.ContactX());
			R.Contact = CricketBatting::ResolveContact(AtBat, Aim, R.Shot, Input.DirectionDeg, Timing, Bat, Input.Intent == EBatIntent::Loft);
			if (R.Contact.HasContact())
			{
				bContact = true;
				B = AtBat;
				B.Vel = R.Contact.ExitVel;
				B.Spin = FVector::ZeroVector;
				B.SwingAccel = B.SeamKick = 0.f;
				B.Bounces = 0;
				B.bRolling = false;
				R.ContactTime = B.Time;
				break;
			}
		}
		if (Crossed(CricketGeo::PoppingCrease))
		{
			const float Lat = AtPlane(Prev, B, CricketGeo::PoppingCrease).Pos.Y * Off;
			bWideLine = Lat > CricketGeo::WideLineOff || Lat < -CricketGeo::WideLineLeg;
		}
		if (Crossed(PadX))
		{
			const FBallState S = AtPlane(Prev, B, PadX);
			const float Lat = S.Pos.Y * Off;
			if (Lat >= PadMin - CricketGeo::BallRadius && Lat <= PadMax + CricketGeo::BallRadius && S.Pos.Z < 0.6f)
			{
				R.bPadImpact = true;
				// Ball tracking: would it have gone on to hit the stumps?
				FBallState Track = S;
				const bool bWouldHit = SimulateToPlane(Track, 0.f, C, 1.f) && HitsStumps(Track.Pos);
				const bool bPitchedOutsideLeg = R.PitchTime >= 0.f && R.PitchPos.Y * Off < -InLine;
				const bool bImpactInLine = FMath::Abs(Lat) <= InLine;
				const bool bNoShotOutsideOff = !bShot && Lat > InLine;
				if (!bPitchedOutsideLeg && (bImpactInLine || bNoShotOutsideOff) && bWouldHit) R.Dismissal = EDismissal::LBW;
				B = S;
				B.Vel = FVector(1.2f, Rng.FRandRange(-1.5f, 1.5f), 0.8f);
				B.Spin = FVector::ZeroVector;
				B.SwingAccel = 0.f;
				B.bRolling = false;
				bDead = true;
				break;
			}
		}
		if (Crossed(0.f))
		{
			const FBallState S = AtPlane(Prev, B, 0.f);
			if (HitsStumps(S.Pos))
			{
				R.bStumpsHit = true;
				R.StumpsTime = S.Time;
				R.Dismissal = EDismissal::Bowled;
				B = S;
				B.Vel *= 0.35f;
				break;
			}
		}
		R.BallPath.Add(B.Pos);
		if (B.Pos.X < -0.5f) break;
	}
	R.bWide = bWideLine && !bContact && !R.bPadImpact && !R.bNoBall;

	// Phase 2: the ball after the bat / past the batter.
	const int32 PreCount = R.BallPath.Num();
	const float T0 = B.Time;
	TArray<FBallState> Post;
	Post.Add(B);
	R.BallPath.Add(B.Pos);
	while (B.Time - T0 < 12.f)
	{
		const FBallState Prev = B;
		const EStep Ev = Step(B, C);
		if (bContact && !R.bStumpsHit && Prev.Pos.X > 0.f && B.Pos.X <= 0.f && HitsStumps(AtPlane(Prev, B, 0.f).Pos))
		{
			R.bStumpsHit = true; // played on
			R.StumpsTime = B.Time;
			R.Dismissal = EDismissal::Bowled;
			B.Vel *= 0.3f;
		}
		Post.Add(B);
		R.BallPath.Add(B.Pos);
		if (Ev == EStep::Stopped || (bDead && B.Time - T0 > 1.5f)) break;
		if (FVector2D::Distance(FVector2D(B.Pos), FVector2D(CricketGeo::PitchCentre())) > CricketGeo::BoundaryRadius + 5.f) break;
	}

	const float Dt = FDeliveryResult::SampleDt;
	auto TruncateAt = [&](float RelTime)
	{
		const int32 K = FMath::RoundToInt(RelTime / Dt);
		if (PreCount + K < R.BallPath.Num()) R.BallPath.SetNum(PreCount + K + 1);
		else Hold(R.BallPath, R.BallPath.Last(), (PreCount + K - R.BallPath.Num() + 1) * Dt);
	};

	if (!bDead && R.Dismissal == EDismissal::None)
	{
		R.Fielding = CricketField::SolveFielding(Post, Dt, Ctx.Field, Ctx.Fielding, bContact, Rng);
		const FFieldingOutcome& F = R.Fielding;
		if (F.Boundary)
		{
			TruncateAt(F.BoundaryTime + 0.7f);
			R.DeadTime = T0 + F.BoundaryTime + 1.5f;
		}
		else if (F.Fielder >= 0)
		{
			TruncateAt(F.FieldTime);
			if (F.bCaught)
			{
				R.Dismissal = EDismissal::Caught;
				Hold(R.BallPath, F.FieldPos, 2.f);
				R.DeadTime = T0 + F.FieldTime + 2.f;
			}
			else if (bContact)
			{
				R.Running = CricketField::SolveRunning(F, Ctx.Striker, Ctx.NonStriker, Ctx.Fielding,
					Ctx.Field[F.Fielder].bKeeper, Ctx.RunMargin, Rng);
				const FVector Hand = FVector(F.FieldPos.X, F.FieldPos.Y, 1.4f);
				const FVector Stumps(R.Running.bThrowToStrikerEnd ? 0.f : CricketGeo::PitchLength, 0.f, 0.6f);
				Hold(R.BallPath, Hand, R.Running.ThrowRelease - F.FieldTime);
				const int32 N = FMath::Max(1, FMath::CeilToInt((R.Running.ThrowArrive - R.Running.ThrowRelease) / Dt));
				for (int32 I = 1; I <= N; ++I)
				{
					FVector P = FMath::Lerp(Hand, Stumps, float(I) / N);
					P.Z += 4.f * FMath::Min(8.f, FVector::Dist(Hand, Stumps) / 8.f) * (float(I) / N) * (1.f - float(I) / N);
					R.BallPath.Add(P);
				}
				Hold(R.BallPath, Stumps, 1.2f);
				if (R.Running.bRunOut) R.Dismissal = EDismissal::RunOut;
				const float LastRun = R.Running.RunTimes.Num() ? R.Running.RunTimes.Last() : 0.f;
				R.DeadTime = T0 + FMath::Max(R.Running.ThrowArrive, LastRun) + 1.2f;
			}
			else
			{
				Hold(R.BallPath, F.FieldPos, 1.f);
				R.DeadTime = T0 + F.FieldTime + 1.f;
			}
		}
	}
	if (R.DeadTime <= 0.f) R.DeadTime = T0 + 2.f;
	if (!bContact) R.ContactTime = T0;

	// Only a run out stands off a no-ball or a free hit.
	const bool bProtected = R.bNoBall || Ctx.bFreeHit;
	if (bProtected && R.Dismissal != EDismissal::None && R.Dismissal != EDismissal::RunOut) R.Dismissal = EDismissal::None;

	// Commentary-style summary for the HUD.
	FString What = bShot ? FString::Printf(TEXT("%s - %s"), *ShotName(R.Shot.Shot), *ZoneName(R.Contact.Zone))
		: (R.bTooLate ? TEXT("Too late on the shot") : TEXT("Left alone"));
	FString Result;
	switch (R.Dismissal)
	{
	case EDismissal::Bowled: Result = TEXT("BOWLED!"); break;
	case EDismissal::LBW: Result = TEXT("LBW!"); break;
	case EDismissal::Caught: Result = FString::Printf(TEXT("CAUGHT by %s!"), *Ctx.Field[R.Fielding.Fielder].Position); break;
	case EDismissal::RunOut: Result = TEXT("RUN OUT!"); break;
	default:
		if (R.Fielding.Boundary) Result = R.Fielding.Boundary == 6 ? TEXT("SIX!") : TEXT("FOUR!");
		else if (R.bWide) Result = TEXT("WIDE");
		else if (R.bPadImpact) Result = TEXT("Hit on the pad - not out");
		else if (R.Fielding.bCatchChance && !R.Fielding.bCaught) Result = TEXT("DROPPED!");
		else Result = R.Running.Completed > 0 ? FString::Printf(TEXT("%d run%s"), R.Running.Completed, R.Running.Completed > 1 ? TEXT("s") : TEXT(""))
			: TEXT("No run");
	}
	if (R.bNoBall) Result += TEXT("  (NO BALL)");
	R.Summary = What + TEXT("  >  ") + Result;
	return R;
}
