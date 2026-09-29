#include "CricketAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "TwoBoneIK.h"

void FCricketAnimProxy::PreUpdate(UAnimInstance* Instance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
	DeltaTime = DeltaSeconds;
	const UCricketAnimInstance* I = CastChecked<UCricketAnimInstance>(Instance);
	Idle = I->Idle;
	Walk = I->Walk;
	Jog = I->Jog;
	Sprint = I->Sprint;
	Pose = I->Pose;
	if (Idle) IdleTime = FMath::Fmod(IdleTime + DeltaSeconds, double(FMath::Max(Idle->GetPlayLength(), 0.01f)));
	if (Walk) WalkTime = FMath::Fmod(WalkTime + DeltaSeconds * Pose.WalkRate, double(FMath::Max(Walk->GetPlayLength(), 0.01f)));
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
		Pose.PalmFacing[S] = C.InverseTransformVectorNoScale(Pose.PalmFacing[S]);
		Pose.FingerFacing[S] = C.InverseTransformVectorNoScale(Pose.FingerFacing[S]);
		Pose.Foot[S] = C.InverseTransformPosition(Pose.Foot[S]);
	}
	FCricketBatterPose& B = Pose.Batter;
	B.Pelvis = C.InverseTransformPosition(B.Pelvis);
	B.Grip = C.InverseTransformPosition(B.Grip);
	for (FVector* V : { &B.Hips, &B.Chest, &B.ChestUp, &B.BatAxis, &B.BatFace, &B.Toe[0], &B.Toe[1] }) *V = C.InverseTransformVectorNoScale(*V);
	for (int32 F = 0; F < 2; ++F) B.Ball[F] = C.InverseTransformPosition(B.Ball[F]);
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
	if (Walk && Pose.WalkWeight > 0.01f)
	{
		FPoseContext WalkPose(this);
		FAnimationPoseData WalkData(WalkPose);
		Walk->GetAnimationPose(WalkData, FAnimExtractContext(WalkTime, false));
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(Out, WalkData, 1.f - Pose.WalkWeight);
	}
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
	if (Pose.Batter.Weight <= 0.f)
	{
		bSolved = false;
		ApplyActions(Output);
		return true;
	}
	FCompactPose Solved;
	Solved.CopyBonesFrom(Output.Pose);
	SolveBatter(Solved);
	const float W = FMath::Min(Pose.Batter.Weight, 1.f);
	if (W < 1.f) ApplyActions(Output);
	for (FCompactPoseBoneIndex I : Output.Pose.ForEachBoneIndex()) Output.Pose[I].BlendWith(Solved[I], W);
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

	void Reach(FCS& CS, FCompactPoseBoneIndex A, FCompactPoseBoneIndex B, FCompactPoseBoneIndex C, const FVector& InEffector, const FVector& Pole)
	{
		if (A == INDEX_NONE || B == INDEX_NONE || C == INDEX_NONE) return;
		FTransform TA = CS.GetComponentSpaceTransform(A), TB = CS.GetComponentSpaceTransform(B), TC = CS.GetComponentSpaceTransform(C);
		// Never ask for more arm (or leg) than there is: an effector past full extension snaps the
		// middle joint straight and pops it side to side frame by frame, which reads as the elbow
		// coming apart on the auction's seated staff at full paddle reach. Just inside full reach the
		// solver bends the joint instead, with the hand a millimetre short nobody can see.
		FVector Effector = InEffector;
		const float L1 = FVector::Dist(TA.GetLocation(), TB.GetLocation()), L2 = FVector::Dist(TB.GetLocation(), TC.GetLocation());
		const float MaxReach = 0.995f * (L1 + L2);
		if (MaxReach > 1.f && FVector::Dist(TA.GetLocation(), Effector) > MaxReach)
			Effector = TA.GetLocation() + (Effector - TA.GetLocation()).GetSafeNormal() * MaxReach;
		AnimationCore::SolveTwoBoneIK(TA, TB, TC, Pole, Effector, false, 1.0, 1.0);
		const FBoneTransform Chain[] = { FBoneTransform(A, TA), FBoneTransform(B, TB), FBoneTransform(C, TC) };
		CS.SafeSetCSBoneTransforms(Chain);
	}
}

