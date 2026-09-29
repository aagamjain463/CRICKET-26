#include "CricketBatter.h"

namespace CricketBatter
{
namespace
{
	using CricketPose::FBat;

	// The batter frame: X toward the bowler, Y toward the off side, Z up, as for a right-hander. Yaws are degrees
	// from facing the off side (side-on) toward facing the bowler (front-on). Foot [0] is the front foot.
	struct FFootL
	{
		FVector2D Ball = FVector2D::ZeroVector;
		float Toe = 0.f, Heel = 0.f;
	};

	struct FPoseL
	{
		FVector2D Pelvis = FVector2D(-0.01f, -0.05f);
		float Drop = 0.07f;
		float Hips = 10.f, Chest = 22.f;
		float Flex = 16.f, Side = 0.f; // chest bent forward, and toward the front side (deg)
		FFootL Foot[2] = { { FVector2D(0.24f, 0.07f), 15.f, 0.f }, { FVector2D(-0.23f, 0.07f), 0.f, 0.f } };
	};

	// When each part of the body moves from the last phase's pose to this one: the weight (pelvis, knees and
	// forward bend), the hips' turn, the chest's turn, and the feet, stepping Steps times between them.
	struct FPhase
	{
		FPoseL To;
		float Weight0 = 0.f, Weight1 = 0.f, Hips0 = 0.f, Hips1 = 0.f, Chest0 = 0.f, Chest1 = 0.f, Feet0 = 0.f, Feet1 = 0.f;
		int32 Steps = 0, First = 0;
		float Height = 0.04f;
	};

	constexpr float HipHeight = 0.97f, LegLength = 0.86f; // a nominal adult's hips standing, and hip to ankle a little bent

	float Ease(float A, float B, float T) { return B > A ? FMath::SmoothStep(A, B, T) : (T >= B ? 1.f : 0.f); }

	FVector Facing(float YawDeg)
	{
		const float R = FMath::DegreesToRadians(YawDeg);
		return FVector(FMath::Sin(R), FMath::Cos(R), 0.f);
	}

	// Toward the front shoulder, square to the facing.
	FVector FrontSide(float YawDeg)
	{
		const float R = FMath::DegreesToRadians(YawDeg);
		return FVector(FMath::Cos(R), -FMath::Sin(R), 0.f);
	}

	FVector ChestUp(const FPoseL& P)
	{
		const float F = FMath::DegreesToRadians(P.Flex), S = FMath::DegreesToRadians(P.Side);
		return (FVector::UpVector * FMath::Cos(F) + Facing(P.Chest) * FMath::Sin(F) + FrontSide(P.Chest) * FMath::Sin(S)).GetSafeNormal();
	}

	// A nominal adult's shoulders ([0] front, [1] back) in pose P: the reach checks' model of the body.
	void Shoulders(const FPoseL& P, FVector (&Out)[2])
	{
		const FVector Mid = FVector(P.Pelvis, 0.97f - P.Drop) + ChestUp(P) * 0.49f - Facing(P.Chest) * 0.03f;
		Out[0] = Mid + FrontSide(P.Chest) * 0.18f;
		Out[1] = Mid - FrontSide(P.Chest) * 0.18f;
	}

	FFootL Lerp(const FFootL& A, const FFootL& B, float W)
	{
		return { FMath::Lerp(A.Ball, B.Ball, W), FMath::Lerp(A.Toe, B.Toe, W), FMath::Lerp(A.Heel, B.Heel, W) };
	}

	// Feet from A to B in Steps alternate steps between T0 and T1, foot First first. A foot's ball only moves while
	// that foot is off the ground; a foot with no steps (or a step shorter than a centimetre) pivots on its ball.
	void Walk(FFootL (&Out)[2], float (&Lift)[2], const FFootL (&A)[2], const FFootL (&B)[2], float T, float T0, float T1, int32 Steps, int32 First, float Height)
	{
		const int32 Count[2] = { First == 0 ? (Steps + 1) / 2 : Steps / 2, First == 1 ? (Steps + 1) / 2 : Steps / 2 };
		const float Span = FMath::Max(T1 - T0, 0.01f);
		for (int32 F = 0; F < 2; ++F)
		{
			if (Count[F] > 0 && FVector2D::Distance(A[F].Ball, B[F].Ball) >= 0.01f) continue;
			Out[F] = Lerp(A[F], B[F], Ease(T0, T1, T));
			Out[F].Ball = A[F].Ball;
			Lift[F] = 0.f;
		}
		const float Each = Span / FMath::Max(Steps, 1);
		for (int32 K = 0; K < Steps; ++K)
		{
			const int32 F = (First + K) % 2;
			if (Count[F] == 0 || FVector2D::Distance(A[F].Ball, B[F].Ball) < 0.01f) continue;
			const float U = FMath::Clamp((T - T0 - K * Each) / Each, 0.f, 1.f);
			if (T < T0 + K * Each) continue;
			const int32 M = K / 2;
			const FFootL From = Lerp(A[F], B[F], float(M) / Count[F]), To = Lerp(A[F], B[F], float(M + 1) / Count[F]);
			Out[F] = Lerp(From, To, FMath::SmoothStep(0.f, 1.f, U));
			Lift[F] = U < 1.f ? Height * FMath::Sin(PI * U) * FMath::Min(1.f, FVector2D::Distance(From.Ball, To.Ball) / 0.08f) : 0.f;
		}
	}

