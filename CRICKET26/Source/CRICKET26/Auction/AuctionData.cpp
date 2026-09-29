#include "AuctionTypes.h"

namespace
{
	// The roster, one CSV line per entry, generated from Scripts/auction/Players.csv by Scripts/auction/roster.py.
	#include "AuctionRoster.inl"

	FAuctionFranchise Make(const TCHAR* Code, const TCHAR* Name, const TCHAR* Short, const TCHAR* City, const TCHAR* Home,
		uint32 Primary, uint32 Secondary, int32 Titles)
	{
		FAuctionFranchise F;
		F.Code = Code;
		F.Name = Name;
		F.Short = Short;
		F.City = City;
		F.Home = Home;
		F.Primary = FLinearColor::FromSRGBColor(FColor((Primary >> 16) & 255, (Primary >> 8) & 255, Primary & 255));
		F.Secondary = FLinearColor::FromSRGBColor(FColor((Secondary >> 16) & 255, (Secondary >> 8) & 255, Secondary & 255));
		F.Titles = Titles;
		return F;
	}

	// Aggression, star, youth, overseas, loyalty, patience; then role bias BA, WK, AL, FA, SP.
	void Style(FAuctionFranchise& F, float Aggression, float Star, float Youth, float Overseas, float Loyalty, float Patience,
		float Ba, float Wk, float Al, float Fa, float Sp)
	{
		F.Aggression = Aggression; F.StarBias = Star; F.YouthBias = Youth; F.OverseasBias = Overseas; F.Loyalty = Loyalty; F.Patience = Patience;
		const float Bias[] = { Ba, Wk, Al, Fa, Sp };
		for (int32 I = 0; I < int32(EAuctionRole::Count); ++I) F.RoleBias[I] = Bias[I];
	}

	EAuctionRole ParseRole(const FString& S)
	{
		if (S == TEXT("WK")) return EAuctionRole::Keeper;
		if (S == TEXT("AR")) return EAuctionRole::AllRounder;
		if (S == TEXT("PACE")) return EAuctionRole::Pace;
		if (S == TEXT("SPIN")) return EAuctionRole::Spin;
		return EAuctionRole::Batter;
	}
}

int32 FAuctionPlayer::Overall() const
{
	switch (Role)
	{
	case EAuctionRole::Batter: return BatRating;
	case EAuctionRole::Keeper: return FMath::Min(99, BatRating + 2);
	case EAuctionRole::Pace:
	case EAuctionRole::Spin: return BowlRating;
	default:
	{
		const int32 Hi = FMath::Max(BatRating, BowlRating), Lo = FMath::Min(BatRating, BowlRating);
		return FMath::Min(99, FMath::RoundToInt(FMath::Max(0.95f * Hi, 0.62f * Hi + 0.45f * Lo)));
	}
	}
}

namespace AuctionRules
{
	int32 CappedRetentionCost(int32 Index)
	{
		static const int32 Slab[] = { 1800, 1400, 1100, 1800, 1400 };
		return Slab[FMath::Clamp(Index, 0, 4)];
	}

	int32 NextBid(int32 Current)
	{
		if (Current < 100) return Current + 5;
		if (Current < 200) return Current + 10;
		if (Current < 500) return Current + 20;
		return Current + 25;
	}

	FString Money(int32 Lakh)
	{
		return Lakh >= 100 ? FString::Printf(TEXT("₹%.2fCr"), Lakh / 100.0) : FString::Printf(TEXT("₹%dL"), Lakh);
	}

	FString Spoken(int32 Lakh)
	{
		if (Lakh < 100) return FString::Printf(TEXT("%d lakh"), Lakh);
		const int32 Crore = Lakh / 100, Rest = Lakh % 100;
		return Rest ? FString::Printf(TEXT("%d crore %d"), Crore, Rest) : FString::Printf(TEXT("%d crore"), Crore);
	}

	const TCHAR* RoleName(EAuctionRole Role)
	{
		static const TCHAR* Names[] = { TEXT("BATTER"), TEXT("WICKETKEEPER"), TEXT("ALL-ROUNDER"), TEXT("FAST BOWLER"), TEXT("SPINNER") };
		return Names[FMath::Min(int32(Role), 4)];
	}

