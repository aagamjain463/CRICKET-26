#include "Widgets/FrontendScreens.h"
#include "Widgets/FrontendRoot.h"
#include "Widgets/FrontendUI.h"
#include "FrontendData.h"
#include "FrontendStatics.h"
#include "FrontendSettingsSave.h"
#include "IPLSeason.h"
#include "IPLSeasonSave.h"
#include "AuctionTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Texture2D.h"

using namespace FrontendStyle;
using namespace FrontendUI;

namespace
{
	const TCHAR* StadiumArt = TEXT("T_CRICKET26_Stadium_Night");
	const TCHAR* BatterArt = TEXT("T_CRICKET26_Hero_Batter_v2");
	const TCHAR* FranchiseArt = TEXT("T_CRICKET26_Franchise_Stadium");
	const TCHAR* BatterCutout = TEXT("T_CRICKET26_Cutout_Batter");
	const TCHAR* BowlerCutout = TEXT("T_CRICKET26_Cutout_Bowler");

	const TCHAR* GoldFace = TEXT("T_CRICKET26_Tile_Gold");
	const TCHAR* RoyalFace = TEXT("T_CRICKET26_Tile_Royal");
	const TCHAR* EmberFace = TEXT("T_CRICKET26_Tile_Ember");
	const TCHAR* NightFace = TEXT("T_CRICKET26_Tile_Night");

	void Coming(UFrontendRoot* Root, const FString& Title, const FString& Line)
	{
		UFrontendRoot::FSheet S;
		S.Eyebrow = TEXT("CRICKET 26");
		S.Title = Title;
		S.Body = Line;
		S.Status = EFeatureStatus::ComingSoon;
		Root->ShowSheet(S);
	}

	UWidget* SeasonHub(UFrontendRoot* Root);
	UWidget* TeamSelect(UFrontendRoot* Root);

	// A tappable tile in the football-game manner: a brand gradient face (gold, royal, ember or night), optionally a
	// photo plate laid over it, a figure breaking the frame on the right, a shade from the left so the copy reads, a
	// gloss line along the top, an accent rule along the bottom and a hairline frame.
	struct FTileLook
	{
		const TCHAR* Face = NightFace;
		const TCHAR* Plate = nullptr;
		FLinearColor PlateTint = FLinearColor::White;
		FLinearColor Accent = Hex(0xFFFFFF, 0.25f);
		float Shade = 0.8f; // strength of the left-hand shade behind the copy
		UWidget* Figure = nullptr;
	};

	UWidget* Tile(UWidgetTree* T, const FTileLook& Look, UWidget* Content, TFunction<void()> OnTap, const FMargin& Pad = FMargin(S4, S3))
	{
		UOverlay* Layers = Stack(T);
		Layers->SetClipping(EWidgetClipping::ClipToBounds);
		Add(Layers, Box(T, Flat(Card())));
		Add(Layers, Backdrop(T, Look.Face));
		if (Look.Plate) Add(Layers, Backdrop(T, Look.Plate, Look.PlateTint));
		if (Look.Figure) Add(Layers, Look.Figure, HAlign_Right, VAlign_Bottom);
		if (Look.Shade > 0.f) Add(Layers, Fade(T, Hex(0x03061A, Look.Shade), false));
		Add(Layers, Sized(T, Fade(T, Hex(0x03061A, Look.Shade * 0.7f), true), 0.f, 160.f), HAlign_Fill, VAlign_Bottom);
		Add(Layers, Sized(T, Box(T, Flat(Hex(0xFFFFFF, 0.28f))), 0.f, 2.f), HAlign_Fill, VAlign_Top);
		Add(Layers, Sized(T, Box(T, Flat(Look.Accent)), 0.f, 5.f), HAlign_Fill, VAlign_Bottom);
		Add(Layers, Box(T, Rounded(FLinearColor::Transparent, 0.f, Hex(0xFFFFFF, 0.18f))));
		Add(Layers, Content, HAlign_Fill, VAlign_Fill, Pad);
		return Button(T, Layers, EButtonKind::Quiet, MoveTemp(OnTap), 0.f);
	}

	UWidget* Chip(UWidgetTree* T, const FString& Label, const FLinearColor& Tint, bool bLive = false)
	{
		UHorizontalBox* Row = HBox(T);
		if (bLive) Add(Row, Dot(T, Tint, 10.f), FMargin(0.f, 0.f, S1, 0.f), 0.f, VAlign_Center);
		else if (Label == TEXT("Coming soon")) Add(Row, Icon(T, Glyph::Lock, 16, Tint), FMargin(0.f, 0.f, 6.f, 0.f), 0.f, VAlign_Center);
		Add(Row, Text(T, Label.ToUpper(), 14, bLive ? Ink() : Tint, EWeight::Condensed, 180), FMargin(0.f), 0.f, VAlign_Center);
		return Box(T, Rounded(Hex(0x03061A, 0.55f), RPill, Tint * FLinearColor(1.f, 1.f, 1.f, 0.7f)), FMargin(14.f, 5.f), Row);
	}

	// The copy block shared by the hub tiles: status chip top left, then eyebrow, title and line along the bottom.
	UWidget* TileCopy(UWidgetTree* T, UWidget* Status, const FString& Eyebrow, const FLinearColor& EyebrowTint, const FString& Title,
		int32 TitleSize, const FString& Line)
	{
		UVerticalBox* Copy = VBox(T);
		if (Status) Add(Copy, Status, FMargin(0.f), 0.f, HAlign_Left);
		Add(Copy, Space(T, 0.f, 0.f), FMargin(0.f), 1.f);
		// The copy shrinks rather than clips when a narrow (4:3 tablet) screen squeezes the tile.
		UVerticalBox* Block = VBox(T);
		Add(Block, Text(T, Eyebrow.ToUpper(), 15, EyebrowTint, EWeight::Bold, 220));
		UTextBlock* Heading = Text(T, Title.ToUpper(), TitleSize, Ink(), EWeight::Black, 10);
		Heading->SetShadowOffset(FVector2D(0.f, 3.f));
		Heading->SetShadowColorAndOpacity(Hex(0x000000, 0.45f));
		Add(Block, Heading, FMargin(0.f, -TitleSize * 0.1f, 0.f, -TitleSize * 0.06f));
		if (!Line.IsEmpty()) Add(Block, Text(T, Line, 18, InkDim(), EWeight::Medium));
		Add(Copy, Fit(T, Block), FMargin(0.f), 0.f, HAlign_Left);
		return Copy;
	}