	// Ride: how much (0 to 1) the hips are carried Offset (m) past the middle of the feet, rather than where the
	// phases put them: charging, the feet's shuffle sets the pace.
	FPoseL Evaluate(const FPoseL& Start, TConstArrayView<FPhase> Phases, float T, float (&Lift)[2], float Ride = 0.f, float Offset = 0.f)
	{
		FPoseL P = Start;
		Lift[0] = Lift[1] = 0.f;
		const FPoseL* Prev = &Start;
		for (const FPhase& Ph : Phases)
		{
			const float W = Ease(Ph.Weight0, Ph.Weight1, T), H = Ease(Ph.Hips0, Ph.Hips1, T), C = Ease(Ph.Chest0, Ph.Chest1, T);
			P.Pelvis = FMath::Lerp(P.Pelvis, Ph.To.Pelvis, W);
			P.Drop = FMath::Lerp(P.Drop, Ph.To.Drop, W);
			P.Flex = FMath::Lerp(P.Flex, Ph.To.Flex, W);
			P.Hips = FMath::Lerp(P.Hips, Ph.To.Hips, H);
			P.Chest = FMath::Lerp(P.Chest, Ph.To.Chest, C);
			P.Side = FMath::Lerp(P.Side, Ph.To.Side, C);
			if (T >= Ph.Feet0) Walk(P.Foot, Lift, Prev->Foot, Ph.To.Foot, T, Ph.Feet0, Ph.Feet1, Ph.Steps, Ph.First, Ph.Height);
			Prev = &Ph.To;
		}
		// The hips ride between the feet: never out past the leading one, nor far back of the middle (a body walking
		// down the track moves on its feet, neither ahead of them nor left behind them).
		const float Behind = FMath::Min(P.Foot[0].Ball.X, P.Foot[1].Ball.X), Ahead = FMath::Max(P.Foot[0].Ball.X, P.Foot[1].Ball.X);
		// Nor more than 0.8 m past either foot, so neither leg is stretched out flat behind a stride.
		const float Mid = 0.5f * (Behind + Ahead), Low = FMath::Max3(Behind, Mid - 0.2f, Ahead - 0.8f);
		P.Pelvis.X = FMath::Lerp(P.Pelvis.X, Mid + Offset, Ride);
		P.Pelvis.X = FMath::Clamp(P.Pelvis.X, Low, FMath::Max(Low, FMath::Min(Ahead - 0.05f, Behind + 0.8f)));
		// Sit as low as a long stride needs: each hip within a slightly bent leg of its ankle, which is behind and
		// above the ball of the foot, higher as the heel comes up.
		for (int32 F = 0; F < 2; ++F)
		{
			const float H = FMath::DegreesToRadians(P.Foot[F].Heel), C = FMath::Cos(H), S = FMath::Sin(H);
			const FVector2D Toe(Facing(P.Foot[F].Toe));
			const FVector2D Ankle = P.Foot[F].Ball + Toe * (-0.146f * C + 0.078f * S);
			const float AnkleZ = 0.01f + 0.146f * S + 0.078f * C + Lift[F];
			const FVector2D Hip = P.Pelvis + FVector2D(FrontSide(P.Hips)) * (F == 0 ? 0.1f : -0.1f);
			const float Flat = FMath::Min(FVector2D::Distance(Hip, Ankle), LegLength - 0.01f);
			P.Drop = FMath::Max(P.Drop, HipHeight - AnkleZ - FMath::Sqrt(LegLength * LegLength - Flat * Flat));
		}
		return P;
	}

	// The bat in the batter frame, as a grip and a rotation (Z along the bat toward the toe, Y out of the face).
	struct FBatL
	{
		FVector Grip = FVector::ZeroVector;
		FQuat Rot = FQuat::Identity;
		FVector Axis() const { return Rot.GetAxisZ(); }
		FVector Face() const { return Rot.GetAxisY(); }
	};

	FQuat BatRot(const FVector& Axis, const FVector& FaceHint) { return FRotationMatrix::MakeFromZY(Axis, FaceHint).ToQuat(); }

	FBatL Bat(const FVector& Grip, const FVector& Axis, const FVector& FaceHint) { return { Grip, BatRot(Axis.GetSafeNormal(), FaceHint) }; }

	FBatL Lerp(const FBatL& A, const FBatL& B, float W) { return { FMath::Lerp(A.Grip, B.Grip, W), FQuat::Slerp(A.Rot, B.Rot, W).GetNormalized() }; }

	// Through keys at the given times: positions on a cubic through them with tangents from their neighbours
	// (at rest at the first and last), and the rotation the same way on its axis and face, so the bat's path and
	// turn both run smoothly through every key.
	struct FKey
	{
		float T = 0.f;
		FBatL Bat;
	};

	FVector Hermite(const FVector& P0, const FVector& M0, const FVector& P1, const FVector& M1, float H, float U)
	{
		const float U2 = U * U, U3 = U2 * U;
		return P0 * (2.f * U3 - 3.f * U2 + 1.f) + M0 * (H * (U3 - 2.f * U2 + U)) + P1 * (-2.f * U3 + 3.f * U2) + M1 * (H * (U3 - U2));
	}

	FBatL Spline(TConstArrayView<FKey> Keys, float T)
	{
		const int32 N = Keys.Num();
		if (T <= Keys[0].T) return Keys[0].Bat;
		if (T >= Keys[N - 1].T) return Keys[N - 1].Bat;
		int32 I = 0;
		while (T > Keys[I + 1].T) ++I;
		auto Tangent = [&](int32 K, auto Get)
		{
			if (K == 0 || K == N - 1) return FVector::ZeroVector;
			return (Get(Keys[K + 1].Bat) - Get(Keys[K - 1].Bat)) / FMath::Max(Keys[K + 1].T - Keys[K - 1].T, 0.01f);
		};
		const float H = FMath::Max(Keys[I + 1].T - Keys[I].T, 0.001f), U = (T - Keys[I].T) / H;
		auto Along = [&](auto Get) { return Hermite(Get(Keys[I].Bat), Tangent(I, Get), Get(Keys[I + 1].Bat), Tangent(I + 1, Get), H, U); };
		const FVector Grip = Along([](const FBatL& B) { return B.Grip; });
		const FVector Axis = Along([](const FBatL& B) { return B.Axis(); });
		const FVector Face = Along([](const FBatL& B) { return B.Face(); });
		return { Grip, BatRot(Axis.GetSafeNormal(UE_SMALL_NUMBER, Keys[I].Bat.Axis()), Face) };
	}

	enum class EFamily : uint8 { Vertical, Pull, Cut, Sweep };

	EFamily FamilyOf(EShotType Shot)
	{
		switch (Shot)
		{
		case EShotType::Pull: case EShotType::Hook: return EFamily::Pull;
		case EShotType::Cut: return EFamily::Cut;
		case EShotType::Sweep: case EShotType::SlogSweep: case EShotType::ReverseSweep: case EShotType::Scoop: return EFamily::Sweep;
		default: return EFamily::Vertical;
		}
	}

	// How far the bat turns (deg, about the swing's axis) from its contact pose: back to the top of the downswing
	// and its loop, and on through the two follow-through keys.
	struct FArc
	{
		float Top = -140.f, Loop = -70.f, Through = 50.f, Finish = 190.f; // through the ball the hands lead up, the bat still near upright
	};

