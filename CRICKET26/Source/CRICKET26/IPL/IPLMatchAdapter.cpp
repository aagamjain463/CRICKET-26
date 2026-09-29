#include "IPLMatchAdapter.h"
#include "AuctionTypes.h"
#include "SuperOverMatch.h"
#include "IPLSeason.h"

namespace IPLMatchAdapter
{
	int32 ShirtNumber(int32 PlayerId)
	{
		return (PlayerId % 98) + 1;
	}

	FCricketPlayer FromAuctionPlayer(const FAuctionPlayer& P)
	{
		FCricketPlayer C;
		C.Name = P.Name;
		C.BatHand = P.bLeftBat ? ECricketHand::Left : ECricketHand::Right;
		C.BowlHand = P.BowlStyle.Contains(TEXT("Left-arm")) ? ECricketHand::Left : ECricketHand::Right;
		if (P.Role == EAuctionRole::Pace) C.BowlerType = EBowlerType::Pace;
		else if (P.Role == EAuctionRole::Spin)
			C.BowlerType = (P.BowlStyle.Contains(TEXT("leg")) || P.BowlStyle.Contains(TEXT("wrist"))) ? EBowlerType::LegSpin : EBowlerType::OffSpin;
		else
			C.BowlerType = (P.BowlStyle.Contains(TEXT("fast")) || P.BowlStyle.Contains(TEXT("medium"))) ? EBowlerType::Pace : EBowlerType::OffSpin;
		const float Bat = FMath::Clamp(float(P.BatRating) / 99.f, 0.f, 1.f);
		const float Bowl = FMath::Clamp(float(P.BowlRating) / 99.f, 0.f, 1.f);
		C.Timing = 0.35f + 0.60f * Bat;
		C.Technique = 0.35f + 0.60f * Bat;
		C.Power = P.Role == EAuctionRole::Batter || P.Role == EAuctionRole::AllRounder
			? 0.35f + 0.60f * Bat : 0.25f + 0.45f * Bat;
		C.Accuracy = 0.40f + 0.55f * Bowl;
		if (C.BowlerType == EBowlerType::Pace) C.PaceKph = 128.f + 18.f * Bowl;
		else if (P.Role == EAuctionRole::Pace || P.Role == EAuctionRole::Spin) C.PaceKph = 80.f + 16.f * Bowl;
		else C.PaceKph = 100.f + 20.f * Bowl;
		C.Movement = 0.30f + 0.60f * Bowl;
		C.Catching = 0.55f + 0.35f * Bat;
		C.Throwing = 0.50f + 0.35f * FMath::Max(Bat, Bowl);
		C.RunSpeed = 6.2f + 1.6f * FMath::Clamp(float(P.Age > 0 ? (38 - P.Age) : 8) / 18.f, 0.f, 1.f);
		C.Number = ShirtNumber(P.Id);
		return C;
	}

	bool ValidateXI(const FIPLSeason& Season, int32 Team, const FIPLPlayingXI& XI, FString* Why)
	{
		auto Fail = [&](const FString& Msg)
		{
			if (Why) *Why = Msg;
			return false;
		};
		if (!Season.Squads.IsValidIndex(Team)) return Fail(TEXT("unknown franchise"));
		if (XI.BattingOrder.Num() != IPLSeason::PlayingXI) return Fail(TEXT("the XI must be eleven players"));
		TSet<int32> Seen;
		for (int32 Id : XI.BattingOrder)
		{
			if (Id == INDEX_NONE) return Fail(TEXT("the XI holds an empty slot"));
			if (Seen.Contains(Id)) return Fail(TEXT("a player is picked twice"));
			Seen.Add(Id);
			if (!Season.Squads[Team].Players.ContainsByPredicate([Id](const FIPLSquadPlayer& S) { return S.PlayerId == Id; }))
				return Fail(TEXT("a picked player belongs to another franchise"));
		}
		return true;
	}

