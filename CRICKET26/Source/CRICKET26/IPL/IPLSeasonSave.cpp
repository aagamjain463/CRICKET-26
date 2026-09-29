#include "IPLSeasonSave.h"
#include "Kismet/GameplayStatics.h"

UIPLSeasonSave* UIPLSeasonSave::Get()
{
	static UIPLSeasonSave* Live = nullptr; // rooted: one copy for the whole app session, across map travel
	if (!Live)
	{
		Live = Cast<UIPLSeasonSave>(UGameplayStatics::LoadGameFromSlot(Slot(), 0));
		if (!Live) Live = NewObject<UIPLSeasonSave>();
		Live->AddToRoot();
	}
	return Live;
}

void UIPLSeasonSave::Reset()
{
	// The static inside Get() cannot be cleared directly; persist an empty season and reload it.
	UIPLSeasonSave* Fresh = NewObject<UIPLSeasonSave>();
	UGameplayStatics::SaveGameToSlot(Fresh, Slot(), 0);
	UIPLSeasonSave* Live = Get();
	Live->Season = FIPLSeason();
	Live->bHasSeason = false;
}

void UIPLSeasonSave::Persist()
{
	UGameplayStatics::SaveGameToSlot(this, Slot(), 0);
}

void UIPLSeasonSave::Clear()
{
	Season = FIPLSeason();
	bHasSeason = false;
	Persist();
}
