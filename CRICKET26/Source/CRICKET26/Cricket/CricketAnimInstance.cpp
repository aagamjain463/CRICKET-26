#include "CricketAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "TwoBoneIK.h"

void FCricketAnimProxy::PreUpdate(UAnimInstance* Instance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
	const UCricketAnimInstance* I = CastChecked<UCricketAnimInstance>(Instance);
	Idle = I->Idle;
	Jog = I->Jog;
	Sprint = I->Sprint;
	Pose = I->Pose;
	if (Idle) IdleTime = FMath::Fmod(IdleTime + DeltaSeconds, double(FMath::Max(Idle->GetPlayLength(), 0.01f)));
	if (Jog) JogTime = FMath::Fmod(JogTime + DeltaSeconds * Pose.JogRate, double(FMath::Max(Jog->GetPlayLength(), 0.01f)));
	if (Sprint) SprintTime = FMath::Fmod(SprintTime + DeltaSeconds * Pose.SprintRate, double(FMath::Max(Sprint->GetPlayLength(), 0.01f)));
	// World to component space, here on the game thread where the component's transform is current.
	const FTransform C = GetComponentTransform();
	Pose.PelvisOffset = C.InverseTransformVector(Pose.PelvisOffset);
	Pose.ChestFacing = C.InverseTransformVectorNoScale(Pose.ChestFacing);
	Pose.LookAt = C.InverseTransformPosition(Pose.LookAt);
	Pose.ShouldersAt = C.InverseTransformPosition(Pose.ShouldersAt);
	for (int32 S = 0; S < 2; ++S)
	{
		Pose.Hand[S] = C.InverseTransformPosition(Pose.Hand[S]);
		Pose.Elbow[S] = C.InverseTransformPosition(Pose.Elbow[S]);
	}
}

bool FCricketAnimProxy::Evaluate(FPoseContext& Output)
{
	if (!Idle)
	{
		Output.ResetToRefPose();
		return true;
	}
	FAnimationPoseData Out(Output);
	Idle->GetAnimationPose(Out, FAnimExtractContext(IdleTime, false));
	if (Jog && Pose.JogWeight > 0.01f)
	{
		FPoseContext JogPose(this);
		FAnimationPoseData JogData(JogPose);
		Jog->GetAnimationPose(JogData, FAnimExtractContext(JogTime, false));
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(Out, JogData, 1.f - Pose.JogWeight);
	}
	if (Sprint && Pose.JogWeight * Pose.SprintWeight > 0.01f)
	{
		FPoseContext SprintPose(this);
		FAnimationPoseData SprintData(SprintPose);
		Sprint->GetAnimationPose(SprintData, FAnimExtractContext(SprintTime, false));
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(Out, SprintData, 1.f - Pose.JogWeight * Pose.SprintWeight);
	}
	for (int32 L = 0; L < 2; ++L)
	{
		if (!Pose.Clip[L] || Pose.ClipWeight[L] <= 0.01f) continue;
		FPoseContext ClipPose(this);
		FAnimationPoseData ClipData(ClipPose);
		Pose.Clip[L]->GetAnimationPose(ClipData, FAnimExtractContext(FMath::Clamp(double(Pose.ClipTime[L]), 0.0, double(Pose.Clip[L]->GetPlayLength())), false));
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(Out, ClipData, 1.f - FMath::Min(Pose.ClipWeight[L], 1.f));
	}
	ApplyActions(Output);
	return true;
}

// Named rather than anonymous: unity builds merge this file with others that have their own helpers.
namespace CricketIK
{
	using FCS = FCSPose<FCompactPose>;

	void SetCS(FCS& CS, FCompactPoseBoneIndex Bone, const FTransform& T)
	{
		const FBoneTransform One[] = { FBoneTransform(Bone, T) };
		CS.SafeSetCSBoneTransforms(One);
	}

	void Turn(FCS& CS, FCompactPoseBoneIndex Bone, const FQuat& Delta)
	{
		if (Bone == INDEX_NONE) return;
		FTransform T = CS.GetComponentSpaceTransform(Bone);
		T.SetRotation(Delta * T.GetRotation());
		SetCS(CS, Bone, T);
	}

	void Reach(FCS& CS, FCompactPoseBoneIndex A, FCompactPoseBoneIndex B, FCompactPoseBoneIndex C, const FVector& Effector, const FVector& Pole)
	{
		if (A == INDEX_NONE || B == INDEX_NONE || C == INDEX_NONE) return;
		FTransform TA = CS.GetComponentSpaceTransform(A), TB = CS.GetComponentSpaceTransform(B), TC = CS.GetComponentSpaceTransform(C);
		AnimationCore::SolveTwoBoneIK(TA, TB, TC, Pole, Effector, false, 1.0, 1.0);
		const FBoneTransform Chain[] = { FBoneTransform(A, TA), FBoneTransform(B, TB), FBoneTransform(C, TC) };
		CS.SafeSetCSBoneTransforms(Chain);
	}
}

