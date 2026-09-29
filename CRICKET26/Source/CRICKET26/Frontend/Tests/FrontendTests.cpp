// Frontend automation tests: catalogue sanity, the options that carry match setup into the Super Over,
// travel URLs, and the shell's navigation and back behaviour.

#include "Misc/AutomationTest.h"
#include "FrontendData.h"
#include "FrontendStyle.h"
#include "Fonts/CompositeFont.h"
#include "FrontendSettingsSave.h"
#include "FrontendStatics.h"
#include "CricketStadium.h"
#include "Widgets/FrontendMatchOverlay.h"
#include "Widgets/FrontendRoot.h"
#include "Widgets/FrontendUI.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Spacer.h"
#include "Layout/ArrangedChildren.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CricketFrontendTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrontendCatalogueTest, "CRICKET26.Frontend.Catalogue", CricketFrontendTests::Flags)
bool FFrontendCatalogueTest::RunTest(const FString&)
{
	for (uint8 I = 0; I < uint8(EFrontendTab::Count); ++I) TestFalse(TEXT("every tab has a name"), FrontendData::TabName(EFrontendTab(I)).IsEmpty());
	TestEqual(TEXT("Super Over is the playable mode"), FrontendData::SuperOver().Status, EFeatureStatus::Available);
	for (const FFrontendFeature& F : FrontendData::HomeModules()) TestNotEqual(TEXT("franchise modules are not playable"), F.Status, EFeatureStatus::Available);
	for (const FFrontendFeature& F : FrontendData::MoreModes()) TestNotEqual(TEXT("unbuilt modes are not marked playable"), F.Status, EFeatureStatus::Available);
	TestEqual(TEXT("the AI auction is playable"), FrontendData::AuctionModes()[0].Status, EFeatureStatus::Available);
	TestNotEqual(TEXT("the online auction is not"), FrontendData::AuctionModes()[1].Status, EFeatureStatus::Available);
	for (const FStoreOffer& O : FrontendData::StoreOffers()) TestNotEqual(TEXT("nothing in the store is buyable"), O.Status, EFeatureStatus::Available);
	for (EFeatureStatus S : { EFeatureStatus::Available, EFeatureStatus::InDevelopment, EFeatureStatus::ComingSoon, EFeatureStatus::Locked })
	{
		TestFalse(TEXT("every status has a label"), FrontendData::StatusLabel(S).IsEmpty());
	}

	const TArray<FFranchisePlayerRow> Squad = FrontendData::Squad();
	TestEqual(TEXT("squad is the Super Over side: three batters and a bowler"), Squad.Num(), 4);
	for (const FFranchisePlayerRow& P : Squad) TestTrue(TEXT("ratings are on the 0-100 card scale"), P.Rating >= 40 && P.Rating <= 95);

	const TArray<FString> Venues = FrontendData::VenueNames();
	TestEqual(TEXT("venue list is Random plus every stadium venue"), Venues.Num(), CricketStadium::NumVenues + 1);
	TestEqual(TEXT("venue names are title case"), Venues[1], FString(TEXT("Harbourside Oval")));
	TestEqual(TEXT("four difficulty levels"), FrontendData::DifficultyNames().Num(), 4);
	return true;
}

// Barlow Condensed has no rupee sign, so every auction figure drew a missing-glyph box before its sub-font existed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrontendRupeeTest, "CRICKET26.Frontend.RupeeGlyph", CricketFrontendTests::Flags)
bool FFrontendRupeeTest::RunTest(const FString&)
{
	const FSlateFontInfo Info = FrontendStyle::Font(20, FrontendStyle::EWeight::Bold);
	if (!TestNotNull(TEXT("the broadcast face is a composite font"), Info.CompositeFont.Get())) return false;
	bool bMapped = false;
	for (const FCompositeSubFont& Sub : Info.CompositeFont->SubTypefaces)
		for (const FInt32Range& R : Sub.CharacterRanges)
			if (R.Contains(0x20B9))
			{
				bMapped = true;
				for (const FTypefaceEntry& E : Sub.Typeface.Fonts) TestTrue(TEXT("the rupee font file exists"), FPaths::FileExists(E.Font.GetFontFilename()));
			}
	TestTrue(TEXT("U+20B9 has a sub-font"), bMapped);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrontendMatchOptionsTest, "CRICKET26.Frontend.MatchOptions", CricketFrontendTests::Flags)
