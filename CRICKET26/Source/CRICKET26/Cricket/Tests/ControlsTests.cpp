// Control-layer automation tests: the touch layout (the only gameplay input, the mouse on desktop) maps onto
// its intents, and the pull/aim/power/timing, bowling target/effort/dial/release and running-call math behave.

#include "Misc/AutomationTest.h"
#include "CricketControls.h"
#include "DeliveryResolver.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTouchControls, "CRICKET26.Touch.Controls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTouchControls::RunTest(const FString&)
{
	using namespace CricketTouch;
	const float Aspect = 16.f / 9.f;
	const TArray<FFinger> None;
	FGesture G;
	auto Tap = [&](EMode Mode, const FVector2D& P)
	{
		FGesture Fresh;
		const TArray<FFinger> F = { { 0, P, true } };
		return Read(Mode, 7, Aspect, F, Fresh);
	};

	// Every button is on screen, buttons do not overlap, and each tap gives exactly its own intent.
	// (Effort/Dial are sliders: holding them gives a value rather than a button press.)
	for (const EMode Mode : { EMode::Batting, EMode::Bowling, EMode::Review })
	{
		const TArray<FButton> Buttons = Layout(Mode, 7, Aspect);
		for (int32 I = 0; I < Buttons.Num(); ++I)
		{
			const FButton& B = Buttons[I];
			const FString Name = FString::Printf(TEXT("mode %d button %d"), int32(Mode), I);
			TestTrue(Name + TEXT(" is on screen"), B.Rect.Min.X >= 0.f && B.Rect.Max.X <= Aspect && B.Rect.Min.Y >= 0.f && B.Rect.Max.Y <= 1.f);
			for (int32 J = I + 1; J < Buttons.Num(); ++J)
				TestFalse(Name + TEXT(" does not overlap another"), B.Rect.Intersect(Buttons[J].Rect));
			const FCricketControls C = Tap(Mode, B.Rect.GetCenter());
			switch (B.Button)
			{
			case EButton::Defend: TestTrue(Name, C.bDefend); break;
			case EButton::Ground: TestTrue(Name, C.bGround); break;
			case EButton::Loft: TestTrue(Name, C.bLoft); break;
			case EButton::Run: TestTrue(Name, C.bRun); break;
			case EButton::Cancel: TestTrue(Name, C.bCancel); break;
			case EButton::Bowl: TestTrue(Name, C.bAction); break;
			case EButton::Delivery: TestEqual(Name, C.DeliveryPick, B.Index); break;
			case EButton::Effort: TestTrue(Name + TEXT(" slider held"), C.Effort >= 0.f && C.Effort <= 1.f); break;
			case EButton::Dial: TestTrue(Name + TEXT(" slider held"), C.Dial >= -1.f && C.Dial <= 1.f); break;
			case EButton::Review: TestTrue(Name, C.bReview && !C.bProgress); break;
			case EButton::Accept: TestTrue(Name, C.bProgress && !C.bReview); break;
			}
		}
	}
	TestEqual(TEXT("seven delivery buttons, BOWL, two sliders and the field editor entry"), Layout(EMode::Bowling, 7, Aspect).Num(), 11);
	TestEqual(TEXT("batting has mode, run and cancel buttons"), Layout(EMode::Batting, 7, Aspect).Num(), 5);
	// Mobile 10/10: every tap target is 48dp+ (0.067 of screen height at 720p).
	for (const EMode Mode : { EMode::Batting, EMode::Review })
		for (const FButton& B : Layout(Mode, 7, Aspect))
			TestTrue(*FString::Printf(TEXT("mode %d button %d min touch size"), int32(Mode), int32(B.Button)),
				B.Rect.GetSize().GetMin() >= 0.13f);
	TestEqual(TEXT("an LBW call has REVIEW and ACCEPT"), Layout(EMode::Review, 7, Aspect).Num(), 2);
	TestFalse(TEXT("a stray tap neither reviews nor accepts"), Tap(EMode::Review, FVector2D(0.5f, 0.2f)).bReview || Tap(EMode::Review, FVector2D(0.5f, 0.2f)).bProgress);

	// Buttons act on touch-down only: a finger still resting on LOFT does not play a second shot.
	const FVector2D Loft = Layout(EMode::Batting, 7, Aspect)[2].Rect.GetCenter();
	{
		FGesture Fresh;
		TestFalse(TEXT("held button does not repeat"), Read(EMode::Batting, 7, Aspect, { { 0, Loft, false } }, Fresh).bLoft);
	}

	// The pull gesture: touch down in the zone claims it, moving updates the pull, lifting releases it.
	{
		const FVector2D Origin = GestureZone(EMode::Batting, Aspect).GetCenter();
		FGesture Pull;
		FCricketControls Held = Read(EMode::Batting, 7, Aspect, { { 0, Origin, true } }, Pull);
		TestTrue(TEXT("touch-down in the zone holds a pull"), Held.bPullHeld && !Held.bPullReleased);
		FCricketControls Dragged = Read(EMode::Batting, 7, Aspect, { { 0, Origin + FVector2D(0.05f, 0.1f), false } }, Pull);
		TestTrue(TEXT("drag updates the pull"), Dragged.bPullHeld && Dragged.Pull.Size() > 0.05f);
		FCricketControls LetGo = Read(EMode::Batting, 7, Aspect, None, Pull);
		TestTrue(TEXT("lift releases the shot"), LetGo.bPullReleased && !LetGo.bPullHeld);
		TestTrue(TEXT("release carries the pull"), LetGo.Pull.Size() > 0.05f);
		TestFalse(TEXT("no duplicate release"), Read(EMode::Batting, 7, Aspect, None, Pull).bPullReleased);
	}

	// A touch-down on a button never starts a gesture.
	{
		FGesture Pull;
		Read(EMode::Batting, 7, Aspect, { { 0, Loft, true } }, Pull);
		TestTrue(TEXT("button tap does not claim the gesture"), Pull.Finger == INDEX_NONE);
	}

	// Bowling drag: a gesture in the zone reports its frame-to-frame delta as the target drag.
	{
		const FVector2D Origin = GestureZone(EMode::Bowling, Aspect).GetCenter();
		FGesture Drag;
		Read(EMode::Bowling, 7, Aspect, { { 0, Origin, true } }, Drag);
		const FCricketControls Moved = Read(EMode::Bowling, 7, Aspect, { { 0, Origin + FVector2D(0.02f, -0.03f), false } }, Drag);
		TestTrue(TEXT("bowling drag moves the target"), (Moved.TargetDrag - FVector2D(0.02f, -0.03f)).Size() < 1e-4f);
	}

	// Progress: a tap anywhere; None: nothing at all.
	TestTrue(TEXT("progress on any tap"), Tap(EMode::Progress, FVector2D(0.5f, 0.2f)).bProgress);
	{
		FGesture Fresh;
		TestFalse(TEXT("progress needs a new tap"), Read(EMode::Progress, 7, Aspect, { { 0, FVector2D(0.5f, 0.2f), false } }, Fresh).bProgress);
	}
	{
		FGesture Fresh;
		const FCricketControls Idle = Read(EMode::None, 7, Aspect, { { 0, Loft, true } }, Fresh);
		TestTrue(TEXT("no controls while the ball is live for the AI side"), !Idle.bLoft && !Idle.bProgress && !Idle.bPullHeld);
	}

	(void)G;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FControlIntent, "CRICKET26.Controls.Intent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FControlIntent::RunTest(const FString&)
{
	using namespace CricketControl;
	const FCricketControlTuning T;

	// Pull magnitude: dead zone, then 0..1 up to the max pull.
	TestTrue(TEXT("inside the dead zone is no pull"), FMath::IsNearlyZero(PullMagnitude(FVector2D(0.005f, 0.005f), T)));
	const float Full = PullMagnitude(FVector2D(0.f, T.PullMax), T);
	TestTrue(TEXT("max pull is full"), FMath::IsNearlyEqual(Full, 1.f, 1e-3f));
	const float Half = PullMagnitude(FVector2D(0.f, 0.5f * (T.PullDeadZone + T.PullMax)), T);
	TestTrue(TEXT("half pull is half"), FMath::IsNearlyEqual(Half, 0.5f, 0.05f));
	// Power curve: fine control low, top end saved for the stretch.
	TestTrue(TEXT("curve is concave"), ShapeMagnitude(0.5f) < 0.5f && ShapeMagnitude(0.9f) > 0.7f);
	TestTrue(TEXT("curve ends"), FMath::IsNearlyEqual(ShapeMagnitude(1.f), 1.f));

	// Aim: down the screen is straight; handedness mirrors without the player reversing.
	TestTrue(TEXT("straight is straight"), FMath::IsNearlyZero(AimFromPull(FVector2D(0.f, 0.15f), ECricketHand::Right, T), 1.f));
	const float ROff = AimFromPull(FVector2D(-0.1f, 0.1f), ECricketHand::Right, T); // screen left for a right-hander
	const float LOff = AimFromPull(FVector2D(0.1f, 0.1f), ECricketHand::Left, T);   // screen right for a left-hander
	TestTrue(TEXT("right-hander screen left is off side"), ROff > 20.f);
	TestTrue(TEXT("left-hander screen right is off side"), LOff > 20.f);
	TestTrue(TEXT("mirror matches"), FMath::IsNearlyEqual(ROff, LOff, 2.f));
	TestTrue(TEXT("aim clamps"), FMath::Abs(AimFromPull(FVector2D(0.2f, -0.2f), ECricketHand::Right, T)) <= T.MaxAimDeg);

	// Aim + shot: short pulls defend, committed pulls play the mode with pull-driven power.
	{
		const FShotAim TapAim = Aim(FVector2D(0.001f, 0.001f), ECricketHand::Right, T);
		TestFalse(TEXT("tap does not commit"), TapAim.bCommitted);
		const FBatInput Tap = ShotFor(TapAim, EBatIntent::Loft, 0.5f);
		TestTrue(TEXT("tap is a defensive push"), Tap.Intent == EBatIntent::Defend && Tap.PressTime == 0.5f);
		const FShotAim FullAim = Aim(FVector2D(0.f, T.PullMax), ECricketHand::Right, T);
		TestTrue(TEXT("full pull commits"), FullAim.bCommitted && FMath::IsNearlyEqual(FullAim.Power, T.MaxPower, 1e-3f));
		const FBatInput Loft = ShotFor(FullAim, EBatIntent::Loft, 0.5f);
		TestTrue(TEXT("committed loft stays loft with pull power"), Loft.Intent == EBatIntent::Loft && FMath::IsNearlyEqual(Loft.Power, T.MaxPower, 1e-3f));
		const FShotAim MidAim = Aim(FVector2D(0.f, 0.5f * (T.PullDeadZone + T.PullMax)), ECricketHand::Right, T);
		const FBatInput MidA = ShotFor(MidAim, EBatIntent::Ground, 0.5f);
		const FBatInput MidB = ShotFor(FullAim, EBatIntent::Ground, 0.5f);
		TestTrue(TEXT("two lofts differ by pull"), MidA.Power < MidB.Power);
		TestTrue(TEXT("leave stays leave"), !ShotFor(FullAim, EBatIntent::Leave, 0.5f).IsShot());
	}
	TestTrue(TEXT("zone named"), FString(ZoneName(0.f)).Len() > 0 && FString(ZoneName(90.f)).Len() > 0 && FString(ZoneName(-90.f)).Len() > 0);
	TestTrue(TEXT("bands differ"), FString(PowerBandName(0.9f)) != FString(PowerBandName(0.2f)));

	// Timing grades use the simulated contact error, not a global timer.
	{
		auto Graded = [&](float Err, bool bContact = true)
		{
			FDeliveryResult R;
			R.Contact.TimingError = Err;
			R.Contact.Zone = bContact ? EContactZone::Middle : EContactZone::Miss;
			FBatInput In;
			In.Intent = EBatIntent::Ground;
			In.PressTime = 0.5f;
			return GradeShot(R, In, T);
		};
		TestTrue(TEXT("perfect"), Graded(0.f) == ETimingGrade::Perfect);
		TestTrue(TEXT("good"), Graded(T.GoodTiming * 0.9f) == ETimingGrade::Good);
		TestTrue(TEXT("early"), Graded(-T.EarlyLateTiming * 0.9f) == ETimingGrade::Early);
		TestTrue(TEXT("late"), Graded(T.EarlyLateTiming * 0.9f) == ETimingGrade::Late);
		TestTrue(TEXT("very early"), Graded(-1.f) == ETimingGrade::VeryEarly);
		TestTrue(TEXT("very late"), Graded(1.f) == ETimingGrade::VeryLate);
		TestTrue(TEXT("miss"), Graded(0.f, false) == ETimingGrade::Miss);
		FBatInput Leave;
		TestTrue(TEXT("leave is none"), GradeShot(FDeliveryResult(), Leave, T) == ETimingGrade::None);
	}

	// Release grades, including the overstep no-ball.
	TestTrue(TEXT("release perfect"), GradeRelease(0.f, T) == EReleaseGrade::Perfect);
	TestTrue(TEXT("release good"), GradeRelease(T.GoodRelease * 0.9f, T) == EReleaseGrade::Good);
	TestTrue(TEXT("release early"), GradeRelease(-0.3f, T) == EReleaseGrade::Early);
	TestTrue(TEXT("release late"), GradeRelease(0.3f, T) == EReleaseGrade::Late);
	TestTrue(TEXT("release very late"), GradeRelease(0.8f, T) == EReleaseGrade::VeryLate);
	TestTrue(TEXT("overstep is a no-ball"), GradeRelease(0.9f, T) == EReleaseGrade::NoBall);

	// Bowling target: continuous line/length, mirrored for handedness, clamped to legal bounds.
	{
		FDeliveryPlan P;
		P.Line = 0.1f;
		P.Length = 6.f;
		DragTarget(P, FVector2D(-0.1f, 0.f), ECricketHand::Right, T); // screen left for a right-hander
		TestTrue(TEXT("drag left moves off for a right-hander"), P.Line > 0.1f);
		FDeliveryPlan Q;
		Q.Line = 0.1f;
		Q.Length = 6.f;
		DragTarget(Q, FVector2D(-0.1f, 0.f), ECricketHand::Left, T);
		TestTrue(TEXT("same drag mirrors for a left-hander"), Q.Line < 0.1f);
		FDeliveryPlan F;
		F.Line = 0.1f;
		F.Length = 6.f;
		DragTarget(F, FVector2D(0.f, 0.2f), ECricketHand::Right, T); // down the screen is fuller
		TestTrue(TEXT("drag down is fuller"), F.Length < 6.f);
		FDeliveryPlan Far;
		Far.Line = 10.f;
		Far.Length = 100.f;
		ClampTarget(Far);
		TestTrue(TEXT("target clamps"), Far.Line <= 1.6f && Far.Length <= 13.f);
	}

	// Swing/spin dial: gradual, signed on screen, limited by the bowler via Movement.
	{
		TestTrue(TEXT("stock swings"), IsSwingFamily(EDeliveryType::Stock) && IsSwingFamily(EDeliveryType::Outswing));
		TestFalse(TEXT("seam is not swing"), IsSwingFamily(EDeliveryType::Seam));
		FDeliveryPlan P;
		P.Type = EDeliveryType::Stock;
		P.Movement = 1.f;
		ApplyDial(P, -0.8f, ECricketHand::Right, T); // screen left: away from a right-hander
		TestTrue(TEXT("screen left is outswing to a right-hander"), P.Type == EDeliveryType::Outswing && FMath::IsNearlyEqual(P.Movement, 0.8f, 1e-3f));
		ApplyDial(P, 0.f, ECricketHand::Right, T);
		TestTrue(TEXT("centre dial is stock"), P.Type == EDeliveryType::Stock);
		FDeliveryPlan L;
		L.Type = EDeliveryType::Stock;
		ApplyDial(L, 0.8f, ECricketHand::Left, T); // screen right: away from a left-hander
		TestTrue(TEXT("screen right is outswing to a left-hander"), L.Type == EDeliveryType::Outswing);
		FDeliveryPlan S;
		S.Type = EDeliveryType::OffBreak;
		ApplyDial(S, 0.f, ECricketHand::Right, T);
		TestTrue(TEXT("spin dial sets amount, not type"), S.Type == EDeliveryType::OffBreak && FMath::IsNearlyEqual(S.Movement, 0.5f, 1e-3f));
		FDeliveryPlan D;
		D.Type = EDeliveryType::Outswing;
		D.Movement = 0.6f;
		TestTrue(TEXT("dial reads back"), FMath::IsNearlyEqual(FMath::Abs(DialFor(D, ECricketHand::Right)), 0.6f, 1e-3f));
	}

	// Running: calls queue, cancel drops a pending call or turns the batters back, states read out.
	{
		FRunCalls Calls;
		FRunningOutcome Idle;
		TestTrue(TEXT("first call queues"), CallRun(Calls, Idle, 0.2f, T) && Calls.Go.Num() == 1 && Calls.bManual);
		TestTrue(TEXT("second call queues"), CallRun(Calls, Idle, 1.5f, T) && Calls.Go.Num() == 2);
		TestTrue(TEXT("requested state"), RunState(Idle, Calls, 0.3f) == ERunState::RunRequested);
		TestTrue(TEXT("cancel drops a pending call"), CancelRun(Calls, Idle, 0.1f) == ECancelResult::Dropped);
		TestEqual(TEXT("one call left"), Calls.Go.Num(), 1);
		// Under way: cancelling turns them back.
		FRunningOutcome Live;
		Live.Attempted = 1;
		Live.Leaves = { 0.2f };
		Live.RunTimes = { 3.f };
		TestTrue(TEXT("cancel turns back"), CancelRun(Calls, Live, 1.f) == ECancelResult::TurnBack);
		TestTrue(TEXT("back recorded"), Calls.Back >= 0.f);
		TestTrue(TEXT("turning state"), RunState(Live, Calls, 1.f) == ERunState::Turning);
		Live.bSentBack = true;
		TestTrue(TEXT("returning state"), RunState(Live, Calls, 1.5f) == ERunState::Returning);
		FRunCalls Many;
		for (int32 I = 0; I < T.MaxRuns + 2; ++I) CallRun(Many, Idle, 0.1f * I, T);
		TestEqual(TEXT("calls cap at max runs"), Many.Go.Num(), T.MaxRuns);
		FRunCalls Empty;
		TestTrue(TEXT("nothing to cancel"), CancelRun(Empty, Idle, 1.f) == ECancelResult::Nothing);
	}
	return true;
}

#endif
