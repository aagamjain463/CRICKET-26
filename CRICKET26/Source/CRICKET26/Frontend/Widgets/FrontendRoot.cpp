#include "Widgets/FrontendRoot.h"
#include "Widgets/FrontendScreens.h"
#include "Widgets/FrontendUI.h"
#include "FrontendData.h"
#include "FrontendSettingsSave.h"
#include "FrontendStatics.h"
#include "IPLSeason.h"
#include "IPLSeasonSave.h"
#include "IPLPending.h"
#include "IPLMatchAdapter.h"
#include "AuctionTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/SafeZone.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Texture2D.h"
#include "UnrealClient.h"
#include "TimerManager.h"

using namespace FrontendStyle;
using namespace FrontendUI;

namespace
{
	float EaseOut(float T) { T = FMath::Clamp(T, 0.f, 1.f); return 1.f - FMath::Cube(1.f - T); }

	constexpr float SplashLength = 0.65f, SplashFade = 0.25f, LoadingHold = 0.6f;
}

bool UFrontendRoot::Initialize()
{
	if (!Super::Initialize()) return false; // already built
	UWidgetTree* T = WidgetTree;

	// The brand plate sits behind every page: the stadium out of focus, graded night royal, floodlights blooming
	// gold. Match setup swaps in the home-blue / away-red split of the same ground.
	UOverlay* Root = Stack(T);
	Add(Root, Box(T, Flat(Bg())));
	Add(Root, Backdrop(T, TEXT("T_CRICKET26_Shell_Bg")));
	VersusLayer = Backdrop(T, TEXT("T_CRICKET26_VS_Bg"));
	Add(Root, VersusLayer);
	Add(Root, Sized(T, Fade(T, Hex(0x03061A, 0.9f), true), 0.f, 420.f), HAlign_Fill, VAlign_Bottom);
	Add(Root, Sized(T, Box(T, Flat(Hex(0x03061A, 0.55f))), 0.f, 120.f), HAlign_Fill, VAlign_Top);
	UWidget* Streaks = Backdrop(T, TEXT("T_CRICKET26_Streaks"), Hex(0xFFFFFF, 0.35f));
	Add(Root, Streaks);
	Animate(Streaks, EMotion::Pulse);
	USafeZone* Safe = T->ConstructWidget<USafeZone>();
	UVerticalBox* Shell = VBox(T);
	Add(Shell, BuildTopBar(), FMargin(S4, S2, S4, S2 + 4.f));
	Pages = T->ConstructWidget<UWidgetSwitcher>();
	for (uint8 I = 0; I < uint8(EFrontendTab::Count); ++I)
	{
		// Hub pages fill the screen like a console menu; only long lists scroll.
		const EFrontendTab Page = EFrontendTab(I);
		UBorder* Pad = Box(T, Flat(FLinearColor::Transparent), FMargin(S4, 0.f, S4, S2), FrontendScreens::Build(this, Page));
		if (Page == EFrontendTab::Settings || Page == EFrontendTab::Franchise || Page == EFrontendTab::Scouts
			|| Page == EFrontendTab::IPLSeason)
		{
			UScrollBox* Scroll = T->ConstructWidget<UScrollBox>();
			Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
			Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
			Scroll->AddChild(Pad);
			Pages->AddChild(Scroll);
		}
		else
		{
			Pages->AddChild(Pad);
		}
	}
	Add(Shell, Pages, FMargin(0.f), 1.f);
	Add(Shell, BuildRail());
	Safe->SetContent(Shell);
	Add(Root, Safe);

	SheetLayer = Stack(T);
	SheetLayer->SetVisibility(ESlateVisibility::Collapsed);
	Add(Root, SheetLayer);

	Loading = Box(T, Flat(Bg()));
	Loading->SetVisibility(ESlateVisibility::Collapsed);
	Add(Root, Loading);

	TransitionLayer = Stack(T);
	TransitionLayer->SetVisibility(ESlateVisibility::Collapsed);
	Add(TransitionLayer, BuildTransition());
	Add(Root, TransitionLayer);

	Add(Root, BuildSplash());
	WidgetTree->RootWidget = Root;
	RefreshNav();
	return true;
}

void UFrontendRoot::Configure(EFrontendTab InStartTab, bool bAllowSplash)
{
	bSplash = bAllowSplash;
	Splash->SetVisibility(bSplash ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	SplashT = bSplash ? 0.f : -1.f;
	ShowTab(InStartTab);
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("FrontendAutoStart")))
	{
		Splash->SetVisibility(ESlateVisibility::Collapsed);
		SplashT = -1.f;
		StartSuperOver();
	}
#endif
}

namespace
{
	// Tab bar order; RefreshNav walks the same list.
	const EFrontendTab NavOrder[] = { EFrontendTab::Home, EFrontendTab::Play, EFrontendTab::Franchise, EFrontendTab::Auction, EFrontendTab::Store };
}

