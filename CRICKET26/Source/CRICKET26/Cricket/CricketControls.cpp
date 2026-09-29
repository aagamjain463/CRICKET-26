// Control layer: player input -> intent data. Everything here only produces data (a shot's
// direction/power/press time, a delivery plan's target/effort/movement, run calls) which the
// simulation turns into outcomes; nothing here scores, moves a player or picks an animation.

#include "CricketControls.h"
#include "DeliveryResolver.h"

namespace CricketControl
{
float PullMagnitude(const FVector2D& Pull, const FCricketControlTuning& T)
{
	const float Len = Pull.Size() * FMath::Max(T.PullSensitivity, 0.01f);
	if (Len <= T.PullDeadZone) return 0.f;
	const float Span = FMath::Max(T.PullMax - T.PullDeadZone, 1e-4f);
	return FMath::Clamp((Len - T.PullDeadZone) / Span, 0.f, 1.f);
}

float ShapeMagnitude(float Magnitude)
{
	const float M = FMath::Clamp(Magnitude, 0.f, 1.f);
	// Gentle power curve: fine control over short pulls, the top end reachable without a full stretch,
	// so the pull feels smooth and immediate rather than stiff or laggy.
	return FMath::Pow(M, 1.3f);
}

float AimFromPull(const FVector2D& Pull, ECricketHand BatHand, const FCricketControlTuning& T)
{
	if (Pull.Size() <= T.PullDeadZone) return 0.f;
	// The ball goes the way the finger pulls: down the screen is straight (0 deg).
	const float RawDeg = FMath::RadiansToDegrees(FMath::Atan2(Pull.X, Pull.Y));
	const float Off = OffSideSign(BatHand);
	const float Dir = -RawDeg * Off * T.DirectionSensitivity;
	return FMath::Clamp(Dir, -T.MaxAimDeg, T.MaxAimDeg);
}

FShotAim Aim(const FVector2D& Pull, ECricketHand BatHand, const FCricketControlTuning& T)
{
	FShotAim A;
	A.Magnitude = PullMagnitude(Pull, T);
	A.DirectionDeg = AimFromPull(Pull, BatHand, T);
	A.Power = T.MinPower + (T.MaxPower - T.MinPower) * ShapeMagnitude(A.Magnitude);
	A.bCommitted = A.Magnitude >= T.MinCommit;
	return A;
}

FBatInput ShotFor(const FShotAim& Aim, EBatIntent Mode, float PressTime)
{
	FBatInput In;
	if (Mode == EBatIntent::Leave)
		return In; // not playing: leave
	if (!Aim.bCommitted)
	{
		// A tap, or a pull too short to commit, is a defensive push, never a full stroke.
		In.Intent = EBatIntent::Defend;
		In.DirectionDeg = 0.f;
		In.PressTime = PressTime;
		In.Power = 0.3f;
		return In;
	}
	In.Intent = Mode;
	In.DirectionDeg = Mode == EBatIntent::Defend ? 0.f : Aim.DirectionDeg;
	In.PressTime = PressTime;
	In.Power = Mode == EBatIntent::Defend ? FMath::Min(Aim.Power, 0.5f) : Aim.Power;
	return In;
}

const TCHAR* ZoneName(float DirectionDeg)
{
	const float A = FMath::Abs(DirectionDeg);
	const bool bOff = DirectionDeg >= 0.f;
	if (A < 12.f) return TEXT("Straight");
	if (A < 32.f) return bOff ? TEXT("Mid-off") : TEXT("Mid-on");
	if (A < 55.f) return bOff ? TEXT("Extra cover") : TEXT("Midwicket");
	if (A < 80.f) return bOff ? TEXT("Cover") : TEXT("Square leg");
	if (A < 105.f) return bOff ? TEXT("Point") : TEXT("Square leg");
	if (A < 130.f) return bOff ? TEXT("Third man") : TEXT("Fine leg");
	return bOff ? TEXT("Third man") : TEXT("Fine leg");
}

const TCHAR* PowerBandName(float Magnitude)
{
	if (Magnitude < 0.06f) return TEXT("DEFENSIVE");
	if (Magnitude < 0.3f) return TEXT("SOFT");
	if (Magnitude < 0.55f) return TEXT("CONTROLLED");
	if (Magnitude < 0.8f) return TEXT("FIRM");
	return TEXT("FULL");
}

ETimingGrade GradeShot(const FDeliveryResult& Result, const FBatInput& Input, const FCricketControlTuning& T)
{
	if (!Input.IsShot()) return ETimingGrade::None;
	if (Result.bTooLate) return ETimingGrade::Miss;
	if (!Result.Contact.HasContact()) return ETimingGrade::Miss;
	const float Err = Result.Contact.TimingError; // + late, from the simulated ball vs the bat
	const float A = FMath::Abs(Err);
	if (A <= T.PerfectTiming) return ETimingGrade::Perfect;
	if (A <= T.GoodTiming) return ETimingGrade::Good;
	if (A <= T.EarlyLateTiming) return Err < 0.f ? ETimingGrade::Early : ETimingGrade::Late;
	return Err < 0.f ? ETimingGrade::VeryEarly : ETimingGrade::VeryLate;
}

const TCHAR* TimingGradeName(ETimingGrade Grade)
{
	switch (Grade)
	{
	case ETimingGrade::VeryEarly: return TEXT("VERY EARLY");
	case ETimingGrade::Early: return TEXT("EARLY");
	case ETimingGrade::Good: return TEXT("GOOD");
	case ETimingGrade::Perfect: return TEXT("PERFECT");
	case ETimingGrade::Late: return TEXT("LATE");
	case ETimingGrade::VeryLate: return TEXT("VERY LATE");
	case ETimingGrade::Miss: return TEXT("MISS");
	default: return TEXT("-");
	}
}

EReleaseGrade GradeRelease(float Meter, const FCricketControlTuning& T)
{
	if (Meter > 0.85f) return EReleaseGrade::NoBall; // the simulation's overstep rule
	const float A = FMath::Abs(Meter);
	if (A <= T.PerfectRelease) return EReleaseGrade::Perfect;
	if (A <= T.GoodRelease) return EReleaseGrade::Good;
	if (A <= T.EarlyLateRelease) return Meter < 0.f ? EReleaseGrade::Early : EReleaseGrade::Late;
	return Meter < 0.f ? EReleaseGrade::VeryEarly : EReleaseGrade::VeryLate;
}

const TCHAR* ReleaseGradeName(EReleaseGrade Grade)
{
	switch (Grade)
	{
	case EReleaseGrade::VeryEarly: return TEXT("VERY EARLY");
	case EReleaseGrade::Early: return TEXT("EARLY");
	case EReleaseGrade::Good: return TEXT("GOOD");
	case EReleaseGrade::Perfect: return TEXT("PERFECT");
	case EReleaseGrade::Late: return TEXT("LATE");
	case EReleaseGrade::VeryLate: return TEXT("VERY LATE");
	case EReleaseGrade::NoBall: return TEXT("NO BALL");
	default: return TEXT("-");
	}
}

float PitchMarkerAlpha(const FCricketControlTuning& T, int32 Difficulty, float Since, float PitchAt)
{
	const int32 D = FMath::Clamp(Difficulty, 0, 3);
	if (PitchAt <= 0.f || T.PitchMarkerRadius[D] <= 0.f) return 0.f;
	const float Fade = FMath::Max(T.PitchMarkerFade, 1e-3f);
	const float In = FMath::Clamp((Since - T.PitchMarkerDelay[D]) / Fade, 0.f, 1.f);
	const float Out = FMath::Clamp(1.f - (Since - PitchAt) / Fade, 0.f, 1.f);
	return FMath::Min(In, Out);
}

void DragTarget(FDeliveryPlan& Plan, const FVector2D& Drag, ECricketHand BatHand, const FCricketControlTuning& T)
{
	// The target follows the finger: across the screen is line, up the screen (toward the batter) is fuller.
	// The camera looks down the pitch from behind the bowler: screen left is world +Y,
	// which is the off side for a right-hander, mirrored for a left-hander.
	const float Off = OffSideSign(BatHand);
	Plan.Line += -Drag.X * T.LineDragScale * Off;
	Plan.Length += Drag.Y * T.LengthDragScale;
	ClampTarget(Plan);
}

void ClampTarget(FDeliveryPlan& Plan)
{
	Plan.Line = FMath::Clamp(Plan.Line, -1.f, 1.6f);
	Plan.Length = FMath::Clamp(Plan.Length, 0.5f, 13.f);
	Plan.Effort = FMath::Clamp(Plan.Effort, 0.f, 1.f);
	Plan.Movement = FMath::Clamp(Plan.Movement, 0.f, 1.f);
}

bool IsSwingFamily(EDeliveryType Type)
{
	return Type == EDeliveryType::Stock || Type == EDeliveryType::Outswing || Type == EDeliveryType::Inswing;
}

float DialFor(const FDeliveryPlan& Plan, ECricketHand BatHand)
{
	if (IsSwingFamily(Plan.Type))
	{
		if (Plan.Type == EDeliveryType::Stock) return 0.f;
		// Signed on screen: + is the ball moving to the right of the screen.
		// Screen right is leg side for a right-hander (inswing) and off side for a left-hander (outswing).
		const bool bOut = Plan.Type == EDeliveryType::Outswing;
		const float Sign = BatHand == ECricketHand::Right ? (bOut ? -1.f : 1.f) : (bOut ? 1.f : -1.f);
		return Sign * FMath::Clamp(Plan.Movement, 0.f, 1.f);
	}
	// Seam, cutters, slower balls and all spin: the amount, -1 for none.
	return -1.f + 2.f * FMath::Clamp(Plan.Movement, 0.f, 1.f);
}

void ApplyDial(FDeliveryPlan& Plan, float Dial, ECricketHand BatHand, const FCricketControlTuning& T)
{
	const float D = FMath::Clamp(Dial, -1.f, 1.f);
	if (IsSwingFamily(Plan.Type))
	{
		if (FMath::Abs(D) <= T.DialStockBand)
		{
			Plan.Type = EDeliveryType::Stock;
			Plan.Movement = 1.f;
			return;
		}
		// Screen right (+) picks the delivery that moves the ball to the screen's right.
		const bool bPositive = D > 0.f;
		if (BatHand == ECricketHand::Right)
			Plan.Type = bPositive ? EDeliveryType::Inswing : EDeliveryType::Outswing;
		else
			Plan.Type = bPositive ? EDeliveryType::Outswing : EDeliveryType::Inswing;
		Plan.Movement = FMath::Clamp(FMath::Abs(D), 0.f, 1.f);
		return;
	}
	Plan.Movement = FMath::Clamp(0.5f * (D + 1.f), 0.f, 1.f);
}

bool CallRun(FRunCalls& Calls, const FRunningOutcome& Run, float Post, const FCricketControlTuning& T)
{
	(void)Run;
	if (Calls.Go.Num() >= T.MaxRuns) return false;
	Calls.bManual = true;
	Calls.Go.Add(Post); // pre-contact calls stay negative and count as "at contact" in the simulation
	return true;
}

ECancelResult CancelRun(FRunCalls& Calls, const FRunningOutcome& Run, float Post)
{
	if (Calls.Go.Num() == 0 || Calls.Back >= 0.f) return ECancelResult::Nothing;
	// Drop the last call when the batters have not set off on it yet; else ask them to turn back.
	// The simulation refuses the turn-back past TurnBackLimit of the leg.
	if (Run.Attempted == 0 || !Run.Leaves.IsValidIndex(Run.Attempted - 1) || Post < Run.Leaves.Last())
	{
		Calls.Go.Pop();
		if (Calls.Go.Num() == 0) Calls.bManual = false;
		return ECancelResult::Dropped;
	}
	Calls.Back = Post;
	return ECancelResult::TurnBack;
}

ERunState RunState(const FRunningOutcome& Run, const FRunCalls& Calls, float Post)
{
	if (Run.bSentBack) return ERunState::Returning;
	if (Calls.Back >= 0.f) return ERunState::Turning;
	if (Run.Attempted == 0)
		return Calls.Go.Num() == 0 ? ERunState::AtCrease : ERunState::RunRequested;
	if (Run.Completed < Run.Attempted)
	{
		if (Calls.Go.Num() > Run.Attempted) return ERunState::NextRunRequested;
		// Mid-leg past the first turn reads as crossing; closing in reads as approaching.
		if (Run.RunTimes.IsValidIndex(Run.Completed) && Post > Run.RunTimes[Run.Completed] - 0.35f)
			return ERunState::ApproachingCrease;
		if (Run.Completed >= 1) return ERunState::Crossing;
		return ERunState::Running;
	}
	if (Calls.Go.Num() > Run.Completed) return ERunState::NextRunRequested;
	if (Post > Run.EndTime() + 1.5f) return ERunState::BallDead;
	return ERunState::RunComplete;
}

const TCHAR* RunStateName(ERunState State)
{
	switch (State)
	{
	case ERunState::AtCrease: return TEXT("AT CREASE");
	case ERunState::RunRequested: return TEXT("RUN REQUESTED");
	case ERunState::Running: return TEXT("RUNNING");
	case ERunState::Crossing: return TEXT("CROSSING");
	case ERunState::ApproachingCrease: return TEXT("APPROACHING");
	case ERunState::RunComplete: return TEXT("RUN COMPLETE");
	case ERunState::NextRunRequested: return TEXT("ANOTHER");
	case ERunState::Turning: return TEXT("TURNING");
	case ERunState::Returning: return TEXT("RETURNING");
	case ERunState::BallDead: return TEXT("DEAD");
	default: return TEXT("-");
	}
}

float ClampGuard(float Guard, const FCricketControlTuning& T)
{
	return FMath::Clamp(Guard, -FMath::Max(T.GuardMax, 0.f), FMath::Max(T.GuardMax, 0.f));
}

float MoveGuard(float Guard, bool bLeft, bool bRight, ECricketHand BatHand, const FCricketControlTuning& T)
{
	if (bLeft == bRight) return ClampGuard(Guard, T);
	// Screen-left is world +Y; batter-relative guard is world Y times the off-side sign.
	const float Off = OffSideSign(BatHand);
	const float Step = FMath::Max(T.GuardStep, 0.01f) * Off;
	Guard += bLeft ? Step : -Step;
	return ClampGuard(Guard, T);
}

const TCHAR* GuardName(float Guard)
{
	const float A = FMath::Abs(Guard);
	if (A < 0.03f) return TEXT("MIDDLE");
	return Guard > 0.f ? TEXT("OFF SIDE") : TEXT("LEG SIDE");
}
} // namespace CricketControl

