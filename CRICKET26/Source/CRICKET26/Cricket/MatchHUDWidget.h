// The match HUD, painted in Slate over the game viewport: the broadcast score strip along the bottom, the field
// radar, the touch controls (drawn from the same CricketTouch layout the game mode reads, so what is drawn is what is
// pressed), the captain's field editor, and the event, review and replay panels. It only reads the game mode.
// The F1 debug overlay stays on ASuperOverHUD's canvas.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class ASuperOverGameMode;
class ASuperOverHUD;

class SCricketMatchHUD : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCricketMatchHUD) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, ASuperOverGameMode* InGame, ASuperOverHUD* InHUD);
	/** Puts the HUD over the game viewport, below the frontend's pause and result cards. The game mode calls this once. */
	static void AddToGameViewport(ASuperOverGameMode* InGame, ASuperOverHUD* InHUD);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	TWeakObjectPtr<ASuperOverGameMode> Game;
	TWeakObjectPtr<ASuperOverHUD> HUD;
	// The newest ball in the over, to pulse its disc once when it lands in the strip.
	mutable int32 SeenBalls = -1;
	mutable double NewBallAt = -100.0;
};
