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

// The striker's whole body from CricketBatter::Plan, in world space (cm). Solved bone by bone over the idle:
// pelvis and hips, the spine sharing the chest's turn and bend, the legs to feet placed on the ground, the
// head to the ball, and the arms to hands gripping the bat, each elbow swivelled clear of the torso.
struct FCricketBatterPose
{
	float Weight = 0.f;                   // 0: the idle and the actions below; 1: all this
	FVector Pelvis = FVector::ZeroVector; // the ground under the pelvis
	float Drop = 0.f;                     // pelvis below its standing height, for a body whose pelvis stands 97 cm up
	FVector Hips = FVector::ForwardVector, Chest = FVector::ForwardVector, ChestUp = FVector::UpVector;
	FVector Ball[2] = { FVector::ZeroVector, FVector::ZeroVector }; // [0] left, [1] right: the ball of each foot, on the ground
	FVector Toe[2] = { FVector::ForwardVector, FVector::ForwardVector };
	float Heel[2] = { 0.f, 0.f };         // degrees raised, pivoting on the ball
	float Lift[2] = { 0.f, 0.f };         // cm off the ground, mid-step
	FVector Grip = FVector::ZeroVector, BatAxis = -FVector::UpVector, BatFace = FVector::ForwardVector;
	int32 TopHand = 0;
	// No bat (the keeper): each palm's centre on Hand, its elbow toward Elbow, the palms to Chest's facing,
	// the fingers down below the chest and up above it.
	bool bGloves = false;
};

/** How a hand's fingers are held over the clip's: as the clip has them, open and flat, a fist, or a fist with the forefinger out. */
enum class ECricketHandShape : uint8 { Clip, Open, Fist, Point };

struct FCricketBodyPose
{
	bool bKeeper = false;
	float WalkWeight = 0.f, WalkRate = 1.f;
	float JogWeight = 0.f, JogRate = 1.f;
	float SprintWeight = 0.f, SprintRate = 1.f; // share of the jog that is a sprint, and its playback rate
	FVector PelvisOffset = FVector::ZeroVector; // world cm
	// Where the feet go instead of where the idle puts them, e.g. forward of a chair with the pelvis dropped onto its
	// seat; the knees then bend forward and up (sitting), not down (a crouch).
	float FootWeight = 0.f;
	FVector Foot[2] = { FVector::ZeroVector, FVector::ZeroVector }; // world cm, [0] left, [1] right
	FVector ChestFacing = FVector::ZeroVector;  // world direction to turn the chest toward (zero: no turn)
	float ChestBend = 0.f;                      // degrees forward
	float LookWeight = 0.f;
	FVector LookAt = FVector::ZeroVector;
	float HandWeight[2] = { 0.f, 0.f };         // [0] left, [1] right
	FVector Hand[2] = { FVector::ZeroVector, FVector::ZeroVector };
	FVector Elbow[2] = { FVector::ZeroVector, FVector::ZeroVector }; // where each elbow should point
	FVector PalmFacing[2] = { FVector::ZeroVector, FVector::ZeroVector }; // world direction out of each palm; zero keeps clip rotation
	FVector FingerFacing[2] = { FVector::ZeroVector, FVector::ZeroVector }; // world direction from wrist toward fingers
	ECricketHandShape HandShape[2] = { ECricketHandShape::Clip, ECricketHandShape::Clip }; // with a facing: the fingers' shape
	// Captured actions over the idle and jog, kept alive by the game mode: [0] a stroke or a dive, [1] a throw over it.
	UAnimSequence* Clip[2] = { nullptr, nullptr };
	float ClipTime[2] = { 0.f, 0.f }, ClipWeight[2] = { 0.f, 0.f };
	FVector ShouldersAt = FVector::ZeroVector; // where the point between the shoulders should be: the whole upper body
	float ShouldersWeight = 0.f;               // moves there over planted feet, e.g. to keep a clip's hands on the bat
	FCricketBatterPose Batter;

