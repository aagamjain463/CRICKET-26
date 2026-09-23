// Placeholder broadcast HUD drawn on the canvas: score bug, pressure line, prompts, release meter,
// event banners and the F1 debug overlay. Listens to the game mode's semantic events.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SuperOverMatch.h"
#include "SuperOverHUD.generated.h"

UCLASS()
class ASuperOverHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

private:
	FString Banner;
	int32 BannerPriority = 0;
	double BannerAt = -100.0;

	void OnEvent(ECricketEvent Event);
	void Text(const FString& S, float X, float Y, const FLinearColor& Colour = FLinearColor::White, float Scale = 1.f, bool bCentre = false);
};
