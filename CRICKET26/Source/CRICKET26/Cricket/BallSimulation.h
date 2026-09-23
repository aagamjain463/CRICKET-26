// Authoritative cricket-ball physics. Fixed-step, deterministic, independent of frame rate and
// rendering. Frame and units: see CricketTypes.h.
//
// Forces in flight: gravity, quadratic drag, Magnus (spin x velocity) and a seam-driven swing force.
// Ground contact: rigid-sphere impulse model with restitution and Coulomb friction acting on the
// contact-point slip velocity, so spin turn, top-spin kick and skid all fall out of one rule.

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.h"
#include "BallSimulation.generated.h"

USTRUCT(BlueprintType)
struct FBallState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FVector Pos = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector Vel = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector Spin = FVector::ZeroVector; // rad/s
	UPROPERTY(BlueprintReadOnly) float SwingAccel = 0.f;              // lateral m/s^2 (+Y), until first bounce
	UPROPERTY(BlueprintReadOnly) float SeamKick = 0.f;                // lateral m/s added at the first pitch bounce
	UPROPERTY(BlueprintReadOnly) int32 Bounces = 0;
	UPROPERTY(BlueprintReadOnly) bool bRolling = false;
	UPROPERTY(BlueprintReadOnly) float Time = 0.f;
};

struct FSurface
{
	float Restitution = 0.5f;
	float Friction = 0.35f;
	float RollingDecel = 1.1f; // m/s^2 once the ball is rolling
};

struct FPitchConditions
{
	// Vertical restitution is the effective value for an oblique impact at bowling speed, calibrated so
	// a 135 km/h ball passes the crease at ~0.75 m off a 6 m length and ~1.45 m off 10 m (ball-tracking).
	FSurface Pitch{ 0.63f, 0.32f, 1.5f };
	FSurface Outfield{ 0.38f, 0.45f, 1.1f };
};

namespace CricketBall
{
	constexpr float FixedDt = 1.f / 240.f;

	enum class EStep : uint8 { None, Bounce, Stopped };

	/** Advances one fixed step. */
	EStep Step(FBallState& Ball, const FPitchConditions& Conditions, float Dt = FixedDt);

	/** Magnus-free ballistic prediction of where a ball will be when it reaches plane X = PlaneX,
	 *  as a batter would read it (drag, gravity, a nominal bounce; no swing, seam or spin). */
	bool PredictAtPlane(FBallState Ball, float PlaneX, const FPitchConditions& Conditions, FVector& OutPos, float& OutTime);

	/** Simulates until PlaneX is crossed (moving toward -X). Returns false if the ball stops first. */
	bool SimulateToPlane(FBallState& Ball, float PlaneX, const FPitchConditions& Conditions, float MaxTime = 3.f);

	/** Spin vector producing a Magnus force along Direction for a ball moving along Velocity. */
	FVector SpinForMagnus(const FVector& Velocity, const FVector& Direction, float RadPerSec);
}

/** What the bowler is trying to do. Line is batter-relative: metres toward the batter's off side from middle stump. */
USTRUCT(BlueprintType)
struct FDeliveryPlan
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EDeliveryType Type = EDeliveryType::Stock;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Line = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Length = 6.f; // pitching distance from the striker's stumps
};

struct FDeliveryRelease
{
	FBallState Ball;
	bool bNoBall = false;
	float SpeedKph = 0.f;
	FVector2D AimedPitch = FVector2D::ZeroVector; // after execution error, before movement
};

namespace CricketBowling
{
	/** Delivery types available to a bowler style. */
	TArray<EDeliveryType> Repertoire(EBowlerType Type);

	/**
	 * Turns intent + execution into a physical release. ReleaseTiming is the signed error of the
	 * release input in [-1, 1]: 0 is perfect, negative is early (overpitched), positive is late
	 * (dragged down, and beyond 0.85 the front foot oversteps: no-ball).
	 * Execution scatter is drawn from Seed, so the same inputs always produce the same ball.
	 */
	FDeliveryRelease Execute(const FCricketPlayer& Bowler, ECricketHand BatterHand, const FDeliveryPlan& Plan,
		float ReleaseTiming, int32 Seed, const FPitchConditions& Conditions);
}
