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
	bActive = true;
	FixtureId = InFixtureId;
	HomeXI = InHomeXI;
	AwayXI = InAwayXI;
}

void UIPLPendingMatch::Clear()
{
	bActive = false;
	FixtureId = INDEX_NONE;
	HomeXI = FIPLPlayingXI();
	AwayXI = FIPLPlayingXI();
}
