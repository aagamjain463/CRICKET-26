// Shared cricket vocabulary: geometry, enums and player data.
//
// Simulation frame (metres, seconds): X runs along the pitch from the striker's stumps (X=0)
// to the bowler's stumps (X=20.12), Z is up with the ground at Z=0, and Y is lateral.
// In Unreal's (left-handed) world a right-handed batter facing the bowler has the off side at +Y,
// so OffSideSign(Right) = +1. Presentation maps this frame to world space with a pure scale.

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.generated.h"

namespace CricketGeo
{
	constexpr float PitchLength = 20.12f;
	constexpr float PitchHalfWidth = 1.52f;
	constexpr float PoppingCrease = 1.22f;
	constexpr float StumpHeight = 0.711f;
	constexpr float StumpsHalfWidth = 0.114f;
	constexpr float BallRadius = 0.036f;
	constexpr float BallMass = 0.156f;
	constexpr float WideLineOff = 0.89f;   // off-side wide guideline from middle stump
	constexpr float WideLineLeg = 0.40f;   // T20: past leg stump and clear of the batter
	constexpr float BoundaryRadius = 65.f; // from pitch centre
	constexpr float Gravity = 9.81f;
	// Striker standing upright at the popping crease: the umpire's reference heights.
	constexpr float WaistHeight = 1.0f;    // full toss above this is a no-ball
	constexpr float ShoulderHeight = 1.45f; // short ball above this counts as a bouncer
	constexpr float HeadHeight = 1.75f;
	inline FVector PitchCentre() { return FVector(PitchLength * 0.5f, 0.f, 0.f); }
}

namespace CricketMath
{
	/** Standard normal sample (Box-Muller) from a seeded stream, so execution error stays reproducible. */
	inline float Gauss(FRandomStream& R)
	{
		const float U1 = FMath::Max(R.GetFraction(), 1e-6f);
		return FMath::Sqrt(-2.f * FMath::Loge(U1)) * FMath::Cos(2.f * PI * R.GetFraction());
	}

	/**
	 * When a key seen at the start of a frame actually went down: somewhere in the Dt just gone, so the
	 * midpoint is the unbiased estimate at any frame rate (error within +-Dt/2, mean zero).
	 */
	inline float PressTime(float FrameStartTime, float Dt) { return FrameStartTime + 0.5f * Dt; }
}

UENUM(BlueprintType)
enum class ECricketHand : uint8 { Right, Left };

inline float OffSideSign(ECricketHand BatHand) { return BatHand == ECricketHand::Right ? 1.f : -1.f; }

UENUM(BlueprintType)
enum class EBowlerType : uint8 { Pace, OffSpin, LegSpin };

UENUM(BlueprintType)
enum class EDeliveryType : uint8
{
	Stock, Outswing, Inswing, Cutter, Slower,          // pace
	OffBreak, ArmBall, LegBreak, Googly, TopSpinner,   // spin
	Seam, CrossSeam,                                   // pace: seam-up and scrambled seam
	Slider                                             // leg spin: backspin, skids on
};

/** What the batter intends; the concrete shot is chosen from the read of the ball. */
UENUM(BlueprintType)
enum class EBatIntent : uint8 { Leave, Defend, Ground, Loft };

UENUM(BlueprintType)
enum class EShotType : uint8 { Leave, Defend, Drive, Loft, Punch, Cut, Pull, Sweep, Flick, Hook, SlogSweep, ReverseSweep, Scoop };

UENUM(BlueprintType)
enum class EFootwork : uint8 { Front, Back, Advance }; // Advance: down the track, out of the crease

UENUM(BlueprintType)
enum class EContactZone : uint8
{
	Miss, Middle, InnerHalf, OuterHalf, Toe, Upper, InsideEdge, OutsideEdge, TopEdge, BottomEdge
};

UENUM(BlueprintType)
enum class EDismissal : uint8 { None, Bowled, Caught, LBW, RunOut, Stumped, HitWicket };

USTRUCT(BlueprintType)
struct FCricketPlayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ECricketHand BatHand = ECricketHand::Right;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ECricketHand BowlHand = ECricketHand::Right;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EBowlerType BowlerType = EBowlerType::Pace;

	// Attributes are 0..1 unless stated.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Timing = 0.6f;    // widens the good-contact window
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Technique = 0.6f; // how late the batter can adjust to movement
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Power = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Accuracy = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float PaceKph = 135.f;  // stock-ball release speed
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Movement = 0.6f;  // swing/seam or spin revs
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Catching = 0.7f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Throwing = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float RunSpeed = 7.f;   // m/s top speed
};

USTRUCT(BlueprintType)
struct FCricketTeam
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Short;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FCricketPlayer> Batters; // Super Over batting order (3)
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FCricketPlayer Bowler;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor Colour = FLinearColor::Blue;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Sponsor; // printed across the shirt's chest
};