	FArc ArcOf(EShotType Shot, EBatterStyle Style = EBatterStyle::Classical, bool bMiss = false)
	{
		if (bMiss)
		{
			if (Shot == EShotType::Defend) return { -55.f, -25.f, 4.f, 6.f };
			return { -120.f, -60.f, 35.f, 65.f }; // checked swing, natural deceleration
		}
		switch (Shot)
		{
		case EShotType::Defend: return { -60.f, -28.f, 6.f, 8.f };         // short backlift, the bat checked at the ball
		case EShotType::Punch: return { -100.f, -50.f, 45.f, 80.f };
		case EShotType::Loft:
			if (Style == EBatterStyle::PowerHitter) return { -155.f, -80.f, 60.f, 220.f };
			return { -150.f, -75.f, 55.f, 210.f };
		case EShotType::Flick:
			if (Style == EBatterStyle::Unorthodox) return { -125.f, -65.f, 85.f, 180.f };
			return { -120.f, -60.f, 80.f, 160.f };
		case EShotType::Cut: return { -140.f, -70.f, 45.f, 68.f };           // a tilted plane: further round lifts the toe
		case EShotType::Pull: case EShotType::Hook:
			if (Style == EBatterStyle::ExpressPuller) return { -155.f, -75.f, 95.f, 175.f };
			return { -150.f, -75.f, 90.f, 165.f };
		case EShotType::Sweep: case EShotType::ReverseSweep: case EShotType::Scoop: return { -130.f, -65.f, 80.f, 150.f };
		case EShotType::SlogSweep:
			if (Style == EBatterStyle::PowerHitter) return { -140.f, -70.f, 90.f, 170.f };
			return { -130.f, -65.f, 80.f, 150.f };
		default: return {};
		}
	}

	FVector Clamp2(const FVector& V, float MinX, float MaxX, float MinY, float MaxY)
	{
		return FVector(FMath::Clamp(V.X, MinX, MaxX), FMath::Clamp(V.Y, MinY, MaxY), V.Z);
	}

	// The body at the moment of contact, from where the ball is met (batter frame).
	FPoseL ContactPose(const FPoseL& Ready, EShotType Shot, EFootwork Foot, float Dir, const FVector& C, EBatterStyle Style = EBatterStyle::Classical)
	{
		FPoseL P = Ready;
		FFootL& Front = P.Foot[0];
		FFootL& Back = P.Foot[1];
		const EFamily Family = FamilyOf(Shot);
		const bool bBack = Foot == EFootwork::Back && Family != EFamily::Sweep;
		if (Family == EFamily::Sweep)
		{
			// Down on the back knee, the front foot a long stride out to the pitch of the ball.
			Front.Ball = FVector2D(FMath::Clamp(C.X - 0.4f, 0.45f, 1.1f), FMath::Clamp(C.Y - 0.42f, -0.1f, 0.4f));
			Front.Toe = 40.f;
			Back.Heel = 80.f;
			Back.Toe = 20.f;
			P.Pelvis = FMath::Lerp(Back.Ball, Front.Ball, 0.45f) + FVector2D(0.f, -0.1f);
			P.Drop = 0.42f;
			P.Hips = 25.f;
			P.Chest = 45.f;
			P.Flex = 32.f;
			P.Side = 5.f;
		}
		else if (bBack)
		{
			// Back and across: the back foot to the line of the ball, the front drawn back beside it.
			// A cut goes across to a wide ball, meeting it beside the body at arm's length rather than behind it.
			if (Style == EBatterStyle::ExpressPuller && Family == EFamily::Pull)
			{
				Back.Ball = FVector2D(FMath::Clamp(C.X - 0.65f, -0.58f, -0.15f), FMath::Clamp(C.Y - 0.32f, -0.05f, 0.4f));
				Back.Toe = 42.f;
				Front.Ball = Back.Ball + FVector2D(0.26f, -0.34f);
				Front.Toe = 68.f;
				P.Hips = 68.f;
				P.Chest = 98.f;
				P.Flex = 8.f;
				P.Side = -6.5f;
			}
			else
			{
				Back.Ball = Family == EFamily::Cut ? FVector2D(FMath::Clamp(C.X - 0.42f, -0.55f, -0.15f), FMath::Clamp(C.Y - 0.45f, 0.f, 0.5f))
					: FVector2D(FMath::Clamp(C.X - 0.62f, -0.55f, -0.15f), FMath::Clamp(C.Y - 0.3f, 0.f, 0.4f));
				Back.Toe = Family == EFamily::Cut ? -15.f : Family == EFamily::Pull ? 40.f : 0.f;
				Front.Ball = Back.Ball + (Family == EFamily::Pull ? FVector2D(0.28f, -0.32f) : FVector2D(0.32f, -0.04f));
				Front.Toe = Family == EFamily::Pull ? 65.f : 15.f;
				switch (Family)
				{
				case EFamily::Pull: P.Hips = 65.f; P.Chest = 95.f; P.Flex = 8.f; P.Side = -6.f; break;
				case EFamily::Cut: P.Hips = 0.f; P.Chest = 5.f; P.Flex = 20.f; P.Side = 4.f; break;
				default: P.Hips = 12.f; P.Chest = 24.f; P.Flex = 14.f; P.Side = 2.f; break;
				}
			}
			P.Pelvis = FMath::Lerp(Back.Ball, Front.Ball, 0.35f) + FVector2D(0.f, -0.08f);
			P.Drop = 0.05f;
			const float OnToes = FMath::Clamp((C.Z - 0.95f) * 60.f, 0.f, 18.f); // up on the toes to a high ball
			Back.Heel = Front.Heel = OnToes;
			P.Drop -= 0.2f * FMath::Sin(FMath::DegreesToRadians(OnToes));
		}
		else
		{
			// Front foot: a stride out to beside the pitch of the ball, the toe opening toward the shot, the weight
			// going through onto it with the head over the ball. Down the track the feet carry the body all the way
			// to the ball, rather than stopping at a stride and leaving the hips to chase it.
			const float Stride = Foot == EFootwork::Advance ? 4.f : 1.25f;
			Front.Ball = FVector2D(FMath::Clamp(C.X - 0.3f, 0.35f, Stride), FMath::Clamp(C.Y - 0.3f, -0.15f, 0.45f));
			Front.Toe = FMath::Clamp(35.f - 0.25f * Dir, 10.f, 55.f);
			// Most of the weight over the front foot: the hips well forward, so the front knee bends over the
			// toes and the back leg trails from a raised heel.
			Back.Toe = 10.f;
			Back.Heel = 40.f;
			P.Pelvis = FMath::Lerp(Back.Ball, Front.Ball, 0.74f) + FVector2D(0.f, -0.1f);
			P.Drop = 0.13f + FMath::Clamp((0.45f - C.Z) * 0.25f, 0.f, 0.1f);
			P.Hips = FMath::Clamp(25.f - 0.2f * Dir, 10.f, 45.f);
			P.Chest = FMath::Clamp(28.f - 0.3f * Dir, 12.f, 60.f);
			P.Flex = FMath::Clamp(28.f + (0.5f - C.Z) * 40.f, 18.f, 45.f);
			P.Side = 6.f;
			if (Family == EFamily::Cut) { P.Chest = 5.f; P.Hips = 5.f; }
			if (Family == EFamily::Pull) { P.Hips = 55.f; P.Chest = 85.f; P.Flex = 10.f; }
			if (Style == EBatterStyle::PowerHitter) { P.Drop += 0.02f; P.Hips += 4.f; }
			// A forward defence meets the ball beside the front pad, under the eyes: the front foot to the ball and
			// the chest bent over the knee, so the hands reach ahead of the ball and the bat angles down from them.
			// The head stays well above the hands (the knee bends, not just the back), leaving the top elbow room to
			// sit high and forward rather than winging out beside a chin-high grip.
			if (Shot == EShotType::Defend)
			{
				Front.Ball.X = FMath::Clamp(C.X - 0.08f, 0.35f, Stride);
				P.Hips = 15.f; P.Chest = 20.f; P.Flex = 24.f; P.Drop += 0.04f; Back.Heel = 30.f;
				P.Pelvis = FMath::Lerp(Back.Ball, Front.Ball, 0.66f) + FVector2D(0.f, -0.1f);
			}
		}
		return P;
	}

