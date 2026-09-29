#include "AuctionGameMode.h"
#include "AuctionCalls.h"
#include "AuctionHUD.h"
#include "AuctionRoom.h"
#include "CricketAudio.h"
#include "FrontendStatics.h"
#include "IPLSeason.h"
#include "IPLSeasonSave.h"
#include "AuctionTypes.h"
#include "CRICKET26.h"
#include "Camera/CameraActor.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWaveProcedural.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Sound/SoundWaveProcedural.h"
#include "Engine/World.h"
#include "UnrealClient.h"

AAuctionGameMode::AAuctionGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = nullptr;
}

double AAuctionGameMode::Now() const
{
	return GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
}

void AAuctionGameMode::BeginPlay()
{
	Super::BeginPlay();
	Room = GetWorld()->SpawnActor<AAuctionRoom>();
	FParse::Value(FCommandLine::Get(), TEXT("AuctionSpeed="), Speed);
	VoiceWave = NewObject<USoundWaveProcedural>(this);
	VoiceWave->SetSampleRate(CricketAudio::SampleRate);
	VoiceWave->NumChannels = 1;
	VoiceAudio = UGameplayStatics::CreateSound2D(this, VoiceWave, 1.f, 1.f, 0.f, nullptr, true); // null with -nosound
	if (VoiceAudio)
	{
		VoiceAudio->SetVolumeMultiplier(CricketAudio::BusTrim(CricketAudio::EMixBus::Commentary) * CricketAudio::MixGain);
		VoiceAudio->Play();
	}
	bAuto = FParse::Param(FCommandLine::Get(), TEXT("AuctionAuto"));
	FString Code;
	if (FParse::Value(FCommandLine::Get(), TEXT("AuctionTeam="), Code) && AuctionData::FranchiseIndex(Code) != INDEX_NONE)
	{
		PickTeamAndFormat(AuctionData::FranchiseIndex(Code), false);
		if (bAuto) { SuggestKeep(); ConfirmRetentions(); }
	}
#if !UE_BUILD_SHIPPING
	// Dev QA: periodic screenshots with the HUD, and a timed quit.
	float Every = 0.f, QuitAfter = 0.f;
	FTimerHandle Shots, Quit;
	if (FParse::Value(FCommandLine::Get(), TEXT("AuctionShotEvery="), Every) && Every > 0.f)
	{
		GetWorldTimerManager().SetTimer(Shots, [this]()
		{
			static int32 N = 0;
			const TCHAR* Names[] = { TEXT("Pick"), TEXT("Retain"), TEXT("Live"), TEXT("Results") };
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Auction_%03d_%s"), N++, Names[int32(Screen)]), true, false);
		}, Every, true);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("AuctionQuitAfter="), QuitAfter) && QuitAfter > 0.f)
	{
		GetWorldTimerManager().SetTimer(Quit, [this]() { UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false); }, QuitAfter, false);
	}
#endif
}

void AAuctionGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	// Same teardown race as the match: the voice channel is procedural with outer = this.
	// Stop and detach before the world goes away so REMATCH / menu travel does not crash the mixer.
	if (VoiceAudio)
	{
		VoiceAudio->Stop();
		VoiceAudio->SetSound(nullptr);
		VoiceAudio->DestroyComponent();
	}
	if (VoiceWave) VoiceWave->ResetAudio();
	VoiceAudio = nullptr;
	VoiceWave = nullptr;
	Super::EndPlay(Reason);
}

void AAuctionGameMode::PickTeam(int32 Index)
{
	if (Screen != EScreen::PickTeam || !AuctionData::Franchises().IsValidIndex(Index)) return;
	PendingPickTeam = Index;
}

void AAuctionGameMode::PickTeamAndFormat(int32 Index, bool bNoRetentionsChoice)
{
	if (Screen != EScreen::PickTeam || !AuctionData::Franchises().IsValidIndex(Index)) return;
	Team = Index;
	PendingPickTeam = INDEX_NONE;
	Auction = MakeUnique<FAuction>(Index, int32(FDateTime::Now().GetTicks() % 100000));
	bNoRetentions = bNoRetentionsChoice;
	Auction->bNoRetentions = bNoRetentionsChoice;
	Keep.Reset();

	if (bNoRetentions)
	{
		Auction->Retain(Team, TArray<int32>());
		Auction->BeginAuction();
		Screen = EScreen::Live;
	}
	else
	{
		Screen = EScreen::Retain;
	}
}