namespace CricketTouch
{
namespace
{
	// The layout's frame: a margin inside the safe area, then the thumb clusters hang off the bottom-right corner.
	constexpr float Margin = 0.03f;
	FBox2D Box(float X0, float Y0, float X1, float Y1) { return FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)); }
	FBox2D Square(const FVector2D& C, float Side) { return FBox2D(C - 0.5f * Side, C + 0.5f * Side); }
	float Right(float Aspect, const FInsets& Safe) { return Aspect - Margin - Safe.R; }
	float Left(const FInsets& Safe) { return Margin + Safe.L; }
	// The score strip runs the full safe width along the foot; the controls stand on a line just above it.
	constexpr float StripH = 0.095f, StripGap = 0.025f;
	float StripFoot(const FInsets& Safe) { return 1.f - Margin - Safe.B; }
	float Bottom(const FInsets& Safe) { return StripFoot(Safe) - StripH - StripGap; }
	// The bowling setup is split for two thumbs: delivery chips stacked straight onto the SWING/SPIN bar on the left,
	// PACE + the BOWL hero on the right. The two bars share one baseline so the foot reads as a single row;
	// the middle stays open for the target drag.
	constexpr float BowlPrimary = 0.16f, SliderH = 0.068f, ClusterGap = 0.02f, MidGap = 0.06f;
	constexpr float ChipRowH = 0.06f, ChipGap = 0.01f;
	float SideWidth(float Aspect, const FInsets& Safe)
	{
		const float Usable = Right(Aspect, Safe) - Left(Safe);
		return FMath::Clamp(0.5f * (Usable - MidGap), 0.30f, 0.46f);
	}
	FBox2D EffortRect(float Aspect, const FInsets& Safe)
	{
		// PACE lives left of BOWL on the bottom line, level with SWING/SPIN.
		const float R = Right(Aspect, Safe), B = Bottom(Safe), Side = SideWidth(Aspect, Safe);
		const float X1 = R - BowlPrimary - ClusterGap, X0 = X1 - (Side - BowlPrimary - ClusterGap);
		return Box(X0, B - SliderH, X1, B);
	}
	FBox2D DialRect(float Aspect, const FInsets& Safe)
	{
		// SWING/SPIN spans the left cluster on the bottom line.
		const float B = Bottom(Safe), X0 = Left(Safe);
		return Box(X0, B - SliderH, X0 + SideWidth(Aspect, Safe), B);
	}
	// Top of the left cluster: the bar plus Rows chip rows.
	float ChipsTop(int32 Rows, const FInsets& Safe) { return Bottom(Safe) - SliderH - Rows * (ChipRowH + ChipGap); }
	float SliderAt(const FBox2D& Rect, float X) { return FMath::Clamp((X - Rect.Min.X) / FMath::Max(Rect.GetSize().X, 1e-4f), 0.f, 1.f); }
	bool InsideAnyButton(const TArray<FButton>& Buttons, const FVector2D& P)
	{
		for (const FButton& B : Buttons) if (B.Rect.IsInside(P)) return true;
		return false;
	}
	// The field editor: the map with a column of actions beside it, the pair centred on the screen.
	constexpr float EditPanelW = 0.34f, EditGap = 0.04f;
}