UWidget* UFrontendRoot::BuildRail()
{
	// Bottom tab bar in the FC Mobile manner: a dark glass strip of equal tabs, icon and label side by side, hairline
	// separators, and the current tab lit from below in gold with a gold rule. Nothing here competes with the page's
	// one gold action.
	UWidgetTree* T = WidgetTree;
	UHorizontalBox* Row = HBox(T);
	auto NavItem = [this, T, Row](EFrontendTab Item, const TCHAR* NavText, TCHAR NavGlyph)
	{
		UOverlay* Cell = Stack(T);
		UOverlay* Active = Stack(T); // shown only on the current tab
		Add(Active, Fade(T, Hex(0xF2B632, 0.30f), true));
		Add(Active, Glow(T, Hex(0xFFD56B, 0.30f), 260.f), HAlign_Center, VAlign_Bottom, FMargin(0.f, 0.f, 0.f, -150.f));
		Add(Active, Sized(T, Box(T, Flat(Gold())), 0.f, 4.f), HAlign_Fill, VAlign_Bottom);
		Add(Cell, Active);
		UHorizontalBox* Inline = HBox(T);
		UTextBlock* Mark = Icon(T, NavGlyph, 30, InkDim());
		UTextBlock* Name = Text(T, NavText, 21, InkDim(), EWeight::Black, 80);
		Add(Inline, Mark, FMargin(0.f, 0.f, 10.f, 0.f), 0.f, VAlign_Center);
		Add(Inline, Name, FMargin(0.f), 0.f, VAlign_Center);
		Add(Cell, Inline, HAlign_Center, VAlign_Center);
		UFrontendButton* B = Button(T, Sized(T, Cell, 0.f, 84.f), EButtonKind::Quiet, [this, Item]() { ShowTab(Item); }, 0.f);
		if (Row->GetChildrenCount() > 0) Add(Row, Sized(T, Box(T, Flat(Line())), 1.f, 36.f), FMargin(0.f), 0.f, VAlign_Center);
		NavButtons.Add(B);
		NavLabels.Add(Name);
		NavIcons.Add(Mark);
		NavBars.Add(Active);
		Add(Row, B, FMargin(0.f), 1.f, VAlign_Bottom);
	};
	NavItem(EFrontendTab::Home, TEXT("HOME"), Glyph::Home);
	NavItem(EFrontendTab::Play, TEXT("PLAY"), Glyph::Cricket);
	NavItem(EFrontendTab::Franchise, TEXT("CLUB"), Glyph::Shield);
	NavItem(EFrontendTab::Auction, TEXT("LIVE"), Glyph::Live);
	NavItem(EFrontendTab::Store, TEXT("STORE"), Glyph::Store);

	UOverlay* Strip = Stack(T);
	Add(Strip, Box(T, Flat(Hex(0x050A1E, 0.92f))));
	Add(Strip, Fade(T, Hex(0x1E3CC8, 0.18f), true));
	Add(Strip, Sized(T, Box(T, Flat(Hex(0xA9C2FF, 0.22f))), 0.f, 1.f), HAlign_Fill, VAlign_Top);
	Add(Strip, Row, HAlign_Fill, VAlign_Bottom, FMargin(S5, 0.f));
	return Strip;
}

UWidget* UFrontendRoot::BuildTopBar()
{
	UWidgetTree* T = WidgetTree;
	UHorizontalBox* Row = HBox(T);
	Add(Row, Logo(T, 1.3f), FMargin(0.f, 0.f, S4, 0.f), 0.f, VAlign_Center);

	// Club chip: crest, club name and role, the way sports games show the player's profile.
	UHorizontalBox* Club = HBox(T);
	Add(Club, Picture(T, TEXT("T_CRICKET26_Crest_Home"), 54.f), FMargin(0.f, 0.f, 12.f, 0.f), 0.f, VAlign_Center);
	UVerticalBox* ClubText = VBox(T);
	Add(ClubText, Text(T, TEXT("HOME XI"), 22, Ink(), EWeight::Black, 30));
	Add(ClubText, Text(T, TEXT("CAPTAIN  /  SEASON 26"), 13, Teal(), EWeight::Bold, 160), FMargin(0.f, -4.f, 0.f, 0.f));
	Add(Club, ClubText, FMargin(0.f), 0.f, VAlign_Center);
	UFrontendButton* ClubChip = Button(T, Box(T, Rounded(Glass(), RPill, Line()), FMargin(8.f, 4.f, S4, 4.f), Club), EButtonKind::Quiet,
		[this]() { ShowTab(EFrontendTab::Franchise); }, RPill);
	Add(Row, ClubChip, FMargin(0.f), 0.f, VAlign_Center);

	Add(Row, Space(T, 0.f, 0.f), FMargin(0.f), 1.f);

	// Squad rating on the gold OVR shield: the mean of the real squad ratings, never an invented progression number.
	int32 Sum = 0;
	const TArray<FFranchisePlayerRow> Squad = FrontendData::Squad();
	for (const FFranchisePlayerRow& P : Squad) Sum += P.Rating;
	UHorizontalBox* Ovr = HBox(T);
	UOverlay* Shield = Stack(T);
	Add(Shield, Picture(T, TEXT("T_CRICKET26_Ovr_Badge"), 56.f));
	Add(Shield, Text(T, FString::FromInt(Squad.Num() ? FMath::RoundToInt(float(Sum) / Squad.Num()) : 0), 24, GoldHi(), EWeight::Black),
		HAlign_Center, VAlign_Center, FMargin(0.f, 0.f, 0.f, 6.f));
	Add(Ovr, Shield, FMargin(0.f, 0.f, 10.f, 0.f), 0.f, VAlign_Center);
	UVerticalBox* OvrText = VBox(T);
	Add(OvrText, Text(T, TEXT("SQUAD"), 13, InkDim(), EWeight::Bold, 200));
	Add(OvrText, Text(T, TEXT("OVR"), 20, Ink(), EWeight::Black, 60), FMargin(0.f, -4.f, 0.f, 0.f));
	Add(Ovr, OvrText, FMargin(0.f), 0.f, VAlign_Center);
	Add(Row, Box(T, Rounded(Glass(), RPill, Line()), FMargin(8.f, 2.f, S3, 2.f), Ovr), FMargin(0.f, 0.f, S2, 0.f), 0.f, VAlign_Center);

	UOverlay* Gear = Stack(T);
	Add(Gear, Box(T, Rounded(Glass(), RPill, Line())));
	Add(Gear, Icon(T, Glyph::Settings, 30, Ink()), HAlign_Center, VAlign_Center);
	Add(Row, Button(T, Sized(T, Gear, 60.f, 60.f), EButtonKind::Quiet, [this]() { ShowTab(EFrontendTab::Settings); }, RPill),
		FMargin(0.f), 0.f, VAlign_Center);
	return Row;
}

