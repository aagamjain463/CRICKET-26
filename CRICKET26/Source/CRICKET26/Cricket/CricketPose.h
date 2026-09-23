// Cricket actions as geometry. A stroke is the bat turning rigidly about a pivot between the batter's
// shoulders, planned so that at angle 0 the sweet spot is exactly where the simulation says the ball met
// the bat: the swing is built backwards from the contact, so bat and ball meet on screen on the frame the
// simulation resolves the contact. The bowling and throwing arm windmills about the shoulder in the plane
// of the delivery. Everything is in the simulation frame (metres); the anim layer puts the hands on the
// bat with IK.

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.h"

namespace CricketPose
{
	constexpr float BatLength = 0.85f;     // the drawn bat
	constexpr float HandleLength = 0.29f;
	constexpr float GripFromTop = 0.12f;   // centre of the two hands on the handle
	constexpr float SweetFromGrip = 0.55f; // grip to the middle of the sweet spot
	constexpr float MaxGripReach = 0.55f;  // pivot to grip with the arms extended
	constexpr float MinGripReach = 0.3f;   // pivot to grip with the arms bent in to the chest
	constexpr float MaxLean = 0.55f;       // how far the upper body can lean into a stroke (waist and front knee)
	constexpr float FoldedReach = 0.6f;    // share of the contact reach left with the arms folded (bat up)

	struct FBat
	{
		FVector Grip = FVector::ZeroVector;
		FVector Axis = -FVector::UpVector;  // grip toward the toe
		FVector Face = FVector::ForwardVector; // the way the blade's face points
		FVector SweetSpot() const { return Grip + Axis * SweetFromGrip; }
		FVector Top() const { return Grip - Axis * GripFromTop; }
	};

	struct FSwing
	{
		FVector Pivot = FVector::ZeroVector;   // after any lean toward the ball
		FVector Lean = FVector::ZeroVector;    // the upper body's shift to reach the contact
		FVector Contact = FVector::ZeroVector;
		FVector Spin = FVector::RightVector;   // rotation axis: positive angles move the bat along the stroke
		FVector Grip0 = FVector::ZeroVector;   // grip at the moment of contact
		FVector Face0 = FVector::ForwardVector;
		float Backlift = 110.f, Follow = 140.f; // degrees either side of the contact
		bool bHold = false;                     // a leave: the bat stays up out of the way
	};

	/**
	 * Plans a stroke that meets the ball at Contact, driving it along BallDir. Pivot is the point between
	 * the shoulders at address. When the contact is out of the arms' reach the upper body leans toward it,
	 * and when it is too close the body sways away from it (either at most MaxLean); the plan's Pivot and
	 * Lean say by how much.
	 */
	FSwing PlanSwing(EShotType Shot, const FVector& Pivot, const FVector& Contact, const FVector& BallDir);

	/** The bat turned AngleDeg about the swing's axis from its contact pose. */
	FBat BatAt(const FSwing& Swing, float AngleDeg);

	/** Blends two bat poses (0 = A). */
	FBat Blend(const FBat& A, const FBat& B, float Alpha);

	/**
	 * Swing angle at time T: held at the backlift until PressTime, an accelerating downswing that reaches 0
	 * exactly at ImpactTime, then an easing follow-through.
	 */
	float StrokeAngle(const FSwing& Swing, float T, float PressTime, float ImpactTime);
	constexpr float FollowSeconds = 0.35f;

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
