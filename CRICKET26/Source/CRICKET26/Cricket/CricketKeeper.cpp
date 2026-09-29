#include "CricketKeeper.h"
#include "FieldingModel.h"

namespace CricketKeeper
{
	FVector2D KeeperHome(EBowlerType BowlerType, float PaceKph, ECricketHand BatHand)
	{
		const float Off = OffSideSign(BatHand);
		if (BowlerType == EBowlerType::Pace)
		{
			const float Depth = KeeperDepth(BowlerType, PaceKph);
			// Aligned naturally behind middle/off: a touch to the off side so the
			// gloves sit outside the stumps and never clip them.
			return FVector2D(-Depth, 0.45f * Off);
		}
		// Spin: standing up, low and close, just outside the stumps.
		return FVector2D(-SpinDepth, 0.28f * Off);
	}

	float KeeperDepth(EBowlerType BowlerType, float PaceKph)
	{
		if (BowlerType != EBowlerType::Pace)
			return SpinDepth;
		// Stock pace sets the depth: back to where the stock ball arrives at the gloves (PaceDepthAt135).
		const float Depth = PaceDepthAt135 + PaceDepthPerKph * (PaceKph - 135.f);
		return FMath::Clamp(Depth, PaceDepthMin, PaceDepthMax);
	}

	FKeeperReady ReadyFor(bool bStandingUp)
	{
		FKeeperReady R;
		if (bStandingUp)
		{
			// Standing up: lower, more compact, gloves ready beside the stumps
			// for a quick take and an economical stumping transfer.
		R.Drop = 0.27f;
		R.ChestBend = 28.f;
		R.GloveHeight = 0.34f;
			R.GloveForward = 0.35f;
		}
		else
		{
			// Standing back: athletic base, knees flexed, hips loaded, torso
			// slightly forward, gloves presented by the knees, head stable.
		R.Drop = 0.22f;
		R.ChestBend = 23.f;
		R.GloveHeight = 0.46f;
			R.GloveForward = 0.40f;
		}
		return R;
	}

	float TakeSideStep(float GapY)
	{
		const float Gap = FMath::Abs(GapY);
		const float Step = FMath::Min(Gap, FMath::Max(BehindTheLine, Gap - KeeperArmsLength));
		return FMath::Sign(GapY) * FMath::Min(Step, KeeperReach - KeeperArmsLength);
	}

	FVector2D TakeBodyAt(const FFielderMove* Run, const FFielder& Keeper, float RunSpeed, const FVector& Take,
		float FieldTime, float StepFrom, float Post)
	{
		const FVector2D Base = Run && Post >= Run->Start
			? CricketField::PositionOf(*Run, Keeper, FMath::Min(Post, FieldTime), RunSpeed) : Keeper.Home;
		const FVector2D AtTake = Run ? CricketField::PositionOf(*Run, Keeper, FieldTime, RunSpeed) : Keeper.Home;
		const float Side = TakeSideStep(Take.Y - AtTake.Y);
		return FVector2D(Base.X, Base.Y + Side * FMath::SmoothStep(StepFrom, FieldTime, Post));
	}

	float StanceDrop(bool bStandingUp, float Ttr, float RiseAt)
	{
		// Down on the toes through the gather, hips below the knees' line for
		// spin, a little higher standing back; up with the bounce.
		const float Squat = bStandingUp ? 0.47f : 0.41f;
		const float Down = FMath::Lerp(RelaxedDrop, Squat, FMath::SmoothStep(-1.1f, -0.3f, Ttr));
		if (RiseAt < 0.f)
			return Down;
		return FMath::Lerp(Down, ReadyFor(bStandingUp).Drop, FMath::SmoothStep(RiseAt - 0.22f, RiseAt + 0.04f, Ttr));
	}

	float TakeDrop(float Drop, float ReadyDrop, float Height, float Receive)
	{
		if (Height > 1.1f)
			return FMath::Max(0.08f, Drop - 0.15f * FMath::SmoothStep(1.1f, 1.7f, Height) * Receive);
		if (Height < 0.45f)
			return FMath::Lerp(Drop, FMath::Min(MaxTakeDrop, FMath::Max(Drop, ReadyDrop + 0.45f - Height)), Receive);
		return Drop;
	}

