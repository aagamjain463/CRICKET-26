#include "BallSimulation.h"

namespace
{
	// 0.5 * rho * Cd * A / m with rho 1.2, Cd 0.45, r 3.6 cm, m 156 g.
	constexpr float KDrag = 0.0069f;
	// Lift ~ 0.6 * (R w / v) * 0.5 rho A v^2 / m, i.e. linear in (w x v).
	constexpr float KMagnus = 3.4e-4f;
	constexpr float RollThreshold = 0.6f; // vertical rebound speed below which the ball rolls
	constexpr float StopSpeed = 0.05f;

	bool IsOnPitch(const FVector& P)
	{
		return FMath::Abs(P.Y) < CricketGeo::PitchHalfWidth && P.X > -1.5f && P.X < CricketGeo::PitchLength + 1.5f;
	}

	void Bounce(FBallState& B, const FSurface& S, float Grip)
	{
		const float R = CricketGeo::BallRadius;
		const float Vn = -B.Vel.Z;
		B.Vel.Z = S.Restitution * Vn;

		// Slip of the contact point; Coulomb friction opposes it until the ball rolls. Friction is
		// applied per ground axis (a friction pyramid, as game physics engines do) rather than along the
		// total slip, so side spin grips even though the forward slip dominates.
		const FVector Arm(0.f, 0.f, -R);
		const FVector Slip = FVector(B.Vel.X, B.Vel.Y, 0.f) + FVector::CrossProduct(B.Spin, Arm);
		const float MaxImpulse = S.Friction * (1.f + S.Restitution) * Vn;
		// Solid sphere: slip on an axis stops once the impulse reaches 2/7 of it.
		// Grip scales the sideways part, the turn: a dusty or worn surface bites harder than a fresh one.
		const FVector J(-FMath::Sign(Slip.X) * FMath::Min(MaxImpulse, (2.f / 7.f) * FMath::Abs(Slip.X)),
			-FMath::Sign(Slip.Y) * FMath::Min(MaxImpulse, (2.f / 7.f) * FMath::Abs(Slip.Y)) * Grip, 0.f); // per unit mass
		B.Vel += J;
		B.Spin += FVector::CrossProduct(Arm, J) / (0.4f * R * R);
		B.Pos.Z = R;
		B.Bounces++;
		if (B.Vel.Z < RollThreshold)
		{
			B.Vel.Z = 0.f;
			B.bRolling = true;
		}
	}
}

FPitchConditions CricketBall::Conditions(EPitchType Type, float Wear, float Cloud, bool bDew)
{
	FPitchConditions C;
	switch (Type)
	{
	case EPitchType::Green: C.Pitch.Restitution = 0.67f; C.Seam = 1.6f; C.Grip = 0.8f; C.Rough = 0.1f; break; // bounce and seam, little turn
	case EPitchType::Dusty: C.Pitch.Restitution = 0.59f; C.Seam = 0.7f; C.Grip = 1.3f; C.Rough = 0.6f; break; // low and slow, it turns
	default: C.Rough = 0.3f; break;
	}
	Wear = FMath::Clamp(Wear, 0.f, 1.f);
	C.Rough = FMath::Min(1.f, C.Rough + 0.4f * Wear);
	C.Pitch.Restitution -= 0.04f * Wear;
	C.Grip += 0.3f * Wear;
	C.Seam *= 1.f - 0.3f * Wear;
	C.Swing = 1.f + 0.6f * FMath::Clamp(Cloud, 0.f, 1.f);
	if (bDew)
	{
		// A wet ball skids on and slips out of the fingers, and races across the damp outfield.
		C.Grip *= 0.8f;
		C.Swing *= 0.85f;
		C.Outfield.RollingDecel *= 0.85f;
	}
	return C;
}

bool CricketBall::IsInRough(const FVector& P)
{
	const float Y = FMath::Abs(P.Y);
	return P.X > 2.2f && P.X < 5.f && Y > 0.3f && Y < 1.1f;
}

