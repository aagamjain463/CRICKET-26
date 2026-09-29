// Root of the frontend shell, built entirely in C++ (no widget Blueprint): splash, side navigation rail,
// top bar, one page per EFrontendTab, a modal sheet layer and the pre-match transition. Screens are built by
// FrontendScreens and talk back only through the public calls below.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FrontendTypes.h"
#include "RealTeams.h"
#include "FrontendRoot.generated.h"

class UBorder;
class UFrontendButton;
class UOverlay;
class USizeBox;
class UTextBlock;
class UWidgetSwitcher;

/** Match setup's steps: which competition, then the two teams, the user's playing XI, and the toss. */
enum class EQuickStep : uint8 { Competition, Teams, PlayingXI, Toss };

UCLASS()
class UFrontendRoot : public UUserWidget
{
	GENERATED_BODY()

public:
	// Call before AddToViewport. The splash plays once per app session, not on every return from a match.
	void Configure(EFrontendTab StartTab, bool bAllowSplash);

	void ShowTab(EFrontendTab Tab);
	EFrontendTab CurrentTab() const { return Tab; }

	// Back button / Esc: closes the sheet, else returns to Home. False when there is nothing to go back to.
	bool Back();

	struct FSheet
	{
		FString Eyebrow, Title, Body;
		TArray<FString> Bullets;
		EFeatureStatus Status = EFeatureStatus::ComingSoon;
		bool bShowStatus = true;
		FString PrimaryLabel; // empty: only a close button
		TFunction<void()> OnPrimary;
		FString CloseLabel = TEXT("Close");
	};
	void ShowSheet(const FSheet& Sheet);
	void ShowFeature(const FFrontendFeature& Feature); // the detail sheet for a not-yet-built feature
	void CloseSheet();
	bool IsSheetOpen() const { return bSheetOpen; }

	// Covers the shell with the pre-match card for a moment, then travels into the Super Over.
	void StartSuperOver();
	/** The format screen's pick: on to match setup, starting with the competition. */
	void SelectMatchOvers(int32 Overs);
	int32 MatchOvers() const { return SelectedOvers; }

	// ---- Quick match between two real teams (the choices persist in UFrontendSettingsSave) ----
	EQuickStep QuickStep = EQuickStep::Competition;
	/** The competition picked in match setup (kept in the settings save). */
	ECompetition QuickCompetition() const;
	TArray<int32> QuickXIOrder; // the user's XI as tapped, in batting order (player ids); empty: the team's real XI
	void ChooseCompetition(uint8 Competition);
	/** Steps the user's team (or the opponent) through the competition's teams, never onto the other side's. */
	void StepQuickTeam(bool bOpponent, int32 Delta);
	void ShowQuickStep(EQuickStep Step);
	void ToggleQuickXIPlayer(int32 PlayerId);
	void ResetQuickXI();
	/** The XI the user's team takes in: the edited one when it is a full, valid XI, else their real XI. */
	FIPLPlayingXI QuickUserXI() const;
	/** Stages the quick match with the toss's result and travels into it. */
	void StartQuickMatch();

	// ---- The toss, before every match the user plays (quick match, or an IPL season fixture) ----
	int32 TossStage = 0;          // 0: call it; 1: the user won and chooses; 2: decided
	bool bTossForSeason = false;  // the toss is for the staged season fixture, not a quick match
	bool bTossHeads = false, bUserWonToss = false, bUserBatsFirst = true;
	void BeginToss(bool bForSeason);
	void CallToss(bool bHeads);
	void ElectToBat(bool bBat);
	/** Plays the match the toss was for. */
	void ConfirmToss();
	/** The two sides the toss is between, for its panel: the user's first. */
	void TossTeams(FString& OutUser, FString& OutOpponent) const;

	/** Rebuilds one page from the current state (steps that change what a page shows). */
	void RefreshPage(EFrontendTab Page);
	bool IsStartingMatch() const { return LoadingT >= 0.f; }