	void StanceFeet(const FVector2D& Centre, const FVector2D& Forward, float Drop, FVector2D Out[2])
	{
		const FVector2D Right(-Forward.Y, Forward.X);
		const float Low = FMath::Clamp((Drop - RelaxedDrop) / 0.3f, 0.f, 1.f);
		const float Half = FMath::Lerp(0.16f, 0.27f, Low);
		const FVector2D Ahead = Forward * FMath::Lerp(0.03f, 0.09f, Low);
		Out[0] = Centre - Right * Half + Ahead;
		Out[1] = Centre + Right * Half + Ahead;
	}

	void StepFeet(FKeeperFeet& Feet, const FVector2D Ideal[2], float Dt, float OutLift[2])
	{
		OutLift[0] = OutLift[1] = 0.f;
		if (!Feet.bValid || Dt <= 0.f || Dt > 0.2f
			|| FVector2D::Distance(Feet.Ball[0], Ideal[0]) > 1.5f || FVector2D::Distance(Feet.Ball[1], Ideal[1]) > 1.5f)
		{
			for (int32 F = 0; F < 2; ++F)
			{
				Feet.Ball[F] = Feet.From[F] = Ideal[F];
				Feet.Phase[F] = 1.f;
			}
			Feet.bValid = true;
			return;
		}
		// Start a step: the planted foot furthest from where it belongs (the
		// lead foot, on the side the body moves to, on a tie), once the other
		// is down or nearly (never both feet high: a shuffle, not a hop).
		const bool bDown = Feet.Phase[0] >= 1.f && Feet.Phase[1] >= 1.f;
		Feet.Rest = bDown ? Feet.Rest + Dt : 0.f;
		const float Trigger = Feet.Rest > 0.2f ? SettleTrigger : StepTrigger;
		int32 Pick = -1;
		float Worst = 0.f;
		for (int32 F = 0; F < 2; ++F)
		{
			if (Feet.Phase[F] < 1.f || Feet.Phase[1 - F] < 0.7f) continue;
			const FVector2D Err = Ideal[F] - Feet.Ball[F];
			const float Score = Err.Size() + 0.03f * FMath::Sign(Err | (Ideal[F] - Ideal[1 - F]));
			if (Err.Size() > Trigger && Score > Worst) { Worst = Score; Pick = F; }
		}
		if (Pick >= 0)
		{
			Feet.From[Pick] = Feet.Ball[Pick];
			Feet.Phase[Pick] = 0.f;
			Feet.Rest = 0.f;
		}
		// A stepping foot lands where it belongs as it comes down, even if that moved meanwhile.
		for (int32 F = 0; F < 2; ++F)
		{
			if (Feet.Phase[F] >= 1.f) continue;
			Feet.Phase[F] = FMath::Min(1.f, Feet.Phase[F] + Dt / StepSeconds);
			Feet.Ball[F] = FMath::Lerp(Feet.From[F], Ideal[F], FMath::SmoothStep(0.f, 1.f, Feet.Phase[F]));
			OutLift[F] = StepLift * FMath::Sin(PI * Feet.Phase[F]) * FMath::Clamp(FVector2D::Distance(Feet.From[F], Ideal[F]) / 0.25f, 0.4f, 1.f);
		}
	}