void FCricketAnimProxy::ApplyActions(FPoseContext& Output) const
{
	using namespace CricketIK;
	const bool bPelvis = !Pose.PelvisOffset.IsNearlyZero(0.5f) || Pose.ShouldersWeight > 0.f || Pose.FootWeight > 0.f;
	const bool bChest = !Pose.ChestFacing.IsNearlyZero() || Pose.ChestBend != 0.f;
	const bool bHands = Pose.HandWeight[0] > 0.f || Pose.HandWeight[1] > 0.f;
	if (!bPelvis && !bChest && !bHands && Pose.LookWeight <= 0.f) return;

	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	if (!ActionsRig.bValid || ActionsRig.Serial != Bones.GetSerialNumber())
	{
		auto Bone = [&](const TCHAR* Name)
		{
			const int32 Mesh = Bones.GetPoseBoneIndexForBoneName(FName(Name));
			return Mesh == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh));
		};
		ActionsRig.Pelvis = Bone(TEXT("pelvis"));
		ActionsRig.Spine1 = Bone(TEXT("spine_01"));
		ActionsRig.Spine3 = Bone(TEXT("spine_03"));
		ActionsRig.Neck = Bone(TEXT("neck_01"));
		ActionsRig.Head = Bone(TEXT("head"));
		for (int32 S = 0; S < 2; ++S)
		{
			const TCHAR* Suffix = S == 0 ? TEXT("_l") : TEXT("_r");
			ActionsRig.Thigh[S] = Bone(*FString::Printf(TEXT("thigh%s"), Suffix));
			ActionsRig.Calf[S] = Bone(*FString::Printf(TEXT("calf%s"), Suffix));
			ActionsRig.Foot[S] = Bone(*FString::Printf(TEXT("foot%s"), Suffix));
			ActionsRig.Clavicle[S] = Bone(*FString::Printf(TEXT("clavicle%s"), Suffix));
			ActionsRig.Upper[S] = Bone(*FString::Printf(TEXT("upperarm%s"), Suffix));
			ActionsRig.Lower[S] = Bone(*FString::Printf(TEXT("lowerarm%s"), Suffix));
			ActionsRig.Hand[S] = Bone(*FString::Printf(TEXT("hand%s"), Suffix));
		}
		ActionsRig.Serial = Bones.GetSerialNumber();
		ActionsRig.bValid = true;
	}
	const FCompactPoseBoneIndex Pelvis = ActionsRig.Pelvis, Spine1 = ActionsRig.Spine1, Spine3 = ActionsRig.Spine3,
		Neck = ActionsRig.Neck, Head = ActionsRig.Head;
	const FCompactPoseBoneIndex* Thigh = ActionsRig.Thigh, *Calf = ActionsRig.Calf, *Foot = ActionsRig.Foot;
	const FCompactPoseBoneIndex* Clavicle = ActionsRig.Clavicle;
	const FCompactPoseBoneIndex* Upper = ActionsRig.Upper, *Lower = ActionsRig.Lower, *Hand = ActionsRig.Hand;
	if (Pelvis == INDEX_NONE) return;

	FCS CS;
	CS.InitPose(Output.Pose);
	if ((!Pose.PalmFacing[0].IsNearlyZero() || !Pose.PalmFacing[1].IsNearlyZero()) && (!Rig.bValid || Rig.Serial != Bones.GetSerialNumber()))
		BuildRig(Bones);
	// The mannequin faces +Y in its own space.
	const FVector Forward(0.f, 1.f, 0.f);

	// Crouch or lean: move the pelvis, then put the feet back where they were.
	if (bPelvis)
	{
		FVector FootAt[2];
		for (int32 S = 0; S < 2; ++S) FootAt[S] = Foot[S] != INDEX_NONE ? CS.GetComponentSpaceTransform(Foot[S]).GetLocation() : FVector::ZeroVector;
		if (Pose.FootWeight > 0.f)
			for (int32 S = 0; S < 2; ++S) FootAt[S] = FMath::Lerp(FootAt[S], Pose.Foot[S], FMath::Min(Pose.FootWeight, 1.f));
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
			const FVector Bend = Pose.FootWeight > 0.f ? Forward + 0.5f * FVector::UpVector : (Knee - 0.5f * (Hip + FootAt[S])).GetSafeNormal() + 0.5f * Forward;
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
		if (Pose.bKeeper && Clavicle[S] != INDEX_NONE)
		{
			const FVector Shoulder = CS.GetComponentSpaceTransform(Clavicle[S]).GetLocation();
			const FVector Current = CS.GetComponentSpaceTransform(Hand[S]).GetLocation() - Shoulder;
			const FVector Desired = Pose.Hand[S] - Shoulder;
			if (Current.SizeSquared() > 100.f && Desired.SizeSquared() > 100.f)
			{
				const FQuat Shift = FQuat::FindBetweenNormals(Current.GetSafeNormal(), Desired.GetSafeNormal());
				const float Fraction = FMath::Min(0.45f, FMath::DegreesToRadians(12.f) / FMath::Max(Shift.GetAngle(), 0.001f));
				Turn(CS, Clavicle[S], FQuat::Slerp(FQuat::Identity, Shift, Fraction * FMath::Min(Pose.HandWeight[S], 1.f)));
			}
		}
		const FVector Now = CS.GetComponentSpaceTransform(Hand[S]).GetLocation();
		Reach(CS, Upper[S], Lower[S], Hand[S], FMath::Lerp(Now, Pose.Hand[S], FMath::Min(Pose.HandWeight[S], 1.f)), Pose.Elbow[S]);
		if (!Pose.PalmFacing[S].IsNearlyZero() && !Pose.FingerFacing[S].IsNearlyZero() && Rig.bValid && Rig.Hand[S] != INDEX_NONE)
		{
			// Orient only the wrist after the arm reaches its target; neither shoulder nor elbow moves.
			const FQuat Reference = Rig.Ref[Rig.Hand[S]].GetRotation();
			const FQuat Wrist = CS.GetComponentSpaceTransform(Hand[S]).GetRotation();
			const FQuat From = FRotationMatrix::MakeFromXY(Rig.Fingers[S], Rig.Palm[S]).ToQuat();
			const FQuat To = FRotationMatrix::MakeFromXY(Pose.FingerFacing[S], Pose.PalmFacing[S]).ToQuat();
			Turn(CS, Hand[S], To * From.Inverse() * Reference * Wrist.Inverse());
		}
	}

	FCS::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Output.Pose);
}