	// The follow-through: hips and chest carry on turning, the weight settles, and the back heel comes up.
	FPoseL FinishPose(const FPoseL& C, EShotType Shot, EFootwork Foot, EBatterStyle Style = EBatterStyle::Classical, bool bMiss = false)
	{
		FPoseL P = C;
		if (bMiss)
		{
			// Play-and-miss: checked swing, head tracks ball past edge, slight recoil
			P.Chest += 8.f;
			P.Hips += 4.f;
			P.Flex += 4.f;
			P.Pelvis.X -= 0.02f;
			return P;
		}
		switch (FamilyOf(Shot))
		{
		case EFamily::Pull:
			P.Hips += (Style == EBatterStyle::ExpressPuller ? 35.f : 30.f);
			P.Chest += (Style == EBatterStyle::ExpressPuller ? 50.f : 45.f);
			break;
		case EFamily::Cut: P.Chest += 10.f; break;
		case EFamily::Sweep:
			P.Chest += (Style == EBatterStyle::PowerHitter ? 30.f : 25.f);
			P.Hips += 10.f;
			break;
		default:
			if (Shot == EShotType::Defend) break;
			P.Hips += (Style == EBatterStyle::PowerHitter ? 20.f : 15.f);
			P.Chest += (Shot == EShotType::Punch ? 10.f : Style == EBatterStyle::PowerHitter ? 30.f : 25.f);
			P.Flex -= 8.f;
			if (Foot != EFootwork::Back) { P.Foot[1].Heel = 65.f; P.Foot[1].Toe = 30.f; }
			break;
		}
		return P;
	}

	// Where the hands can be: each hand within the arms' reach of its shoulder.
	float ReachShort(const FBatL& B, const FVector (&Sh)[2])
	{
		const FVector Top = B.Grip - B.Axis() * 0.045f, Bottom = B.Grip + B.Axis() * 0.045f;
		return FMath::Max(FVector::Dist(Top, Sh[0]), FVector::Dist(Bottom, Sh[1])) - Reach;
	}

	// Pushes a bat's grip out from the body: from the spine at the grip's height, which leans forward of the pelvis
	// as the chest bends.
	FBatL OffChest(FBatL B, const FPoseL& P)
	{
		const FVector Up = ChestUp(P);
		const float Along = FMath::Clamp((B.Grip.Z - (HipHeight - P.Drop)) / FMath::Max(Up.Z, 0.3f), 0.f, 0.49f);
		const FVector2D Out = FVector2D(B.Grip) - (P.Pelvis + FVector2D(Up) * Along);
		if (Out.Size() < 0.22f) B.Grip += FVector((Out.IsNearlyZero() ? FVector2D(Facing(P.Chest)) : Out.GetSafeNormal()) * (0.22f - Out.Size()), 0.f);
		return B;
	}

	// Pushes a bat's grip out from the shoulders until neither hand is folded up against its own (a turning chest
	// sweeps a shoulder across the bat's path).
	FBatL Uncramped(FBatL B, const FPoseL& P)
	{
		FVector Sh[2];
		Shoulders(P, Sh);
		for (int32 S = 0; S < 2; ++S)
		{
			const FVector D = B.Grip + B.Axis() * (S == 0 ? -0.045f : 0.045f) - Sh[S];
			if (D.Size() < MinReach) B.Grip += D.GetSafeNormal(UE_SMALL_NUMBER, Facing(P.Chest)) * (MinReach - D.Size());
		}
		return B;
	}

