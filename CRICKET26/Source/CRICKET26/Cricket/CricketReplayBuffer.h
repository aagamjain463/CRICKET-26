// Rolling replay buffer: faithful visual capture of the ACTUAL delivery for replay playback.
//
// The resolver's stored ball path already replays the same ball; this buffer goes further and records
// what was REALLY on screen each tick (ball + every figure's presented transform, keyed by ball time)
// so a replay reproduces the event instead of re-posing an approximation of it.
//
// Bounded by design (mobile, §42): fixed sample rate, fixed window, capped actor count. Recording is
// O(actors) per sample with no allocation on the hot path (ring overwrite). Playback interpolates.

#pragma once

#include "CoreMinimal.h"
#include "CricketReplayBuffer.generated.h"

/** One actor's presented transform, world units (cm). Rotation matters: dives tip bodies. */
USTRUCT(BlueprintType)
struct FReplayActorPose
{
	GENERATED_BODY()
	UPROPERTY() FVector Position = FVector::ZeroVector;
	UPROPERTY() FQuat Rotation = FQuat::Identity;
	UPROPERTY() FVector Scale = FVector::OneVector;
};

USTRUCT(BlueprintType)
struct FReplayBufferFrame
{
	GENERATED_BODY()
	/** Ball time (s after release; DeadTime once the ball is dead). */
	UPROPERTY() float BallT = 0.f;
	UPROPERTY() FVector BallPos = FVector::ZeroVector;
	UPROPERTY() FVector BallVel = FVector::ZeroVector; // cm/s
	UPROPERTY() TArray<FReplayActorPose> Actors;       // fixed order, fixed count
};

/**
 * FCricketReplayBuffer: a time-keyed ring of presented frames plus event markers.
 * Plain value type; the game mode owns one and feeds it while live, reads it during replays.
 */
USTRUCT(BlueprintType)
struct FCricketReplayBuffer
{
	GENERATED_BODY()

	/** Seconds of history kept. Old frames fall off; memory never grows. */
	UPROPERTY(EditAnywhere, Category = "Replay Buffer") float Window = 14.f;
	/** Fixed recording rate (Hz). Timestamps interpolate on playback: any display rate reads clean. */
	UPROPERTY(EditAnywhere, Category = "Replay Buffer") float Rate = 30.f;
	/** Max actors recorded (ball excluded). Extra actors are ignored, deterministically (first N win). */
	UPROPERTY(EditAnywhere, Category = "Replay Buffer") int32 MaxActors = 20;

	void Reset();
	/** Rough memory footprint (bytes) for the perf notes. */
	int64 FootprintBytes() const;
	int32 NumFrames() const { return Frames.Num(); }
	/** Newest stored ball time (logical order: the ring wraps, so this is not the physical last). */
	float LatestT() const { return Frames.Num() ? FrameAt(Frames.Num() - 1)->BallT : -1.f; }
	float EarliestT() const { return Frames.Num() ? Frames[Head].BallT : -1.f; }

	/**
	 * Record a presented frame. Throttled to Rate internally: call every tick, it keeps every Nth.
	 * Actors must arrive in a STABLE order (same actor, same index, every call).
	 */
	void Record(float BallT, const FVector& BallPos, const FVector& BallVel, const TArray<FReplayActorPose>& Actors);

	/** True when every ball time in [From, To] can be interpolated (with a half-sample margin). */
	bool HasCoverage(float From, float To) const;

	/**
	 * Interpolate the presented state at BallT. Returns false outside coverage (caller falls back
	 * to analytic posing from the resolver result).
	 */
	bool SampleAt(float BallT, FVector& OutBallPos, TArray<FReplayActorPose>& OutActors) const;

	/** Event markers (release, contact, broken stumps, boundary) for directors and debug. */
	void MarkEvent(float BallT, const FString& Tag);
	const TArray<TPair<float, FString>>& Events() const { return EventMarks; }

private:
	UPROPERTY() TArray<FReplayBufferFrame> Frames;
	UPROPERTY() int32 Head = 0; // index of the oldest frame (ring)
	UPROPERTY() int32 ActorCount = 0;
	float LastRecordedT = -1e9f;
	/** Integer tick of the last recorded sample: exact at any rate (2*(1/60) < 1/30 in float). */
	int64 LastTick = -1;
	TArray<TPair<float, FString>> EventMarks; // transient debug markers: TPair is not reflected, so no UPROPERTY

	int32 Capacity() const;
	const FReplayBufferFrame* FrameAt(int32 Logical) const;
};
