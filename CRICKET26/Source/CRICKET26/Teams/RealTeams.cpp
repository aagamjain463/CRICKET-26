#include "RealTeams.h"
#include "IPLMatchAdapter.h"
#include "IPLSeason.h"

namespace
{
	// The national squads, one CSV line per entry, generated from Scripts/teams/International.csv by
	// Scripts/teams/international.py.
	#include "InternationalRoster.inl"

	FLinearColor FromHex(uint32 Rgb)
	{
		return FLinearColor::FromSRGBColor(FColor((Rgb >> 16) & 255, (Rgb >> 8) & 255, Rgb & 255));
	}

	FRealTeam Nation(const TCHAR* Code, const TCHAR* Name, uint32 Primary, uint32 Secondary)
	{
		FRealTeam T;
		T.Code = Code;
		T.Name = Name;
		T.Primary = FromHex(Primary);
		T.Secondary = FromHex(Secondary);
		return T;
	}

	// The twelve ICC full members; the kit colours are approximate brand colours, not official codes.
	TArray<FRealTeam> Nations()
	{
		return {
			Nation(TEXT("IND"), TEXT("India"), 0x1C5BC7, 0xFF9933),
			Nation(TEXT("AUS"), TEXT("Australia"), 0xFFCD00, 0x00843D),
			Nation(TEXT("ENG"), TEXT("England"), 0x1B3F8F, 0xD2232A),
			Nation(TEXT("SA"), TEXT("South Africa"), 0x007A4D, 0xFFB612),
			Nation(TEXT("NZ"), TEXT("New Zealand"), 0x1A1A1A, 0x8FD8C8),
			Nation(TEXT("PAK"), TEXT("Pakistan"), 0x0B6E3A, 0xC9E265),
			Nation(TEXT("SL"), TEXT("Sri Lanka"), 0x0C2D83, 0xFFC20E),
			Nation(TEXT("WI"), TEXT("West Indies"), 0x7B1E3C, 0xF2B632),
			Nation(TEXT("BAN"), TEXT("Bangladesh"), 0x006A4E, 0xE0344B),
			Nation(TEXT("AFG"), TEXT("Afghanistan"), 0x1C4E9D, 0xD32011),
			Nation(TEXT("IRE"), TEXT("Ireland"), 0x169B62, 0x1D2F6F),
			Nation(TEXT("ZIM"), TEXT("Zimbabwe"), 0xD40000, 0xFFD200),
		};
	}

	// The IPL captains named at the start of the 2026 season (Scripts/auction/Teams.csv), by franchise code.
	const TCHAR* IplCaptain(const FString& Code)
	{
		static const TCHAR* const Captains[][2] = {
			{ TEXT("CSK"), TEXT("Ruturaj Gaikwad") }, { TEXT("MI"), TEXT("Hardik Pandya") }, { TEXT("RCB"), TEXT("Rajat Patidar") },
			{ TEXT("KKR"), TEXT("Ajinkya Rahane") }, { TEXT("SRH"), TEXT("Pat Cummins") }, { TEXT("DC"), TEXT("Axar Patel") },
			{ TEXT("PBKS"), TEXT("Shreyas Iyer") }, { TEXT("RR"), TEXT("Riyan Parag") }, { TEXT("GT"), TEXT("Shubman Gill") },
			{ TEXT("LSG"), TEXT("Rishabh Pant") } };
		for (const auto& Pair : Captains)
			if (Code == Pair[0]) return Pair[1];
		return TEXT("");
	}

	TArray<FIPLSquadPlayer> AsSquad(const TArray<int32>& Ids)
	{
		TArray<FIPLSquadPlayer> Out;
		for (int32 Id : Ids)
		{
			FIPLSquadPlayer S;
			S.PlayerId = Id;
			Out.Add(S);
		}
		return Out;
	}