	// Pulls a bat's grip back within reach of the shoulders, then out from them and in front of the chest.
	FBatL Reachable(FBatL B, const FPoseL& P)
	{
		B = OffChest(Uncramped(B, P), P);
		FVector Sh[2];
		Shoulders(P, Sh);
		const float Short = ReachShort(B, Sh);
		if (Short > 0.f) B.Grip += (0.5f * (Sh[0] + Sh[1]) - B.Grip).GetSafeNormal() * Short;
		return B;
	}
}

FBody Plan(const FInput& In)
{
	using namespace CricketPose;
	const float T = In.Time;
	auto Local = [&](const FVector& V) { return FVector(V.X, V.Y * In.Off, V.Z); }; // a mirror: its own inverse

	// Stance: side-on, feet a little wider than the hips, knees flexed, the bat grounded by the back toe and the
	// hands resting by the front thigh. Backlift as the bowler gathers; the trigger (back foot back and across, a
	// small press forward) as the ball is released.
	FPoseL Stance;
	if (In.Style == EBatterStyle::ExpressPuller)
	{
		Stance.Foot[0] = { FVector2D(0.24f, 0.06f), 18.f, 0.f };
		Stance.Foot[1] = { FVector2D(-0.24f, 0.08f), 0.f, 0.f };
		Stance.Pelvis = FVector2D(-0.01f, -0.04f);
		Stance.Drop = 0.065f;
		Stance.Hips = 12.f;
		Stance.Chest = 24.f;
		Stance.Flex = 15.f;
	}
	else if (In.Style == EBatterStyle::Unorthodox)
	{
		Stance.Foot[0] = { FVector2D(0.27f, 0.06f), 20.f, 0.f };
		Stance.Foot[1] = { FVector2D(-0.27f, 0.06f), 0.f, 0.f };
		Stance.Pelvis = FVector2D(-0.02f, -0.04f);
		Stance.Drop = 0.085f;
		Stance.Hips = 12.f;
		Stance.Chest = 25.f;
		Stance.Flex = 18.f;
	}
	else if (In.Style == EBatterStyle::PowerHitter)
	{
		Stance.Foot[0] = { FVector2D(0.26f, 0.05f), 16.f, 0.f };
		Stance.Foot[1] = { FVector2D(-0.26f, 0.06f), 0.f, 0.f };
		Stance.Pelvis = FVector2D(-0.01f, -0.05f);
		Stance.Drop = 0.09f;
		Stance.Hips = 10.f;
		Stance.Chest = 22.f;
		Stance.Flex = 19.f;
	}

	FPoseL Lifted = Stance;
	Lifted.Chest = In.Style == EBatterStyle::ExpressPuller ? 19.f : In.Style == EBatterStyle::Unorthodox ? 20.f : 17.f;
	Lifted.Drop = In.Style == EBatterStyle::PowerHitter ? 0.10f : 0.08f;
	Lifted.Flex = In.Style == EBatterStyle::ExpressPuller ? 16.f : 17.f;
	FPoseL Ready = Lifted;
	if (In.Style == EBatterStyle::Unorthodox)
	{
		// Steve Smith: back foot shuffles across outside off-stump, chest open
		Ready.Foot[1] = { FVector2D(-0.25f, 0.20f), 5.f, 0.f };
		Ready.Foot[0] = { FVector2D(0.28f, 0.14f), 20.f, 0.f };
		Ready.Pelvis = FVector2D(-0.01f, 0.05f);
		Ready.Drop = 0.09f;
		Ready.Hips = 14.f;
		Ready.Chest = 26.f;
		Ready.Flex = 18.f;
	}
	else if (In.Style == EBatterStyle::ExpressPuller)
	{
		// Rohit Sharma: back-and-across shuffle
		Ready.Foot[1] = { FVector2D(-0.29f, 0.14f), 0.f, 0.f };
		Ready.Foot[0] = { FVector2D(0.26f, 0.09f), 18.f, 0.f };
		Ready.Pelvis = FVector2D(-0.04f, 0.01f);
		Ready.Drop = 0.085f;
		Ready.Hips = 10.f;
		Ready.Flex = 17.f;
	}
	else if (In.Style == EBatterStyle::PowerHitter)
	{
		Ready.Foot[1] = { FVector2D(-0.28f, 0.12f), 0.f, 0.f };
		Ready.Foot[0] = { FVector2D(0.28f, 0.09f), 18.f, 0.f };
		Ready.Pelvis = FVector2D(-0.02f, -0.02f);
		Ready.Drop = 0.10f;
		Ready.Hips = 9.f;
		Ready.Flex = 19.f;
	}
	else
	{
		Ready.Foot[1] = { FVector2D(-0.28f, 0.13f), 0.f, 0.f };
		Ready.Foot[0] = { FVector2D(0.27f, 0.1f), 18.f, 0.f };
		Ready.Pelvis = FVector2D(-0.03f, -0.01f);
		Ready.Drop = 0.09f;
		Ready.Hips = 8.f;
		Ready.Flex = 18.f;
	}

	TArray<FPhase, TInlineAllocator<5>> Phases;
	Phases.Add({ Lifted, -0.6f, -0.05f, -0.6f, -0.05f, -0.6f, -0.05f, -0.6f, -0.05f, 0, 0 });
	const float TriggerEnd = FMath::Min(0.1f, In.bStroke || In.bLeave ? In.Press : 0.1f);
	Phases.Add({ Ready, -0.15f, TriggerEnd, -0.15f, TriggerEnd, -0.15f, TriggerEnd, -0.15f, TriggerEnd, 2, 1, 0.03f });

	const FVector C = Local(In.Contact - In.Home);
	const FVector Shot = Local(In.ShotDir).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const float Press = FMath::Max(In.Press, TriggerEnd), Impact = FMath::Max(In.Impact, Press + 0.1f), Dur = Impact - Press;
	const bool bAdvance = In.Foot == EFootwork::Advance;
	const bool bBackFoot = In.Foot == EFootwork::Back && FamilyOf(In.Shot) != EFamily::Sweep;
	const float Plant = Press + (bAdvance ? 0.8f : bBackFoot ? 0.45f : 0.55f) * Dur;
	FPoseL AtContact = Ready, Finish = Ready;
	FBatL Contact;
	int32 Strides = 4; // down the track and back
	if (In.bStroke)
	{
		AtContact = ContactPose(Ready, In.Shot, In.Foot, In.DirectionDeg, C, In.Style);
		FPhase Stroke;
		Stroke.To = AtContact;
		Stroke.Weight0 = Press;
		Stroke.Weight1 = Plant + 0.3f * (Impact - Plant);
		Stroke.Hips0 = FMath::Max(Press, Plant - 0.06f);
		Stroke.Hips1 = Impact + 0.02f;
		Stroke.Chest0 = FMath::Max(Press, Plant - 0.02f);
		Stroke.Chest1 = Impact + 0.07f;
		Stroke.Feet0 = Press;
		Stroke.Feet1 = bAdvance ? Impact - 0.04f : bBackFoot ? Impact - 0.02f : Plant;
		// Down the track in chassé steps of at most 0.55 m a foot, so the legs never splay and the hips barely dip.
		const float Travel = FVector2D::Distance(Ready.Foot[0].Ball, AtContact.Foot[0].Ball);
		Strides = FMath::Clamp(2 * FMath::CeilToInt(Travel / 0.55f), 4, 8);
		Stroke.Steps = bAdvance ? Strides : bBackFoot ? 2 : 1;
		Stroke.First = bBackFoot ? 1 : 0;
		Stroke.Height = bBackFoot ? 0.04f : bAdvance ? 0.1f : 0.07f;
		if (bAdvance)
		{
			// Down the track: the back foot finishes a stride behind the front one.
			Stroke.To.Foot[1].Ball = Stroke.To.Foot[0].Ball - FVector2D(0.55f, -0.02f);
			Stroke.To.Foot[1].Heel = 20.f;
			Stroke.To.Pelvis = FMath::Lerp(Stroke.To.Foot[1].Ball, Stroke.To.Foot[0].Ball, 0.6f) + FVector2D(0.f, -0.1f);
		}
		Phases.Add(Stroke);
		// The bat at contact: the sweet spot on the ball, the face along the shot. A straight bat hangs from hands
		// ahead of the ball; a cross bat lies square to the shot, away from the body. If that puts the hands out of
		// reach the bat angles toward the shoulders, and failing that the body leans in. The reach is the body's at
		// the contact itself, the chest still turning through it.
		const EFamily Family = FamilyOf(In.Shot);
		FVector Want;
		if (Family == EFamily::Vertical)
			Want = FVector(In.Shot == EShotType::Defend ? -0.6f : In.Shot == EShotType::Loft ? -0.12f : -0.28f, 0.05f, -1.f);
		else
		{
			const FVector2D S = FVector2D(Shot).GetSafeNormal();
			const FVector2D Square = FVector2D(-S.Y, S.X).X >= 0.f ? FVector2D(-S.Y, S.X) : FVector2D(S.Y, -S.X);
			Want = FVector(Square, Family == EFamily::Cut ? -0.45f : Family == EFamily::Sweep ? -0.3f : -0.15f);
		}
		Want = Want.GetSafeNormal();
		FPoseL& Lean = Phases.Last().To;
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			float NoLift[2];
			FVector Sh[2];
			Shoulders(Evaluate(Stance, Phases, Impact, NoLift), Sh);
			const FVector Toward = (C - 0.5f * (Sh[0] + Sh[1])).GetSafeNormal();
			auto BatWith = [&](float A)
			{
				const FVector Axis = FMath::Lerp(Want, Toward, A).GetSafeNormal();
				return Bat(C - Axis * SweetFromGrip, Axis, Shot);
			};
			float Lo = 0.f, Hi = 1.f;
			if (ReachShort(BatWith(0.f), Sh) > 0.f)
				for (int32 I = 0; I < 12; ++I) (ReachShort(BatWith(0.5f * (Lo + Hi)), Sh) > 0.f ? Lo : Hi) = 0.5f * (Lo + Hi);
			else Hi = 0.f;
			Contact = BatWith(Hi);
			const float Short = ReachShort(Contact, Sh);
			// A ball into the body (a hook at the shoulder) cramps the arms instead: the body makes room, back from it.
			const float Cramped = MinReach - FMath::Min(FVector::Dist(Contact.Grip - Contact.Axis() * 0.045f, Sh[0]), FVector::Dist(Contact.Grip + Contact.Axis() * 0.045f, Sh[1]));
			if (Short <= 0.f && Cramped <= 0.f) break;
			const FVector2D ToBall = (FVector2D(C) - Lean.Pelvis).GetSafeNormal();
			if (Short > 0.f)
			{
				// The hips lean in but stay behind the front foot, the chest bending the rest: hips past the front
				// foot leave the legs trailing and the knees sunk to the turf.
				Lean.Pelvis += ToBall * FMath::Min(Short, 0.15f);
				Lean.Pelvis.X = FMath::Min(Lean.Pelvis.X, Lean.Foot[0].Ball.X - 0.1f);
				Lean.Flex = FMath::Min(Lean.Flex + Short * 60.f, 50.f);
			}
			else Lean.Pelvis -= ToBall * FMath::Min(Cramped + 0.01f, 0.15f);
		}
		AtContact = Lean;
		Finish = FinishPose(AtContact, In.Shot, In.Foot, In.Style, In.bMiss);
		Phases.Add({ Finish, Impact, Impact + 0.45f, Impact, Impact + 0.4f, Impact + 0.02f, Impact + 0.5f, Impact - 0.02f, Impact + 0.45f, 0, 0 });
	}
	else if (In.bLeave)
	{
		// Leaving: a small press forward and the bat up out of the way.
		Finish = Ready;
		Finish.Foot[0].Ball.X += 0.12f;
		Finish.Pelvis.X += 0.05f;
		Finish.Chest = 30.f;
		Phases.Add({ Finish, In.Press, In.Press + 0.3f, In.Press, In.Press + 0.3f, In.Press, In.Press + 0.3f, In.Press, In.Press + 0.25f, 1, 0, 0.04f });
	}
	// Recover: step back into the stance (walking back up the pitch from down the track).
	const float Settle = FMath::Max(In.Settle, In.bStroke ? Impact + 0.5f : Press);
	Phases.Add({ Stance, Settle, Settle + RecoverSeconds, Settle, Settle + RecoverSeconds, Settle, Settle + RecoverSeconds, Settle,
		Settle + RecoverSeconds, bAdvance && In.bStroke ? Strides : 2, 0, 0.05f });

