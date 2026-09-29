#include "FrontendData.h"
#include "FrontendStyle.h"
#include "CricketAI.h"
#include "CricketStadium.h"
#include "SuperOverGameMode.h"

namespace FrontendData
{
	namespace
	{
		FFrontendFeature Feature(const TCHAR* Eyebrow, const TCHAR* Title, const TCHAR* Subtitle, EFeatureStatus Status, EFrontendTab Target, TArray<FString> Bullets = {})
		{
			FFrontendFeature F;
			F.Eyebrow = Eyebrow;
			F.Title = Title;
			F.Subtitle = Subtitle;
			F.Status = Status;
			F.Target = Target;
			F.Bullets = MoveTemp(Bullets);
			return F;
		}

		const FCricketTeam& HumanSide()
		{
			static const FCricketTeam Team = ASuperOverGameMode::DefaultSquads()[0];
			return Team;
		}

		int32 BatRating(const FCricketPlayer& P) { return FMath::RoundToInt(40.f + 55.f * (0.4f * P.Timing + 0.3f * P.Technique + 0.3f * P.Power)); }
		int32 BowlRating(const FCricketPlayer& P)
		{
			const float Pace = P.BowlerType == EBowlerType::Pace ? FMath::Clamp((P.PaceKph - 120.f) / 30.f, 0.f, 1.f) : 0.6f;
			return FMath::RoundToInt(40.f + 55.f * (0.45f * P.Accuracy + 0.35f * P.Movement + 0.2f * Pace));
		}
	}

	FString TabName(EFrontendTab Tab)
	{
		static const TCHAR* Names[] = { TEXT("Home"), TEXT("Play"), TEXT("Live"), TEXT("Franchise"), TEXT("Scouts"), TEXT("Store"), TEXT("Settings"), TEXT("Match Setup"), TEXT("Match Format"), TEXT("IPL Season") };
		return Tab < EFrontendTab::Count ? Names[uint8(Tab)] : TEXT("");
	}

	FString StatusLabel(EFeatureStatus Status)
	{
		switch (Status)
		{
		case EFeatureStatus::Available: return TEXT("PLAYABLE");
		case EFeatureStatus::InDevelopment: return TEXT("IN DEVELOPMENT");
		case EFeatureStatus::ComingSoon: return TEXT("COMING SOON");
		case EFeatureStatus::Locked: return TEXT("LOCKED");
		}
		return FString();
	}

	FLinearColor StatusTint(EFeatureStatus Status)
	{
		switch (Status)
		{
		case EFeatureStatus::Available: return FrontendStyle::Teal();
		case EFeatureStatus::InDevelopment: return FrontendStyle::Gold();
		case EFeatureStatus::ComingSoon: return FrontendStyle::InkDim();
		case EFeatureStatus::Locked: return FrontendStyle::InkFaint();
		}
		return FrontendStyle::Ink();
	}

	FFrontendFeature SuperOver()
	{
		return Feature(TEXT("PLAYABLE"), TEXT("SUPER OVER"), TEXT("6 balls. One winner."),
			EFeatureStatus::Available, EFrontendTab::MatchSetup,
			{ TEXT("One over each side, three batters, sudden death"), TEXT("Bat and bowl with full touch controls"), TEXT("Ties go to another Super Over") });
	}

	TArray<FFrontendFeature> MoreModes()
	{
		return {
			Feature(TEXT("MATCH"), TEXT("Quick Match"), TEXT("A short-format game between two full sides."), EFeatureStatus::ComingSoon, EFrontendTab::Play,
				{ TEXT("T5 and T10 formats"), TEXT("Pick both XIs and the venue"), TEXT("Full scorecard at the end") }),
			Feature(TEXT("SEASON"), TEXT("Franchise Season"), TEXT("A league campaign with the squad you build."), EFeatureStatus::ComingSoon, EFrontendTab::Play,
				{ TEXT("Fixtures, table and playoffs"), TEXT("Plays with the squad from your auction"), TEXT("Form, fatigue and injuries") }),
		};
	}