	const TCHAR* RoleCode(EAuctionRole Role)
	{
		static const TCHAR* Codes[] = { TEXT("BA"), TEXT("WK"), TEXT("AL"), TEXT("FA"), TEXT("SP") };
		return Codes[FMath::Min(int32(Role), 4)];
	}
}

namespace AuctionData
{
	const TArray<FAuctionFranchise>& Franchises()
	{
		static const TArray<FAuctionFranchise> All = []()
		{
			TArray<FAuctionFranchise> T;
			T.Add(Make(TEXT("CSK"), TEXT("Chennai Super Kings"), TEXT("SUPER KINGS"), TEXT("Chennai"), TEXT("MA Chidambaram Stadium"), 0xF9CD05, 0x0B4EA2, 5));
			Style(T.Last(), 0.95f, 1.0f, 0.85f, 1.0f, 1.35f, 0.7f, 1.0f, 1.0f, 1.15f, 0.95f, 1.25f);
			T.Add(Make(TEXT("MI"), TEXT("Mumbai Indians"), TEXT("INDIANS"), TEXT("Mumbai"), TEXT("Wankhede Stadium"), 0x004BA0, 0xD1AB3E, 5));
			Style(T.Last(), 1.05f, 1.15f, 1.15f, 1.0f, 1.2f, 0.6f, 1.0f, 1.0f, 1.05f, 1.2f, 0.95f);
			T.Add(Make(TEXT("RCB"), TEXT("Royal Challengers Bengaluru"), TEXT("ROYAL CHALLENGERS"), TEXT("Bengaluru"), TEXT("M Chinnaswamy Stadium"), 0xD6121F, 0x1A1A1A, 1));
			Style(T.Last(), 1.1f, 1.2f, 1.0f, 1.05f, 1.1f, 0.35f, 1.15f, 1.0f, 1.0f, 1.1f, 0.95f);
			T.Add(Make(TEXT("KKR"), TEXT("Kolkata Knight Riders"), TEXT("KNIGHT RIDERS"), TEXT("Kolkata"), TEXT("Eden Gardens"), 0x3A225D, 0xB3A123, 3));
			Style(T.Last(), 1.05f, 1.0f, 1.05f, 1.1f, 1.25f, 0.55f, 0.95f, 1.0f, 1.25f, 0.95f, 1.15f);
			T.Add(Make(TEXT("SRH"), TEXT("Sunrisers Hyderabad"), TEXT("SUNRISERS"), TEXT("Hyderabad"), TEXT("Rajiv Gandhi International Stadium"), 0xF26522, 0x1A1A1A, 1));
			Style(T.Last(), 1.1f, 1.1f, 1.0f, 1.1f, 1.1f, 0.4f, 1.2f, 1.0f, 1.0f, 1.15f, 0.9f);
			T.Add(Make(TEXT("DC"), TEXT("Delhi Capitals"), TEXT("CAPITALS"), TEXT("Delhi"), TEXT("Arun Jaitley Stadium"), 0x17479E, 0xEF1B23, 0));
			Style(T.Last(), 1.0f, 1.05f, 1.1f, 1.0f, 1.05f, 0.5f, 1.0f, 1.15f, 1.0f, 1.05f, 1.0f);
			T.Add(Make(TEXT("PBKS"), TEXT("Punjab Kings"), TEXT("KINGS"), TEXT("Mullanpur"), TEXT("Maharaja Yadavindra Singh Stadium"), 0xDD1F2D, 0xA7A9AC, 0));
			Style(T.Last(), 1.15f, 1.15f, 1.0f, 1.0f, 1.0f, 0.3f, 1.0f, 1.0f, 1.05f, 1.05f, 1.0f);
			T.Add(Make(TEXT("RR"), TEXT("Rajasthan Royals"), TEXT("ROYALS"), TEXT("Jaipur"), TEXT("Sawai Mansingh Stadium"), 0xEA1A85, 0x254AA5, 1));
			Style(T.Last(), 0.95f, 0.95f, 1.25f, 1.0f, 1.15f, 0.6f, 1.05f, 1.0f, 1.0f, 1.0f, 1.1f);
			T.Add(Make(TEXT("GT"), TEXT("Gujarat Titans"), TEXT("TITANS"), TEXT("Ahmedabad"), TEXT("Narendra Modi Stadium"), 0x1B2A4A, 0xDBBE6E, 1));
			Style(T.Last(), 1.0f, 1.0f, 1.0f, 1.0f, 1.1f, 0.55f, 1.0f, 1.0f, 1.0f, 1.2f, 1.15f);
			T.Add(Make(TEXT("LSG"), TEXT("Lucknow Super Giants"), TEXT("SUPER GIANTS"), TEXT("Lucknow"), TEXT("Ekana Cricket Stadium"), 0x0A4FA8, 0xF58220, 0));
			Style(T.Last(), 1.15f, 1.25f, 1.0f, 1.0f, 1.0f, 0.35f, 1.0f, 1.1f, 1.0f, 1.15f, 1.0f);
			return T;
		}();
		return All;
	}