	float Lift[2];
	// Charging, the hips go with the feet's shuffle, easing from the stance's place between them to the stroke's,
	// rather than surging out over the front foot and dipping as the back one catches up.
	auto Between = [](const FPoseL& At) { return At.Pelvis.X - 0.5f * (At.Foot[0].Ball.X + At.Foot[1].Ball.X); };
	const bool bCharge = bAdvance && In.bStroke;
	auto Charged = [&](float At, float (&L)[2])
	{
		const float Ride = bCharge ? FMath::SmoothStep(Press, Press + 0.08f, At) * (1.f - FMath::SmoothStep(Impact - 0.12f, Impact, At)) : 0.f;
		return Evaluate(Stance, Phases, At, L, Ride, FMath::Lerp(Between(Ready), Between(AtContact), FMath::SmoothStep(Press, Impact, At)));
	};
	FPoseL P = Charged(T, Lift);
	// Life in the stance: breathing and a slow sway of the weight, gone once the bowler gathers.
	const float Still = T < -0.6f ? 1.f - FMath::SmoothStep(-1.2f, -0.6f, T) : FMath::SmoothStep(Settle + RecoverSeconds, Settle + RecoverSeconds + 0.5f, T);
	P.Flex += 1.2f * Still * FMath::Sin(In.Clock * 2.f * PI * 0.23f);
	P.Pelvis.Y += 0.01f * Still * FMath::Sin(In.Clock * 2.f * PI * 0.09f);

