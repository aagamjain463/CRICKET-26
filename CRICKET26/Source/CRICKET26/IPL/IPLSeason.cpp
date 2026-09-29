// IPL season orchestration: auction handoff, schedule, simulation, table, playoffs, validation.

#include "IPLSeason.h"
#include "AuctionEngine.h"
#include "AuctionTypes.h"

namespace IPLSeason
{
	namespace
	{
		const FAuctionPlayer& PlayerOf(const TArray<FAuctionPlayer>& Players, int32 Id)
		{
			check(Players.IsValidIndex(Id));
			return Players[Id];
		}

		bool XIIsUsable(const FIPLPlayingXI& XI, const FIPLSquad& Squad)
		{
			if (XI.BattingOrder.Num() != PlayingXI) return false;
			TSet<int32> Seen;
			for (int32 Id : XI.BattingOrder)
			{
				if (Id == INDEX_NONE || Seen.Contains(Id)) return false;
				Seen.Add(Id);
				if (!Squad.Players.ContainsByPredicate([Id](const FIPLSquadPlayer& S) { return S.PlayerId == Id; }))
					return false;
			}
			return true;
		}

		float AvgBat(const TArray<int32>& XI, const TArray<FAuctionPlayer>& Players)
		{
			if (XI.Num() == 0) return 50.f;
			const int32 N = FMath::Min(7, XI.Num());
			double Sum = 0.0;
			for (int32 I = 0; I < N; ++I) Sum += PlayerOf(Players, XI[I]).BatRating;
			return float(Sum / N);
		}

		float AvgBowl(const TArray<int32>& XI, const TArray<FAuctionPlayer>& Players)
		{
			TArray<int32> Bowls;
			for (int32 Id : XI) Bowls.Add(PlayerOf(Players, Id).BowlRating);
			Bowls.Sort(TGreater<int32>());
			const int32 N = FMath::Min(5, Bowls.Num());
			if (N == 0) return 50.f;
			double Sum = 0.0;
			for (int32 I = 0; I < N; ++I) Sum += Bowls[I];
			return float(Sum / N);
		}

		struct FInningsSim
		{
			int32 Runs = 0;
			int32 Wickets = 0;
			int32 Balls = 0;
		};

		// One innings, ball by ball. Rates come from the two XIs: never a coin flip.
		FInningsSim SimInnings(const TArray<int32>& BatXI, const TArray<int32>& BowlXI,
			const TArray<FAuctionPlayer>& Players, FRandomStream& Rng, int32 MaxBalls, int32 Target)
		{
			FInningsSim In;
			const float Edge = FMath::Clamp((AvgBat(BatXI, Players) - AvgBowl(BowlXI, Players)) / 99.f, -1.f, 1.f);
			while (In.Balls < MaxBalls && In.Wickets < 10 && !(Target > 0 && In.Runs >= Target))
			{
				float PBoundary4 = 0.14f + 0.05f * Edge;
				float PBoundary6 = 0.075f + 0.035f * Edge;
				float PWicket = FMath::Clamp(0.030f - 0.012f * Edge, 0.008f, 0.07f);
				if (Target > 0)
				{
					const int32 Need = Target - In.Runs;
					const int32 Left = MaxBalls - In.Balls;
					if (Left > 0 && Need > 0)
					{
						const float Aggro = FMath::Clamp((float(Need) / float(Left) - 1.2f) / 2.5f, 0.f, 1.f);
						PBoundary4 *= (1.f + 0.8f * Aggro);
						PBoundary6 *= (1.f + 1.0f * Aggro);
						PWicket *= (1.f + 1.2f * Aggro);
					}
				}
				const float POne = 0.34f, PTwo = 0.10f, PThree = 0.005f;
				const float Total = POne + PTwo + PThree + PBoundary4 + PBoundary6 + PWicket;
				const float PDot = FMath::Max(0.f, 1.f - Total);
				const float Roll = Rng.FRand();
				float Acc = PDot;
				int32 Scored = 0;
				bool bOut = false;
				if (Roll < Acc) { Scored = 0; }
				else if ((Acc += POne) > Roll) { Scored = 1; }
				else if ((Acc += PTwo) > Roll) { Scored = 2; }
				else if ((Acc += PThree) > Roll) { Scored = 3; }
				else if ((Acc += PBoundary4) > Roll) { Scored = 4; }
				else if ((Acc += PBoundary6) > Roll) { Scored = 6; }
				else { bOut = true; }
				// A wicket costs the crossed run: standard scoring, runs before the dismissal stand
				// except the batter is out, so keep it simple and deterministic-friendly: dot + wicket.
				if (bOut) { In.Wickets++; }
				else { In.Runs += Scored; }
				In.Balls++;
			}
			return In;
		}

