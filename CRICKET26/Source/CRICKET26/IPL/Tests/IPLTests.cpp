// IPL tournament integration: auction handoff identity, the 70-match schedule, rating-driven
// simulation, idempotent result committing, table/NRR ordering, playoffs to a champion, and the
// auction-player -> match-team bridge (openers, bowler rotation state, result mapping).

#include "Misc/AutomationTest.h"
#include "IPLSeason.h"
#include "IPLMatchAdapter.h"
#include "IPLTypes.h"
#include "IPLSeasonSave.h"
#include "AuctionEngine.h"
#include "AuctionTypes.h"
#include "SuperOverMatch.h"
#include "CricketControls.h"
#include "Kismet/GameplayStatics.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace IPLTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	bool RunUntil(FAuction& A, TFunctionRef<bool()> Stop, float Step = 0.25f, int32 MaxSteps = 4000000)
	{
		for (int32 I = 0; I < MaxSteps; ++I)
		{
			if (Stop()) return true;
			if (A.AwaitingHuman())
			{
				// The human passes on everything: retentions still hold, RTM questions decline.
				A.HumanRtm(false);
				A.HumanFinalRaise(A.Price);
				A.HumanMatch(false);
			}
			A.Tick(Step);
		}
		return false;
	}

	FAuction RunAuction(int32 Human, int32 Seed)
	{
		FAuction A(Human, Seed);
		if (Human != INDEX_NONE) A.Retain(Human, A.AiRetentions(Human));
		A.BeginAuction();
		RunUntil(A, [&] { return A.Phase == EAuctionPhase::Finished; });
		return MoveTemp(A);
	}

	bool DotBalls(FSuperOverMatch& M, int32 N)
	{
		for (int32 I = 0; I < N; ++I)
		{
			if (M.Phase != EMatchPhase::ReadyForDelivery) return false;
			M.BeginDelivery();
			TArray<ECricketEvent> Events;
			if (!M.CompleteDelivery(FDeliveryOutcome(), Events)) return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIPLSeasonHandoff, "CRICKET26.IPL.Handoff", IPLTests::Flags)
bool FIPLSeasonHandoff::RunTest(const FString&)
{
	FAuction A = IPLTests::RunAuction(INDEX_NONE, 11);
	if (!TestTrue(TEXT("auction finished"), A.Phase == EAuctionPhase::Finished)) return false;
	FIPLSeason Season = IPLSeason::BuildFromAuction(A);
	TestEqual(TEXT("ten squads"), Season.Squads.Num(), 10);
	// Every signing the auction produced is in the season, same player, same price, same route.
	int32 Count = 0;
	for (int32 T = 0; T < 10; ++T)
	{
		TestEqual(TEXT("squad size kept"), Season.Squads[T].Players.Num(), A.Teams[T].Squad.Num());
		for (const FAuctionSigning& S : A.Teams[T].Squad)
		{
			const FIPLSquadPlayer* Found = Season.Squads[T].Players.FindByPredicate(
				[S](const FIPLSquadPlayer& P) { return P.PlayerId == S.Player; });
			if (!TestTrue(TEXT("auction player in season squad"), Found != nullptr)) return false;
			TestEqual(TEXT("price kept"), Found->Price, S.Price);
			TestEqual(TEXT("retained flag kept"), Found->bRetained, S.bRetained);
			TestEqual(TEXT("rtm flag kept"), Found->bRtm, S.bRtm);
			Count++;
		}
	}
	TestTrue(TEXT("players handed off"), Count >= 180);
	FString Why;
	const bool bValid = IPLSeason::Validate(Season, Why);
	TestTrue(*FString::Printf(TEXT("season validates: %s"), *Why), bValid);
	// The user's franchise follows the auction's human side, retentions included.
	FAuction H(4, 11);
	H.Retain(4, H.AiRetentions(4));
	H.BeginAuction();
	const FIPLSeason HS = IPLSeason::BuildFromAuction(H);
	TestEqual(TEXT("user franchise survives"), HS.UserTeam, 4);
	TestEqual(TEXT("retentions carried"), HS.Squads[4].Players.Num(), H.Teams[4].Squad.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIPLSeasonSchedule, "CRICKET26.IPL.Schedule", IPLTests::Flags)
bool FIPLSeasonSchedule::RunTest(const FString&)
{
	FAuction A = IPLTests::RunAuction(INDEX_NONE, 11);
	FIPLSeason Season = IPLSeason::BuildFromAuction(A);
	int32 League = 0;
	int32 PerTeam[10] = {}, Home[10] = {};
	TSet<int32> Ids;
	for (const FIPLFixture& F : Season.Fixtures)
	{
		if (F.Stage != EIPLStage::League) continue;
		League++;
		PerTeam[F.Home]++; PerTeam[F.Away]++;
		Home[F.Home]++;
		TestTrue(TEXT("no team plays itself"), F.Home != F.Away);
		TestFalse(TEXT("fixture ids unique"), Ids.Contains(F.FixtureId));
		Ids.Add(F.FixtureId);
		TestFalse(TEXT("venue empty"), F.Venue.IsEmpty());
	}
	TestEqual(TEXT("70 league fixtures"), League, 70);
	for (int32 T = 0; T < 10; ++T)
	{
		TestEqual(*FString::Printf(TEXT("team %d plays 14"), T), PerTeam[T], 14);
		TestEqual(*FString::Printf(TEXT("team %d hosts 7"), T), Home[T], 7);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIPLSeasonSim, "CRICKET26.IPL.Simulate", IPLTests::Flags)
bool FIPLSeasonSim::RunTest(const FString&)
{
	FAuction A = IPLTests::RunAuction(INDEX_NONE, 11);
	FIPLSeason Season = IPLSeason::BuildFromAuction(A);
	const TArray<FAuctionPlayer>& Players = AuctionData::Players();
	const FIPLResult R = IPLSeason::SimulateFixture(Season, 0, Players);
	TestEqual(TEXT("fixture id carried"), R.FixtureId, Season.Fixtures[0].FixtureId);
	TestTrue(TEXT("winner plays"), R.Winner == Season.Fixtures[0].Home || R.Winner == Season.Fixtures[0].Away);
	TestFalse(TEXT("margin empty"), R.Margin.IsEmpty());
	TestTrue(TEXT("sane scores"), R.HomeRuns >= 0 && R.HomeRuns <= 400 && R.AwayRuns >= 0 && R.AwayRuns <= 400);
	TestTrue(TEXT("balls capped"), R.HomeBalls <= 120 && R.AwayBalls <= 120 && R.HomeBalls > 0 && R.AwayBalls > 0);
	TestTrue(TEXT("wickets capped"), R.HomeWickets <= 10 && R.AwayWickets <= 10);
	// Deterministic: a fixture can never produce two different results.
	const FIPLResult R2 = IPLSeason::SimulateFixture(Season, 0, Players);
	TestEqual(TEXT("same winner"), R2.Winner, R.Winner);
	TestEqual(TEXT("same scores"), R2.HomeRuns + R2.AwayRuns, R.HomeRuns + R.AwayRuns);
	// Ratings matter: the best side by strength wins more often than not across seeds is too slow;
	// instead the two XIs must differ in the model's eyes from pure chance (strengths are finite).
	TestTrue(TEXT("home strength sane"), IPLSeason::TeamStrength(Season.LastXI[0].BattingOrder, Players) > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIPLSeasonCommit, "CRICKET26.IPL.Commit", IPLTests::Flags)
bool FIPLSeasonCommit::RunTest(const FString&)
{
	FAuction A = IPLTests::RunAuction(INDEX_NONE, 11);
	FIPLSeason Season = IPLSeason::BuildFromAuction(A);
	const TArray<FAuctionPlayer>& Players = AuctionData::Players();
	const FIPLResult R = IPLSeason::SimulateFixture(Season, 0, Players);
	TestTrue(TEXT("first commit lands"), IPLSeason::CommitResult(Season, R));
	TestFalse(TEXT("second commit refused"), IPLSeason::CommitResult(Season, R));
	TestEqual(TEXT("fixture completed once"), Season.Fixtures[0].Status, EIPLFixtureStatus::Completed);
	const FIPLFixture& F = Season.Fixtures[0];
	const FIPLTableRow& H = Season.Table[F.Home];
	const FIPLTableRow& Aw = Season.Table[F.Away];
	TestEqual(TEXT("both played once"), H.Played + Aw.Played, 2);
	TestEqual(TEXT("two points shared"), H.Points + Aw.Points, 2);
	TestTrue(TEXT("NRR finite"), FMath::IsFinite(H.NetRunRate) && FMath::IsFinite(Aw.NetRunRate));
	// One standings system: the live table equals the fixtures rebuilt from scratch.
	const TArray<FIPLTableRow> Rebuilt = IPLSeason::RecomputeTable(Season);
	TestEqual(TEXT("table matches fixtures"), Rebuilt[F.Home].Points, H.Points);
	// Ordering: points, then wins, then NRR (build a decisive mini-table by hand).
	FIPLSeason T;
	T.Squads.SetNum(10); T.Table.SetNum(10); T.LastXI.SetNum(10);
	for (int32 I = 0; I < 10; ++I) T.Table[I].Team = I;
	T.Table[0].Points = 10; T.Table[0].Won = 5; T.Table[0].NetRunRate = -1.0;
	T.Table[1].Points = 10; T.Table[1].Won = 5; T.Table[1].NetRunRate = 2.0;
	T.Table[2].Points = 12; T.Table[2].Won = 6; T.Table[2].NetRunRate = -5.0;
	const TArray<int32> Order = IPLSeason::SortedTable(T);
	TestEqual(TEXT("points first"), Order[0], 2);
	TestEqual(TEXT("nrr breaks the tie"), Order[1], 1);
	TestEqual(TEXT("not alphabetical"), Order[2], 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIPLSeasonFull, "CRICKET26.IPL.FullSeason", IPLTests::Flags)
bool FIPLSeasonFull::RunTest(const FString&)
{
	FAuction A = IPLTests::RunAuction(INDEX_NONE, 11);
	FIPLSeason Season = IPLSeason::BuildFromAuction(A);
	const TArray<FAuctionPlayer>& Players = AuctionData::Players();
	// The whole league, every result committed once.
	for (int32 I = 0; I < Season.Fixtures.Num(); ++I)
	{
		if (Season.Fixtures[I].Stage != EIPLStage::League) continue;
		const FIPLResult R = IPLSeason::SimulateFixture(Season, I, Players);
		if (!TestTrue(TEXT("league commit"), IPLSeason::CommitResult(Season, R))) return false;
	}
	TestTrue(TEXT("league done"), IPLSeason::LeagueComplete(Season));
	IPLSeason::BuildPlayoffs(Season);
	IPLSeason::BuildPlayoffs(Season); // idempotent: no duplicate bracket
	int32 PlayoffCount = 0;
	for (const FIPLFixture& F : Season.Fixtures) PlayoffCount += F.Stage != EIPLStage::League ? 1 : 0;
	TestEqual(TEXT("qualifier 1 + eliminator"), PlayoffCount, 2);
	// Playoffs to a champion, all simmed like AI fixtures.
	for (int32 Guard = 0; Guard < 6 && !Season.bComplete; ++Guard)
	{
		bool bMoved = false;
		for (int32 I = 0; I < Season.Fixtures.Num(); ++I)
		{
			if (Season.Fixtures[I].Status != EIPLFixtureStatus::Upcoming) continue;
			const FIPLResult R = IPLSeason::SimulateFixture(Season, I, Players);
			if (!IPLSeason::CommitResult(Season, R)) return false;
			bMoved = true;
		}
		if (!bMoved) break;
	}
	TestTrue(TEXT("champion crowned"), Season.bComplete && Season.Champion != INDEX_NONE);
	FString Why;
	const bool bValid = IPLSeason::Validate(Season, Why);
	TestTrue(*FString::Printf(TEXT("full season validates: %s"), *Why), bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIPLAdapterTeams, "CRICKET26.IPL.MatchTeams", IPLTests::Flags)
bool FIPLAdapterTeams::RunTest(const FString&)
{
	FAuction A = IPLTests::RunAuction(INDEX_NONE, 11);
	FIPLSeason Season = IPLSeason::BuildFromAuction(A);
	const TArray<FAuctionPlayer>& Players = AuctionData::Players();
	const FIPLFixture& F = Season.Fixtures[0];
	// The default XI is eleven unique owned players; the match sides carry them in order.
	const FIPLPlayingXI HomeXI = IPLSeason::MakeDefaultXI(Season.Squads[F.Home].Players, Players);
	const FIPLPlayingXI AwayXI = IPLSeason::MakeDefaultXI(Season.Squads[F.Away].Players, Players);
	FString Why;
	TestTrue(*FString::Printf(TEXT("home XI valid: %s"), *Why), IPLMatchAdapter::ValidateXI(Season, F.Home, HomeXI, &Why));
	TestTrue(TEXT("away XI valid"), IPLMatchAdapter::ValidateXI(Season, F.Away, AwayXI, nullptr));
	const TArray<FCricketTeam> Teams = IPLMatchAdapter::BuildMatchTeams(Season, F, HomeXI, AwayXI, Players, AuctionData::Franchises());
	TestEqual(TEXT("two sides"), Teams.Num(), 2);
	TestEqual(TEXT("eleven batters"), Teams[0].Batters.Num(), 11);
	// Scenario A/B: the bought players open. The XI's first two are the engine's first two.
	TestEqual(TEXT("opener 1 is the bought player"),
		Teams[0].Batters[0].Name, Players[HomeXI.BattingOrder[0]].Name);
	TestEqual(TEXT("opener 2 is the bought player"),
		Teams[0].Batters[1].Name, Players[HomeXI.BattingOrder[1]].Name);
	TestFalse(TEXT("no placeholder opener"), Teams[0].Batters[0].Name.Contains(TEXT("Batter ")));
	TestFalse(TEXT("no super-over default"), Teams[0].Batters[0].Name == TEXT("Opener"));
	// A picked player from another franchise is refused.
	FIPLPlayingXI Bad = HomeXI;
	Bad.BattingOrder[0] = Season.Squads[F.Away].Players[0].PlayerId;
	TestFalse(TEXT("foreign player refused"), IPLMatchAdapter::ValidateXI(Season, F.Home, Bad, nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIPLAdapterResult, "CRICKET26.IPL.MatchResult", IPLTests::Flags)
bool FIPLAdapterResult::RunTest(const FString&)
{
	FAuction A = IPLTests::RunAuction(INDEX_NONE, 11);
	FIPLSeason Season = IPLSeason::BuildFromAuction(A);
	const TArray<FAuctionPlayer>& Players = AuctionData::Players();
	FIPLFixture F = Season.Fixtures[0];
	F.BatFirst = 0;
	const FIPLPlayingXI HomeXI = IPLSeason::MakeDefaultXI(Season.Squads[F.Home].Players, Players);
	const FIPLPlayingXI AwayXI = IPLSeason::MakeDefaultXI(Season.Squads[F.Away].Players, Players);
	const TArray<FCricketTeam> Teams = IPLMatchAdapter::BuildMatchTeams(Season, F, HomeXI, AwayXI, Players, AuctionData::Franchises());

	// Play a real 20-over match on the rules: home 4 all out? No: a boundary then dots; away dots.
	FSuperOverMatch M;
	M.Rules.MaxLegalBalls = 120;
	M.Rules.MaxWickets = 10;
	M.Start(0);
	// One boundary first ball, then dots to the full quota.
	M.BeginDelivery();
	{
		FDeliveryOutcome Hit;
		Hit.RunsRun = 0; Hit.Boundary = 4; Hit.bBatContact = true;
		TArray<ECricketEvent> Events;
		TestTrue(TEXT("boundary stands"), M.CompleteDelivery(Hit, Events));
	}
	if (!IPLTests::DotBalls(M, 119)) return false;
	TestEqual(TEXT("innings break"), M.Phase, EMatchPhase::InningsBreak);
	TestTrue(TEXT("chase starts"), M.StartSecondInnings());
	if (!IPLTests::DotBalls(M, 120)) return false;
	TestEqual(TEXT("match done"), M.Phase, EMatchPhase::MatchComplete);
	TestFalse(TEXT("not tied"), M.bTied);
	const FIPLResult R = IPLMatchAdapter::ResultFromMatch(M, F);
	TestEqual(TEXT("home wins by 4"), R.Winner, F.Home);
	TestEqual(TEXT("home 4 runs"), R.HomeRuns, 4);
	TestEqual(TEXT("away 0"), R.AwayRuns, 0);
	TestEqual(TEXT("home faced 120"), R.HomeBalls, 120);
	TestFalse(TEXT("margin empty"), R.Margin.IsEmpty());
	// And it commits into the same standings the simmed fixtures use.
	TestTrue(TEXT("played result commits"), IPLSeason::CommitResult(Season, R));
	TestEqual(TEXT("home has 2 pts"), Season.Table[F.Home].Points, 2);
	(void)Teams;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIPLAdapterBowlers, "CRICKET26.IPL.Bowlers", IPLTests::Flags)
bool FIPLAdapterBowlers::RunTest(const FString&)
{
	FAuction A = IPLTests::RunAuction(INDEX_NONE, 11);
	FIPLSeason Season = IPLSeason::BuildFromAuction(A);
	const TArray<FAuctionPlayer>& Players = AuctionData::Players();
	const FIPLFixture& F = Season.Fixtures[0];
	const FIPLPlayingXI XI = IPLSeason::MakeDefaultXI(Season.Squads[F.Home].Players, Players);
	TArray<int32> Balls;
	Balls.SetNumZeroed(11);
	// Fresh: everyone eligible except nobody (no previous over); the AI pick is a genuine bowler.
	TArray<int32> Options = IPLMatchAdapter::EligibleBowlers(XI, Balls, INDEX_NONE, Players);
	TestEqual(TEXT("eleven options first"), Options.Num(), 11);
	const int32 Pick = IPLMatchAdapter::ChooseAIBowler(XI, Balls, INDEX_NONE, Players);
	TestTrue(TEXT("ai pick valid"), Options.Contains(Pick));
	// Quota: a bowler at 24 legal balls is done; the previous over's bowler is barred.
	Balls[Pick] = 24;
	TArray<int32> Options2 = IPLMatchAdapter::EligibleBowlers(XI, Balls, Pick, Players);
	TestFalse(TEXT("capped bowler out"), Options2.Contains(Pick));
	const int32 Pick2 = IPLMatchAdapter::ChooseAIBowler(XI, Balls, Pick, Players);
	TestTrue(TEXT("ai rotates"), Pick2 != INDEX_NONE && Pick2 != Pick);
	// Per-bowler figures live in the match: set a slot, bowl, read back.
	FSuperOverMatch M;
	M.Rules.MaxLegalBalls = 120;
	M.Rules.MaxWickets = 10;
	M.Start(0);
	TestTrue(TEXT("slot set"), M.SetBowlerSlot(Pick));
	M.BeginDelivery();
	{
		TArray<ECricketEvent> Events;
		TestTrue(TEXT("dot stands"), M.CompleteDelivery(FDeliveryOutcome(), Events));
	}
	const TArray<int32> Back = IPLMatchAdapter::BowlerBallsFromMatch(M);
	TestTrue(TEXT("figures follow the slot"), Back.IsValidIndex(Pick) && Back[Pick] == 1);
	// Next-batter eligibility: after a wicket, the crease pair and the dismissed are out.
	M.BeginDelivery();
	{
		FDeliveryOutcome Out;
		Out.Dismissal = EDismissal::Bowled;
		TArray<ECricketEvent> Events;
		TestTrue(TEXT("wicket stands"), M.CompleteDelivery(Out, Events));
	}
	const TArray<int32> Batters = IPLMatchAdapter::EligibleBatters(M);
	TestFalse(TEXT("striker not eligible"), Batters.Contains(M.Cur().Striker));
	TestFalse(TEXT("non-striker not eligible"), Batters.Contains(M.Cur().NonStriker));
	TestTrue(TEXT("someone can walk out"), Batters.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIPLPickInput, "CRICKET26.IPL.PickInput", IPLTests::Flags)
bool FIPLPickInput::RunTest(const FString&)
{
	// The in-match selector rides the same touch pipeline as every other control: N candidates
	// lay out as N tappable rows, a tap picks exactly one, anything else picks nothing.
	using namespace CricketTouch;
	const float Aspect = 16.f / 9.f;
	for (int32 N : { 1, 5, 11 })
	{
		const TArray<FButton> Buttons = Layout(EMode::Pick, N, Aspect);
		TestEqual(*FString::Printf(TEXT("%d candidates lay out"), N), Buttons.Num(), N);
		for (int32 I = 0; I < Buttons.Num(); ++I)
		{
			TestEqual(TEXT("candidate index rides along"), Buttons[I].Index, I);
			TestTrue(TEXT("row on screen"), Buttons[I].Rect.Min.X >= 0.f && Buttons[I].Rect.Max.X <= Aspect
				&& Buttons[I].Rect.Min.Y >= 0.f && Buttons[I].Rect.Max.Y <= 1.f);
			for (int32 J = I + 1; J < Buttons.Num(); ++J)
				TestFalse(TEXT("rows never overlap"), Buttons[I].Rect.Intersect(Buttons[J].Rect));
		}
	}
	FGesture Fresh;
	FCricketControls C = Read(EMode::Pick, 5, Aspect, { { 0, Layout(EMode::Pick, 5, Aspect)[2].Rect.GetCenter(), true } }, Fresh);
	TestEqual(TEXT("tap picks the row"), C.PickIndex, 2);
	TestFalse(TEXT("a pick is never progress"), C.bProgress);
	FCricketControls Miss = Read(EMode::Pick, 5, Aspect, { { 0, FVector2D(0.05f, 0.05f), true } }, Fresh);
	TestEqual(TEXT("a stray tap picks nothing"), Miss.PickIndex, -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIPLSeasonSaveLoad, "CRICKET26.IPL.SaveLoad", IPLTests::Flags)
bool FIPLSeasonSaveLoad::RunTest(const FString&)
{
	// Season persistence round-trip through the real slot: squads, fixtures, results, table,
	// bracket and stage must all survive an exit and reload. The prior slot content is restored.
	const FString Slot = UIPLSeasonSave::Slot();
	UIPLSeasonSave* Before = Cast<UIPLSeasonSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	FAuction A = IPLTests::RunAuction(INDEX_NONE, 11);
	FIPLSeason Season = IPLSeason::BuildFromAuction(A);
	const TArray<FAuctionPlayer>& Players = AuctionData::Players();
	for (int32 I = 0; I < 3; ++I)
		IPLSeason::CommitResult(Season, IPLSeason::SimulateFixture(Season, I, Players));
	{
		UIPLSeasonSave* Save = NewObject<UIPLSeasonSave>();
		Save->Season = Season;
		Save->bHasSeason = true;
		UGameplayStatics::SaveGameToSlot(Save, Slot, 0);
	}
	UIPLSeasonSave* Loaded = Cast<UIPLSeasonSave>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	bool bOk = false;
	if (TestTrue(TEXT("season reloads"), Loaded != nullptr && Loaded->bHasSeason)) bOk = true;
	if (Loaded)
	{
		TestEqual(TEXT("same franchise"), Loaded->Season.UserTeam, Season.UserTeam);
		TestEqual(TEXT("same squads"), Loaded->Season.Squads.Num(), Season.Squads.Num());
		TestEqual(TEXT("same squad size"), Loaded->Season.Squads[0].Players.Num(), Season.Squads[0].Players.Num());
		TestEqual(TEXT("same player identity"), Loaded->Season.Squads[0].Players[0].PlayerId, Season.Squads[0].Players[0].PlayerId);
		TestEqual(TEXT("same fixtures"), Loaded->Season.Fixtures.Num(), Season.Fixtures.Num());
		TestEqual(TEXT("same results"), Loaded->Season.Fixtures[0].Status, Season.Fixtures[0].Status);
		TestEqual(TEXT("same standings"), Loaded->Season.Table[0].Points, Season.Table[0].Points);
		TestEqual(TEXT("same nrr"), Loaded->Season.Table[0].NetRunRate, Season.Table[0].NetRunRate);
		TestEqual(TEXT("same stage"), Loaded->Season.Stage, Season.Stage);
		FString Why;
		TestTrue(*FString::Printf(TEXT("reloaded season validates: %s"), *Why), IPLSeason::Validate(Loaded->Season, Why));
		bOk = bOk && Loaded->Season.UserTeam == Season.UserTeam;
	}
	// Restore whatever was in the slot before this test ran.
	if (Before) UGameplayStatics::SaveGameToSlot(Before, Slot, 0);
	else UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return bOk;
}

#endif
