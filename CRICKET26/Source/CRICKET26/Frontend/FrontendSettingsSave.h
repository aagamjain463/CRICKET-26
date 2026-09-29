// Persisted frontend preferences. Only settings that really take effect live here: audio and frame-rate
// apply engine-wide straight away; difficulty, venue, graphics tier and the timing bar travel to the
// Super Over as URL options (ASuperOverGameMode reads them in StartPlay). Squads and balance are not stored.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "FrontendSettingsSave.generated.h"

UCLASS()
class UFrontendSettingsSave : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() float MasterVolume = 1.f;
	UPROPERTY() int32 Quality = PLATFORM_IOS || PLATFORM_ANDROID ? 1 : 2; // 0 Low .. 3 Epic, same default as the match
	UPROPERTY() bool bHighFrameRate = true; // 60 fps cap, else 30 to save battery
	UPROPERTY() int32 Difficulty = 2;       // CricketAI::EDifficulty, Hard as in the match
	UPROPERTY() int32 Venue = -1;           // -1 random, else CricketStadium venue index
	UPROPERTY() bool bTimingBar = true;

	static FString Slot() { return TEXT("Cricket26FrontendV2"); }

	// The one live copy for the session; loaded from disk on first use.
	static UFrontendSettingsSave* Get();
	void Persist();

	// Pushes the engine-wide settings (volume, frame cap) into the running engine.
	void ApplyGlobal() const;

	// "?Difficulty=2?Quality=1?TimingBar=1" plus "?Venue=N" when a venue is picked.
	FString MatchOptions() const;
};
