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
	struct FCase { EShotType Shot; FVector Contact; float Dir; bool bAdvance = false; };
	const FCase Cases[] = {
		{ EShotType::Defend, FVector(1.8f, 0.1f, 0.45f), 0.f },
		{ EShotType::Drive, FVector(2.f, 0.1f, 0.4f), 0.f },
		{ EShotType::Drive, FVector(2.f, 0.3f, 0.4f), 45.f },
		{ EShotType::Loft, FVector(2.f, 0.05f, 0.5f), 0.f },
		{ EShotType::Flick, FVector(2.f, -0.2f, 0.4f), -55.f },
		{ EShotType::Sweep, FVector(2.f, 0.f, 0.3f), -80.f },
		{ EShotType::SlogSweep, FVector(2.f, 0.f, 0.3f), -70.f },
		{ EShotType::Punch, FVector(1.f, 0.3f, 0.8f), 20.f },
		{ EShotType::Cut, FVector(1.f, 0.5f, 0.65f), 70.f },
		{ EShotType::Pull, FVector(1.f, 0.f, 1.f), -70.f },
		{ EShotType::Hook, FVector(1.f, -0.1f, 1.3f), -80.f },
		// Down the track, near and far (AdvanceX spans 2.6 to 4.4 m, further for an earlier charge).
		{ EShotType::Drive, FVector(2.8f, 0.1f, 0.4f), 0.f, true },
		{ EShotType::Loft, FVector(4.4f, 0.05f, 0.5f), 0.f, true },
		{ EShotType::Drive, FVector(3.8f, 0.3f, 0.4f), 40.f, true },
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
			In.Foot = C.bAdvance ? EFootwork::Advance : CricketBatting::Profile(C.Shot).Foot;
			In.DirectionDeg = C.Dir;
			In.Impact = C.bAdvance ? 0.95f : 0.6f;
			// A charge that far out was committed early enough to get there (DownTheTrack): the ball, at a spinner's
			// ~24 m/s, arrives at the stumps X / 24 s after the contact.
			In.Press = C.bAdvance ? In.Impact + C.Contact.X / 24.f - FMath::GetMappedRangeValueClamped(FVector2f(3.4f, 4.4f),
				FVector2f(CricketBatting::AdvanceLead, 0.85f), C.Contact.X) : 0.35f;
			In.Contact = FVector(C.Contact.X, C.Contact.Y * Off, C.Contact.Z);
			In.ShotDir = CricketBatting::DirectionToWorld(C.Dir, Hand).GetSafeNormal();
			In.Settle = In.Impact + 0.9f;
			return In;
		};
		const FString Name = FString::Printf(TEXT("shot %d dir %.0f%s"), int32(C.Shot), C.Dir, C.bAdvance ? TEXT(" advancing") : TEXT(""));
		CricketBatter::FInput In = Input(ECricketHand::Right), Lh = Input(ECricketHand::Left);
		bool bFinite = true, bFrame = true, bMirror = true, bPlanted = true;
		const bool bSweep = C.Shot == EShotType::Sweep || C.Shot == EShotType::SlogSweep;
		float Past = -1.f, PastAt = 0.f, Sunk = 0.f, SunkAt = 0.f; // hips ahead of the leading foot; knees' bend
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
			const float Ahead = B.Pelvis.X - FMath::Max(B.Foot[0].Ball.X, B.Foot[1].Ball.X);
			if (Ahead > Past) { Past = Ahead; PastAt = T; }
			if (B.Drop > Sunk) { Sunk = B.Drop; SunkAt = T; }
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
		// The legs carry the body: the hips never pass the leading foot, and only a sweep sinks toward the turf.
		TestTrue(*FString::Printf(TEXT("%s: hips behind the leading foot (worst %.2f m ahead at %.2f s)"), *Name, Past, PastAt), Past <= 0.05f);
		TestTrue(*FString::Printf(TEXT("%s: knees clear of the turf (drop %.2f m at %.2f s)"), *Name, Sunk, SunkAt), bSweep || Sunk <= 0.45f);
		TestTrue(*FString::Printf(TEXT("%s: no jumps (grip at most %.1f m/s, at %.2f s)"), *Name, WorstSpeed, JumpAt), WorstSpeed < 15.f);
		TestTrue(Name + TEXT(": left-hander mirrors right-hander"), bMirror);
	}
	// A slog sweep's arms swing up to a high finish, as in the broadcast reference; a sweep rolls round at chest height.
	auto FinishZ = [](EShotType Shot)
	{
		CricketBatter::FInput In;
		In.Home = FVector(0.9f, -0.35f, 0.f);
		In.bStroke = true;
		In.Shot = Shot;
		In.Foot = CricketBatting::Profile(Shot).Foot;
		In.DirectionDeg = -70.f;
		In.Press = 0.35f;
		In.Impact = 0.6f;
		In.Contact = FVector(2.f, 0.f, 0.3f);
		In.ShotDir = CricketBatting::DirectionToWorld(-70.f, ECricketHand::Right).GetSafeNormal();
		In.Time = In.Impact + 0.5f;
		return CricketBatter::Plan(In).Bat.Grip.Z;
	};
	const float Sweep = FinishZ(EShotType::Sweep), Slog = FinishZ(EShotType::SlogSweep);
	TestTrue(*FString::Printf(TEXT("slog sweep finishes high (hands %.2f m, sweep %.2f m)"), Slog, Sweep), Slog > Sweep + 0.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimBatterStyles, "CRICKET26.Animation.BatterStyles", CricketAnimationTests::Flags)
