#include "Widgets/FrontendMatchOverlay.h"
#include "Widgets/FrontendUI.h"
#include "FrontendStatics.h"
#include "FrontendSettingsSave.h"
#include "SuperOverGameMode.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UnrealClient.h"

using namespace FrontendStyle;
using namespace FrontendUI;

void UFrontendMatchOverlay::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UWidgetTree* T = WidgetTree;
	UOverlay* Root = Stack(T);
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UFrontendButton* Menu = CTA(T, TEXT("II  PAUSE"), EButtonKind::Secondary, [this]() { SetConfirmOpen(true); }, 50.f);
	Add(Root, Menu, HAlign_Left, VAlign_Top, FMargin(28.f, 16.f));

	Confirm = Stack(T);
	UFrontendButton* Scrim = Button(T, Space(T, 0.f, 0.f), EButtonKind::Quiet, [this]() { SetConfirmOpen(false); });
	FButtonStyle ScrimStyle = Scrim->GetStyle();
	ScrimStyle.Normal = ScrimStyle.Hovered = ScrimStyle.Pressed = Flat(FrontendStyle::Scrim());
	Scrim->SetStyle(ScrimStyle);
	Add(Confirm, Scrim);

	UVerticalBox* Col = VBox(T);
	Add(Col, Eyebrow(T, TEXT("CRICKET 26 / LIVE MATCH"), Gold()), FMargin(0.f, 0.f, 0.f, S2));
	Add(Col, Text(T, TEXT("MATCH PAUSED"), 54, Ink(), EWeight::Black), FMargin(0.f, 0.f, 0.f, S1));
	PauseTeams = Para(T, TEXT("HOME XI  /  AWAY XI"), 20, InkDim());
	Add(Col, PauseTeams, FMargin(0.f, 0.f, 0.f, S3));
	Add(Col, CTA(T, TEXT("RESUME MATCH  ›"), EButtonKind::Primary, [this]() { SetConfirmOpen(false); }, 62.f));
	Add(Col, Eyebrow(T, TEXT("AUDIO"), InkDim()), FMargin(0.f, S3, 0.f, S1));
	UFrontendSettingsSave* Settings = UFrontendSettingsSave::Get();
	Add(Col, Slider(T, Settings->MasterVolume, [Settings](float Value)
	{
		Settings->MasterVolume = Value;
		Settings->ApplyGlobal();
		Settings->Persist();
	}));
	Add(Col, Para(T, TEXT("Bat: choose shot and time contact. Bowl: set line, length and release."), 16, InkDim()), FMargin(0.f, S3, 0.f, S3));
	UHorizontalBox* Actions = HBox(T);
	Add(Actions, CTA(T, TEXT("RESTART"), EButtonKind::Secondary, [this]()
	{
		UGameplayStatics::SetGamePaused(this, false);
		if (ASuperOverGameMode* GM = GetWorld()->GetAuthGameMode<ASuperOverGameMode>())
		{
			// Eleven-a-side: replay this exact fixture or quick match (or leave a committed fixture to the hub).
			if (GM->IsIPLMatch()) { GM->RestartIPLFixture(); return; }
			UFrontendStatics::OpenMatch(this, GM->SelectedMatchOvers);
		}
	}, 56.f), FMargin(0.f, 0.f, S2, 0.f));
	Add(Actions, CTA(T, TEXT("EXIT MATCH"), EButtonKind::Ghost, [this]()
	{
		UGameplayStatics::SetGamePaused(this, false);
		if (ASuperOverGameMode* GM = GetWorld()->GetAuthGameMode<ASuperOverGameMode>(); GM && GM->IsSeasonMatch())
			UFrontendStatics::OpenFrontend(this, EFrontendTab::IPLSeason);
		else
			UFrontendStatics::OpenFrontend(this, EFrontendTab::Play);
	}, 56.f));
	Add(Col, Actions);
	Add(Confirm, Glow(T, Hex(0x2F6BFF, 0.45f), 1100.f), HAlign_Center, VAlign_Center);
	UVerticalBox* Framed = VBox(T);
	Add(Framed, Sized(T, Box(T, Flat(Gold())), 0.f, 4.f));
	Add(Framed, Box(T, Flat(Glass()), FMargin(S5), Col));
	Add(Confirm, Framed, HAlign_Center, VAlign_Center);
	Add(Root, Confirm);
	ResultLayer = Stack(T);
	ResultLayer->SetVisibility(ESlateVisibility::Collapsed);
	Add(Root, ResultLayer);
	WidgetTree->RootWidget = Root;
	SetConfirmOpen(false);
	// ponytail: 4 Hz result poll; bind the match event if UI polling shows up in device profiles.
	GetWorld()->GetTimerManager().SetTimer(ResultPoll, this, &UFrontendMatchOverlay::CheckForResult, 0.25f, true);
