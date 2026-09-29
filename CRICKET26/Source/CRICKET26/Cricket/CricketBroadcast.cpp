// Broadcast camera director implementation. See CricketBroadcast.h for the contract.

#include "CricketBroadcast.h"
#include "DeliveryResolver.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"

namespace CricketBroadcast
{
	namespace
	{
		constexpr float SimToWorld = 100.f; // simulation metres -> Unreal cm
		FVector Sim(float X, float Y, float Z) { return FVector(X, Y, Z) * SimToWorld; }
	}

	// ---------- Phase & classification ----------

	EBroadcastPhase ResolvePhase(const FPhaseInput& In, const FBroadcastTuning& Tune)
	{
		if (In.bReplaying) return EBroadcastPhase::Replay;
		if (In.bWicketFallen || In.Dismissal != EDismissal::None)
		{
			// A dismissal owns the screen from the moment it resolves.
			if (In.bDead && In.Boundary == 0) return EBroadcastPhase::Wicket;
		}
		if (In.bDead)
		{
			if (In.Boundary > 0) return EBroadcastPhase::Boundary;
			return EBroadcastPhase::BallDead;
		}
		if (In.bRunUp) return EBroadcastPhase::RunUp;
		if (!In.bBallLive) return EBroadcastPhase::PreDelivery;
		// Before the bat (or the batter's plane) the ball is in delivery: AfterContact is negative there,
		// so the contact breath is only the ContactHoldTime after it, never the whole flight.
		if (In.BallT < In.ContactTime) return EBroadcastPhase::Delivery;
		if (In.bHasContact && In.AfterContact < Tune.ContactHoldTime) return EBroadcastPhase::Contact;
		if (In.bCaught) return EBroadcastPhase::Wicket;
		// A boundary in flight stays BallInPlay: the rope shot is a timed handoff (follow first, rope
		// near the arrival), driven by the shot class, not a phase jump at contact.
		return EBroadcastPhase::BallInPlay;
	}

	EShotClass ClassifyShot(const FDeliveryResult& Result, EDismissal Dismissal)
	{
		using namespace CricketGeo;
		if (Dismissal == EDismissal::Bowled || Dismissal == EDismissal::LBW || Dismissal == EDismissal::HitWicket
			|| (Result.bStumpsHit && Dismissal != EDismissal::None))
			return EShotClass::WicketEvent;
		if (Dismissal == EDismissal::Caught || Dismissal == EDismissal::Stumped
			|| (Result.Fielding.bCaught && Dismissal != EDismissal::None))
			return EShotClass::WicketEvent;
		if (Dismissal == EDismissal::RunOut)
			return EShotClass::RunningPlay;

		const FContactResult& C = Result.Contact;
		// Beaten: the delivery lens holds the keeper's take. Byes (the ball past the keeper, the batters running)
		// leave that frame, so they are covered like any running ball.
		if (!C.HasContact()) return Result.Running.Attempted > 0 && Result.Fielding.Fielder > 0 ? EShotClass::RunningPlay : EShotClass::NoContact;

		const bool bEdge = C.Zone == EContactZone::InsideEdge || C.Zone == EContactZone::OutsideEdge
			|| C.Zone == EContactZone::TopEdge || C.Zone == EContactZone::BottomEdge;
		if (Result.Fielding.Boundary > 0) return EShotClass::BoundaryTrajectory;
		if (bEdge)
		{
			// Into the keeper's gloves, or a live edge with fielders converging. A low edge someone else
			// fields (third man, fine leg) is a ball in the outfield like any other: the keeper camera would
			// hold the batter while the ball ran away out of shot.
			if (Result.Fielding.Fielder == 0) return EShotClass::KeeperEdge;
			if (Result.Contact.ExitVel.Z >= 3.f) return EShotClass::LoftedInfield;
		}

		const FVector V = C.ExitVel; // sim m/s
		const float Horizontal = FVector2D(V.X, V.Y).Size();
		const float LoftAngle = FMath::RadiansToDegrees(FMath::Atan2(V.Z, FMath::Max(Horizontal, 0.01f)));
		const bool bLofted = LoftAngle > 12.f || V.Z > 6.f;

		// Where it is going: sample the stored path for max height and carry.
		float MaxZ = 0.f;
		for (const FVector& P : Result.BallPath) MaxZ = FMath::Max(MaxZ, P.Z);
		const bool bAerial = bLofted || MaxZ > 3.5f;

		if (Result.Fielding.bCatchChance && !Result.Fielding.bCaught)
			return bAerial ? EShotClass::HighCatchChance : EShotClass::LoftedInfield;
		if (Result.Fielding.bCaught)
			return EShotClass::WicketEvent; // taken: dismissal coverage (run-out checked above)

		// Infield vs outfield: how far the ball gets before being gathered.
		const float Carry = Result.Fielding.Fielder >= 0
			? FVector2D(Result.Fielding.FieldPos - C.ContactPos).Size()
			: FVector2D(Result.BallAt(Result.DeadTime) - C.ContactPos).Size();
		const bool bOutfield = Carry > 27.f;

		if (!bAerial)
		{
			if (Result.Running.Attempted > 0) return EShotClass::RunningPlay;
			if (C.Shot == EShotType::Defend || (Horizontal < 8.f && V.Z < 2.f)) return EShotClass::DeadDefence;
			return bOutfield ? EShotClass::GroundOutfield : EShotClass::GroundInfield;
		}
		if (Result.Running.Attempted > 0 && !bOutfield) return EShotClass::RunningPlay;
		return bOutfield ? EShotClass::LoftedOutfield : EShotClass::LoftedInfield;
	}

	float DeliveryProgress(float BallT, float ReleaseTime, float ContactTime)
	{
		const float Span = FMath::Max(ContactTime - ReleaseTime, 0.05f);
		return FMath::Clamp((BallT - ReleaseTime) / Span, 0.f, 1.f);
	}

	FDeliveryWeights DeliveryWeights(float Progress01, const FDeliveryCameraTune& Tune)
	{
		// Smooth operator move (§8): bowler-dominant at the top of the run-up, release corridor +
		// ball rising through the flight, batter + outgoing ball by contact. Cosine blends throughout.
		const float P = FMath::SmoothStep(0.f, 1.f, FMath::Clamp(Progress01, 0.f, 1.f));
		FDeliveryWeights W;
		W.Bowler = FMath::Lerp(Tune.BowlerWeight + 0.25f, 0.05f, P);
		W.Release = FMath::Lerp(0.10f, Tune.ReleaseWeight + 0.10f, FMath::SmoothStep(0.f, 0.6f, P) * (1.f - FMath::SmoothStep(0.7f, 1.f, P)));
		W.Ball = FMath::Lerp(0.02f, Tune.BallWeight + Tune.ReleaseBias * 0.35f, P * P);
		W.Batter = FMath::Lerp(Tune.BatterWeight, Tune.BatterWeight + 0.15f, P);
		const float Sum = W.Bowler + W.Release + W.Ball + W.Batter;
		const float Inv = Sum > 1e-5f ? 1.f / Sum : 0.25f;
		W.Bowler *= Inv; W.Release *= Inv; W.Ball *= Inv; W.Batter *= Inv;
		return W;
	}

	// ---------- Geometry ----------

	FVector DeliveryLocation(const FDeliveryCameraTune& Tune, float ArmSign)
	{
		using namespace CricketGeo;
		// High in the stand behind the bowler. Lateral offset reads in world Y (sim lateral).
		// A whisper toward the bowling arm side opens the release; default stays central.
		return Sim(PitchLength + Tune.Distance, Tune.LateralOffset + 0.35f * ArmSign * FMath::Abs(Tune.LateralOffset), Tune.Height);
	}

	FVector DeliveryLookAt(const FDeliveryCameraTune& Tune)
	{
		return Sim(Tune.LookAtX, 0.f, Tune.LookAtHeight);
	}

	float DeliveryClock(float RunUpProgress01, float BallT, float ContactTime)
	{
		if (BallT < 0.f) return FMath::Clamp(RunUpProgress01, 0.f, 1.f);
		return 1.f + DeliveryProgress(BallT, 0.f, ContactTime);
	}

