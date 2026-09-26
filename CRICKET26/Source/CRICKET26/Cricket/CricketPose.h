// Cricket actions as geometry: the bat, the fielders' captured throw and dive timed onto the simulation, and
// the bowling and throwing arm windmilling about the shoulder in the plane of the delivery. The striker's
// stroke is CricketBatter's. Everything is in the simulation frame (metres).

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.h"

namespace CricketPose
{
	constexpr float BatLength = 0.85f;     // the drawn bat
	constexpr float HandleLength = 0.29f;
	constexpr float GripFromTop = 0.12f;   // centre of the two hands on the handle
	constexpr float SweetFromGrip = 0.55f; // grip to the middle of the sweet spot

	struct FBat
	{
		FVector Grip = FVector::ZeroVector;
		FVector Axis = -FVector::UpVector;  // grip toward the toe
		FVector Face = FVector::ForwardVector; // the way the blade's face points
		FVector SweetSpot() const { return Grip + Axis * SweetFromGrip; }
		FVector Top() const { return Grip - Axis * GripFromTop; }
	};

	/** Blends two bat poses (0 = A). */
	FBat Blend(const FBat& A, const FBat& B, float Alpha);

	/** Where to play a clip, and how much of it over what is under it. */
	struct FClipPlay
	{
		float Time = 0.f;   // s into the clip
		float Weight = 0.f; // 0 to 1
	};

	/**
	 * The captured throw (Mixamo "Baseball Pitching", released ThrowClipRelease s in) for a fielder with the ball in
	 * hand from Ready who lets go at Release, times after contact: its release lands on the simulation's. A short gap
	 * joins the wind-up part way through rather than squeezing it.
	 */
	FClipPlay ThrowClip(float Post, float Ready, float Release);
	constexpr float ThrowClipRelease = 1.6f;

	/**
	 * The captured dive (Mixamo "Goalkeeper Diving Save", hands at full stretch DiveClipStretch s in) for a take at
	 * FieldTime: the hands reach out on the take, then the fielder lies and gets up.
	 */
	FClipPlay DiveClip(float Post, float FieldTime);
	constexpr float DiveClipStretch = 1.25f, DiveClipLanded = 1.6f, DiveClipUp = 3.2f;

	/**
	 * A hand windmilling about the shoulder for a bowling or throwing action: 0 straight up, positive
	 * angles over the top toward Forward, -180 straight down.
	 */
	FVector ArmCircle(const FVector& Shoulder, const FVector& Forward, float AngleDeg, float Reach);

	/**
	 * Bowling arm angle from the time to release (negative before): swung down and back in the gather, over
	 * the top to just past vertical at release, then down across the body.
	 */
	float BowlingArmAngle(float TimeToRelease);
	/** How much of the bowling action is layered over the run-up (0 during the approach). */
	float BowlingArmWeight(float TimeToRelease);
	constexpr float ReleaseAngle = 10.f;
}
