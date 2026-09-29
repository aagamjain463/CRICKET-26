#include "FrontendGameMode.h"
#include "FrontendPlayerController.h"

AFrontendGameMode::AFrontendGameMode()
{
	PlayerControllerClass = AFrontendPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
}