UWidget* UFrontendRoot::BuildSplash()
{
	UWidgetTree* T = WidgetTree;
	UOverlay* Layers = Stack(T);
	Add(Layers, Backdrop(T, TEXT("T_CRICKET26_Shell_Bg")));
	Add(Layers, Box(T, Flat(Hex(0x03061A, 0.35f))));
	Add(Layers, Glow(T, Hex(0xF2B632, 0.30f), 1300.f), HAlign_Center, VAlign_Center);
	Add(Layers, Backdrop(T, TEXT("T_CRICKET26_Streaks"), Hex(0xFFFFFF, 0.8f)));

	UVerticalBox* Col = VBox(T);
	Add(Col, Logo(T, 2.6f), FMargin(0.f, 0.f, 0.f, S5), 0.f, HAlign_Center);
	Add(Col, Text(T, FrontendData::Tagline().ToUpper(), 22, InkDim(), EWeight::Bold, 300), FMargin(0.f, 0.f, 0.f, S5), 0.f, HAlign_Center);

	USizeBox* Fill = Cast<USizeBox>(Sized(T, Box(T, Flat(Gold())), 1.f, 4.f));
	UHorizontalBox* Track = HBox(T);
	Add(Track, Fill);
	Add(Col, Sized(T, Box(T, Flat(FLinearColor(1.f, 1.f, 1.f, 0.12f)), FMargin(0.f), Track), 320.f, 4.f), FMargin(0.f), 0.f, HAlign_Center);
	SplashLogo = Col;
	SplashBar = Fill;
	Add(Layers, Col, HAlign_Center, VAlign_Center);

	Splash = Box(T, Flat(Bg()), FMargin(0.f), Layers);
	return Splash;
}

UWidget* UFrontendRoot::BuildLoading()
{
	UWidgetTree* T = WidgetTree;
	const UFrontendSettingsSave* S = UFrontendSettingsSave::Get();
	const TArray<FString> Venues = FrontendData::VenueNames();
	const FString Venue = Venues.IsValidIndex(S->Venue + 1) ? Venues[S->Venue + 1] : Venues[0];
	const TArray<FString> Levels = FrontendData::DifficultyNames();
	const FString Level = Levels.IsValidIndex(S->Difficulty) ? Levels[S->Difficulty] : FString();
	UOverlay* Layers = Stack(T);
	Layers->SetClipping(EWidgetClipping::ClipToBounds);
	Add(Layers, Backdrop(T, TEXT("T_CRICKET26_VS_Bg")));
	Add(Layers, Backdrop(T, TEXT("T_CRICKET26_Streaks"), Hex(0xFFFFFF, 0.35f)));
	Add(Layers, Versus(T, 900.f), HAlign_Fill, VAlign_Bottom, FMargin(0.f, 0.f, 0.f, 0.f));
	Add(Layers, Sized(T, Fade(T, Hex(0x050913, 1.f), true), 0.f, 420.f), HAlign_Fill, VAlign_Bottom);

	Add(Layers, Text(T, IPLLoadingFixture != INDEX_NONE ? TEXT("IPL  /  MATCH DAY") : (SelectedOvers == 1 ? TEXT("SUPER OVER  /  MATCH DAY") : TEXT("CRICKET 26  /  MATCH DAY")), 20, Gold(), EWeight::Bold, 300), HAlign_Center, VAlign_Top, FMargin(0.f, 70.f));
	UHorizontalBox* Plates = HBox(T);
	Add(Plates, NamePlate(T, TEXT("HOME XI"), TEXT("Batting first"), Blue(), false), FMargin(0.f), 1.f, VAlign_Bottom);
	Add(Plates, NamePlate(T, TEXT("AWAY XI"), TEXT("Bowling first"), Red(), true), FMargin(0.f), 1.f, VAlign_Bottom);
	Add(Layers, Plates, HAlign_Fill, VAlign_Bottom, FMargin(110.f, 0.f, 110.f, 170.f));

	UVerticalBox* Foot = VBox(T);
	Add(Foot, Text(T, FString::Printf(TEXT("%s  /  %s AI"), *Venue.ToUpper(), *Level.ToUpper()), 22, Ink(), EWeight::Bold, 200),
		FMargin(0.f, 0.f, 0.f, S2), 0.f, HAlign_Center);
	USizeBox* Bar = Cast<USizeBox>(Sized(T, Box(T, Flat(Gold())), 1.f, 5.f));
	LoadingBar = Bar;
	UHorizontalBox* Track = HBox(T);
	Add(Track, Bar);
	Add(Foot, Sized(T, Box(T, Flat(FLinearColor(1.f, 1.f, 1.f, 0.12f)), FMargin(0.f), Track), 420.f, 5.f), FMargin(0.f), 0.f, HAlign_Center);
	Add(Foot, Text(T, TEXT("TEAMS TAKING THE FIELD"), 14, InkDim(), EWeight::Bold, 260), FMargin(0.f, S2, 0.f, 0.f), 0.f, HAlign_Center);
	Add(Layers, Foot, HAlign_Center, VAlign_Bottom, FMargin(0.f, 0.f, 0.f, 56.f));
	return Box(T, Flat(Bg()), FMargin(0.f), Layers);
}