	// The bat: grounded in the stance, up in the backlift, then the stroke through keys built from the contact.
	const FBatL Grounded = Bat(FVector(P.Pelvis, -P.Drop) + FVector(0.11f, 0.24f, 0.87f), FVector(-0.15f, 0.18f, -1.f), FVector::ForwardVector);
	auto Up = [&](const FPoseL& At)
	{
		if (In.Style == EBatterStyle::Unorthodox)
			return Bat(FVector(At.Pelvis, -At.Drop) + FVector(-0.06f, 0.18f, 1.25f), FVector(-0.22f, 0.22f, 0.95f), FVector(-0.2f, 0.95f, -0.15f));
		if (In.Style == EBatterStyle::ExpressPuller)
			return Bat(FVector(At.Pelvis, -At.Drop) + FVector(-0.12f, 0.22f, 1.08f), FVector(-0.48f, 0.42f, 0.77f), FVector(-0.35f, 0.88f, -0.2f));
		if (In.Style == EBatterStyle::PowerHitter)
			return Bat(FVector(At.Pelvis, -At.Drop) + FVector(-0.08f, 0.21f, 1.15f), FVector(-0.42f, 0.38f, 0.82f), FVector(-0.28f, 0.92f, -0.22f));
		return Bat(FVector(At.Pelvis, -At.Drop) + FVector(-0.1f, 0.19f, 1.1f), FVector(-0.45f, 0.4f, 0.8f), FVector(-0.3f, 0.9f, -0.2f));
	};
	FBatL B = Lerp(Grounded, Up(P), Ease(-0.6f, -0.05f, T));
	if (In.bStroke && T > Press)
	{
		float NoLift[2];
		auto BodyAt = [&](float At) { return Charged(At, NoLift); };
		const FArc Arc = ArcOf(In.Shot, In.Style, In.bMiss);
		// The swing's axis: square to the bat and to its path through the ball, turning so the bat runs along the shot.
		const FVector Along = Contact.Face();
		FVector Spin = FVector::CrossProduct(Contact.Axis(), Along).GetSafeNormal();
		if ((FQuat(Spin, FMath::DegreesToRadians(-5.f)).RotateVector(Contact.Axis()) | Along) > 0.f) Spin = -Spin; // back in time, away from the shot
		auto Turned = [&](float Deg, const FVector& Grip)
		{
			return FBatL{ Grip, (FQuat(Spin, FMath::DegreesToRadians(Deg)) * Contact.Rot).GetNormalized() };
		};
		const float TTop = Press + 0.35f * Dur, TLoop = TTop + 0.55f * (Impact - TTop), TThrough = Impact + 0.12f, TFinish = Impact + 0.5f;
		const EFamily Family = FamilyOf(In.Shot);
		const FPoseL AtTop = BodyAt(TTop), AtLoop = BodyAt(TLoop), AtThrough = BodyAt(TThrough), AtFinish = BodyAt(TFinish);
		auto Rel = [](const FPoseL& At, float F, float S, float U) { return FVector(At.Pelvis, -At.Drop) + FVector(F, S, U); };
		FVector TopGrip = Rel(AtTop, -0.16f, 0.17f, 1.08f), LoopGrip = Rel(AtLoop, -0.05f, 0.22f, 0.95f);
		if (Family == EFamily::Pull) { TopGrip = Rel(AtTop, -0.18f, 0.18f, 1.18f); LoopGrip = Rel(AtLoop, -0.05f, 0.22f, 1.05f); }
		if (Family == EFamily::Cut) { TopGrip = Rel(AtTop, -0.1f, 0.25f, 1.28f); LoopGrip = Rel(AtLoop, 0.f, 0.33f, 1.12f); }
		if (Family == EFamily::Sweep) { TopGrip = Rel(AtTop, -0.1f, 0.18f, 1.f); LoopGrip = Rel(AtLoop, 0.02f, 0.28f, 0.75f); }
		if (In.Shot == EShotType::Defend) { TopGrip = Rel(AtTop, -0.06f, 0.2f, 1.f); LoopGrip = FMath::Lerp(TopGrip, Contact.Grip, 0.5f); }
		const FVector ShotFlat = FVector(FVector2D(Shot).GetSafeNormal(), 0.f);
		FVector ThroughGrip = Contact.Grip + ShotFlat * 0.25f + FVector(0.f, 0.f, 0.35f);
		FVector FinishSh[2];
		Shoulders(AtFinish, FinishSh);
		// A drive finishes high: the hands up beside the head over the front shoulder (0.4 m above it, higher on a
		// loft), just outside it rather than in front of the face, the elbows forward under them.
		const FVector FrontOut = FVector(FVector2D(FinishSh[0] - FinishSh[1]).GetSafeNormal(), 0.f);
		FVector FinishGrip = FinishSh[0] + FrontOut * 0.05f + Facing(AtFinish.Chest) * 0.05f + FVector(0.f, 0.f, In.Shot == EShotType::Loft ? 0.46f : 0.4f);
		if (In.bMiss)
		{
			ThroughGrip = Contact.Grip + ShotFlat * 0.14f + Facing(AtThrough.Chest) * 0.05f + FVector(0.f, 0.f, 0.12f);
			FinishGrip = Contact.Grip + ShotFlat * 0.18f + Facing(AtFinish.Chest) * 0.08f + FVector(0.f, 0.f, 0.18f);
		}
		else
		{
			switch (Family)
			{
			case EFamily::Pull:
			{
				// The hands come round in front of the chest at the contact's height, then finish by the front shoulder:
				// ahead of the turning chest all the way, never through the shoulder it sweeps round.
				FVector ThroughSh[2];
				Shoulders(AtThrough, ThroughSh);
				ThroughGrip = FVector(FVector2D(0.5f * (ThroughSh[0] + ThroughSh[1]) + Facing(AtThrough.Chest) * 0.42f), Contact.Grip.Z + 0.08f);
				FinishGrip = FinishSh[0] + Facing(AtFinish.Chest) * (In.Style == EBatterStyle::ExpressPuller ? 0.34f : 0.3f);
				break;
			}
			case EFamily::Cut: ThroughGrip = Contact.Grip + FVector(0.05f, 0.05f, -0.12f); FinishGrip = Contact.Grip + FVector(0.12f, -0.05f, -0.2f); break;
			case EFamily::Sweep:
				ThroughGrip = Contact.Grip + FVector(0.05f, -0.3f, 0.1f);
				FinishGrip = FinishSh[0] + Facing(AtFinish.Chest) * 0.25f;
				// A slog sweep's arms swing on up to a high finish, long, the hands up and forward over the front shoulder,
				// rather than rolling round at chest height.
				if (In.Shot == EShotType::SlogSweep) FinishGrip += FVector(0.f, 0.f, 0.3f) - Facing(AtFinish.Chest) * 0.05f;
				break;
			default:
				if (In.Shot == EShotType::Defend) ThroughGrip = FinishGrip = Contact.Grip + FVector(0.02f, 0.f, 0.02f);
				else if (In.Shot == EShotType::Punch) FinishGrip = Contact.Grip + ShotFlat * 0.15f + FVector(0.f, 0.f, 0.3f);
				else if (In.Style == EBatterStyle::Classical)
				{
					FinishGrip = FinishSh[0] + FrontOut * 0.06f + Facing(AtFinish.Chest) * 0.05f + FVector(0.f, 0.f, In.Shot == EShotType::Loft ? 0.48f : 0.42f);
				}
				break;
			}
		}
		const FKey Keys[] = {
			{ Press, Up(BodyAt(Press)) },
			{ TTop, Reachable(Family == EFamily::Vertical ? Turned(Arc.Top, TopGrip) : Up(AtTop), AtTop) },
			{ TLoop, Reachable(Turned(Arc.Loop, LoopGrip), AtLoop) },
			{ Impact, Contact },
			{ TThrough, Reachable(Turned(Arc.Through, ThroughGrip), AtThrough) },
			{ TFinish, Reachable(Turned(Arc.Finish, FinishGrip), AtFinish) },
		};
		// Only the keys are checked for reach; between them the spline can bow out of it, so after the contact
		// (which must stay on the ball) the hands are kept within reach every frame, and never folded up.
		B = Spline(Keys, T);
		if (bAdvance && T < Impact)
		{
			// Down the track the body covers metres between keys: the hands ride with it, keyed from the pelvis,
			// rather than trailing on a path drawn through where it was.
			auto Anchor = [](const FPoseL& At) { return FVector(At.Pelvis, 0.f); }; // not the drop: the hands ride over each stride's dip
			FKey Carried[UE_ARRAY_COUNT(Keys)];
			for (int32 K = 0; K < UE_ARRAY_COUNT(Keys); ++K)
			{
				Carried[K] = Keys[K];
				Carried[K].Bat.Grip -= Anchor(BodyAt(Keys[K].T));
			}
			B = Spline(Carried, T);
			B.Grip += Anchor(P);
			B = Reachable(B, P); // within reach between the keys, and never through the chest as the body closes on the hands
		}
		B = T > Impact ? Reachable(B, P) : Uncramped(B, P); // the contact is never cramped: no push there
		// Recovering, the hands carry the finished bat with the body as it walks back into the stance, rather than
		// leaving it where the stroke ended while the body steps away from it.
		if (T > Settle)
		{
			FBatL Carried = Keys[5].Bat;
			Carried.Grip += FVector(P.Pelvis - AtFinish.Pelvis, AtFinish.Drop - P.Drop);
			// From a high finish the straight way down runs through the front shoulder, so the hands come down in an
			// arc out in front of the chest.
			const float Down = Ease(Settle, Settle + RecoverSeconds, T);
			B = Lerp(Carried, Grounded, Down);
			B.Grip += Facing(P.Chest) * (0.3f * FMath::Sin(PI * Down));
			B = Reachable(B, P);
		}
	}
	else if (In.bLeave && T > In.Press)
	{
		const FBatL High = Bat(FVector(Finish.Pelvis, -Finish.Drop) + FVector(-0.05f, 0.08f, 1.35f), FVector(-0.3f, 0.25f, 0.92f), FVector(0.f, 1.f, 0.f));
		B = Lerp(Lerp(B, High, Ease(In.Press, In.Press + 0.3f, T)), Grounded, Ease(Settle, Settle + RecoverSeconds, T));
	}
	else if (T > Settle) B = Lerp(Up(Ready), Grounded, Ease(Settle, Settle + RecoverSeconds, T));

