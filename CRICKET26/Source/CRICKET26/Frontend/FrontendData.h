// Catalogue behind the frontend screens. Squad rows are derived from the playable Super Over squads;
// auction, scouting and store entries describe features that are not built yet and are marked so.

#pragma once

#include "CoreMinimal.h"
#include "FrontendTypes.h"

namespace FrontendData
{
	inline FString AppName() { return TEXT("CRICKET 26"); }
	inline FString Tagline() { return TEXT("Own the team. Own the moment."); }
	inline FString Season() { return TEXT("SEASON 26"); }

	FString TabName(EFrontendTab Tab);
	FString StatusLabel(EFeatureStatus Status);
	FLinearColor StatusTint(EFeatureStatus Status);

	FFrontendFeature SuperOver();
	TArray<FFrontendFeature> MoreModes();
	TArray<FFrontendFeature> AuctionModes();
	TArray<FFrontendFeature> HomeModules(); // the shortcut cards under the home hero

	TArray<FFranchisePlayerRow> Squad(); // the human side of ASuperOverGameMode::DefaultSquads
	FString FranchiseName();
	FString FranchiseShort();
	FLinearColor FranchiseColour();
	FString FranchiseSponsor();
	float PurseCrore(); // auction budget; fixed until the auction exists

	TArray<FScoutRegion> ScoutRegions();
	TArray<FScoutProspect> Prospects();
	TArray<FStoreOffer> StoreOffers();
	TArray<FString> StoreCategories();

	TArray<FString> VenueNames(); // "Random" first, then the stadium's venues
	FString VenueConditions(int32 Venue); // "Flat pitch · Day"; -1 describes the random pick
	TArray<FString> DifficultyNames();
}
