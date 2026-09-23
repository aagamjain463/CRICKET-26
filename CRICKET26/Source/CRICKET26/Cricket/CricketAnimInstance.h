// The body animation for every player and umpire: the mannequin's idle and jog, blended by speed, with
// the cricket actions layered on top by IK. The game fills FCricketBodyPose each frame in world space; the
// anim thread drops the pelvis (feet stay planted), turns and bends the chest, turns the head to look, and
// puts each hand on its target. No animation blueprint or authored cricket clip is needed.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "CricketAnimInstance.generated.h"

class UAnimSequence;

struct FCricketBodyPose
{
	float JogWeight = 0.f, JogRate = 1.f;
	FVector PelvisOffset = FVector::ZeroVector; // world cm
	FVector ChestFacing = FVector::ZeroVector;  // world direction to turn the chest toward (zero: no turn)
	float ChestBend = 0.f;                      // degrees forward
	float LookWeight = 0.f;
	FVector LookAt = FVector::ZeroVector;
	float HandWeight[2] = { 0.f, 0.f };         // [0] left, [1] right
	FVector Hand[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector Elbow[2] = { FVector::ZeroVector, FVector::ZeroVector }; // where each elbow should point

	/** Keeps the locomotion, clears the actions. */
	void ClearActions() { const float W = JogWeight, R = JogRate; *this = FCricketBodyPose(); JogWeight = W; JogRate = R; }
};

struct FCricketAnimProxy : public FAnimInstanceProxy
{
	FCricketAnimProxy() = default;
	FCricketAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	void ApplyActions(FPoseContext& Output) const;

	UAnimSequence* Idle = nullptr;
	UAnimSequence* Jog = nullptr;
	double IdleTime = 0.0, JogTime = 0.0;
	FCricketBodyPose Pose; // component space, converted on the game thread
};

UCLASS(Transient, NotBlueprintable)
class UCricketAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY() TObjectPtr<UAnimSequence> Idle;
	UPROPERTY() TObjectPtr<UAnimSequence> Jog;
	FCricketBodyPose Pose; // world space

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FCricketAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override { delete Proxy; }
};
