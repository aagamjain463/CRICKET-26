// The body animation for every player and umpire: the mannequin's idle, jog and sprint, blended by speed, with
// the cricket actions layered on top by IK. The game fills FCricketBodyPose each frame in world space; the
// anim thread drops the pelvis (feet stay planted), turns and bends the chest, turns the head to look, and
// puts each hand on its target. Captured clips can take over the body for a stroke, dive or throw (Clip), with
// the same actions layered over them. No animation blueprint is needed.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "CricketAnimInstance.generated.h"

class UAnimSequence;

struct FCricketBodyPose
{
	float JogWeight = 0.f, JogRate = 1.f;
	float SprintWeight = 0.f, SprintRate = 1.f; // share of the jog that is a sprint, and its playback rate
	FVector PelvisOffset = FVector::ZeroVector; // world cm
	FVector ChestFacing = FVector::ZeroVector;  // world direction to turn the chest toward (zero: no turn)
	float ChestBend = 0.f;                      // degrees forward
	float LookWeight = 0.f;
	FVector LookAt = FVector::ZeroVector;
	float HandWeight[2] = { 0.f, 0.f };         // [0] left, [1] right
	FVector Hand[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector Elbow[2] = { FVector::ZeroVector, FVector::ZeroVector }; // where each elbow should point
	// Captured actions over the idle and jog, kept alive by the game mode: [0] a stroke or a dive, [1] a throw over it.
	UAnimSequence* Clip[2] = { nullptr, nullptr };
	float ClipTime[2] = { 0.f, 0.f }, ClipWeight[2] = { 0.f, 0.f };
	FVector ShouldersAt = FVector::ZeroVector; // where the point between the shoulders should be: the whole upper body
	float ShouldersWeight = 0.f;               // moves there over planted feet, e.g. to keep a clip's hands on the bat

	/** Keeps the locomotion, clears the actions. */
	void ClearActions()
	{
		const FCricketBodyPose Keep = *this;
		*this = FCricketBodyPose();
		JogWeight = Keep.JogWeight;
		JogRate = Keep.JogRate;
		SprintWeight = Keep.SprintWeight;
		SprintRate = Keep.SprintRate;
	}
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
	UAnimSequence* Sprint = nullptr;
	double IdleTime = 0.0, JogTime = 0.0, SprintTime = 0.0;
	FCricketBodyPose Pose; // component space, converted on the game thread
};

UCLASS(Transient, NotBlueprintable)
class UCricketAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY() TObjectPtr<UAnimSequence> Idle;
	UPROPERTY() TObjectPtr<UAnimSequence> Jog;
	UPROPERTY() TObjectPtr<UAnimSequence> Sprint; // optional
	FCricketBodyPose Pose; // world space

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FCricketAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override { delete Proxy; }
};