UWidget* UFrontendRoot::BuildTransition()
{
	UWidgetTree* T = WidgetTree;
	UOverlay* Root = Stack(T);
	Root->SetClipping(EWidgetClipping::ClipToBounds);

	// Blades container (centered, sheared to give the high-speed sports broadcast angle)
	UOverlay* Blades = Stack(T);
	Blades->SetRenderShear(FVector2D(-0.28f, 0.f));
	Blades->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));

	// Carbon dark blade: spans 3800x1600 to completely occlude the viewport
	UBorder* DarkPlate = Box(T, Flat(Hex(0x040816, 1.0f)));
	UWidget* DarkSized = Sized(T, DarkPlate, 3800.f, 1600.f);
	Add(Blades, DarkSized, HAlign_Center, VAlign_Center);

	// Leading edge energy blades (sheared together with the assembly)
	// Right edge of dark plate is at +1900.f. We place accents along the leading edge.
	TransitionBladeBlue = Sized(T, Box(T, Flat(Hex(0x1A44D8, 0.95f))), 160.f, 1600.f);
	TransitionBladeBlue->SetRenderTranslation(FVector2D(1980.f, 0.f));
	Add(Blades, TransitionBladeBlue, HAlign_Center, VAlign_Center);

	TransitionBladeGold = Sized(T, Box(T, Flat(GoldHi())), 40.f, 1600.f);
	TransitionBladeGold->SetRenderTranslation(FVector2D(2080.f, 0.f));
	Add(Blades, TransitionBladeGold, HAlign_Center, VAlign_Center);

	TransitionGlint = Sized(T, Box(T, Flat(Hex(0xFFFFFF, 0.92f))), 10.f, 1600.f);
	TransitionGlint->SetRenderTranslation(FVector2D(2105.f, 0.f));
	Add(Blades, TransitionGlint, HAlign_Center, VAlign_Center);

	// Trailing edge energy blades (left edge of dark plate is at -1900.f)
	UWidget* TrailGlint = Sized(T, Box(T, Flat(Hex(0xFFFFFF, 0.92f))), 10.f, 1600.f);
	TrailGlint->SetRenderTranslation(FVector2D(-2105.f, 0.f));
	Add(Blades, TrailGlint, HAlign_Center, VAlign_Center);

	UWidget* TrailGold = Sized(T, Box(T, Flat(GoldHi())), 40.f, 1600.f);
	TrailGold->SetRenderTranslation(FVector2D(-2080.f, 0.f));
	Add(Blades, TrailGold, HAlign_Center, VAlign_Center);

	UWidget* TrailBlue = Sized(T, Box(T, Flat(Hex(0x1A44D8, 0.95f))), 160.f, 1600.f);
	TrailBlue->SetRenderTranslation(FVector2D(-1980.f, 0.f));
	Add(Blades, TrailBlue, HAlign_Center, VAlign_Center);

	TransitionBladeDark = Blades;
	Add(Root, Blades, HAlign_Center, VAlign_Center);

	// FIFA 26 Mobile Center Badge
	UOverlay* BadgeLayers = Stack(T);
	BadgeLayers->SetClipping(EWidgetClipping::ClipToBounds);

	// Golden ambient aura behind the badge
	Add(BadgeLayers, Glow(T, Hex(0xF2B632, 0.35f), 520.f), HAlign_Center, VAlign_Center);

	// Dark glass card
	UVerticalBox* BadgeCol = VBox(T);

	// Eyebrow / Tournament chip
	UHorizontalBox* EyebrowRow = HBox(T);
	Add(EyebrowRow, Pill(T, TEXT("CRICKET 26"), GoldHi(), true), FMargin(0.f), 0.f, VAlign_Center);
	Add(BadgeCol, EyebrowRow, FMargin(0.f, 0.f, 0.f, S2), 0.f, HAlign_Center);

	// Mode title
	TransitionTitle = Text(T, TEXT("CRICKET 26"), 42, Ink(), EWeight::Black, 30);
	TransitionTitle->SetShadowOffset(FVector2D(0.f, 3.f));
	TransitionTitle->SetShadowColorAndOpacity(Hex(0x000000, 0.65f));
	Add(BadgeCol, TransitionTitle, FMargin(0.f, 0.f, 0.f, S1), 0.f, HAlign_Center);

	// Mode subtitle
	TransitionSubtitle = Text(T, TEXT("LOADING..."), 18, Gold(), EWeight::Bold, 80);
	Add(BadgeCol, TransitionSubtitle, FMargin(0.f, 0.f, 0.f, S3), 0.f, HAlign_Center);

	// Loading / energy bar
	USizeBox* Bar = Cast<USizeBox>(Sized(T, Box(T, Flat(GoldHi())), 1.f, 4.f));
	TransitionBar = Bar;
	UHorizontalBox* Track = HBox(T);
	Add(Track, Bar);
	Add(BadgeCol, Sized(T, Box(T, Flat(FLinearColor(1.f, 1.f, 1.f, 0.15f)), FMargin(0.f), Track), 440.f, 4.f),
		FMargin(0.f), 0.f, HAlign_Center);

	UBorder* Card = Box(T, Rounded(Hex(0x060B1C, 0.95f), 20.f, Hex(0xF2B632, 0.65f)), FMargin(S5, S3 + 4.f, S5, S3 + 4.f), BadgeCol);
	Add(BadgeLayers, Card, HAlign_Center, VAlign_Center);

	TransitionBadge = Sized(T, BadgeLayers, 660.f, 0.f);
	TransitionBadge->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	TransitionBadge->SetRenderOpacity(0.f);
	Add(Root, TransitionBadge, HAlign_Center, VAlign_Center);

	return Root;
}

