// Player controls as intents for one frame. The keyboard and the touch screen each fill an
// FCricketControls and the game mode merges them, so both drive exactly the same batting and bowling
// logic. The touch layout is pure (screen-height units, origin top left) so the HUD draws the same
// buttons the input reads, and tests can press them.

#pragma once

#include "CoreMinimal.h"

struct FCricketControls
{
	// Held: shot direction when batting, moving the target when bowling.
	bool bLeft = false, bRight = false, bUp = false, bDown = false;
	// Pressed this frame.
	bool bGround = false, bLoft = false, bDefend = false, bRun = false;
	bool bAction = false;   // bowling: start the run-up, then release
	bool bProgress = false; // next innings / next match, skip a replay
	int32 DeliveryPick = -1;

	void Merge(const FCricketControls& O);
};

namespace CricketTouch
{
	enum class EMode : uint8 { None, Batting, Bowling, Progress };

	enum class EButton : uint8 { Defend, Ground, Loft, Run, Bowl, Delivery };

	struct FButton
	{
		EButton Button;
		int32 Index = 0;  // which delivery, for Delivery buttons
		FBox2D Rect;      // screen-height units: y 0..1, x 0..Aspect
	};

	/** Thumb stick on the left for direction and target. */
	constexpr float StickRadius = 0.13f;
	inline FVector2D StickCentre() { return FVector2D(0.24f, 0.78f); }

	TArray<FButton> Layout(EMode Mode, int32 NumDeliveries, float Aspect);

	/**
	 * Controls from the fingers on the screen. Held: every finger down; Pressed: fingers that touched
	 * down this frame. Buttons act on touch-down, not release, so a batting tap is as prompt as a key.
	 */
	FCricketControls Read(EMode Mode, int32 NumDeliveries, float Aspect, TArrayView<const FVector2D> Held, TArrayView<const FVector2D> Pressed);
}
