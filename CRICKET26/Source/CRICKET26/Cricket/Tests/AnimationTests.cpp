// Animation automation tests: the stroke and bowling geometry that the procedural bodies are posed from.

#include "Misc/AutomationTest.h"
#include "BattingModel.h"
#include "CricketPose.h"
#include "DeliveryResolver.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimStrokeClips, "CRICKET26.Animation.StrokeClips", CricketAnimationTests::Flags)
bool FAnimStrokeClips::RunTest(const FString&)
{
	using namespace CricketPose;
	// Every stroke has a captured take, chosen by footwork and direction, and a leave has none.
	TestTrue(TEXT("leave: no clip"), StrokeClip(EShotType::Leave, EFootwork::Front, 0.f).Name == nullptr);
	for (int32 S = int32(EShotType::Defend); S <= int32(EShotType::Scoop); ++S)
		TestTrue(*FString::Printf(TEXT("shot %d has a clip"), S), StrokeClip(EShotType(S), EFootwork::Front, 0.f).Name != nullptr);
	TestEqual(TEXT("back-foot defence"), FString(StrokeClip(EShotType::Defend, EFootwork::Back, 0.f).Name), FString(TEXT("Bat_Defend_Back")));
	TestEqual(TEXT("cover drive"), FString(StrokeClip(EShotType::Drive, EFootwork::Front, 45.f).Name), FString(TEXT("Bat_Drive_Cover")));
	TestEqual(TEXT("straight drive"), FString(StrokeClip(EShotType::Drive, EFootwork::Front, 0.f).Name), FString(TEXT("Bat_Drive")));

	// The clip's bat meets the ball exactly when the simulation's does, however quick the swing, and the clip
	// never runs backwards.
	const FStrokeClip Drive = StrokeClip(EShotType::Drive, EFootwork::Front, 0.f);
	for (const float Swing : { 0.15f, 0.25f, 0.6f })
	{
		const float Press = 3.f, Impact = Press + Swing;
		TestEqual(TEXT("downswing starts at the press"), StrokeClipTime(Drive, Press, Press, Impact), Drive.Contact - ClipDownswing);
		TestEqual(TEXT("contact on the impact"), StrokeClipTime(Drive, Impact, Press, Impact), Drive.Contact);
		TestEqual(TEXT("real time after it"), StrokeClipTime(Drive, Impact + 0.3f, Press, Impact), Drive.Contact + 0.3f, 1e-4f);
		float Last = -1.f;
		bool bForward = true;
		for (float T = Press - 0.5f; T < Impact + 1.f; T += 0.01f)
		{
			const float C = StrokeClipTime(Drive, T, Press, Impact);
			bForward &= C >= Last;
			Last = C;
		}
		TestTrue(TEXT("never backwards"), bForward);
	}
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
