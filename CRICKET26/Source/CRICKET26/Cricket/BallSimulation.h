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
	float Swing = 1.f; // scales the swing in the air: cloud cover helps it, dew on the ball takes some away
	float Seam = 1.f;  // scales the movement off the seam: grass on the pitch helps it
	float Grip = 1.f;  // scales how much of a spinner's revs the pitch turns into turn: dust and wear raise it
	float Rough = 0.f; // 0 smooth to 1 torn up: how much harder the footmarks outside the stumps grip, and how much lower they keep
};

/** A pitch's character, set by the venue. */
enum class EPitchType : uint8 { Green, Flat, Dusty };

namespace CricketBall
{
	constexpr float FixedDt = 1.f / 240.f;

	/**
	 * The conditions for a pitch of this type, worn by Wear (0 fresh, 1 at the end of the match: the surface breaks
	 * up, keeps lower and grips more), under Cloud (0 clear, 1 overcast), with or without dew on the outfield.
	 * The flat, fresh, clear, dry pitch is the default FPitchConditions.
	 */
	FPitchConditions Conditions(EPitchType Type, float Wear = 0.f, float Cloud = 0.f, bool bDew = false);

	/** The rough: footmarks outside the stumps on a spinner's length at the batter's end, dug by bowlers following
	 *  through from the other end. The pitch material draws the same patches. */
	bool IsInRough(const FVector& P);

	enum class EStep : uint8 { None, Bounce, Stopped };

	/** Advances one fixed step. */
	EStep Step(FBallState& Ball, const FPitchConditions& Conditions, float Dt = FixedDt);

	/** Magnus-free ballistic prediction of where a ball will be when it reaches plane X = PlaneX,
	 *  as a batter would read it (drag, gravity, a nominal bounce; no swing, seam or spin). */
	bool PredictAtPlane(FBallState Ball, float PlaneX, const FPitchConditions& Conditions, FVector& OutPos, float& OutTime);

	/** Simulates until PlaneX is crossed (moving toward -X). Returns false if the ball stops first. Path, when given,
	 *  gets the position after every step, ending on the plane. */
	bool SimulateToPlane(FBallState& Ball, float PlaneX, const FPitchConditions& Conditions, float MaxTime = 3.f, TArray<FVector>* Path = nullptr);

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
	EDeliveryType Type = EDeliveryType::Stock;
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
