#include "FrontendSettingsSave.h"
#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

UFrontendSettingsSave* UFrontendSettingsSave::Get()
{
	static UFrontendSettingsSave* Live = nullptr; // rooted: one copy for the whole app session, across map travel
	if (!Live)
	{
		Live = Cast<UFrontendSettingsSave>(UGameplayStatics::LoadGameFromSlot(Slot(), 0));
		if (!Live) Live = NewObject<UFrontendSettingsSave>();
		Live->AddToRoot();
	}
	return Live;
}

void UFrontendSettingsSave::Persist()
{
	UGameplayStatics::SaveGameToSlot(this, Slot(), 0);
}

void UFrontendSettingsSave::ApplyGlobal() const
{
	if (!GEngine) return;
	if (FAudioDeviceHandle Audio = GEngine->GetMainAudioDevice())
	{
		Audio->SetTransientPrimaryVolume(FMath::Clamp(MasterVolume, 0.f, 1.f));
	}
	if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
	{
		MaxFps->Set(bHighFrameRate ? 60 : 30, ECVF_SetByGameSetting);
	}
}

FString UFrontendSettingsSave::MatchOptions() const
{
	FString Options = FString::Printf(TEXT("?Difficulty=%d?Quality=%d?TimingBar=%d"), FMath::Clamp(Difficulty, 0, 3), FMath::Clamp(Quality, 0, 3), bTimingBar ? 1 : 0);
	if (Venue >= 0) Options += FString::Printf(TEXT("?Venue=%d"), Venue);
	return Options;
}