void UFrontendRoot::PlayTransition(TFunction<void()> OnMidpoint, const FString& ModeTitle, const FString& ModeSubtitle)
{
	if (!TransitionLayer)
	{
		if (OnMidpoint) OnMidpoint();
		return;
	}

	if (Clock < 0.05f || !GetWorld() || !GetWorld()->IsGameWorld())
	{
		if (OnMidpoint) OnMidpoint();
		return;
	}

	if (TransitionT >= 0.f && !bTransitionSwitched && TransitionMidpoint)
	{
		TFunction<void()> OldMidpoint = MoveTemp(TransitionMidpoint);
		OldMidpoint();
	}

	TransitionMidpoint = OnMidpoint;
	bTransitionSwitched = false;
	TransitionT = 0.f;

	if (TransitionTitle) TransitionTitle->SetText(FText::FromString(ModeTitle.ToUpper()));
	if (TransitionSubtitle) TransitionSubtitle->SetText(FText::FromString(ModeSubtitle.ToUpper()));
	if (TransitionBar) TransitionBar->SetWidthOverride(1.f);
	if (TransitionBadge)
	{
		TransitionBadge->SetRenderOpacity(0.f);
		TransitionBadge->SetRenderScale(FVector2D(0.85f, 0.85f));
	}
	if (TransitionBladeDark)
	{
		TransitionBladeDark->SetRenderTranslation(FVector2D(-3800.f, 0.f));
	}

	TransitionLayer->SetVisibility(ESlateVisibility::Visible);
}

void UFrontendRoot::ShowTabDirect(EFrontendTab NewTab)
{
	if (NewTab >= EFrontendTab::Count) return;
	const bool bChanged = NewTab != Tab || Pages->GetActiveWidgetIndex() != int32(NewTab);
	Tab = NewTab;
	Pages->SetActiveWidgetIndex(int32(Tab));
	if (bChanged)
	{
		PageT = 0.f;
		PageOpened = Clock;
	}
	RefreshNav();
}

void UFrontendRoot::OpenAuctionWithTransition()
{
	PlayTransition([this]()
	{
		UFrontendStatics::OpenAuction(this);
	}, TEXT("MEGA AUCTION"), TEXT("THE ₹120 CR BIDDING WAR"));
}

void UFrontendRoot::ShowTab(EFrontendTab NewTab)
{
	if (NewTab >= EFrontendTab::Count) return;
	if (bSplash || Clock < 0.05f || NewTab == Tab)
	{
		ShowTabDirect(NewTab);
		return;
	}

	FString Title = TEXT("CRICKET 26");
	FString Subtitle = TEXT("LOADING...");
	switch (NewTab)
	{
	case EFrontendTab::Home:
		Title = TEXT("MAIN MENU");
		Subtitle = TEXT("CRICKET 26");
		break;
	case EFrontendTab::Play:
		Title = TEXT("MATCH MODES");
		Subtitle = TEXT("SELECT YOUR FORMAT");
		break;
	case EFrontendTab::MatchFormat:
		Title = TEXT("MATCH FORMAT");
		Subtitle = TEXT("QUICK PLAY & TOURNAMENTS");
		break;
	case EFrontendTab::MatchSetup:
		Title = TEXT("MATCH PREVIEW");
		Subtitle = TEXT("LINEUPS & MATCH CONDITIONS");
		break;
	case EFrontendTab::IPLSeason:
		Title = TEXT("TATA IPL 2026");
		Subtitle = TEXT("THE ROAD TO GLORY");
		break;
	case EFrontendTab::Auction:
		Title = TEXT("MEGA AUCTION");
		Subtitle = TEXT("₹120 CRORE PURSE • 10 FRANCHISES");
		break;
	case EFrontendTab::Franchise:
		Title = TEXT("FRANCHISE HQ");
		Subtitle = TEXT("SQUAD MANAGEMENT & CONTRACTS");
		break;
	case EFrontendTab::Scouts:
		Title = TEXT("SCOUTING NETWORK");
		Subtitle = TEXT("DISCOVER GLOBAL TALENT");
		break;
	case EFrontendTab::Store:
		Title = TEXT("CLUB STORE");
		Subtitle = TEXT("OFFICIAL KITS & CRICKET PASS");
		break;
	case EFrontendTab::Settings:
		Title = TEXT("SETTINGS");
		Subtitle = TEXT("AUDIO, GRAPHICS & GAMEPLAY");
		break;
	default:
		break;
	}

	Tab = NewTab;
	RefreshNav();

	PlayTransition([this, NewTab]()
	{
		ShowTabDirect(NewTab);
	}, Title, Subtitle);
}

