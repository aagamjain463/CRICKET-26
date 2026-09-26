// Animation automation tests: the stroke and bowling geometry that the procedural bodies are posed from.

#include "Misc/AutomationTest.h"
#include "BattingModel.h"
#include "CricketBatter.h"
#include "CricketPose.h"
#include "DeliveryResolver.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CricketAnimationTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimBatterPlan, "CRICKET26.Animation.BatterPlan", CricketAnimationTests::Flags)
bool FAnimBatterPlan::RunTest(const FString&)
{
	// Every stroke, right- and left-handed, sampled at 120 Hz from the backlift to past the recovery: the sweet
	// spot on the ball at the contact, a true bat frame, the handle within the arms' reach and off the chest, no
	// planted foot moving, no jumps, and a left-hander the exact mirror of a right-hander.
	struct FCase { EShotType Shot; FVector Contact; float Dir; };
	const FCase Cases[] = {
		{ EShotType::Defend, FVector(1.8f, 0.1f, 0.45f), 0.f },
		{ EShotType::Drive, FVector(2.f, 0.1f, 0.4f), 0.f },
		{ EShotType::Drive, FVector(2.f, 0.3f, 0.4f), 45.f },
		{ EShotType::Loft, FVector(2.f, 0.05f, 0.5f), 0.f },
		{ EShotType::Flick, FVector(2.f, -0.2f, 0.4f), -55.f },
		{ EShotType::Sweep, FVector(2.f, 0.f, 0.3f), -80.f },
		{ EShotType::Punch, FVector(1.f, 0.3f, 0.8f), 20.f },
		{ EShotType::Cut, FVector(1.f, 0.5f, 0.65f), 70.f },
		{ EShotType::Pull, FVector(1.f, 0.f, 1.f), -70.f },
		{ EShotType::Hook, FVector(1.f, -0.1f, 1.3f), -80.f },
	};
	constexpr float Dt = 1.f / 120.f;
	// Reach is the planner's comfortable, elbows-bent limit for the stroke. At rest in the stance the arms hang
	// nearly straight, which the nominal body allows up to its straight arm (the solve also lets the clavicles
	// reach forward a little).
	constexpr float StraightArm = 0.67f;
	for (const FCase& C : Cases)
	{
		auto Input = [&](ECricketHand Hand)
		{
			const float Off = OffSideSign(Hand);
			CricketBatter::FInput In;
			In.Off = Off;
			In.Home = FVector(0.9f, -0.35f * Off, 0.f);
			In.bStroke = true;
			In.Shot = C.Shot;
			In.Foot = CricketBatting::Profile(C.Shot).Foot;
			In.DirectionDeg = C.Dir;
			In.Press = 0.35f;
			In.Impact = 0.6f;
			In.Contact = FVector(C.Contact.X, C.Contact.Y * Off, C.Contact.Z);
			In.ShotDir = CricketBatting::DirectionToWorld(C.Dir, Hand).GetSafeNormal();
			In.Settle = In.Impact + 0.9f;
			return In;
		};
		const FString Name = FString::Printf(TEXT("shot %d dir %.0f"), int32(C.Shot), C.Dir);
		CricketBatter::FInput In = Input(ECricketHand::Right), Lh = Input(ECricketHand::Left);
		bool bFinite = true, bFrame = true, bMirror = true, bPlanted = true;
		float Cramped = 1.f, CrampAt = 0.f, JumpAt = 0.f, Closest = 1.f, WorstReach = -1.f, WorstArm = -1.f, WorstSpeed = 0.f, WorstAt = 0.f, ArmAt = 0.f, CloseAt = 0.f;
		CricketBatter::FBody Prev;
		for (float T = -1.f; T < In.Settle + CricketBatter::RecoverSeconds + 0.3f; T += Dt)
		{
			In.Time = Lh.Time = T;
			const CricketBatter::FBody B = CricketBatter::Plan(In), M = CricketBatter::Plan(Lh);
			const CricketPose::FBat& Bat = B.Bat;
			bFinite &= !Bat.Grip.ContainsNaN() && !Bat.Axis.ContainsNaN() && !Bat.Face.ContainsNaN() && !B.Pelvis.ContainsNaN() && FMath::IsFinite(B.Drop);
			bFrame &= FMath::IsNearlyEqual(Bat.Axis.Size(), 1.f, 1e-3f) && FMath::IsNearlyEqual(Bat.Face.Size(), 1.f, 1e-3f) && FMath::Abs(Bat.Axis | Bat.Face) < 1e-3f;
			// The hands 4.5 cm either side of the grip, the top hand's from the front shoulder.
			const FVector Top = Bat.Grip - Bat.Axis * 0.045f, Bottom = Bat.Grip + Bat.Axis * 0.045f;
			const float Arm = FMath::Max(FVector::Dist(Top, B.Shoulder[B.TopHand]), FVector::Dist(Bottom, B.Shoulder[1 - B.TopHand]));
			if (T >= In.Press && T <= In.Settle && Arm > WorstReach) { WorstReach = Arm; WorstAt = T; }
			if (Arm > WorstArm) { WorstArm = Arm; ArmAt = T; }
			const float Fold = FMath::Min(FVector::Dist(Top, B.Shoulder[B.TopHand]), FVector::Dist(Bottom, B.Shoulder[1 - B.TopHand]));
			if (Fold < Cramped) { Cramped = Fold; CrampAt = T; }
			// Off the spine at the grip's height (the spine leans forward of the pelvis as the chest bends).
			const float Along = FMath::Clamp((Bat.Grip.Z - (0.97f - B.Drop)) / FMath::Max(B.ChestUp.Z, 0.3f), 0.f, 0.49f);
			const float Clear = FVector2D::Distance(FVector2D(Bat.Grip), FVector2D(B.Pelvis + B.ChestUp * Along));
			if (Clear < Closest) { Closest = Clear; CloseAt = T; }
			auto Flip = [](const FVector& V) { return FVector(V.X, -V.Y, V.Z); };
			bMirror &= M.TopHand == 1 - B.TopHand && M.Bat.Grip.Equals(Flip(Bat.Grip), 1e-3f) && M.Bat.Axis.Equals(Flip(Bat.Axis), 1e-3f)
				&& M.Foot[1 - B.TopHand].Ball.Equals(Flip(B.Foot[B.TopHand].Ball), 1e-3f);
			if (T > -1.f)
			{
				const float Speed = FVector::Dist(Bat.Grip, Prev.Bat.Grip) / Dt;
				if (Speed > WorstSpeed) { WorstSpeed = Speed; JumpAt = T; }
				for (int32 F = 0; F < 2; ++F)
					if (B.Foot[F].Lift <= 0.f && Prev.Foot[F].Lift <= 0.f) bPlanted &= B.Foot[F].Ball.Equals(Prev.Foot[F].Ball, 1e-3f);
			}
			Prev = B;
		}
		In.Time = In.Impact;
		const CricketBatter::FBody AtImpact = CricketBatter::Plan(In);
		TestTrue(Name + TEXT(": finite"), bFinite);
		TestTrue(*FString::Printf(TEXT("%s: sweet spot on the ball at contact (%.1f cm off)"), *Name, 100.f * FVector::Dist(AtImpact.Bat.SweetSpot(), In.Contact)),
			AtImpact.Bat.SweetSpot().Equals(In.Contact, 0.02f));
		TestTrue(Name + TEXT(": orthonormal bat"), bFrame);
		TestTrue(*FString::Printf(TEXT("%s: hands within reach through the stroke (worst %.2f m of %.2f at %.2f s)"), *Name, WorstReach, CricketBatter::Reach, WorstAt),
			WorstReach <= CricketBatter::Reach + 0.02f);
		TestTrue(*FString::Printf(TEXT("%s: hands within a straight arm throughout (worst %.2f m of %.2f at %.2f s)"), *Name, WorstArm, StraightArm, ArmAt), WorstArm <= StraightArm);
		TestTrue(*FString::Printf(TEXT("%s: handle off the chest (closest %.2f m at %.2f s)"), *Name, Closest, CloseAt), Closest > 0.15f);
		TestTrue(*FString::Printf(TEXT("%s: arms not cramped (closest %.2f m at %.2f s)"), *Name, Cramped, CrampAt), Cramped >= CricketBatter::MinReach - 0.02f);
		TestTrue(Name + TEXT(": planted feet stay put"), bPlanted);
		TestTrue(*FString::Printf(TEXT("%s: no jumps (grip at most %.1f m/s, at %.2f s)"), *Name, WorstSpeed, JumpAt), WorstSpeed < 15.f);
		TestTrue(Name + TEXT(": left-hander mirrors right-hander"), bMirror);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimBowlingArm, "CRICKET26.Animation.BowlingArm", CricketAnimationTests::Flags)
bool FAnimBowlingArm::RunTest(const FString&)
{
	using namespace CricketPose;
	TestEqual(TEXT("released just past vertical"), BowlingArmAngle(0.f), ReleaseAngle, 1e-3f);
	TestEqual(TEXT("gathered down behind"), BowlingArmAngle(-0.4f), -150.f);
	float Prev = BowlingArmAngle(-1.f);
	for (float S = -1.f; S <= 1.f; S += 0.01f)
	{
		const float A = BowlingArmAngle(S);
		if (A < Prev - 1e-3f) { AddError(FString::Printf(TEXT("arm swings backward at %.2f s"), S)); break; }
		Prev = A;
	}
	TestEqual(TEXT("no action during the approach"), BowlingArmWeight(-1.f), 0.f);
	TestEqual(TEXT("full action at release"), BowlingArmWeight(0.f), 1.f);
	TestEqual(TEXT("back to running after"), BowlingArmWeight(1.f), 0.f);
	const FVector Shoulder(0.f, 0.f, 1.5f), Fwd(-1.f, 0.f, 0.f);
	TestTrue(TEXT("0 deg is straight up"), ArmCircle(Shoulder, Fwd, 0.f, 0.8f).Equals(Shoulder + FVector(0.f, 0.f, 0.8f), 1e-3f));
	TestTrue(TEXT("90 deg is ahead"), ArmCircle(Shoulder, Fwd, 90.f, 0.8f).Equals(Shoulder + Fwd * 0.8f, 1e-3f));
	TestTrue(TEXT("release is ahead of the shoulder"), ArmCircle(Shoulder, Fwd, ReleaseAngle, 0.8f).X < Shoulder.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimContactBatPos, "CRICKET26.Animation.ContactBatPosition", CricketAnimationTests::Flags)
bool FAnimContactBatPos::RunTest(const FString&)
{
	// The simulation reports where the batter put the bat so the body can be posed there: on the ball for a
	// middled stroke, off it for a miss, and nowhere for a leave.
	FBallState Ball;
	Ball.Pos = FVector(2.f, 0.1f, 0.4f);
	Ball.Vel = FVector(-30.f, 0.f, 3.f);
	const FCricketPlayer Batter;
	const FContactResult Middled = CricketBatting::ResolveContact(Ball, Ball.Pos, CricketBatting::Profile(EShotType::Drive), 0.f, 0.f, Batter, false);
	TestTrue(TEXT("middled: bat on the ball"), Middled.HasContact() && Middled.BatPos.Equals(Ball.Pos, 0.01f));
	const FContactResult Missed = CricketBatting::ResolveContact(Ball, Ball.Pos + FVector(0.f, 0.f, 0.4f), CricketBatting::Profile(EShotType::Drive), 0.f, 0.f, Batter, false);
	TestTrue(TEXT("missed: bat above the ball"), !Missed.HasContact() && Missed.BatPos.Z > Ball.Pos.Z + 0.2f);
	const FContactResult Left = CricketBatting::ResolveContact(Ball, Ball.Pos, CricketBatting::Profile(EShotType::Leave), 0.f, 0.f, Batter, false);
	TestTrue(TEXT("leave: no bat position"), Left.BatPos.IsZero());
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimFieldingClips, "CRICKET26.Animation.FieldingClips", CricketAnimationTests::Flags)
bool FAnimFieldingClips::RunTest(const FString&)
{
	using namespace CricketPose;
	// The throw's release lands on the simulation's, fully blended in, whether the fielder had a long or a short
	// time with the ball; it is gone before the ball is in hand and after the follow-through.
	for (const float Gap : { 0.25f, 0.6f, 1.5f })
	{
		const float Ready = 2.f, Release = Ready + Gap;
		TestEqual(TEXT("release on release"), ThrowClip(Release, Ready, Release).Time, ThrowClipRelease);
		TestEqual(TEXT("in by the release"), ThrowClip(Release, Ready, Release).Weight, 1.f);
		TestEqual(TEXT("not before the ball is in hand"), ThrowClip(Ready - 0.2f, Ready, Release).Weight, 0.f);
		TestEqual(TEXT("out after the follow-through"), ThrowClip(Release + 1.5f, Ready, Release).Weight, 0.f);
	}
	// The dive's hands are at full stretch on the take, and it is out once the fielder is up.
	TestEqual(TEXT("full stretch on the take"), DiveClip(4.f, 4.f).Time, DiveClipStretch);
	TestEqual(TEXT("in on the take"), DiveClip(4.f, 4.f).Weight, 1.f);
	TestEqual(TEXT("not before the dive"), DiveClip(4.f - DiveClipStretch - 0.1f, 4.f).Weight, 0.f);
	TestEqual(TEXT("out once up"), DiveClip(4.f - DiveClipStretch + DiveClipUp, 4.f).Weight, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimBallInHand, "CRICKET26.Animation.BallInHand", CricketAnimationTests::Flags)
bool FAnimBallInHand::RunTest(const FString&)
{
	// The ball is drawn in whoever has it (a captured dive or throw carries the body away from where it was taken):
	// the fielder from the take to the throw, the relay fielder from their catch to theirs, and after a catch to the end.
	FDeliveryResult R;
	R.Fielding.Fielder = 3;
	R.Fielding.FieldTime = 2.f;
	TestEqual(TEXT("nobody before the take"), R.HolderAt(1.9f), -1);
	TestEqual(TEXT("caught: kept"), R.HolderAt(9.f), 3);
	R.Running.ThrowRelease = 2.6f;
	TestEqual(TEXT("taken"), R.HolderAt(2.3f), 3);
	TestEqual(TEXT("thrown"), R.HolderAt(2.7f), -1);
	R.Running.RelayMove.Fielder = 5;
	R.Running.RelayCatch = 3.5f;
	R.Running.RelayRelease = 3.9f;
	TestEqual(TEXT("in the air to the relay"), R.HolderAt(3.f), -1);
	TestEqual(TEXT("with the relay"), R.HolderAt(3.6f), 5);
	TestEqual(TEXT("relayed on"), R.HolderAt(4.f), -1);
	R.Fielding.Boundary = 4;
	TestEqual(TEXT("a boundary is nobody's"), R.HolderAt(2.3f), -1);
	return true;
}

#endif
