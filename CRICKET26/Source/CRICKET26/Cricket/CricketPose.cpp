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

FBat CarriedBat(const FVector& Elbow, const FVector& Wrist, const FVector& Forward, const FVector& Right)
{
	const FVector Forearm = (Wrist - Elbow).GetSafeNormal(UE_SMALL_NUMBER, -FVector::UpVector);
	FBat B;
	B.Axis = (Forearm + 0.8f * Forward).GetSafeNormal();
	// The toe stays well below the hands however far the arm swings.
	if (B.Axis.Z > -0.3f) B.Axis = (FVector(B.Axis.X, B.Axis.Y, 0.f).GetSafeNormal() * 0.954f - FVector::UpVector * 0.3f);
	// The palm is a hand's width past the wrist; it holds the handle below the grip's centre.
	B.Grip = Wrist + Forearm * 0.08f - B.Axis * 0.05f;
	B.Face = Right;
	return B;
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
	// In as the captured keeper's shuffle ends, so a running fielder goes straight into the launch; out once back
	// on the feet.
	return { FMath::Max(0.f, Time), DiveClipIn(Time) * (1.f - FMath::SmoothStep(DiveClipUp - 0.3f, DiveClipUp, Time)) };
}

FVector ClipInActor(const FQuat& ActorQuat, const FQuat& BodyQuat, const FVector& BodyScale, const FVector& C)
{
	return (ActorQuat.Inverse() * BodyQuat).RotateVector(C * BodyScale) / 100.f;
}

FQuat LeanRotation(const FQuat& Current, const FVector& Up)
{
	// Stand the figure up first: the heading of a tipped figure's forward is not the way it faces.
	const FQuat Standing = FQuat::FindBetweenNormals(Current.GetUpVector(), FVector::UpVector) * Current;
	return FQuat::FindBetweenNormals(FVector::UpVector, Up.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector)) * Standing;
}

float DiveClipIn(float Time)
{
	return FMath::SmoothStep(DiveClipLaunch - 0.25f, DiveClipLaunch, Time);
}

bool UseDiveClip(int32 Action, bool bDive, float LateralM, float HeightM)
{
	// EFieldAction: None=0, CatchFlat=1, CatchHigh=2, CatchLow=3, CatchDiving=4, CatchKeeper=5,
	// CatchBoundary=6, CatchRelay=7, KeeperTake=8, PickupClean=9, PickupOnRun=10, LongBarrier=11,
	// SlideStop=12, DiveStop=13, Fumble=14.
	const bool bDiveAction = (Action == 4 || Action == 13);
	if (!bDive && !bDiveAction) return false;
	if (Action == 12) return false; // slide stops lean procedurally, never the keeper dive
	// Above a leap's reach the fielder stays on their feet and stretches up. There is no lower limit: a take the
	// solver could only reach at full stretch is a full-length dive however close to the grass it is.
	if (HeightM > 2.1f) return false;
	return bDiveAction || LateralM > 0.9f;
}

float CatchReadyDrop(int32 Action, bool bTaker)
{
	if (!bTaker) return 0.08f; // walking in, ready
	// Deeper for low takes and ground balls, taller for skiers.
	switch (Action)
	{
	case 3: return 0.30f;  // CatchLow
	case 2: return 0.07f;  // CatchHigh
	case 4: return 0.20f;  // CatchDiving
	case 5: return 0.10f;  // CatchKeeper (outfielder covering)
	case 6: return 0.14f;  // CatchBoundary
	case 7: return 0.14f;  // CatchRelay
	case 9: return 0.26f;  // PickupClean
	case 10: return 0.18f; // PickupOnRun
	case 11: return 0.30f; // LongBarrier: kneel side-on
	case 12: return 0.24f; // SlideStop: slide lean
	case 13: return 0.20f; // DiveStop
	case 14: return 0.18f; // Fumble: second effort
	case 8: return 0.22f;  // KeeperTake (outfielder covering)
	default: break;
	}
	return 0.16f; // CatchFlat and general
}

float TakeCrouchWeight(float Post, float FieldTime)
{
	return FMath::SmoothStep(FieldTime - 0.5f, FieldTime - 0.1f, Post) * (1.f - FMath::SmoothStep(FieldTime + 0.3f, FieldTime + 0.8f, Post));
}

