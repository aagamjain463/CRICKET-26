// Travel helpers, so neither the menu nor the match hardcodes map URLs.
// The whole game runs on the empty Entry map and the ?game= URL option picks the GameMode:
// no option -> FrontendGameMode (Entry map prefix in DefaultEngine.ini); Super Over -> SuperOverGameMode.

#pragma once

#include "CoreMinimal.h"
#include "FrontendTypes.h"
#include "UObject/NoExportTypes.h"
#include "FrontendStatics.generated.h"

UCLASS()
class UFrontendStatics : public UObject
{
	GENERATED_BODY()

public:
	static const TCHAR* EntryMap() { return TEXT("/Engine/Maps/Entry"); }

	// Relative travel copies the current URL's options, so leaving a match would keep its game= and reload it.
	static constexpr bool bAbsoluteTravel = true;

	// Opens the menu shell; Tab picks the screen it lands on (the match returns to Play).
	static void OpenFrontend(UObject* Ctx, EFrontendTab Tab = EFrontendTab::Home);

	// Starts a Super Over with the match setup saved in UFrontendSettingsSave.
	UFUNCTION(BlueprintCallable, Category = "Cricket26|Frontend")
	static void OpenSuperOver(UObject* Ctx);
	static void OpenMatch(UObject* Ctx, int32 Overs);
	// Starts an IPL season fixture: the existing 20-over match with the staged XIs (?IPLFixture=).
	static void OpenIPLMatch(UObject* Ctx, int32 FixtureId);

	static FString SuperOverOptions(int32 Overs = 1);

	// Starts the IPL mega auction against nine AI franchises (AuctionGameMode).
	/** The auction; bCareer continues a finished IPL season into its next auction (mini, or mega every three years). */
	static void OpenAuction(UObject* Ctx, bool bCareer = false);
	static EFrontendTab TabFromOptions(const FString& Options);

	// Dev QA: with -FrontendShot=Name, saves the viewport and its UI to Saved/Screenshots after Delay, then quits.
	// Does nothing without the switch or in Shipping.
	static void ScheduleDevShot(UObject* Ctx, float Delay);
};
