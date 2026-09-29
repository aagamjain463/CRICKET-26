#include "FrontendStatics.h"
#include "FrontendSettingsSave.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UnrealClient.h"

void UFrontendStatics::OpenFrontend(UObject* Ctx, EFrontendTab Tab)
{
	if (!Ctx) return;
	// No game= option: the Entry map prefix resolves to the frontend GameMode.
	UGameplayStatics::OpenLevel(Ctx, EntryMap(), bAbsoluteTravel, FString::Printf(TEXT("Tab=%d"), int32(Tab)));
}

void UFrontendStatics::OpenSuperOver(UObject* Ctx)
{
	OpenMatch(Ctx, 1);
}

void UFrontendStatics::OpenMatch(UObject* Ctx, int32 Overs)
{
	if (!Ctx) return;
	UGameplayStatics::OpenLevel(Ctx, EntryMap(), bAbsoluteTravel, SuperOverOptions(Overs));
}

void UFrontendStatics::OpenAuction(UObject* Ctx)
{
	if (Ctx) UGameplayStatics::OpenLevel(Ctx, EntryMap(), bAbsoluteTravel, TEXT("game=/Script/CRICKET26.AuctionGameMode"));
}

FString UFrontendStatics::SuperOverOptions(int32 Overs)
{
	// Same empty map; the game= option overrides the Entry prefix GameMode.
	return TEXT("game=/Script/CRICKET26.SuperOverGameMode") + UFrontendSettingsSave::Get()->MatchOptions()
		+ FString::Printf(TEXT("?Overs=%d"), Overs);
}

void UFrontendStatics::OpenIPLMatch(UObject* Ctx, int32 FixtureId)
{
	// The XIs ride the pending context (consumed once); the id rides the URL as the fallback key.
	if (!Ctx) return;
	UGameplayStatics::OpenLevel(Ctx, EntryMap(), bAbsoluteTravel,
		SuperOverOptions(20) + FString::Printf(TEXT("?IPLFixture=%d"), FixtureId));
}

EFrontendTab UFrontendStatics::TabFromOptions(const FString& Options)
{
	const int32 Tab = UGameplayStatics::GetIntOption(Options, TEXT("Tab"), 0);
	return Tab >= 0 && Tab < int32(EFrontendTab::Count) ? EFrontendTab(Tab) : EFrontendTab::Home;
}

void UFrontendStatics::ScheduleDevShot(UObject* Ctx, float Delay)
{
#if !UE_BUILD_SHIPPING
	FString Shot;
	UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
	if (!World || !FParse::Value(FCommandLine::Get(), TEXT("FrontendShot="), Shot)) return;
	TWeakObjectPtr<UObject> Weak(Ctx);
	FTimerHandle Capture, Quit;
	World->GetTimerManager().SetTimer(Capture, [Shot]() { FScreenshotRequest::RequestScreenshot(Shot, true, false); }, Delay, false);
	World->GetTimerManager().SetTimer(Quit, [Weak]() { if (Weak.IsValid()) UKismetSystemLibrary::QuitGame(Weak.Get(), nullptr, EQuitPreference::Quit, false); }, Delay + 1.5f, false);
#endif
}
