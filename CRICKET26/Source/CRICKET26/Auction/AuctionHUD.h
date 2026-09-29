// The auction's broadcast HUD, painted in Slate over the 3D room: the team pick and retention screens, then the
// live lower third (base price, the player, the current bid), the auctioneer's caption, the hammer and SOLD cards,
// the Right to Match dialogs, the purse table, player list and squad panels, the purse ticker, and the results.
// Every control is a touch target (the mouse is the finger on desktop); it only reads the game mode and calls it.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

class AAuctionGameMode;

class SAuctionHUD : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAuctionHUD) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, AAuctionGameMode* InGame);
	static void AddToGameViewport(AAuctionGameMode* InGame);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
		int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;

	/** A touch target laid down while painting; the last one painted under the finger wins. */
	struct FHit
	{
		FBox2D Box;
		TFunction<void()> Do;
	};

private:
	TWeakObjectPtr<AAuctionGameMode> Game;
	mutable TArray<FHit> Hits;
	FBox2D Pressed = FBox2D(ForceInit);
	double PressedAt = -100.0;
	int32 Scroll = 0; // first row of long lists
};