bool FAnimBatterStyles::RunTest(const FString&)
{
	const EBatterStyle Styles[] = {
		EBatterStyle::Classical,
		EBatterStyle::ExpressPuller,
		EBatterStyle::Unorthodox,
		EBatterStyle::PowerHitter
	};

	constexpr float Dt = 1.f / 60.f;
	constexpr float StraightArm = 0.67f;

	for (EBatterStyle Style : Styles)
	{
		const EShotType Shots[] = { EShotType::Drive, EShotType::Pull, EShotType::Flick, EShotType::Loft };
		for (EShotType Shot : Shots)
		{
			auto Input = [&](ECricketHand Hand)
			{
				const float Off = OffSideSign(Hand);
				CricketBatter::FInput In;
				In.Off = Off;
				In.Home = FVector(0.9f, -0.35f * Off, 0.f);
				In.bStroke = true;
				In.Shot = Shot;
				In.Style = Style;
				In.Foot = CricketBatting::Profile(Shot).Foot;
				In.DirectionDeg = Shot == EShotType::Pull ? -70.f : Shot == EShotType::Flick ? -45.f : 20.f;
				In.Press = 0.35f;
				In.Impact = 0.6f;
				In.Contact = Shot == EShotType::Pull ? FVector(1.f, 0.f, 1.f) : FVector(2.f, 0.2f * Off, 0.4f);
				In.ShotDir = CricketBatting::DirectionToWorld(In.DirectionDeg, Hand).GetSafeNormal();
				In.Settle = In.Impact + 0.9f;
				return In;
			};

			CricketBatter::FInput In = Input(ECricketHand::Right), Lh = Input(ECricketHand::Left);
			bool bFinite = true, bFrame = true, bMirror = true, bPlanted = true;
			float WorstReach = 0.f, WorstArm = 0.f, WorstSpeed = 0.f;
			CricketBatter::FBody Prev;

			for (float T = -0.5f; T < In.Settle + CricketBatter::RecoverSeconds; T += Dt)
			{
				In.Time = Lh.Time = T;
				const CricketBatter::FBody B = CricketBatter::Plan(In), M = CricketBatter::Plan(Lh);
				const CricketPose::FBat& Bat = B.Bat;

				bFinite &= !Bat.Grip.ContainsNaN() && !Bat.Axis.ContainsNaN() && !Bat.Face.ContainsNaN();
				bFrame &= FMath::IsNearlyEqual(Bat.Axis.Size(), 1.f, 1e-3f) && FMath::IsNearlyEqual(Bat.Face.Size(), 1.f, 1e-3f);

				const FVector Top = Bat.Grip - Bat.Axis * 0.045f, Bottom = Bat.Grip + Bat.Axis * 0.045f;
				const float Arm = FMath::Max(FVector::Dist(Top, B.Shoulder[B.TopHand]), FVector::Dist(Bottom, B.Shoulder[1 - B.TopHand]));
				if (T >= In.Press && T <= In.Settle && Arm > WorstReach) WorstReach = Arm;
				if (Arm > WorstArm) WorstArm = Arm;

				auto Flip = [](const FVector& V) { return FVector(V.X, -V.Y, V.Z); };
				bMirror &= M.TopHand == 1 - B.TopHand && M.Bat.Grip.Equals(Flip(Bat.Grip), 1e-3f);

				if (T > -0.5f)
				{
					const float Speed = FVector::Dist(Bat.Grip, Prev.Bat.Grip) / Dt;
					if (Speed > WorstSpeed) WorstSpeed = Speed;
					for (int32 F = 0; F < 2; ++F)
						if (B.Foot[F].Lift <= 0.f && Prev.Foot[F].Lift <= 0.f)
							bPlanted &= B.Foot[F].Ball.Equals(Prev.Foot[F].Ball, 1e-3f);
				}
				Prev = B;
			}

			In.Time = In.Impact;
			const CricketBatter::FBody AtImpact = CricketBatter::Plan(In);
			const FString Name = FString::Printf(TEXT("Style %d Shot %d"), int32(Style), int32(Shot));

			TestTrue(Name + TEXT(": finite"), bFinite);
			TestTrue(Name + TEXT(": sweet spot on ball"), AtImpact.Bat.SweetSpot().Equals(In.Contact, 0.02f));
			TestTrue(Name + TEXT(": hands within reach"), WorstReach <= CricketBatter::Reach + 0.02f);
			TestTrue(Name + TEXT(": straight arm limit"), WorstArm <= StraightArm);
			TestTrue(Name + TEXT(": planted feet stay put"), bPlanted);
			TestTrue(Name + TEXT(": no jumps"), WorstSpeed < 15.f);
			TestTrue(Name + TEXT(": left-right mirror"), bMirror);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimMissReaction, "CRICKET26.Animation.MissReaction", CricketAnimationTests::Flags)
bool FAnimMissReaction::RunTest(const FString&)
{
	CricketBatter::FInput In;
	In.Home = FVector(0.9f, -0.35f, 0.f);
	In.bStroke = true;
	In.bMiss = true;
	In.Shot = EShotType::Drive;
	In.Foot = EFootwork::Front;
	In.DirectionDeg = 30.f;
	In.Press = 0.35f;
	In.Impact = 0.6f;
	In.Contact = FVector(2.f, 0.3f, 0.4f);
	In.ShotDir = CricketBatting::DirectionToWorld(30.f, ECricketHand::Right).GetSafeNormal();
	In.Settle = In.Impact + 1.2f;

	constexpr float Dt = 1.f / 60.f;
	float WorstSpeed = 0.f;
	CricketBatter::FBody Prev;
	bool bPlanted = true;

	for (float T = -0.5f; T < In.Settle + CricketBatter::RecoverSeconds; T += Dt)
	{
		In.Time = T;
		const CricketBatter::FBody B = CricketBatter::Plan(In);
		if (T > -0.5f)
		{
			const float Speed = FVector::Dist(B.Bat.Grip, Prev.Bat.Grip) / Dt;
			if (Speed > WorstSpeed) WorstSpeed = Speed;
			for (int32 F = 0; F < 2; ++F)
				if (B.Foot[F].Lift <= 0.f && Prev.Foot[F].Lift <= 0.f)
					bPlanted &= B.Foot[F].Ball.Equals(Prev.Foot[F].Ball, 1e-3f);
		}
		Prev = B;
	}

	TestTrue(TEXT("Miss reaction: no jumps"), WorstSpeed < 15.f);
	TestTrue(TEXT("Miss reaction: feet stay planted"), bPlanted);
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
	// An underarm flick stays below the shoulder all the way: back, down through, and out ahead at the release.
	for (float S = -0.6f; S <= 1.f; S += 0.01f)
		if (ArmCircle(Shoulder, Fwd, UnderarmArmAngle(S), 0.8f).Z > Shoulder.Z - 0.05f) { AddError(FString::Printf(TEXT("underarm hand over the shoulder at %.2f s"), S)); break; }
	TestTrue(TEXT("underarm starts behind"), ArmCircle(Shoulder, Fwd, UnderarmArmAngle(-0.3f), 0.8f).X > Shoulder.X);
	TestTrue(TEXT("underarm lets go ahead"), ArmCircle(Shoulder, Fwd, UnderarmArmAngle(0.f), 0.8f).X < Shoulder.X);
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

	// Premium fielding: the captured dive only owns full-stretch takes, so routine catches never
	// pop into a soccer-keeper dive. Actions: CatchDiving=4, SlideStop=12, DiveStop=13.
	TestTrue(TEXT("routine flat catch stays on feet"), !UseDiveClip(1, false, 0.3f, 1.2f));
	TestTrue(TEXT("routine high catch stays on feet"), !UseDiveClip(2, false, 0.4f, 1.9f));
	TestTrue(TEXT("routine low catch stays on feet"), !UseDiveClip(3, false, 0.3f, 0.5f));
	TestTrue(TEXT("diving catch uses the clip"), UseDiveClip(4, true, 1.4f, 1.2f));
	TestTrue(TEXT("dive stop uses the clip"), UseDiveClip(13, true, 1.2f, 0.8f));
	TestTrue(TEXT("slide never uses the dive clip"), !UseDiveClip(12, true, 1.5f, 0.6f));
	TestTrue(TEXT("flat catch at full stretch only dives sideways"), UseDiveClip(1, true, 1.2f, 1.2f));
	TestTrue(TEXT("flat catch steps in, not dives"), !UseDiveClip(1, true, 0.4f, 1.2f));
	// A catch the solver could only reach at full stretch dives however low: ball 10's diving catch at 0.21 m
	// was left standing with the hands clutched at the chest.
	TestTrue(TEXT("low diving catch dives full length"), UseDiveClip(4, true, 2.25f, 0.21f));
	TestTrue(TEXT("grass-level dive stop dives"), UseDiveClip(13, true, 1.4f, 0.05f));
	// The dive is placed from the clip in the actor's frame. A MetaHuman body is turned -90 inside a blueprint that is
	// itself turned, so its relative rotation read 0 and the dive landed 90 degrees off, hands 4.8 m from the ball.
	{
		const FQuat Actor(FRotator(0.f, -49.8f, 0.f)), Body(FRotator(0.f, -139.8f, 0.f));
		const FVector Ahead = ClipInActor(Actor, Body, FVector::OneVector, FVector(0.f, 100.f, 0.f)); // the mannequin faces +Y
		TestTrue(TEXT("clip forward is the actor's forward"), Ahead.Equals(FVector(1.f, 0.f, 0.f), 1e-3f));
		TestTrue(TEXT("clip scaled with the body"), ClipInActor(Actor, Body, FVector(1.05f), FVector(0.f, 100.f, 0.f)).Equals(FVector(1.05f, 0.f, 0.f), 1e-3f));
	}
	// The primary's slide lean was built from its up alone (MakeFromZ), which picks yaw -90 when upright: every
	// frame of a live ball a standing keeper was turned side-on to the leg side.
	{
		const FQuat Facing(FRotator(0.f, 35.f, 0.f));
		TestTrue(TEXT("upright keeps its facing"), LeanRotation(FQuat::Identity, FVector::UpVector).GetForwardVector().Equals(FVector(1.f, 0.f, 0.f), 1e-4f));
		const FVector Tip = FVector(0.3f, 0.5f, 0.8f).GetSafeNormal();
		FQuat Q = Facing;
		for (int32 Frame = 0; Frame < 120; ++Frame) Q = LeanRotation(Q, Tip); // two seconds of a slide
		TestTrue(TEXT("lean up is the slide's up"), Q.GetUpVector().Equals(Tip, 1e-4f));
		TestTrue(TEXT("a long lean keeps the heading"), LeanRotation(Q, FVector::UpVector).Equals(Facing, 1e-4f));
	}
	TestEqual(TEXT("dive clip skips the keeper shuffle"), DiveClip(4.f - DiveClipStretch + 0.1f, 4.f).Weight, 0.f);
	TestEqual(TEXT("dive clip all in by the launch"), DiveClip(4.f - DiveClipStretch + DiveClipLaunch, 4.f).Weight, 1.f);
	TestTrue(TEXT("too high stays on feet"), !UseDiveClip(4, true, 1.4f, 2.4f));
	// Catch cups stay in front of the body, never crossed or behind the head.
	for (const int32 Act : { 1, 2, 3, 4, 6, 7, 9, 10, 11 })
	{
		const FVector Off = CatchGloveOffset(Act, 1.2f, 0.6f);
		TestTrue(*FString::Printf(TEXT("action %d gloves forward"), Act), Off.X > 0.2f && Off.X < 0.7f);
		TestTrue(*FString::Printf(TEXT("action %d gloves within reach"), Act), FMath::Abs(Off.Y) <= 1.2f && Off.Z > -1.f && Off.Z < 0.8f);
	}
	TestTrue(TEXT("high cup is overhead"), CatchGloveOffset(2, 1.9f, 0.2f).Z > 0.25f);
	TestTrue(TEXT("low cup is down"), CatchGloveOffset(3, 0.4f, 0.2f).Z < -0.5f);
	// Absorb gives toward the chest only after the take; secure holds then releases smoothly.
	TestEqual(TEXT("no give before the take"), CatchGive(1.9f, 2.f), 0.f);
	TestTrue(TEXT("give grows after the take"), CatchGive(2.1f, 2.f) > 0.f && CatchGive(2.1f, 2.f) <= 0.14f + 1e-4f);
	// Secured in front of the belly, never pulled up to the chin: the post-take hunch had the hands at the face.
	for (const int32 Act : { 1, 2, 3, 4, 6, 9, 11 })
	{
		const FVector Cup = CatchGloveOffset(Act, Act == 2 ? 2.1f : Act == 3 ? 0.3f : 1.2f, 0.4f);
		const FVector Held = CatchSecureOffset(Cup, 0.14f);
		TestTrue(*FString::Printf(TEXT("action %d secured below the chest"), Act), Held.Z <= -0.25f);
		TestTrue(*FString::Printf(TEXT("action %d secured forward of the body"), Act), Held.X >= 0.25f);
		TestEqual(*FString::Printf(TEXT("action %d meets the cup on the take"), Act), CatchSecureOffset(Cup, 0.f), Cup);
	}
	// The taker crouches into the take, not through the chase (ball 4's long barrier knelt at contact, then ran).
	TestEqual(TEXT("no crouch through the chase"), TakeCrouchWeight(0.2f, 3.3f), 0.f);
	TestEqual(TEXT("crouched on the take"), TakeCrouchWeight(3.3f, 3.3f), 1.f);
	TestEqual(TEXT("up again after"), TakeCrouchWeight(4.2f, 3.3f), 0.f);
	TestEqual(TEXT("no secure before the take"), CatchSecure(1.9f, 2.f, 2.35f), 0.f);
	TestEqual(TEXT("secure through the hold"), CatchSecure(2.2f, 2.f, 2.35f), 1.f);
	TestTrue(TEXT("secure releases after the hold"), CatchSecure(2.7f, 2.f, 2.35f) < 1.f && CatchSecure(3.f, 2.f, 2.35f) <= 0.01f);
	// Crow-hop gathers into the release and is gone just after.
	TestEqual(TEXT("no gather early"), ThrowGatherWeight(1.f, 2.f), 0.f);
	TestTrue(TEXT("gather peaks at release"), ThrowGatherWeight(1.97f, 2.f) > 0.8f);
	TestEqual(TEXT("gather gone after"), ThrowGatherWeight(2.3f, 2.f), 0.f);

	// The bowling action lets go on the simulation's release, whenever the run-up set off. Waiting at the mark (the
	// game's time to release is -100 then) it shows the stride the run-up starts on, so setting off never jumps.
	for (const float Start : { -0.7f, -0.9f, -1.1f })
	{
		TestEqual(TEXT("release on release"), BowlClip(0.f, Start).Time, BowlClipRelease);
		TestEqual(TEXT("in by release"), BowlClip(0.f, Start).Weight, 1.f);
		TestEqual(TEXT("not while waiting"), BowlClip(-100.f, Start).Weight, 0.f);
		TestEqual(TEXT("waits on the first stride"), BowlClip(-100.f, Start).Time, BowlClip(Start, Start).Time);
		TestEqual(TEXT("out after follow-through"), BowlClip(BowlClipEnd - BowlClipRelease, Start).Weight, 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimBowlerClearsStumps, "CRICKET26.Animation.BowlerClearsStumps", CricketAnimationTests::Flags)
bool FAnimBowlerClearsStumps::RunTest(const FString&)
{
	using namespace CricketPose;
	using namespace CricketGeo;
	// Regression: the authored run-in carried the bowler's right knee 6 cm from the middle stump's line, through the
	// stumps. A run-in along the pitch whose knee crosses the stumps at KneeY, the rest of the body wide of them.
	auto RunIn = [](float KneeY, float Side)
	{
		return [KneeY, Side](int32 I, float T)
		{
			const float X = FMath::Lerp(PitchLength + 2.f, PitchLength - 1.3f, T / BowlClipRelease);
			return FVector(X, Side * (I == 4 ? KneeY : 0.33f), I == 4 ? 0.6f : I == 0 ? 1.f : 0.3f);
		};
	};
	for (const float Side : { 1.f, -1.f })
	{
		const auto Path = RunIn(0.06f, Side);
		const float Push = StumpPush(Path, Side);
		const float ReleaseX = Path(0, BowlClipRelease).X;
		float Closest = 1e9f;
		for (float T = 0.f; T <= BowlClipRelease; T += 1.f / 240.f)
		{
			const FVector Knee = Path(4, T);
			if (FMath::Abs(Knee.X - PitchLength) < 0.02f)
				Closest = FMath::Min(Closest, Side * (Knee.Y + Side * Push * StumpPushWeight(Path(0, T).X, ReleaseX)));
		}
		TestTrue(FString::Printf(TEXT("knee clears the stumps (%.3f m from the middle)"), Closest), Closest >= StumpsHalfWidth + 0.06f + StumpGap - 0.01f);
		TestEqual(TEXT("no push at the release"), StumpPushWeight(ReleaseX, ReleaseX), 0.f);
		TestEqual(TEXT("a run-in already wide of the stumps is left alone"), StumpPush(RunIn(0.3f, Side), Side), 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnimCarriedBatFollowsHand, "CRICKET26.Animation.CarriedBatFollowsHand", CricketAnimationTests::Flags)
bool FAnimCarriedBatFollowsHand::RunTest(const FString&)
{
	using namespace CricketPose;
	// Regression: a carried bat was pinned beside the hip in world axes and the elbow pole fixed along the pitch, so
	// the carrying arm bent back on itself and never swung. The bat now goes where the clip's hand is, for any facing.
	for (const float Yaw : { 0.f, 90.f, 180.f, 235.f })
	{
		const FQuat Q(FVector::UpVector, FMath::DegreesToRadians(Yaw));
		const FVector F = Q.RotateVector(FVector::ForwardVector), R = Q.RotateVector(FVector::RightVector);
		const FVector Elbow = FVector(5.f, 3.f, 1.15f) + R * 0.22f;
		const FBat Hanging = CarriedBat(Elbow, Elbow - FVector::UpVector * 0.27f, F, R);
		TestTrue(FString::Printf(TEXT("yaw %.0f: grip in the hand"), Yaw), FVector::Dist(Hanging.Grip, Elbow - FVector::UpVector * 0.35f) < 0.08f);
		TestTrue(FString::Printf(TEXT("yaw %.0f: blade ahead and down"), Yaw), (Hanging.Axis | F) > 0.4f && Hanging.Axis.Z < -0.6f);
		const FBat Forward = CarriedBat(Elbow, Elbow + (F - FVector::UpVector).GetSafeNormal() * 0.27f, F, R);
		TestTrue(FString::Printf(TEXT("yaw %.0f: flatter on the forward swing"), Yaw), Forward.Axis.Z > Hanging.Axis.Z && Forward.Axis.Z <= -0.29f);
		const FBat Back = CarriedBat(Elbow, Elbow + (-F - FVector::UpVector).GetSafeNormal() * 0.27f, F, R);
		TestTrue(FString::Printf(TEXT("yaw %.0f: steeper on the back swing, the toe still ahead"), Yaw), Back.Axis.Z < Hanging.Axis.Z && (Back.Axis | F) > 0.f);
	}
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