FVector CatchGloveOffset(int32 Action, float HeightM, float LateralM)
{
	// X forward toward the ball, Y toward the take side, Z up from the chest.
	// Keeps hands cupped in front of the body, never crossed, never behind the head.
	const float Side = FMath::Clamp(LateralM, -1.2f, 1.2f);
	switch (Action)
	{
	case 2: return FVector(0.30f, 0.5f * Side, FMath::Clamp(HeightM - 1.35f, 0.25f, 0.75f)); // CatchHigh: overhead cup
	case 3: return FVector(0.38f, 0.5f * Side, FMath::Clamp(HeightM - 1.35f, -0.95f, -0.55f)); // CatchLow: fingers down
	case 4: return FVector(0.55f, Side, FMath::Clamp(HeightM - 1.25f, -0.5f, 0.3f)); // CatchDiving: full reach
	case 6:
	case 7: return FVector(0.34f, 0.6f * Side, FMath::Clamp(HeightM - 1.35f, -0.1f, 0.55f)); // rope/relay: jump cup
	default: break;
	}
	if (Action >= 8) return FVector(0.42f, 0.4f * Side, -0.62f); // ground takes: low and forward
	return FVector(0.36f, 0.5f * Side, FMath::Clamp(HeightM - 1.35f, -0.25f, 0.25f)); // CatchFlat: chest cup
}

FVector CatchSecureOffset(const FVector& Cup, float Give)
{
	// Gathered in front of the belt buckle, elbows down, never up at the chin: a cupped take is pulled in toward
	// the body and down, not hugged to the chest.
	const FVector Secure(0.30f, 0.f, -0.30f);
	return FMath::Lerp(Cup, Secure, FMath::Clamp(Give / 0.14f, 0.f, 1.f));
}

float CatchGive(float Post, float FieldTime)
{
	if (Post <= FieldTime) return 0.f;
	return 0.14f * FMath::Clamp((Post - FieldTime) / 0.18f, 0.f, 1.f);
}

float CatchSecure(float Post, float FieldTime, float HoldUntil)
{
	if (Post < FieldTime) return 0.f;
	return 1.f - FMath::SmoothStep(HoldUntil, HoldUntil + 0.45f, Post);
}

float ThrowGatherWeight(float Post, float Release)
{
	// Crow-hop: ramps in over the 0.3 s before release, out just after.
	return FMath::SmoothStep(Release - 0.35f, Release - 0.05f, Post) * (1.f - FMath::SmoothStep(Release, Release + 0.25f, Post));
}

FClipPlay BowlClip(float TimeToRelease, float Start)
{
	// ponytail: a release more than BowlClipRelease after setting off holds the clip's first frame until it catches up;
	// author a longer run-in if the AI's late releases show it.
	const float Time = FMath::Clamp(BowlClipRelease + FMath::Max(TimeToRelease, Start), 0.f, BowlClipEnd);
	return { Time, FMath::SmoothStep(Start, Start + 0.15f, TimeToRelease) * (1.f - FMath::SmoothStep(BowlClipEnd - 0.3f, BowlClipEnd, Time)) };
}

float StumpPushWeight(float PelvisX, float ReleasePelvisX)
{
	return FMath::SmoothStep(ReleasePelvisX, CricketGeo::PitchLength - 0.3f, PelvisX);
}

float StumpPush(TFunctionRef<FVector(int32 Bone, float T)> BoneAt, float Side)
{
	using namespace CricketGeo;
	const float ReleasePelvisX = BoneAt(0, BowlClipRelease).X;
	float Push = 0.f;
	for (float T = 0.f; T <= BowlClipRelease; T += 1.f / 60.f)
	{
		const float W = StumpPushWeight(BoneAt(0, T).X, ReleasePelvisX);
		if (W < 0.05f) continue; // ponytail: a joint at the stumps this close to the release would need the release point moved
		for (int32 I = 1; I < UE_ARRAY_COUNT(StumpBones); ++I) // the legs: the pelvis rides above the stumps
		{
			const FVector J = BoneAt(I, T);
			const float Reach = StumpsHalfWidth + StumpBones[I].Radius + StumpGap;
			if (FMath::Abs(J.X - PitchLength) < StumpBones[I].Radius + StumpGap)
				Push = FMath::Max(Push, (Reach - Side * J.Y) / W);
		}
	}
	return Push;
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

float UnderarmArmAngle(float TimeToRelease)
{
	const float S = TimeToRelease;
	if (S < 0.f) return FMath::Lerp(-125.f, -225.f, FMath::SmoothStep(-0.3f, 0.f, S)); // back, then down through
	return -225.f - 40.f * FMath::SmoothStep(0.f, 0.3f, S);                             // follows the ball out
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