	TArray<FFrontendFeature> AuctionModes()
	{
		return {
			Feature(TEXT("SINGLE PLAYER"), TEXT("IPL Mega Auction"), TEXT("Run a franchise's table against nine AI front offices, under the real IPL rules."),
				EFeatureStatus::Available, EFrontendTab::Auction,
				{ TEXT("Nine AI owners that bid to their own needs"), TEXT("Retentions, Right to Match, purse, overseas and squad-size rules"), TEXT("Marquee sets and accelerated rounds") }),
			Feature(TEXT("ONLINE"), TEXT("Multiplayer Auction"), TEXT("A live room with your friends. One purse each, one hammer, no second chances."),
				EFeatureStatus::ComingSoon, EFrontendTab::Auction,
				{ TEXT("Private rooms with invite codes"), TEXT("Live bidding with a shot clock"), TEXT("AI fills empty seats") }),
		};
	}

	TArray<FFrontendFeature> HomeModules()
	{
		return {
			Feature(TEXT("YOUR TEAM"), TEXT("Franchise"), TEXT("Your XI, your colours, your call."), EFeatureStatus::ComingSoon, EFrontendTab::Franchise),
			Feature(TEXT("RECRUITMENT"), TEXT("Scouts"), TEXT("Find the next match-winner first."), EFeatureStatus::InDevelopment, EFrontendTab::Scouts),
			Feature(TEXT("KIT ROOM"), TEXT("Store"), TEXT("Kits, gear and scout reports."), EFeatureStatus::ComingSoon, EFrontendTab::Store),
		};
	}

	TArray<FFranchisePlayerRow> Squad()
	{
		const FCricketTeam& T = HumanSide();
		TArray<FFranchisePlayerRow> Rows;
		for (const FCricketPlayer& P : T.Batters)
		{
			FFranchisePlayerRow R;
			R.Name = P.Name;
			R.Role = TEXT("BAT");
			R.Detail = P.BatHand == ECricketHand::Left ? TEXT("Left-hand bat") : TEXT("Right-hand bat");
			R.Rating = BatRating(P);
			R.bInXI = true;
			Rows.Add(R);
		}
		const FCricketPlayer& B = T.Bowler;
		FFranchisePlayerRow R;
		R.Name = B.Name;
		R.Role = TEXT("BOWL");
		R.Detail = B.BowlerType == EBowlerType::Pace ? FString::Printf(TEXT("Pace · %d km/h"), FMath::RoundToInt(B.PaceKph))
			: B.BowlerType == EBowlerType::OffSpin ? TEXT("Off-spin") : TEXT("Leg-spin");
		R.Rating = BowlRating(B);
		R.bInXI = true;
		Rows.Add(R);
		return Rows;
	}

	FString FranchiseName() { return HumanSide().Name; }
	FString FranchiseShort() { return HumanSide().Short; }
	FLinearColor FranchiseColour() { return HumanSide().Colour; }
	FString FranchiseSponsor() { return HumanSide().Sponsor; }
	float PurseCrore() { return 90.f; }

	TArray<FScoutRegion> ScoutRegions()
	{
		auto Region = [](const TCHAR* Name, const TCHAR* Focus, EFeatureStatus S) { FScoutRegion R; R.Name = Name; R.Focus = Focus; R.Status = S; return R; };
		return { Region(TEXT("North"), TEXT("Pace and power hitters"), EFeatureStatus::InDevelopment),
			Region(TEXT("South"), TEXT("Spin and top-order technique"), EFeatureStatus::InDevelopment),
			Region(TEXT("East"), TEXT("Raw quicks and keepers"), EFeatureStatus::ComingSoon),
			Region(TEXT("West"), TEXT("Finishers and all-rounders"), EFeatureStatus::ComingSoon),
			Region(TEXT("Overseas"), TEXT("Marquee signings"), EFeatureStatus::Locked) };
	}

