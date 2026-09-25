#include "CricketPose.h"

namespace CricketPose
{
namespace
{
	FVector Rotate(const FVector& V, const FVector& Axis, float Deg) { return FQuat(Axis, FMath::DegreesToRadians(Deg)).RotateVector(V); }
}

FSwing PlanSwing(EShotType Shot, const FVector& Pivot, const FVector& Contact, const FVector& BallDir)
{
	FSwing S;
	S.Contact = Contact;
	// Lean in when the grip would have to be further from the shoulders than the arms reach, and sway away
	// to make room when the ball is so close the bat would have to choke.
	const FVector ToBall = Contact - Pivot;
	const float Dist = ToBall.Size();
	const float Short = Dist - SweetFromGrip - MaxGripReach, Cramped = SweetFromGrip + MinGripReach - Dist;
	S.Lean = ToBall.GetSafeNormal() * (Short > 0.f ? FMath::Min(Short, MaxLean) : Cramped > 0.f ? -FMath::Min(Cramped, MaxLean) : 0.f);
	S.Pivot = Pivot + S.Lean;
	const FVector D = Contact - S.Pivot;
	const FVector Dir = D.GetSafeNormal();
	// The grip on the line from the shoulders to the ball, a sweet spot's length short of it, so the sweet
	// spot is on the ball (unless the ball is so close the bat must choke: the grip stays off the chest).
	S.Grip0 = S.Pivot + Dir * FMath::Clamp(D.Size() - SweetFromGrip, 0.15f, MaxGripReach);
	// The bat moves through the ball along the shot; the swing's axis is square to that and the bat.
	const FVector V = BallDir.GetSafeNormal();
	FVector Spin = FVector::CrossProduct(Dir, V);
	if (Spin.Size() < 0.2f) Spin = FVector::CrossProduct(Dir, FVector::UpVector); // hit straight along the bat
	S.Spin = Spin.GetSafeNormal();
	const FVector Axis = (Contact - S.Grip0).GetSafeNormal();
	S.Face0 = (V - Axis * FVector::DotProduct(V, Axis)).GetSafeNormal(UE_SMALL_NUMBER, FVector::CrossProduct(S.Spin, Axis));
	switch (Shot)
	{
	case EShotType::Leave: S.Backlift = 125.f; S.Follow = 0.f; S.bHold = true; break;
	case EShotType::Defend: S.Backlift = 45.f; S.Follow = 10.f; break;
	case EShotType::Loft: case EShotType::SlogSweep: case EShotType::Scoop: S.Backlift = 125.f; S.Follow = 175.f; break;
	case EShotType::Cut: case EShotType::Pull: case EShotType::Hook: case EShotType::Sweep: case EShotType::ReverseSweep:
		S.Backlift = 100.f; S.Follow = 150.f; break;
	default: S.Backlift = 110.f; S.Follow = 140.f; break; // drive, punch, flick
	}
	return S;
}

FBat BatAt(const FSwing& S, float AngleDeg)
{
	FBat B;
	// Away from the contact the arms fold, drawing the hands in toward the chest: at full stretch round
	// behind the far shoulder the grip would be out of reach.
	const float Fold = FMath::Lerp(1.f, FoldedReach, FMath::Clamp(FMath::Abs(AngleDeg) / 90.f, 0.f, 1.f));
	B.Grip = S.Pivot + Rotate(S.Grip0 - S.Pivot, S.Spin, AngleDeg) * Fold;
	B.Axis = Rotate((S.Contact - S.Grip0).GetSafeNormal(), S.Spin, AngleDeg);
	B.Face = Rotate(S.Face0, S.Spin, AngleDeg);
	return B;
}

FBat Blend(const FBat& A, const FBat& B, float Alpha)
{
	FBat O;
	O.Grip = FMath::Lerp(A.Grip, B.Grip, Alpha);
	O.Axis = FMath::Lerp(A.Axis, B.Axis, Alpha).GetSafeNormal(UE_SMALL_NUMBER, A.Axis);
	O.Face = FMath::Lerp(A.Face, B.Face, Alpha).GetSafeNormal(UE_SMALL_NUMBER, A.Face);
	return O;
}

float StrokeAngle(const FSwing& S, float T, float PressTime, float ImpactTime)
{
	if (S.bHold || T <= PressTime) return -S.Backlift;
	if (T < ImpactTime)
	{
		const float U = (T - PressTime) / FMath::Max(ImpactTime - PressTime, 0.05f);
		return -S.Backlift * (1.f - U * U); // gathers speed into the ball
	}
	const float V = FMath::Min((T - ImpactTime) / FollowSeconds, 1.f);
	return S.Follow * (1.f - (1.f - V) * (1.f - V)); // and runs out of it
}

FStrokeClip StrokeClip(EShotType Shot, EFootwork Foot, float DirectionDeg)
{
	// Contact times read off the filmed takes, 0.1 s apart.
	switch (Shot)
	{
	case EShotType::Defend: return Foot == EFootwork::Back ? FStrokeClip{ TEXT("Bat_Defend_Back"), 1.65f } : FStrokeClip{ TEXT("Bat_Defend_Front"), 1.5f };
	case EShotType::Drive: return DirectionDeg > 20.f ? FStrokeClip{ TEXT("Bat_Drive_Cover"), 1.35f } : FStrokeClip{ TEXT("Bat_Drive"), 1.15f };
	case EShotType::Loft: return { TEXT("Bat_Loft"), 0.9f };
	case EShotType::Punch: return { TEXT("Bat_Punch"), 1.45f };
	case EShotType::Cut: return { TEXT("Bat_Cut"), 1.05f };
	case EShotType::Pull:
	case EShotType::Hook: return { TEXT("Bat_Pull"), 1.1f };
	case EShotType::Flick: return { TEXT("Bat_Flick"), 0.9f };
	case EShotType::Sweep:
	case EShotType::SlogSweep:
	case EShotType::ReverseSweep:
	case EShotType::Scoop: return { TEXT("Bat_Sweep"), 0.85f }; // all played down on one knee
	default: return {};
	}
}

float StrokeClipTime(const FStrokeClip& Clip, float T, float PressTime, float ImpactTime)
{
	if (T >= ImpactTime) return Clip.Contact + (T - ImpactTime);
	const float U = FMath::Clamp((T - PressTime) / FMath::Max(ImpactTime - PressTime, 0.05f), 0.f, 1.f);
	return Clip.Contact - ClipDownswing * (1.f - U);
}

FClipPlay ThrowClip(float Post, float Ready, float Release)
{
	// In over up to 0.4 s from a second before the release (or once the ball is in hand, if later); out as the
	// follow-through settles.
	const float From = FMath::Max(Release - 1.f, Ready - 0.1f), In = FMath::Min(0.4f, Release - From);
	return { FMath::Max(0.f, ThrowClipRelease + Post - Release),
		FMath::SmoothStep(From, From + In, Post) * (1.f - FMath::SmoothStep(Release + 0.8f, Release + 1.4f, Post)) };
}

FClipPlay DiveClip(float Post, float FieldTime)
{
	const float Time = DiveClipStretch + Post - FieldTime;
	// In through the shuffle before the launch; out once back on the feet.
	return { FMath::Max(0.f, Time), FMath::SmoothStep(0.f, 0.3f, Time) * (1.f - FMath::SmoothStep(DiveClipUp - 0.3f, DiveClipUp, Time)) };
}

FVector ArmCircle(const FVector& Shoulder, const FVector& Forward, float AngleDeg, float Reach)
{
	const float R = FMath::DegreesToRadians(AngleDeg);
	const FVector F = FVector(Forward.X, Forward.Y, 0.f).GetSafeNormal();
	return Shoulder + Reach * (FMath::Cos(R) * FVector::UpVector + FMath::Sin(R) * F);
}

float BowlingArmAngle(float TimeToRelease)
{
	const float S = TimeToRelease;
	if (S < -0.18f) return -150.f; // the gather: hand down by the hip, behind
	if (S < 0.f)
	{
		const float U = (S + 0.18f) / 0.18f;
		return FMath::Lerp(-150.f, ReleaseAngle, U * U); // whips over the top
	}
	const float V = FMath::Min(S / 0.3f, 1.f);
	return ReleaseAngle + 200.f * (1.f - (1.f - V) * (1.f - V)); // down across the body
}

float BowlingArmWeight(float TimeToRelease)
{
	const float S = TimeToRelease;
	if (S < -0.5f || S > 0.9f) return 0.f;
	if (S < -0.3f) return (S + 0.5f) / 0.2f;
	if (S > 0.6f) return (0.9f - S) / 0.3f;
	return 1.f;
}
}
