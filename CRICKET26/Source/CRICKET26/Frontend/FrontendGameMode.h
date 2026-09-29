// Boot GameMode for the app: an empty world that shows only the frontend shell.
// No stadium, no match, no simulation. The Super Over lives in SuperOverGameMode
// on the same Entry map via ?game= (see UFrontendStatics).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FrontendGameMode.generated.h"

UCLASS()
class AFrontendGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFrontendGameMode();
};
