#include "BattingModel.h"

// File-local helpers live in the model's namespace, not an anonymous one: unity builds merge this file with
// others (CricketPose has its own HandleLength), and the definitions below find them by the enclosing namespace.
namespace CricketBatting
{
	constexpr float BladeHalfWidth = 0.054f;
	constexpr float ToeLength = 0.17f;    // sweet spot to toe
	constexpr float HandleLength = 0.45f; // sweet spot to the top of the blade / gloves
	constexpr float MissWindow = 0.12f;   // |timing| beyond which the bat is not in the ball's path
	constexpr float BladeSlip = 2.f;      // m along the blade per second of timing error: late finds the toe, early the splice
	constexpr float TimingWindow = 0.07f; // timing error (s) at which the swing has lost all its own pace

	FShotProfile Make(EShotType Shot, EFootwork Foot, bool bCross, float MinZ, float MaxZ, float RMin, float RMax,
		float Speed, float Loft, float DMin, float DMax, float Swing)
	{
		FShotProfile P;
		P.Shot = Shot; P.Foot = Foot; P.bCrossBat = bCross; P.MinZ = MinZ; P.MaxZ = MaxZ; P.ReachMin = RMin; P.ReachMax = RMax;
		P.BatSpeed = Speed; P.LoftDeg = Loft; P.DirMin = DMin; P.DirMax = DMax; P.SwingTime = Swing;
		P.bSoftHands = Shot == EShotType::Defend;
		return P;
	}

	/** A charging batter goes to meet the ball where they read it pitching, as far as their feet get them. */
	FShotProfile DownTheTrack(EShotType Shot, float ReadPitchX)
	{
		FShotProfile P = CricketBatting::Profile(Shot, EFootwork::Advance);
		P.AdvanceX = FMath::Clamp(ReadPitchX - 0.4f, 2.6f, 4.4f); // ~2.5 m out of the crease, bat ahead of the front pad
		return P;
	}

	FVector Dir3(float DirDeg, float LoftDeg, float Off)
	{
		const float T = FMath::DegreesToRadians(DirDeg), L = FMath::DegreesToRadians(LoftDeg);
		return FVector(FMath::Cos(L) * FMath::Cos(T), Off * FMath::Cos(L) * FMath::Sin(T), FMath::Sin(L));
	}

	/** Ball-bat impulse along Normal. Tangential velocity keeps ~5/7 (the ball rolls on the face). */
	FVector Collide(const FVector& V, const FVector& Normal, float BatSpeed, float E, float M)
	{
		const float Mb = CricketGeo::BallMass;
		const float Vn = FVector::DotProduct(V, Normal);
		const float VnOut = ((1.f + E) * M * BatSpeed + (Mb - E * M) * Vn) / (Mb + M);
		return (V - Vn * Normal) * 0.7f + VnOut * Normal;
	}
}

