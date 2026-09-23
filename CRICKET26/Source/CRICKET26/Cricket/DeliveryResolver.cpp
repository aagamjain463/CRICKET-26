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
	case EShotType::Flick: return TEXT("Flick");
	case EShotType::Hook: return TEXT("Hook");
	case EShotType::SlogSweep: return TEXT("Slog sweep");
	case EShotType::ReverseSweep: return TEXT("Reverse sweep");
	case EShotType::Scoop: return TEXT("Scoop");
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
	R.bPitched = bPitched;
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
	O.bBouncer = bBouncer;
	O.bLegBye = bPadImpact && !O.bBatContact;
	O.Dismissal = Dismissal;
	O.bRunOutStriker = Running.bRunOutStriker;
	O.Boundary = bRunsAllowed ? Fielding.Boundary : 0;
	O.RunsRun = bRunsAllowed && !Fielding.Boundary ? Running.Completed : 0;
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

	// The umpire judges height where the ball passes, or would have passed, the striker standing
	// upright at the popping crease - independent of what the batter does with it.
	bool bOverHeadWide = false;
	FBallState Ghost = Release.Ball;
	if (SimulateToPlane(Ghost, CricketGeo::PoppingCrease, C))
	{
		if (Ghost.Bounces == 0 && Ghost.Pos.Z > CricketGeo::WaistHeight)
		{
			R.bBeamer = true;
			R.bNoBall = true;
		}
		else if (Ghost.Bounces > 0 && Ghost.Pos.Z > CricketGeo::ShoulderHeight)
		{
			R.bBouncer = true;
			const bool bOverHead = Ghost.Pos.Z > CricketGeo::HeadHeight;
			if (bOverHead && Ctx.Rules.bOverHeadIsWide) bOverHeadWide = true;
			else if (bOverHead || Ctx.BouncersBowled >= Ctx.Rules.MaxBouncersPerOver) R.bNoBall = true;
		}
	}

	// Shot choice uses the batter's read at the moment the player commits; bat placement uses the
	// read ReadLead seconds before the ball arrives at the chosen contact plane.
	FVector Aim = FVector::ZeroVector;
	float Timing = 0.f, FaceDir = Input.DirectionDeg;
	if (Input.IsShot())
	{
		const FBallRead Seen = Read(Release.Ball, Input.PressTime, C);
		const float Lead = Seen.bPitched ? 0.f : Seen.ArrivalTime - Input.PressTime;
		R.Shot = CricketBatting::ChooseShot(Input.Intent, Input.DirectionDeg, Seen.PitchX, Seen.HeightAtBat, Ctx.Bowler.BowlerType, Lead);
		FBallState Probe = Release.Ball;
		if (SimulateToPlane(Probe, R.Shot.ContactX(), C) && Input.PressTime <= Probe.Time)
		{
			Timing = Input.PressTime + R.Shot.SwingTime - Probe.Time;
			FBallState Early = Release.Ball;
			const float ReadAt = Probe.Time - CricketBatting::ReadLead(Bat);
			while (Early.Time < ReadAt) Step(Early, C);
			float Unused;
			if (!PredictAtPlane(Early, R.Shot.ContactX(), C, Aim, Unused)) Aim = Probe.Pos;
			// The read leaves out movement still to come, except that batters partly play for a spinner's
			// stock turn (it varies ball to ball); the variations (arm ball, googly, top-spinner) deceive.
			FBallState Expect = Early;
			Expect.SwingAccel = Expect.SeamKick = 0.f;
			if (Ctx.Bowler.BowlerType != EBowlerType::Pace && Release.Type == CricketBowling::Repertoire(Ctx.Bowler.BowlerType)[0]
				&& SimulateToPlane(Expect, R.Shot.ContactX(), C))
			{
				Aim = FMath::Lerp(Aim, Expect.Pos, 0.5f * FMath::Clamp(Bat.Technique, 0.f, 1.f));
			}
			// Judgement and hand-eye error in placing the bat: grows with pace and with the size of the
			// swing, shrinks with technique. This is what separates middled shots from mistimed ones.
			const float Swing = Input.Intent == EBatIntent::Defend ? 0.6f : Input.Intent == EBatIntent::Loft ? 1.25f : 1.f;
			const float Sigma = (0.012f + 0.03f * (1.f - FMath::Clamp(Bat.Technique, 0.f, 1.f))) * Swing * (Probe.Vel.Size() / 33.f);
			Aim.Y += CricketMath::Gauss(Rng) * Sigma;
			Aim.Z += CricketMath::Gauss(Rng) * Sigma;
			FaceDir += CricketMath::Gauss(Rng) * (4.f + 12.f * (1.f - FMath::Clamp(Bat.Technique, 0.f, 1.f))) * Swing;
		}
		else
		{
			R.bTooLate = true;
			R.Shot = CricketBatting::Profile(EShotType::Leave);
		}
	}

	const bool bShot = R.Shot.Shot != EShotType::Leave;
	const EFootwork Foot = bShot ? R.Shot.Foot : EFootwork::Back;
	const float PadX = Foot == EFootwork::Advance ? R.Shot.AdvanceX - 0.25f : Foot == EFootwork::Front ? 1.75f : 0.8f;
	const float PadMin = Foot == EFootwork::Back ? -0.32f : -0.22f, PadMax = Foot == EFootwork::Back ? 0.0f : 0.10f;
	const float InLine = CricketGeo::StumpsHalfWidth + CricketGeo::BallRadius;

	// Phase 1: release until bat, pad, stumps, or past the batter.
	FBallState B = Release.Ball;
	R.BallPath.Add(B.Pos);
	bool bContact = false, bWideLine = false, bDead = false;
	float PassedBat = -1.f, HeightAtBat = 0.f; // when/how high the ball crossed the bat's plane
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
			PassedBat = AtBat.Time;
			HeightAtBat = AtBat.Pos.Z;
			R.Contact = CricketBatting::ResolveContact(AtBat, Aim, R.Shot, FaceDir, Timing, Bat, Input.Intent == EBatIntent::Loft);
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

				// The pads are rounded: a ball striking off-centre glances away toward that side, one
				// struck square-on drops dead in front. Pads absorb most of the impact.
				const float HalfPad = 0.5f * (PadMax - PadMin) + CricketGeo::BallRadius;
				const float Across = FMath::Clamp((Lat - 0.5f * (PadMin + PadMax)) / HalfPad, -0.9f, 0.9f);
				const FVector N(FMath::Sqrt(1.f - Across * Across), Across * Off, 0.f);
				const float Vn = FVector::DotProduct(S.Vel, N);
				B = S;
				B.Vel = 0.5f * (S.Vel - Vn * N) - 0.15f * Vn * N;
				B.Spin = FVector::ZeroVector;
				B.SwingAccel = B.SeamKick = 0.f;
				B.bRolling = false;
				// Leg byes only when the batter offered a stroke (Law 23.2).
				R.bRunsAllowed = bShot;
				bDead = R.Dismissal == EDismissal::LBW;
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
	R.bWide = (bWideLine || bOverHeadWide) && !bContact && !R.bPadImpact && !R.bNoBall;
	// Struck, an over-head ball is not a wide (Law 22.4) - it is a bouncer, a no-ball once over the limit.
	if (bOverHeadWide && !R.bWide && Ctx.BouncersBowled >= Ctx.Rules.MaxBouncersPerOver) R.bNoBall = true;

	// Hit wicket: rocking deep into the crease against a rising ball and getting there late, the batter
	// can tread on or swing into the stumps.
	// ponytail: one balance roll scaled by technique, not a foot/body simulation; replace once the
	// animation rig tracks the back foot against the stumps.
	if (bShot && R.Dismissal == EDismissal::None && R.Shot.Foot == EFootwork::Back && R.Shot.bCrossBat
		&& Timing > 0.06f && HeightAtBat > 1.2f && Rng.GetFraction() < 0.3f * (1.f - FMath::Clamp(Bat.Technique, 0.f, 1.f)))
	{
		R.Dismissal = EDismissal::HitWicket;
		bDead = true;
	}

	// Phase 2: the ball after the bat / pad / past the batter.
	const int32 PreCount = R.BallPath.Num();
	const float T0 = B.Time;
	TArray<FBallState> Post;
	Post.Add(B);
	R.BallPath.Add(B.Pos);
	while (B.Time - T0 < 12.f)
	{
		const FBallState Prev = B;
		const EStep Ev = Step(B, C);
		if ((bContact || R.bPadImpact) && !bDead && !R.bStumpsHit && Prev.Pos.X > 0.f && B.Pos.X <= 0.f && HitsStumps(AtPlane(Prev, B, 0.f).Pos))
		{
			R.bStumpsHit = true; // played on (off bat or pad)
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
		// A beaten ball: the keeper has been tracking it since it pitched (or since release for a full toss).
		const float KeeperLead = bContact ? 0.f : T0 - FMath::Max(R.PitchTime, 0.f);
		R.Fielding = CricketField::SolveFielding(Post, Dt, Ctx.Field, Ctx.Fielding, bContact, Rng, KeeperLead);
		const FFieldingOutcome& F = R.Fielding;
		const bool bKeeperUp = F.Fielder >= 0 && Ctx.Field[F.Fielder].bKeeper && Ctx.Field[F.Fielder].Home.Size() < 3.f;
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
			else if (bShot && Foot == EFootwork::Advance && !bContact && !R.bPadImpact && bKeeperUp && PassedBat >= 0.f)
			{
				// Beaten down the track: the keeper, standing up, races the striker back to the crease.
				// The striker has to get the bat grounded ~1 m behind where they met the ball.
				const float OutOfGround = R.Shot.ContactX() - CricketGeo::PoppingCrease - 1.f;
				const float Regain = PassedBat - T0 + 0.2f + CricketField::TimeToCover(OutOfGround, Bat.RunSpeed);
				const bool bCleanTake = Rng.GetFraction() < 0.75f + 0.25f * FMath::Clamp(Ctx.Fielding.Catching, 0.f, 1.f);
				const float Break = F.FieldTime + (bCleanTake ? 0.25f : 0.9f);
				if (Break < Regain) R.Dismissal = EDismissal::Stumped;
				Hold(R.BallPath, FVector(0.f, 0.f, 0.5f), 1.5f);
				R.DeadTime = T0 + Break + 1.5f;
			}
			else if (R.bRunsAllowed)
			{
				R.Running = CricketField::SolveRunning(F, Ctx.Striker, Ctx.NonStriker, Ctx.Fielding,
					Ctx.Field[F.Fielder].bKeeper, Ctx.RunMargin, Rng);
			}

			if (R.DeadTime > 0.f)
			{
				// Already decided above (catch or stumping).
			}
			else if (bContact || R.Running.Attempted > 0)
			{
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
				R.DeadTime = T0 + FMath::Max(R.Running.ThrowArrive, R.Running.EndTime()) + 1.2f;
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
	// Timing readout so the player can learn the window: perfect / good, else early or late by how much.
	const int32 Ms = FMath::RoundToInt(Timing * 1000.f);
	const FString TimingText = FMath::Abs(Ms) <= 15 ? FString(TEXT("perfect timing")) : FMath::Abs(Ms) <= 40 ? FString(TEXT("good timing"))
		: FString::Printf(TEXT("%d ms %s"), FMath::Abs(Ms), Ms < 0 ? TEXT("early") : TEXT("late"));
	FString What = bShot ? FString::Printf(TEXT("%s%s - %s, %s"), Foot == EFootwork::Advance ? TEXT("Down the track, ") : TEXT(""),
			*ShotName(R.Shot.Shot), *ZoneName(R.Contact.Zone), *TimingText)
		: (R.bTooLate ? TEXT("Too late on the shot") : TEXT("Left alone"));
	const FDeliveryOutcome O = R.ToOutcome();
	const TCHAR* Extra = O.bLegBye ? TEXT("leg bye") : TEXT("bye");
	FString Result;
	switch (R.Dismissal)
	{
	case EDismissal::Bowled: Result = TEXT("BOWLED!"); break;
	case EDismissal::LBW: Result = TEXT("LBW!"); break;
	case EDismissal::Caught: Result = Ctx.Field[R.Fielding.Fielder].bKeeper ? FString(TEXT("CAUGHT BEHIND!"))
		: FString::Printf(TEXT("CAUGHT by %s!"), *Ctx.Field[R.Fielding.Fielder].Position); break;
	case EDismissal::RunOut: Result = TEXT("RUN OUT!"); break;
	case EDismissal::Stumped: Result = TEXT("STUMPED!"); break;
	case EDismissal::HitWicket: Result = TEXT("HIT WICKET!"); break;
	default:
		if (O.Boundary && !O.bBatContact) Result = FString::Printf(TEXT("FOUR %sS"), *FString(Extra).ToUpper());
		else if (O.Boundary) Result = O.Boundary == 6 ? TEXT("SIX!") : TEXT("FOUR!");
		else if (R.bPadImpact && !R.bRunsAllowed) Result = TEXT("Off the pad, no stroke offered - no leg byes");
		else if (R.Fielding.bCatchChance && !R.Fielding.bCaught) Result = TEXT("DROPPED!");
		else if (O.RunsRun > 0 && !O.bBatContact) Result = FString::Printf(TEXT("%d %s%s"), O.RunsRun, Extra, O.RunsRun > 1 ? TEXT("s") : TEXT(""));
		else if (R.bPadImpact) Result = TEXT("Hit on the pad - not out");
		else if (O.RunsRun > 0) Result = FString::Printf(TEXT("%d run%s"), O.RunsRun, O.RunsRun > 1 ? TEXT("s") : TEXT(""));
		else Result = TEXT("No run");
		if (R.Running.bSentBack) Result += TEXT(" - sent back!");
	}
	if (R.bWide) Result += bOverHeadWide ? TEXT("  (WIDE - over head height)") : TEXT("  (WIDE)");
	if (R.bNoBall) Result += R.bBeamer ? TEXT("  (NO BALL - above waist)") : R.bBouncer && !Release.bNoBall ? TEXT("  (NO BALL - bouncer)") : TEXT("  (NO BALL)");
	R.Summary = What + TEXT("  >  ") + Result;
	return R;
}
