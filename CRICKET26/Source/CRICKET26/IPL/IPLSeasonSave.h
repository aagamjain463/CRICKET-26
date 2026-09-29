// Persisted IPL season. One slot; the tournament model owns the truth and the UI only reads it.
// Uses the same rooted-singleton pattern as the frontend settings so the season survives map
// travel (auction -> hub -> match -> hub) and resumes after a relaunch.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "IPLTypes.h"
#include "IPLSeasonSave.generated.h"

UCLASS()
class UIPLSeasonSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() FIPLSeason Season;
	UPROPERTY() bool bHasSeason = false;

	static FString Slot() { return TEXT("Cricket26IPLv1"); }

	// The one live copy for the session; loaded from disk on first use.
	static UIPLSeasonSave* Get();
	// Drops the cached copy so the next Get() reloads from disk (tests, new season).
	static void Reset();
	void Persist();
	void Clear();
};