const FShotProfile& CricketBatting::Profile(EShotType Shot, EFootwork Foot)
{
	using F = EFootwork;
	static const FShotProfile Leave = Make(EShotType::Leave, F::Back, false, 0, 0, 0, 0, 0, 0, 0, 0, 0.2f);
	// Straight-bat MinZ is the lowest the sweet spot gets with the toe on the ground and the bat angled
	// forward (~0.17 m up the blade): anything lower comes off the toe or goes under it - the block hole.
	static const FShotProfile DefendF = Make(EShotType::Defend, F::Front, false, 0.1f, 0.95f, -0.35f, 0.7f, 3.f, -6.f, -40.f, 40.f, 0.22f);
	static const FShotProfile DefendB = Make(EShotType::Defend, F::Back, false, 0.35f, 1.4f, -0.3f, 0.7f, 3.f, -6.f, -40.f, 40.f, 0.2f);
	static const FShotProfile Drive = Make(EShotType::Drive, F::Front, false, 0.12f, 0.85f, -0.35f, 0.75f, 24.f, 3.f, -50.f, 75.f, 0.28f);
	// Lofting, the blade comes through upright with the face open rather than angled forward, so its sweet spot
	// bottoms out a little higher - but still low enough to get under a half-volley, the ball to hit straight for six.
	static const FShotProfile Loft = Make(EShotType::Loft, F::Front, false, 0.15f, 1.0f, -0.45f, 0.8f, 26.f, 30.f, -70.f, 80.f, 0.3f);
	static const FShotProfile Punch = Make(EShotType::Punch, F::Back, false, 0.5f, 1.3f, -0.2f, 0.7f, 20.f, 2.f, -35.f, 70.f, 0.24f);
	static const FShotProfile Cut = Make(EShotType::Cut, F::Back, true, 0.45f, 1.3f, 0.15f, 1.05f, 22.f, 3.f, 70.f, 135.f, 0.24f);
	static const FShotProfile Pull = Make(EShotType::Pull, F::Back, true, 0.75f, 1.75f, -0.4f, 0.55f, 26.f, 14.f, -135.f, -35.f, 0.26f);
	// Down the track: the walk adds to the swing time, and the body still moving into the shot adds
	// ~2 m/s to the bat.
	static const FShotProfile DriveA = Make(EShotType::Drive, F::Advance, false, 0.12f, 0.85f, -0.35f, 0.75f, 26.f, 3.f, -50.f, 75.f, 0.58f);
	static const FShotProfile LoftA = Make(EShotType::Loft, F::Advance, false, 0.1f, 1.0f, -0.45f, 0.8f, 28.f, 30.f, -70.f, 80.f, 0.6f);
	static const FShotProfile Sweep = Make(EShotType::Sweep, F::Front, true, 0.0f, 0.65f, -0.35f, 0.6f, 20.f, 6.f, -150.f, -55.f, 0.3f);
	// Wrists through the leg side off a full ball on the pads; fine enough it is a glance.
	static const FShotProfile Flick = Make(EShotType::Flick, F::Front, false, 0.12f, 0.9f, -0.45f, 0.3f, 20.f, 2.f, -150.f, -20.f, 0.26f);
	// Cross-bat strokes. Loft intent adds 20 deg to all but the lofted drive, so these start low.
	static const FShotProfile Hook = Make(EShotType::Hook, F::Back, true, 1.1f, 2.1f, -0.5f, 0.5f, 25.f, 4.f, -170.f, -60.f, 0.24f);
	static const FShotProfile SlogSweep = Make(EShotType::SlogSweep, F::Front, true, 0.0f, 0.8f, -0.4f, 0.6f, 25.f, 10.f, -110.f, -30.f, 0.3f);
	static const FShotProfile ReverseSweep = Make(EShotType::ReverseSweep, F::Front, true, 0.0f, 0.6f, -0.3f, 0.7f, 17.f, 6.f, 70.f, 160.f, 0.3f);
	// Face opened to the sky: little bat speed, the pace comes off the ball.
	static const FShotProfile Scoop = Make(EShotType::Scoop, F::Front, true, 0.0f, 0.7f, -0.3f, 0.4f, 8.f, 15.f, -178.f, -135.f, 0.3f);
	switch (Shot)
	{
	case EShotType::Defend: return Foot == F::Front ? DefendF : DefendB;
	case EShotType::Drive: return Foot == F::Advance ? DriveA : Drive;
	case EShotType::Loft: return Foot == F::Advance ? LoftA : Loft;
	case EShotType::Punch: return Punch;
	case EShotType::Cut: return Cut;
	case EShotType::Pull: return Pull;
	case EShotType::Sweep: return Sweep;
	case EShotType::Flick: return Flick;
	case EShotType::Hook: return Hook;
	case EShotType::SlogSweep: return SlogSweep;
	case EShotType::ReverseSweep: return ReverseSweep;
	case EShotType::Scoop: return Scoop;
	default: return Leave;
	}
}

float CricketBatting::ReadLead(const FCricketPlayer& Batter)
{
	return 0.10f + 0.14f * (1.f - FMath::Clamp(Batter.Technique, 0.f, 1.f));
}