	float FDeliveryClockFollow::Update(float Target, float Dt)
	{
		if (Prev < 0.f || Target < Prev - 1e-3f) { Reset(Target); return U; }
		if (Dt <= 0.f) return U;
		const float Feed = FMath::Clamp((Target - Prev) / Dt, 0.f, MaxFeed);
		Prev = Target;
		// Fixed substeps keep the spring identical at 30, 60 or 120 Hz.
		const int32 N = FMath::Clamp(FMath::CeilToInt(Dt * 240.f), 1, 64);
		const float H = Dt / N, W = Stiffness;
		for (int32 I = 0; I < N; ++I)
		{
			Rate += (W * W * (Target - U) + 2.f * W * (Feed - Rate)) * H;
			U += FMath::Max(Rate, 0.f) * H;
			// Never ahead of the game: the fed-forward speed would otherwise carry it past a clock that stops.
			if (U > Target) { U = Target; Rate = FMath::Min(Rate, Feed); }
		}
		return U;
	}

	float AspectFOV(float Fov16x9, float Aspect)
	{
		const float VTan = FMath::Tan(FMath::DegreesToRadians(0.5f * Fov16x9)) * (9.f / 16.f);
		return FMath::RadiansToDegrees(2.f * FMath::Atan(VTan * FMath::Max(Aspect, 0.1f)));
	}

	bool SampleLensCurve(const FDeliveryCameraTune& Tune, float U, float& OutFov16x9, float& OutCreaseY)
	{
		const TArray<FDeliveryLensKey>& K = Tune.LensCurve;
		if (K.IsEmpty()) return false;
		int32 I = 0;
		while (I + 1 < K.Num() && K[I + 1].U <= U) ++I;
		if (I + 1 >= K.Num() || U <= K[I].U)
		{
			OutFov16x9 = K[I].FOV;
			OutCreaseY = K[I].CreaseY;
			return true;
		}
		// Monotone cubic (Fritsch-Carlson) through the keys: straight segments kinked the zoom speed at every
		// key, which read as the lens lurching. Flat at both ends, so the push eases out of the wide and into
		// the batter. A key at the release (U = 1) joins two clocks running at different speeds (a run-up of
		// seconds, a flight of half a second), so each side keeps its own secant there: that is what keeps the
		// zoom speed in real time steady through the release. A zoom reads even in log space: equal steps of
		// U are equal magnification ratios.
		auto Slope = [&K](int32 J, bool bLeftSide, TFunctionRef<float(const FDeliveryLensKey&)> V)
		{
			if (J <= 0 || J + 1 >= K.Num()) return 0.f;
			const float H0 = FMath::Max(K[J].U - K[J - 1].U, 1e-4f), H1 = FMath::Max(K[J + 1].U - K[J].U, 1e-4f);
			const float D0 = (V(K[J]) - V(K[J - 1])) / H0, D1 = (V(K[J + 1]) - V(K[J])) / H1;
			if (FMath::IsNearlyEqual(K[J].U, 1.f)) return bLeftSide ? D0 : D1;
			if (D0 * D1 <= 0.f) return 0.f;
			const float W0 = 2.f * H1 + H0, W1 = H1 + 2.f * H0;
			return (W0 + W1) / (W0 / D0 + W1 / D1);
		};
		auto Hermite = [&](TFunctionRef<float(const FDeliveryLensKey&)> V)
		{
			const float H = FMath::Max(K[I + 1].U - K[I].U, 1e-4f), T = (U - K[I].U) / H, T2 = T * T, T3 = T2 * T;
			return (2.f * T3 - 3.f * T2 + 1.f) * V(K[I]) + (T3 - 2.f * T2 + T) * H * Slope(I, false, V)
				+ (3.f * T2 - 2.f * T3) * V(K[I + 1]) + (T3 - T2) * H * Slope(I + 1, true, V);
		};
		OutFov16x9 = FMath::Exp(Hermite([](const FDeliveryLensKey& Key) { return FMath::Loge(Key.FOV); }));
		OutCreaseY = Hermite([](const FDeliveryLensKey& Key) { return Key.CreaseY; });
		return true;
	}

	FLiveCameraSolution SolveDeliveryShot(const FDeliveryCameraTune& Tune, float U, float Aspect, float ArmSign, const FVector& PanTarget)
	{
		using namespace CricketGeo;
		FLiveCameraSolution Sol;
		Sol.Location = DeliveryLocation(Tune, ArmSign);
		float Fov = Tune.FOV, CreaseY = 0.5f;
		if (!SampleLensCurve(Tune, U, Fov, CreaseY))
		{
			Sol.LookAt = DeliveryLookAt(Tune);
			Sol.FOV = AspectFOV(Fov, Aspect);
			return Sol;
		}
		// Tilt: the crease sits CreaseY down the screen, so the axis dips below it by the angle that
		// offset subtends (exact for points on the vertical centre line of a pinhole camera).
		const FVector Crease = Sim(PoppingCrease, 0.f, 0.f);
		const FVector Flat = FVector(FVector2D(Crease - Sol.Location), 0.f);
		const FVector H = Flat.GetSafeNormal();
		const float CreaseDip = FMath::Atan2(Sol.Location.Z - Crease.Z, Flat.Size());
		const float VTan = FMath::Tan(FMath::DegreesToRadians(0.5f * Fov)) * (9.f / 16.f);
		const float Dip = CreaseDip - FMath::Atan((2.f * CreaseY - 1.f) * VTan) + FMath::DegreesToRadians(Tune.PitchTrim);
		FVector Dir = H * FMath::Cos(Dip) - FVector::UpVector * FMath::Sin(Dip);
		// Pan: a share of the yaw toward the weighted target, so a wide run-up or a batter's step
		// across drifts the frame a touch without ever leaving the pitch line.
		const FVector ToT = FVector(FVector2D(PanTarget - Sol.Location), 0.f).GetSafeNormal();
		const float Yaw = ToT.IsNearlyZero() ? 0.f
			: FMath::Atan2(FVector::CrossProduct(H, ToT).Z, FVector::DotProduct(H, ToT));
		const float Pan = Yaw * FMath::Clamp(Tune.LateralFollow * Tune.TrackingStrength, 0.f, 1.f)
			+ FMath::DegreesToRadians(Tune.YawOffset);
		Dir = FQuat(FVector::UpVector, Pan).RotateVector(Dir);
		// The look point stops at the turf when the axis meets it before the crease (the wide run-up tilt).
		const float Reach = FMath::Min(FVector::Dist(Sol.Location, Crease), Sol.Location.Z / FMath::Max(FMath::Sin(Dip), 1e-3f));
		Sol.LookAt = Sol.Location + Dir * Reach;
		Sol.FOV = AspectFOV(Fov, Aspect);
		return Sol;
	}

	FVector WeightedDeliveryTarget(const FBroadcastFrame& Frame, const FDeliveryCameraTune& Tune, float Progress01)
	{
		using namespace CricketGeo;
		const FDeliveryWeights W = DeliveryWeights(Progress01, Tune);
		const FVector ReleaseArea = Sim(0.55f * PitchLength, 0.f, 1.2f);
		FVector Target = Frame.BowlerPos * W.Bowler + ReleaseArea * W.Release
			+ Frame.BallPos * W.Ball + Frame.StrikerPos * W.Batter;
		// At release the eye drops into the corridor so the trajectory reads against the pitch.
		const float ReleasePull = Tune.ReleaseBias * FMath::SmoothStep(0.35f, 0.65f, Progress01);
		Target = FMath::Lerp(Target, ReleaseArea, 0.35f * ReleasePull * Tune.TrackingStrength);
		Target.Z = FMath::Max(Target.Z, 40.f); // never into the turf in dead pixels
		return Target;
	}