#if !UE_BUILD_SHIPPING
	FString PauseShot;
	if (FParse::Value(FCommandLine::Get(), TEXT("FrontendPauseShot="), PauseShot))
	{
		SetConfirmOpen(true);
		FScreenshotRequest::RequestScreenshot(PauseShot, true, false);
	}
#endif
}

void UFrontendMatchOverlay::CheckForResult()
{
	ASuperOverGameMode* GM = GetWorld() ? Cast<ASuperOverGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	if (!GM || GM->Match.Phase != EMatchPhase::MatchComplete || !GM->ShowingScorecard()) return;
	GetWorld()->GetTimerManager().ClearTimer(ResultPoll);
	UWidgetTree* T = WidgetTree;
	ResultLayer->ClearChildren();
	Add(ResultLayer, Box(T, Flat(Scrim())));
	UVerticalBox* Col = VBox(T);
	Add(Col, Eyebrow(T, TEXT("CRICKET 26  /  MATCH RESULT"), Gold()), FMargin(0.f, 0.f, 0.f, S2));
	// Victory is the user's side winning (HumanTeam is 0 in standalone, so that read is unchanged).
	const bool bWon = GM->Match.Winner == GM->HumanTeam;
	const bool bIPL = GM->IsSeasonMatch();
	const bool bQuick = GM->bQuickMatch;
	const FString Winner = GM->Match.Winner >= 0 && GM->Match.Winner < 2 ? GM->Teams[GM->Match.Winner].Name.ToUpper() : TEXT("MATCH TIED");
	Add(Col, Eyebrow(T, bWon ? TEXT("VICTORY") : TEXT("FINAL RESULT"), bWon ? Gold() : InkDim()));
	Add(Col, Text(T, Winner, 84, Ink(), EWeight::Black), FMargin(0.f, 4.f, 0.f, S3));
	Add(Col, Sized(T, Box(T, Flat(Gold())), 110.f, 4.f), FMargin(0.f, 0.f, 0.f, S3), 0.f, HAlign_Left);
	for (const FInningsState& Inn : GM->Match.Innings)
	{
		UHorizontalBox* Score = HBox(T);
		Add(Score, Text(T, GM->Teams[Inn.BattingTeam].Name.ToUpper(), 23, Ink(), EWeight::Bold), FMargin(0.f), 1.f, VAlign_Center);
		Add(Score, Text(T, FString::Printf(TEXT("%d/%d  (%d.%d)"), Inn.Runs, Inn.Wickets, Inn.LegalBalls / 6, Inn.LegalBalls % 6), 26, Gold(), EWeight::Black), FMargin(0.f), 0.f, VAlign_Center);
		Add(Score, Sized(T, Box(T, Flat(Inn.BattingTeam == 0 ? Blue() : Red())), 6.f, 40.f), FMargin(S2, 0.f, 0.f, 0.f), 0.f, VAlign_Center);
		Add(Col, Box(T, Rounded(Hex(0x050913, 0.7f), 4.f, Line()), FMargin(S3, S2), Score), FMargin(0.f, 0.f, 0.f, S1));
	}
	int32 BestRuns = -1, BestBalls = 0;
	FString BestName;
	for (const FInningsState& Inn : GM->Match.Innings)
	{
		const FCricketTeam& Team = GM->Teams[Inn.BattingTeam];
		for (int32 I = 0; I < FMath::Min(Inn.Batters.Num(), Team.Batters.Num()); ++I)
		{
			if (Inn.Batters[I].Runs <= BestRuns) continue;
			BestRuns = Inn.Batters[I].Runs;
			BestBalls = Inn.Batters[I].Balls;
			BestName = Team.Batters[I].Name;
		}
	}
	if (BestRuns >= 0)
		Add(Col, Text(T, FString::Printf(TEXT("TOP SCORE  /  %s  %d (%d)"), *BestName.ToUpper(), BestRuns, BestBalls), 17, InkDim(), EWeight::Bold), FMargin(0.f, S2));
	Add(Col, Eyebrow(T, GM->Match.Rules.MaxLegalBalls == 6 ? TEXT("THE DECIDER  /  SUPER OVER") : TEXT("CRICKET 26  /  MATCH"), InkDim()), FMargin(0.f, S2, 0.f, S3));
	UHorizontalBox* Actions = HBox(T);
	// IPL season: the result already belongs to the season, so REMATCH becomes the season hub. A quick match's rematch
	// goes back to match setup with the same teams, so the new match gets its own toss.
	Add(Actions, CTA(T, bIPL ? TEXT("SEASON HUB  ›") : TEXT("REMATCH  ›"), EButtonKind::Primary, [this, bIPL, bQuick, Overs = GM->SelectedMatchOvers]()
	{
		if (bIPL) UFrontendStatics::OpenFrontend(this, EFrontendTab::IPLSeason);
		else if (bQuick) UFrontendStatics::OpenFrontend(this, EFrontendTab::MatchSetup);
		else UFrontendStatics::OpenMatch(this, Overs);
	}, 62.f), FMargin(0.f, 0.f, S2, 0.f));
	Add(Actions, CTA(T, TEXT("CONTINUE"), EButtonKind::Secondary, [this, bIPL]()
	{
		UFrontendStatics::OpenFrontend(this, bIPL ? EFrontendTab::IPLSeason : EFrontendTab::Play);
	}, 62.f));
	Add(Col, Actions);
	Add(ResultLayer, Box(T, Flat(Bg())));
	Add(ResultLayer, Backdrop(T, TEXT("T_CRICKET26_Stadium_Night"), Hex(0x8C9CC0)));
	Add(ResultLayer, Box(T, Flat(Hex(0x050913, 0.6f))));
	Add(ResultLayer, Glow(T, bWon ? Hex(0xF2B632, 0.4f) : Hex(0x2F6BFF, 0.4f), 1300.f), HAlign_Center, VAlign_Center);
	UVerticalBox* Framed = VBox(T);
	Add(Framed, Sized(T, Box(T, Flat(bWon ? Gold() : InkFaint())), 0.f, 4.f));
	Add(Framed, Box(T, Flat(Glass()), FMargin(S5), Col));
	Add(ResultLayer, Sized(T, Framed, 900.f, 0.f), HAlign_Center, VAlign_Center);
	ResultLayer->SetVisibility(ESlateVisibility::Visible);
}