	/** Keeps the locomotion, clears the actions. */
	void ClearActions()
	{
		const FCricketBodyPose Keep = *this;
		*this = FCricketBodyPose();
		WalkWeight = Keep.WalkWeight;
		WalkRate = Keep.WalkRate;
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
	void SolveBatter(FCompactPose& Out);

	// The skeleton's reference pose in component space and what the batter solve needs of it, built once per
	// bone container. Indices are the mesh's bone indices, INDEX_NONE if it lacks the bone; the geometry comes from
	// the whole skeleton, so a distant LOD without fingers or twist bones still solves (and simply skips them).
	struct FBatterRig
	{
		uint16 Serial = 0;
		bool bValid = false;
		TArray<FTransform> Ref;
		TArray<int32> Compact; // mesh bone index to this LOD's compact pose index, INDEX_NONE if not in it
		int32 Pelvis = INDEX_NONE, Head = INDEX_NONE;
		TArray<int32> Spine, Neck;
		int32 Clavicle[2], Upper[2], Lower[2], Hand[2], Twist[2][2], Thigh[2], Calf[2], Foot[2], Ball[2];
		int32 Finger[2][5][3];
		FVector Across[2], Palm[2], GripOffset[2]; // per hand: index to pinky knuckle, out of the palm, hand bone to the handle's centre
		FVector Fingers[2], PalmOffset[2]; // the way the fingers point, and hand bone to the middle of a glove's palm
		FVector CurlAxis[2][5];
		float PelvisZ = 97.f;
	};
	struct FActionsRig
	{
		uint16 Serial = 0;
		bool bValid = false;
		FCompactPoseBoneIndex Pelvis = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex Spine1 = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex Spine3 = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex Neck = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex Head = FCompactPoseBoneIndex(INDEX_NONE);
		FCompactPoseBoneIndex Thigh[2] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
		FCompactPoseBoneIndex Calf[2] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
		FCompactPoseBoneIndex Foot[2] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
		FCompactPoseBoneIndex Clavicle[2] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
		FCompactPoseBoneIndex Upper[2] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
		FCompactPoseBoneIndex Lower[2] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
		FCompactPoseBoneIndex Hand[2] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
	};
	void BuildRig(const FBoneContainer& Bones) const;
	mutable FActionsRig ActionsRig;
	mutable FBatterRig Rig;
	float Swivel[2] = { 0.f, 0.f }, Roll[2] = { 0.f, 0.f }; // last frame's arm solution, for continuity
	bool bSolved = false;
	FVector LastGrip = FVector::ZeroVector; // component cm: a jump past 30 cm is a new pose, not a frame's motion
	float DeltaTime = 1.f / 60.f;
	// Forearm twist and wrist bend past these (degrees) read as a wrung-out arm; roll and swivel turn at most this fast (rad/s).
	static constexpr float MaxTwist = 85.f, MaxBend = 70.f, RollRate = 2.f * PI, SwivelRate = 4.f * PI;

	UAnimSequence* Idle = nullptr;
	UAnimSequence* Walk = nullptr;
	UAnimSequence* Jog = nullptr;
	UAnimSequence* Sprint = nullptr;
	double IdleTime = 0.0, WalkTime = 0.0, JogTime = 0.0, SprintTime = 0.0;
	FCricketBodyPose Pose; // component space, converted on the game thread
};

UCLASS(Transient, NotBlueprintable)
class UCricketAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY() TObjectPtr<UAnimSequence> Idle;
	UPROPERTY() TObjectPtr<UAnimSequence> Walk; // optional
	UPROPERTY() TObjectPtr<UAnimSequence> Jog;
	UPROPERTY() TObjectPtr<UAnimSequence> Sprint; // optional
	FCricketBodyPose Pose; // world space

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FCricketAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override { delete Proxy; }
};
