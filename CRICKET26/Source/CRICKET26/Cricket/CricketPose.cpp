#include "CricketPose.h"

namespace CricketPose
{
FBat Blend(const FBat& A, const FBat& B, float Alpha)
{
	FBat O;
	O.Grip = FMath::Lerp(A.Grip, B.Grip, Alpha);
	O.Axis = FMath::Lerp(A.Axis, B.Axis, Alpha).GetSafeNormal(UE_SMALL_NUMBER, A.Axis);
	O.Face = FMath::Lerp(A.Face, B.Face, Alpha).GetSafeNormal(UE_SMALL_NUMBER, A.Face);
	return O;
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
