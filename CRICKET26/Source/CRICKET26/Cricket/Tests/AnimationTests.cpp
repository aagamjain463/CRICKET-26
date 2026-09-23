// Animation automation tests: the stroke and bowling geometry that the procedural bodies are posed from.

#include "Misc/AutomationTest.h"
#include "BattingModel.h"
#include "CricketPose.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CricketAnimationTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimStrokeMeetsBall, "CRICKET26.Animation.StrokeMeetsBall", CricketAnimationTests::Flags)
bool FAnimStrokeMeetsBall::RunTest(const FString&)
{
	using namespace CricketPose;
	// A right-hander (off side +Y) and a left-hander (mirrored) playing the same strokes: at angle 0 the
	// sweet spot is on the contact, the grip is within the arms' reach all the way through the swing, and
	// the downswing only ever moves toward the ball.
	for (const float Off : { 1.f, -1.f })
	{
		const FVector Pivot(-0.2f, -0.35f * Off, 1.35f);
		struct FCase { EShotType Shot; FVector Contact; FVector Dir; };
		const FCase Cases[] = {
			{ EShotType::Drive, FVector(0.5f, 0.1f * Off, 0.35f), FVector(1.f, 0.3f * Off, 0.f) },
			{ EShotType::Defend, FVector(0.4f, 0.f, 0.3f), FVector(1.f, 0.f, 0.f) },
			{ EShotType::Pull, FVector(0.3f, -0.1f * Off, 1.1f), FVector(0.f, -1.f * Off, 0.1f) },
			{ EShotType::Cut, FVector(0.2f, 0.7f * Off, 0.8f), FVector(0.f, 1.f * Off, 0.f) },
			{ EShotType::Loft, FVector(0.6f, 0.1f * Off, 0.15f), FVector(1.f, 0.f, 0.6f) },   // beyond the arms: leans in
			{ EShotType::Hook, FVector(0.1f, -0.2f * Off, 1.3f), FVector(-0.3f, -1.f * Off, 0.3f) }, // cramped: sways away
		};
		for (const FCase& C : Cases)
		{
			const FString Name = FString::Printf(TEXT("shot %d off %+.0f"), int32(C.Shot), Off);
			const FSwing S = PlanSwing(C.Shot, Pivot, C.Contact, C.Dir);
			TestTrue(Name + TEXT(": sweet spot on the ball"), BatAt(S, 0.f).SweetSpot().Equals(C.Contact, 0.02f));
			TestTrue(Name + TEXT(": lean bounded"), S.Lean.Size() <= MaxLean + 1e-3f);
			float Prev = StrokeAngle(S, 0.f, 0.1f, 0.4f);
			TestEqual(Name + TEXT(": held at the backlift"), Prev, -S.Backlift);
			TestEqual(Name + TEXT(": meets the ball on time"), StrokeAngle(S, 0.4f, 0.1f, 0.4f), 0.f, 1e-3f);
			for (float T = 0.f; T <= 1.f; T += 0.01f)
			{
				const float A = StrokeAngle(S, T, 0.1f, 0.4f);
				if (A < Prev - 1e-3f) { AddError(FString::Printf(TEXT("%s: swing reverses at %.2f s"), *Name, T)); break; }
				Prev = A;
				const FBat B = BatAt(S, A);
				if ((B.Grip - S.Pivot).Size() > MaxGripReach + 1e-3f) { AddError(FString::Printf(TEXT("%s: grip out of reach at %.0f deg"), *Name, A)); break; }
				if (!FMath::IsNearlyEqual(B.Axis.Size(), 1.f, 1e-3f) || FMath::Abs(FVector::DotProduct(B.Axis, B.Face)) > 0.05f)
				{
					AddError(FString::Printf(TEXT("%s: bat frame skewed at %.0f deg"), *Name, A));
					break;
				}
			}
		}
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

#endif