void AAuctionGameMode::StartWithNoRetentions()
{
	bNoRetentions = true;
	Keep.Reset();
	if (Auction)
	{
		Auction->bNoRetentions = true;
		Auction->Retain(Team, TArray<int32>());
		Auction->BeginAuction();
	}
	Screen = EScreen::Live;
}

void AAuctionGameMode::StartWithRetentions()
{
	bNoRetentions = false;
	Screen = EScreen::Retain;
}

void AAuctionGameMode::ToggleKeep(int32 Player)
{
	if (Screen != EScreen::Retain) return;
	if (Keep.Remove(Player) == 0)
	{
		TArray<int32> Try = Keep;
		Try.Add(Player);
		if (Auction->CanRetain(Team, Try)) Keep = MoveTemp(Try);
	}
}

void AAuctionGameMode::SuggestKeep()
{
	if (Screen == EScreen::Retain) Keep = Auction->AiRetentions(Team);
}

void AAuctionGameMode::ConfirmRetentions()
{
	if (Screen != EScreen::Retain || !Auction->Retain(Team, Keep)) return;
	Auction->BeginAuction();
	Screen = EScreen::Live;
}

void AAuctionGameMode::Bid()
{
	if (bPaused) return;
	if (Auction) Auction->HumanBid(); // presented with the other events next tick
}

void AAuctionGameMode::SkipCurrentLot()
{
	if (Screen != EScreen::Live || !Auction) return;
	if (Auction->Holder == Team && Auction->Phase == EAuctionPhase::Bidding) return;

	Auction->FastResolveCurrentLot();

	if (VoiceAudio) VoiceAudio->Stop();
	if (VoiceWave) VoiceWave->ResetAudio();
	VoiceUntil = -100.0;
	Caption.Empty();

	int32 FinalEventIdx = INDEX_NONE;
	for (int32 I = Auction->Events.Num() - 1; I >= 0; --I)
	{
		if (Auction->Events[I].Type == EAuctionEvent::Sold || Auction->Events[I].Type == EAuctionEvent::Unsold)
		{
			FinalEventIdx = I;
			break;
		}
	}

	if (FinalEventIdx != INDEX_NONE)
	{
		Seen = FinalEventIdx;
		Present(Auction->Events[Seen++]);
	}
}

void AAuctionGameMode::TogglePause()
{
	if (Screen != EScreen::Live) return;
	bPaused = !bPaused;
	if (VoiceAudio) VoiceAudio->SetPaused(bPaused);
}

void AAuctionGameMode::RestartAuction()
{
	if (Screen != EScreen::Live && Screen != EScreen::Results) return;
	Auction = MakeUnique<FAuction>(Team, int32(FDateTime::Now().GetTicks() % 100000));
	Auction->bNoRetentions = bNoRetentions;
	if (bNoRetentions)
	{
		Keep.Reset();
		Auction->Retain(Team, TArray<int32>());
	}
	else
	{
		Auction->Retain(Team, Keep); // the same retentions; BeginAuction fills in the nine AI sides
	}
	Auction->BeginAuction();
	Screen = EScreen::Live;
	Caption.Empty();
	CaptionAt = Now();
	VoiceUntil = -100.0;
	Seen = 0;
	SoldAt = -100.0;
	RaiseTo = 0;
	Panel = EPanel::None;
	bPaused = false;
	if (VoiceWave) VoiceWave->ResetAudio();
	if (VoiceAudio) VoiceAudio->SetPaused(false);
	if (Room) Room->ResetDirector();
}

void AAuctionGameMode::ExitToMenu()
{
	UFrontendStatics::OpenFrontend(this, EFrontendTab::Home);
}