CricketBall::EStep CricketBall::Step(FBallState& B, const FPitchConditions& C, float Dt)
{
	const FSurface& S = IsOnPitch(B.Pos) ? C.Pitch : C.Outfield;
	B.Time += Dt;

	if (B.bRolling)
	{
		const FVector H(B.Vel.X, B.Vel.Y, 0.f);
		const float Speed = H.Size();
		const float NewSpeed = Speed - (S.RollingDecel + KDrag * Speed * Speed) * Dt;
		if (NewSpeed < StopSpeed)
		{
			B.Vel = FVector::ZeroVector;
			B.Spin = FVector::ZeroVector;
			return EStep::Stopped;
		}
		B.Vel = H * (NewSpeed / Speed);
		B.Pos += B.Vel * Dt;
		B.Pos.Z = CricketGeo::BallRadius;
		return EStep::None;
	}

	FVector Accel(0.f, 0.f, -CricketGeo::Gravity);
	Accel -= KDrag * B.Vel.Size() * B.Vel;
	Accel += KMagnus * FVector::CrossProduct(B.Spin, B.Vel);
	if (B.Bounces == 0) Accel.Y += B.SwingAccel;

	B.Vel += Accel * Dt;
	B.Pos += B.Vel * Dt;

	if (B.Pos.Z <= CricketGeo::BallRadius && B.Vel.Z < 0.f)
	{
		const bool bPitch = IsOnPitch(B.Pos);
		if (bPitch && B.Bounces == 0) B.Vel.Y += B.SeamKick;
		FSurface Surface = bPitch ? C.Pitch : C.Outfield;
		float Grip = bPitch ? C.Grip : 1.f;
		if (bPitch && IsInRough(B.Pos))
		{
			Surface.Restitution -= 0.06f * C.Rough;
			Grip *= 1.f + 0.5f * C.Rough;
		}
		Bounce(B, Surface, Grip);
		return EStep::Bounce;
	}
	return EStep::None;
}

bool CricketBall::SimulateToPlane(FBallState& B, float PlaneX, const FPitchConditions& C, float MaxTime, TArray<FVector>* Path)
{
	const float EndTime = B.Time + MaxTime;
	while (B.Pos.X > PlaneX)
	{
		const FBallState Prev = B;
		if (Step(B, C) == EStep::Stopped || B.Time > EndTime) return false;
		if (B.Pos.X <= PlaneX)
		{
			const float F = (Prev.Pos.X - PlaneX) / FMath::Max(Prev.Pos.X - B.Pos.X, KINDA_SMALL_NUMBER);
			B.Pos = FMath::Lerp(Prev.Pos, B.Pos, F);
			B.Time = FMath::Lerp(Prev.Time, B.Time, F);
			if (Path) Path->Add(B.Pos);
			return true;
		}
		if (Path) Path->Add(B.Pos);
	}
	return true;
}

bool CricketBall::PredictAtPlane(FBallState B, float PlaneX, const FPitchConditions& C, FVector& OutPos, float& OutTime)
{
	B.Spin = FVector::ZeroVector;
	B.SwingAccel = 0.f;
	B.SeamKick = 0.f;
	if (!SimulateToPlane(B, PlaneX, C)) return false;
	OutPos = B.Pos;
	OutTime = B.Time;
	return true;
}

FVector CricketBall::SpinForMagnus(const FVector& Velocity, const FVector& Direction, float RadPerSec)
{
	return FVector::CrossProduct(Velocity, Direction).GetSafeNormal() * RadPerSec;
}

TArray<EDeliveryType> CricketBowling::Repertoire(EBowlerType Type)
{
	switch (Type)
	{
	case EBowlerType::OffSpin: return { EDeliveryType::OffBreak, EDeliveryType::ArmBall, EDeliveryType::TopSpinner };
	case EBowlerType::LegSpin: return { EDeliveryType::LegBreak, EDeliveryType::Googly, EDeliveryType::TopSpinner, EDeliveryType::Slider };
	default: return { EDeliveryType::Stock, EDeliveryType::Outswing, EDeliveryType::Inswing, EDeliveryType::Cutter, EDeliveryType::Slower,
			EDeliveryType::Seam, EDeliveryType::CrossSeam };
	}
}

