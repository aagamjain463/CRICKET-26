// Real teams for quick matches: the national squads and XIs compiled in from Scripts/teams/International.csv, the IPL's
// original 2026 squads (never the auction's), XI rules, the bowler's over limit by format, the engine sides and the toss.

#include "Misc/AutomationTest.h"
#include "RealTeams.h"
#include "IPLSeason.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace RealTeamsTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	int32 TeamIndex(ECompetition C, const TCHAR* Code)
	{
		return RealTeams::Teams(C).IndexOfByPredicate([Code](const FRealTeam& T) { return T.Code == Code; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRealTeamsNationsTest, "CRICKET26.Teams.Nations", RealTeamsTests::Flags)
bool FRealTeamsNationsTest::RunTest(const FString&)
{
	const TArray<FRealTeam>& Teams = RealTeams::Teams(ECompetition::International);
	const TArray<FAuctionPlayer>& Players = RealTeams::Players(ECompetition::International);
	TestEqual(TEXT("the twelve ICC full members"), Teams.Num(), 12);
	for (const TCHAR* Code : { TEXT("IND"), TEXT("AUS"), TEXT("ENG"), TEXT("SA"), TEXT("NZ"), TEXT("PAK"), TEXT("SL"), TEXT("WI"),
		TEXT("BAN"), TEXT("AFG"), TEXT("IRE"), TEXT("ZIM") })
		TestTrue(FString::Printf(TEXT("%s is playable"), Code), RealTeamsTests::TeamIndex(ECompetition::International, Code) != INDEX_NONE);
	TSet<int32> Seen;
	for (int32 I = 0; I < Teams.Num(); ++I)
	{
		const FRealTeam& T = Teams[I];
		TestTrue(FString::Printf(TEXT("%s squad is a real squad size"), *T.Code), T.Squad.Num() >= 13 && T.Squad.Num() <= 18);
		FString Why;
		const bool bLegal = RealTeams::ValidXI(T, T.RealXI, &Why);
		TestTrue(FString::Printf(TEXT("%s real XI is legal (%s)"), *T.Code, *Why), bLegal);
		TestTrue(FString::Printf(TEXT("%s default XI is their real XI"), *T.Code),
			RealTeams::DefaultXI(ECompetition::International, I).BattingOrder == T.RealXI.BattingOrder);
		TestTrue(FString::Printf(TEXT("%s has a captain in the squad"), *T.Code), T.Squad.Contains(T.Captain));
		int32 Keepers = 0, Bowlers = 0;
		for (int32 Id : T.RealXI.BattingOrder)
		{
			if (!Players.IsValidIndex(Id)) continue;
			Keepers += Players[Id].Role == EAuctionRole::Keeper;
			Bowlers += Players[Id].Role == EAuctionRole::Pace || Players[Id].Role == EAuctionRole::Spin || Players[Id].Role == EAuctionRole::AllRounder;
		}
		TestTrue(FString::Printf(TEXT("%s XI keeps wicket"), *T.Code), Keepers >= 1);
		TestTrue(FString::Printf(TEXT("%s XI has five bowling options"), *T.Code), Bowlers >= 5);
		for (int32 Id : T.Squad)
		{
			TestFalse(TEXT("nobody plays for two nations"), Seen.Contains(Id));
			Seen.Add(Id);
			TestTrue(TEXT("ratings on the 0..99 scale"), Players.IsValidIndex(Id) && Players[Id].BatRating > 0 && Players[Id].BowlRating > 0);
		}
	}
	// Spot checks against the sources (Scripts/teams/SOURCES.md).
	const int32 Ind = RealTeamsTests::TeamIndex(ECompetition::International, TEXT("IND"));
	if (Teams.IsValidIndex(Ind) && Teams[Ind].RealXI.BattingOrder.Num() == 11)
	{
		TestEqual(TEXT("India open with Abhishek Sharma"), Players[Teams[Ind].RealXI.BattingOrder[0]].Name, FString(TEXT("Abhishek Sharma")));
		TestTrue(TEXT("India's captain"), Players.IsValidIndex(Teams[Ind].Captain) && Players[Teams[Ind].Captain].Name == TEXT("Shreyas Iyer"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRealTeamsIplTest, "CRICKET26.Teams.IplOriginalSquads", RealTeamsTests::Flags)
bool FRealTeamsIplTest::RunTest(const FString&)
{
	const TArray<FRealTeam>& Teams = RealTeams::Teams(ECompetition::IPL);
	const TArray<FAuctionPlayer>& Players = RealTeams::Players(ECompetition::IPL);
	TestEqual(TEXT("the ten franchises"), Teams.Num(), 10);
	TestTrue(TEXT("IPL players are the auction database"), &Players == &AuctionData::Players());
	for (int32 I = 0; I < Teams.Num(); ++I)
	{
		const FRealTeam& T = Teams[I];
		// The squad is exactly the original 2026 squad: every Team2026 player and nobody else.
		int32 Original = 0;
		for (const FAuctionPlayer& P : Players) Original += P.Team2026 == T.Code;
		TestEqual(FString::Printf(TEXT("%s squad is its original 2026 squad"), *T.Code), T.Squad.Num(), Original);
		for (int32 Id : T.Squad) TestEqual(TEXT("only the franchise's own players"), Players[Id].Team2026, T.Code);
		TestTrue(FString::Printf(TEXT("%s best XI is legal"), *T.Code), RealTeams::ValidXI(T, RealTeams::DefaultXI(ECompetition::IPL, I)));
		TestTrue(FString::Printf(TEXT("%s names its captain"), *T.Code), T.Squad.Contains(T.Captain));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRealTeamsRulesTest, "CRICKET26.Teams.Rules", RealTeamsTests::Flags)
bool FRealTeamsRulesTest::RunTest(const FString&)
{
	// Parsing keeps the squad columns in step with the player rows, skipping a row with no name.
	const RealTeams::FParsedSquads P = RealTeams::ParseSquads(
		TEXT("Name,Role,BatHand,Team,XI,Captain,BatRating,BowlRating\nA One,BAT,R,AAA,1,1,70,10\n,BAT,R,AAA,2,0,1,1\nB Two,PACE,L,AAA,,0,20,70\n"));
	TestEqual(TEXT("two players"), P.Players.Num(), 2);
	TestEqual(TEXT("columns in step"), P.Team.Num(), 2);
	TestEqual(TEXT("XI slot read"), P.XISlot[0], 1);
	TestEqual(TEXT("not in the XI"), P.XISlot[1], 0);
	TestTrue(TEXT("captain read"), P.bCaptain[0] && !P.bCaptain[1]);
	TestTrue(TEXT("left-hander read"), P.Players[1].bLeftBat);

	// The bowler's limit: a fifth of the overs, rounded up.
	TestEqual(TEXT("super over"), RealTeams::MaxBallsPerBowler(1), 6);
	TestEqual(TEXT("three overs"), RealTeams::MaxBallsPerBowler(3), 6);
	TestEqual(TEXT("five overs"), RealTeams::MaxBallsPerBowler(5), 6);
	TestEqual(TEXT("ten overs"), RealTeams::MaxBallsPerBowler(10), 12);
	TestEqual(TEXT("a T20"), RealTeams::MaxBallsPerBowler(20), IPLSeason::MaxBallsPerBowler);

	// XI rules.
	const TArray<FRealTeam>& Teams = RealTeams::Teams(ECompetition::International);
	if (!TestTrue(TEXT("teams loaded"), Teams.Num() >= 2)) return false;
	const FRealTeam& Home = Teams[0];
	FIPLPlayingXI XI = Home.RealXI;
	FString Why;
	TestTrue(TEXT("the real XI is legal"), RealTeams::ValidXI(Home, XI, &Why));
	XI.BattingOrder.Pop();
	TestFalse(TEXT("ten players are not an XI"), RealTeams::ValidXI(Home, XI, &Why));
	XI.BattingOrder.Add(XI.BattingOrder[0]);
	TestFalse(TEXT("nobody bats twice"), RealTeams::ValidXI(Home, XI, &Why));
	XI.BattingOrder.Last() = Teams[1].Squad[0];
	TestFalse(TEXT("no player from the other side"), RealTeams::ValidXI(Home, XI, &Why));

	// The engine side: the XI in batting order, opened with its best bowler.
	const TArray<FAuctionPlayer>& Players = RealTeams::Players(ECompetition::International);
	const FCricketTeam Side = RealTeams::MatchSide(Home, Home.RealXI, Players);
	TestEqual(TEXT("eleven batters"), Side.Batters.Num(), 11);
	TestEqual(TEXT("batting order kept"), Side.Batters[0].Name, Players[Home.RealXI.BattingOrder[0]].Name);
	int32 Best = Home.RealXI.BattingOrder[0];
	for (int32 Id : Home.RealXI.BattingOrder) if (Players[Id].BowlRating > Players[Best].BowlRating) Best = Id;
	TestEqual(TEXT("the best bowler opens"), Side.Bowler.Name, Players[Best].Name);
	TestEqual(TEXT("named for the team"), Side.Name, Home.Name);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRealTeamsTossTest, "CRICKET26.Teams.Toss", RealTeamsTests::Flags)
bool FRealTeamsTossTest::RunTest(const FString&)
{
	FRandomStream Coin(26);
	int32 Heads = 0;
	for (int32 I = 0; I < 10000; ++I) Heads += RealTeams::FlipCoin(Coin);
	TestTrue(TEXT("a fair coin"), Heads > 4800 && Heads < 5200);
	// A captain bats first with the stronger batting, otherwise chases, and sometimes goes against that.
	int32 StrongBatsFirst = 0, WeakBatsFirst = 0;
	for (int32 I = 0; I < 1000; ++I)
	{
		StrongBatsFirst += RealTeams::AIElectsToBat(Coin, 80.f, 60.f);
		WeakBatsFirst += RealTeams::AIElectsToBat(Coin, 55.f, 75.f);
	}
	TestTrue(TEXT("strong batting mostly bats"), StrongBatsFirst > 700 && StrongBatsFirst < 900);
	TestTrue(TEXT("strong bowling mostly chases"), WeakBatsFirst > 100 && WeakBatsFirst < 300);
	float Bat = 0.f, Bowl = 0.f;
	const TArray<FRealTeam>& Teams = RealTeams::Teams(ECompetition::International);
	RealTeams::XIStrength(Teams[0].RealXI, RealTeams::Players(ECompetition::International), Bat, Bowl);
	TestTrue(TEXT("strengths on the rating scale"), Bat > 30.f && Bat <= 99.f && Bowl > 30.f && Bowl <= 99.f);
	return true;
}

#endif