bool FFrontendMatchOptionsTest::RunTest(const FString&)
{
	UFrontendSettingsSave* S = NewObject<UFrontendSettingsSave>();
	S->Difficulty = 3;
	S->Quality = 1;
	S->bTimingBar = false;
	S->Venue = -1;
	const FString Random = S->MatchOptions();
	TestEqual(TEXT("difficulty travels"), UGameplayStatics::GetIntOption(Random, TEXT("Difficulty"), -1), 3);
	TestEqual(TEXT("quality travels"), UGameplayStatics::GetIntOption(Random, TEXT("Quality"), -1), 1);
	TestEqual(TEXT("timing bar travels"), UGameplayStatics::GetIntOption(Random, TEXT("TimingBar"), -1), 0);
	TestFalse(TEXT("a random venue sends no Venue option"), UGameplayStatics::HasOption(Random, TEXT("Venue")));

	S->Venue = 2;
	S->Difficulty = 9;
	const FString Picked = S->MatchOptions();
	TestEqual(TEXT("a picked venue travels"), UGameplayStatics::GetIntOption(Picked, TEXT("Venue"), -1), 2);
	TestEqual(TEXT("difficulty is clamped to the AI levels"), UGameplayStatics::GetIntOption(Picked, TEXT("Difficulty"), -1), 3);

	TestTrue(TEXT("the Super Over URL picks its game mode"), UFrontendStatics::SuperOverOptions().StartsWith(TEXT("game=/Script/CRICKET26.SuperOverGameMode?")));
	for (int32 Overs : {1, 3, 5, 10, 20})
		TestEqual(TEXT("selected overs travel with the match"), UGameplayStatics::GetIntOption(TEXT("?") + UFrontendStatics::SuperOverOptions(Overs), TEXT("Overs"), -1), Overs);
	TestEqual(TEXT("default launch remains Super Over"), UGameplayStatics::GetIntOption(TEXT("?") + UFrontendStatics::SuperOverOptions(), TEXT("Overs"), -1), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrontendTravelTest, "CRICKET26.Frontend.Travel", CricketFrontendTests::Flags)
bool FFrontendTravelTest::RunTest(const FString&)
{
	TestEqual(TEXT("Tab option picks the screen"), UFrontendStatics::TabFromOptions(TEXT("?Tab=1")), EFrontendTab::Play);
	TestEqual(TEXT("no Tab option lands on Home"), UFrontendStatics::TabFromOptions(TEXT("")), EFrontendTab::Home);
	TestEqual(TEXT("out-of-range Tab lands on Home"), UFrontendStatics::TabFromOptions(TEXT("?Tab=42")), EFrontendTab::Home);

	// Regression: leaving a match reopened the match. OpenLevel resolves the new URL against the current one;
	// relative travel keeps the match's game= option, so the menu URL must be resolved absolutely.
	FURL Match(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/CRICKET26.SuperOverGameMode?Difficulty=2"), TRAVEL_Absolute);
	const FURL Menu(&Match, *FString::Printf(TEXT("%s?Tab=1"), UFrontendStatics::EntryMap()),
		UFrontendStatics::bAbsoluteTravel ? TRAVEL_Absolute : TRAVEL_Relative);
	TestFalse(TEXT("the menu URL does not carry the match's game mode"), Menu.HasOption(TEXT("game")));
	TestEqual(TEXT("the menu URL keeps its tab"), FCString::Atoi(Menu.GetOption(TEXT("Tab="), TEXT("0"))), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrontendOverlayGateTest, "CRICKET26.Frontend.MatchOverlayGate", CricketFrontendTests::Flags)
bool FFrontendOverlayGateTest::RunTest(const FString&)
{
	TestTrue(TEXT("a player's match shows the menu button"), UFrontendMatchSubsystem::WantsOverlay(TEXT("-game -windowed")));
	TestFalse(TEXT("soak runs have no menu button"), UFrontendMatchSubsystem::WantsOverlay(TEXT("-game -CricketAutoPlay")));
	TestFalse(TEXT("injected-touch runs have no menu button"), UFrontendMatchSubsystem::WantsOverlay(TEXT("-game -CricketTouchScript")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrontendShellNavigationTest, "CRICKET26.Frontend.ShellNavigation", CricketFrontendTests::Flags)
bool FFrontendShellNavigationTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	UFrontendRoot* Root = CreateWidget<UFrontendRoot>(World, UFrontendRoot::StaticClass()); // builds every page
	if (!TestNotNull(TEXT("shell builds"), Root))
	{
		World->DestroyWorld(false);
		return false;
	}

	Root->Configure(EFrontendTab::Play, false);
	TestEqual(TEXT("starts on the requested tab"), Root->CurrentTab(), EFrontendTab::Play);
	TestTrue(TEXT("back from a section goes Home"), Root->Back());
	TestEqual(TEXT("back lands on Home"), Root->CurrentTab(), EFrontendTab::Home);
	TestFalse(TEXT("back on Home has nowhere to go"), Root->Back());
	Root->ShowTab(EFrontendTab::MatchFormat);
	TestTrue(TEXT("back from formats"), Root->Back());
	TestEqual(TEXT("formats return to Play"), Root->CurrentTab(), EFrontendTab::Play);
	Root->ShowTab(EFrontendTab::MatchFormat);
	Root->SelectMatchOvers(20);
	TestEqual(TEXT("20-over selection opens setup"), Root->CurrentTab(), EFrontendTab::MatchSetup);
	TestEqual(TEXT("20-over selection stored"), Root->MatchOvers(), 20);
	TestTrue(TEXT("back from setup"), Root->Back());
	TestEqual(TEXT("setup returns to formats"), Root->CurrentTab(), EFrontendTab::MatchFormat);
	Root->SelectMatchOvers(1);
	TestEqual(TEXT("Super Over replaces prior selection"), Root->MatchOvers(), 1);

	Root->ShowTab(EFrontendTab::Store);
	UFrontendRoot::FSheet Sheet;
	Sheet.Title = TEXT("Test");
	Root->ShowSheet(Sheet);
	TestTrue(TEXT("sheet opens"), Root->IsSheetOpen());
	TestTrue(TEXT("back closes the sheet first"), Root->Back());
	TestFalse(TEXT("sheet is closed"), Root->IsSheetOpen());
	TestEqual(TEXT("closing the sheet keeps the tab"), Root->CurrentTab(), EFrontendTab::Store);

	Root->ShowTab(EFrontendTab::Count);
	TestEqual(TEXT("an invalid tab is ignored"), Root->CurrentTab(), EFrontendTab::Store);
	Root->MarkAsGarbage();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrontendAlignTest, "CRICKET26.Frontend.Align", CricketFrontendTests::Flags)
bool FFrontendAlignTest::RunTest(const FString&)
{
	// Regression: crests and avatars drew their initials top-left. UBorder's alignment setters leave an existing
	// content slot untouched, so centring after Box(..., Content) did nothing.
	UWidgetTree* T = NewObject<UWidgetTree>();
	UBorder* B = FrontendUI::Box(T, FrontendStyle::Flat(FLinearColor::Black), FMargin(0.f), T->ConstructWidget<USpacer>());
	FrontendUI::Align(B, HAlign_Center, VAlign_Center);
	const UBorderSlot* Slot = Cast<UBorderSlot>(B->GetContentSlot());
	if (!TestNotNull(TEXT("border has a content slot"), Slot)) return false;
	TestEqual(TEXT("content is centred horizontally"), Slot->GetHorizontalAlignment(), HAlign_Center);
	TestEqual(TEXT("content is centred vertically"), Slot->GetVerticalAlignment(), VAlign_Center);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrontendFitTest, "CRICKET26.Frontend.Fit", CricketFrontendTests::Flags)
bool FFrontendFitTest::RunTest(const FString&)
{
	// Regression: on a 4:3 tablet the home tiles clipped their titles ("MEGA AUCT"). Tile copy now sits in Fit, which
	// shrinks it into a narrow tile and leaves it at full size in a wide one.
	UWidgetTree* T = NewObject<UWidgetTree>();
	TSharedRef<SWidget> Fit = FrontendUI::Fit(T, FrontendUI::Sized(T, T->ConstructWidget<USpacer>(), 400.f, 100.f))->TakeWidget();
	Fit->SlatePrepass(1.f);
	auto ScaleIn = [&Fit](float Width)
	{
		FArrangedChildren Children(EVisibility::Visible);
		Fit->ArrangeChildren(FGeometry::MakeRoot(FVector2D(Width, 100.f), FSlateLayoutTransform()), Children);
		return Children.Num() > 0 ? Children[0].Geometry.Scale : -1.f;
	};
	TestEqual(TEXT("copy shrinks into a narrow tile"), ScaleIn(200.f), 0.5f, 0.01f);
	TestEqual(TEXT("copy never grows in a wide tile"), ScaleIn(800.f), 1.f, 0.01f);
	return true;
}

#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrontendBrushTest, "CRICKET26.Frontend.Brushes", CricketFrontendTests::Flags)
bool FFrontendBrushTest::RunTest(const FString&)
{
	// Regression: a fixed 999 radius exceeds any chip, so the rounded-box shader drew nothing. Pills follow the height.
	TestEqual(TEXT("pill rounding"), FrontendStyle::Rounded(FLinearColor::White, FrontendStyle::RPill).OutlineSettings.RoundingType,
		ESlateBrushRoundingType::HalfHeightRadius);
	TestEqual(TEXT("card rounding"), FrontendStyle::Rounded(FLinearColor::White, FrontendStyle::RCard).OutlineSettings.RoundingType,
		ESlateBrushRoundingType::FixedRadius);
	// Regression: art that has not been imported painted a white slab; it must draw nothing instead.
	TestEqual(TEXT("missing art"), FrontendStyle::Art(TEXT("T_CRICKET26_DoesNotExist")).DrawAs, ESlateBrushDrawType::NoDrawType);
	// Regression: art read before its texture was built reported the square placeholder's size, so the wide logo
	// lockup drew squashed into a square. Pictures size from the texture source's real proportions.
	const FSlateBrush LogoArt = FrontendStyle::Art(TEXT("T_CRICKET26_Logo"));
	if (LogoArt.DrawAs != ESlateBrushDrawType::NoDrawType)
	{
		TestTrue(TEXT("logo art keeps its wide aspect"), LogoArt.ImageSize.X > LogoArt.ImageSize.Y * 2.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFrontendMotionTest, "CRICKET26.Frontend.Motion", CricketFrontendTests::Flags)
bool FFrontendMotionTest::RunTest(const FString&)
{
	// Entrances hide a widget until its delay has passed, then settle it fully visible in place.
	UWidgetTree* T = NewObject<UWidgetTree>();
	USpacer* W = T->ConstructWidget<USpacer>();
	FrontendUI::Animate(W, FrontendUI::EMotion::Enter, 0.2f);
	TestEqual(TEXT("hidden before the page ticks"), W->GetRenderOpacity(), 0.f);
	FrontendUI::TickMotion(0.1f, 0.1f);
	TestEqual(TEXT("still hidden inside its delay"), W->GetRenderOpacity(), 0.f);
	FrontendUI::TickMotion(1.f, 1.f);
	TestEqual(TEXT("visible once settled"), W->GetRenderOpacity(), 1.f);
	TestTrue(TEXT("back in place once settled"), W->GetRenderTransform().Translation.IsNearlyZero());
	// Icons render from the bundled Material Icons font, not the engine fallback.
	TestTrue(TEXT("icon font loads"), FrontendStyle::IconFont(24).CompositeFont.IsValid());
	return true;
}