	// The AI auction room. The tile says what it is without naming any real league.
	UWidget* AuctionTile(UWidgetTree* T, UFrontendRoot* Root, int32 TitleSize = 40)
	{
		FTileLook Look;
		Look.Face = EmberFace;
		Look.Accent = Hex(0xFF7A5C);
		Look.Shade = 0.45f;
		UOverlay* Content = Stack(T);
		Add(Content, Icon(T, 0xe90e, 120, Hex(0xFFFFFF, 0.16f)), HAlign_Right, VAlign_Top, FMargin(0.f, -S1, -S1, 0.f)); // gavel
		Add(Content, TileCopy(T, Chip(T, TEXT("Available"), Teal(), true), TEXT("Single player"), GoldHi(), TEXT("Mega Auction"), TitleSize,
			TEXT("Ten franchises. One hammer.")));
		return Tile(T, Look, Content, [Root]() { Root->OpenAuctionWithTransition(); });
	}

	UWidget* SoonTile(UWidgetTree* T, UFrontendRoot* Root, const TCHAR* Art, const FString& Title, const FString& Line, const FString& Eyebrow)
	{
		FTileLook Look;
		Look.Plate = Art;
		Look.PlateTint = Hex(0x7080A8, 0.55f);
		return Tile(T, Look, TileCopy(T, Chip(T, TEXT("Coming soon"), GoldHi()), Eyebrow, Teal(), Title, 38, Line),
			[Root, Title, Line]() { Coming(Root, Title, Line); });
	}

	// Card renders per squad slot; FrontendData::Squad() lists the opener, finisher, allrounder and spinner in that order.
	const TCHAR* SquadRender(int32 Index)
	{
		static const TCHAR* Renders[] = { TEXT("T_CRICKET26_Cutout_Batter"), TEXT("T_CRICKET26_Cutout_Finisher"),
			TEXT("T_CRICKET26_Cutout_Allrounder"), TEXT("T_CRICKET26_Cutout_Spinner") };
		return Renders[FMath::Clamp(Index, 0, UE_ARRAY_COUNT(Renders) - 1)];
	}

	int32 BestIndex(const TArray<FFranchisePlayerRow>& Squad)
	{
		int32 Best = 0;
		for (int32 I = 1; I < Squad.Num(); ++I) if (Squad[I].Rating > Squad[Best].Rating) Best = I;
		return Best;
	}

	UWidget* Home(UFrontendRoot* Root)
	{
		UWidgetTree* T = Root->WidgetTree;
		UHorizontalBox* Page = HBox(T);

		// Cover athletes: the home batter breaks out of the grid in front of the allrounder, lit gold and blue like
		// key art, with the side's best player on the elite card at his feet.
		UOverlay* Hero = Stack(T);
		Add(Hero, Glow(T, Hex(0x2F6BFF, 0.60f), 1150.f), HAlign_Center, VAlign_Center, FMargin(0.f, 0.f, 0.f, 80.f));
		Add(Hero, Glow(T, Hex(0xF2B632, 0.40f), 720.f), HAlign_Center, VAlign_Top, FMargin(0.f, -60.f, 0.f, 0.f));
		Add(Hero, Picture(T, TEXT("T_CRICKET26_Cutout_Allrounder"), 700.f, Hex(0x5C6C9C)), HAlign_Right, VAlign_Bottom, FMargin(0.f, 0.f, -S3, -S2));
		Add(Hero, Picture(T, BatterCutout, 820.f), HAlign_Center, VAlign_Bottom, FMargin(0.f, 0.f, 0.f, -S2));
		Add(Hero, Sized(T, Fade(T, Hex(0x03061A, 0.95f), true), 0.f, 220.f), HAlign_Fill, VAlign_Bottom);
		const TArray<FFranchisePlayerRow> Squad = FrontendData::Squad();
		if (Squad.Num() > 0)
		{
			const int32 Best = BestIndex(Squad);
			UWidget* Star = PlayerCard(T, Squad[Best], SquadRender(Best), true, 380.f);
			Add(Hero, Star, HAlign_Left, VAlign_Bottom, FMargin(0.f, 0.f, 0.f, S1));
			Animate(Star, EMotion::Enter, 0.12f);
		}
		Add(Page, Hero, FMargin(0.f, 0.f, S3, 0.f), 0.8f);

		UVerticalBox* Right = VBox(T);
		// The featured event banner: tapping it goes to match setup; the gold PLAY tile below is the page's one gold action.
		FTileLook Banner;
		Banner.Face = RoyalFace;
		Banner.Plate = TEXT("T_CRICKET26_VS_Bg");
		Banner.PlateTint = Hex(0xFFFFFF, 0.85f);
		Banner.Accent = Gold();
		Banner.Shade = 0.85f;
		Banner.Figure = Picture(T, BowlerCutout, 520.f);
		UVerticalBox* Copy = VBox(T);
		Add(Copy, Chip(T, TEXT("Featured  /  Available"), Teal(), true), FMargin(0.f), 0.f, HAlign_Left);
		Add(Copy, Space(T, 0.f, 0.f), FMargin(0.f), 1.f);
		UVerticalBox* Block = VBox(T);
		Add(Block, Text(T, TEXT("THE DECIDER"), 18, GoldHi(), EWeight::Bold, 300));
		UTextBlock* Title = Text(T, TEXT("SUPER OVER"), 104, Ink(), EWeight::Black);
		Title->SetShadowOffset(FVector2D(0.f, 5.f));
		Title->SetShadowColorAndOpacity(Hex(0x000000, 0.5f));
		Add(Block, Title, FMargin(0.f, -18.f, 0.f, -14.f));
		Add(Block, Text(T, TEXT("Six balls. Three batters. One winner."), 22, InkDim(), EWeight::Medium));
		Add(Copy, Fit(T, Block), FMargin(0.f), 0.f, HAlign_Left);
		UWidget* Feature = Tile(T, Banner, Copy, [Root]() { Root->ShowTab(EFrontendTab::MatchSetup); });
		Add(Right, Feature, FMargin(0.f, 0.f, 0.f, S2), 1.5f);
		Animate(Feature, EMotion::Enter, 0.f);

		UHorizontalBox* Row = HBox(T);
		// Club: the squad rating on the OVR shield beside the club crest.
		{
			FTileLook Look;
			Look.Face = RoyalFace;
			Look.Accent = Teal();
			Look.Shade = 0.35f;
			int32 Sum = 0;
			for (const FFranchisePlayerRow& P : Squad) Sum += P.Rating;
			UVerticalBox* Club = VBox(T);
			UHorizontalBox* Marks = HBox(T);
			Add(Marks, Picture(T, TEXT("T_CRICKET26_Crest_Home"), 96.f), FMargin(0.f, 0.f, S2, 0.f), 0.f, VAlign_Center);
			UOverlay* Shield = Stack(T);
			Add(Shield, Picture(T, TEXT("T_CRICKET26_Ovr_Badge"), 96.f));
			UVerticalBox* Num = VBox(T);
			Add(Num, Text(T, FString::FromInt(Squad.Num() ? FMath::RoundToInt(float(Sum) / Squad.Num()) : 0), 38, GoldHi(), EWeight::Black),
				FMargin(0.f), 0.f, HAlign_Center);
			Add(Num, Text(T, TEXT("OVR"), 13, Ink(), EWeight::Bold, 200), FMargin(0.f, -8.f, 0.f, 0.f), 0.f, HAlign_Center);
			Add(Shield, Num, HAlign_Center, VAlign_Center, FMargin(0.f, 0.f, 0.f, 10.f));
			Add(Marks, Shield, FMargin(0.f), 0.f, VAlign_Center);
			Add(Club, Marks, FMargin(0.f), 0.f, HAlign_Left);
			Add(Club, Space(T, 0.f, 0.f), FMargin(0.f), 1.f);
			UVerticalBox* Words = VBox(T);
			Add(Words, Text(T, TEXT("MY CLUB"), 15, Teal(), EWeight::Bold, 220));
			Add(Words, Text(T, TEXT("HOME XI"), 40, Ink(), EWeight::Black, 10), FMargin(0.f, -4.f, 0.f, -2.f));
			Add(Words, Text(T, TEXT("Squad and road to the title"), 18, InkDim(), EWeight::Medium));
			Add(Club, Fit(T, Words), FMargin(0.f), 0.f, HAlign_Left);
			Add(Row, Tile(T, Look, Club, [Root]() { Root->ShowTab(EFrontendTab::Franchise); }), FMargin(0.f, 0.f, S2, 0.f), 1.f);
		}
		Add(Row, AuctionTile(T, Root, 40), FMargin(0.f, 0.f, S2, 0.f), 1.f);
		// Play: the page's one gold action.
		{
			FTileLook Look;
			Look.Face = GoldFace;
			Look.Accent = Hex(0xFFF1C2);
			Look.Shade = 0.f;
			UOverlay* Content = Stack(T);
			UImage* Shine = T->ConstructWidget<UImage>();
			Shine->SetBrush(Art(TEXT("T_CRICKET26_Shine"), Hex(0xFFFFFF, 0.5f)));
			Shine->SetVisibility(ESlateVisibility::HitTestInvisible);
			Add(Content, Sized(T, Shine, 90.f, 0.f), HAlign_Left, VAlign_Fill, FMargin(-S4, -S3));
			Animate(Shine, EMotion::Shine, 0.6f);
			UVerticalBox* Col = VBox(T);
			Add(Col, Icon(T, Glyph::Play, 64, GoldInk()), FMargin(-8.f, -S1, 0.f, 0.f), 0.f, HAlign_Left);
			Add(Col, Space(T, 0.f, 0.f), FMargin(0.f), 1.f);
			UVerticalBox* Words = VBox(T);
			Add(Words, Text(T, TEXT("CHOOSE FORMAT"), 16, Hex(0x3D2A08), EWeight::Bold, 260));
			Add(Words, Text(T, TEXT("PLAY MATCH"), 58, GoldInk(), EWeight::Black, 20), FMargin(0.f, -16.f, 0.f, -12.f));
			Add(Col, Fit(T, Words), FMargin(0.f), 0.f, HAlign_Left);
			Add(Content, Col);
			Add(Row, Tile(T, Look, Content, [Root]() { Root->ShowTab(EFrontendTab::MatchFormat); }), FMargin(0.f), 1.f);
		}
		Add(Right, Row, FMargin(0.f), 1.f);
		Animate(Row, EMotion::Enter, 0.08f);
		Add(Page, Right, FMargin(0.f), 1.2f);
		return Page;
	}