TArray<FButton> Layout(EMode Mode, int32 NumDeliveries, float Aspect, const FInsets& Safe)
{
	// Hit areas are larger than what the HUD draws inside them. The primary action (RUN, BOWL, REVIEW) sits in the
	// bottom-right corner under the right thumb; the rest fan out from it. Everything keeps clear of the score strip
	// (along the foot), the radar (top right) and the gesture zones (the middle).
	const float R = Right(Aspect, Safe), B = Bottom(Safe);
	constexpr float Primary = 0.16f, Shot = 0.11f, Arc = 0.2f;
	const FVector2D Corner(R - 0.5f * Primary, B - 0.5f * Primary);
	TArray<FButton> Out;
	if (Mode == EMode::Batting || Mode == EMode::Ready)
	{
		// Shot modes on an arc round the corner, where RUN appears once the shot is played: DEFEND to the left,
		// GROUND on the diagonal, LOFT above.
		auto OnArc = [&](float Deg) { const float T = FMath::DegreesToRadians(Deg); return Corner + Arc * FVector2D(FMath::Cos(T), -FMath::Sin(T)); };
		Out.Add({ EButton::Defend, 0, Square(OnArc(180.f), Shot) });
		Out.Add({ EButton::Ground, 0, Square(OnArc(135.f), Shot) });
		Out.Add({ EButton::Loft, 0, Square(OnArc(90.f), Shot) });
		// Before the ball, PLAY takes the corner: the bowler runs in only when the batter is ready.
		if (Mode == EMode::Ready) Out.Add({ EButton::Play, 0, Square(Corner, Primary) });
		// Crease shuffle under the left thumb: two arrows above the strip, clear of the pull pad.
		// Screen-left (◀) is world +Y, screen-right (▶) world -Y; the game converts to batter-relative guard.
		constexpr float GuardSize = 0.11f, GuardGap = 0.015f;
		const float GX = Left(Safe);
		Out.Add({ EButton::GuardLeft, 0, Box(GX, B - GuardSize, GX + GuardSize, B) });
		Out.Add({ EButton::GuardRight, 0, Box(GX + GuardSize + GuardGap, B - GuardSize, GX + 2.f * GuardSize + GuardGap, B) });
	}
	else if (Mode == EMode::Running)
	{
		Out.Add({ EButton::Run, 0, Square(Corner, Primary) });
		Out.Add({ EButton::Cancel, 0, Square(Corner - FVector2D(0.f, 0.2f), Shot) }); // 48dp+ on phones
	}
	else if (Mode == EMode::Release)
	{
		Out.Add({ EButton::Bowl, 0, Square(Corner, Primary) });
	}
	else if (Mode == EMode::Review)
	{
		Out.Add({ EButton::Accept, 0, Box(R - 0.37f, B - 0.1f, R - 0.19f, B) });
		Out.Add({ EButton::Review, 0, Box(R - 0.17f, B - 0.1f, R, B) });
	}
	else if (Mode == EMode::Bowling)
	{
		Out.Add({ EButton::Bowl, 0, Square(Corner, BowlPrimary) });
		// The repertoire as compact chips over the left cluster only, four to a row, the last row straight on the
		// dial. Right stays PACE + BOWL so each thumb owns one group.
		const float LX = Left(Safe), Side = SideWidth(Aspect, Safe);
		constexpr int32 Cols = 4;
		const float ColW = (Side - (Cols - 1) * ChipGap) / Cols;
		const int32 Rows = (NumDeliveries + Cols - 1) / Cols;
		for (int32 I = 0; I < NumDeliveries; ++I)
		{
			const int32 Row = I / Cols, Col = I % Cols;
			const float X0 = LX + Col * (ColW + ChipGap), Y1 = ChipsTop(Rows - Row, Safe) + ChipRowH;
			Out.Add({ EButton::Delivery, I, Box(X0, Y1 - ChipRowH, X0 + ColW, Y1) });
		}
		Out.Add({ EButton::Effort, 0, EffortRect(Aspect, Safe) });
		Out.Add({ EButton::Dial, 0, DialRect(Aspect, Safe) });
		// The radar is the way into the field editor.
		Out.Add({ EButton::Field, 0, Radar(Aspect, Safe) });
	}
	else if (Mode == EMode::FieldEdit)
	{
		// Beside the map: the title, the presets as a two-column grid, then RESET and CANCEL side by side and APPLY
		// at the foot, where the thumb rests. (The rule counters sit in the map's empty corners.)
		const FBox2D Map = FieldMap(Aspect, Safe);
		const float X0 = Map.Max.X + EditGap, X1 = X0 + EditPanelW, XM = 0.5f * (X0 + X1), Row = 0.085f, Gap = 0.015f, Cell = 0.01f;
		const float Top = Map.Min.Y + 0.07f, Foot = Map.Max.Y - 2 * Row - Gap - 0.02f;
		const int32 Rows = (NumFieldPresets + 1) / 2;
		const float PresetRow = (Foot - Top - (Rows - 1) * Cell) / Rows;
		for (int32 I = 0; I < NumFieldPresets; ++I)
		{
			const float PX = I % 2 ? XM + 0.5f * Cell : X0, PY = Top + (I / 2) * (PresetRow + Cell);
			Out.Add({ EButton::FieldPreset, I, Box(PX, PY, PX + 0.5f * (EditPanelW - Cell), PY + PresetRow) });
		}
		Out.Add({ EButton::FieldReset, 0, Box(X0, Map.Max.Y - 2 * Row - Gap, XM - 0.5f * Cell, Map.Max.Y - Row - Gap) });
		Out.Add({ EButton::FieldCancel, 0, Box(XM + 0.5f * Cell, Map.Max.Y - 2 * Row - Gap, X1, Map.Max.Y - Row - Gap) });
		Out.Add({ EButton::FieldApply, 0, Box(X0, Map.Max.Y - Row, X1, Map.Max.Y) });
	}
	else if (Mode == EMode::Pick)
	{
		// IPL selector: NumDeliveries carries the candidate count. A right-hand column of tall
		// rows under the radar, clear of the score strip: the HUD paints each candidate's name
		// inside its row, in the same order. Rows shrink to fit eleven candidates on screen.
		const float T = Margin + Safe.T + 0.28f, X0 = R - 0.46f;
		const float Foot = Bottom(Safe) - 0.12f;
		constexpr float Gap = 0.008f;
		const float RowH = FMath::Max(0.03f, FMath::Min(0.075f, (Foot - T) / FMath::Max(1, NumDeliveries) - Gap));
		for (int32 I = 0; I < NumDeliveries; ++I)
			Out.Add({ EButton::Pick, I, Box(X0, T + I * (RowH + Gap), R, T + I * (RowH + Gap) + RowH) });
	}
	return Out;
}