	TArray<FCricketTeam> BuildMatchTeams(const FIPLSeason& Season, const FIPLFixture& Fixture,
		const FIPLPlayingXI& HomeXI, const FIPLPlayingXI& AwayXI,
		const TArray<FAuctionPlayer>& Players, const TArray<FAuctionFranchise>& Franchises)
	{
		TArray<FCricketTeam> Teams;
		const int32 Sides[2] = { Fixture.Home, Fixture.Away };
		const FIPLPlayingXI* XIs[2] = { &HomeXI, &AwayXI };
		for (int32 S = 0; S < 2; ++S)
		{
			FCricketTeam Team;
			const int32 Fr = Sides[S];
			if (Franchises.IsValidIndex(Fr))
			{
				Team.Name = Franchises[Fr].Name;
				Team.Short = Franchises[Fr].Code;
				Team.Colour = Franchises[Fr].Primary;
				Team.Accent = Franchises[Fr].Secondary;
				Team.Sponsor = Franchises[Fr].Code;
			}
			int32 BestBowler = 0;
			for (int32 I = 0; I < XIs[S]->BattingOrder.Num(); ++I)
			{
				const int32 Id = XIs[S]->BattingOrder[I];
				Team.Batters.Add(Players.IsValidIndex(Id) ? FromAuctionPlayer(Players[Id]) : FCricketPlayer());
				if (Players.IsValidIndex(Id) && Players.IsValidIndex(XIs[S]->BattingOrder[BestBowler])
					&& Players[Id].BowlRating > Players[XIs[S]->BattingOrder[BestBowler]].BowlRating)
					BestBowler = I;
			}
			Team.Bowler = Team.Batters.IsValidIndex(BestBowler) ? Team.Batters[BestBowler] : FCricketPlayer();
			Teams.Add(Team);
		}
		return Teams;
	}

	FIPLResult ResultFromMatch(const FSuperOverMatch& Match, const FIPLFixture& Fixture)
	{
		FIPLResult R;
		R.FixtureId = Fixture.FixtureId;
		if (Match.Innings.Num() < 2) return R;
		const FInningsState& First = Match.Innings[0];
		const FInningsState& Second = Match.Innings[1];
		// Innings[0] is batted by BatFirst (0 = home). Winner is a match-team index; map to franchise.
		const int32 FirstSide = Fixture.BatFirst == 1 ? 1 : 0;
		const int32 HomeInnings = FirstSide == 0 ? 0 : 1;
		const FInningsState& Home = HomeInnings == 0 ? First : Second;
		const FInningsState& Away = HomeInnings == 0 ? Second : First;
		R.BatFirst = Fixture.BatFirst != INDEX_NONE ? Fixture.BatFirst : 0;
		R.HomeRuns = Home.Runs; R.HomeWickets = Home.Wickets; R.HomeBalls = Home.LegalBalls;
		R.AwayRuns = Away.Runs; R.AwayWickets = Away.Wickets; R.AwayBalls = Away.LegalBalls;
		const TArray<FAuctionFranchise>& Fr = AuctionData::Franchises();
		auto Code = [&](int32 T) -> FString { return Fr.IsValidIndex(T) ? Fr[T].Code : FString::Printf(TEXT("T%d"), T); };
		if (Match.bTied)
		{
			R.Winner = INDEX_NONE;
			R.bNoResult = true; // unreachable in IPL (ties go to a Super Over); kept for engine fidelity
			R.Margin = TEXT("Match tied");
			return R;
		}
		if (Match.Winner == INDEX_NONE) return R;
		R.Winner = Match.Winner == 0 ? (FirstSide == 0 ? Fixture.Home : Fixture.Away)
			: (FirstSide == 0 ? Fixture.Away : Fixture.Home);
		const bool bHomeWon = R.Winner == Fixture.Home;
		const FInningsState& WinIn = bHomeWon ? Home : Away;
		const FInningsState& LoseIn = bHomeWon ? Away : Home;
		const bool bWonBattingFirst = (bHomeWon && FirstSide == 0) || (!bHomeWon && FirstSide == 1);
		if (bWonBattingFirst)
		{
			const int32 Diff = WinIn.Runs - LoseIn.Runs;
			R.Margin = FString::Printf(TEXT("%s won by %d run%s"), *Code(R.Winner), Diff, Diff == 1 ? TEXT("") : TEXT("s"));
		}
		else
		{
			const int32 WktsLeft = 10 - WinIn.Wickets;
			const int32 BallsLeft = IPLSeason::MatchBalls - WinIn.LegalBalls;
			R.Margin = FString::Printf(TEXT("%s won by %d wicket%s (%d ball%s left)"), *Code(R.Winner), WktsLeft,
				WktsLeft == 1 ? TEXT("") : TEXT("s"), BallsLeft, BallsLeft == 1 ? TEXT("") : TEXT("s"));
		}
		return R;
	}