	// A tall mode card for the Play hub.
	UWidget* ModeCard(UWidgetTree* T, const FTileLook& Look, const FString& Eyebrow, const FString& Title, const FString& Line,
		bool bAvailable, TFunction<void()> OnTap, UWidget* Action = nullptr)
	{
		UVerticalBox* Copy = VBox(T);
		Add(Copy, bAvailable ? Chip(T, TEXT("Available"), Teal(), true) : Chip(T, TEXT("Coming soon"), GoldHi()), FMargin(0.f), 0.f, HAlign_Left);
		Add(Copy, Space(T, 0.f, 0.f), FMargin(0.f), 1.f);
		// The title stacks its last word and the copy shrinks rather than clips on a narrow (4:3 tablet) screen.
		FString Stacked = Title.ToUpper();
		int32 Break = INDEX_NONE;
		if (Stacked.FindLastChar(TEXT(' '), Break)) Stacked[Break] = TEXT('\n');
		UVerticalBox* Words = VBox(T);
		Add(Words, Text(T, Eyebrow.ToUpper(), 16, bAvailable ? GoldHi() : Teal(), EWeight::Bold, 260));
		UTextBlock* Heading = Text(T, Stacked, 58, Ink(), EWeight::Black);
		Heading->SetLineHeightPercentage(0.82f);
		Heading->SetShadowOffset(FVector2D(0.f, 4.f));
		Heading->SetShadowColorAndOpacity(Hex(0x000000, 0.45f));
		Add(Words, Heading, FMargin(0.f, -8.f, 0.f, 0.f));
		Add(Words, Text(T, Line, 19, InkDim(), EWeight::Medium));
		Add(Copy, Fit(T, Words), FMargin(0.f, 0.f, 0.f, Action ? S3 : 0.f), 0.f, HAlign_Left);
		if (Action) Add(Copy, Action, FMargin(S1, 0.f, 0.f, 0.f), 0.f, HAlign_Left);
		return Tile(T, Look, Copy, MoveTemp(OnTap));
	}

