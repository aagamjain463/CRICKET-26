// Cricket actions as geometry: the bat, the fielders' captured throw and dive timed onto the simulation, and
// the bowling and throwing arm windmilling about the shoulder in the plane of the delivery. The striker's
// stroke is CricketBatter's. Everything is in the simulation frame (metres).

#pragma once

#include "CoreMinimal.h"
#include "CricketTypes.h"

namespace CricketPose
{
	constexpr float BatLength = 0.85f;     // the drawn bat
	constexpr float HandleLength = 0.29f;
	constexpr float GripFromTop = 0.12f;   // centre of the two hands on the handle
	constexpr float SweetFromGrip = 0.55f; // grip to the middle of the sweet spot

	struct FBat
	{
		FVector Grip = FVector::ZeroVector;
		FVector Axis = -FVector::UpVector;  // grip toward the toe
		FVector Face = FVector::ForwardVector; // the way the blade's face points
		FVector SweetSpot() const { return Grip + Axis * SweetFromGrip; }
		FVector Top() const { return Grip - Axis * GripFromTop; }
	};

	/** Blends two bat poses (0 = A). */
	FBat Blend(const FBat& A, const FBat& B, float Alpha);

	/**
	 * A bat carried in one hand wherever the idle or the jog leaves that hand, from its elbow and wrist: the blade
	 * angled down ahead of the forearm, flatter as the arm swings forward, its face to the Right.
	 */
	FBat CarriedBat(const FVector& Elbow, const FVector& Wrist, const FVector& Forward, const FVector& Right);

	/** Where to play a clip, and how much of it over what is under it. */
	struct FClipPlay
	{
		float Time = 0.f;   // s into the clip
		float Weight = 0.f; // 0 to 1
	};

	/**
	 * The captured throw (Mixamo "Baseball Pitching", released ThrowClipRelease s in) for a fielder with the ball in
	 * hand from Ready who lets go at Release, times after contact: its release lands on the simulation's. A short gap
	 * joins the wind-up part way through rather than squeezing it.
	 */
	FClipPlay ThrowClip(float Post, float Ready, float Release);
	constexpr float ThrowClipRelease = 1.6f;

	/**
	 * The captured dive (Mixamo "Goalkeeper Diving Save", hands at full stretch DiveClipStretch s in) for a take at
	 * FieldTime: the hands reach out on the take, then the fielder lies and gets up.
	 * Premium fielding keeps this clip ONLY for genuine full-stretch takes (UseDiveClip): routine
	 * flat/high/low catches stay on their feet with cupped hands, so the catch never pops into a
	 * soccer-keeper dive. Slide stops never use this clip; they lean and slide procedurally.
	 */
	FClipPlay DiveClip(float Post, float FieldTime);
	/**
	 * A clip's component-space offset C (cm) in its actor's frame (m), for a body whose world rotation is BodyQuat on an
	 * actor turned ActorQuat. A MetaHuman's body sits inside its own blueprint actor, so its relative rotation is not
	 * its turn from the figure: this is.
	 */
	FVector ClipInActor(const FQuat& ActorQuat, const FQuat& BodyQuat, const FVector& BodyScale, const FVector& C);
	/** A figure now at Current tipped so its up is Up: a lean or a slide that keeps the way it faces, frame after frame. */
	FQuat LeanRotation(const FQuat& Current, const FVector& Up);
	constexpr float DiveClipLaunch = 0.4f, DiveClipStretch = 1.25f, DiveClipLanded = 1.6f, DiveClipUp = 3.2f;
	/** How much of the dive clip is in at clip Time: in over the last of the shuffle, all in by the launch. */
	float DiveClipIn(float Time);

