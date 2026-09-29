// The frontend's only presence inside the Super Over: a small Menu button (phones have no Esc key) and a
// leave-match confirm. Added by a world subsystem, so no gameplay code creates or knows about it.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/WorldSubsystem.h"
#include "FrontendMatchOverlay.generated.h"

class UOverlay;
class UTextBlock;

UCLASS()
class UFrontendMatchOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetConfirmOpen(bool bOpen);
	bool IsConfirmOpen() const;

protected:
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY(Transient) TObjectPtr<UOverlay> Confirm;
	UPROPERTY(Transient) TObjectPtr<UOverlay> ResultLayer;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PauseTeams; // "INDIA  /  AUSTRALIA", filled when the menu opens
	FTimerHandle ResultPoll;
	void CheckForResult();
};

// Puts the overlay on screen when a Super Over world begins play, unless it is an automated run.
UCLASS()
class UFrontendMatchSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// Automation (soak runs, injected-touch tests) plays without the menu so taps cannot land on it.
	static bool WantsOverlay(const TCHAR* CommandLine);
};
