// Player controller for the frontend shell: owns the root widget, pointer + touch input, and the
// Esc / Android back key, which walks back through the shell and finally offers to quit.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FrontendPlayerController.generated.h"

class UFrontendRoot;

UCLASS()
class AFrontendPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UFrontendRoot* GetRoot() const { return Root; }

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	UPROPERTY(Transient) TObjectPtr<UFrontendRoot> Root;

	void HandleBack();
};
