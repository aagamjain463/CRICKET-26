#include "CricketControls.h"

void FCricketControls::Merge(const FCricketControls& O)
{
	bLeft |= O.bLeft; bRight |= O.bRight; bUp |= O.bUp; bDown |= O.bDown;
	bGround |= O.bGround; bLoft |= O.bLoft; bDefend |= O.bDefend; bRun |= O.bRun;
	bAction |= O.bAction; bProgress |= O.bProgress;
	if (O.DeliveryPick >= 0) DeliveryPick = O.DeliveryPick;
}

namespace CricketTouch
{
namespace
{
	FBox2D Box(float X0, float Y0, float X1, float Y1) { return FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)); }
}

TArray<FButton> Layout(EMode Mode, int32 NumDeliveries, float Aspect)
{
	// Thumb-sized (0.16 of screen height) buttons along the bottom right, clear of the score bug (top
	// left), the commentary caption (bottom centre) and the stick (bottom left).
	const float R = Aspect - 0.04f, B = 0.95f, S = 0.16f, Gap = 0.02f;
	TArray<FButton> Out;
	if (Mode == EMode::Batting)
	{
		Out.Add({ EButton::Defend, 0, Box(R - 3 * S - 2 * Gap, B - S, R - 2 * S - 2 * Gap, B) });
		Out.Add({ EButton::Ground, 0, Box(R - 2 * S - Gap, B - S, R - S - Gap, B) });
		Out.Add({ EButton::Loft, 0, Box(R - S, B - S, R, B) });
		Out.Add({ EButton::Run, 0, Box(R - S, B - 2 * S - Gap, R, B - S - Gap) });
	}
	else if (Mode == EMode::Bowling)
	{
		Out.Add({ EButton::Bowl, 0, Box(R - 1.5f * S, B - 1.5f * S, R, B) });
		const float Row = 0.075f, Top = 0.12f;
		for (int32 I = 0; I < NumDeliveries; ++I)
			Out.Add({ EButton::Delivery, I, Box(R - 0.3f, Top + I * Row, R, Top + (I + 1) * Row - 0.01f) });
	}
	return Out;
}

FCricketControls Read(EMode Mode, int32 NumDeliveries, float Aspect, TArrayView<const FVector2D> Held, TArrayView<const FVector2D> Pressed)
{
	FCricketControls C;
	if (Mode == EMode::None) return C;
	if (Mode == EMode::Progress)
	{
		C.bProgress = Pressed.Num() > 0; // tap anywhere
		return C;
	}
	// The stick: any finger held on the left, lower part of the screen, read against the stick centre.
	for (const FVector2D& P : Held)
	{
		if (P.X > Aspect * 0.4f || P.Y < 0.4f) continue;
		const FVector2D D = (P - StickCentre()) / StickRadius;
		C.bLeft |= D.X < -0.4f;
		C.bRight |= D.X > 0.4f;
		C.bUp |= D.Y < -0.4f;
		C.bDown |= D.Y > 0.4f;
	}
	const TArray<FButton> Buttons = Layout(Mode, NumDeliveries, Aspect);
	for (const FVector2D& P : Pressed)
		for (const FButton& Btn : Buttons)
		{
			if (!Btn.Rect.IsInside(P)) continue;
			switch (Btn.Button)
			{
			case EButton::Defend: C.bDefend = true; break;
			case EButton::Ground: C.bGround = true; break;
			case EButton::Loft: C.bLoft = true; break;
			case EButton::Run: C.bRun = true; break;
			case EButton::Bowl: C.bAction = true; break;
			case EButton::Delivery: C.DeliveryPick = Btn.Index; break;
			}
		}
	return C;
}
}