FShotProfile CricketBatting::ChooseShot(EBatIntent Intent, float Dir, float ReadPitchX, float ReadHeight, EBowlerType BowlerType,
	float LeadTime)
{
	const bool bShort = ReadPitchX > 7.f || ReadHeight > 0.95f;
	const bool bAdvance = BowlerType != EBowlerType::Pace && LeadTime > AdvanceLead && !bShort;
	const bool bSweepable = BowlerType != EBowlerType::Pace && !bAdvance;
	switch (Intent)
	{
	case EBatIntent::Leave: return Profile(EShotType::Leave);
	case EBatIntent::Defend: return Profile(EShotType::Defend, bShort || ReadPitchX > 5.5f ? EFootwork::Back : EFootwork::Front);
	case EBatIntent::Ground:
		if (bShort) return Profile(Dir > 35.f ? EShotType::Cut : Dir < -35.f ? EShotType::Pull : EShotType::Punch);
		if (Dir < -60.f && bSweepable) return Profile(EShotType::Sweep);
		if (Dir > 80.f && bSweepable) return Profile(EShotType::ReverseSweep);
		if (Dir < -50.f && !bAdvance) return Profile(EShotType::Flick);
		return bAdvance ? DownTheTrack(EShotType::Drive, ReadPitchX) : Profile(EShotType::Drive);
	case EBatIntent::Loft:
		if (bShort) return Profile(Dir > 35.f ? EShotType::Cut : ReadHeight > 1.1f && Dir < 0.f ? EShotType::Hook : EShotType::Pull);
		if (Dir < -135.f && !bAdvance) return Profile(EShotType::Scoop);
		if (Dir < -45.f && bSweepable) return Profile(EShotType::SlogSweep);
		return bAdvance ? DownTheTrack(EShotType::Loft, ReadPitchX) : Profile(EShotType::Loft);
	}
	return Profile(EShotType::Leave);
}

FVector CricketBatting::DirectionToWorld(float Deg, ECricketHand Hand)
{
	return Dir3(Deg, 0.f, OffSideSign(Hand));
}