FDeliveryRelease CricketBowling::Execute(const FCricketPlayer& Bowler, ECricketHand BatterHand, const FDeliveryPlan& Plan,
	float Timing, int32 Seed, const FPitchConditions& C)
{
	FRandomStream Rng(Seed);
	const float Off = OffSideSign(BatterHand);
	const float Arm = Bowler.BowlHand == ECricketHand::Right ? 1.f : -1.f;
	const bool bSpin = Bowler.BowlerType != EBowlerType::Pace;
	Timing = FMath::Clamp(Timing, -1.f, 1.f);
	const float Q = 1.f - FMath::Abs(Timing);

	FDeliveryRelease Out;
	Out.bNoBall = Timing > 0.85f;

	// Execution: accuracy sets the scatter, a poor release widens it and biases length
	// (early = overpitched, late = dragged down).
	// A scrambled seam gives up movement for control: it is the easiest ball to land.
	const float Scatter = (0.5f + 1.5f * (1.f - Q)) * (Plan.Type == EDeliveryType::CrossSeam ? 0.75f : 1.f) * EffortScatter(Plan.Effort);
	const float Skill = FMath::Clamp(Bowler.Accuracy, 0.f, 1.f);
	const float LengthSigma = (0.15f + 0.9f * (1.f - Skill)) * Scatter;
	const float LineSigma = (0.04f + 0.25f * (1.f - Skill)) * Scatter;
	const float Length = Plan.Length + Timing * 1.8f + CricketMath::Gauss(Rng) * LengthSigma;
	const float LineY = (Plan.Line + CricketMath::Gauss(Rng) * LineSigma) * Off;
	Out.AimedPitch = FVector2D(Length, LineY);
	Out.Type = Plan.Type;

	float SpeedFactor = 1.f;
	switch (Plan.Type)
	{
	case EDeliveryType::Outswing: case EDeliveryType::Inswing: SpeedFactor = 0.98f; break;
	case EDeliveryType::Cutter: SpeedFactor = 0.9f; break;
	case EDeliveryType::Slower: SpeedFactor = 0.78f; break;
	case EDeliveryType::CrossSeam: SpeedFactor = 0.96f; break;
	case EDeliveryType::ArmBall: case EDeliveryType::Slider: SpeedFactor = 1.06f; break;
	case EDeliveryType::Googly: SpeedFactor = 0.97f; break;
	default: break;
	}
	const float Speed = Bowler.PaceKph / 3.6f * SpeedFactor * EffortPace(Plan.Effort) * (1.f - 0.05f * (1.f - Q) + 0.01f * CricketMath::Gauss(Rng));
	Out.SpeedKph = Speed * 3.6f;

	FBallState& B = Out.Ball;
	B.Pos = FVector(CricketGeo::PitchLength - 1.5f, 0.26f * Arm, bSpin ? 1.95f : 2.15f);

	// Movement. Magnitudes scale with the bowler's skill and the quality of the release.
	const float Move = FMath::Clamp(Bowler.Movement, 0.f, 1.f) * FMath::Clamp(Plan.Movement, 0.f, 1.f) * FMath::Lerp(0.35f, 1.f, Q);
	FVector Spin = FVector::ZeroVector;
	if (!bSpin)
	{
		const float SpeedWindow = FMath::Clamp(1.f - FMath::Abs(Speed - 36.f) / 20.f, 0.3f, 1.f);
		const float Swing = 3.4f * Move * SpeedWindow;
		switch (Plan.Type)
		{
		case EDeliveryType::Outswing: B.SwingAccel = Swing * Off; break;
		case EDeliveryType::Inswing: B.SwingAccel = -Swing * Off; break;
		case EDeliveryType::Slower: B.SwingAccel = 0.3f * Swing * CricketMath::Gauss(Rng); break;
		case EDeliveryType::Cutter: Spin.X = Arm * 70.f * Move; break; // off-cutter: into the right-hander
		case EDeliveryType::Seam: B.SwingAccel = 0.1f * Swing * CricketMath::Gauss(Rng); break;
		case EDeliveryType::CrossSeam: break; // no seam to swing on or land on
		default: B.SwingAccel = 0.25f * Swing * CricketMath::Gauss(Rng); break;
		}
		// Off the pitch the ball deviates whichever way the seam lands, unknown to bowler and batter alike.
		// An upright seam lands on it more often and moves further; the scrambled seam lands on leather.
		const float SeamLands = Plan.Type == EDeliveryType::Seam ? 0.65f : Plan.Type == EDeliveryType::CrossSeam ? 0.f : 0.35f;
		B.SeamKick = SeamLands * Move * CricketMath::Gauss(Rng);
	}
	else
	{
		const float Revs = (60.f + 110.f * Move);
		// Side spin about the direction of travel turns the ball; Spin.X > 0 turns it toward -Y.
		switch (Plan.Type)
		{
		case EDeliveryType::OffBreak: Spin = FVector(Arm * Revs, -0.35f * Revs, 0.f); break;
		case EDeliveryType::Googly: Spin = 0.8f * FVector(Arm * Revs, -0.35f * Revs, 0.f); break; // out of the back of the hand: fewer revs
		case EDeliveryType::LegBreak: Spin = FVector(-Arm * Revs, -0.3f * Revs, 0.f); break;
		case EDeliveryType::TopSpinner: Spin = FVector(0.f, -0.9f * Revs, 0.f); break;
		case EDeliveryType::ArmBall: B.SwingAccel = 0.9f * Arm * Move; Spin = FVector(0.f, 0.4f * Revs, 0.f); break;
		case EDeliveryType::Slider: Spin = FVector(-0.2f * Arm * Revs, 0.5f * Revs, 0.f); break; // backspin: flatter, lower, little turn
		default: break;
		}
	}

	B.SwingAccel *= C.Swing;
	B.SeamKick *= C.Seam;

	// Solve elevation and azimuth so the ball pitches at the (error-perturbed) target.
	// Bisection on elevation inside the low-trajectory bracket, where the pitch point moves monotonically
	// toward the batter as elevation rises. (An unbounded secant step could jump onto the lob branch.)
	const float Dist = B.Pos.X - Length;
	float Lo = FMath::DegreesToRadians(-35.f), Hi = FMath::DegreesToRadians(20.f);
	float Elev = 0.5f * (Lo + Hi);
	float Azim = FMath::Atan2(LineY - B.Pos.Y, Dist);
	for (int32 Iter = 0; Iter < 30; ++Iter)
	{
		Elev = 0.5f * (Lo + Hi);
		FBallState Probe = B;
		Probe.Vel = Speed * FVector(-FMath::Cos(Elev) * FMath::Cos(Azim), FMath::Cos(Elev) * FMath::Sin(Azim), FMath::Sin(Elev));
		Probe.Spin = !bSpin ? CricketBall::SpinForMagnus(Probe.Vel, FVector::UpVector, 30.f) + Spin : Spin;
		Probe.SeamKick = 0.f;
		while (Probe.Bounces == 0 && Probe.Time < 4.f) CricketBall::Step(Probe, C);
		const float ErrX = Probe.Pos.X - Length; // > 0: pitched short of target, aim fuller (raise)
		const float ErrY = Probe.Pos.Y - LineY;
		if (FMath::Abs(ErrX) < 0.01f && FMath::Abs(ErrY) < 0.01f) break;
		(ErrX > 0.f ? Lo : Hi) = Elev;
		Azim -= ErrY / FMath::Max(Dist, 1.f);
	}
	B.Vel = Speed * FVector(-FMath::Cos(Elev) * FMath::Cos(Azim), FMath::Cos(Elev) * FMath::Sin(Azim), FMath::Sin(Elev));
	B.Spin = !bSpin ? CricketBall::SpinForMagnus(B.Vel, FVector::UpVector, 30.f) + Spin : Spin;
	return Out;
}