	TArray<FScoutProspect> Prospects()
	{
		auto P = [](const TCHAR* Name, const TCHAR* Role, const TCHAR* Region, int32 Rating, int32 Potential, bool bRevealed)
		{
			FScoutProspect S; S.Name = Name; S.Role = Role; S.Region = Region; S.Rating = Rating; S.Potential = Potential; S.bRevealed = bRevealed; return S;
		};
		return { P(TEXT("A. Rathore"), TEXT("Opener · RHB"), TEXT("North"), 72, 91, true),
			P(TEXT("J. D'Souza"), TEXT("Wrist-spin"), TEXT("South"), 68, 89, true),
			P(TEXT("T. Ngangom"), TEXT("Death pace"), TEXT("East"), 70, 88, true),
			P(TEXT("Unknown"), TEXT("Finisher · LHB"), TEXT("West"), 0, 0, false) };
	}

	TArray<FStoreOffer> StoreOffers()
	{
		auto O = [](const TCHAR* Title, const TCHAR* Detail, const TCHAR* Category, bool bFeatured)
		{
			FStoreOffer S; S.Title = Title; S.Detail = Detail; S.Category = Category; S.bFeatured = bFeatured; return S;
		};
		return { O(TEXT("Season 26 Home Kit"), TEXT("The launch kit, with your sponsor across the chest."), TEXT("Kits"), true),
			O(TEXT("Away Kit"), TEXT("Clash colours for the road."), TEXT("Kits"), false),
			O(TEXT("Willow Pro Bat"), TEXT("Cosmetic bat skin with a gold sticker."), TEXT("Gear"), false),
			O(TEXT("Scout Report Pack"), TEXT("Three reports that reveal a prospect's numbers."), TEXT("Scouting"), false),
			O(TEXT("Stadium Lights"), TEXT("Night-match lighting preset for your home ground."), TEXT("Stadium"), false),
			O(TEXT("Season Pass"), TEXT("A reward track that runs with the franchise season."), TEXT("Passes"), false) };
	}

	TArray<FString> StoreCategories() { return { TEXT("All"), TEXT("Kits"), TEXT("Gear"), TEXT("Scouting"), TEXT("Stadium"), TEXT("Passes") }; }

	TArray<FString> VenueNames()
	{
		TArray<FString> Names = { TEXT("Random") };
		for (int32 I = 0; I < CricketStadium::NumVenues; ++I) Names.Add(CricketStadium::Venue(I).Name);
		for (FString& N : Names)
		{
			// "HARBOURSIDE OVAL" -> "Harbourside Oval": the stadium stores broadcast caps, the menu uses title case.
			TArray<FString> Words;
			N.ParseIntoArray(Words, TEXT(" "));
			for (FString& W : Words) W = W.Left(1).ToUpper() + W.Mid(1).ToLower();
			N = FString::Join(Words, TEXT(" "));
		}
		return Names;
	}

	FString VenueConditions(int32 Venue)
	{
		if (Venue < 0 || Venue >= CricketStadium::NumVenues) return TEXT("A different ground, pitch and sky each match");
		const CricketStadium::FVenue& V = CricketStadium::Venue(Venue);
		const TCHAR* Pitch = V.Pitch == EPitchType::Green ? TEXT("Green seamer") : V.Pitch == EPitchType::Dusty ? TEXT("Dry turner") : TEXT("Flat deck");
		const TCHAR* Sky = V.bNight ? TEXT("Under lights") : V.Cloud > 0.5f ? TEXT("Overcast") : TEXT("Bright afternoon");
		return FString::Printf(TEXT("%s  \u00B7  %s"), Pitch, Sky);
	}

	TArray<FString> DifficultyNames()
	{
		TArray<FString> Names;
		for (uint8 I = 0; I < 4; ++I) Names.Add(CricketAI::DifficultyName(CricketAI::EDifficulty(I)));
		return Names;
	}
}