	const RealTeams::FParsedSquads& National()
	{
		static const RealTeams::FParsedSquads Parsed = []()
		{
			FString Csv;
			for (const TCHAR* Row : InternationalRows) { Csv += Row; Csv += TEXT("\n"); }
			return RealTeams::ParseSquads(Csv);
		}();
		return Parsed;
	}
}

namespace RealTeams
{
	const TCHAR* CompetitionName(ECompetition C)
	{
		return C == ECompetition::IPL ? TEXT("IPL") : TEXT("INTERNATIONAL");
	}

	FParsedSquads ParseSquads(const FString& Csv)
	{
		FParsedSquads Out;
		// The player columns are the IPL roster's, so its parser reads them; the squad columns are read here, skipping
		// exactly the rows it skips (no name) so both stay in step.
		Out.Players = AuctionData::ParseCsv(Csv);
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines);
		if (Lines.IsEmpty()) return Out;
		TArray<FString> Header;
		Lines[0].ParseIntoArray(Header, TEXT(","), false);
		const int32 NameCol = Header.IndexOfByKey(TEXT("Name")), TeamCol = Header.IndexOfByKey(TEXT("Team"));
		const int32 XICol = Header.IndexOfByKey(TEXT("XI")), CaptainCol = Header.IndexOfByKey(TEXT("Captain"));
		for (int32 L = 1; L < Lines.Num(); ++L)
		{
			TArray<FString> Cells;
			Lines[L].ParseIntoArray(Cells, TEXT(","), false);
			auto Get = [&Cells](int32 Col) { return Cells.IsValidIndex(Col) ? Cells[Col].TrimStartAndEnd() : FString(); };
			if (Get(NameCol).IsEmpty()) continue;
			Out.Team.Add(Get(TeamCol));
			Out.XISlot.Add(FCString::Atoi(*Get(XICol)));
			Out.bCaptain.Add(Get(CaptainCol) == TEXT("1"));
		}
		check(Out.Team.Num() == Out.Players.Num());
		return Out;
	}

	TArray<FRealTeam> BuildTeams(const FParsedSquads& Parsed, const TArray<FRealTeam>& Codes)
	{
		TArray<FRealTeam> Out = Codes;
		for (FRealTeam& T : Out)
		{
			TArray<TPair<int32, int32>> Slots; // XI slot, player id
			for (int32 Id = 0; Id < Parsed.Players.Num(); ++Id)
			{
				if (Parsed.Team[Id] != T.Code) continue;
				T.Squad.Add(Id);
				if (Parsed.XISlot[Id] > 0) Slots.Add({ Parsed.XISlot[Id], Id });
				if (Parsed.bCaptain[Id]) T.Captain = Id;
			}
			Slots.Sort([](const TPair<int32, int32>& A, const TPair<int32, int32>& B) { return A.Key < B.Key; });
			for (const TPair<int32, int32>& S : Slots) T.RealXI.BattingOrder.Add(S.Value);
		}
		return Out;
	}

	const TArray<FAuctionPlayer>& Players(ECompetition C)
	{
		return C == ECompetition::IPL ? AuctionData::Players() : National().Players;
	}

	const TArray<FRealTeam>& Teams(ECompetition C)
	{
		static const TArray<FRealTeam> International = BuildTeams(National(), Nations());
		static const TArray<FRealTeam> Ipl = []()
		{
			// The original 2026 squads (Team2026), never the auction's: the XI is the best the game picks from them.
			const TArray<FAuctionPlayer>& All = AuctionData::Players();
			TArray<FRealTeam> Out;
			for (const FAuctionFranchise& F : AuctionData::Franchises())
			{
				FRealTeam T;
				T.Code = F.Code;
				T.Name = F.Name;
				T.Primary = F.Primary;
				T.Secondary = F.Secondary;
				const FString Captain = IplCaptain(F.Code);
				for (const FAuctionPlayer& P : All)
				{
					if (P.Team2026 != F.Code) continue;
					T.Squad.Add(P.Id);
					if (P.Name == Captain) T.Captain = P.Id;
				}
				T.RealXI = IPLSeason::MakeDefaultXI(AsSquad(T.Squad), All);
				Out.Add(MoveTemp(T));
			}
			return Out;
		}();
		return C == ECompetition::IPL ? Ipl : International;
	}

	bool ValidXI(const FRealTeam& Team, const FIPLPlayingXI& XI, FString* Why)
	{
		auto Fail = [Why](const TCHAR* Msg)
		{
			if (Why) *Why = Msg;
			return false;
		};
		if (XI.BattingOrder.Num() != IPLSeason::PlayingXI) return Fail(TEXT("the XI must be eleven players"));
		TSet<int32> Seen;
		for (int32 Id : XI.BattingOrder)
		{
			if (Seen.Contains(Id)) return Fail(TEXT("a player is picked twice"));
			Seen.Add(Id);
			if (!Team.Squad.Contains(Id)) return Fail(TEXT("a picked player is not in the squad"));
		}
		return true;
	}

	FIPLPlayingXI DefaultXI(ECompetition C, int32 TeamIndex)
	{
		const TArray<FRealTeam>& All = Teams(C);
		if (!All.IsValidIndex(TeamIndex)) return FIPLPlayingXI();
		const FRealTeam& Team = All[TeamIndex];
		if (ValidXI(Team, Team.RealXI)) return Team.RealXI;
		return IPLSeason::MakeDefaultXI(AsSquad(Team.Squad), Players(C));
	}

	int32 MaxBallsPerBowler(int32 Overs)
	{
		return FMath::Max(1, (Overs + 4) / 5) * 6;
	}

	FCricketTeam MatchSide(const FRealTeam& Team, const FIPLPlayingXI& XI, const TArray<FAuctionPlayer>& All)
	{
		FCricketTeam Side;
		Side.Name = Team.Name;
		Side.Short = Team.Code;
		Side.Colour = Team.Primary;
		Side.Accent = Team.Secondary;
		Side.Sponsor = Team.Code;
		int32 Best = INDEX_NONE, BestRating = -1;
		for (int32 I = 0; I < XI.BattingOrder.Num(); ++I)
		{
			const int32 Id = XI.BattingOrder[I];
			if (!All.IsValidIndex(Id))
			{
				Side.Batters.Add(FCricketPlayer());
				continue;
			}
			Side.Batters.Add(IPLMatchAdapter::FromAuctionPlayer(All[Id]));
			if (All[Id].BowlRating > BestRating)
			{
				BestRating = All[Id].BowlRating;
				Best = I;
			}
		}
		Side.Bowler = Side.Batters.IsValidIndex(Best) ? Side.Batters[Best] : FCricketPlayer();
		return Side;
	}

	bool FlipCoin(FRandomStream& Rng)
	{
		return Rng.FRand() < 0.5f;
	}

	bool AIElectsToBat(FRandomStream& Rng, float BatStrength, float BowlStrength)
	{
		const bool bBat = BatStrength > BowlStrength + 3.f;
		return Rng.FRand() < 0.2f ? !bBat : bBat;
	}

	void XIStrength(const FIPLPlayingXI& XI, const TArray<FAuctionPlayer>& All, float& OutBat, float& OutBowl)
	{
		TArray<int32> Bat, Bowl;
		for (int32 Id : XI.BattingOrder)
		{
			if (!All.IsValidIndex(Id)) continue;
			Bat.Add(All[Id].BatRating);
			Bowl.Add(All[Id].BowlRating);
		}
		Bat.Sort(TGreater<int32>());
		Bowl.Sort(TGreater<int32>());
		auto Mean = [](const TArray<int32>& V, int32 N)
		{
			float Sum = 0.f;
			const int32 Count = FMath::Min(N, V.Num());
			for (int32 I = 0; I < Count; ++I) Sum += V[I];
			return Count > 0 ? Sum / Count : 0.f;
		};
		OutBat = Mean(Bat, 7);
		OutBowl = Mean(Bowl, 5);
	}
}