	UWidget* Play(UFrontendRoot* Root)
	{
		UWidgetTree* T = Root->WidgetTree;
		UVerticalBox* Page = VBox(T);
		Add(Page, Text(T, TEXT("CHOOSE YOUR GAME"), 44, Ink(), EWeight::Black, 20), FMargin(0.f, 0.f, 0.f, S2));
		UHorizontalBox* Cards = HBox(T);
		auto ToSetup = [Root]() { Root->ShowTab(EFrontendTab::MatchFormat); };
		FTileLook Quick;
		Quick.Face = RoyalFace;
		Quick.Plate = StadiumArt;
		Quick.PlateTint = Hex(0xFFFFFF, 0.55f);
		Quick.Accent = Gold();
		Quick.Figure = Picture(T, BatterCutout, 640.f);
		Add(Cards, ModeCard(T, Quick, TEXT("Quick match"), TEXT("Play Match"), TEXT("Choose your match length."), true, ToSetup,
			CTA(T, TEXT("SELECT"), EButtonKind::Primary, ToSetup, 64.f)), FMargin(0.f, 0.f, S2, 0.f), 1.35f);
		FTileLook Auction;
		Auction.Face = EmberFace;
		Auction.Accent = Hex(0xFF7A5C);
		Auction.Shade = 0.45f;
		UVerticalBox* Gavel = VBox(T); // lifted clear of the ENTER button
		Add(Gavel, Icon(T, 0xe90e, 300, Hex(0xFFFFFF, 0.12f)));
		Add(Gavel, Space(T, 0.f, 260.f));
		Auction.Figure = Gavel;
		auto ToAuction = [Root]() { Root->OpenAuctionWithTransition(); };
		Add(Cards, ModeCard(T, Auction, TEXT("Single player"), TEXT("Mega Auction"), TEXT("Ten franchises. One hammer."), true, ToAuction,
			CTA(T, TEXT("ENTER"), EButtonKind::Secondary, ToAuction, 56.f)), FMargin(0.f, 0.f, S2, 0.f), 1.f);
		FTileLook Soon;
		Soon.Plate = BatterArt;
		Soon.PlateTint = Hex(0x7080A8, 0.5f);
		Add(Cards, ModeCard(T, Soon, TEXT("Online"), TEXT("Live Super Over"), TEXT("You vs a rival."), false,
			[Root]() { Coming(Root, TEXT("Live Super Over"), TEXT("You vs a rival, six balls each.")); }), FMargin(0.f, 0.f, S2, 0.f), 1.f);
		Soon.Plate = FranchiseArt;
		// The auction-built season lives here once the hammer falls; before that, the road starts at the auction.
		auto ToSeason = [Root]()
		{
			if (UIPLSeasonSave::Get() && UIPLSeasonSave::Get()->bHasSeason) Root->OpenSeasonHub();
			else Coming(Root, TEXT("League Season"), TEXT("Play the Mega Auction first; the squads you build become the tournament."));
		};
		Add(Cards, ModeCard(T, Soon, TEXT("Career"), TEXT("League Season"), TEXT("The road to the final."),
			UIPLSeasonSave::Get() && UIPLSeasonSave::Get()->bHasSeason, ToSeason), FMargin(0.f), 1.f);
		for (int32 I = 0; I < Cards->GetChildrenCount(); ++I) Animate(Cards->GetChildAt(I), EMotion::Enter, 0.07f * I);
		Add(Page, Cards, FMargin(0.f), 1.f);
		return Page;
	}

	UWidget* MatchFormat(UFrontendRoot* Root)
	{
		UWidgetTree* T = Root->WidgetTree;
		UOverlay* Page = Stack(T);
		Add(Page, CTA(T, TEXT("‹  BACK"), EButtonKind::Secondary, [Root]() { Root->ShowTab(EFrontendTab::Play); }, 52.f), HAlign_Left, VAlign_Top);
		UVerticalBox* Options = VBox(T);
		Add(Options, Text(T, TEXT("SELECT FORMAT"), 44, Ink(), EWeight::Black, 20), FMargin(0.f, 0.f, 0.f, S2), 0.f, HAlign_Center);
		for (int32 Overs : {1, 3, 5, 10, 20})
		{
			const FString Label = Overs == 1 ? TEXT("SUPER OVER") : FString::Printf(TEXT("%d OVERS"), Overs);
			Add(Options, CTA(T, Label, EButtonKind::Primary, [Root, Overs]() { Root->SelectMatchOvers(Overs); }, 60.f), FMargin(0.f, 0.f, 0.f, S1));
		}
		Add(Page, Options, HAlign_Center, VAlign_Center);
		return Page;
	}

	UWidget* MatchSetup(UFrontendRoot* Root)
	{
		UWidgetTree* T = Root->WidgetTree;
		UFrontendSettingsSave* S = UFrontendSettingsSave::Get();
		UOverlay* Page = Stack(T);
		Add(Page, Versus(T, 700.f), HAlign_Fill, VAlign_Top, FMargin(0.f, 0.f, 0.f, 0.f));
		Add(Page, CTA(T, TEXT("‹  BACK"), EButtonKind::Secondary, [Root]() { Root->ShowTab(EFrontendTab::MatchFormat); }, 52.f),
			HAlign_Left, VAlign_Top);
		Add(Page, Text(T, TEXT("MATCH SETUP"), 18, GoldHi(), EWeight::Bold, 300), HAlign_Center, VAlign_Top, FMargin(0.f, 14.f));

		UHorizontalBox* Plates = HBox(T);
		Add(Plates, NamePlate(T, TEXT("HOME XI"), TEXT("You  /  Bat first"), Blue(), false), FMargin(0.f), 1.f, VAlign_Bottom);
		Add(Plates, NamePlate(T, TEXT("AWAY XI"), TEXT("AI  /  Bowl first"), Red(), true), FMargin(0.f), 1.f, VAlign_Bottom);

		UVerticalBox* Col = VBox(T);
		Add(Col, Plates, FMargin(S3, 0.f, S3, S3));

		UHorizontalBox* Controls = HBox(T);
		UVerticalBox* Difficulty = VBox(T);
		Add(Difficulty, Text(T, TEXT("AI DIFFICULTY"), 15, InkDim(), EWeight::Bold, 220), FMargin(0.f, 0.f, 0.f, S1));
		Add(Difficulty, Segmented(T, FrontendData::DifficultyNames(), FMath::Clamp(S->Difficulty, 0, 3),
			[S](int32 I) { S->Difficulty = I; S->Persist(); }));
		Add(Controls, Difficulty, FMargin(0.f, 0.f, S4, 0.f), 1.2f, VAlign_Center);
		UVerticalBox* Venue = VBox(T);
		Add(Venue, Text(T, TEXT("VENUE"), 15, InkDim(), EWeight::Bold, 220), FMargin(0.f, 0.f, 0.f, S1));
		const TArray<FString> Venues = FrontendData::VenueNames();
		UTextBlock* VenueName = Text(T, Venues[FMath::Clamp(S->Venue + 1, 0, Venues.Num() - 1)].ToUpper(), 24, Ink(), EWeight::Black, 40);
		UHorizontalBox* Pick = HBox(T);
		Add(Pick, CTA(T, TEXT("‹"), EButtonKind::Secondary, [S, VenueName, Venues]() {
			S->Venue = (S->Venue + Venues.Num()) % Venues.Num() - 1;
			VenueName->SetText(FText::FromString(Venues[S->Venue + 1].ToUpper())); S->Persist();
		}, 56.f), FMargin(0.f, 0.f, S2, 0.f));
		Add(Pick, VenueName, FMargin(0.f, 0.f, S2, 0.f), 1.f, VAlign_Center);
		Add(Pick, CTA(T, TEXT("›"), EButtonKind::Secondary, [S, VenueName, Venues]() {
			S->Venue = (S->Venue + 2) % Venues.Num() - 1;
			VenueName->SetText(FText::FromString(Venues[S->Venue + 1].ToUpper())); S->Persist();
		}, 56.f));
		Add(Venue, Pick);
		Add(Controls, Venue, FMargin(0.f, 0.f, S4, 0.f), 0.8f, VAlign_Center);
		Add(Controls, CTA(T, TEXT("START MATCH"), EButtonKind::Primary, [Root]() { Root->StartSuperOver(); }, 76.f),
			FMargin(S2, 0.f, S2, 0.f), 0.f, VAlign_Bottom);
		Add(Col, Box(T, Rounded(Glass(), 4.f, Line()), FMargin(S4, S3), Controls));
		Add(Page, Col, HAlign_Fill, VAlign_Bottom);
		return Page;
	}