	FKeeperSelection Classify(const FVector2D& Home, const FVector& FieldPos, bool bKeeperTakes,
		bool bDive, bool bContact, bool bBounced, bool bStandingUp, float TimeToArrival, float OffSign)
	{
		FKeeperSelection S;
		S.TimeToArrival = TimeToArrival;
		if (!bKeeperTakes)
		{
			// Not the keeper's ball (throw backing up, cover, or beaten wide):
			// hold shape, honest miss path handled by the caller.
			S.Family = EKeeperTake::Miss;
			S.Reach = EKeeperReach::Unreachable;
			S.Footwork = EKeeperFootwork::Hold;
			S.AnimName = TEXT("KeeperHoldMiss");
			S.MaxIKCorrection = 0.f;
			S.bUnreachableMiss = true;
			return S;
		}

		const FVector2D Take2D(FieldPos.X, FieldPos.Y);
		S.Dist2D = FVector2D::Distance(Take2D, Home);
		S.Lateral = (FieldPos.Y - Home.Y) * OffSign; // + off side, batter-relative
		S.Height = FieldPos.Z;
		const float AbsLat = FMath::Abs(S.Lateral);
		const bool bLeg = S.Lateral < 0.f;

		// Home-to-ball distance includes the run. The solver already decided
		// reachability at interception, after moving the keeper.
		S.Reach = bDive ? EKeeperReach::DiveRequired : EKeeperReach::Reachable;

		const bool bNeedsDive = S.Reach == EKeeperReach::DiveRequired;

		// Edges react AFTER bat contact, never preselected. Large deviation
		// (dive) vs thin edge (sharp small correction).
		if (bContact)
		{
			if (bNeedsDive)
			{
				S.Family = EKeeperTake::EdgeDive;
				S.Footwork = EKeeperFootwork::PushDive;
				S.AnimName = bLeg ? TEXT("KeeperEdgeDiveLeg") : TEXT("KeeperEdgeDiveOff");
				S.MaxIKCorrection = 0.12f;
			}
			else
			{
				S.Family = EKeeperTake::EdgeCatch;
				S.Footwork = AbsLat > 0.9f ? EKeeperFootwork::Crossover : AbsLat > 0.35f ? EKeeperFootwork::Shuffle : EKeeperFootwork::Set;
				S.AnimName = TEXT("KeeperEdgeCatch");
				S.MaxIKCorrection = 0.10f;
			}
			S.Steps = FMath::Clamp(FMath::CeilToInt(AbsLat / 0.6f), 0, 3);
			return S;
		}

		// Spin standing up is a different skill, not pace moved closer.
		if (bStandingUp)
		{
			if (bNeedsDive)
			{
				S.Family = bLeg ? EKeeperTake::DiveLeg : EKeeperTake::DiveOff;
				S.Footwork = EKeeperFootwork::PushDive;
				S.AnimName = bLeg ? TEXT("KeeperSpinDiveLeg") : TEXT("KeeperSpinDiveOff");
				S.MaxIKCorrection = 0.12f;
			}
			else if (bLeg && AbsLat > 0.35f)
			{
				S.Family = EKeeperTake::SpinLegTake;
				S.Footwork = AbsLat > 0.9f ? EKeeperFootwork::Crossover : EKeeperFootwork::Shuffle;
				S.AnimName = TEXT("KeeperSpinLegTake");
				S.MaxIKCorrection = 0.10f;
			}
			else
			{
				S.Family = EKeeperTake::SpinTake;
				S.Footwork = AbsLat > 0.9f ? EKeeperFootwork::Crossover : AbsLat > 0.3f ? EKeeperFootwork::Shuffle : EKeeperFootwork::Set;
				S.AnimName = TEXT("KeeperSpinTake");
				S.MaxIKCorrection = 0.08f;
			}
			S.Steps = FMath::Clamp(FMath::CeilToInt(AbsLat / 0.5f), 0, 3);
			return S;
		}

		// Bouncing ball before the keeper: gloves under/behind, never the clean
		// airborne catch shape.
		if (bBounced && S.Height < 0.45f && !bNeedsDive)
		{
			S.Family = EKeeperTake::BounceTake;
			S.Footwork = AbsLat > 0.9f ? EKeeperFootwork::Crossover : AbsLat > 0.35f ? EKeeperFootwork::Shuffle : EKeeperFootwork::Set;
			S.AnimName = TEXT("KeeperBounceTake");
			S.MaxIKCorrection = 0.10f;
			S.Steps = FMath::Clamp(FMath::CeilToInt(AbsLat / 0.6f), 0, 3);
			return S;
		}

		// Pace takes, standing back.
		if (bNeedsDive)
		{
			S.Family = bLeg ? EKeeperTake::DiveLeg : EKeeperTake::DiveOff;
			S.Footwork = EKeeperFootwork::PushDive;
			S.AnimName = bLeg ? TEXT("KeeperDiveLeg") : TEXT("KeeperDiveOff");
			S.MaxIKCorrection = 0.12f;
			S.Steps = 2;
			return S;
		}
		if (AbsLat <= 0.35f)
		{
			// Central: height decides the body shape. Lower through
			// ankles/knees/hips for low balls, never just the spine.
			if (S.Height < 0.35f) { S.Family = EKeeperTake::LowCentral; S.AnimName = TEXT("KeeperLowCentral"); }
			else if (S.Height > 1.35f) { S.Family = EKeeperTake::HighCentral; S.AnimName = TEXT("KeeperHighCentral"); }
			else if (S.Height < 0.6f) { S.Family = EKeeperTake::CentralWaist; S.AnimName = TEXT("KeeperCentralWaist"); }
			else { S.Family = EKeeperTake::CentralChest; S.AnimName = TEXT("KeeperCentralTake"); }
			S.Footwork = EKeeperFootwork::Set;
			S.Steps = 0;
			S.MaxIKCorrection = 0.08f;
			return S;
		}
		if (AbsLat <= 0.9f)
		{
			S.Family = bLeg ? EKeeperTake::SmallLeg : EKeeperTake::SmallOff;
			S.Footwork = EKeeperFootwork::Shuffle;
			S.AnimName = bLeg ? TEXT("KeeperSmallLeg") : TEXT("KeeperSmallOff");
			S.Steps = 1;
			S.MaxIKCorrection = 0.10f;
			if (S.Height < 0.35f) { S.Family = EKeeperTake::LowLateral; S.AnimName = TEXT("KeeperLowLateral"); }
			else if (S.Height > 1.35f) { S.Family = EKeeperTake::HighLateral; S.AnimName = TEXT("KeeperHighLateral"); }
			return S;
		}
		// Routine lateral: body travels behind the line, then receives. Never
		// one disconnected arm while the feet stay frozen.
		S.Family = bLeg ? EKeeperTake::LateralLeg : EKeeperTake::LateralOff;
		S.Footwork = EKeeperFootwork::Crossover;
		S.AnimName = bLeg ? TEXT("KeeperLateralLeg") : TEXT("KeeperLateralOff");
		S.Steps = 2;
		S.MaxIKCorrection = 0.12f;
		if (S.Height < 0.35f) { S.Family = EKeeperTake::LowLateral; S.AnimName = TEXT("KeeperLowLateral"); }
		else if (S.Height > 1.35f) { S.Family = EKeeperTake::HighLateral; S.AnimName = TEXT("KeeperHighLateral"); }
		return S;
	}