	/**
	 * Premium outfield catch choreography (original, inspired by broadcast reference: ready crouch,
	 * split-step, tracking, cupped take, absorb-and-secure, crow-hop throw). Keeps the simulation
	 * authoritative: positions/times come from the solver, this only shapes the body.
	 * LateralM is sideways metres from the fielder's chest line to the take, HeightM the take height.
	 * All weights ease with SmoothStep so there are no pops.
	 */
	bool UseDiveClip(int32 Action, bool bDive, float LateralM, float HeightM);
	/** Ready crouch depth (m) for the taker before the take; 0 for non-takers walking in. */
	float CatchReadyDrop(int32 Action, bool bTaker);
	/**
	 * How much of the taker's CatchReadyDrop is in: none through the chase, in over the last strides into the take,
	 * held through the take and out as they come up with the ball. The long barrier knelt from the moment of contact.
	 */
	float TakeCrouchWeight(float Post, float FieldTime);
	/** Glove offset from the chest (m, actor space: X forward to ball, Y sideways, Z up) for the cup. */
	FVector CatchGloveOffset(int32 Action, float HeightM, float LateralM);
	/**
	 * Where the gloves go after a take that met them at Cup (both offsets from the chest as CatchGloveOffset's):
	 * from the cup at Give 0 in toward the belly, forward of it, as Give reaches its full 0.14.
	 */
	FVector CatchSecureOffset(const FVector& Cup, float Give);
	/** How far the gloves give toward the chest after the take (m, 0 before, up to ~0.14). */
	float CatchGive(float Post, float FieldTime);
	/** Secure weight: 1 from the take through HoldUntil, out over 0.45 s. */
	float CatchSecure(float Post, float FieldTime, float HoldUntil);
	/** Crow-hop gather before an overarm throw (0..1): pelvis/shoulders shift toward the target. */
	float ThrowGatherWeight(float Post, float Release);

	/**
	 * The authored bowling action (Scripts/anim/author_bowl.py: the last strides of the run-in, the bound, the delivery
	 * stride and the follow-through, released BowlClipRelease s in) at TimeToRelease (negative before) for a run-up
	 * that set off at Start: it holds the first stride's pose until then, comes in over the first stride and goes out
	 * as the follow-through ends.
	 */
	FClipPlay BowlClip(float TimeToRelease, float Start);
	constexpr float BowlClipRelease = 1.f, BowlClipEnd = 2.f;

	/**
	 * The authored run-in brings the bowler's legs across the line of the stumps at their end. The body passes them
	 * pushed this far sideways (m, away from the stumps), fading out by the release so the hand still lets go at the
	 * simulation's release point. StumpBones names the joints checked, with their radius; BoneAt(I, T) is joint I at
	 * clip time T on the unpushed path (m, simulation frame) and joint 0 is the pelvis. Side is +1 when the body runs
	 * in on the +Y side of the stumps, -1 on the -Y side.
	 */
	struct FStumpBone { const TCHAR* Name; float Radius; };
	inline constexpr FStumpBone StumpBones[] = { { TEXT("pelvis"), 0.18f }, { TEXT("thigh_l"), 0.08f }, { TEXT("thigh_r"), 0.08f },
		{ TEXT("calf_l"), 0.06f }, { TEXT("calf_r"), 0.06f }, { TEXT("foot_l"), 0.05f }, { TEXT("foot_r"), 0.05f },
		{ TEXT("ball_l"), 0.04f }, { TEXT("ball_r"), 0.04f } };
	constexpr float StumpGap = 0.08f; // clear air between a leg and the stumps
	float StumpPush(TFunctionRef<FVector(int32 Bone, float T)> BoneAt, float Side);
	/** How much of the push applies with the pelvis at PelvisX: all of it from just short of the stumps back, none by the release. */
	float StumpPushWeight(float PelvisX, float ReleasePelvisX);

	/**
	 * A hand windmilling about the shoulder for a bowling or throwing action: 0 straight up, positive
	 * angles over the top toward Forward, -180 straight down.
	 */
	FVector ArmCircle(const FVector& Shoulder, const FVector& Forward, float AngleDeg, float Reach);

	/**
	 * Bowling arm angle from the time to release (negative before): swung down and back in the gather, over
	 * the top to just past vertical at release, then down across the body.
	 */
	float BowlingArmAngle(float TimeToRelease);
	/** How much of the bowling action is layered over the run-up (0 during the approach). */
	float BowlingArmWeight(float TimeToRelease);
	/**
	 * An underarm flick's arm angle at a time from the release (negative before): back behind the hip, swung low
	 * through under the shoulder and let go forward of the hip, then up a little into the follow-through. Never over
	 * the top. Weighted in and out as BowlingArmWeight.
	 */
	float UnderarmArmAngle(float TimeToRelease);
	constexpr float ReleaseAngle = 10.f;
}
