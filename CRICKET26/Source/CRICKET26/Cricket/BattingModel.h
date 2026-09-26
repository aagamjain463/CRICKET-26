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
	float AdvanceX = 3.0f;                   // down the track: where the batter meets the ball (m from the stumps)
	// Where the ball is met (m from the stumps). A forward defence meets it later than a drive, under the eyes
	// beside the front pad, with the handle ahead of the blade.
	float ContactX() const
	{
		return Foot == EFootwork::Advance ? AdvanceX : Foot == EFootwork::Back ? 1.0f : Shot == EShotType::Defend ? 1.8f : 2.0f;
	}
};

struct FContactResult
{
	EShotType Shot = EShotType::Leave;
	EContactZone Zone = EContactZone::Miss;
	float TimingError = 0.f; // s, + late
	FVector ContactPos = FVector::ZeroVector;
	FVector ExitVel = FVector::ZeroVector;
	FVector BatPos = FVector::ZeroVector; // where the batter put the sweet spot on the contact plane (zero: no stroke)
	float ContactTime = 0.f;
	float Quality = 0.f;     // 0..1: timing and how near the sweet spot, i.e. the share of a perfect hit's energy
	bool HasContact() const { return Zone != EContactZone::Miss; }
};

namespace CricketBatting
{
	const FShotProfile& Profile(EShotType Shot, EFootwork Foot = EFootwork::Front);

	/** Seconds before arrival at which the batter commits the bat's position. */
	float ReadLead(const FCricketPlayer& Batter);

	/**
	 * Contextual shot choice from intent, direction and the batter's read of the length. Committing
	 * LeadTime seconds before the ball arrives: against spin, an attacking stroke committed that early
	 * (before the ball has pitched) means using the feet - down the track, out of the crease.
	 */
	FShotProfile ChooseShot(EBatIntent Intent, float DirectionDeg, float ReadPitchX, float ReadHeightAtBat, EBowlerType BowlerType,
		float LeadTime = 0.f);

	/** Commitment lead (s) beyond which an attacking stroke against spin goes down the track. */
	constexpr float AdvanceLead = 0.5f;

	/**
	 * Resolves contact of the actual ball (state at the shot's contact plane) with a bat placed at
	 * AimPos (the read). DirectionDeg is the intended direction, TimingError is + late.
	 */
	FContactResult ResolveContact(const FBallState& BallAtPlane, const FVector& AimPos, const FShotProfile& Shot,
		float DirectionDeg, float TimingError, const FCricketPlayer& Batter, bool bAerial);

	/** Batter-relative direction (deg) to world-frame unit vector on the ground. */
	FVector DirectionToWorld(float DirectionDeg, ECricketHand BatHand);
}
