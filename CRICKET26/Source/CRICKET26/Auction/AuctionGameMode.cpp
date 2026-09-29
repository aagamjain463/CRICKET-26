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
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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

FString AAuctionGameMode::SavePath()
{
	return FPaths::ProjectSavedDir() / TEXT("Auction/Resume.txt");
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
	bHasSave = IFileManager::Get().FileExists(*SavePath());
	bAuto = FParse::Param(FCommandLine::Get(), TEXT("AuctionAuto"));
	if (FParse::Param(FCommandLine::Get(), TEXT("AuctionMini"))) Setup.Mode = EAuctionMode::Mini;

	// A career: the season just played hands its squads to the next auction, a mini one or, every three years, a mega.
	UIPLSeasonSave* Season = UIPLSeasonSave::Get();
	if (UGameplayStatics::HasOption(OptionsString, TEXT("career")) && Season && Season->bHasSeason && Season->Season.bComplete)
	{
		const FIPLSeason& S = Season->Season;
		bCareer = true;
		Setup.Season = S.Year + 1;
		Setup.Mode = AuctionRules::IsMegaSeason(Setup.Season) ? EAuctionMode::Mega : EAuctionMode::Mini;
		Setup.Carried.SetNum(AuctionData::Franchises().Num());
		for (int32 T = 0; T < FMath::Min(S.Squads.Num(), Setup.Carried.Num()); ++T)
			for (const FIPLSquadPlayer& P : S.Squads[T].Players) Setup.Carried[T].Add({ P.PlayerId, P.Price, P.Price, false, false, P.Price });
		Picked = { FMath::Max(0, S.UserTeam) };
		BuildAuction();
	}

	FString Code;
	if (!bCareer && FParse::Value(FCommandLine::Get(), TEXT("AuctionTeam="), Code) && AuctionData::FranchiseIndex(Code) != INDEX_NONE)
	{
		PickTeamAndSetup(AuctionData::FranchiseIndex(Code), Setup.Mode, false);
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

// ---- Setup ---------------------------------------------------------------------------------------------------------

void AAuctionGameMode::PickTeam(int32 Index)
{
	if (Screen != EScreen::PickTeam || !AuctionData::Franchises().IsValidIndex(Index) || Picked.Contains(Index)) return;
	// The first seat chooses the auction; the others just take a table.
	if (Picked.IsEmpty()) { PendingPickTeam = Index; return; }
	Picked.Add(Index);
	if (Picked.Num() >= Seats) BuildAuction();
}

void AAuctionGameMode::PickTeamAndSetup(int32 Index, EAuctionMode Mode, bool bNoRetentionsChoice)
{
	if (Screen != EScreen::PickTeam || !AuctionData::Franchises().IsValidIndex(Index)) return;
	PendingPickTeam = INDEX_NONE;
	Setup.Mode = Mode;
	Setup.bNoRetentions = Mode == EAuctionMode::Mega && bNoRetentionsChoice;
	bNoRetentions = Setup.bNoRetentions;
	Picked = { Index };
	if (Picked.Num() >= Seats) BuildAuction();
}

void AAuctionGameMode::BuildAuction()
{
	Setup.Humans = Picked;
	Auction = MakeUnique<FAuction>(Setup, int32(FDateTime::Now().GetTicks() % 100000));
	bNoRetentions = Auction->bNoRetentions;
	// The AI sides do their deals first, so the trade window shows them.
	Auction->OpenTradeWindow();
	RetainSeat = 0;
	Team = Picked.IsEmpty() ? 0 : Picked[0];
	Keep.Reset();
	ClearSave();
	if (bNoRetentions)
	{
		for (int32 T : Picked) Auction->Retain(T, TArray<int32>());
		BeginLive();
		return;
	}
	if (Auction->IsMini()) SuggestKeep(); // a mini auction starts from the squad you have
	Screen = EScreen::Retain;
}

void AAuctionGameMode::StartWithNoRetentions()
{
	if (!Auction || Screen != EScreen::Retain || Auction->IsMini()) return;
	// Every table gives up its retentions: rebuild the auction as a fresh mega auction.
	Setup.bNoRetentions = true;
	BuildAuction();
}

void AAuctionGameMode::StartWithRetentions()
{
	bNoRetentions = false;
	Screen = EScreen::Retain;
}

void AAuctionGameMode::ToggleKeep(int32 Player)
{
	if (Screen != EScreen::Retain || !Auction) return;
	if (Keep.Remove(Player) == 0)
	{
		TArray<int32> Try = Keep;
		Try.Add(Player);
		if (Auction->CanRetain(Team, Try)) Keep = MoveTemp(Try);
	}
}

void AAuctionGameMode::SuggestKeep()
{
	if (Screen == EScreen::Retain || (Auction && Auction->Phase == EAuctionPhase::Retention)) Keep = Auction->AiRetentions(Team);
}

void AAuctionGameMode::ConfirmRetentions()
{
	if (Screen != EScreen::Retain || !Auction || !Auction->Retain(Team, Keep)) return;
	// Pass the paddle: the next person makes their retentions.
	if (++RetainSeat < Picked.Num())
	{
		Team = Picked[RetainSeat];
		Keep.Reset();
		TradeGive = TradeWith = TradeGet = INDEX_NONE;
		TradeResult.Reset();
		if (Auction->IsMini()) SuggestKeep();
		return;
	}
	BeginLive();
}

void AAuctionGameMode::BeginLive()
{
	Auction->BeginAuction();
	Screen = EScreen::Live;
	Team = Picked.IsEmpty() ? 0 : Picked[0];
	Panel = EPanel::None;
	SaveProgress();
}

void AAuctionGameMode::ProposeTrade()
{
	if (!Auction || TradeGive == INDEX_NONE || TradeGet == INDEX_NONE || TradeWith == INDEX_NONE) return;
	FString Why;
	if (Auction->ProposeTrade(Team, TradeGive, TradeWith, TradeGet, &Why))
	{
		TradeResult = FString::Printf(TEXT("Done: %s for %s."), *FAuction::Player(TradeGive).Name, *FAuction::Player(TradeGet).Name);
		Keep.Remove(TradeGive);
		TradeGive = TradeGet = INDEX_NONE;
	}
	else TradeResult = FString::Printf(TEXT("No deal: %s."), *Why);
}

void AAuctionGameMode::FocusNext()
{
	if (Picked.Num() < 2 || Screen != EScreen::Live) return;
	Team = Picked[(Picked.IndexOfByKey(Team) + 1) % Picked.Num()];
}

// ---- Live ----------------------------------------------------------------------------------------------------------

void AAuctionGameMode::Bid(int32 Table)
{
	if (bPaused || bDayBreak || !Auction) return;
	Auction->HumanBid(Table == INDEX_NONE ? Team : Table); // presented with the other events next tick
}

void AAuctionGameMode::JumpBid(int32 Table)
{
	if (bPaused || bDayBreak || !Auction) return;
	if (Auction->HumanJumpBid(Table, JumpTo)) JumpTo = 0;
}

void AAuctionGameMode::Timeout(int32 Table)
{
	if (!bPaused && Auction) Auction->RequestTimeout(Table);
}

void AAuctionGameMode::SetWish(int32 Player, int32 Max, bool bAutoBid)
{
	if (Auction) Auction->SetWish(Team, Player, Max, bAutoBid);
}

void AAuctionGameMode::SkipCurrentLot()
{
	if (Screen != EScreen::Live || !Auction) return;
	if (Auction->IsHuman(Auction->Holder) && Auction->Phase == EAuctionPhase::Bidding) return;

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

void AAuctionGameMode::EndDayBreak()
{
	bDayBreak = false;
}

void AAuctionGameMode::SetPace(EPace Pace)
{
	PaceMode = Pace;
	if (!Auction) return;
	if (Pace == EPace::SimToEnd) Auction->SkipSet();
}

void AAuctionGameMode::RestartAuction()
{
	if ((Screen != EScreen::Live && Screen != EScreen::Results) || !Auction) return;
	const TMap<int32, TArray<int32>> Kept = [this]()
	{
		// The same retentions: every human table keeps what it kept.
		TMap<int32, TArray<int32>> M;
		for (int32 T : Picked)
			for (const FAuctionSigning& S : Auction->Teams[T].Squad) if (S.bRetained) M.FindOrAdd(T).Add(S.Player);
		return M;
	}();
	Auction = MakeUnique<FAuction>(Setup, int32(FDateTime::Now().GetTicks() % 100000));
	Auction->OpenTradeWindow();
	for (int32 T : Picked) Auction->Retain(T, Kept.FindRef(T));
	Caption.Empty();
	CaptionAt = Now();
	VoiceUntil = -100.0;
	Seen = 0;
	SoldAt = -100.0;
	RaiseTo = JumpTo = 0;
	bPaused = bDayBreak = false;
	if (VoiceWave) VoiceWave->ResetAudio();
	if (VoiceAudio) VoiceAudio->SetPaused(false);
	if (Room) Room->ResetDirector();
	BeginLive();
}

void AAuctionGameMode::SaveProgress() const
{
	if (Auction && Auction->Phase != EAuctionPhase::Retention && Auction->Phase != EAuctionPhase::Finished)
		FFileHelper::SaveStringToFile(Auction->SaveState(), *SavePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AAuctionGameMode::ClearSave()
{
	IFileManager::Get().Delete(*SavePath(), false, true, true);
	bHasSave = false;
}

void AAuctionGameMode::ResumeSaved()
{
	FString Text;
	FAuctionConfig Read;
	int32 SavedSeed = 0;
	if (!FFileHelper::LoadFileToString(Text, *SavePath()) || !FAuction::ReadSaveHeader(Text, Read, SavedSeed)) { ClearSave(); return; }
	TUniquePtr<FAuction> Loaded = MakeUnique<FAuction>(Read, SavedSeed);
	if (!Loaded->LoadState(Text)) { ClearSave(); return; }
	Setup = Read;
	Picked = Read.Humans;
	Auction = MoveTemp(Loaded);
	bNoRetentions = Auction->bNoRetentions;
	Team = Picked.IsEmpty() ? 0 : Picked[0];
	// Resume at the next lot: the history is already in the squads, not replayed.
	Seen = Auction->Events.Num();
	Caption = TEXT("Welcome back. We continue with the next lot.");
	CaptionAt = Now();
	PendingPickTeam = INDEX_NONE;
	bDayBreak = Auction->Phase == EAuctionPhase::Break;
	Screen = Auction->Phase == EAuctionPhase::Finished ? EScreen::Results : EScreen::Live;
}

void AAuctionGameMode::ExitToMenu()
{
	SaveProgress();
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
	ClearSave();
	UE_LOG(LogCRICKET26, Display, TEXT("IPL %d season started: user %s, %d league fixtures"), Season.Year,
		AuctionData::Franchises().IsValidIndex(Season.UserTeam) ? *AuctionData::Franchises()[Season.UserTeam].Code : TEXT("?"),
		Season.Fixtures.Num());
	UFrontendStatics::OpenFrontend(this, EFrontendTab::IPLSeason);
}

// ---- Presentation --------------------------------------------------------------------------------------------------

void AAuctionGameMode::Desk()
{
	// Two analysts between sets: who has spent, who has room, and what the leading sides still lack.
	AnalystLines.Reset();
	if (!Auction) return;
	const TArray<FAuctionFranchise>& Fr = AuctionData::Franchises();
	int32 Rich = 0, Poor = 0;
	for (int32 T = 1; T < Auction->Teams.Num(); ++T)
	{
		if (Auction->Teams[T].Purse > Auction->Teams[Rich].Purse) Rich = T;
		if (Auction->Teams[T].Purse < Auction->Teams[Poor].Purse) Poor = T;
	}
	const TArray<FAuctionRecord> Records = Auction->Records();
	if (Records.Num() > 0)
		AnalystLines.Add(FString::Printf(TEXT("The big one so far: %s to %s for %s."), *FAuction::Player(Records[0].Player).Name,
			*Fr[Records[0].Team].Short, *AuctionRules::Money(Records[0].Price)));
	AnalystLines.Add(FString::Printf(TEXT("%s still have %s to spend. %s are down to %s: they'll be shopping at base price."),
		*Fr[Rich].Name, *AuctionRules::Money(Auction->Teams[Rich].Purse), *Fr[Poor].Name, *AuctionRules::Money(Auction->Teams[Poor].Purse)));
	for (int32 T = 0; T < Auction->Teams.Num() && AnalystLines.Num() < 3; ++T)
	{
		const FAuctionNeeds N = Auction->Needs(T);
		if (N.Rank <= 3 && N.Holes.Num() > 0)
			AnalystLines.Add(FString::Printf(TEXT("%s look strong, but: %s."), *Fr[T].Name, *N.Holes[0].ToLower()));
	}
	AnalystAt = Now();
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
	switch (E.Type)
	{
	case EAuctionEvent::Sold:
	{
		SoldAt = Now();
		const TArray<FAuctionRecord> Records = Auction->Records();
		if (Records.Num() > 0 && Records[0].Player == E.Player && E.Amount >= 1500)
		{
			RecordBanner = E.Amount > AuctionRules::IplRecord ? TEXT("MOST EXPENSIVE PLAYER IN IPL HISTORY") : TEXT("MOST EXPENSIVE OF THE AUCTION");
			RecordAt = Now();
		}
		SaveProgress();
		break;
	}
	case EAuctionEvent::Unsold:
		SoldAt = Now();
		SaveProgress();
		break;
	case EAuctionEvent::RtmUsed:
		if (Auction->IsHuman(Auction->Holder)) RaiseTo = AuctionRules::NextBid(Auction->Price);
		break;
	case EAuctionEvent::DayEnded:
		// The hall empties: the clock holds on the day's summary until the tables come back.
		bDayBreak = !bAuto && PaceMode != EPace::SimToEnd;
		SaveProgress();
		break;
	case EAuctionEvent::SetOpened:
		if (++SetsSinceDesk >= 3 && PaceMode == EPace::Watch && Auction->LotsHeld > 0) { SetsSinceDesk = 0; Desk(); }
		if (PaceMode == EPace::SimToEnd) Auction->SkipSet();
		break;
	case EAuctionEvent::LotOpened:
		// Only the lots someone at a human table cares about play out: the shortlist, and a player whose Right to Match
		// a human side holds.
		if (PaceMode == EPace::Targets)
		{
			bool bWanted = false;
			for (int32 T : Picked) bWanted |= Auction->WishFor(T, E.Player) != nullptr || Auction->OwnerOf(E.Player) == T;
			if (!bWanted) Auction->SkipLot();
		}
		break;
	default:
		break;
	}
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
	if (Screen == EScreen::Live && Auction && !bPaused && !bDayBreak)
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
		// Simulating to the end: every set is skipped as it opens (the RTM questions to a human still wait).
		if (PaceMode == EPace::SimToEnd && !Auction->IsFast() && Auction->Phase != EAuctionPhase::Break) Auction->SkipSet();
		// A skip can finish inside one tick, clearing the engine's fast flag before we look: remember it was on.
		const bool bWasFast = Auction->IsFast();
		if (!(bVoiceBusy && Seen < Auction->Events.Num()) || bWasFast)
			Auction->Tick(FMath::Min(Dt, 0.1f) * Speed);
		// When fast resolving, do not rewind into old intermediate bids; but a day's end is always shown.
		if (Auction->Events.Num() - Seen > 1 && (bWasFast || Auction->IsFast()))
		{
			const int32 Day = Auction->Events.IndexOfByPredicate([this](const FAuctionEventRecord& X) { return &X - Auction->Events.GetData() >= Seen && X.Type == EAuctionEvent::DayEnded; });
			Seen = Day != INDEX_NONE ? Day : Auction->Events.Num() - 1;
		}
		if (Seen < Auction->Events.Num() && Now() >= VoiceUntil) Present(Auction->Events[Seen++]);
		if (Auction->Phase == EAuctionPhase::Finished && Now() - CaptionAt > 5.0)
		{
			Screen = EScreen::Results;
			PanelTeam = Team;
			ClearSave();
			UE_LOG(LogCRICKET26, Display, TEXT("Auction finished: %d lots held"), Auction->LotsHeld);
		}
	}
	if (Room) Room->Update(Auction.Get(), Dt, Team);
}