namespace CricketIK
{
	// A rotation taking the frame (X along A, Y toward B) to the frame (X along C, Y toward D).
	FQuat Map(const FVector& A, const FVector& B, const FVector& C, const FVector& D)
	{
		return FRotationMatrix::MakeFromXY(C, D).ToQuat() * FRotationMatrix::MakeFromXY(A, B).ToQuat().Inverse();
	}

	// Where the middle joint of a limb goes: Upper and Lower long from Root to End, bent toward Bend.
	FVector Joint(const FVector& Root, const FVector& End, float Upper, float Lower, const FVector& Bend)
	{
		const FVector U = (End - Root).GetSafeNormal();
		const float D = FMath::Clamp(FVector::Dist(Root, End), FMath::Abs(Upper - Lower) + 0.1f, Upper + Lower - 0.01f);
		const float X = (Upper * Upper - Lower * Lower + D * D) / (2.f * D);
		const FVector Side = (Bend - U * (Bend | U)).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		return Root + U * X + Side * FMath::Sqrt(FMath::Max(Upper * Upper - X * X, 0.f));
	}
}

void FCricketAnimProxy::BuildRig(const FBoneContainer& Bones) const
{
	FBatterRig& R = Rig;
	R = FBatterRig();
	R.Serial = Bones.GetSerialNumber();
	const FReferenceSkeleton& Skeleton = Bones.GetReferenceSkeleton();
	const TArray<FTransform>& Local = Skeleton.GetRefBonePose();
	R.Ref.SetNum(Local.Num());
	for (int32 I = 0; I < Local.Num(); ++I)
	{
		const int32 Parent = Skeleton.GetParentIndex(I);
		R.Ref[I] = Parent == INDEX_NONE ? Local[I] : Local[I] * R.Ref[Parent];
	}
	R.Compact.Init(INDEX_NONE, Local.Num());
	for (int32 I = 0; I < Bones.GetCompactPoseNumBones(); ++I)
		R.Compact[Bones.MakeMeshPoseIndex(FCompactPoseBoneIndex(I)).GetInt()] = I;
	auto Find = [&](const FString& Name) { return Skeleton.FindBoneIndex(FName(*Name)); };
	auto Posed = [&](int32 I) { return I != INDEX_NONE && R.Compact[I] != INDEX_NONE; };
	R.Pelvis = Find(TEXT("pelvis"));
	R.Head = Find(TEXT("head"));
	for (const TCHAR* Name : { TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05") })
		if (const int32 I = Find(Name); Posed(I)) R.Spine.Add(I);
	for (const TCHAR* Name : { TEXT("neck_01"), TEXT("neck_02") })
		if (const int32 I = Find(Name); Posed(I)) R.Neck.Add(I);
	static const TCHAR* Fingers[5] = { TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") };
	bool bAll = Posed(R.Pelvis) && Posed(R.Head) && R.Spine.Num() > 0;
	for (int32 S = 0; S < 2; ++S)
	{
		const TCHAR* Side = S == 0 ? TEXT("l") : TEXT("r");
		auto Bone = [&](const TCHAR* Name) { return Find(FString::Printf(TEXT("%s_%s"), Name, Side)); };
		R.Clavicle[S] = Bone(TEXT("clavicle"));
		R.Upper[S] = Bone(TEXT("upperarm"));
		R.Lower[S] = Bone(TEXT("lowerarm"));
		R.Hand[S] = Bone(TEXT("hand"));
		R.Twist[S][0] = Bone(TEXT("lowerarm_twist_01"));
		R.Twist[S][1] = Bone(TEXT("lowerarm_twist_02"));
		R.Thigh[S] = Bone(TEXT("thigh"));
		R.Calf[S] = Bone(TEXT("calf"));
		R.Foot[S] = Bone(TEXT("foot"));
		R.Ball[S] = Bone(TEXT("ball"));
		for (int32 F = 0; F < 5; ++F)
			for (int32 K = 0; K < 3; ++K) R.Finger[S][F][K] = Find(FString::Printf(TEXT("%s_%02d_%s"), Fingers[F], K + 1, Side));
		for (int32 I : { R.Upper[S], R.Lower[S], R.Hand[S], R.Thigh[S], R.Calf[S], R.Foot[S] })
			bAll &= Posed(I);
		for (int32 I : { R.Ball[S], R.Finger[S][1][0], R.Finger[S][2][0], R.Finger[S][2][2], R.Finger[S][4][0] })
			bAll &= I != INDEX_NONE;
		if (!bAll) continue;
		// The hand's own frame, measured: the knuckle line, the way the fingers point, and the palm (the side the
		// fingers curl toward). The handle lies along the knuckles, a finger's width out from the palm.
		auto At = [&](int32 I) { return R.Ref[I].GetLocation(); };
		const FVector Wrist = At(R.Hand[S]), Index = At(R.Finger[S][1][0]), Pinky = At(R.Finger[S][4][0]), Middle = At(R.Finger[S][2][0]);
		const FVector Across = (Pinky - Index).GetSafeNormal();
		const FVector Out = ((Middle - Wrist) - Across * ((Middle - Wrist) | Across)).GetSafeNormal();
		const FVector N = FVector::CrossProduct(Out, Across);
		const FVector Curl = At(R.Finger[S][2][2]) - Middle;
		R.Across[S] = Across;
		R.Palm[S] = (Curl | N) >= 0.f ? N : -N;
		R.GripOffset[S] = 0.5f * (Index + Pinky) + Out * 1.5f + R.Palm[S] * 2.8f - Wrist;
		R.Fingers[S] = Out;
		R.PalmOffset[S] = 0.6f * (0.5f * (Index + Pinky) - Wrist) + R.Palm[S] * 3.f; // a keeping glove's padding
		for (int32 F = 0; F < 5; ++F)
		{
			const int32 A = R.Finger[S][F][0], B = R.Finger[S][F][1];
			R.CurlAxis[S][F] = A != INDEX_NONE && B != INDEX_NONE ? FVector::CrossProduct((At(B) - At(A)).GetSafeNormal(), R.Palm[S]).GetSafeNormal() : FVector::ZeroVector;
		}
	}
	R.PelvisZ = bAll ? FMath::Max(R.Ref[R.Pelvis].GetLocation().Z, 50.f) : 97.f;
	R.bValid = bAll;
}

void FCricketAnimProxy::SolveBatter(FCompactPose& Out)
{
	using namespace CricketIK;
	const FBoneContainer& Bones = Out.GetBoneContainer();
	if (!Rig.bValid || Rig.Serial != Bones.GetSerialNumber()) BuildRig(Bones);
	const FBatterRig& R = Rig;
	if (!R.bValid) return;
	const FCricketBatterPose& B = Pose.Batter;
	const FVector Up = FVector::UpVector;
	const float Size = R.PelvisZ / 97.f;
	FCS CS;
	CS.InitPose(Out);
	auto At = [&](int32 I) { return R.Ref[I].GetLocation(); };
	auto Rot = [&](int32 I) { return R.Ref[I].GetRotation(); };
	auto Posed = [&](int32 I) { return FCompactPoseBoneIndex(I == INDEX_NONE ? INDEX_NONE : R.Compact[I]); };
	auto Now = [&](int32 I) { return CS.GetComponentSpaceTransform(Posed(I)).GetLocation(); };
	// A bone's component-space rotation, its position left where its parent carries it (or placed).
	auto Place = [&](int32 I, const FQuat& Q, const FVector* Where = nullptr)
	{
		const FCompactPoseBoneIndex C = Posed(I);
		if (C == INDEX_NONE) return;
		FTransform T = CS.GetComponentSpaceTransform(C);
		T.SetRotation(Q.GetNormalized());
		if (Where) T.SetLocation(*Where);
		SetCS(CS, C, T);
	};

	// Hips: turned to their facing, tipped forward with part of the chest's bend (a hinge at the hips), at the
	// planned height over the ground.
	const FVector Hips = FVector(B.Hips.X, B.Hips.Y, 0.f).GetSafeNormal(UE_SMALL_NUMBER, FVector(0.f, 1.f, 0.f));
	const FVector ChestUp = B.ChestUp.GetSafeNormal(UE_SMALL_NUMBER, Up);
	const float Flex = FMath::Acos(FMath::Clamp(ChestUp | Up, -1.f, 1.f));
	const FVector HipsUp = FQuat(FVector::CrossProduct(Up, Hips).GetSafeNormal(), 0.35f * Flex).RotateVector(Up);
	const FQuat QHips = FRotationMatrix::MakeFromZY(HipsUp, Hips).ToQuat();
	const FQuat QChest = FRotationMatrix::MakeFromZY(ChestUp, B.Chest).ToQuat();

	// Feet: turned to their toe, the heel raised about the ball, the ankle wherever that puts it.
	FQuat FootQ[2], Yaw[2];
	FVector Ankle[2];
	for (int32 F = 0; F < 2; ++F)
	{
		const FVector RefToe = FVector(At(R.Ball[F]) - At(R.Foot[F])).GetSafeNormal2D();
		const FVector Toe = FVector(B.Toe[F].X, B.Toe[F].Y, 0.f).GetSafeNormal(UE_SMALL_NUMBER, RefToe);
		Yaw[F] = FQuat::FindBetweenNormals(RefToe, Toe);
		FootQ[F] = FQuat(FVector::CrossProduct(Up, Toe).GetSafeNormal(), FMath::DegreesToRadians(B.Heel[F])) * Yaw[F];
		const FVector BallAt = FVector(B.Ball[F].X, B.Ball[F].Y, B.Ball[F].Z + At(R.Ball[F]).Z) + Up * B.Lift[F];
		Ankle[F] = BallAt + FootQ[F].RotateVector(At(R.Foot[F]) - At(R.Ball[F]));
	}
	// Lower the hips as far as the legs need to reach the feet.
	FVector PelvisAt(B.Pelvis.X, B.Pelvis.Y, B.Pelvis.Z + R.PelvisZ - B.Drop * Size);
	Place(R.Pelvis, QHips * Rot(R.Pelvis), &PelvisAt);
	float Sink = 0.f;
	for (int32 F = 0; F < 2; ++F)
	{
		const float Leg = 0.995f * (FVector::Dist(At(R.Thigh[F]), At(R.Calf[F])) + FVector::Dist(At(R.Calf[F]), At(R.Foot[F])));
		const FVector D = Ankle[F] - Now(R.Thigh[F]);
		const float Flat = FVector2D(D).Size();
		if (D.Size() > Leg) Sink = FMath::Max(Sink, -D.Z - (Flat < Leg ? FMath::Sqrt(Leg * Leg - Flat * Flat) : 0.f));
	}
	if (Sink > 0.f)
	{
		PelvisAt.Z -= Sink;
		Place(R.Pelvis, QHips * Rot(R.Pelvis), &PelvisAt);
	}

	// Legs: each knee over its toe and a little out, the thigh and shin turned about the knee's hinge.
	for (int32 F = 0; F < 2; ++F)
	{
		const FVector Hip = Now(R.Thigh[F]);
		const float L1 = FVector::Dist(At(R.Thigh[F]), At(R.Calf[F])), L2 = FVector::Dist(At(R.Calf[F]), At(R.Foot[F]));
		const FVector Outward = QHips.RotateVector(FVector(F == 0 ? 1.f : -1.f, 0.f, 0.f));
		const FVector Knee = Joint(Hip, Ankle[F], L1, L2, FootQ[F].RotateVector(FVector(At(R.Ball[F]) - At(R.Foot[F])).GetSafeNormal2D()) + Outward * 0.3f);
		const FVector RefHinge = FVector::CrossProduct(At(R.Foot[F]) - At(R.Thigh[F]), FVector(0.f, 1.f, 0.f)).GetSafeNormal();
		const FVector Hinge = FVector::CrossProduct(Ankle[F] - Hip, Knee - Hip).GetSafeNormal();
		// The reference knee bends toward +Y; the hinge above is (hip to ankle) x (toward the knee), the same way round.
		Place(R.Thigh[F], Map(At(R.Calf[F]) - At(R.Thigh[F]), RefHinge, Knee - Hip, Hinge) * Rot(R.Thigh[F]));
		Place(R.Calf[F], Map(At(R.Foot[F]) - At(R.Calf[F]), RefHinge, Ankle[F] - Knee, Hinge) * Rot(R.Calf[F]));
		Place(R.Foot[F], FootQ[F] * Rot(R.Foot[F]));
		Place(R.Ball[F], Yaw[F] * Rot(R.Ball[F])); // toes flat on the ground
	}

	// Spine: the turn and bend from hips to chest shared up the vertebrae, most in the upper back.
	static const float Share[5] = { 0.15f, 0.35f, 0.55f, 0.8f, 1.f };
	const FQuat Twist = QChest * QHips.Inverse();
	for (int32 I = 0; I < R.Spine.Num(); ++I)
		Place(R.Spine[I], FQuat::Slerp(FQuat::Identity, Twist, R.Spine.Num() == 5 ? Share[I] : float(I + 1) / R.Spine.Num()) * QHips * Rot(R.Spine[I]));

	// Head: level-ish, eyes on the ball, at most 80 degrees round from the chest.
	{
		const FVector Eyes = Now(R.Head);
		const FVector Forward = FVector(B.Chest.X, B.Chest.Y, 0.f).GetSafeNormal(UE_SMALL_NUMBER, FVector(0.f, 1.f, 0.f));
		FVector Look = (Pose.LookAt - Eyes).GetSafeNormal(UE_SMALL_NUMBER, Forward);
		const float Angle = FMath::Acos(FMath::Clamp(Look | Forward, -1.f, 1.f)), Max = FMath::DegreesToRadians(80.f);
		if (Angle > Max) Look = FMath::Lerp(Forward, Look, Max / Angle).GetSafeNormal();
		const FQuat QLook = FRotationMatrix::MakeFromYZ(Look, FMath::Lerp(Up, ChestUp, 0.3f)).ToQuat();
		const FQuat QHead = FQuat::Slerp(QChest, QLook, FMath::Clamp(Pose.LookWeight, 0.f, 1.f));
		for (int32 I = 0; I < R.Neck.Num(); ++I) Place(R.Neck[I], FQuat::Slerp(QChest, QHead, 0.35f * (I + 1)) * Rot(R.Neck[I]));
		Place(R.Head, QHead * Rot(R.Head));
	}

	// Arms. Each hand's grip on the handle leaves one free turn about the handle (how far round it the hand
	// sits) and the elbow one about the shoulder-to-wrist line; both are searched for the most natural arm:
	// the wrist near its neutral set on the forearm, the forearm turning within its range, the elbow and
	// forearm outside the torso, the elbow hanging down and out, and close to last frame's answer.
	const FVector Axis = B.BatAxis.GetSafeNormal(UE_SMALL_NUMBER, -Up);
	const FVector Face0 = (B.BatFace - Axis * (B.BatFace | Axis)).GetSafeNormal(UE_SMALL_NUMBER, FVector(0.f, 1.f, 0.f));
	const FVector Pelvis0 = Now(R.Pelvis), Neck0 = R.Neck.Num() > 0 ? Now(R.Neck[0]) : Now(R.Head);
	const FVector TorsoAxis = (Neck0 - Pelvis0).GetSafeNormal(), Wide = QChest.RotateVector(FVector(1.f, 0.f, 0.f)), Deep = QChest.RotateVector(FVector(0.f, 1.f, 0.f));
	const float TorsoLength = FVector::Dist(Neck0, Pelvis0);
	auto TorsoDistance = [&](const FVector& P) -> float
	{
		const float T = FMath::Clamp((P - Pelvis0) | TorsoAxis, 0.f, TorsoLength);
		const FVector V = P - Pelvis0 - TorsoAxis * T;
		return FMath::Sqrt(FMath::Square((V | Wide) / (16.f * Size)) + FMath::Square((V | Deep) / (12.f * Size)));
	};
	auto Inside = [&](const FVector& P)
	{
		return FMath::Max(1.15f - TorsoDistance(P), 0.f);
	};
	for (int32 H = 0; H < 2; ++H)
	{
		const bool bTop = H == B.TopHand;
		// Pull the collarbone a little toward the hands, and further (the shoulder reaching forward, as a bottom
		// hand does through the ball) when the handle is beyond the straight arm.
		const FVector Handle = B.bGloves ? Pose.Hand[H] : B.Grip + Axis * (bTop ? -4.5f : 4.5f);
		const float L1 = FVector::Dist(At(R.Upper[H]), At(R.Lower[H])), L2 = FVector::Dist(At(R.Lower[H]), At(R.Hand[H]));
		if (Posed(R.Clavicle[H]) != INDEX_NONE)
		{
			const FVector Clav = Now(R.Clavicle[H]), Arm = Now(R.Upper[H]) - Clav;
			const FQuat Q = FQuat::FindBetweenNormals(Arm.GetSafeNormal(), (Handle - Clav).GetSafeNormal());
			FVector QAxis;
			float QAngle;
			Q.ToAxisAndAngle(QAxis, QAngle);
			float Angle = FMath::Min(0.2f * QAngle, FMath::DegreesToRadians(12.f));
			while (Angle < FMath::Min(QAngle, FMath::DegreesToRadians(30.f)) && FVector::Dist(Clav + FQuat(QAxis, Angle).RotateVector(Arm), Handle) > 0.97f * (L1 + L2))
				Angle += FMath::DegreesToRadians(2.f);
			Turn(CS, Posed(R.Clavicle[H]), FQuat(QAxis, Angle));
		}
		const FVector Shoulder = Now(R.Upper[H]);
		const FVector RefUpper = (At(R.Lower[H]) - At(R.Upper[H])).GetSafeNormal(), RefLower = (At(R.Hand[H]) - At(R.Lower[H])).GetSafeNormal();
		const FVector RefHinge = FVector::CrossProduct(RefUpper, RefLower).GetSafeNormal();
		const FVector Outward = H == 0 ? Wide : -Wide;
		if (B.bGloves)
		{
			// Gloves: palms to the ball, fingers down below the chest and up above it, out to the sides between.
			// The hand is set first, then the forearm turns and the wrist bends toward it only so far.
			const FVector Facing = FVector(B.Chest.X, B.Chest.Y, 0.f).GetSafeNormal(UE_SMALL_NUMBER, Hips);
			const float High = FMath::SmoothStep(95.f * Size, 125.f * Size, float(Handle.Z - B.Pelvis.Z));
			const float Mid = 1.f - FMath::Abs(2.f * High - 1.f);
			const FVector Fingers = (FMath::Lerp(-Up + 0.35f * Facing, Up + 0.2f * Facing, High) + Outward * Mid).GetSafeNormal();
			const FVector Palm = (Facing - Fingers * (Facing | Fingers)).GetSafeNormal(UE_SMALL_NUMBER, Outward);
			FQuat HandQ = Map(R.Fingers[H], R.Palm[H], Fingers, Palm);
			const FVector Wrist = Handle - HandQ.RotateVector(R.PalmOffset[H]);
			const FVector Elbow = Joint(Shoulder, Wrist, L1, L2, Pose.Elbow[H] - Shoulder);
			const FVector Hinge = FVector::CrossProduct(Elbow - Shoulder, Wrist - Elbow).GetSafeNormal(UE_SMALL_NUMBER, RefHinge);
			const FQuat QUpper = Map(RefUpper, RefHinge, Elbow - Shoulder, Hinge), QLower = Map(RefLower, RefHinge, Wrist - Elbow, Hinge);
			FQuat Swing, Spin;
			(QLower.Inverse() * HandQ).ToSwingTwist(RefLower, Swing, Spin);
			const float Pronate = FMath::Clamp(FMath::UnwindRadians(Spin.GetTwistAngle(RefLower)), -FMath::DegreesToRadians(MaxTwist - 10.f), FMath::DegreesToRadians(MaxTwist - 10.f));
			FVector SwingAxis;
			float SwingAngle;
			Swing.ToAxisAndAngle(SwingAxis, SwingAngle);
			HandQ = QLower * FQuat(SwingAxis, FMath::Min(SwingAngle, FMath::DegreesToRadians(MaxBend - 10.f))) * FQuat(RefLower, Pronate);
			Place(R.Upper[H], QUpper * Rot(R.Upper[H]));
			Place(R.Lower[H], QLower * Rot(R.Lower[H]));
			const FVector ForeAxis = (Wrist - Elbow).GetSafeNormal();
			for (int32 K = 0; K < 2; ++K) Place(R.Twist[H][K], FQuat(ForeAxis, Pronate * (K == 0 ? 2.f / 3.f : 1.f / 3.f)) * QLower * Rot(R.Twist[H][K]));
			Place(R.Hand[H], HandQ * Rot(R.Hand[H]));
			// Fingers a little flexed inside the glove, the thumb spread.
			static const float GloveCurl[5][3] = { { 5.f, 10.f, 10.f }, { 12.f, 15.f, 10.f }, { 12.f, 15.f, 10.f }, { 14.f, 16.f, 10.f }, { 16.f, 18.f, 10.f } };
			for (int32 F = 0; F < 5; ++F)
			{
				const FVector CurlAxis = HandQ.RotateVector(R.CurlAxis[H][F]);
				float Sum = 0.f;
				for (int32 K = 0; K < 3; ++K)
				{
					Sum += GloveCurl[F][K];
					if (R.Finger[H][F][K] != INDEX_NONE) Place(R.Finger[H][F][K], FQuat(CurlAxis, FMath::DegreesToRadians(Sum)) * HandQ * Rot(R.Finger[H][F][K]));
				}
			}
			continue;
		}
		struct FArm { FQuat Hand; FVector Wrist, Elbow; float Cost; float Twist; };
		auto Try = [&](float RollAngle, float SwivelAngle)
		{
			FArm A;
			const FVector Palm = FQuat(Axis, RollAngle).RotateVector(Face0);
			A.Hand = Map(R.Across[H], R.Palm[H], -Axis, Palm);
			A.Wrist = Handle - A.Hand.RotateVector(R.GripOffset[H]);
			const FVector ToWrist = A.Wrist - Shoulder;
			const float Dist = ToWrist.Size();
			const FVector U = ToWrist / FMath::Max(Dist, 1.f);
			const FVector E1 = (-Up - U * (-Up | U)).GetSafeNormal(UE_SMALL_NUMBER, Deep), E2 = FVector::CrossProduct(U, E1);
			const FVector Bend = E1 * FMath::Cos(SwivelAngle) + E2 * FMath::Sin(SwivelAngle);
			A.Elbow = Joint(Shoulder, A.Wrist, L1, L2, Bend);
			A.Cost = FMath::Square(FMath::Max(Dist - (L1 + L2) * 0.99f, 0.f)) * 2.f;
			const FVector Hinge = FVector::CrossProduct(A.Elbow - Shoulder, A.Wrist - A.Elbow).GetSafeNormal(UE_SMALL_NUMBER, RefHinge);
			const FQuat Fore = Map(RefLower, RefHinge, A.Wrist - A.Elbow, Hinge);
			FQuat Swing, Spin;
			(Fore.Inverse() * A.Hand).ToSwingTwist(RefLower, Swing, Spin);
			A.Twist = FMath::UnwindRadians(Spin.GetTwistAngle(RefLower));
			// The forearm turns and the wrist bends only so far: past MaxTwist or MaxBend the arm reads as wrung
			// out, so those are walls, not preferences.
			const float Bent = FMath::RadiansToDegrees(Swing.GetAngle()), Turned = FMath::Abs(FMath::RadiansToDegrees(A.Twist));
			A.Cost += FMath::Square(Bent / 40.f) + FMath::Square(FMath::Max(Turned - 55.f, 0.f) / 10.f);
			if (Turned > MaxTwist) A.Cost += 1000.f + Turned;
			if (Bent > MaxBend) A.Cost += 1000.f + Bent;
			// Torso penetration check: no body part should ever move inside another body part.
			for (const float T : { 0.f, 0.25f, 0.5f, 0.75f, 1.0f })
			{
				const float E = TorsoDistance(FMath::Lerp(A.Elbow, A.Wrist, T));
				if (E < 1.0f) A.Cost += 25000.f + (1.0f - E) * 50000.f;
				else if (E < 1.15f) A.Cost += 200.f * FMath::Square(1.15f - E);
			}
			for (const float T : { 0.4f, 0.7f })
			{
				const float E = TorsoDistance(FMath::Lerp(Shoulder, A.Elbow, T));
				if (E < 1.0f) A.Cost += 25000.f + (1.0f - E) * 50000.f;
				else if (E < 1.15f) A.Cost += 200.f * FMath::Square(1.15f - E);
			}
			// Both elbows hang under the hands: the upper arm down from the shoulder and the forearm rising to the
			// grip, never the elbow lifted with the forearm dropping to the handle. The top elbow leans a little out
			// toward the bowler; with the hand raised it points forward under the hands instead, as in a high finish.
			const float Raised = FMath::SmoothStep(-20.f * Size, 10.f * Size, float(A.Wrist.Z - Shoulder.Z));
			const FVector Prefer = -Up + Outward * ((bTop ? 0.3f : 0.15f) * (1.f - Raised)) + Deep * (0.5f * Raised);
			const FVector Bow = A.Elbow - Shoulder - U * ((A.Elbow - Shoulder) | U);
			A.Cost += 2.f * (1.f - (Bow.GetSafeNormal() | (Prefer - U * (Prefer | U)).GetSafeNormal()));
			A.Cost += 20.f * FMath::Square(FMath::Max(float(Bow.Z), 0.f) / (0.5f * L1));
			if (bSolved)
				A.Cost += 3.f * (FMath::Square(FMath::FindDeltaAngleRadians(SwivelAngle, Swivel[H]) / PI) + FMath::Square(FMath::FindDeltaAngleRadians(RollAngle, Roll[H]) / PI));
			return A;
		};
		// ponytail: a 12 x 16 grid (plus last frame's answer) then four halving passes (~230 tries an arm); a gradient
		// step from last frame if profiling cares.
		float BestRoll = 0.f, BestSwivel = 0.f;
		FArm Best = Try(0.f, 0.f);
		auto Consider = [&](float RollAngle, float SwivelAngle)
		{
			const FArm A = Try(RollAngle, SwivelAngle);
			if (A.Cost < Best.Cost) { Best = A; BestRoll = RollAngle; BestSwivel = SwivelAngle; }
		};
		if (bSolved) Consider(Roll[H], Swivel[H]);
		for (int32 I = 0; I < 12; ++I)
			for (int32 J = 0; J < 16; ++J) Consider(2.f * PI * I / 12.f, 2.f * PI * J / 16.f);
		for (float Step : { PI / 12.f, PI / 24.f, PI / 48.f, PI / 96.f })
		{
			const float R0 = BestRoll, S0 = BestSwivel;
			for (int32 I = -1; I <= 1; ++I)
				for (int32 J = -1; J <= 1; ++J) Consider(R0 + I * Step, S0 + J * Step);
		}
		// A hand only slides round the handle, and an elbow only swings, so fast: a best answer far from last
		// frame's is reached over a few frames rather than in one pop. Not across a cut (the grip jumping).
		if (bSolved && FVector::Dist(B.Grip, LastGrip) < 30.f)
		{
			const float Dt = FMath::Clamp(DeltaTime, 1.f / 240.f, 1.f / 15.f);
			const float R1 = Roll[H] + FMath::Clamp(FMath::FindDeltaAngleRadians(Roll[H], BestRoll), -RollRate * Dt, RollRate * Dt);
			const float S1 = Swivel[H] + FMath::Clamp(FMath::FindDeltaAngleRadians(Swivel[H], BestSwivel), -SwivelRate * Dt, SwivelRate * Dt);
			if (!FMath::IsNearlyEqual(R1, BestRoll) || !FMath::IsNearlyEqual(S1, BestSwivel))
			{
				// Never through a twisted arm or torso penetration: a pop beats a body penetration or wrung-out forearm.
				const FArm Limited = Try(R1, S1);
				if ((Limited.Cost < 1000.f || Best.Cost >= 1000.f) && (Limited.Cost < 20000.f || Best.Cost >= 20000.f))
				{
					Best = Limited;
					BestRoll = R1;
					BestSwivel = S1;
				}
			}
		}
		Roll[H] = FMath::UnwindRadians(BestRoll);
		Swivel[H] = FMath::UnwindRadians(BestSwivel);

		// Hard physical clearance: ensure Best.Elbow is strictly outside the torso ellipse.
		const float DistElbow = TorsoDistance(Best.Elbow);
		if (DistElbow < 1.03f)
		{
			const float TE = FMath::Clamp((Best.Elbow - Pelvis0) | TorsoAxis, 0.f, TorsoLength);
			const FVector VE = Best.Elbow - Pelvis0 - TorsoAxis * TE;
			const FVector RadialOut = (VE.IsNearlyZero() ? Outward : (VE | Wide) * Wide + (VE | Deep) * Deep).GetSafeNormal(UE_SMALL_NUMBER, Outward);
			const float Push = (1.05f - DistElbow) * (14.f * Size);
			Best.Elbow = Joint(Shoulder, Best.Wrist, L1, L2, (Best.Elbow - Shoulder) + RadialOut * Push);
		}

		const FVector Hinge = FVector::CrossProduct(Best.Elbow - Shoulder, Best.Wrist - Best.Elbow).GetSafeNormal(UE_SMALL_NUMBER, RefHinge);
		const FQuat QUpper = Map(RefUpper, RefHinge, Best.Elbow - Shoulder, Hinge), QLower = Map(RefLower, RefHinge, Best.Wrist - Best.Elbow, Hinge);
		Place(R.Upper[H], QUpper * Rot(R.Upper[H]));
		Place(R.Lower[H], QLower * Rot(R.Lower[H]));
		// The forearm's turn shared by its twist bones, most near the wrist.
		const FVector ForeAxis = (Best.Wrist - Best.Elbow).GetSafeNormal();
		for (int32 K = 0; K < 2; ++K) Place(R.Twist[H][K], FQuat(ForeAxis, Best.Twist * (K == 0 ? 2.f / 3.f : 1.f / 3.f)) * QLower * Rot(R.Twist[H][K]));
		Place(R.Hand[H], Best.Hand * Rot(R.Hand[H]));
		// Fingers closed round the handle, each joint about the finger's own hinge; the thumb less.
		static const float Curl[5][3] = { { 15.f, 25.f, 25.f }, { 50.f, 65.f, 40.f }, { 55.f, 70.f, 40.f }, { 60.f, 70.f, 40.f }, { 65.f, 70.f, 40.f } };
		for (int32 F = 0; F < 5; ++F)
		{
			const FVector CurlAxis = Best.Hand.RotateVector(R.CurlAxis[H][F]);
			float Sum = 0.f;
			for (int32 K = 0; K < 3; ++K)
			{
				Sum += Curl[F][K];
				if (R.Finger[H][F][K] != INDEX_NONE) Place(R.Finger[H][F][K], FQuat(CurlAxis, FMath::DegreesToRadians(Sum)) * Best.Hand * Rot(R.Finger[H][F][K]));
			}
		}
	}
	bSolved = true;
	LastGrip = B.Grip;
	FCS::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Out);
}