void AAuctionGameMode::StartSeason()
{
	// Only from the finished auction: the final squads, verbatim, become the season. The results
	// screen stays open on failure (never a half-built season), and succeeds exactly once per tap
	// because a staged season is simply overwritten by the same squads.
	if (Screen != EScreen::Results || !Auction || Auction->Phase != EAuctionPhase::Finished) return;
	FIPLSeason Season = IPLSeason::BuildFromAuction(*Auction);
	FString Why;
	if (!IPLSeason::Validate(Season, Why))
	{
		UE_LOG(LogCRICKET26, Error, TEXT("IPL season refused: %s"), *Why);
		return;
	}
	UIPLSeasonSave* Save = UIPLSeasonSave::Get();
	Save->Season = Season;
	Save->bHasSeason = true;
	Save->Persist();
	UE_LOG(LogCRICKET26, Display, TEXT("IPL season started: user %s, %d league fixtures"),
		AuctionData::Franchises().IsValidIndex(Season.UserTeam) ? *AuctionData::Franchises()[Season.UserTeam].Code : TEXT("?"),
		Season.Fixtures.Num());
	UFrontendStatics::OpenFrontend(this, EFrontendTab::IPLSeason);
}

void AAuctionGameMode::Present(const FAuctionEventRecord& E)
{
	const AuctionCalls::FCall Call = AuctionCalls::Call(*Auction, E);
	if (!Call.Text.IsEmpty())
	{
		Caption = Call.Text;
		CaptionAt = Now();
		float Seconds = 0.f;
		if (VoiceAudio)
		{
			const TArray<int16> Pcm = AuctionCalls::Voice(Call);
			VoiceWave->ResetAudio();
			VoiceWave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.GetData()), Pcm.Num() * sizeof(int16));
			Seconds = float(Pcm.Num()) / float(CricketAudio::SampleRate);
		}
		if (Seconds <= 0.f)
		{
			// No clip (or no audio device): still give her line its reading time, so the next
			// event never cuts her caption off mid-sentence either.
			int32 Words = 0;
			for (const TCHAR* P = *Call.Text; *P; ++P) Words += FChar::IsWhitespace(*P) && !FChar::IsWhitespace(*(P + 1));
			Seconds = FMath::Clamp(0.42f * float(Words + 1) + 0.4f, 1.2f, 8.f);
		}
		VoiceUntil = CaptionAt + double(Seconds) + 0.3; // a breath before the next line
	}
	if (E.Type == EAuctionEvent::Sold || E.Type == EAuctionEvent::Unsold) SoldAt = Now();
	if (E.Type == EAuctionEvent::RtmUsed && Auction->IsHuman(Auction->Holder)) RaiseTo = AuctionRules::NextBid(Auction->Price);
	if (Room) Room->Present(*Auction, E);
}

void AAuctionGameMode::Tick(float Dt)
{
	Super::Tick(Dt);
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && !bViewSet && Room)
	{
		PC->SetViewTarget(Room->Camera);
		PC->bShowMouseCursor = true; // the mouse is the finger on desktop
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
		SAuctionHUD::AddToGameViewport(this);
		bViewSet = true;
	}
	if (Screen == EScreen::Live && Auction && !bPaused)
	{
		if (bAuto && Auction->AwaitingHuman())
		{
			Auction->HumanRtm(false);
			Auction->HumanFinalRaise(Auction->Price);
			Auction->HumanMatch(false);
		}
		// The auctioneer finishes her line before the next paddle: while she is speaking and events
		// are already waiting, the clock holds instead of piling up bids she would talk over. This
		// is also the bidding pace: one call at a time, about as fast as the real room talks.
		const bool bVoiceBusy = Now() < VoiceUntil;
		if (!(bVoiceBusy && Seen < Auction->Events.Num()))
			Auction->Tick(FMath::Min(Dt, 0.1f) * Speed);
		// When fast resolving, do not rewind into old intermediate bids
		if (Auction->Events.Num() - Seen > 1 && Auction->IsFast()) Seen = Auction->Events.Num() - 1;
		if (Seen < Auction->Events.Num() && Now() >= VoiceUntil) Present(Auction->Events[Seen++]);
		if (Auction->Phase == EAuctionPhase::Finished && Now() - CaptionAt > 5.0)
		{
			Screen = EScreen::Results;
			PanelTeam = Team;
			UE_LOG(LogCRICKET26, Display, TEXT("Auction finished: %d lots held"), Auction->LotsHeld);
		}
	}
	if (Room) Room->Update(Auction.Get(), Dt, Team);
}