	int32 FranchiseIndex(const FString& Code)
	{
		return Franchises().IndexOfByPredicate([&](const FAuctionFranchise& F) { return F.Code == Code; });
	}

	TArray<FAuctionPlayer> ParseCsv(const FString& Csv)
	{
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines);
		TArray<FAuctionPlayer> Out;
		if (Lines.IsEmpty()) return Out;
		TArray<FString> Header;
		Lines[0].ParseIntoArray(Header, TEXT(","), false);
		for (int32 L = 1; L < Lines.Num(); ++L)
		{
			TArray<FString> Cells;
			Lines[L].ParseIntoArray(Cells, TEXT(","), false);
			auto Get = [&](const TCHAR* Column) -> FString
			{
				const int32 I = Header.IndexOfByKey(Column);
				return Cells.IsValidIndex(I) ? Cells[I].TrimStartAndEnd() : FString();
			};
			auto Int = [&](const TCHAR* Column) { return FCString::Atoi(*Get(Column)); };
			auto Real = [&](const TCHAR* Column) { return FCString::Atof(*Get(Column)); };
			FAuctionPlayer P;
			P.Name = Get(TEXT("Name"));
			if (P.Name.IsEmpty()) continue;
			P.Id = Out.Num();
			P.Short = Get(TEXT("Short"));
			if (P.Short.IsEmpty()) P.Short = P.Name;
			P.Country = Get(TEXT("Country"));
			P.Role = ParseRole(Get(TEXT("Role")));
			P.bLeftBat = Get(TEXT("BatHand")) == TEXT("L");
			P.BowlStyle = Get(TEXT("BowlStyle"));
			P.Age = Int(TEXT("Age"));
			P.bCapped = Get(TEXT("Capped")) != TEXT("0");
			P.Team2026 = Get(TEXT("Team2026"));
			P.Price2026 = Int(TEXT("Price2026Lakh"));
			P.Ipl = { Int(TEXT("IplMatches")), Int(TEXT("IplRuns")), Int(TEXT("IplWkts")), float(Real(TEXT("IplSR"))), float(Real(TEXT("IplEcon"))) };
			P.T20 = { Int(TEXT("T20Matches")), Int(TEXT("T20Runs")), Int(TEXT("T20Wkts")), float(Real(TEXT("T20SR"))), float(Real(TEXT("T20Econ"))) };
			P.Base = FMath::Max(AuctionRules::MinBase, Int(TEXT("BasePriceLakh")));
			P.BatRating = FMath::Clamp(Int(TEXT("BatRating")), 0, 99);
			P.BowlRating = FMath::Clamp(Int(TEXT("BowlRating")), 0, 99);
			P.bRetained2026 = Get(TEXT("Retained2026")) == TEXT("1");
			if (FranchiseIndex(P.Team2026) == INDEX_NONE) P.Team2026.Reset();
			Out.Add(MoveTemp(P));
		}
		return Out;
	}

	const TArray<FAuctionPlayer>& Players()
	{
		static const TArray<FAuctionPlayer> All = []()
		{
			FString Csv;
			for (const TCHAR* Row : RosterRows) { Csv += Row; Csv += TEXT("\n"); }
			return ParseCsv(Csv);
		}();
		return All;
	}
}
