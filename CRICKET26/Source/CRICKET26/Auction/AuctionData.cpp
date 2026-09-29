#include "AuctionTypes.h"

namespace
{
	// The roster and the franchises, one CSV line per entry, generated from Scripts/auction/Players.csv and Teams.csv by
	// Scripts/auction/roster.py.
	#include "AuctionRoster.inl"
	#include "AuctionTeams.inl"

	/** How each front office bids, keyed by franchise code: aggression, star, youth, overseas, loyalty, patience, tempo,
	 *  enforcer; then role bias BA, WK, AL, FA, SP. Read from how each side has behaved at real auctions: CSK loyal and
	 *  patient with experience and spin, MI and RR scouting youth, KKR all-rounders and mystery spin, PBKS and LSG the
	 *  big early spenders, SRH batting firepower, GT pace and spin. */
	struct FStyleRow { const TCHAR* Code; float Aggression, Star, Youth, Overseas, Loyalty, Patience, Tempo, Enforcer, Ba, Wk, Al, Fa, Sp; };
	const FStyleRow Styles[] = {
		{ TEXT("CSK"),  0.95f, 1.0f,  0.85f, 1.0f,  1.35f, 0.7f,  0.85f, 0.15f, 1.0f,  1.0f,  1.15f, 0.95f, 1.25f },
		{ TEXT("MI"),   1.05f, 1.15f, 1.15f, 1.0f,  1.2f,  0.6f,  1.0f,  0.35f, 1.0f,  1.0f,  1.05f, 1.2f,  0.95f },
		{ TEXT("RCB"),  1.1f,  1.2f,  1.0f,  1.05f, 1.1f,  0.35f, 1.1f,  0.3f,  1.15f, 1.0f,  1.0f,  1.1f,  0.95f },
		{ TEXT("KKR"),  1.05f, 1.0f,  1.05f, 1.1f,  1.25f, 0.55f, 1.0f,  0.4f,  0.95f, 1.0f,  1.25f, 0.95f, 1.15f },
		{ TEXT("SRH"),  1.1f,  1.1f,  1.0f,  1.1f,  1.1f,  0.4f,  1.1f,  0.3f,  1.2f,  1.0f,  1.0f,  1.15f, 0.9f },
		{ TEXT("DC"),   1.0f,  1.05f, 1.1f,  1.0f,  1.05f, 0.5f,  1.0f,  0.45f, 1.0f,  1.15f, 1.0f,  1.05f, 1.0f },
		{ TEXT("PBKS"), 1.15f, 1.15f, 1.0f,  1.0f,  1.0f,  0.3f,  1.25f, 0.5f,  1.0f,  1.0f,  1.05f, 1.05f, 1.0f },
		{ TEXT("RR"),   0.95f, 0.95f, 1.25f, 1.0f,  1.15f, 0.6f,  0.9f,  0.25f, 1.05f, 1.0f,  1.0f,  1.0f,  1.1f },
		{ TEXT("GT"),   1.0f,  1.0f,  1.0f,  1.0f,  1.1f,  0.55f, 0.95f, 0.3f,  1.0f,  1.0f,  1.0f,  1.2f,  1.15f },
		{ TEXT("LSG"),  1.15f, 1.25f, 1.0f,  1.0f,  1.0f,  0.35f, 1.2f,  0.45f, 1.0f,  1.1f,  1.0f,  1.15f, 1.0f },
	};

	FLinearColor ParseHex(const FString& Hex)
	{
		const uint32 V = uint32(FCString::Strtoi(*Hex.Replace(TEXT("#"), TEXT("")), nullptr, 16));
		return FLinearColor::FromSRGBColor(FColor((V >> 16) & 255, (V >> 8) & 255, V & 255));
	}

	/** A CSV parsed by its header row: Cells(Row) with Get(Column). */
	struct FCsv
	{
		TArray<FString> Header;
		TArray<TArray<FString>> Rows;
		explicit FCsv(const FString& Csv)
		{
			TArray<FString> Lines;
			Csv.ParseIntoArrayLines(Lines);
			for (int32 L = 0; L < Lines.Num(); ++L)
			{
				TArray<FString> Cells;
				Lines[L].ParseIntoArray(Cells, TEXT(","), false);
				if (L == 0) Header = MoveTemp(Cells);
				else Rows.Add(MoveTemp(Cells));
			}
		}
		FString Get(const TArray<FString>& Cells, const TCHAR* Column) const
		{
			const int32 I = Header.IndexOfByKey(FString(Column));
			return Cells.IsValidIndex(I) ? Cells[I].TrimStartAndEnd() : FString();
		}
	};

	EAuctionRole ParseRole(const FString& S)
	{
		if (S == TEXT("WK")) return EAuctionRole::Keeper;
		if (S == TEXT("AR")) return EAuctionRole::AllRounder;
		if (S == TEXT("PACE")) return EAuctionRole::Pace;
		if (S == TEXT("SPIN")) return EAuctionRole::Spin;
		return EAuctionRole::Batter;
	}
}

namespace AuctionTags
{
	const TCHAR* Name(int32 I)
	{
		static const TCHAR* Names[] = { TEXT("Opener"), TEXT("Anchor"), TEXT("Finisher"), TEXT("KeeperBat"), TEXT("PowerplayPace"),
			TEXT("DeathPace"), TEXT("LeftArmPace"), TEXT("WristSpin"), TEXT("FingerSpin"), TEXT("Mystery"), TEXT("Captain") };
		static_assert(UE_ARRAY_COUNT(Names) == Count, "a name per tag");
		return I >= 0 && I < Count ? Names[I] : TEXT("");
	}