	// Tilts a stand-camera follow down until the horizon sits HorizonY of the half height above centre,
	// sliding the look target along the ground toward the lens. Never below the turf.
	static FVector HoldHorizon(const FVector& Loc, const FVector& LookAt, float Fov16x9, float HorizonY)
	{
		const float MinDip = FMath::Atan(HorizonY * FMath::Tan(FMath::DegreesToRadians(Fov16x9) * 0.5f) * 9.f / 16.f);
		const FVector2D Flat(LookAt.X - Loc.X, LookAt.Y - Loc.Y);
		const float Reach = Flat.Size();
		if (Reach < 1.f || FMath::Atan2(Loc.Z - LookAt.Z, Reach) >= MinDip) return LookAt;
		const float GroundReach = FMath::Min(Reach, Loc.Z / FMath::Tan(MinDip));
		return FVector(FVector2D(Loc.X, Loc.Y) + Flat / Reach * GroundReach, Loc.Z - GroundReach * FMath::Tan(MinDip));
	}

	FLiveCameraSolution SolveShotGeometry(EBroadcastShot Shot, const FBroadcastFrame& Frame, const FBroadcastTuning& Tune)
	{
		using namespace CricketGeo;
		const float Off = Frame.OffSign;
		const FVector Centre = Sim(0.5f * PitchLength, 0.f, 0.f);
		FLiveCameraSolution Sol;
		Sol.Shot = Shot;

		auto Toward = [](const FVector& From, const FVector& To) { return (To - From).GetSafeNormal(); };

		switch (Shot)
		{
		case EBroadcastShot::StandardDelivery:
		{
			// The top of the run-up; the game mode drives the clock and the pan each frame.
			const FLiveCameraSolution D = SolveDeliveryShot(Tune.Delivery, 0.f, 16.f / 9.f, Frame.ArmSign, Sim(PoppingCrease, 0.f, 0.f));
			Sol.Location = D.Location;
			Sol.LookAt = D.LookAt;
			Sol.FOV = D.FOV;
			break;
		}
		case EBroadcastShot::AlternateDelivery:
		{
			// Slightly lower, wider, a touch squarer: the second delivery angle for replays.
			FDeliveryCameraTune T = Tune.Delivery;
			T.Distance *= 0.72f; T.Height *= 0.75f; T.LateralOffset += 6.f * Off;
			Sol.Location = DeliveryLocation(T, Frame.ArmSign);
			Sol.LookAt = DeliveryLookAt(T);
			Sol.FOV = T.FOV * 1.7f;
			break;
		}
		case EBroadcastShot::StraightOn:
		{
			// Behind the keeper's end, down the pitch: release and swing read large.
			Sol.Location = Sim(-26.f, 3.f * Off, 7.f);
			Sol.LookAt = Sim(0.6f * PitchLength, 0.f, 1.2f);
			Sol.FOV = 14.f;
			break;
		}
		case EBroadcastShot::BatterEnd:
		case EBroadcastShot::Keeper:
		{
			// Low behind the stumps at the striker's end: edges, takes, stumpings.
			Sol.Location = Sim(-9.f, -4.5f * Off, 2.6f);
			Sol.LookAt = Sim(1.2f, 0.f, 0.9f);
			Sol.FOV = 24.f;
			break;
		}
		case EBroadcastShot::SideOn:
		{
			// Square of the pitch on the off side, batter height: strokes and creases.
			const float X = FMath::Clamp(Frame.ContactPos.X, 0.f, PitchLength);
			Sol.Location = Sim(X + 1.5f, 30.f * Off, 2.2f);
			Sol.LookAt = Sim(X, 0.f, 1.f);
			Sol.FOV = Tune.ReplaySideFOV;
			break;
		}
		case EBroadcastShot::Slip:
		{
			// Behind the cordon: the edge carrying through.
			Sol.Location = Sim(-6.f, 9.f * Off, 2.2f);
			Sol.LookAt = Sim(0.8f, 0.f, 0.8f);
			Sol.FOV = 26.f;
			break;
		}
		case EBroadcastShot::GroundFollow:
		{
			// Cricket 24 rides the delivery camera out of the stroke: the same stand position, the lens
			// opening and panning with the ball so ball and striker share the frame (no crane move).
			Sol.Location = DeliveryLocation(Tune.Delivery, Frame.ArmSign);
			const FVector Ahead = Frame.bHasPrediction ? (Frame.BallPos + Frame.PredictedPos) * 0.5f : Frame.BallPos;
			Sol.LookAt = (2.f * Ahead + Frame.StrikerPos) / 3.f;
			const float Spread = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(
				(Ahead - Sol.Location).GetSafeNormal(), (Frame.StrikerPos - Sol.Location).GetSafeNormal()), -1.f, 1.f)));
			Sol.FOV = FMath::Clamp(2.2f * Spread + 6.f, Tune.FollowMinFOV, Tune.FollowFOV);
			Sol.LookAt = HoldHorizon(Sol.Location, Sol.LookAt, Sol.FOV, Tune.FollowHorizonY);
			break;
		}
		case EBroadcastShot::OutfieldFollow:
		{
			// Cricket 24 cuts from the delivery to a reverse angle for a ball bound for the outfield: high
			// in the stand opposite the shot, looking out over the pitch at the ball, the chaser and the rope
			// with the boards and crowd behind, and it stays on that camera through to the rope. The station
			// comes from the exit line (fixed per stroke) so the camera never dollies; only pan and zoom follow.
			FVector2D Out2D(Frame.ExitVel.X, Frame.ExitVel.Y);
			if (Out2D.IsNearlyZero()) Out2D = FVector2D(Frame.BallPos.X, Frame.BallPos.Y) - FVector2D(Centre.X, Centre.Y);
			if (Out2D.IsNearlyZero()) Out2D = FVector2D(0.f, Off);
			Out2D.Normalize();
			Sol.Location = Centre - FVector(Out2D, 0.f) * Tune.OutfieldCamBack * SimToWorld + FVector(0.f, 0.f, Tune.OutfieldCamHeight * SimToWorld);
			Sol.LookAt = Frame.bHasPrediction ? (Frame.BallPos + Frame.PredictedPos) * 0.5f : Frame.BallPos;
			const float Dist = FMath::Max(FVector::Dist(Sol.Location, Sol.LookAt), 100.f);
			Sol.FOV = FMath::Clamp(FMath::RadiansToDegrees(2.f * FMath::Atan(Tune.OutfieldHalfWidth / Dist)), Tune.FollowMinFOV, Tune.FollowFOV);
			// A climbing ball takes the tilt up with it (crowd behind the flight); a ground ball keeps the field.
			Sol.LookAt = FMath::Lerp(HoldHorizon(Sol.Location, Sol.LookAt, Sol.FOV, Tune.OutfieldHorizonY), Sol.LookAt,
				FMath::SmoothStep(200.f, 800.f, float(Frame.BallPos.Z)));
			break;
		}
		case EBroadcastShot::AerialBall:
		{
			// Framed by where the ball is GOING (§12): landing area + converging fielder, never empty sky.
			// NOTE: all direction math here is plan (2D): using the 3D ball direction would drive the
			// camera underground when the ball climbs, and the safeguard clamp would leave it staring
			// at the sky from the turf.
			FVector2D Out2D = FVector2D(Frame.BallPos.X, Frame.BallPos.Y) - FVector2D(Centre.X, Centre.Y);
			if (Out2D.Size() < 300.f) Out2D = FVector2D(Frame.ExitVel.X, Frame.ExitVel.Y); // early flight: the shot line
			if (Out2D.IsNearlyZero()) Out2D = FVector2D(0.f, Frame.OffSign);
			Out2D.Normalize();
			// The fielder pulls the frame horizontally (landing area stays in context) but never
			// vertically: averaging a 20 m ball with a 1 m fielder would bury the eye halfway down.
			FVector Anchor = Frame.bHasPrediction ? Frame.PredictedPos : Frame.BallPos;
			if (Frame.bHasFielder)
			{
				Anchor.X = (Anchor.X + Frame.FielderPos.X) * 0.5f;
				Anchor.Y = (Anchor.Y + Frame.FielderPos.Y) * 0.5f;
			}
			Anchor.Z = FMath::Max(Anchor.Z, 120.f);
			Sol.Location = Centre - FVector(Out2D, 0.f) * 30.f * SimToWorld + FVector(0.f, 0.f, 16.f * SimToWorld);
			Sol.LookAt = (Frame.BallPos + Anchor) * 0.5f;
			Sol.FOV = Tune.BoundaryFOV;
			// Never look up into pure sky: keep the look target below the ball when it is very high.
			if ((Sol.LookAt.Z - Frame.BallPos.Z) > 0.f && Frame.BallPos.Z > 1500.f)
				Sol.LookAt.Z = Frame.BallPos.Z - 150.f;
			break;
		}
		case EBroadcastShot::Boundary:
		{
		// Beyond the rope where the ball crosses: in the strip between the boards (+4 m) and the
		// stands (+9 m), to the fielder's far side and above head height, so fielders, boards and
		// crowd read in context instead of filling the lens. The eye meets the incoming ball; once
		// it has crossed, the camera HOLDS the arrival point instead of chasing the ball behind the
		// lens (which ends staring at the boards).
		FVector2D Out2D = FVector2D(1.f, 0.f);
		FVector Cross = Centre + FVector(Out2D, 0.f) * (BoundaryRadius + 8.f) * SimToWorld;
		if (Frame.bHasBoundaryCross)
		{
			Cross = Frame.BoundaryCrossPos;
			const FVector2D C2(Centre.X, Centre.Y);
			Out2D = (FVector2D(Cross.X, Cross.Y) - C2).GetSafeNormal();
		}
		// A fielder converging on the crossing must never stand between the lens and the ball:
		// take their far side.
		FVector2D Side2D(-Out2D.Y, Out2D.X);
		if (Frame.bHasFielder)
		{
			const FVector2D ToFielder = FVector2D(Frame.FielderPos.X, Frame.FielderPos.Y) - FVector2D(Cross.X, Cross.Y);
			if (ToFielder.Size() < 12.f * SimToWorld && FVector2D::DotProduct(Side2D, ToFielder.GetSafeNormal()) > 0.f)
				Side2D = -Side2D;
		}
			const FVector PlatDir = (FVector(Out2D, 0.f) * 7.f + FVector(Side2D, 0.f) * 6.f).GetSafeNormal();
			Sol.Location = Cross + PlatDir * 9.f * SimToWorld + FVector(0.f, 0.f, 6.f * SimToWorld);
			// An operator frames ball AND rope, not ball alone: before the crossing the ball leads,
			// after it the eye settles between the ball and the arrival point so the rope, the ball
			// and the retrieving fielder share the frame. A minimum plan distance keeps the lens from
			// pitching into the turf if the ball rolls underneath.
			const bool bCrossed = Frame.BoundaryTime >= 0.f && Frame.AfterContact > Frame.BoundaryTime + 0.1f;
			const FVector RopePoint(Cross.X, Cross.Y, Cross.Z + 50.f);
			FVector Look = bCrossed ? (Frame.BallPos + RopePoint) * 0.5f : Frame.BallPos;
			{
				FVector2D D(Look.X - Sol.Location.X, Look.Y - Sol.Location.Y);
				if (D.Size() < 250.f)
				{
					const FVector2D Push = D.IsNearlyZero() ? Out2D : D.GetSafeNormal();
					Look.X = Sol.Location.X + Push.X * 250.f;
					Look.Y = Sol.Location.Y + Push.Y * 250.f;
				}
			}
			Sol.LookAt = Look;
			Sol.FOV = Tune.BoundaryFOV;
			break;
		}
		case EBroadcastShot::Catch:
		{
			// Off the fielder toward the pitch: descending ball AND catcher in frame (§14). Framed
			// horizontally between them but vertically on the ball: a high ball averaged with the
			// ground would point the lens at empty air below it. Never a tight pre-catch close-up:
			// the action must stay readable until the take resolves.
			const FVector At = Frame.bHasFielder ? Frame.FielderPos : Frame.BallPos;
			const FVector In = Toward(At, Centre + FVector(0.f, 0.f, 100.f));
			Sol.Location = At + In * 900.f + FVector(0.f, 0.f, 150.f);
			Sol.LookAt = FVector((At.X + Frame.BallPos.X) * 0.5f, (At.Y + Frame.BallPos.Y) * 0.5f, Frame.BallPos.Z + 40.f);
			Sol.FOV = Tune.FieldingFOV;
			break;
		}
		case EBroadcastShot::RunOut:
		{
			// Square-on to the threatened crease: runner, crease, stumps, incoming ball (§16).
			// The end the throw is going to; a runner mid-pitch says nothing about which crease is under threat.
			const bool bNear = Frame.bHasThrowEnd ? Frame.bThrowToStrikerEnd : Frame.StrikerPos.X < 0.5f * PitchLength * SimToWorld;
			const float StumpsX = bNear ? 0.f : PitchLength;
			const float CreaseX = StumpsX + (bNear ? 1.f : -1.f) * PoppingCrease;
			Sol.Location = Sim(CreaseX, 20.f * Off, 1.4f);
			Sol.LookAt = Sim(StumpsX, 0.f, 0.5f);
			Sol.FOV = 16.f;
			break;
		}
		case EBroadcastShot::WicketClose:
		{
			// Bowler's celebration / batter's walk: front-on, head above the banner line.
			const FVector Head = Frame.BowlerPos + FVector(0.f, 0.f, 85.f);
			const FVector Front = FVector(-SimToWorld, 0.3f * Frame.ArmSign * SimToWorld, 10.f).GetSafeNormal();
			Sol.Location = Head + Front * 800.f;
			Sol.LookAt = Head - FVector(0.f, 0.f, 35.f);
			Sol.FOV = Tune.CloseUpFOV;
			break;
		}
		case EBroadcastShot::ReplayBeauty:
		{
			// Side-on from the off side at batter height: the stroke reads clean.
			const float X = FMath::Clamp(Frame.ContactPos.X, 0.f, PitchLength);
			Sol.Location = Sim(X + 2.f, 30.f * Off, 2.2f);
			Sol.LookAt = Sol.Location.Z > 0.f ? Sim(X, 0.f, 1.f) : Frame.BallPos;
			Sol.FOV = Tune.ReplaySideFOV;
			break;
		}
		case EBroadcastShot::ReplaySlowMo:
		{
			// From the main camera's place high behind the bowler, in close on contact.
			Sol.Location = DeliveryLocation(Tune.Delivery, Frame.ArmSign);
			const float X = FMath::Clamp(Frame.ContactPos.X, 0.f, PitchLength);
			Sol.LookAt = Sim(X, 0.f, 1.f);
			Sol.FOV = Tune.SuperSlowFOV;
			break;
		}
		default:
			Sol.Location = DeliveryLocation(Tune.Delivery, Frame.ArmSign);
			Sol.LookAt = DeliveryLookAt(Tune.Delivery);
			Sol.FOV = Tune.Delivery.FOV;
			break;
		}

		Sol.Location = ApplyCameraSafeguards(Sol.Location, Tune);
		return Sol;
	}

	FVector ApplyCameraSafeguards(const FVector& WantLoc, const FBroadcastTuning& Tune)
	{
		using namespace CricketGeo;
		FVector Out = WantLoc;
		Out.Z = FMath::Max(Out.Z, Tune.MinCameraHeightM * SimToWorld);
		// Stay inside the bowl: well short of the stands, above the rope line.
		const FVector2D Centre2D(0.5f * PitchLength * SimToWorld, 0.f);
		FVector2D Flat(Out.X, Out.Y);
		const float MaxR = (BoundaryRadius + 30.f) * SimToWorld;
		if ((Flat - Centre2D).Size() > MaxR)
			Flat = Centre2D + (Flat - Centre2D).GetSafeNormal() * MaxR;
		Out.X = Flat.X; Out.Y = Flat.Y;
		return Out;
	}

	FVector ApplyOcclusion(UWorld* World, const FVector& LookAt, const FVector& WantLoc)
	{
		if (!World) return WantLoc;
		const FVector Dir = WantLoc - LookAt;
		const float Len = Dir.Size();
		if (Len < 1.f) return WantLoc;
		FHitResult Hit;
		// WorldStatic covers stands, sightscreens and boards; pawns/players are ignored so a fielder
		// crossing the lens never yanks the camera.
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, LookAt, WantLoc, ECC_WorldStatic);
		if (bBlocked && Hit.Distance > 100.f)
			return LookAt + Dir.GetSafeNormal() * (Hit.Distance - 60.f);
		return WantLoc;
	}

	// ---------- Director ----------

	EBroadcastShot SelectLiveShot(EBroadcastPhase Phase, const FBroadcastFrame& Frame)
	{
		switch (Phase)
		{
		case EBroadcastPhase::PreDelivery:
		case EBroadcastPhase::RunUp:
		case EBroadcastPhase::Delivery:
		case EBroadcastPhase::Contact:
			return EBroadcastShot::StandardDelivery;
		case EBroadcastPhase::Boundary:
			// Cricket 24 holds the reverse angle through the dive and the rope; the rope camera is a replay angle.
			return EBroadcastShot::OutfieldFollow;
		case EBroadcastPhase::Wicket:
			return Frame.Dismissal == EDismissal::RunOut || Frame.Dismissal == EDismissal::Stumped
				? EBroadcastShot::RunOut
				: EBroadcastShot::WicketClose;
		case EBroadcastPhase::Replay:
			return EBroadcastShot::ReplayBeauty;
		case EBroadcastPhase::ReturnToLive:
			return EBroadcastShot::StandardDelivery;
		case EBroadcastPhase::BallDead:
			// CRICKET26.mp4 reference (§BallDead): after the ball is dead the truck cuts to the
			// bowler's close-up for 2-3 s (waist-up, walking back), then hard-cuts to the wide
			// delivery for the next ball. Never holds the wide through the dead-ball gap: that
			// reads as a game camera, not TV. Min-shot gating keeps the rope/follow up first.
			return EBroadcastShot::WicketClose;
		case EBroadcastPhase::BallInPlay:
		case EBroadcastPhase::Fielding:
		case EBroadcastPhase::Running:
			break;
		}

		// Post-contact coverage from the actual shot class (§10-16). Cricket 24 covers every ball that leaves the
		// square, boundary or not, from the high reverse angle: pitch, batters, chaser and ball in one frame, held
		// through the stop, the throw and the runs. The low fielder camera (Catch) and the crease camera (RunOut)
		// sit at head height looking out at the boards; cutting to them for a routine stop or a single read as the
		// camera falling to the turf, so live they are kept for the dismissal and a run-out that is really on.
		switch (Frame.ShotClass)
		{
		case EShotClass::NoContact:
			// Beaten or padded: the keeper's take is already in the delivery frame; hold it.
			return EBroadcastShot::StandardDelivery;
		case EShotClass::DeadDefence:
			return Frame.AfterContact > 0.9f ? EBroadcastShot::WicketClose : EBroadcastShot::StandardDelivery;
		case EShotClass::GroundInfield:
		case EShotClass::GroundOutfield:
		case EShotClass::LoftedInfield:
		case EShotClass::HighCatchChance: // a chance that goes down: the reverse angle holds the drop, as the reference does
		case EShotClass::LoftedOutfield:
		case EShotClass::BoundaryTrajectory:
			return EBroadcastShot::OutfieldFollow;
		case EShotClass::KeeperEdge:
			return EBroadcastShot::Keeper;
		case EShotClass::RunningPlay:
			// Ends and stumps matter only when the throw can beat the runner, and only once it is on its way (§16).
			return Frame.bRunOutChance && Frame.AfterContact > Frame.ThrowTime - 0.2f ? EBroadcastShot::RunOut : EBroadcastShot::OutfieldFollow;
		case EShotClass::WicketEvent:
			return Frame.Dismissal == EDismissal::RunOut || Frame.Dismissal == EDismissal::Stumped
				? EBroadcastShot::RunOut
				: (Frame.bCatchChance || Frame.bCaught ? EBroadcastShot::Catch : EBroadcastShot::WicketClose);
		}
		return EBroadcastShot::StandardDelivery;
	}

	ECameraTransition TransitionFor(EBroadcastShot From, EBroadcastShot To)
	{
		if (From == To) return ECameraTransition::Blend;
		// The stand camera behind the bowler pulling out to follow an infield ball: zoom and pan, never a cut.
		// Same physical camera, same stand position: only the lens opens and the head pans with the ball.
		if (From == EBroadcastShot::StandardDelivery && To == EBroadcastShot::GroundFollow)
			return ECameraTransition::Blend;
		// CRICKET26.mp4 reference: EVERYTHING else cuts, including the close-up back to the wide
		// (bowler face 2-3 s, then a hard cut to the delivery wide). A blend home would glide the
		// camera across the ground through the sightscreen: the cheapest-looking move in the game.
		// TV trucks cut between cameras; they only blend (ease) within one camera's move.
		return ECameraTransition::Cut;
	}

	float MinShotDuration(EBroadcastShot Shot, const FBroadcastTuning& Tune)
	{
		switch (Shot)
		{
		case EBroadcastShot::StandardDelivery: return 0.f; // the delivery shot never blocks leaving
		case EBroadcastShot::Catch:
		case EBroadcastShot::RunOut:
		case EBroadcastShot::Boundary: return FMath::Max(Tune.MinShotDuration, 1.f);
		default: return Tune.MinShotDuration;
		}
	}

	void FDirectorState::Reset(EBroadcastShot Shot)
	{
		ActiveShot = Shot;
		ShotTime = 99.f;
		bSnapped = false;
		ReplayAngle = INDEX_NONE;
	}

	EBroadcastShot FDirectorState::Update(float Dt, EBroadcastShot Desired, bool bForce, const FBroadcastTuning& Tune, bool& bOutCut, int32 Angle)
	{
		ShotTime += Dt;
		bOutCut = false;
		// Into a replay, or on to its next angle: a wipe, never a glide across the ground from the live close-up.
		if (Angle != ReplayAngle)
		{
			ReplayAngle = Angle;
			if (Angle != INDEX_NONE)
			{
				bOutCut = true;
				ActiveShot = Desired;
				ShotTime = 0.f;
				return ActiveShot;
			}
		}
		if (Desired == ActiveShot) return ActiveShot;
		// No thrashing (§17): a young shot survives unless the event forces the change.
		if (!bForce && ShotTime < MinShotDuration(ActiveShot, Tune)) return ActiveShot;
		// No ping-pong: A -> B -> A inside two minimum durations is held on B.
		bOutCut = TransitionFor(ActiveShot, Desired) == ECameraTransition::Cut;
		ActiveShot = Desired;
		ShotTime = 0.f;
		return ActiveShot;
	}

	void FSmoother::Snap(const FVector& Loc, const FQuat& Rot, float InFOV)
	{
		Location = Loc;
		Rotation = Rot;
		FOV = InFOV;
		bInit = true;
	}

	void FSmoother::Update(const FVector& WantLoc, const FVector& LookAt, float WantFOV, float Dt,
		float PosLambda, float RotLambda, float MaxDegPerSec, float DeadZoneCm, float FovLambda)
	{
		const FQuat WantRot = (LookAt - Location).GetSafeNormal().Rotation().Quaternion();
		if (!bInit)
		{
			Snap(WantLoc, (LookAt - WantLoc).GetSafeNormal().Rotation().Quaternion(), WantFOV);
			return;
		}
		// Position: exponential, critically-damped feel, no overshoot by construction.
		Location = FMath::Lerp(Location, WantLoc, DampFactor(PosLambda, Dt));
		// Rotation: dead zone kills micro-jitter; slew cap kills whips.
		FVector CurDir = Rotation.Vector(), WantDir = (LookAt - Location).GetSafeNormal();
		const float ErrCm = (LookAt - (Location + CurDir * FVector::Dist(Location, LookAt))).Size();
		if (ErrCm > DeadZoneCm * FMath::Clamp(FOV / 40.f, 0.02f, 1.f))
		{
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(CurDir, WantDir), -1.f, 1.f)));
			const float MaxStep = MaxDegPerSec * Dt;
			const float Step = FMath::Min(MaxStep, Angle * DampFactor(RotLambda, Dt)); // eased, and never faster than the cap
			const FVector Axis = FVector::CrossProduct(CurDir, WantDir).GetSafeNormal();
			if (Axis.SizeSquared() > 1e-6f && Angle > 1e-3f)
				Rotation = (FQuat(Axis, FMath::DegreesToRadians(Step)) * Rotation).GetNormalized();
			else
				Rotation = FMath::QInterpTo(Rotation, WantRot, Dt, RotLambda).GetNormalized();
		}
		FOV = FMath::Lerp(FOV, WantFOV, DampFactor(FovLambda < 0.f ? PosLambda : FovLambda, Dt));
		// Broadcast cameras keep a level horizon: successive small rotations about varying axes
		// would otherwise compose into a Dutch tilt while tracking across the sky. Roll is not a
		// broadcast move, so it is removed every frame (yaw/pitch are untouched).
		{
			FRotator Level = Rotation.Rotator();
			Level.Roll = 0.f;
			Rotation = Level.Quaternion();
		}
	}

	// ---------- Replay direction ----------

	FReplayTrigger ClassifyReplayEvent(const FDeliveryResult& Result, const FDeliveryOutcome& Outcome, bool bMilestone)
	{
		FReplayTrigger T;
		const bool bPath = Result.BallPath.Num() > 1;
		if (!bPath) return T;

		auto Decisive = [&]() -> float
		{
			if (Result.bStumpsHit) return Result.StumpsTime;
			if (Result.BrokenTime >= 0.f) return Result.BrokenTime;
			if (Result.Fielding.bCaught) return Result.ContactTime + Result.Fielding.FieldTime;
			return Result.ContactTime;
		};

		if (bMilestone)
		{
			T.Event = EReplayEventType::Milestone;
			T.Priority = EReplayPriority::Hero;
			T.DecisiveT = Decisive();
			return T;
		}

		switch (Outcome.Dismissal)
		{
		case EDismissal::Bowled: T.Event = EReplayEventType::Bowled; T.Priority = EReplayPriority::High; break;
		case EDismissal::LBW: T.Event = EReplayEventType::LBW; T.Priority = EReplayPriority::High; break;
		case EDismissal::Stumped: T.Event = EReplayEventType::Stumped; T.Priority = EReplayPriority::Hero; break;
		case EDismissal::HitWicket: T.Event = EReplayEventType::HitWicket; T.Priority = EReplayPriority::High; break;
		case EDismissal::Caught:
		{
			const bool bDiving = Result.Fielding.bDive
				|| Result.Fielding.Action == EFieldAction::CatchDiving
				|| Result.Fielding.Action == EFieldAction::CatchBoundary
				|| Result.Fielding.Action == EFieldAction::CatchRelay;
			T.Event = bDiving ? EReplayEventType::DivingCatch : EReplayEventType::Caught;
			T.Priority = bDiving ? EReplayPriority::Hero : EReplayPriority::High;
			break;
		}
		case EDismissal::RunOut:
		{
			const bool bDirect = Result.Running.bDirectHit;
			T.Event = EReplayEventType::RunOut;
			T.Priority = bDirect ? EReplayPriority::Hero : EReplayPriority::High;
			break;
		}
		default: break;
		}
		if (T.Event != EReplayEventType::None) { T.DecisiveT = Decisive(); return T; }

		if (Outcome.Boundary == 6) { T.Event = EReplayEventType::Six; T.Priority = EReplayPriority::High; T.DecisiveT = Result.ContactTime; return T; }
		if (Outcome.Boundary == 4) { T.Event = EReplayEventType::Four; T.Priority = EReplayPriority::Medium; T.DecisiveT = Result.ContactTime; return T; }

		// Close chances that survived: still replay-worthy, lighter package.
		if (Result.Fielding.bCatchChance && !Result.Fielding.bCaught)
		{
			T.Event = EReplayEventType::DroppedCatch;
			T.Priority = EReplayPriority::Medium;
			T.DecisiveT = Result.ContactTime + Result.Fielding.FieldTime;
			return T;
		}
		{
			const EContactZone Z = Result.Contact.Zone;
			const bool bEdge = Z == EContactZone::InsideEdge || Z == EContactZone::OutsideEdge
				|| Z == EContactZone::TopEdge || Z == EContactZone::BottomEdge;
			if (bEdge && Result.Contact.HasContact())
			{
				T.Event = EReplayEventType::EdgeNotOut;
				T.Priority = EReplayPriority::Low;
				T.DecisiveT = Result.ContactTime;
				return T;
			}
		}
		if (Result.Running.Attempted > 0 && FMath::Abs(Result.Running.Margin) < 0.25f)
		{
			T.Event = EReplayEventType::CloseRunOut;
			T.Priority = EReplayPriority::Medium;
			T.DecisiveT = Result.BrokenTime >= 0.f ? Result.BrokenTime : Result.ContactTime + Result.Running.ThrowArrive;
			return T;
		}
		if (Result.Fielding.bDive || Result.Fielding.Action == EFieldAction::SlideStop || Result.Fielding.Action == EFieldAction::DiveStop)
		{
			T.Event = EReplayEventType::SpectacularStop;
			T.Priority = EReplayPriority::Low;
			T.DecisiveT = Result.ContactTime + Result.Fielding.FieldTime;
			return T;
		}
		return T; // dot balls: no replay
	}

	EBroadcastShot PickReplayShot(const TArray<EBroadcastShot>& Candidates, TArray<EBroadcastShot>& Recent)
	{
		if (Candidates.Num() == 0) return EBroadcastShot::ReplayBeauty;
		// First candidate is the best angle: take it unless it just played (then the next-best
		// unused one). Variation never means a bad angle (§28): order encodes quality.
		for (const EBroadcastShot S : Candidates)
			if (!Recent.Contains(S)) { Recent.Add(S); if (Recent.Num() > 6) Recent.RemoveAt(0); return S; }
		// All played recently: reuse the best, it is still the right angle.
		return Candidates[0];
	}

	static void AddAngle(FReplayPackage& P, EBroadcastShot Shot, float StartTp, float EndTp, float DecisiveTp, float Slow, float Wall)
	{
		FReplayAnglePlay A;
		A.Shot = Shot;
		A.StartTp = FMath::Max(StartTp, 0.f);
		A.EndTp = FMath::Max(EndTp, A.StartTp + 0.2f);
		A.DecisiveTp = FMath::Clamp(DecisiveTp, A.StartTp, A.EndTp);
		A.SlowFactor = Slow;
		A.WallTime = Wall;
		P.Angles.Add(A);
	}

	FReplayPackage BuildReplayPackage(const FReplayTrigger& Trigger, const FDeliveryResult& Result,
		const FBroadcastFrame& Frame, const FBroadcastTuning& Tune, TArray<EBroadcastShot>& RecentShots)
	{
		FReplayPackage P;
		P.Event = Trigger.Event;
		P.Priority = Trigger.Priority;
		if (Trigger.Priority == EReplayPriority::None) return P;

		const float Contact = Result.ContactTime;
		const float Lead = FMath::Min(Tune.ReplayLeadMax, Contact * 0.9f);
		const float Start = FMath::Max(0.f, Contact - Lead);
		const float End = FMath::Min(Result.DeadTime, Trigger.DecisiveT + 1.2f);
		const float Wall = Tune.ReplayAngleTime;
		const float Slow = Tune.ReplaySlowFactor, SuperSlow = Tune.SuperSlowFactor;

		auto CountFor = [&]() -> int32
		{
			switch (Trigger.Priority)
			{
			case EReplayPriority::Low: return 1;
			case EReplayPriority::Medium: return 2;
			case EReplayPriority::High: return 3;
			case EReplayPriority::Hero: return 3;
			default: return 0;
			}
		};
		const int32 Count = CountFor();

		switch (Trigger.Event)
		{
		case EReplayEventType::Six:
			// Contact, then the cinematic batter-side angle, then the flight to the rope. The flight
			// angle joins the ball late (already deep): joining at the bat leaves the ball a speck
			// from the rope camera for most of the angle.
			AddAngle(P, PickReplayShot({ EBroadcastShot::ReplayBeauty, EBroadcastShot::AlternateDelivery }, RecentShots),
				Start, Contact + 1.f, Contact, 0.5f, Wall);
			if (Count >= 2) AddAngle(P, PickReplayShot({ EBroadcastShot::SideOn, EBroadcastShot::StraightOn }, RecentShots),
				FMath::Max(0.f, Contact - 0.3f), Contact + 0.5f, Contact, SuperSlow, Wall * 0.8f);
			if (Count >= 3) AddAngle(P, PickReplayShot({ EBroadcastShot::AerialBall, EBroadcastShot::Boundary, EBroadcastShot::OutfieldFollow }, RecentShots),
				FMath::Max(Contact + 0.5f, Result.ContactTime + Result.Fielding.BoundaryTime - 2.2f), End, End - 0.3f, 0.6f, Wall);
			break;
		case EReplayEventType::Four:
			AddAngle(P, PickReplayShot({ EBroadcastShot::ReplayBeauty, EBroadcastShot::AlternateDelivery }, RecentShots),
				Start, Contact + 1.f, Contact, 0.5f, Wall);
			if (Count >= 2) AddAngle(P, PickReplayShot({ EBroadcastShot::Boundary, EBroadcastShot::OutfieldFollow, EBroadcastShot::GroundFollow }, RecentShots),
				FMath::Max(Contact + 0.5f, Result.ContactTime + Result.Fielding.BoundaryTime - 1.8f), End, End - 0.2f, 0.7f, Wall);
			break;
		case EReplayEventType::Bowled:
		case EReplayEventType::LBW:
			// Release-to-impact readability first (§32), then the tight slow angle.
			AddAngle(P, PickReplayShot({ EBroadcastShot::StandardDelivery, EBroadcastShot::StraightOn }, RecentShots),
				FMath::Max(0.f, Contact - Lead - 0.4f), Trigger.DecisiveT + 0.3f, Contact, 0.5f, Wall);
			if (Count >= 2) AddAngle(P, PickReplayShot({ EBroadcastShot::ReplaySlowMo, EBroadcastShot::SideOn }, RecentShots),
				FMath::Max(0.f, Contact - 0.35f), Trigger.DecisiveT + 0.3f, Trigger.DecisiveT, SuperSlow, Wall * 0.8f);
			if (Count >= 3) AddAngle(P, PickReplayShot({ EBroadcastShot::SideOn, EBroadcastShot::BatterEnd }, RecentShots),
				Start, Trigger.DecisiveT + 0.4f, Contact, 0.5f, Wall);
			break;
		case EReplayEventType::Caught:
		case EReplayEventType::DivingCatch:
			// Shot, flight, approach, take (§33); the spectacular body gets the second angle.
			AddAngle(P, PickReplayShot({ EBroadcastShot::ReplayBeauty, EBroadcastShot::SideOn }, RecentShots),
				Start, Contact + 0.9f, Contact, 0.5f, Wall);
			if (Count >= 2) AddAngle(P, PickReplayShot({ EBroadcastShot::Catch, EBroadcastShot::AerialBall, EBroadcastShot::OutfieldFollow }, RecentShots),
				Contact + 0.3f, Trigger.DecisiveT + 0.5f, Trigger.DecisiveT, Slow, Wall);
			if (Count >= 3) AddAngle(P, PickReplayShot({ EBroadcastShot::ReplaySlowMo, EBroadcastShot::SideOn }, RecentShots),
				FMath::Max(0.f, Trigger.DecisiveT - 0.5f), Trigger.DecisiveT + 0.4f, Trigger.DecisiveT, SuperSlow, Wall * 0.8f);
			break;
		case EReplayEventType::RunOut:
		case EReplayEventType::CloseRunOut:
		case EReplayEventType::Stumped:
			// Diagnostic over cinematic (§34): runner, crease, ball, broken stumps.
			AddAngle(P, PickReplayShot({ EBroadcastShot::RunOut, EBroadcastShot::SideOn }, RecentShots),
				FMath::Max(0.f, Trigger.DecisiveT - 1.f), Trigger.DecisiveT + 0.5f, Trigger.DecisiveT, Slow, Wall);
			if (Count >= 2) AddAngle(P, PickReplayShot({ EBroadcastShot::ReplayBeauty, EBroadcastShot::GroundFollow }, RecentShots),
				Start, Contact + 0.9f, Contact, 0.6f, Wall);
			if (Count >= 3) AddAngle(P, PickReplayShot({ EBroadcastShot::ReplaySlowMo, EBroadcastShot::StraightOn }, RecentShots),
				FMath::Max(0.f, Trigger.DecisiveT - 0.5f), Trigger.DecisiveT + 0.4f, Trigger.DecisiveT, SuperSlow, Wall * 0.8f);
			break;
		default:
			// Four-edge cases, drops, stops, milestones: clean broadcast replay + one closer look.
			AddAngle(P, PickReplayShot({ EBroadcastShot::ReplayBeauty, EBroadcastShot::SideOn, EBroadcastShot::AlternateDelivery }, RecentShots),
				Start, Contact + 0.9f, Contact, 0.5f, Wall);
			if (Count >= 2)
			{
				const EBroadcastShot Close = Trigger.Event == EReplayEventType::EdgeNotOut
					? PickReplayShot({ EBroadcastShot::Keeper, EBroadcastShot::Slip, EBroadcastShot::BatterEnd }, RecentShots)
					: PickReplayShot({ EBroadcastShot::ReplaySlowMo, EBroadcastShot::Catch, EBroadcastShot::OutfieldFollow }, RecentShots);
				AddAngle(P, Close, FMath::Max(0.f, Trigger.DecisiveT - 0.5f), Trigger.DecisiveT + 0.5f, Trigger.DecisiveT, SuperSlow, Wall * 0.8f);
			}
			if (Count >= 3) AddAngle(P, PickReplayShot({ EBroadcastShot::StandardDelivery, EBroadcastShot::StraightOn }, RecentShots),
				FMath::Max(0.f, Contact - Lead), Contact + 0.6f, Contact, 0.6f, Wall);
			break;
		}

		// Readability guard (§31): never replay so tight the action is lost before it starts.
		if (P.Angles.Num() > 0 && (P.Angles[0].Shot == EBroadcastShot::ReplaySlowMo || P.Angles[0].Shot == EBroadcastShot::Keeper))
			P.Angles[0].Shot = EBroadcastShot::ReplayBeauty;

		// The full replay leads every package, as a TV replay does: the whole ball from the bowler's delivery
		// stride to the end of the event, directed like live coverage (delivery lens, then the follow), near
		// real speed with a gentle slow through the decisive moment. The detail angles follow it; the ones it
		// already shows (the delivery lens, the live follows) are dropped.
		FReplayAnglePlay Full;
		Full.bFullPass = true;
		Full.Shot = EBroadcastShot::StandardDelivery;
		Full.StartTp = -Tune.ReplayFullLead;
		Full.DecisiveTp = Trigger.DecisiveT;
		const float Rope = Result.Fielding.Boundary > 0 ? Result.ContactTime + Result.Fielding.BoundaryTime + 0.6f : -1.f;
		Full.EndTp = Rope > 0.f ? Rope : Trigger.DecisiveT + 1.2f;
		// ponytail: capped span: a ball chased to the rope and thrown back is cut at the cap, not at the throw.
		Full.EndTp = FMath::Clamp(FMath::Min(Full.EndTp, Result.DeadTime), Full.DecisiveTp + 0.3f, Full.StartTp + Tune.ReplayFullMax);
		Full.SlowFactor = Tune.ReplayFullSlow;
		Full.WallTime = NaturalWallTime(Full);
		P.Angles.RemoveAll([](const FReplayAnglePlay& A)
		{
			return A.Shot == EBroadcastShot::StandardDelivery || A.Shot == EBroadcastShot::OutfieldFollow || A.Shot == EBroadcastShot::GroundFollow;
		});
		P.Angles.Insert(Full, 0);
		// Two angles (full + the stroke, as the reference plays a FOUR) unless the event earns a third.
		P.Angles.SetNum(FMath::Min(P.Angles.Num(), FMath::Clamp(Count, 2, 3)));
		(void)Frame;
		return P;
	}

	float FReplayPackage::TotalWallTime() const
	{
		float T = 0.f;
		for (const FReplayAnglePlay& A : Angles) T += A.WallTime;
		return T;
	}

	float ReplaySpeedAt(float BallT, float DecisiveT, float SlowFactor, float Width)
	{
		// Smooth dip: 1 far away, SlowFactor at the moment. Cosine pulse, C1 continuous (§30).
		const float D = FMath::Abs(BallT - DecisiveT) / FMath::Max(Width, 0.05f);
		if (D >= 1.f) return 1.f;
		const float Pulse = 0.5f + 0.5f * FMath::Cos(PI * FMath::Clamp(D, 0.f, 1.f));
		return FMath::Lerp(1.f, SlowFactor, Pulse);
	}

	FTimeRemap BuildTimeRemap(const FReplayAnglePlay& Angle, int32 Samples)
	{
		FTimeRemap R;
		R.WallTime = FMath::Max(Angle.WallTime, 0.2f);
		// Integrate ball time over wall time with the speed curve, then normalize so the angle shows
		// exactly [StartTp, EndTp]: the pacing breathes, the endpoints are exact.
		TArray<float> Cum;
		Cum.SetNumUninitialized(Samples + 1);
		Cum[0] = 0.f;
		for (int32 I = 1; I <= Samples; ++I)
		{
			const float BallGuess = Angle.StartTp + (Angle.EndTp - Angle.StartTp) * float(I - 1) / float(Samples);
			Cum[I] = Cum[I - 1] + ReplaySpeedAt(BallGuess, Angle.DecisiveTp, Angle.SlowFactor);
		}
		const float Total = Cum[Samples];
		R.BallAt.SetNumUninitialized(Samples + 1);
		for (int32 I = 0; I <= Samples; ++I)
			R.BallAt[I] = Angle.StartTp + (Angle.EndTp - Angle.StartTp) * (Total > 1e-6f ? Cum[I] / Total : float(I) / float(Samples));
		return R;
	}

	float NaturalWallTime(const FReplayAnglePlay& Angle, int32 Samples)
	{
		// Wall = integral of d(ball) / speed: the remap then plays exactly the speed curve.
		const float Step = (Angle.EndTp - Angle.StartTp) / float(FMath::Max(Samples, 1));
		float Wall = 0.f;
		for (int32 I = 0; I < Samples; ++I)
			Wall += Step / FMath::Max(ReplaySpeedAt(Angle.StartTp + (I + 0.5f) * Step, Angle.DecisiveTp, Angle.SlowFactor), 0.05f);
		return FMath::Max(Wall, 0.2f);
	}

	float FTimeRemap::Sample(float WallInto) const
	{
		if (BallAt.Num() < 2) return 0.f;
		const float U = FMath::Clamp(WallInto / FMath::Max(WallTime, 1e-4f), 0.f, 1.f) * float(BallAt.Num() - 1);
		const int32 I = FMath::Min(int32(U), BallAt.Num() - 2);
		return FMath::Lerp(BallAt[I], BallAt[I + 1], U - float(I));
	}

	float FTimeRemap::BallSpan() const
	{
		if (BallAt.Num() < 2) return 0.f;
		return BallAt.Last() - BallAt[0];
	}

	// ---------- Debug ----------

	const TCHAR* ShotName(EBroadcastShot Shot)
	{
		switch (Shot)
		{
		case EBroadcastShot::StandardDelivery: return TEXT("DELIVERY");
		case EBroadcastShot::AlternateDelivery: return TEXT("ALT-DELIVERY");
		case EBroadcastShot::StraightOn: return TEXT("STRAIGHT-ON");
		case EBroadcastShot::BatterEnd: return TEXT("BATTER-END");
		case EBroadcastShot::SideOn: return TEXT("SIDE-ON");
		case EBroadcastShot::GroundFollow: return TEXT("GROUND-FOLLOW");
		case EBroadcastShot::OutfieldFollow: return TEXT("OUTFIELD-FOLLOW");
		case EBroadcastShot::AerialBall: return TEXT("AERIAL");
		case EBroadcastShot::Boundary: return TEXT("BOUNDARY");
		case EBroadcastShot::Keeper: return TEXT("KEEPER");
		case EBroadcastShot::Slip: return TEXT("SLIP");
		case EBroadcastShot::RunOut: return TEXT("RUN-OUT");
		case EBroadcastShot::WicketClose: return TEXT("WICKET-CLOSE");
		case EBroadcastShot::Catch: return TEXT("CATCH");
		case EBroadcastShot::Presentation: return TEXT("PRESENTATION");
		case EBroadcastShot::ReplayBeauty: return TEXT("REPLAY-BEAUTY");
		case EBroadcastShot::ReplaySlowMo: return TEXT("REPLAY-SLOWMO");
		case EBroadcastShot::Review: return TEXT("REVIEW");
		case EBroadcastShot::Scorecard: return TEXT("SCORECARD");
		default: return TEXT("?");
		}
	}

	const TCHAR* PhaseName(EBroadcastPhase Phase)
	{
		switch (Phase)
		{
		case EBroadcastPhase::PreDelivery: return TEXT("PRE-DELIVERY");
		case EBroadcastPhase::RunUp: return TEXT("RUN-UP");
		case EBroadcastPhase::Delivery: return TEXT("DELIVERY");
		case EBroadcastPhase::Contact: return TEXT("CONTACT");
		case EBroadcastPhase::BallInPlay: return TEXT("BALL-IN-PLAY");
		case EBroadcastPhase::Fielding: return TEXT("FIELDING");
		case EBroadcastPhase::Running: return TEXT("RUNNING");
		case EBroadcastPhase::BallDead: return TEXT("BALL-DEAD");
		case EBroadcastPhase::Wicket: return TEXT("WICKET");
		case EBroadcastPhase::Boundary: return TEXT("BOUNDARY");
		case EBroadcastPhase::Replay: return TEXT("REPLAY");
		case EBroadcastPhase::ReturnToLive: return TEXT("RETURN-TO-LIVE");
		default: return TEXT("?");
		}
	}

	const TCHAR* ReplayEventName(EReplayEventType Event)
	{
		switch (Event)
		{
		case EReplayEventType::Four: return TEXT("FOUR");
		case EReplayEventType::Six: return TEXT("SIX");
		case EReplayEventType::Bowled: return TEXT("BOWLED");
		case EReplayEventType::Caught: return TEXT("CAUGHT");
		case EReplayEventType::DivingCatch: return TEXT("DIVING-CATCH");
		case EReplayEventType::LBW: return TEXT("LBW");
		case EReplayEventType::RunOut: return TEXT("RUN-OUT");
		case EReplayEventType::CloseRunOut: return TEXT("CLOSE-RUN-OUT");
		case EReplayEventType::Stumped: return TEXT("STUMPED");
		case EReplayEventType::HitWicket: return TEXT("HIT-WICKET");
		case EReplayEventType::EdgeNotOut: return TEXT("EDGE");
		case EReplayEventType::DroppedCatch: return TEXT("DROPPED-CATCH");
		case EReplayEventType::SpectacularStop: return TEXT("STOP");
		case EReplayEventType::Milestone: return TEXT("MILESTONE");
		default: return TEXT("NONE");
		}
	}

	FString SummarizeShot(EBroadcastShot Shot, const FLiveCameraSolution& Sol, float ShotTime)
	{
		return FString::Printf(TEXT("%s %.1fs FOV %.1f"), ShotName(Shot), ShotTime, Sol.FOV);
	}
}