void UFrontendMatchOverlay::SetConfirmOpen(bool bOpen)
{
	if (Confirm) Confirm->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	// The sides are only known once the match has started, after this overlay was built.
	const ASuperOverGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ASuperOverGameMode>() : nullptr;
	if (bOpen && PauseTeams && GM && GM->Teams.Num() == 2)
		PauseTeams->SetText(FText::FromString(FString::Printf(TEXT("%s  /  %s"), *GM->Teams[0].Name.ToUpper(), *GM->Teams[1].Name.ToUpper())));
	UGameplayStatics::SetGamePaused(this, bOpen);
}

bool UFrontendMatchOverlay::IsConfirmOpen() const
{
	return Confirm && Confirm->GetVisibility() == ESlateVisibility::Visible;
}

bool UFrontendMatchSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UFrontendMatchSubsystem::WantsOverlay(const TCHAR* CommandLine)
{
	if (FParse::Param(CommandLine, TEXT("FrontendForceOverlay"))) return true;
	return !FParse::Param(CommandLine, TEXT("CricketAutoPlay")) && !FParse::Param(CommandLine, TEXT("CricketTouchScript"));
}

void UFrontendMatchSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!Cast<ASuperOverGameMode>(InWorld.GetAuthGameMode()) || !WantsOverlay(FCommandLine::Get())) return;
	if (APlayerController* PC = InWorld.GetFirstPlayerController())
	{
		if (UFrontendMatchOverlay* Overlay = CreateWidget<UFrontendMatchOverlay>(PC, UFrontendMatchOverlay::StaticClass()))
		{
			Overlay->AddToViewport(50);
			UFrontendStatics::ScheduleDevShot(PC, 12.f); // the stadium streams in first
		}
	}
}
