// The pending match context: what the menu staged for the match map to consume. Map travel only carries URL options,
// so the XIs ride here (rooted, same pattern as the settings save) and the match game mode consumes them once in
// StartPlay. Two kinds: an IPL season fixture (the season hub), or a quick match between two real teams (match setup:
// international or IPL, with the toss already decided). A match started any other way never sets this, so neither can
// leak into a later match.

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

	// Quick match between two real teams (FixtureId stays INDEX_NONE). Home is the user's team.
	UPROPERTY() bool bQuick = false;
	UPROPERTY() uint8 QuickCompetition = 0; // ECompetition
	UPROPERTY() int32 QuickHome = INDEX_NONE; // team indices into RealTeams::Teams(Competition)
	UPROPERTY() int32 QuickAway = INDEX_NONE;
	UPROPERTY() int32 QuickOvers = 1;
	UPROPERTY() int32 QuickBatFirst = 0; // the toss: 0 the user's team bats first, 1 the opponent

	static UIPLPendingMatch* Get();
	void Set(int32 InFixtureId, const FIPLPlayingXI& InHomeXI, const FIPLPlayingXI& InAwayXI);
	void SetQuick(uint8 Competition, int32 Home, int32 Away, const FIPLPlayingXI& InHomeXI, const FIPLPlayingXI& InAwayXI,
		int32 Overs, int32 BatFirst);
	void Clear();
};
