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

void FCricketAnimProxy::BuildRig(const FBoneContainer& Bones)
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
	auto Inside = [&](const FVector& P)
	{
		const float T = FMath::Clamp((P - Pelvis0) | TorsoAxis, 0.f, TorsoLength);
		const FVector V = P - Pelvis0 - TorsoAxis * T;
		const float E = FMath::Sqrt(FMath::Square((V | Wide) / (16.f * Size)) + FMath::Square((V | Deep) / (12.f * Size)));
		return FMath::Max(1.15f - E, 0.f);
	};
	for (int32 H = 0; H < 2; ++H)
	{
		const bool bTop = H == B.TopHand;
		// Pull the collarbone a little toward the hands, and further (the shoulder reaching forward, as a bottom
		// hand does through the ball) when the handle is beyond the straight arm.
		const FVector Handle = B.Grip + Axis * (bTop ? -4.5f : 4.5f);
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
			const float Bent = FMath::RadiansToDegrees(Swing.GetAngle()), Turned = FMath::Abs(FMath::RadiansToDegrees(A.Twist));
			A.Cost += FMath::Square(Bent / 40.f) + FMath::Square(FMath::Max(Turned - 70.f, 0.f) / 15.f);
			for (const float T : { 0.f, 0.35f, 0.7f }) A.Cost += 60.f * FMath::Square(Inside(FMath::Lerp(A.Elbow, A.Wrist, T)));
			A.Cost += 60.f * FMath::Square(Inside(FMath::Lerp(Shoulder, A.Elbow, 0.7f)));
			// The top arm's elbow leads, out toward the bowler (the front elbow of the stance and the downswing); the
			// bottom arm's hangs down, tucked by the back hip. With the hand raised, "out" and "down" would wing the
			// elbow sideways, so it points forward under the hands instead, as in a high finish.
			const float Raised = FMath::SmoothStep(-20.f * Size, 10.f * Size, float(A.Wrist.Z - Shoulder.Z));
			const FVector Low = bTop ? Outward - Up * 0.3f : -Up + Outward * 0.3f;
			const FVector Prefer = (Low.GetSafeNormal() * (1.f - Raised) + Deep * Raised).GetSafeNormal();
			A.Cost += 1.f - ((A.Elbow - Shoulder - U * ((A.Elbow - Shoulder) | U)).GetSafeNormal() | (Prefer - U * (Prefer | U)).GetSafeNormal());
			if (bSolved)
				A.Cost += 3.f * (FMath::Square(FMath::FindDeltaAngleRadians(SwivelAngle, Swivel[H]) / PI) + FMath::Square(FMath::FindDeltaAngleRadians(RollAngle, Roll[H]) / PI));
			return A;
		};
		// ponytail: a 16 x 24 grid then two halving passes (~450 tries an arm, microseconds); a gradient step from last frame if profiling cares.
		float BestRoll = 0.f, BestSwivel = 0.f;
		FArm Best = Try(0.f, 0.f);
		auto Consider = [&](float RollAngle, float SwivelAngle)
		{
			const FArm A = Try(RollAngle, SwivelAngle);
			if (A.Cost < Best.Cost) { Best = A; BestRoll = RollAngle; BestSwivel = SwivelAngle; }
		};
		for (int32 I = 0; I < 16; ++I)
			for (int32 J = 0; J < 24; ++J) Consider(2.f * PI * I / 16.f, 2.f * PI * J / 24.f);
		for (float Step : { PI / 16.f, PI / 32.f, PI / 64.f })
		{
			const float R0 = BestRoll, S0 = BestSwivel;
			for (int32 I = -1; I <= 1; ++I)
				for (int32 J = -1; J <= 1; ++J) Consider(R0 + I * Step, S0 + J * Step);
		}
		Roll[H] = FMath::UnwindRadians(BestRoll);
		Swivel[H] = FMath::UnwindRadians(BestSwivel);

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
	FCS::ConvertComponentPosesToLocalPoses(MoveTemp(CS), Out);
}