FBox2D GestureZone(EMode Mode, float Aspect, const FInsets& Safe)
{
	if (Mode == EMode::Batting)
		// Left-thumb pad mirroring the right-thumb shot cluster: tucked left of the middle so the two hands feel
		// symmetric, tall enough to grab anywhere without reaching, and always clear of the buttons and the strip.
		return Box(0.02f + Safe.L, 0.10f + Safe.T, Aspect - 0.78f - Safe.R, 0.78f);
	// The target drag starts anywhere clear of the two bottom clusters: the buttons (and the radar)
	// take their own touches first, and the strip below the clusters stays control-only.
	if (Mode == EMode::Bowling)
	{
		// Worst case the left cluster holds two chip rows (7 deliveries at 4 across).
		constexpr int32 MaxRows = 2;
		const float Top = FMath::Min(ChipsTop(MaxRows, Safe), Bottom(Safe) - BowlPrimary) - ClusterGap;
		return Box(Left(Safe), Margin + Safe.T, Right(Aspect, Safe), Top);
	}
	if (Mode == EMode::FieldEdit)
		return FieldMap(Aspect, Safe);
	return Box(0.f, 0.f, 0.f, 0.f);
}

FBox2D ScoreStrip(float Aspect, const FInsets& Safe)
{
	const float B = StripFoot(Safe);
	return Box(Margin + Safe.L, B - StripH, Right(Aspect, Safe), B);
}