	uint32 Parse(const FString& Pipe)
	{
		TArray<FString> Parts;
		Pipe.ParseIntoArray(Parts, TEXT("|"), true);
		uint32 Bits = 0;
		for (const FString& Part : Parts)
			for (int32 I = 0; I < Count; ++I)
				if (Part.TrimStartAndEnd() == Name(I)) Bits |= 1u << I;
		return Bits;
	}
}

bool FAuctionPlayer::BowlsPace() const
{
	if (Role == EAuctionRole::Pace) return true;
	if (Role == EAuctionRole::Spin) return false;
	return BowlStyle.Contains(TEXT("fast")) || BowlStyle.Contains(TEXT("medium"));
}

bool FAuctionPlayer::BowlsSpin() const
{
	if (Role == EAuctionRole::Spin) return true;
	if (Role == EAuctionRole::Pace || BowlStyle.IsEmpty()) return false;
	return !BowlsPace();
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

	int32 OnLadder(int32 Amount, int32 From)
	{
		int32 L = From;
		while (L < Amount) L = NextBid(L);
		return L;
	}

	bool IsMegaSeason(int32 Season)
	{
		return Season >= FirstMega && (Season - FirstMega) % MegaCycle == 0;
	}

	int32 Fee(EAuctionMode Mode, bool bOverseas, int32 Price)
	{
		return Mode == EAuctionMode::Mini && bOverseas ? FMath::Min(Price, OverseasFeeCap) : Price;
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
	TArray<FAuctionFranchise> ParseTeams(const FString& Csv)
	{
		const FCsv Table(Csv);
		TArray<FAuctionFranchise> Out;
		for (const TArray<FString>& Cells : Table.Rows)
		{
			FAuctionFranchise F;
			F.Code = Table.Get(Cells, TEXT("Code"));
			if (F.Code.IsEmpty()) continue;
			F.Name = Table.Get(Cells, TEXT("Name"));
			F.Short = Table.Get(Cells, TEXT("Short"));
			if (F.Short.IsEmpty()) F.Short = F.Name.ToUpper();
			F.City = Table.Get(Cells, TEXT("City"));
			F.Home = Table.Get(Cells, TEXT("Home"));
			F.Owner = Table.Get(Cells, TEXT("Owner"));
			F.Captain = Table.Get(Cells, TEXT("Captain2026"));
			F.Coach = Table.Get(Cells, TEXT("HeadCoach2026"));
			F.Primary = ParseHex(Table.Get(Cells, TEXT("PrimaryHex")));
			F.Secondary = ParseHex(Table.Get(Cells, TEXT("SecondaryHex")));
			F.Titles = FCString::Atoi(*Table.Get(Cells, TEXT("Titles")));
			for (const FStyleRow& S : Styles)
			{
				if (F.Code != S.Code) continue;
				F.Aggression = S.Aggression; F.StarBias = S.Star; F.YouthBias = S.Youth; F.OverseasBias = S.Overseas;
				F.Loyalty = S.Loyalty; F.Patience = S.Patience; F.Tempo = S.Tempo; F.Enforcer = S.Enforcer;
				const float Bias[] = { S.Ba, S.Wk, S.Al, S.Fa, S.Sp };
				for (int32 I = 0; I < int32(EAuctionRole::Count); ++I) F.RoleBias[I] = Bias[I];
			}
			Out.Add(MoveTemp(F));
		}
		return Out;
	}

	const TArray<FAuctionFranchise>& Franchises()
	{
		static const TArray<FAuctionFranchise> All = []()
		{
			FString Csv;
			for (const TCHAR* Row : TeamRows) { Csv += Row; Csv += TEXT("\n"); }
			return ParseTeams(Csv);
		}();
		return All;
	}

	int32 FranchiseIndex(const FString& Code)
	{
		return Franchises().IndexOfByPredicate([&](const FAuctionFranchise& F) { return F.Code == Code; });
	}

	TArray<FAuctionPlayer> ParseCsv(const FString& Csv)
	{
		const FCsv Table(Csv);
		TArray<FAuctionPlayer> Out;
		for (const TArray<FString>& Cells : Table.Rows)
		{
			auto Get = [&](const TCHAR* Column) { return Table.Get(Cells, Column); };
			auto Int = [&](const TCHAR* Column) { return FCString::Atoi(*Get(Column)); };
			auto Real = [&](const TCHAR* Column) { return float(FCString::Atof(*Get(Column))); };
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
			P.Ipl = { Int(TEXT("IplMatches")), Int(TEXT("IplRuns")), Int(TEXT("IplWkts")), Real(TEXT("IplSR")), Real(TEXT("IplEcon")) };
			P.T20 = { Int(TEXT("T20Matches")), Int(TEXT("T20Runs")), Int(TEXT("T20Wkts")), Real(TEXT("T20SR")), Real(TEXT("T20Econ")) };
			P.Last = { Int(TEXT("LastMatches")), Int(TEXT("LastRuns")), Int(TEXT("LastWkts")), Real(TEXT("LastSR")), Real(TEXT("LastEcon")) };
			P.Base = FMath::Max(AuctionRules::MinBase, Int(TEXT("BasePriceLakh")));
			P.BatRating = FMath::Clamp(Int(TEXT("BatRating")), 0, 99);
			P.BowlRating = FMath::Clamp(Int(TEXT("BowlRating")), 0, 99);
			P.Tags = AuctionTags::Parse(Get(TEXT("Tags")));
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