	UWidget* Franchise(UFrontendRoot* Root)
	{
		UWidgetTree* T = Root->WidgetTree;
		UVerticalBox* Page = VBox(T);
		UVerticalBox* Copy = VBox(T);
		Add(Copy, Chip(T, TEXT("Coming soon"), GoldHi()), FMargin(0.f), 0.f, HAlign_Left);
		Add(Copy, Space(T, 0.f, 0.f), FMargin(0.f), 1.f);
		Add(Copy, Text(T, TEXT("CAREER MODE"), 18, Teal(), EWeight::Bold, 300));
		Add(Copy, Text(T, TEXT("FRANCHISE"), 96, Ink(), EWeight::Black), FMargin(0.f, -16.f, 0.f, -10.f));
		Add(Copy, Text(T, TEXT("Build a team. Own the league."), 24, InkDim(), EWeight::Medium));
		FTileLook Header;
		Header.Face = RoyalFace;
		Header.Plate = FranchiseArt;
		Header.PlateTint = Hex(0xFFFFFF, 0.6f);
		Header.Accent = Gold();
		Header.Figure = Picture(T, TEXT("T_CRICKET26_Crest_Home"), 300.f);
		Add(Page, Sized(T, Tile(T, Header, Copy, [Root]() { Coming(Root, TEXT("Franchise"), TEXT("Build a team. Own the league.")); }),
			0.f, 340.f), FMargin(0.f, 0.f, 0.f, S3));
		// The squad as it stands today, from the same data the match uses; the strongest player gets the elite card.
		Add(Page, Text(T, TEXT("YOUR SQUAD"), 30, Ink(), EWeight::Black, 20), FMargin(0.f, 0.f, 0.f, S2));
		const TArray<FFranchisePlayerRow> Squad = FrontendData::Squad();
		const int32 Best = BestIndex(Squad);
		UHorizontalBox* Cards = HBox(T);
		for (int32 I = 0; I < Squad.Num(); ++I)
		{
			UWidget* Card = PlayerCard(T, Squad[I], SquadRender(I), I == Best, 380.f);
			Add(Cards, Card, FMargin(0.f, 0.f, S3, 0.f));
			Animate(Card, EMotion::Enter, 0.06f * I);
		}
		Add(Page, Cards, FMargin(0.f, 0.f, 0.f, S4), 0.f, HAlign_Left);
		Add(Page, Text(T, TEXT("THE ROAD TO THE TITLE"), 30, Ink(), EWeight::Black, 20), FMargin(0.f, 0.f, 0.f, S2));
		struct FChapter { const TCHAR* Title; const TCHAR* Line; };
		const FChapter Chapters[] = {
			{ TEXT("Choose a team"), TEXT("Your franchise") }, { TEXT("AI Auction"), TEXT("Win the bid") },
			{ TEXT("Build squad"), TEXT("Shape the XI") }, { TEXT("Scout"), TEXT("Find stars") },
			{ TEXT("League season"), TEXT("Play the league") }, { TEXT("Playoffs"), TEXT("Stay alive") },
			{ TEXT("Dynasty"), TEXT("Own the era") }
		};
		UScrollBox* Journey = T->ConstructWidget<UScrollBox>();
		Journey->SetOrientation(Orient_Horizontal);
		Journey->SetScrollBarVisibility(ESlateVisibility::Collapsed);
		UHorizontalBox* Track = HBox(T);
		for (int32 I = 0; I < UE_ARRAY_COUNT(Chapters); ++I)
		{
			UVerticalBox* Node = VBox(T);
			Add(Node, Text(T, FString::Printf(TEXT("%02d"), I + 1), 40, GoldHi(), EWeight::Black));
			Add(Node, Text(T, FString(Chapters[I].Title).ToUpper(), 22, Ink(), EWeight::Black, 20));
			Add(Node, Text(T, Chapters[I].Line, 17, InkDim(), EWeight::Medium));
			UBorder* Card = Box(T, Rounded(Glass(), 4.f, Line()), FMargin(S3, S2), Node);
			Add(Track, Button(T, Sized(T, Card, 230.f, 0.f), EButtonKind::Quiet,
				[Root, Title = FString(Chapters[I].Title), Line = FString(Chapters[I].Line)]()
				{
					if (Title == TEXT("AI Auction")) Root->OpenAuctionWithTransition();
					else if (Title == TEXT("League season") || Title == TEXT("Playoffs"))
					{
						if (UIPLSeasonSave::Get() && UIPLSeasonSave::Get()->bHasSeason) Root->OpenSeasonHub();
						else Coming(Root, Title, Line);
					}
					else Coming(Root, Title, Line);
				}, 4.f),
				FMargin(0.f, 0.f, S2, 0.f));
		}
		Journey->AddChild(Track);
		Add(Page, Journey);

		return Page;
	}

	UWidget* Live(UFrontendRoot* Root)
	{
		UWidgetTree* T = Root->WidgetTree;
		UVerticalBox* Page = VBox(T);
		Add(Page, Text(T, TEXT("PLAY TOGETHER"), 44, Ink(), EWeight::Black, 20), FMargin(0.f, 0.f, 0.f, S2));
		UHorizontalBox* Row = HBox(T);
		Add(Row, SoonTile(T, Root, FranchiseArt, TEXT("Live Auction"), TEXT("Real players. One auction room."), TEXT("Online")),
			FMargin(0.f, 0.f, S2, 0.f), 1.2f);
		Add(Row, SoonTile(T, Root, BatterArt, TEXT("Live Super Over"), TEXT("You vs a rival. Six balls."), TEXT("Online")), FMargin(0.f), 1.f);
		Add(Page, Row, FMargin(0.f), 1.f);
		return Page;
	}