	TArray<int32> EligibleBatters(const FSuperOverMatch& Match)
	{
		TArray<int32> Out;
		if (!Match.Innings.IsValidIndex(Match.CurrentInnings)) return Out;
		const FInningsState& In = Match.Cur();
		for (int32 I = 0; I < In.Batters.Num(); ++I)
		{
			if (I == In.Striker || I == In.NonStriker) continue;
			if (In.Batters[I].HowOut != EDismissal::None) continue;
			if (In.Batters[I].Balls > 0 || In.Batters[I].Runs > 0) continue; // has batted: not eligible
			Out.Add(I);
		}
		return Out;
	}

	TArray<int32> EligibleBowlers(const FIPLPlayingXI& XI, const TArray<int32>& BallsBowled, int32 LastBowlerSlot,
		const TArray<FAuctionPlayer>& Players, int32 MaxBalls)
	{
		TArray<int32> Out;
		for (int32 I = 0; I < XI.BattingOrder.Num(); ++I)
		{
			if (I == LastBowlerSlot) continue; // consecutive overs are illegal
			const int32 Bowled = BallsBowled.IsValidIndex(I) ? BallsBowled[I] : 0;
			if (Bowled >= MaxBalls) continue;
			Out.Add(I);
		}
		// Credible order: genuine bowlers first, so the list reads like a captain's options.
		Out.Sort([&](int32 A, int32 B)
		{
			const int32 IdA = XI.BattingOrder.IsValidIndex(A) ? XI.BattingOrder[A] : INDEX_NONE;
			const int32 IdB = XI.BattingOrder.IsValidIndex(B) ? XI.BattingOrder[B] : INDEX_NONE;
			const int32 RA = Players.IsValidIndex(IdA) ? Players[IdA].BowlRating : 0;
			const int32 RB = Players.IsValidIndex(IdB) ? Players[IdB].BowlRating : 0;
			return RA > RB;
		});
		return Out;
	}

	int32 ChooseAIBowler(const FIPLPlayingXI& XI, const TArray<int32>& BallsBowled, int32 LastBowlerSlot,
		const TArray<FAuctionPlayer>& Players, int32 MaxBalls)
	{
		const TArray<int32> Options = EligibleBowlers(XI, BallsBowled, LastBowlerSlot, Players, MaxBalls);
		if (Options.Num() == 0) return INDEX_NONE;
		// Best rating with a pace/spin alternation where possible: reuse ratings, never pure random.
		auto IsPace = [&](int32 Slot)
		{
			const int32 Id = XI.BattingOrder.IsValidIndex(Slot) ? XI.BattingOrder[Slot] : INDEX_NONE;
			if (!Players.IsValidIndex(Id)) return false;
			return Players[Id].Role == EAuctionRole::Pace
				|| Players[Id].BowlStyle.Contains(TEXT("fast")) || Players[Id].BowlStyle.Contains(TEXT("medium"));
		};
		const bool bLastPace = LastBowlerSlot != INDEX_NONE ? IsPace(LastBowlerSlot) : false;
		for (int32 Slot : Options)
			if (IsPace(Slot) != bLastPace) return Slot;
		return Options[0];
	}

	TArray<int32> BowlerBallsFromMatch(const FSuperOverMatch& Match)
	{
		TArray<int32> Out;
		if (!Match.Innings.IsValidIndex(Match.CurrentInnings)) return Out;
		const FInningsState& In = Match.Cur();
		Out.SetNumZeroed(FMath::Max(In.BowlerCards.Num(), 0));
		for (int32 I = 0; I < In.BowlerCards.Num(); ++I) Out[I] = In.BowlerCards[I].Balls;
		if (In.BowlerSlot != INDEX_NONE)
		{
			if (!Out.IsValidIndex(In.BowlerSlot)) Out.SetNumZeroed(In.BowlerSlot + 1);
			Out[In.BowlerSlot] = In.Bowler.Balls;
		}
		return Out;
	}
}