FBox2D Radar(float Aspect, const FInsets& Safe)
{
	const float R = Right(Aspect, Safe), T = Margin + Safe.T;
	return Box(R - 0.26f, T, R, T + 0.26f);
}

FBox2D FieldMap(float Aspect, const FInsets& Safe)
{
	const float T = Margin + Safe.T, L = Margin + Safe.L, R = Right(Aspect, Safe);
	const float Side = FMath::Min(0.8f, R - L - EditGap - EditPanelW);
	// Centred as a pair with the action column, within the safe area.
	const float X0 = FMath::Max(L, 0.5f * (L + R) - 0.5f * (Side + EditGap + EditPanelW));
	return Box(X0, T + 0.02f, X0 + Side, T + 0.02f + Side);
}

FVector2D MapToField(const FBox2D& Map, const FVector2D& P)
{
	const float K = 0.5f * Map.GetSize().Y / CricketGeo::BoundaryRadius;
	const FVector2D D = (P - Map.GetCenter()) / K;
	return FVector2D(D.Y + CricketGeo::PitchLength * 0.5f, -D.X);
}

FVector2D FieldToMap(const FBox2D& Map, const FVector2D& Home)
{
	const float K = 0.5f * Map.GetSize().Y / CricketGeo::BoundaryRadius;
	return Map.GetCenter() + K * FVector2D(-Home.Y, Home.X - CricketGeo::PitchLength * 0.5f);
}

	FCricketControls Read(EMode Mode, int32 NumDeliveries, float Aspect, TArrayView<const FFinger> Fingers, FGesture& Gesture, const FInsets& Safe)
{
	FCricketControls C;
	if (Mode == EMode::None) { Gesture = FGesture(); return C; }
	if (Mode == EMode::Progress)
	{
		for (const FFinger& F : Fingers) if (F.bNew) { C.bProgress = true; break; }
		Gesture = FGesture();
		return C;
	}
	if (Mode == EMode::Pick)
	{
		// A tap picks a candidate; anything else is ignored (never a gesture, never progress).
		const TArray<FButton> Buttons = Layout(Mode, NumDeliveries, Aspect, Safe);
		for (const FFinger& F : Fingers)
		{
			if (!F.bNew) continue;
			for (const FButton& Btn : Buttons)
				if (Btn.Button == EButton::Pick && Btn.Rect.IsInside(F.Pos)) { C.PickIndex = Btn.Index; break; }
			if (C.PickIndex != -1) break;
		}
		Gesture = FGesture();
		return C;
	}
	const TArray<FButton> Buttons = Layout(Mode, NumDeliveries, Aspect, Safe);
	const FBox2D Zone = GestureZone(Mode, Aspect, Safe);

	// Buttons act on touch-down so a tap is prompt. Sliders are handled below as held controls.
	for (const FFinger& F : Fingers)
	{
		if (!F.bNew) continue;
		for (const FButton& Btn : Buttons)
		{
			if (Btn.Button == EButton::Effort || Btn.Button == EButton::Dial) continue;
			if (!Btn.Rect.IsInside(F.Pos)) continue;
			switch (Btn.Button)
			{
			case EButton::Defend: C.bDefend = true; break;
			case EButton::Ground: C.bGround = true; break;
			case EButton::Loft: C.bLoft = true; break;
			case EButton::Run: C.bRun = true; break;
			case EButton::Cancel: C.bCancel = true; break;
			case EButton::Bowl: case EButton::Play: C.bAction = true; break;
			case EButton::Delivery: C.DeliveryPick = Btn.Index; break;
			case EButton::Review: C.bReview = true; break;
			case EButton::Accept: C.bProgress = true; break;
			case EButton::Field: C.bFieldOpen = true; break;
			case EButton::FieldApply: C.bFieldApply = true; break;
			case EButton::FieldCancel: C.bFieldCancel = true; break;
			case EButton::FieldReset: C.bFieldReset = true; break;
			case EButton::FieldPreset: C.FieldPreset = Btn.Index; break;
			case EButton::GuardLeft: C.bGuardLeft = true; break;
			case EButton::GuardRight: C.bGuardRight = true; break;
			default: break;
			}
		}
	}

	// One finger at a time owns the gesture: batting pull, bowling target drag, or a bowling slider. A slider keeps
	// the finger that grabbed it until it lifts, wherever it wanders, so a quick sideways swipe is never dropped.
	const FBox2D Effort = EffortRect(Aspect, Safe), Dial = DialRect(Aspect, Safe);
	auto Slide = [&](EButton Which, float X)
	{
		if (Which == EButton::Effort) C.Effort = SliderAt(Effort, X);
		else C.Dial = 2.f * SliderAt(Dial, X) - 1.f;
	};
	if (Gesture.Finger != INDEX_NONE)
	{
		const FFinger* Held = nullptr;
		for (const FFinger& F : Fingers) if (F.Id == Gesture.Finger) { Held = &F; break; }
		if (Held)
		{
			if (Mode == EMode::Batting)
			{
				C.bPullHeld = true;
				C.PullOrigin = Gesture.Origin;
				C.Pull = Held->Pos - Gesture.Origin;
				Gesture.Last = Held->Pos;
			}
			else if (Mode == EMode::Bowling && Gesture.Slider != EButton::Bowl)
			{
				Slide(Gesture.Slider, Held->Pos.X);
			}
			else if (Mode == EMode::Bowling)
			{
				C.TargetDrag = Held->Pos - Gesture.Last;
				Gesture.Last = Held->Pos;
			}
			else if (Mode == EMode::FieldEdit)
			{
				C.bFieldHeld = true;
				C.FieldAt = Gesture.Last = Held->Pos;
			}
		}
		else
		{
			if (Mode == EMode::Batting)
			{
				C.bPullReleased = true;
				C.PullOrigin = Gesture.Origin;
				C.Pull = Gesture.Last - Gesture.Origin;
			}
			else if (Mode == EMode::FieldEdit)
			{
				// Dropped where the finger was last seen: the frame it lifts carries no position.
				C.bFieldDrop = true;
				C.FieldAt = Gesture.Last;
			}
			Gesture.Finger = INDEX_NONE;
			Gesture.Slider = EButton::Bowl;
		}
	}
	else
	{
		for (const FFinger& F : Fingers)
		{
			if (!F.bNew) continue;
			if (Mode == EMode::Bowling && (Effort.IsInside(F.Pos) || Dial.IsInside(F.Pos)))
			{
				Gesture.Finger = F.Id;
				Gesture.Slider = Effort.IsInside(F.Pos) ? EButton::Effort : EButton::Dial;
				Slide(Gesture.Slider, F.Pos.X);
				break;
			}
			if (!Zone.IsInside(F.Pos) || InsideAnyButton(Buttons, F.Pos)) continue;
			Gesture.Finger = F.Id;
			Gesture.Origin = F.Pos;
			Gesture.Last = F.Pos;
			if (Mode == EMode::Batting)
			{
				C.bPullHeld = true;
				C.PullOrigin = F.Pos;
				C.Pull = FVector2D::ZeroVector;
			}
			else if (Mode == EMode::FieldEdit)
			{
				C.bFieldGrab = C.bFieldHeld = true;
				C.FieldAt = F.Pos;
			}
			break;
		}
	}
	return C;
}
} // namespace CricketTouch