FContactResult CricketBatting::ResolveContact(const FBallState& Ball, const FVector& Aim, const FShotProfile& P,
	float Dir, float Timing, const FCricketPlayer& Batter, bool bAerial)
{
	FContactResult R;
	R.Shot = P.Shot;
	R.ContactPos = Ball.Pos;
	R.ContactTime = Ball.Time;
	// Good timers get a wider effective window.
	const float Tau = Timing * (1.25f - 0.5f * FMath::Clamp(Batter.Timing, 0.f, 1.f));
	R.TimingError = Timing;
	const float Off = OffSideSign(Batter.BatHand);
	const float AimLat = FMath::Clamp(Aim.Y * Off, P.ReachMin, P.ReachMax);
	const float AimZ = FMath::Clamp(Aim.Z, P.MinZ, P.MaxZ);
	if (P.Shot != EShotType::Leave) R.BatPos = FVector(Ball.Pos.X, AimLat * Off, AimZ);
	if (P.Shot == EShotType::Leave || FMath::Abs(Tau) > MissWindow) return R;

	const float BallLat = Ball.Pos.Y * Off;
	// Off-time swings meet the ball with the blade rotated away from square-on.
	const float CosPhi = FMath::Max(FMath::Cos(FMath::Clamp(Tau * 9.f, -1.2f, 1.2f)), 0.25f);

	// A: along the blade (+ toward toe), B: across the face. A mistimed swing meets the ball on the wrong part
	// of the blade: late, the bat is still coming down and the ball finds the toe; early, it takes the splice.
	const float A = (P.bCrossBat ? BallLat - AimLat : AimZ - Ball.Pos.Z) / CosPhi + Tau * BladeSlip;
	const float B = P.bCrossBat ? Ball.Pos.Z - AimZ : BallLat - AimLat;
	const bool bAlongBlade = A <= ToeLength + CricketGeo::BallRadius && A >= -HandleLength;
	if (!bAlongBlade) return R;
	if (FMath::Abs(B) <= BladeHalfWidth)
	{
		if (A > 0.07f) R.Zone = EContactZone::Toe;
		else if (A < -0.10f) R.Zone = EContactZone::Upper;
		else if (FMath::Abs(B) < 0.02f || P.bCrossBat) R.Zone = EContactZone::Middle;
		else R.Zone = B > 0.f ? EContactZone::OuterHalf : EContactZone::InnerHalf;
	}
	else if (FMath::Abs(B) <= BladeHalfWidth + CricketGeo::BallRadius)
	{
		R.Zone = P.bCrossBat ? (B > 0.f ? EContactZone::TopEdge : EContactZone::BottomEdge)
			: (B > 0.f ? EContactZone::OutsideEdge : EContactZone::InsideEdge);
	}
	else
	{
		return R;
	}

	// Timing turns the face: late goes finer on the side it was played, early goes squarer / in the air.
	const float Side = Dir >= 0.f ? 1.f : -1.f;
	const float ShotDir = FMath::Clamp(FMath::Clamp(Dir, P.DirMin, P.DirMax) + Side * Tau * 400.f, -175.f, 175.f);
	float Loft = P.LoftDeg + (bAerial && P.Shot != EShotType::Loft ? 20.f : 0.f);
	Loft += P.bCrossBat ? FMath::Abs(Tau) * 200.f : -Tau * 250.f;
	// How far from the sweet spot, 0..1 along the blade (toe falls off faster than the splice) and across it.
	const float OffLength = FMath::Min(1.f, FMath::Square(A > 0.f ? A / 0.13f : -A / 0.22f));
	const float OffWidth = FMath::Clamp((FMath::Abs(B) - 0.015f) / (BladeHalfWidth - 0.015f), 0.f, 1.f); // flat middle, then twist grows with the moment arm
	Loft += A > 0.f ? -10.f * OffLength : 15.f * OffLength; // toe keeps it down, the splice pops it up
	// Below the sweet spot's lowest reach the straight blade has to be angled forward over the ball,
	// closing the face: a ball dug out of the block hole goes into the pitch.
	if (!P.bCrossBat && Ball.Pos.Z < P.MinZ) Loft -= FMath::RadiansToDegrees(FMath::Atan((P.MinZ - Ball.Pos.Z) / ToeLength));

	Loft = FMath::Clamp(Loft, -25.f, 60.f);

	// Effective bat mass (kg) and restitution fall off smoothly with that distance.
	float M = FMath::Max(0.2f, 0.65f - 0.4f * OffLength - 0.35f * OffWidth);
	float E = FMath::Max(0.25f, 0.5f - 0.2f * OffLength - 0.15f * OffWidth);
	// Soft hands deaden the blade (restitution), they do not make it lighter: below ~0.5 kg the ball
	// outweighs a deadened bat and carries on through the face towards the keeper.
	if (P.bSoftHands) E = FMath::Min(E, 0.3f);

	const float TimingQuality = FMath::Max(0.25f, 1.f - FMath::Square(Tau / TimingWindow));
	const float Speed = P.BatSpeed * (0.75f + 0.5f * FMath::Clamp(Batter.Power, 0.f, 1.f)) * TimingQuality;
	R.Quality = TimingQuality * M / 0.65f;

	// The batter angles the face so a sweet-spot hit would travel the intended way.
	const FVector Want = Dir3(ShotDir, Loft, Off);
	FVector Face = Want;
	// Damped: off a dead bat the ball mostly slides along the face, so an undamped correction overshoots.
	for (int32 I = 0; I < 8; ++I)
	{
		const FVector Got = Collide(Ball.Vel, Face, Speed, P.bSoftHands ? 0.3f : 0.5f, 0.65f).GetSafeNormal();
		Face = (Face + 0.6f * (Want - Got)).GetSafeNormal();
	}

	FVector Normal = Face;
	const bool bEdge = R.Zone == EContactZone::InsideEdge || R.Zone == EContactZone::OutsideEdge
		|| R.Zone == EContactZone::TopEdge || R.Zone == EContactZone::BottomEdge;
	if (bEdge)
	{
		// The rounded edge presents a surface normal tilted toward the across-face axis.
		const FVector Across = P.bCrossBat ? FVector(0.f, 0.f, FMath::Sign(B)) : FVector(0.f, Off * FMath::Sign(B), 0.f);
		const float Glance = FMath::Asin(FMath::Clamp((FMath::Abs(B) - BladeHalfWidth) / CricketGeo::BallRadius, 0.f, 1.f));
		Normal = (Face * FMath::Cos(Glance) + Across * FMath::Sin(Glance)).GetSafeNormal();
	}
	R.ExitVel = Collide(Ball.Vel, Normal, Speed * (bEdge ? FMath::Max(0.f, FVector::DotProduct(Normal, Face)) : 1.f), E, M);
	return R;
}