void UFrontendRoot::RefreshNav()
{
	for (int32 I = 0; I < NavButtons.Num(); ++I)
	{
		const bool bOn = NavOrder[I] == Tab || ((Tab == EFrontendTab::MatchSetup || Tab == EFrontendTab::MatchFormat) && NavOrder[I] == EFrontendTab::Play)
			|| (Tab == EFrontendTab::Scouts && NavOrder[I] == EFrontendTab::Franchise);
		NavLabels[I]->SetColorAndOpacity(FSlateColor(bOn ? Ink() : InkFaint()));
		NavIcons[I]->SetColorAndOpacity(FSlateColor(bOn ? GoldHi() : InkFaint()));
		NavBars[I]->SetVisibility(bOn ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (VersusLayer) VersusLayer->SetVisibility(Tab == EFrontendTab::MatchSetup ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

bool UFrontendRoot::Back()
{
	if (IsStartingMatch()) return true;
	if (bSheetOpen)
	{
		CloseSheet();
		return true;
	}
	if (Tab == EFrontendTab::MatchSetup)
	{
		ShowTab(EFrontendTab::MatchFormat);
		return true;
	}
	if (Tab == EFrontendTab::MatchFormat)
	{
		ShowTab(EFrontendTab::Play);
		return true;
	}
	// IPL: the team-select view backs out to the hub, not to Home.
	if (Tab == EFrontendTab::IPLSeason && IPLView != 0)
	{
		IPLView = 0;
		RefreshSeasonHub();
		return true;
	}
	if (Tab != EFrontendTab::Home)
	{
		ShowTab(EFrontendTab::Home);
		return true;
	}
	return false;
}

void UFrontendRoot::ShowSheet(const FSheet& Sheet)
{
	UWidgetTree* T = WidgetTree;
	SheetLayer->ClearChildren();

	// Tapping the dimmed backdrop closes the sheet, like every mobile modal.
	UFrontendButton* Scrim = Button(T, nullptr, EButtonKind::Quiet, [this]() { CloseSheet(); }, 0.f);
	FButtonStyle ScrimStyle = Scrim->GetStyle();
	ScrimStyle.Normal = ScrimStyle.Hovered = ScrimStyle.Pressed = Flat(FrontendStyle::Scrim());
	Scrim->SetStyle(ScrimStyle);
	Scrim->SetRenderTransformPivot(FVector2D(0.5f));
	Scrim->OnPressed.Clear();
	Scrim->OnReleased.Clear();
	Add(SheetLayer, Scrim);

	UVerticalBox* Col = VBox(T);
	UHorizontalBox* Top = HBox(T);
	if (!Sheet.Eyebrow.IsEmpty()) Add(Top, Eyebrow(T, Sheet.Eyebrow, Gold()), FMargin(0.f), 1.f, VAlign_Center);
	if (Sheet.bShowStatus) Add(Top, Badge(T, Sheet.Status), FMargin(0.f), 0.f, VAlign_Center);
	Add(Col, Top, FMargin(0.f, 0.f, 0.f, S2));
	Add(Col, Text(T, Sheet.Title, H1, Ink(), EWeight::Black), FMargin(0.f, 0.f, 0.f, S1));
	if (!Sheet.Body.IsEmpty()) Add(Col, Para(T, Sheet.Body, Body), FMargin(0.f, 0.f, 0.f, S3));
	for (const FString& B : Sheet.Bullets)
	{
		UHorizontalBox* Line = HBox(T);
		Add(Line, Dot(T, Gold(), 8.f), FMargin(0.f, 0.f, S2, 0.f), 0.f, VAlign_Center);
		Add(Line, Para(T, B, 17, Ink()), FMargin(0.f), 1.f, VAlign_Center);
		Add(Col, Line, FMargin(0.f, 0.f, 0.f, 12.f));
	}
	UHorizontalBox* Actions = HBox(T);
	Add(Actions, Space(T, 0.f, 0.f), FMargin(0.f), 1.f);
	Add(Actions, CTA(T, Sheet.CloseLabel, EButtonKind::Ghost, [this]() { CloseSheet(); }, 56.f), FMargin(0.f), 0.f, VAlign_Center);
	if (!Sheet.PrimaryLabel.IsEmpty())
	{
		TFunction<void()> Primary = Sheet.OnPrimary;
		Add(Actions, CTA(T, Sheet.PrimaryLabel, EButtonKind::Primary, [this, Primary]()
		{
			CloseSheet();
			if (Primary) Primary();
		}, 56.f), FMargin(S2, 0.f, 0.f, 0.f), 0.f, VAlign_Center);
	}
	Add(Col, Actions, FMargin(0.f, S3, 0.f, 0.f));

	UOverlay* PanelLayers = Stack(T);
	PanelLayers->SetClipping(EWidgetClipping::ClipToBounds);
	FLinearColor GlowTint = Sheet.bShowStatus ? FrontendData::StatusTint(Sheet.Status) : Gold();
	GlowTint.A = 0.10f;
	Add(PanelLayers, Glow(T, GlowTint, 700.f), HAlign_Right, VAlign_Top, FMargin(0.f, -350.f, -300.f, 0.f));
	Add(PanelLayers, Col, HAlign_Fill, VAlign_Fill, FMargin(S5 - 8.f));
	UBorder* Panel = Box(T, Rounded(Surface(), 28.f, FLinearColor(1.f, 1.f, 1.f, 0.10f)), FMargin(0.f), PanelLayers);
	UWidget* Sized_ = Sized(T, Panel, 780.f, 0.f);
	Sized_->SetRenderTransformPivot(FVector2D(0.5f));
	Add(SheetLayer, Sized_, HAlign_Center, VAlign_Center);
	SheetPanel = Sized_;

	SheetLayer->SetVisibility(ESlateVisibility::Visible);
	bSheetOpen = true;
}

void UFrontendRoot::ShowFeature(const FFrontendFeature& F)
{
	FSheet S;
	S.Eyebrow = F.Eyebrow;
	S.Title = F.Title;
	S.Status = F.Status;
	S.Body = F.Status == EFeatureStatus::Available ? F.Subtitle
		: F.Subtitle + TEXT(" It is not in this build yet; here is what it will bring.");
	S.Bullets = F.Bullets;
	S.CloseLabel = TEXT("Got it");
	ShowSheet(S);
}

void UFrontendRoot::CloseSheet()
{
	bSheetOpen = false;
}

void UFrontendRoot::StartSuperOver()
{
	if (IsStartingMatch()) return;
	CloseSheet();
	Loading->SetContent(BuildLoading());
	Loading->SetRenderOpacity(0.f);
	Loading->SetVisibility(ESlateVisibility::Visible);
	LoadingT = 0.f;
	bTravelled = false;
#if !UE_BUILD_SHIPPING
	FString LoadingShot;
	if (FParse::Value(FCommandLine::Get(), TEXT("FrontendLoadingShot="), LoadingShot))
	{
		FTimerHandle ShotTimer;
		GetWorld()->GetTimerManager().SetTimer(ShotTimer, [LoadingShot]()
		{
			FScreenshotRequest::RequestScreenshot(LoadingShot, true, false);
		}, 0.35f, false);
	}
#endif
}

void UFrontendRoot::SelectMatchOvers(int32 Overs)
{
	SelectedOvers = Overs;
	ShowTab(EFrontendTab::MatchSetup);
}

void UFrontendRoot::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	const float Dt = FMath::Min(DeltaTime, 0.05f); // a hitch must not skip an animation
	Clock += Dt;
	TickMotion(Clock, Clock - PageOpened);

	if (PageT < 1.f)
	{
		PageT = FMath::Min(1.f, PageT + Dt / 0.28f);
		const float E = EaseOut(PageT);
		Pages->SetRenderOpacity(E);
		Pages->SetRenderTranslation(FVector2D(0.f, 20.f * (1.f - E)));
	}

	const float SheetTarget = bSheetOpen ? 1.f : 0.f;
	if (SheetT != SheetTarget)
	{
		SheetT = FMath::FInterpConstantTo(SheetT, SheetTarget, Dt, 1.f / 0.2f);
		const float E = EaseOut(SheetT);
		SheetLayer->SetRenderOpacity(E);
		if (SheetPanel)
		{
			SheetPanel->SetRenderScale(FVector2D(0.96f + 0.04f * E));
			SheetPanel->SetRenderTranslation(FVector2D(0.f, 16.f * (1.f - E)));
		}
		if (SheetT <= 0.f)
		{
			SheetLayer->SetVisibility(ESlateVisibility::Collapsed);
			SheetLayer->ClearChildren();
			SheetPanel = nullptr;
		}
	}

	if (SplashT >= 0.f)
	{
		SplashT += Dt;
		const float In = EaseOut(SplashT / 0.6f);
		SplashLogo->SetRenderOpacity(In);
		SplashLogo->SetRenderTranslation(FVector2D(0.f, 14.f * (1.f - In)));
		Cast<USizeBox>(SplashBar)->SetWidthOverride(FMath::Max(1.f, 320.f * FMath::SmoothStep(0.3f, SplashLength - 0.3f, SplashT)));
		if (SplashT > SplashLength)
		{
			Splash->SetRenderOpacity(1.f - EaseOut((SplashT - SplashLength) / SplashFade));
		}
		if (SplashT > SplashLength + SplashFade)
		{
			Splash->SetVisibility(ESlateVisibility::Collapsed);
			SplashT = -1.f;
			PageT = 0.f; // the first page enters as the splash lifts
			PageOpened = Clock;
		}
	}

	if (LoadingT >= 0.f)
	{
		LoadingT += Dt;
		Loading->SetRenderOpacity(EaseOut(LoadingT / 0.2f));
		if (USizeBox* Bar = Cast<USizeBox>(LoadingBar)) Bar->SetWidthOverride(FMath::Max(1.f, 420.f * EaseOut(LoadingT / LoadingHold)));
		// Travel only after the card has been on screen, since OpenLevel stalls the frame.
		if (LoadingT > LoadingHold && !bTravelled)
		{
			bTravelled = true;
			// IPL: the staged fixture travels with its id; everything else keeps its overs.
			if (IPLLoadingFixture != INDEX_NONE) UFrontendStatics::OpenIPLMatch(this, IPLLoadingFixture);
			else UFrontendStatics::OpenMatch(this, SelectedOvers);
		}
	}

	if (TransitionT >= 0.f)
	{
		TransitionT += Dt / 0.52f;

		float SweepX = 0.f;
		if (TransitionT < 0.5f)
		{
			const float Sub = TransitionT / 0.5f;
			const float E = FMath::InterpEaseOut(0.f, 1.f, Sub, 2.2f);
			SweepX = FMath::Lerp(-3800.f, 0.f, E);

			const float InScale = FMath::Lerp(0.85f, 1.04f, EaseOut(Sub));
			const float InAlpha = FMath::Clamp(Sub * 2.2f, 0.f, 1.f);
			if (TransitionBadge)
			{
				TransitionBadge->SetRenderScale(FVector2D(InScale, InScale));
				TransitionBadge->SetRenderOpacity(InAlpha);
			}
			if (TransitionBar)
			{
				TransitionBar->SetWidthOverride(FMath::Max(1.f, 440.f * EaseOut(Sub)));
			}
		}
		else
		{
			if (!bTransitionSwitched)
			{
				bTransitionSwitched = true;
				if (TransitionMidpoint)
				{
					TFunction<void()> Midpoint = MoveTemp(TransitionMidpoint);
					Midpoint();
				}
			}

			const float ExitSub = (TransitionT - 0.5f) / 0.5f;
			const float E = FMath::InterpEaseIn(0.f, 1.f, ExitSub, 2.2f);
			SweepX = FMath::Lerp(0.f, 3800.f, E);

			const float OutScale = FMath::Lerp(1.04f, 1.15f, ExitSub);
			const float OutAlpha = 1.f - EaseOut(ExitSub * 1.6f);
			if (TransitionBadge)
			{
				TransitionBadge->SetRenderScale(FVector2D(OutScale, OutScale));
				TransitionBadge->SetRenderOpacity(OutAlpha);
			}
			if (TransitionBar)
			{
				TransitionBar->SetWidthOverride(440.f);
			}
		}

		if (TransitionBladeDark)
		{
			TransitionBladeDark->SetRenderTranslation(FVector2D(SweepX, 0.f));
		}
		if (TransitionBladeBlue)
		{
			const float Parallax = (TransitionT < 0.5f) ? (140.f * (1.f - EaseOut(TransitionT / 0.5f))) : (-140.f * EaseOut((TransitionT - 0.5f) / 0.5f));
			TransitionBladeBlue->SetRenderTranslation(FVector2D(1980.f + Parallax, 0.f));
		}

		if (TransitionT >= 1.f)
		{
			TransitionLayer->SetVisibility(ESlateVisibility::Collapsed);
			TransitionT = -1.f;
			bTransitionSwitched = false;
			TransitionMidpoint = nullptr;
		}
	}

	if (Pulse) Pulse->SetRenderOpacity(0.7f + 0.3f * FMath::Sin(Clock * 1.8f));
}

// ---- IPL season hub (the season itself lives in UIPLSeasonSave; this is transient UI state) ----

void UFrontendRoot::OpenSeasonHub()
{
	IPLView = 0;
	RefreshSeasonHub();
	ShowTab(EFrontendTab::IPLSeason);
}

void UFrontendRoot::OpenTeamSelect(int32 FixtureId)
{
	IPLSelectFixture = FixtureId;
	IPLXIOrder.Reset();
	// Pre-fill with the remembered XI in batting order, so START is one tap away.
	if (UIPLSeasonSave* Save = UIPLSeasonSave::Get())
	{
		if (Save->bHasSeason)
		{
			const int32 Fi = IPLSeason::FixtureIndex(Save->Season, FixtureId);
			if (Fi != INDEX_NONE)
			{
				const int32 User = Save->Season.UserTeam;
				if (Save->Season.LastXI.IsValidIndex(User)
					&& IPLMatchAdapter::ValidateXI(Save->Season, User, Save->Season.LastXI[User], nullptr))
					IPLXIOrder = Save->Season.LastXI[User].BattingOrder;
			}
		}
	}
	PlayTransition([this]()
	{
		IPLView = 1;
		RefreshSeasonHub();
		ShowTabDirect(EFrontendTab::IPLSeason);
	}, TEXT("TEAM SELECTION"), TEXT("SET YOUR PLAYING XI"));
}

void UFrontendRoot::ToggleXIPlayer(int32 PlayerId)
{
	// Tap in batting order: the first tap opens, the eleventh finishes the tail.
	if (IPLXIOrder.Remove(PlayerId) == 0 && IPLXIOrder.Num() < IPLSeason::PlayingXI)
		IPLXIOrder.Add(PlayerId);
	RefreshSeasonHub();
}

void UFrontendRoot::StartIPLMatch()
{
	UIPLSeasonSave* Save = UIPLSeasonSave::Get();
	if (!Save || !Save->bHasSeason || IsStartingMatch()) return;
	const int32 Fi = IPLSeason::FixtureIndex(Save->Season, IPLSelectFixture);
	if (Fi == INDEX_NONE) return;
	FIPLFixture& Fx = Save->Season.Fixtures[Fi];
	if (Fx.Status != EIPLFixtureStatus::Upcoming || !IPLSeason::InvolvesUser(Save->Season, Fx)) return;
	const int32 User = Save->Season.UserTeam;
	FIPLPlayingXI UserXI;
	UserXI.BattingOrder = IPLXIOrder;
	FString Why;
	if (!IPLMatchAdapter::ValidateXI(Save->Season, User, UserXI, &Why)) return;
	Save->Season.LastXI[User] = UserXI; // remembered for the next fixture
	const int32 Opp = Fx.Home == User ? Fx.Away : Fx.Home;
	FIPLPlayingXI OppXI = Save->Season.LastXI.IsValidIndex(Opp) ? Save->Season.LastXI[Opp] : FIPLPlayingXI();
	if (!IPLMatchAdapter::ValidateXI(Save->Season, Opp, OppXI, nullptr))
	{
		OppXI = IPLSeason::MakeDefaultXI(Save->Season.Squads[Opp].Players, AuctionData::Players());
		Save->Season.LastXI[Opp] = OppXI;
	}
	Save->Persist();
	// Staged for the match map in home/away order; consumed once in StartPlay.
	if (Fx.Home == User) UIPLPendingMatch::Get()->Set(Fx.FixtureId, UserXI, OppXI);
	else UIPLPendingMatch::Get()->Set(Fx.FixtureId, OppXI, UserXI);
	SelectedOvers = IPLSeason::MatchOvers;
	IPLLoadingFixture = Fx.FixtureId;
	IPLView = 0;
	StartSuperOver();
}

void UFrontendRoot::SimulateIPLFixture(int32 FixtureId)
{
	UIPLSeasonSave* Save = UIPLSeasonSave::Get();
	if (!Save || !Save->bHasSeason) return;
	const int32 Fi = IPLSeason::FixtureIndex(Save->Season, FixtureId);
	if (Fi == INDEX_NONE) return;
	const FIPLFixture& Fx = Save->Season.Fixtures[Fi];
	if (Fx.Status != EIPLFixtureStatus::Upcoming) return; // completed fixtures never re-simulate
	if (IPLSeason::InvolvesUser(Save->Season, Fx)) return; // the user's fixtures are played, not simmed
	const FIPLResult R = IPLSeason::SimulateFixture(Save->Season, Fi, AuctionData::Players());
	if (IPLSeason::CommitResult(Save->Season, R)) Save->Persist();
	RefreshSeasonHub();
}

void UFrontendRoot::RefreshSeasonHub()
{
	if (!Pages) return;
	const int32 Idx = int32(EFrontendTab::IPLSeason);
	UWidget* Slot = Pages->GetChildAt(Idx);
	if (!Slot) return;
	UWidget* Fresh = FrontendScreens::Build(this, EFrontendTab::IPLSeason);
	UBorder* Pad = Box(WidgetTree, Flat(FLinearColor::Transparent), FMargin(S4, 0.f, S4, S2), Fresh);
	if (UScrollBox* Scroll = Cast<UScrollBox>(Slot))
	{
		Scroll->ClearChildren();
		Scroll->AddChild(Pad);
	}
}
