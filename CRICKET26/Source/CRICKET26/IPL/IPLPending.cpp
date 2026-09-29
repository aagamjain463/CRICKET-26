#include "IPLPending.h"

UIPLPendingMatch* UIPLPendingMatch::Get()
{
	static UIPLPendingMatch* Live = nullptr;
	if (!Live)
	{
		Live = NewObject<UIPLPendingMatch>();
		Live->AddToRoot();
	}
	return Live;
}

void UIPLPendingMatch::Set(int32 InFixtureId, const FIPLPlayingXI& InHomeXI, const FIPLPlayingXI& InAwayXI)
{
	const FIPLPlayingXI Home = InHomeXI, Away = InAwayXI; // copied first: the caller may pass this context's own XIs
	Clear();
	bActive = true;
	FixtureId = InFixtureId;
	HomeXI = Home;
	AwayXI = Away;
}

void UIPLPendingMatch::SetQuick(uint8 Competition, int32 Home, int32 Away, const FIPLPlayingXI& InHomeXI, const FIPLPlayingXI& InAwayXI,
	int32 Overs, int32 BatFirst)
{
	const FIPLPlayingXI HomeCopy = InHomeXI, AwayCopy = InAwayXI; // copied first: the caller may pass this context's own XIs
	Clear();
	bActive = true;
	bQuick = true;
	QuickCompetition = Competition;
	QuickHome = Home;
	QuickAway = Away;
	HomeXI = HomeCopy;
	AwayXI = AwayCopy;
	QuickOvers = Overs;
	QuickBatFirst = BatFirst;
}

void UIPLPendingMatch::Clear()
{
	bActive = false;
	FixtureId = INDEX_NONE;
	HomeXI = FIPLPlayingXI();
	AwayXI = FIPLPlayingXI();
	bQuick = false;
	QuickCompetition = 0;
	QuickHome = QuickAway = INDEX_NONE;
	QuickOvers = 1;
	QuickBatFirst = 0;
}