		void AddLeagueFixture(FIPLSeason& Season, int32 Home, int32 Away, const TArray<FString>& Grounds)
		{
			FIPLFixture F;
			F.FixtureId = Season.Fixtures.Num();
			F.MatchNumber = Season.NextMatchNumber++;
			F.Stage = EIPLStage::League;
			F.Home = Home;
			F.Away = Away;
			F.Venue = Grounds.IsValidIndex(Home) ? Grounds[Home] : FString(TEXT("IPL"));
			Season.Fixtures.Add(F);
		}

		const FIPLFixture* FixtureOfStage(const FIPLSeason& Season, EIPLStage Stage)
		{
			for (const FIPLFixture& F : Season.Fixtures)
				if (F.Stage == Stage) return &F;
			return nullptr;
		}

		int32 WinnerOf(const FIPLSeason& Season, EIPLStage Stage)
		{
			const FIPLFixture* F = FixtureOfStage(Season, Stage);
			return (F && F->Status == EIPLFixtureStatus::Completed && !F->Result.bNoResult) ? F->Result.Winner : INDEX_NONE;
		}

		int32 LoserOf(const FIPLSeason& Season, EIPLStage Stage)
		{
			const FIPLFixture* F = FixtureOfStage(Season, Stage);
			if (!F || F->Status != EIPLFixtureStatus::Completed || F->Result.bNoResult) return INDEX_NONE;
			return F->Result.Winner == F->Home ? F->Away : F->Home;
		}

		void AddPlayoffFixture(FIPLSeason& Season, EIPLStage Stage, int32 Home, int32 Away, const TArray<FString>& Grounds)
		{
			FIPLFixture F;
			F.FixtureId = Season.Fixtures.Num();
			F.MatchNumber = Season.NextMatchNumber++;
			F.Stage = Stage;
			F.Home = Home;
			F.Away = Away;
			F.Venue = Grounds.IsValidIndex(Home) ? Grounds[Home] : FString(TEXT("IPL"));
			Season.Fixtures.Add(F);
		}

		TArray<FString> FranchiseGrounds()
		{
			TArray<FString> Grounds;
			for (const FAuctionFranchise& F : AuctionData::Franchises()) Grounds.Add(F.Home);
			return Grounds;
		}
	}

	FIPLPlayingXI MakeDefaultXI(const TArray<FIPLSquadPlayer>& Squad, const TArray<FAuctionPlayer>& Players);

