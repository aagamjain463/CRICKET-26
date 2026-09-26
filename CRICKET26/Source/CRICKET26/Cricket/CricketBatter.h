// The striker's whole body through a delivery, as geometry: stance, backlift, trigger, the stroke's footwork,
// the weight moving onto the planted foot, the kinetic chain (hips, then chest, then arms and bat), the
// follow-through and the recovery into the stance. The stroke is built backwards from the simulation's
// contact, so the sweet spot is on the ball at the moment of contact whatever the body does.
//
// Everything is authored in a batter frame (toward the bowler, toward the off side, up) and mirrored into the
// simulation frame at the end, so a left-hander is the exact mirror of a right-hander. Feet only move while
// stepping (off the ground); planted, a foot can only pivot on its ball. Pure geometry with no engine state:
// UCricketAnimInstance solves the skeleton to it.

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.h"
#include "CricketPose.h"

namespace CricketBatter
{
	struct FFoot
	{
		FVector Ball = FVector::ZeroVector;   // the ball of the foot, on the ground (m)
		FVector Toe = FVector::ForwardVector; // horizontal: the way the toes point
		float Heel = 0.f;                     // heel raised, pivoting on the ball (deg)
		float Lift = 0.f;                     // off the ground mid-step (m); planted at 0
	};

	struct FBody
	{
		FVector Pelvis = FVector::ZeroVector;  // the ground under the pelvis
		float Drop = 0.f;                      // pelvis below its standing height (m): the knees' bend
		FVector Hips = FVector::ForwardVector; // horizontal facings of the hips and the chest
		FVector Chest = FVector::ForwardVector;
		FVector ChestUp = FVector::UpVector;   // the ribcage's up: forward bend and side bend
		FFoot Foot[2];                         // [0] left, [1] right
		CricketPose::FBat Bat;
		int32 TopHand = 0;                     // the top hand, whose side's foot is the front foot
		FVector Shoulder[2] = { FVector::ZeroVector, FVector::ZeroVector }; // a nominal body's shoulders, for reach
	};

	struct FInput
	{
		float Off = 1.f;                       // the off side's Y sign: + for a right-hander
		FVector Home = FVector::ZeroVector;    // stance centre, on the ground
		float Time = -100.f;                   // s after release; negative before it (far negative: waiting)
		float Clock = 0.f;                     // free-running seconds, for the stance's breathing
		bool bStroke = false;                  // a stroke is being played
		bool bLeave = false;                   // or the ball is being left: bat up out of the way
		EShotType Shot = EShotType::Defend;
		EFootwork Foot = EFootwork::Front;
		float DirectionDeg = 0.f;              // + to the off side
		float Press = 0.f, Impact = 0.f;       // s after release: the stroke starts, and meets the ball
		FVector Contact = FVector::ZeroVector; // the sweet spot at Impact
		FVector ShotDir = FVector::ForwardVector; // the ball's way off the bat
		float Settle = 1e9f;                   // s after release: back into the stance from here
	};

	constexpr float RecoverSeconds = 0.7f;
	constexpr float Reach = 0.58f;             // nominal shoulder to the middle of a hand on the handle (m)
	constexpr float MinReach = 0.22f;          // and the closest a hand comes to its shoulder, the elbow folded

	/** The body at In.Time. */
	FBody Plan(const FInput& In);
}
