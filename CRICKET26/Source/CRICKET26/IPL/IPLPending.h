// The pending IPL fixture context: what the season hub staged for the match map to consume.
// Map travel only carries URL options, so the XIs and fixture id ride here (rooted, same pattern
// as the settings save) and the match game mode consumes them once in StartPlay. Standalone
// matches never set this, so they can never leak season state.

#pragma once

#include "CoreMinimal.h"
#include "IPLTypes.h"
#include "IPLPending.generated.h"

UCLASS()
class UIPLPendingMatch : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY() bool bActive = false;
	UPROPERTY() int32 FixtureId = INDEX_NONE;
	UPROPERTY() FIPLPlayingXI HomeXI;
	UPROPERTY() FIPLPlayingXI AwayXI;

	static UIPLPendingMatch* Get();
	void Set(int32 InFixtureId, const FIPLPlayingXI& InHomeXI, const FIPLPlayingXI& InAwayXI);
	void Clear();
};
