// Gate 7 automation test: the touch layout maps taps and the stick onto the same intents as the keyboard.

#include "Misc/AutomationTest.h"
#include "CricketControls.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTouchControls, "CRICKET26.Touch.Controls", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTouchControls::RunTest(const FString&)
{
	using namespace CricketTouch;
	const float Aspect = 16.f / 9.f;
	const TArray<FVector2D> None;
	auto Tap = [&](EMode Mode, const FVector2D& P) { return Read(Mode, 7, Aspect, { P }, { P }); };

	// Every button's centre gives exactly its own intent, and the buttons do not overlap.
	for (const EMode Mode : { EMode::Batting, EMode::Bowling })
	{
		const TArray<FButton> Buttons = Layout(Mode, 7, Aspect);
		for (int32 I = 0; I < Buttons.Num(); ++I)
		{
			const FButton& B = Buttons[I];
			const FString Name = FString::Printf(TEXT("mode %d button %d"), int32(Mode), I);
			TestTrue(Name + TEXT(" is on screen"), B.Rect.Min.X >= 0.f && B.Rect.Max.X <= Aspect && B.Rect.Min.Y >= 0.f && B.Rect.Max.Y <= 1.f);
			TestFalse(Name + TEXT(" is clear of the stick"), B.Rect.IsInside(StickCentre()));
			for (int32 J = I + 1; J < Buttons.Num(); ++J)
				TestFalse(Name + TEXT(" does not overlap another"), B.Rect.Intersect(Buttons[J].Rect));
			const FCricketControls C = Tap(Mode, B.Rect.GetCenter());
			const int32 Intents = C.bGround + C.bLoft + C.bDefend + C.bRun + C.bAction + (C.DeliveryPick >= 0);
			TestEqual(Name + TEXT(" gives one intent"), Intents, 1);
			switch (B.Button)
			{
			case EButton::Defend: TestTrue(Name, C.bDefend); break;
			case EButton::Ground: TestTrue(Name, C.bGround); break;
			case EButton::Loft: TestTrue(Name, C.bLoft); break;
			case EButton::Run: TestTrue(Name, C.bRun); break;
			case EButton::Bowl: TestTrue(Name, C.bAction); break;
			case EButton::Delivery: TestEqual(Name, C.DeliveryPick, B.Index); break;
			}
		}
	}
	TestEqual(TEXT("seven delivery buttons and BOWL"), Layout(EMode::Bowling, 7, Aspect).Num(), 8);

	// Buttons act on touch-down only: a finger still resting on LOFT does not play a second shot.
	const FVector2D Loft = Layout(EMode::Batting, 7, Aspect)[2].Rect.GetCenter();
	TestFalse(TEXT("held button does not repeat"), Read(EMode::Batting, 7, Aspect, { Loft }, None).bLoft);

	// The stick: centre is straight, pushes give the four directions, diagonals give two.
	const FVector2D S = StickCentre();
	const float R = StickRadius;
	auto Hold = [&](const FVector2D& P) { return Read(EMode::Batting, 7, Aspect, { P }, None); };
	const FCricketControls Centre = Hold(S);
	TestTrue(TEXT("stick centre is neutral"), !Centre.bLeft && !Centre.bRight && !Centre.bUp && !Centre.bDown);
	TestTrue(TEXT("stick left"), Hold(S + FVector2D(-R, 0.f)).bLeft);
	TestTrue(TEXT("stick right"), Hold(S + FVector2D(R, 0.f)).bRight);
	TestTrue(TEXT("stick up"), Hold(S + FVector2D(0.f, -R)).bUp);
	TestTrue(TEXT("stick down"), Hold(S + FVector2D(0.f, R)).bDown);
	const FCricketControls Diag = Hold(S + FVector2D(R, -R));
	TestTrue(TEXT("stick diagonal"), Diag.bRight && Diag.bUp && !Diag.bLeft && !Diag.bDown);
	TestFalse(TEXT("a finger on the right half is not the stick"), Hold(FVector2D(Aspect * 0.7f, 0.78f)).bRight);

	// Progress: a tap anywhere; None: nothing at all.
	TestTrue(TEXT("progress on any tap"), Tap(EMode::Progress, FVector2D(0.5f, 0.2f)).bProgress);
	TestFalse(TEXT("progress needs a new tap"), Read(EMode::Progress, 7, Aspect, { FVector2D(0.5f, 0.2f) }, None).bProgress);
	const FCricketControls Idle = Read(EMode::None, 7, Aspect, { Loft, S + FVector2D(-R, 0.f) }, { Loft });
	TestTrue(TEXT("no controls while the ball is live for the AI side"), !Idle.bLoft && !Idle.bLeft && !Idle.bProgress);

	// Merge: keyboard and touch combine, a touch delivery pick overrides.
	FCricketControls Keys;
	Keys.bLeft = true;
	Keys.DeliveryPick = 2;
	FCricketControls Touch;
	Touch.bLoft = true;
	Keys.Merge(Touch);
	TestTrue(TEXT("merge keeps both"), Keys.bLeft && Keys.bLoft);
	TestEqual(TEXT("no touch pick keeps the key pick"), Keys.DeliveryPick, 2);
	Touch.DeliveryPick = 5;
	Keys.Merge(Touch);
	TestEqual(TEXT("touch pick overrides"), Keys.DeliveryPick, 5);
	return true;
}

#endif