void FCricketAnimProxy::ApplyActions(FPoseContext& Output) const
{
	using namespace CricketIK;
	const bool bPelvis = !Pose.PelvisOffset.IsNearlyZero(0.5f) || Pose.ShouldersWeight > 0.f;
	const bool bChest = !Pose.ChestFacing.IsNearlyZero() || Pose.ChestBend != 0.f;
	const bool bHands = Pose.HandWeight[0] > 0.f || Pose.HandWeight[1] > 0.f;
	if (!bPelvis && !bChest && !bHands && Pose.LookWeight <= 0.f) return;

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	auto Bone = [&](const TCHAR* Name)
	{
		const int32 Mesh = Bones.GetPoseBoneIndexForBoneName(FName(Name));
		return Mesh == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh));
	};
	// ponytail: bone names looked up every evaluation (a map find each); cache per bone container if profiling shows it.
	const FCompactPoseBoneIndex Pelvis = Bone(TEXT("pelvis")), Spine1 = Bone(TEXT("spine_01")), Spine3 = Bone(TEXT("spine_03")),
		Neck = Bone(TEXT("neck_01")), Head = Bone(TEXT("head"));
	const FCompactPoseBoneIndex Thigh[2] = { Bone(TEXT("thigh_l")), Bone(TEXT("thigh_r")) }, Calf[2] = { Bone(TEXT("calf_l")), Bone(TEXT("calf_r")) },
		Foot[2] = { Bone(TEXT("foot_l")), Bone(TEXT("foot_r")) };
	const FCompactPoseBoneIndex Upper[2] = { Bone(TEXT("upperarm_l")), Bone(TEXT("upperarm_r")) }, Lower[2] = { Bone(TEXT("lowerarm_l")), Bone(TEXT("lowerarm_r")) },
		Hand[2] = { Bone(TEXT("hand_l")), Bone(TEXT("hand_r")) };
	if (Pelvis == INDEX_NONE) return;

	FCS CS;
	CS.InitPose(Output.Pose);
	// The mannequin faces +Y in its own space.
	const FVector Forward(0.f, 1.f, 0.f);

	// Crouch or lean: move the pelvis, then put the feet back where they were.
	if (bPelvis)
	{
		FVector FootAt[2];
		for (int32 S = 0; S < 2; ++S) FootAt[S] = Foot[S] != INDEX_NONE ? CS.GetComponentSpaceTransform(Foot[S]).GetLocation() : FVector::ZeroVector;
		FVector Offset = Pose.PelvisOffset;
		if (Pose.ShouldersWeight > 0.f && Upper[0] != INDEX_NONE && Upper[1] != INDEX_NONE)
		{
			const FVector Shoulders = 0.5f * (CS.GetComponentSpaceTransform(Upper[0]).GetLocation() + CS.GetComponentSpaceTransform(Upper[1]).GetLocation());
			Offset += (Pose.ShouldersAt - Shoulders) * FMath::Min(Pose.ShouldersWeight, 1.f);
		}
		FTransform P = CS.GetComponentSpaceTransform(Pelvis);
		P.AddToTranslation(Offset);
		SetCS(CS, Pelvis, P);
		for (int32 S = 0; S < 2; ++S)
		{
			if (Thigh[S] == INDEX_NONE || Calf[S] == INDEX_NONE) continue;
			const FVector Hip = CS.GetComponentSpaceTransform(Thigh[S]).GetLocation();
			const FVector Knee = CS.GetComponentSpaceTransform(Calf[S]).GetLocation();
			// Knees keep bending the way they already do (forward if the leg is straight).
			const FVector Bend = (Knee - 0.5f * (Hip + FootAt[S])).GetSafeNormal() + 0.5f * Forward;
			Reach(CS, Thigh[S], Calf[S], Foot[S], FootAt[S], Knee + Bend.GetSafeNormal() * 50.f);
		}
	}

	// Chest: turn toward a facing and bend forward, shared between the lower and upper spine.
	if (bChest)
	{
		FQuat Q = FQuat::Identity;
		const FVector Face = FVector(Pose.ChestFacing.X, Pose.ChestFacing.Y, 0.f).GetSafeNormal();
		if (!Face.IsNearlyZero()) Q = FQuat::FindBetweenNormals(Forward, Face);
		const float Bend = FMath::DegreesToRadians(Pose.ChestBend);
		const FVector BentUp = FVector::UpVector * FMath::Cos(Bend) + Q.RotateVector(Forward) * FMath::Sin(Bend);
		Q = FQuat::FindBetweenNormals(FVector::UpVector, BentUp) * Q;
		const FQuat Half = FQuat::Slerp(FQuat::Identity, Q, 0.5f);
		Turn(CS, Spine1, Half);
		Turn(CS, Spine3, Half);
	}

	// Head: turn toward the look target, at most 80 degrees from the chest.
	if (Pose.LookWeight > 0.f && Head != INDEX_NONE)
	{
		const FVector From = CS.GetComponentSpaceTransform(Head).GetLocation();
		const FVector ChestFwd = !Pose.ChestFacing.IsNearlyZero() ? FVector(Pose.ChestFacing.X, Pose.ChestFacing.Y, 0.f).GetSafeNormal() : Forward;
		FVector To = (Pose.LookAt - From).GetSafeNormal();
		const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(ChestFwd, To), -1.f, 1.f));
		const float Max = FMath::DegreesToRadians(80.f);
		if (Angle > Max) To = FMath::Lerp(ChestFwd, To, Max / Angle).GetSafeNormal();
		const FQuat Q = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(ChestFwd, To), Pose.LookWeight);
		Turn(CS, Neck, FQuat::Slerp(FQuat::Identity, Q, 0.4f));
		Turn(CS, Head, FQuat::Slerp(FQuat::Identity, Q, 0.6f));
	}

	// Hands on their targets.
	for (int32 S = 0; S < 2; ++S)
	{
		if (Pose.HandWeight[S] <= 0.f || Hand[S] == INDEX_NONE) continue;
		const FVector Now = CS.GetComponentSpaceTransform(Hand[S]).GetLocation();
		Reach(CS, Upper[S], Lower[S], Hand[S], FMath::Lerp(Now, Pose.Hand[S], FMath::Min(Pose.HandWeight[S], 1.f)), Pose.Elbow[S]);
	}

	FCS::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Output.Pose);
}