	// ---- IPL season hub (transient UI state; the season itself lives in UIPLSeasonSave) ----
	int32 IPLView = 0; // 0 hub, 1 team select, 2 the toss
	int32 IPLSelectFixture = INDEX_NONE;
	TArray<int32> IPLXIOrder; // tapped player ids in batting order
	int32 IPLLoadingFixture = INDEX_NONE;
	void OpenSeasonHub();
	void OpenTeamSelect(int32 FixtureId);
	void ToggleXIPlayer(int32 PlayerId);
	void StartIPLMatch();
	void SimulateIPLFixture(int32 FixtureId);
	void RefreshSeasonHub();

	// A widget the shell breathes gently (the home hero glow).
	void SetPulse(UWidget* W) { Pulse = W; }

	// FIFA 26 Mobile kinetic transition: plays dynamic angled chevron wipe with badge punch
	void PlayTransition(TFunction<void()> OnMidpoint, const FString& ModeTitle = TEXT("CRICKET 26"), const FString& ModeSubtitle = TEXT("LOADING..."));
	void ShowTabDirect(EFrontendTab Tab);
	void OpenAuctionWithTransition();
	/** The career's next auction, after a finished IPL season. */
	void OpenCareerAuctionWithTransition();

protected:
	// Builds the shell here, not in NativeOnInitialized, which UMG skips when there is no player context
	// (automation tests create the shell without a local player).
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

private:
	EFrontendTab Tab = EFrontendTab::Home;
	int32 SelectedOvers = 1;
	EFrontendTab StartTab = EFrontendTab::Home;
	bool bSplash = true;
	bool bSheetOpen = false;

	UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages;
	UPROPERTY(Transient) TArray<TObjectPtr<UFrontendButton>> NavButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> NavLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> NavIcons;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> NavBars;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PageEyebrow;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PageTitle;
	UPROPERTY(Transient) TObjectPtr<UOverlay> SheetLayer;
	UPROPERTY(Transient) TObjectPtr<UWidget> SheetPanel;
	UPROPERTY(Transient) TObjectPtr<UBorder> Splash;
	UPROPERTY(Transient) TObjectPtr<UWidget> SplashLogo;
	UPROPERTY(Transient) TObjectPtr<UWidget> SplashBar;
	UPROPERTY(Transient) TObjectPtr<UBorder> Loading;
	UPROPERTY(Transient) TObjectPtr<UWidget> LoadingBar;
	UPROPERTY(Transient) TObjectPtr<UWidget> Pulse;
	UPROPERTY(Transient) TObjectPtr<UWidget> VersusLayer; // the split home/away plate behind match setup

	// FIFA 26 Mobile transition layer
	UPROPERTY(Transient) TObjectPtr<UOverlay> TransitionLayer;
	UPROPERTY(Transient) TObjectPtr<UWidget> TransitionBladeBlue;
	UPROPERTY(Transient) TObjectPtr<UWidget> TransitionBladeDark;
	UPROPERTY(Transient) TObjectPtr<UWidget> TransitionBladeGold;
	UPROPERTY(Transient) TObjectPtr<UWidget> TransitionGlint;
	UPROPERTY(Transient) TObjectPtr<UWidget> TransitionBadge;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TransitionTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TransitionSubtitle;
	UPROPERTY(Transient) TObjectPtr<USizeBox> TransitionBar;

	float TransitionT = -1.f;
	bool bTransitionSwitched = false;
	TFunction<void()> TransitionMidpoint;

	float PageT = 1.f;   // 0..1 page enter animation
	float SheetT = 0.f;  // 0..1 sheet open amount
	float SplashT = -1.f; // seconds since boot while the splash shows, else < 0
	float LoadingT = -1.f; // seconds since StartSuperOver, else < 0
	float Clock = 0.f;
	float PageOpened = 0.f; // Clock when the current page opened, for staggered entrances
	bool bTravelled = false;

	UWidget* BuildRail();
	UWidget* BuildTopBar();
	UWidget* BuildSplash();
	UWidget* BuildLoading();
	UWidget* BuildTransition();
	void RefreshNav();
};
