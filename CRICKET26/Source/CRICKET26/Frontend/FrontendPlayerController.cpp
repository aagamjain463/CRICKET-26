#include "FrontendPlayerController.h"
#include "FrontendSettingsSave.h"
#include "FrontendStatics.h"
#include "Widgets/FrontendRoot.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
	// The splash is the app opening, not a loading screen: returning from a match skips it.
	bool bSplashShown = false;
}

void AFrontendPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;

	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableTouchEvents = true;
	UFrontendSettingsSave::Get()->ApplyGlobal();

	const AGameModeBase* Mode = GetWorld()->GetAuthGameMode();
	Root = CreateWidget<UFrontendRoot>(this, UFrontendRoot::StaticClass());
	Root->Configure(UFrontendStatics::TabFromOptions(Mode ? Mode->OptionsString : FString()), !bSplashShown);
	Root->AddToViewport(100);
	bSplashShown = true;

	// Game and UI, so keys the widgets leave unhandled (Esc, Android back) still reach SetupInputComponent.
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	UFrontendStatics::ScheduleDevShot(this, 3.5f);
}

void AFrontendPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AFrontendPlayerController::HandleBack);
	InputComponent->BindKey(EKeys::Android_Back, IE_Pressed, this, &AFrontendPlayerController::HandleBack);
}

void AFrontendPlayerController::HandleBack()
{
	if (!Root || Root->IsStartingMatch() || Root->Back()) return;

	UFrontendRoot::FSheet Quit;
	Quit.Eyebrow = TEXT("CRICKET 26");
	Quit.Title = TEXT("Leave the game?");
	Quit.Body = TEXT("Your settings are saved.");
	Quit.bShowStatus = false;
	Quit.PrimaryLabel = TEXT("Quit");
	Quit.CloseLabel = TEXT("Stay");
	TWeakObjectPtr<AFrontendPlayerController> Self(this);
	Quit.OnPrimary = [Self]() { if (Self.IsValid()) UKismetSystemLibrary::QuitGame(Self.Get(), Self.Get(), EQuitPreference::Quit, false); };
	Root->ShowSheet(Quit);
}
