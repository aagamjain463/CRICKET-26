// Batting: shot selection, the batter's read of the ball, and bat-ball contact.
//
// The player chooses intent (defend / ground / loft / leave), direction and timing. The batter's
// body places the bat where they *read* the ball will be, committing ReadLead seconds before it
// arrives; movement after that moment (late swing, seam or turn off the pitch) is not in the read.
// Contact is then resolved geometrically between the actual ball and that bat, so edges, toe-ends
// and misses are consequences of the delivery, the read and the timing - never dice rolls.

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.h"
#include "BallSimulation.h"

struct FShotProfile
{
	EShotType Shot = EShotType::Leave;
	EFootwork Foot = EFootwork::Front;
	bool bCrossBat = false;
	bool bSoftHands = false;
	float MinZ = 0.f, MaxZ = 1.f;            // heights the bat can get to (m)
	float ReachMin = -0.3f, ReachMax = 0.7f; // batter-relative lateral reach, + toward off side (m)
	float BatSpeed = 0.f;                    // m/s at the sweet spot
	float LoftDeg = 0.f;
	float DirMin = -180.f, DirMax = 180.f;   // allowed shot directions (deg, 0 = straight, + = off side)
	float SwingTime = 0.25f;                 // input-to-contact time (s)
	float ContactX() const { return Foot == EFootwork::Front ? 2.0f : 1.0f; }
};

struct FContactResult
{
	EShotType Shot = EShotType::Leave;
	EContactZone Zone = EContactZone::Miss;
	float TimingError = 0.f; // s, + late
	FVector ContactPos = FVector::ZeroVector;
	FVector ExitVel = FVector::ZeroVector;
	float ContactTime = 0.f;
	bool HasContact() const { return Zone != EContactZone::Miss; }
};

namespace CricketBatting
{
	const FShotProfile& Profile(EShotType Shot, EFootwork Foot = EFootwork::Front);

	/** Seconds before arrival at which the batter commits the bat's position. */
	float ReadLead(const FCricketPlayer& Batter);

	/** Contextual shot choice from intent, direction and the batter's read of the length. */
	FShotProfile ChooseShot(EBatIntent Intent, float DirectionDeg, float ReadPitchX, float ReadHeightAtBat, EBowlerType BowlerType);

	/**
	 * Resolves contact of the actual ball (state at the shot's contact plane) with a bat placed at
	 * AimPos (the read). DirectionDeg is the intended direction, TimingError is + late.
	 */
	FContactResult ResolveContact(const FBallState& BallAtPlane, const FVector& AimPos, const FShotProfile& Shot,
		float DirectionDeg, float TimingError, const FCricketPlayer& Batter, bool bAerial);

	/** Batter-relative direction (deg) to world-frame unit vector on the ground. */
	FVector DirectionToWorld(float DirectionDeg, ECricketHand BatHand);
}