	const TCHAR* TakeName(EKeeperTake Take)
	{
		switch (Take)
		{
		case EKeeperTake::CentralChest: return TEXT("CentralChest");
		case EKeeperTake::CentralWaist: return TEXT("CentralWaist");
		case EKeeperTake::LowCentral: return TEXT("LowCentral");
		case EKeeperTake::HighCentral: return TEXT("HighCentral");
		case EKeeperTake::SmallOff: return TEXT("SmallOff");
		case EKeeperTake::SmallLeg: return TEXT("SmallLeg");
		case EKeeperTake::LateralOff: return TEXT("LateralOff");
		case EKeeperTake::LateralLeg: return TEXT("LateralLeg");
		case EKeeperTake::LowLateral: return TEXT("LowLateral");
		case EKeeperTake::HighLateral: return TEXT("HighLateral");
		case EKeeperTake::DiveOff: return TEXT("DiveOff");
		case EKeeperTake::DiveLeg: return TEXT("DiveLeg");
		case EKeeperTake::BounceTake: return TEXT("BounceTake");
		case EKeeperTake::EdgeCatch: return TEXT("EdgeCatch");
		case EKeeperTake::EdgeDive: return TEXT("EdgeDive");
		case EKeeperTake::SpinTake: return TEXT("SpinTake");
		case EKeeperTake::SpinLegTake: return TEXT("SpinLegTake");
		case EKeeperTake::Stumping: return TEXT("Stumping");
		case EKeeperTake::RunOutTake: return TEXT("RunOutTake");
		case EKeeperTake::Miss: return TEXT("Miss");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* ReachName(EKeeperReach Reach)
	{
		switch (Reach)
		{
		case EKeeperReach::Reachable: return TEXT("Reachable");
		case EKeeperReach::DiveRequired: return TEXT("DiveRequired");
		case EKeeperReach::Unreachable: return TEXT("Unreachable");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* FootworkName(EKeeperFootwork Footwork)
	{
		switch (Footwork)
		{
		case EKeeperFootwork::Set: return TEXT("Set");
		case EKeeperFootwork::Shuffle: return TEXT("Shuffle");
		case EKeeperFootwork::Crossover: return TEXT("Crossover");
		case EKeeperFootwork::PushDive: return TEXT("PushDive");
		case EKeeperFootwork::Hold: return TEXT("Hold");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* EventName(EKeeperEvent Event)
	{
		switch (Event)
		{
		case EKeeperEvent::Ready: return TEXT("KeeperReady");
		case EKeeperEvent::PushOff: return TEXT("KeeperPushOff");
		case EKeeperEvent::CatchContact: return TEXT("KeeperCatchContact");
		case EKeeperEvent::SecureBall: return TEXT("KeeperSecureBall");
		case EKeeperEvent::StumpBreak: return TEXT("KeeperStumpBreak");
		case EKeeperEvent::ThrowRelease: return TEXT("KeeperThrowRelease");
		case EKeeperEvent::GroundContact: return TEXT("KeeperGroundContact");
		case EKeeperEvent::RecoveryComplete: return TEXT("KeeperRecoveryComplete");
		default: return TEXT("Unknown");
		}
	}

	float GloveTarget(const FKeeperSelection& /*Sel*/, const FVector& KeeperChest, const FVector& FieldPos, FVector& OutGloves)
	{
		const FVector Delta = FieldPos - KeeperChest;
		const float Dist = Delta.Size();
		// The dive clip moves the whole body. IK alone may move the gloves only
		// this far from the chest; never ask the arm to span a whole dive.
		OutGloves = KeeperChest + Delta.GetClampedToMaxSize(KeeperGloveReach);
		return FMath::Max(0.f, Dist - KeeperGloveReach);
	}

	float StumpingProgress(float Post, float TakeTime, float BreakTime)
	{
		if (BreakTime < 0.f || TakeTime < 0.f || BreakTime <= TakeTime)
			return 0.f;
		// Possession first: no movement to the wicket before the take.
		if (Post < TakeTime)
			return 0.f;
		// Quick and economical: gloves travel in ~0.18 s, then break.
		const float Travel = FMath::Min(0.18f, 0.8f * (BreakTime - TakeTime));
		return FMath::Clamp((Post - TakeTime) / FMath::Max(Travel, 0.05f), 0.f, 1.f);
	}

	FString SelectionLog(const FKeeperSelection& Sel, const FVector& BallAtTake, float KeeperDepthM, EBowlerType BowlerType)
	{
		const TCHAR* Type = BowlerType == EBowlerType::Pace ? TEXT("Pace") : TEXT("Spin");
		return FString::Printf(TEXT("Keeper sel: type %s depth %.1f m | take (%.2f, %.2f, %.2f) lat %+.2f h %.2f t %.2f s | %s / %s / %s steps %d anim %s ik<=%.2f %s"),
			Type, KeeperDepthM, BallAtTake.X, BallAtTake.Y, BallAtTake.Z, Sel.Lateral, Sel.Height, Sel.TimeToArrival,
			TakeName(Sel.Family), ReachName(Sel.Reach), FootworkName(Sel.Footwork), Sel.Steps, Sel.AnimName, Sel.MaxIKCorrection,
			Sel.bUnreachableMiss ? TEXT("MISS-HONEST") : TEXT(""));
	}
}