	FIPLSeason BuildFromAuction(const FAuction& Auction)
	{
		FIPLSeason Season;
		Season.SeasonId = FString::Printf(TEXT("IPL-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short));
		Season.UserTeam = Auction.Human != INDEX_NONE ? Auction.Human : 0;
		Season.Year = Auction.Config.Season;
		// Verbatim handoff: every signing the auction produced, same player, same price, same route.
		Season.Squads.SetNum(NumTeams);
		for (int32 T = 0; T < NumTeams && T < Auction.Teams.Num(); ++T)
		{
			for (const FAuctionSigning& S : Auction.Teams[T].Squad)
			{
				FIPLSquadPlayer P;
				P.PlayerId = S.Player;
				P.Price = S.Price;
				P.bRetained = S.bRetained;
				P.bRtm = S.bRtm;
				Season.Squads[T].Players.Add(P);
			}
		}
		GenerateLeagueFixtures(Season, FranchiseGrounds());
		Season.Table.SetNum(NumTeams);
		for (int32 T = 0; T < NumTeams; ++T) Season.Table[T].Team = T;
		const TArray<FAuctionPlayer>& Players = AuctionData::Players();
		Season.LastXI.SetNum(NumTeams);
		for (int32 T = 0; T < NumTeams; ++T) Season.LastXI[T] = MakeDefaultXI(Season.Squads[T].Players, Players);
		return Season;
	}

	FIPLPlayingXI MakeDefaultXI(const TArray<FIPLSquadPlayer>& Squad, const TArray<FAuctionPlayer>& Players)
	{
		FIPLPlayingXI XI;
		TArray<int32> Ids;
		for (const FIPLSquadPlayer& S : Squad) Ids.Add(S.PlayerId);
		Ids.Sort([&Players](int32 A, int32 B) { return PlayerOf(Players, A).Overall() > PlayerOf(Players, B).Overall(); });
		TArray<int32> Picked;
		for (int32 I = 0; I < FMath::Min(PlayingXI, Ids.Num()); ++I) Picked.Add(Ids[I]);
		auto HasKeeper = [&](const TArray<int32>& L)
		{
			for (int32 Id : L) if (PlayerOf(Players, Id).Role == EAuctionRole::Keeper) return true;
			return false;
		};
		auto BowlCount = [&](const TArray<int32>& L)
		{
			int32 N = 0;
			for (int32 Id : L) if (PlayerOf(Players, Id).BowlRating >= 40) N++;
			return N;
		};
		const bool bNeedKeeper = [&]
		{
			for (int32 Id : Ids) if (PlayerOf(Players, Id).Role == EAuctionRole::Keeper) return true;
			return false;
		}();
		// Repair: guarantee a keeper and five bowling options where the squad allows it.
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			bool bFixed = false;
			if (bNeedKeeper && !HasKeeper(Picked))
			{
				for (int32 Id : Ids)
					if (!Picked.Contains(Id) && PlayerOf(Players, Id).Role == EAuctionRole::Keeper)
					{
						// Swap out the worst picked non-keeper.
						for (int32 K = Picked.Num() - 1; K >= 0; --K)
							if (PlayerOf(Players, Picked[K]).Role != EAuctionRole::Keeper) { Picked[K] = Id; bFixed = true; break; }
						break;
					}
			}
			if (BowlCount(Picked) < 5)
			{
				for (int32 Id : Ids)
					if (!Picked.Contains(Id) && PlayerOf(Players, Id).BowlRating >= 40)
					{
						int32 Worst = 0;
						for (int32 K = 1; K < Picked.Num(); ++K)
							if (PlayerOf(Players, Picked[K]).BowlRating < PlayerOf(Players, Picked[Worst]).BowlRating) Worst = K;
						if (PlayerOf(Players, Id).BowlRating > PlayerOf(Players, Picked[Worst]).BowlRating)
						{
							Picked[Worst] = Id;
							bFixed = true;
						}
						break;
					}
			}
			if (!bFixed) break;
		}
		// Batting order: best batters first, so the openers are the openers.
		Picked.Sort([&Players](int32 A, int32 B) { return PlayerOf(Players, A).BatRating > PlayerOf(Players, B).BatRating; });
		XI.BattingOrder = Picked;
		return XI;
	}

	void GenerateLeagueFixtures(FIPLSeason& Season, const TArray<FString>& HomeGrounds)
	{
		// Fixed groups (data-driven shape: two groups of five, rivals paired by group position).
		const int32 GroupA[] = { 0, 2, 4, 6, 8 };
		const int32 GroupB[] = { 1, 3, 5, 7, 9 };

		// 1. Inside the group: home and away (8 per team).
		for (int32 I = 0; I < 5; ++I)
			for (int32 J = I + 1; J < 5; ++J)
			{
				AddLeagueFixture(Season, GroupA[I], GroupA[J], HomeGrounds);
				AddLeagueFixture(Season, GroupA[J], GroupA[I], HomeGrounds);
				AddLeagueFixture(Season, GroupB[I], GroupB[J], HomeGrounds);
				AddLeagueFixture(Season, GroupB[J], GroupB[I], HomeGrounds);
			}
		// 2. Cross-group rivals (paired by position): home and away (2 per team).
		for (int32 I = 0; I < 5; ++I)
		{
			AddLeagueFixture(Season, GroupA[I], GroupB[I], HomeGrounds);
			AddLeagueFixture(Season, GroupB[I], GroupA[I], HomeGrounds);
		}
		// 3. Single meetings with the other four across the group (4 per team, 2 home / 2 away).
		//    The cross-group graph minus the rival pairs is 4-regular, so an Eulerian circuit orients
		//    every edge with indegree == outdegree == 2: exactly seven home and seven away each.
		TArray<TPair<int32, int32>> Singles;
		for (int32 I = 0; I < 5; ++I)
			for (int32 J = 0; J < 5; ++J)
			{
				if (I == J) continue;
				Singles.Add(TPair<int32, int32>(GroupA[I], GroupB[J]));
			}
		TArray<TArray<int32>> Adj;
		Adj.SetNum(NumTeams);
		for (int32 E = 0; E < Singles.Num(); ++E)
		{
			Adj[Singles[E].Key].Add(E);
			Adj[Singles[E].Value].Add(E);
		}
		for (TArray<int32>& L : Adj) L.Sort(TGreater<int32>());
		auto Other = [&](int32 E, int32 V) { return Singles[E].Key == V ? Singles[E].Value : Singles[E].Key; };
		TArray<char> Used;
		Used.SetNumZeroed(Singles.Num());
		TArray<int32> Stack, Circuit;
		Stack.Add(0);
		while (Stack.Num() > 0)
		{
			const int32 V = Stack.Last();
			while (Adj[V].Num() > 0 && Used[Adj[V].Last()]) Adj[V].Pop();
			if (Adj[V].Num() == 0) { Circuit.Add(V); Stack.Pop(); }
			else
			{
				const int32 E = Adj[V].Last();
				Adj[V].Pop();
				Used[E] = 1;
				Stack.Add(Other(E, V));
			}
		}
		TSet<int64> Oriented;
		for (int32 I = 0; I + 1 < Circuit.Num(); ++I)
		{
			const int32 U = Circuit[I], V = Circuit[I + 1];
			const int64 Key = int64(FMath::Min(U, V)) * 16 + FMath::Max(U, V);
			if (Oriented.Contains(Key)) continue;
			Oriented.Add(Key);
			AddLeagueFixture(Season, U, V, HomeGrounds);
		}
		// Safety: any pair the circuit missed still gets scheduled (loudly imbalanced, never lost).
		for (const TPair<int32, int32>& S : Singles)
		{
			const int64 Key = int64(FMath::Min(S.Key, S.Value)) * 16 + FMath::Max(S.Key, S.Value);
			if (Oriented.Contains(Key)) continue;
			AddLeagueFixture(Season, S.Key, S.Value, HomeGrounds);
		}
	}

	bool InvolvesUser(const FIPLSeason& Season, const FIPLFixture& Fixture)
	{
		return Fixture.Home == Season.UserTeam || Fixture.Away == Season.UserTeam;
	}

	int32 FixtureIndex(const FIPLSeason& Season, int32 FixtureId)
	{
		for (int32 I = 0; I < Season.Fixtures.Num(); ++I)
			if (Season.Fixtures[I].FixtureId == FixtureId) return I;
		return INDEX_NONE;
	}

	const FIPLFixture* FindFixture(const FIPLSeason& Season, int32 FixtureId)
	{
		const int32 I = FixtureIndex(Season, FixtureId);
		return I != INDEX_NONE ? &Season.Fixtures[I] : nullptr;
	}

	float TeamStrength(const TArray<int32>& PlayerIds, const TArray<FAuctionPlayer>& Players)
	{
		if (PlayerIds.Num() == 0) return 50.f;
		double Sum = 0.0;
		for (int32 Id : PlayerIds)
			if (Players.IsValidIndex(Id)) Sum += Players[Id].Overall();
		return float(Sum / PlayerIds.Num());
	}

	FIPLResult SimulateFixture(const FIPLSeason& Season, int32 FixtureIdx, const TArray<FAuctionPlayer>& Players)
	{
		FIPLResult R;
		if (!Season.Fixtures.IsValidIndex(FixtureIdx)) return R;
		const FIPLFixture& F = Season.Fixtures[FixtureIdx];
		R.FixtureId = F.FixtureId;
		FRandomStream Rng(F.FixtureId * 7321 + 17);
		const TArray<int32>& HomeXI = XIIsUsable(Season.LastXI.IsValidIndex(F.Home) ? Season.LastXI[F.Home] : FIPLPlayingXI(), Season.Squads[F.Home])
			? Season.LastXI[F.Home].BattingOrder : MakeDefaultXI(Season.Squads[F.Home].Players, Players).BattingOrder;
		const TArray<int32>& AwayXI = XIIsUsable(Season.LastXI.IsValidIndex(F.Away) ? Season.LastXI[F.Away] : FIPLPlayingXI(), Season.Squads[F.Away])
			? Season.LastXI[F.Away].BattingOrder : MakeDefaultXI(Season.Squads[F.Away].Players, Players).BattingOrder;
		R.BatFirst = Rng.RandRange(0, 1);
		const bool bHomeBatsFirst = R.BatFirst == 0;
		const FInningsSim First = SimInnings(bHomeBatsFirst ? HomeXI : AwayXI, bHomeBatsFirst ? AwayXI : HomeXI, Players, Rng, MatchBalls, 0);
		const FInningsSim Second = SimInnings(bHomeBatsFirst ? AwayXI : HomeXI, bHomeBatsFirst ? HomeXI : AwayXI, Players, Rng, MatchBalls, First.Runs + 1);
		const int32 FirstRuns = First.Runs, SecondRuns = Second.Runs;
		const int32 HomeRuns = bHomeBatsFirst ? FirstRuns : SecondRuns;
		const int32 AwayRuns = bHomeBatsFirst ? SecondRuns : FirstRuns;
		const FInningsSim& HomeIn = bHomeBatsFirst ? First : Second;
		const FInningsSim& AwayIn = bHomeBatsFirst ? Second : First;
		R.HomeRuns = HomeRuns; R.HomeWickets = HomeIn.Wickets; R.HomeBalls = HomeIn.Balls;
		R.AwayRuns = AwayRuns; R.AwayWickets = AwayIn.Wickets; R.AwayBalls = AwayIn.Balls;

		auto Code = [](int32 T) -> FString
		{
			const TArray<FAuctionFranchise>& Fr = AuctionData::Franchises();
			return Fr.IsValidIndex(T) ? Fr[T].Code : FString::Printf(TEXT("T%d"), T);
		};
		if (FirstRuns == SecondRuns)
		{
			// Level scores: the IPL decider is a Super Over, simulated the same rating-driven way.
			int32 HomeSO = 0, AwaySO = 0;
			for (int32 Round = 0; Round < 5 && HomeSO == AwaySO; ++Round)
			{
				const FInningsSim HSO = SimInnings(HomeXI, AwayXI, Players, Rng, 6, 0);
				const FInningsSim ASO = SimInnings(AwayXI, HomeXI, Players, Rng, HSO.Runs + 1, 0);
				HomeSO = HSO.Runs; AwaySO = ASO.Runs;
			}
			if (HomeSO == AwaySO)
				R.Winner = TeamStrength(HomeXI, Players) >= TeamStrength(AwayXI, Players) ? F.Home : F.Away;
			else
				R.Winner = HomeSO > AwaySO ? F.Home : F.Away;
			R.Margin = FString::Printf(TEXT("%s won the Super Over"), *Code(R.Winner));
			return R;
		}
		const bool bFirstWon = FirstRuns > SecondRuns;
		R.Winner = bFirstWon ? (bHomeBatsFirst ? F.Home : F.Away) : (bHomeBatsFirst ? F.Away : F.Home);
		if (bFirstWon)
			R.Margin = FString::Printf(TEXT("%s won by %d run%s"), *Code(R.Winner), FirstRuns - SecondRuns, (FirstRuns - SecondRuns) == 1 ? TEXT("") : TEXT("s"));
		else
		{
			const FInningsSim& Chase = Second;
			const int32 WktsLeft = 10 - Chase.Wickets;
			const int32 BallsLeft = MatchBalls - Chase.Balls;
			R.Margin = FString::Printf(TEXT("%s won by %d wicket%s (%d ball%s left)"), *Code(R.Winner), WktsLeft,
				WktsLeft == 1 ? TEXT("") : TEXT("s"), BallsLeft, BallsLeft == 1 ? TEXT("") : TEXT("s"));
		}
		return R;
	}

	bool CommitResult(FIPLSeason& Season, const FIPLResult& Result)
	{
		const int32 I = FixtureIndex(Season, Result.FixtureId);
		if (I == INDEX_NONE) return false;
		FIPLFixture& F = Season.Fixtures[I];
		if (F.Status == EIPLFixtureStatus::Completed) return false; // idempotent: never twice
		if (Result.HomeRuns < 0 || Result.AwayRuns < 0 || Result.HomeBalls < 0 || Result.AwayBalls < 0) return false;
		if (Result.HomeBalls > MatchBalls || Result.AwayBalls > MatchBalls) return false;
		if (Result.HomeWickets < 0 || Result.HomeWickets > 10 || Result.AwayWickets < 0 || Result.AwayWickets > 10) return false;
		if (!Result.bNoResult && Result.Winner != F.Home && Result.Winner != F.Away) return false;
		if (Result.bNoResult && Result.Winner != INDEX_NONE) return false;
		F.Result = Result;
		if (F.BatFirst == INDEX_NONE) F.BatFirst = Result.BatFirst != INDEX_NONE ? Result.BatFirst : 0;
		F.bHasResult = true;
		F.Status = EIPLFixtureStatus::Completed;
		if (F.Stage == EIPLStage::League)
			ApplyToTable(Season, Result);
		else
			AdvancePlayoffs(Season);
		if (LeagueComplete(Season) && FixtureOfStage(Season, EIPLStage::Qualifier1) == nullptr)
			BuildPlayoffs(Season);
		return true;
	}

	void ApplyToTable(FIPLSeason& Season, const FIPLResult& Result)
	{
		const int32 I = FixtureIndex(Season, Result.FixtureId);
		if (I == INDEX_NONE) return;
		const FIPLFixture& F = Season.Fixtures[I];
		FIPLTableRow& H = Season.Table[F.Home];
		FIPLTableRow& A = Season.Table[F.Away];
		H.Played++; A.Played++;
		if (Result.bNoResult)
		{
			H.NoResult++; A.NoResult++;
			H.Points += PointsNoResult; A.Points += PointsNoResult;
		}
		else if (Result.Winner == F.Home) { H.Won++; H.Points += PointsWin; A.Lost++; }
		else { A.Won++; A.Points += PointsWin; H.Lost++; }
		// An all-out side is deemed to have faced its full quota (IPL playing conditions).
		const int32 HomeBalls = Result.HomeWickets >= 10 ? FMath::Max(Result.HomeBalls, MatchBalls) : Result.HomeBalls;
		const int32 AwayBalls = Result.AwayWickets >= 10 ? FMath::Max(Result.AwayBalls, MatchBalls) : Result.AwayBalls;
		H.RunsFor += Result.HomeRuns; H.BallsFaced += HomeBalls;
		H.RunsAgainst += Result.AwayRuns; H.BallsBowled += AwayBalls;
		A.RunsFor += Result.AwayRuns; A.BallsFaced += AwayBalls;
		A.RunsAgainst += Result.HomeRuns; A.BallsBowled += HomeBalls;
		RecomputeNRR(H);
		RecomputeNRR(A);
	}

	void RecomputeNRR(FIPLTableRow& Row)
	{
		const double Faced = double(Row.BallsFaced) / 6.0;
		const double Bowled = double(Row.BallsBowled) / 6.0;
		const double For = Faced > 0.0 ? double(Row.RunsFor) / Faced : 0.0;
		const double Against = Bowled > 0.0 ? double(Row.RunsAgainst) / Bowled : 0.0;
		Row.NetRunRate = (Faced > 0.0 && Bowled > 0.0) ? (For - Against) : 0.0;
	}

	TArray<FIPLTableRow> RecomputeTable(const FIPLSeason& Season)
	{
		TArray<FIPLTableRow> Rows;
		Rows.SetNum(NumTeams);
		for (int32 T = 0; T < NumTeams; ++T) Rows[T].Team = T;
		for (const FIPLFixture& F : Season.Fixtures)
		{
			if (F.Stage != EIPLStage::League || F.Status != EIPLFixtureStatus::Completed || !F.bHasResult) continue;
			const FIPLResult& R = F.Result;
			FIPLTableRow& H = Rows[F.Home];
			FIPLTableRow& A = Rows[F.Away];
			H.Played++; A.Played++;
			if (R.bNoResult) { H.NoResult++; A.NoResult++; H.Points += PointsNoResult; A.Points += PointsNoResult; }
			else if (R.Winner == F.Home) { H.Won++; H.Points += PointsWin; A.Lost++; }
			else { A.Won++; A.Points += PointsWin; H.Lost++; }
			const int32 HomeBalls = R.HomeWickets >= 10 ? FMath::Max(R.HomeBalls, MatchBalls) : R.HomeBalls;
			const int32 AwayBalls = R.AwayWickets >= 10 ? FMath::Max(R.AwayBalls, MatchBalls) : R.AwayBalls;
			H.RunsFor += R.HomeRuns; H.BallsFaced += HomeBalls;
			H.RunsAgainst += R.AwayRuns; H.BallsBowled += AwayBalls;
			A.RunsFor += R.AwayRuns; A.BallsFaced += AwayBalls;
			A.RunsAgainst += R.HomeRuns; A.BallsBowled += HomeBalls;
		}
		for (FIPLTableRow& R : Rows) RecomputeNRR(R);
		return Rows;
	}

	TArray<int32> SortedTable(const FIPLSeason& Season)
	{
		TArray<int32> Order;
		for (int32 T = 0; T < NumTeams; ++T) Order.Add(T);
		Order.Sort([&Season](int32 A, int32 B)
		{
			const FIPLTableRow& RA = Season.Table[A];
			const FIPLTableRow& RB = Season.Table[B];
			if (RA.Points != RB.Points) return RA.Points > RB.Points;
			if (RA.Won != RB.Won) return RA.Won > RB.Won;
			if (!FMath::IsNearlyEqual(RA.NetRunRate, RB.NetRunRate, 1e-9)) return RA.NetRunRate > RB.NetRunRate;
			return A < B;
		});
		return Order;
	}

	bool LeagueComplete(const FIPLSeason& Season)
	{
		for (const FIPLFixture& F : Season.Fixtures)
			if (F.Stage == EIPLStage::League && F.Status != EIPLFixtureStatus::Completed) return false;
		return Season.Fixtures.Num() >= LeagueFixtures;
	}

	void BuildPlayoffs(FIPLSeason& Season)
	{
		if (!LeagueComplete(Season)) return;
		if (FixtureOfStage(Season, EIPLStage::Qualifier1) != nullptr) return; // idempotent
		const TArray<int32> Order = SortedTable(Season);
		const TArray<FString> Grounds = FranchiseGrounds();
		// Higher seed hosts: Qualifier 1 is 1st vs 2nd, the Eliminator 3rd vs 4th.
		AddPlayoffFixture(Season, EIPLStage::Qualifier1, Order[0], Order[1], Grounds);
		AddPlayoffFixture(Season, EIPLStage::Eliminator, Order[2], Order[3], Grounds);
		Season.Stage = EIPLStage::Qualifier1;
	}

	void AdvancePlayoffs(FIPLSeason& Season)
	{
		const TArray<FString> Grounds = FranchiseGrounds();
		const int32 Q1W = WinnerOf(Season, EIPLStage::Qualifier1);
		const int32 Q1L = LoserOf(Season, EIPLStage::Qualifier1);
		const int32 ElW = WinnerOf(Season, EIPLStage::Eliminator);
		if (Q1W != INDEX_NONE && ElW != INDEX_NONE && FixtureOfStage(Season, EIPLStage::Qualifier2) == nullptr)
		{
			// Qualifier 2: loser of Qualifier 1 vs winner of the Eliminator; higher league seed hosts.
			const TArray<int32> Order = SortedTable(Season);
			auto Rank = [&](int32 T) { return Order.Find(T); };
			const int32 Home = Rank(Q1L) <= Rank(ElW) ? Q1L : ElW;
			const int32 Away = Home == Q1L ? ElW : Q1L;
			AddPlayoffFixture(Season, EIPLStage::Qualifier2, Home, Away, Grounds);
			Season.Stage = EIPLStage::Qualifier2;
		}
		const int32 Q2W = WinnerOf(Season, EIPLStage::Qualifier2);
		if (Q1W != INDEX_NONE && Q2W != INDEX_NONE && FixtureOfStage(Season, EIPLStage::Final) == nullptr)
		{
			const TArray<int32> Order = SortedTable(Season);
			auto Rank = [&](int32 T) { return Order.Find(T); };
			const int32 Home = Rank(Q1W) <= Rank(Q2W) ? Q1W : Q2W;
			const int32 Away = Home == Q1W ? Q2W : Q1W;
			AddPlayoffFixture(Season, EIPLStage::Final, Home, Away, Grounds);
			Season.Stage = EIPLStage::Final;
		}
		const int32 FW = WinnerOf(Season, EIPLStage::Final);
		if (FW != INDEX_NONE)
		{
			Season.Champion = FW;
			Season.bComplete = true;
			Season.Stage = EIPLStage::Complete;
		}
	}

	bool Validate(const FIPLSeason& Season, FString& Why)
	{
		auto Fail = [&](const FString& Msg) { Why = Msg; return false; };
		if (Season.Squads.Num() != NumTeams) return Fail(FString::Printf(TEXT("squads %d != 10"), Season.Squads.Num()));
		if (Season.Table.Num() != NumTeams) return Fail(TEXT("table is not 10 rows"));
		if (Season.UserTeam < 0 || Season.UserTeam >= NumTeams) return Fail(TEXT("user franchise invalid"));
		TSet<int32> Owners;
		for (int32 T = 0; T < NumTeams; ++T)
		{
			const int32 N = Season.Squads[T].Players.Num();
			if (N < SquadMin || N > SquadMax)
				return Fail(FString::Printf(TEXT("team %d squad %d outside 18..25"), T, N));
			for (const FIPLSquadPlayer& S : Season.Squads[T].Players)
			{
				if (S.PlayerId == INDEX_NONE) return Fail(TEXT("squad holds an invalid player id"));
				if (Owners.Contains(S.PlayerId))
					return Fail(FString::Printf(TEXT("player %d owned by two franchises"), S.PlayerId));
				Owners.Add(S.PlayerId);
			}
		}
		if (Season.LastXI.Num() != NumTeams) return Fail(TEXT("XI memory is not 10 sides"));
		for (int32 T = 0; T < NumTeams; ++T)
		{
			const FIPLPlayingXI& XI = Season.LastXI[T];
			if (XI.BattingOrder.Num() == 0) continue;
			if (!XIIsUsable(XI, Season.Squads[T]))
				return Fail(FString::Printf(TEXT("team %d XI is not 11 unique owned players"), T));
		}
		TSet<int32> FixtureIds, MatchNumbers;
		int32 LeagueCount = 0;
		int32 PerTeamLeague[NumTeams] = {};
		for (const FIPLFixture& F : Season.Fixtures)
		{
			if (FixtureIds.Contains(F.FixtureId)) return Fail(FString::Printf(TEXT("duplicate fixture id %d"), F.FixtureId));
			FixtureIds.Add(F.FixtureId);
			if (MatchNumbers.Contains(F.MatchNumber)) return Fail(FString::Printf(TEXT("duplicate match number %d"), F.MatchNumber));
			MatchNumbers.Add(F.MatchNumber);
			if (F.Home < 0 || F.Home >= NumTeams || F.Away < 0 || F.Away >= NumTeams) return Fail(TEXT("fixture has an invalid side"));
			if (F.Home == F.Away) return Fail(FString::Printf(TEXT("fixture %d: a team plays itself"), F.FixtureId));
			if (F.Stage == EIPLStage::League) { LeagueCount++; PerTeamLeague[F.Home]++; PerTeamLeague[F.Away]++; }
			if (F.Status == EIPLFixtureStatus::Completed)
			{
				if (!F.bHasResult) return Fail(FString::Printf(TEXT("fixture %d completed without a result"), F.FixtureId));
				if (!F.Result.bNoResult && F.Result.Winner != F.Home && F.Result.Winner != F.Away)
					return Fail(FString::Printf(TEXT("fixture %d winner is not playing"), F.FixtureId));
			}
			else if (F.bHasResult) return Fail(FString::Printf(TEXT("fixture %d upcoming but holds a result"), F.FixtureId));
		}
		if (LeagueCount != LeagueFixtures)
			return Fail(FString::Printf(TEXT("league fixtures %d != 70"), LeagueCount));
		for (int32 T = 0; T < NumTeams; ++T)
			if (PerTeamLeague[T] != LeagueMatchesPerTeam)
				return Fail(FString::Printf(TEXT("team %d plays %d league fixtures, not 14"), T, PerTeamLeague[T]));
		// The table must agree exactly with the committed league fixtures: one standings system.
		const TArray<FIPLTableRow> Rebuilt = RecomputeTable(Season);
		for (int32 T = 0; T < NumTeams; ++T)
		{
			const FIPLTableRow& A = Season.Table[T];
			const FIPLTableRow& B = Rebuilt[T];
			if (A.Played != B.Played || A.Won != B.Won || A.Lost != B.Lost || A.NoResult != B.NoResult || A.Points != B.Points
				|| A.RunsFor != B.RunsFor || A.BallsFaced != B.BallsFaced || A.RunsAgainst != B.RunsAgainst || A.BallsBowled != B.BallsBowled
				|| !FMath::IsNearlyEqual(A.NetRunRate, B.NetRunRate, 1e-9))
				return Fail(FString::Printf(TEXT("table disagrees with fixtures for team %d"), T));
		}
		if (Season.bComplete)
		{
			if (Season.Champion < 0 || Season.Champion >= NumTeams) return Fail(TEXT("complete season without a champion"));
			if (WinnerOf(Season, EIPLStage::Final) != Season.Champion) return Fail(TEXT("champion is not the final winner"));
		}
		return true;
	}

	FString OversText(int32 LegalBalls)
	{
		return FString::Printf(TEXT("%d.%d"), LegalBalls / 6, LegalBalls % 6);
	}

	FString StageName(EIPLStage Stage)
	{
		switch (Stage)
		{
		case EIPLStage::League: return TEXT("League");
		case EIPLStage::Qualifier1: return TEXT("Qualifier 1");
		case EIPLStage::Eliminator: return TEXT("Eliminator");
		case EIPLStage::Qualifier2: return TEXT("Qualifier 2");
		case EIPLStage::Final: return TEXT("Final");
		default: return TEXT("Season");
		}
	}
}