	UWidget* Store(UFrontendRoot* Root)
	{
		UWidgetTree* T = Root->WidgetTree;
		UVerticalBox* Page = VBox(T);
		Add(Page, Text(T, TEXT("STORE"), 44, Ink(), EWeight::Black, 20), FMargin(0.f, 0.f, 0.f, S2));
		FTileLook Look;
		Look.Face = GoldFace;
		Look.Plate = FranchiseArt;
		Look.PlateTint = Hex(0x7080A8, 0.6f);
		Look.Shade = 0.9f;
		Look.Figure = Picture(T, TEXT("T_CRICKET26_Cutout_Allrounder"), 640.f, Hex(0xB8C2DE));
		const FString Title = TEXT("Make it yours"), Line = TEXT("Kits and match presentation are on the way.");
		Add(Page, Tile(T, Look, TileCopy(T, Chip(T, TEXT("Coming soon"), GoldHi()), TEXT("Kit room"), Teal(), Title, 64, Line),
			[Root, Title, Line]() { Coming(Root, Title, Line); }), FMargin(0.f), 1.f);
		return Page;
	}

	UWidget* Settings(UFrontendRoot* Root)
	{
		UWidgetTree* T = Root->WidgetTree;
		UFrontendSettingsSave* S = UFrontendSettingsSave::Get();
		UVerticalBox* Col = VBox(T);
		Add(Col, Text(T, TEXT("SETTINGS"), 44, Ink(), EWeight::Black, 20), FMargin(0.f, 0.f, 0.f, S3));
		Add(Col, Text(T, TEXT("AUDIO"), 17, GoldHi(), EWeight::Bold, 260));
		Add(Col, SettingRow(T, TEXT("Master volume"), TEXT("Crowd, commentary and match sound"),
			Slider(T, S->MasterVolume, [S](float V) { S->MasterVolume = V; S->Persist(); S->ApplyGlobal(); }), true),
			FMargin(0.f, S1, 0.f, S3));
		Add(Col, Divider(T), FMargin(0.f, 0.f, 0.f, S3));
		Add(Col, Text(T, TEXT("GRAPHICS"), 17, GoldHi(), EWeight::Bold, 260));
		Add(Col, SettingRow(T, TEXT("Quality"), TEXT("Applies next match"),
			Segmented(T, { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") }, FMath::Clamp(S->Quality, 0, 3),
				[S](int32 I) { S->Quality = I; S->Persist(); }), true), FMargin(0.f, S1));
		Add(Col, SettingRow(T, TEXT("Frame rate"), TEXT("30 saves battery"),
			Segmented(T, { TEXT("30 FPS"), TEXT("60 FPS") }, S->bHighFrameRate ? 1 : 0,
				[S](int32 I) { S->bHighFrameRate = I == 1; S->Persist(); }), true), FMargin(0.f, S1, 0.f, S3));
		Add(Col, CTA(T, TEXT("‹  BACK TO HOME"), EButtonKind::Secondary, [Root]() { Root->ShowTab(EFrontendTab::Home); }, 56.f),
			FMargin(0.f, S2), 0.f, HAlign_Left);
		UVerticalBox* Page = VBox(T);
		Add(Page, Sized(T, Box(T, Rounded(Glass(), 4.f, Line()), FMargin(S5, S4), Col), 1200.f, 0.f), FMargin(0.f), 0.f, HAlign_Center);
		return Page;
	}

	// ---- IPL season hub (reads UIPLSeasonSave; every action goes through UFrontendRoot) ----

	FString TeamCode(int32 T)
	{
		const TArray<FAuctionFranchise>& Fr = AuctionData::Franchises();
		return Fr.IsValidIndex(T) ? Fr[T].Code : FString::Printf(TEXT("T%d"), T);
	}

	FString TeamFullName(int32 T)
	{
		const TArray<FAuctionFranchise>& Fr = AuctionData::Franchises();
		return Fr.IsValidIndex(T) ? Fr[T].Name : TeamCode(T);
	}

	void HubSection(UWidgetTree* T, UVerticalBox* Page, const FString& Title)
	{
		Add(Page, Text(T, Title, 26, Ink(), EWeight::Black, 20), FMargin(0.f, S3, 0.f, S1));
	}

	FString FixtureLabel(const FIPLFixture& F)
	{
		return FString::Printf(TEXT("#%d  •  %s vs %s"), F.MatchNumber, *TeamCode(F.Home), *TeamCode(F.Away));
	}

	UWidget* FixtureRow(UWidgetTree* T, UFrontendRoot* Root, const FIPLFixture& F)
	{
		UHorizontalBox* Row = HBox(T);
		UVerticalBox* Copy = VBox(T);
		Add(Copy, Text(T, FixtureLabel(F), 20, Ink(), EWeight::Black, 40));
		Add(Copy, Text(T, FString::Printf(TEXT("%s  •  %s"), *IPLSeason::StageName(F.Stage), *F.Venue).ToUpper(),
			14, InkDim(), EWeight::Bold, 160));
		Add(Row, Copy, FMargin(0.f), 1.f, VAlign_Center);
		if (F.Status == EIPLFixtureStatus::Completed && F.bHasResult)
		{
			Add(Row, Text(T, F.Result.Margin, 17, Teal(), EWeight::Bold, 40), FMargin(S2, 0.f, 0.f, 0.f), 0.f, VAlign_Center);
		}
		else
		{
			const int32 Fid = F.FixtureId;
			Add(Row, CTA(T, TEXT("SIMULATE"), EButtonKind::Secondary, [Root, Fid]() { Root->SimulateIPLFixture(Fid); }, 48.f),
				FMargin(S2, 0.f, 0.f, 0.f), 0.f, VAlign_Center);
		}
		return Box(T, Rounded(Glass(), 4.f, Line()), FMargin(S3, S1), Row);
	}

	UWidget* SeasonHub(UFrontendRoot* Root)
	{
		UWidgetTree* T = Root->WidgetTree;
		UVerticalBox* Page = VBox(T);
		UIPLSeasonSave* Save = UIPLSeasonSave::Get();
		if (!Save || !Save->bHasSeason)
		{
			Add(Page, Text(T, TEXT("IPL SEASON"), 44, Ink(), EWeight::Black, 20), FMargin(0.f, 0.f, 0.f, S2));
			Add(Page, Text(T, TEXT("No season yet. Play the Mega Auction first: the squads you build under the hammer become the teams of the tournament."),
				20, InkDim(), EWeight::Medium), FMargin(0.f, 0.f, 0.f, S3));
			Add(Page, CTA(T, TEXT("PLAY THE AUCTION"), EButtonKind::Primary, [Root]() { Root->OpenAuctionWithTransition(); }, 64.f),
				FMargin(0.f), 0.f, HAlign_Left);
			return Page;
		}
		if (Root->IPLView == 1) return TeamSelect(Root);
		const FIPLSeason& Season = Save->Season;
		const int32 User = Season.UserTeam;

		Add(Page, Text(T, TEXT("IPL SEASON"), 44, Ink(), EWeight::Black, 20), FMargin(0.f, 0.f, 0.f, 0.f));
		UHorizontalBox* Sub = HBox(T);
		Add(Sub, Chip(T, IPLSeason::StageName(Season.Stage).ToUpper(), GoldHi()), FMargin(0.f, 0.f, S2, 0.f), 0.f, VAlign_Center);
		Add(Sub, Chip(T, FString::Printf(TEXT("YOU: %s"), *TeamCode(User)), Teal(), true), FMargin(0.f), 0.f, VAlign_Center);
		Add(Page, Sub, FMargin(0.f, 0.f, 0.f, S1));

		if (Season.bComplete)
		{
			const FString Line = FString::Printf(TEXT("%s ARE THE CHAMPIONS"), *TeamFullName(Season.Champion).ToUpper());
			Add(Page, Box(T, Rounded(Glass(), 4.f, Gold()), FMargin(S3, S2), Text(T, Line, 30, GoldHi(), EWeight::Black, 20)),
				FMargin(0.f, 0.f, 0.f, S2));
			// The career goes on: the squads carry into the next auction, a mini one or, every three years, a mega.
			const int32 NextYear = Season.Year + 1;
			const FString Next = FString::Printf(TEXT("NEXT: IPL %d %s"), NextYear, AuctionRules::IsMegaSeason(NextYear) ? TEXT("MEGA AUCTION") : TEXT("AUCTION"));
			Add(Page, CTA(T, Next, EButtonKind::Primary, [Root]() { Root->OpenCareerAuctionWithTransition(); }, 64.f),
				FMargin(0.f, 0.f, 0.f, S2), 0.f, HAlign_Left);
		}

		// Next match: the user's next upcoming fixture, or the season-over note when eliminated.
		const FIPLFixture* Next = nullptr;
		for (const FIPLFixture& F : Season.Fixtures)
			if (F.Status == EIPLFixtureStatus::Upcoming && IPLSeason::InvolvesUser(Season, F)) { Next = &F; break; }
		HubSection(T, Page, TEXT("NEXT MATCH"));
		if (Next)
		{
			UVerticalBox* Card = VBox(T);
			Add(Card, Text(T, FString::Printf(TEXT("%s  vs  %s"), *TeamFullName(Next->Home).ToUpper(), *TeamFullName(Next->Away).ToUpper()),
				34, Ink(), EWeight::Black, 20));
			Add(Card, Text(T, FString::Printf(TEXT("%s  •  %s"), *IPLSeason::StageName(Next->Stage), *Next->Venue).ToUpper(),
				16, InkDim(), EWeight::Bold, 200), FMargin(0.f, 0.f, 0.f, S2));
			const int32 Fid = Next->FixtureId;
			Add(Card, CTA(T, TEXT("PLAY MATCH"), EButtonKind::Primary, [Root, Fid]() { Root->OpenTeamSelect(Fid); }, 64.f),
				FMargin(0.f), 0.f, HAlign_Left);
			Add(Page, Box(T, Rounded(Glass(), 4.f, Gold()), FMargin(S3, S2), Card));
		}
		else if (!Season.bComplete)
		{
			Add(Page, Box(T, Rounded(Glass(), 4.f, Line()), FMargin(S3, S2),
				Text(T, TEXT("Your season is over. Simulate the remaining fixtures to crown the champion."), 19, InkDim(), EWeight::Medium)));
		}

		// Upcoming AI fixtures with simulate actions (capped so the page stays light).
		HubSection(T, Page, TEXT("FIXTURES"));
		int32 Shown = 0, Rest = 0;
		for (const FIPLFixture& F : Season.Fixtures)
		{
			if (F.Status != EIPLFixtureStatus::Upcoming || IPLSeason::InvolvesUser(Season, F)) continue;
			if (Shown < 6) Add(Page, FixtureRow(T, Root, F));
			else Rest++;
			Shown++;
		}
		if (Shown == 0) Add(Page, Text(T, TEXT("No fixtures left to simulate."), 18, InkDim(), EWeight::Medium));
		else if (Rest > 0) Add(Page, Text(T, FString::Printf(TEXT("+ %d more fixtures"), Rest), 16, InkFaint(), EWeight::Bold, 160));

		// Latest results across the whole season: one standings system for played and simmed alike.
		HubSection(T, Page, TEXT("RESULTS"));
		int32 Done = 0;
		for (int32 I = Season.Fixtures.Num() - 1; I >= 0 && Done < 6; --I)
		{
			const FIPLFixture& F = Season.Fixtures[I];
			if (F.Status != EIPLFixtureStatus::Completed) continue;
			Add(Page, FixtureRow(T, Root, F));
			Done++;
		}
		if (Done == 0) Add(Page, Text(T, TEXT("No results yet."), 18, InkDim(), EWeight::Medium));

		// Points table: points, then wins, then net run rate.
		HubSection(T, Page, TEXT("POINTS TABLE"));
		const TArray<int32> Order = IPLSeason::SortedTable(Season);
		for (int32 Pos = 0; Pos < Order.Num(); ++Pos)
		{
			const FIPLTableRow& R = Season.Table[Order[Pos]];
			const bool bMine = Order[Pos] == User;
			UHorizontalBox* Row = HBox(T);
			Add(Row, Text(T, FString::Printf(TEXT("%d"), Pos + 1), 20, bMine ? GoldHi() : InkDim(), EWeight::Black, 40),
				FMargin(0.f), 0.25f, VAlign_Center);
			Add(Row, Text(T, TeamCode(Order[Pos]), 22, bMine ? GoldHi() : Ink(), EWeight::Black, 40),
				FMargin(0.f), 1.f, VAlign_Center);
			Add(Row, Text(T, FString::Printf(TEXT("%d  %d  %d  %d"), R.Played, R.Won, R.Lost, R.NoResult),
				18, InkDim(), EWeight::Bold, 60), FMargin(0.f), 1.2f, VAlign_Center);
			const FString NRR = (R.NetRunRate >= 0 ? TEXT("+") : TEXT("")) + FString::Printf(TEXT("%.3f"), R.NetRunRate);
			Add(Row, Text(T, NRR, 18, InkDim(), EWeight::Bold, 60), FMargin(0.f), 0.7f, VAlign_Center);
			Add(Row, Text(T, FString::Printf(TEXT("%d PTS"), R.Points), 20, bMine ? GoldHi() : Ink(), EWeight::Black, 40),
				FMargin(0.f), 0.7f, VAlign_Center);
			Add(Page, Box(T, Rounded(bMine ? Glass() : FLinearColor::Transparent, 4.f, bMine ? Gold() : Line()), FMargin(S2, S1), Row));
		}

		// The user's auction-built squad: the same players the matches are played with.
		HubSection(T, Page, TEXT("MY SQUAD"));
		const TArray<FAuctionPlayer>& Players = AuctionData::Players();
		if (Season.Squads.IsValidIndex(User))
		{
			for (const FIPLSquadPlayer& S : Season.Squads[User].Players)
			{
				if (!Players.IsValidIndex(S.PlayerId)) continue;
				const FAuctionPlayer& P = Players[S.PlayerId];
				UHorizontalBox* Row = HBox(T);
				Add(Row, Text(T, P.Name, 19, Ink(), EWeight::Bold, 40), FMargin(0.f), 1.f, VAlign_Center);
				Add(Row, Text(T, FString(AuctionRules::RoleCode(P.Role)), 16, InkDim(), EWeight::Bold, 100),
					FMargin(0.f), 0.4f, VAlign_Center);
				Add(Row, Text(T, FString::Printf(TEXT("OVR %d"), P.Overall()), 16, InkDim(), EWeight::Bold, 60),
					FMargin(0.f), 0.5f, VAlign_Center);
				Add(Row, Text(T, AuctionRules::Money(S.Price), 16, Teal(), EWeight::Bold, 40),
					FMargin(0.f), 0.6f, VAlign_Center);
				Add(Page, Row, FMargin(S2, 0.f, 0.f, 2.f));
			}
		}
		return Page;
	}

	UWidget* TeamSelect(UFrontendRoot* Root)
	{
		UWidgetTree* T = Root->WidgetTree;
		UVerticalBox* Page = VBox(T);
		UIPLSeasonSave* Save = UIPLSeasonSave::Get();
		if (!Save || !Save->bHasSeason)
		{
			Add(Page, CTA(T, TEXT("‹  SEASON"), EButtonKind::Secondary, [Root]() { Root->OpenSeasonHub(); }, 52.f),
				FMargin(0.f, 0.f, 0.f, S2), 0.f, HAlign_Left);
			Add(Page, Text(T, TEXT("No season loaded."), 22, InkDim(), EWeight::Medium));
			return Page;
		}
		const FIPLSeason& Season = Save->Season;
		const int32 Fi = IPLSeason::FixtureIndex(Season, Root->IPLSelectFixture);
		if (Fi == INDEX_NONE)
		{
			Add(Page, CTA(T, TEXT("‹  SEASON"), EButtonKind::Secondary, [Root]() { Root->OpenSeasonHub(); }, 52.f),
				FMargin(0.f, 0.f, 0.f, S2), 0.f, HAlign_Left);
			Add(Page, Text(T, TEXT("That fixture is gone."), 22, InkDim(), EWeight::Medium));
			return Page;
		}
		const FIPLFixture& F = Season.Fixtures[Fi];
		const int32 User = Season.UserTeam;
		Add(Page, CTA(T, TEXT("‹  SEASON"), EButtonKind::Secondary, [Root]() { Root->OpenSeasonHub(); }, 52.f),
			FMargin(0.f, 0.f, 0.f, S2), 0.f, HAlign_Left);
		Add(Page, Text(T, TEXT("SELECT YOUR XI"), 44, Ink(), EWeight::Black, 20), FMargin(0.f, 0.f, 0.f, 0.f));
		Add(Page, Text(T, FString::Printf(TEXT("%s  •  TAP 11 IN BATTING ORDER, FIRST TWO OPEN"), *FixtureLabel(F)).ToUpper(),
			17, GoldHi(), EWeight::Bold, 200), FMargin(0.f, 0.f, 0.f, S2));

		const TArray<FAuctionPlayer>& Players = AuctionData::Players();
		TArray<int32> SquadIds;
		if (Season.Squads.IsValidIndex(User))
			for (const FIPLSquadPlayer& S : Season.Squads[User].Players) SquadIds.Add(S.PlayerId);
		SquadIds.Sort([&Players](int32 A, int32 B)
		{
			const int32 OA = Players.IsValidIndex(A) ? Players[A].Overall() : 0;
			const int32 OB = Players.IsValidIndex(B) ? Players[B].Overall() : 0;
			return OA > OB;
		});
		for (int32 Id : SquadIds)
		{
			if (!Players.IsValidIndex(Id)) continue;
			const FAuctionPlayer& P = Players[Id];
			const int32 PickPos = Root->IPLXIOrder.Find(Id);
			const bool bPicked = PickPos != INDEX_NONE;
			UHorizontalBox* Row = HBox(T);
			Add(Row, Text(T, bPicked ? FString::Printf(TEXT("%02d"), PickPos + 1) : TEXT("--"), 20,
				bPicked ? GoldHi() : InkFaint(), EWeight::Black, 60), FMargin(0.f), 0.35f, VAlign_Center);
			Add(Row, Text(T, P.Name, 20, Ink(), EWeight::Bold, 40), FMargin(0.f), 1.f, VAlign_Center);
			Add(Row, Text(T, FString(AuctionRules::RoleCode(P.Role)), 16, InkDim(), EWeight::Bold, 100),
				FMargin(0.f), 0.4f, VAlign_Center);
			Add(Row, Text(T, FString::Printf(TEXT("OVR %d"), P.Overall()), 16, InkDim(), EWeight::Bold, 60),
				FMargin(0.f), 0.5f, VAlign_Center);
			UWidget* Card = Box(T, Rounded(Glass(), 4.f, bPicked ? Gold() : Line()), FMargin(S2, S1), Row);
			Add(Page, Button(T, Card, EButtonKind::Quiet, [Root, Id]() { Root->ToggleXIPlayer(Id); }, 4.f));
		}

		Add(Page, Space(T, 0.f, S2));
		if (Root->IPLXIOrder.Num() == IPLSeason::PlayingXI)
			Add(Page, CTA(T, TEXT("START MATCH"), EButtonKind::Primary, [Root]() { Root->StartIPLMatch(); }, 68.f),
				FMargin(0.f), 0.f, HAlign_Left);
		else
			Add(Page, Text(T, FString::Printf(TEXT("PICK 11  —  %d / 11"), Root->IPLXIOrder.Num()), 20, InkDim(), EWeight::Bold, 100));
		return Page;
	}
}

UWidget* FrontendScreens::Build(UFrontendRoot* Root, EFrontendTab Tab)
{
	switch (Tab)
	{
	case EFrontendTab::Home: return Home(Root);
	case EFrontendTab::Play: return Play(Root);
	case EFrontendTab::Auction: return Live(Root);
	case EFrontendTab::Franchise: return Franchise(Root);
	case EFrontendTab::Scouts: return Franchise(Root);
	case EFrontendTab::Store: return Store(Root);
	case EFrontendTab::Settings: return Settings(Root);
	case EFrontendTab::MatchSetup: return MatchSetup(Root);
	case EFrontendTab::MatchFormat: return MatchFormat(Root);
	case EFrontendTab::IPLSeason: return SeasonHub(Root);
	default: return Space(Root->WidgetTree, 0.f, 0.f);
	}
}