	// Into the simulation frame: mirrored for a left-hander, whose top hand and front foot are the right ones.
	FBody Out;
	const int32 Front = In.Off > 0.f ? 0 : 1;
	Out.TopHand = Front;
	Out.Pelvis = In.Home + Local(FVector(P.Pelvis, 0.f));
	Out.Drop = P.Drop;
	Out.Hips = Local(Facing(P.Hips));
	Out.Chest = Local(Facing(P.Chest));
	Out.ChestUp = Local(ChestUp(P));
	for (int32 F = 0; F < 2; ++F)
	{
		FFoot& Foot = Out.Foot[F == 0 ? Front : 1 - Front];
		Foot.Ball = In.Home + Local(FVector(P.Foot[F].Ball, 0.f));
		Foot.Toe = Local(Facing(P.Foot[F].Toe));
		Foot.Heel = P.Foot[F].Heel;
		Foot.Lift = Lift[F];
	}
	Out.Bat.Grip = In.Home + Local(B.Grip);
	Out.Bat.Axis = Local(B.Axis());
	Out.Bat.Face = Local(B.Face());
	FVector Sh[2];
	Shoulders(P, Sh);
	Out.Shoulder[Front] = In.Home + Local(Sh[0]);
	Out.Shoulder[1 - Front] = In.Home + Local(Sh[1]);
	return Out;
}
}
