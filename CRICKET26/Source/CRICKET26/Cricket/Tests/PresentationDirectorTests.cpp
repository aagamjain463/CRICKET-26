// Presentation Director automation tests: levels, merging, fire-once milestones, participants,
// anti-repetition and pacing. Pure logic, no world.

#include "Misc/AutomationTest.h"
#include "CricketPresentation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CricketPresentationDirectorTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	using namespace CricketPresentation;

	FDeliveryInput Ball(int32 Runs = 0, int32 Boundary = 0, EDismissal Out = EDismissal::None)
	{
		FDeliveryInput In;
		In.StrikerId = TEXT("0/0");
		In.BowlerId = TEXT("1/bowl");
		In.Ctx.RunsRun = Boundary ? 0 : Runs;
		In.Ctx.Boundary = Boundary;
		In.Ctx.Dismissal = Out;
		In.Ctx.BallsRemaining = 3;
		In.Ctx.LegalBalls = 3;
		return In;
	}

	bool HasBeat(const FScenePlan& S, ERole Who, EAction A)
	{
		return S.Beats.ContainsByPredicate([&](const FBeat& B) { return B.Who == Who && B.Action == A; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationLevels, "CRICKET26.PresentationDirector.Levels", CricketPresentationDirectorTests::Flags)
bool FPresentationLevels::RunTest(const FString&)
{
	using namespace CricketPresentationDirectorTests;
	FDirector D;
	// Only special moments get a live scene: no one stops to celebrate a boundary.
	TestEqual(TEXT("dot: no scene"), Direct(D, Ball()).Duration, 0.f);
	TestEqual(TEXT("single: no scene"), Direct(D, Ball(1)).Duration, 0.f);
	TestEqual(TEXT("four: no scene"), Direct(D, Ball(0, 4)).Duration, 0.f);
	TestEqual(TEXT("six: no scene"), Direct(D, Ball(0, 6)).Duration, 0.f);
	FDeliveryInput Edge = Ball();
	Edge.bEdge = true;
	TestEqual(TEXT("edge: no scene"), Direct(D, Edge).Duration, 0.f);
	const FScenePlan Out = Direct(D, Ball(0, 0, EDismissal::Bowled));
	TestTrue(TEXT("wicket: scene"), Out.Duration > 0.f);
	FDeliveryInput Win = Ball(0, 6);
	Win.Ctx.bChase = Win.Ctx.bMatchComplete = true;
	Win.Ctx.Winner = 0;
	const FScenePlan W = Direct(D, Win);
	TestEqual(TEXT("match-winning boundary"), W.Type, EScene::MatchWinBoundary);
	TestEqual(TEXT("level 5"), W.Level, 5);
	TestEqual(TEXT("result graphic"), W.Graphic, EGraphic::Result);
	// Every shot and beat lies inside the scene.
	for (const FShot& S : W.Shots) TestTrue(TEXT("shot inside scene"), S.Start >= 0.f && S.Start + S.Duration <= W.Duration + 1e-3f);
	TestNotNull(TEXT("shot at start"), W.ShotAt(0.f));
	TestNull(TEXT("no shot past end"), W.ShotAt(W.Duration + 0.1f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationMilestones, "CRICKET26.PresentationDirector.Milestones", CricketPresentationDirectorTests::Flags)
bool FPresentationMilestones::RunTest(const FString&)
{
	using namespace CricketPresentationDirectorTests;
	FDirector D;
	// Exactly 50 off a single: a routine ball becomes a milestone scene, once.
	FDeliveryInput Single = Ball(1);
	Single.StrikerRunsBefore = 49;
	Single.StrikerRunsAfter = 50;
	const FScenePlan Fifty = Direct(D, Single);
	TestEqual(TEXT("fifty"), Fifty.Milestone, EMilestone::Fifty);
	TestEqual(TEXT("threshold"), Fifty.MilestoneRuns, 50);
	TestEqual(TEXT("milestone graphic"), Fifty.Graphic, EGraphic::Milestone);
	TestTrue(TEXT("striker raises the bat"), HasBeat(Fifty, ERole::Striker, EAction::RaiseBat));
	TestTrue(TEXT("scene plays"), Fifty.Duration >= 4.f);
	Single.StrikerRunsBefore = 50;
	Single.StrikerRunsAfter = 51;
	TestEqual(TEXT("fires once"), Direct(D, Single).Milestone, EMilestone::None);
	// Crossing by a six (96 -> 102): the hundred still fires.
	FDeliveryInput Six = Ball(0, 6);
	Six.StrikerRunsBefore = 96;
	Six.StrikerRunsAfter = 102;
	TestEqual(TEXT("hundred via six"), Direct(D, Six).Milestone, EMilestone::Hundred);
	// Another batter has their own fifty.
	Single.StrikerId = TEXT("0/1");
	Single.StrikerRunsBefore = 49;
	Single.StrikerRunsAfter = 50;
	TestEqual(TEXT("other batter fifty"), Direct(D, Single).Milestone, EMilestone::Fifty);
	// A wide never makes a batting milestone.
	FDeliveryInput Wide = Ball(1);
	Wide.StrikerId = TEXT("0/2");
	Wide.Ctx.bWide = true;
	Wide.StrikerRunsBefore = 49;
	Wide.StrikerRunsAfter = 50;
	TestEqual(TEXT("no milestone off a wide"), Direct(D, Wide).Milestone, EMilestone::None);

	// Hat-trick: three wickets on consecutive legal balls, a wide in between does not break it; once only.
	FMilestoneTracker T;
	TestEqual(TEXT("1st"), T.Bowling(TEXT("b"), 1, true, true), EMilestone::None);
	TestEqual(TEXT("wide"), T.Bowling(TEXT("b"), 1, false, false), EMilestone::None);
	TestEqual(TEXT("2nd"), T.Bowling(TEXT("b"), 2, true, true), EMilestone::None);
	TestEqual(TEXT("3rd"), T.Bowling(TEXT("b"), 3, true, true), EMilestone::HatTrick);
	TestEqual(TEXT("4th is not a second hat-trick"), T.Bowling(TEXT("b"), 4, true, true), EMilestone::None);
	TestEqual(TEXT("5th: five-wicket haul"), T.Bowling(TEXT("b"), 5, true, true), EMilestone::FiveWickets);
	TestEqual(TEXT("haul once"), T.Bowling(TEXT("b"), 6, true, true), EMilestone::None);
	T.Bowling(TEXT("c"), 1, true, true);
	T.Bowling(TEXT("c"), 1, false, true); // dot breaks it
	T.Bowling(TEXT("c"), 2, true, true);
	TestEqual(TEXT("broken streak"), T.Bowling(TEXT("c"), 3, true, true), EMilestone::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationMerge, "CRICKET26.PresentationDirector.Merge", CricketPresentationDirectorTests::Flags)
bool FPresentationMerge::RunTest(const FString&)
{
	using namespace CricketPresentationDirectorTests;
	FDirector D;
	// Match-winning century: one integrated scene, not two.
	FDeliveryInput In = Ball(0, 6);
	In.Ctx.bChase = In.Ctx.bMatchComplete = true;
	In.Ctx.Winner = 0;
	In.StrikerRunsBefore = 97;
	In.StrikerRunsAfter = 103;
	const FScenePlan S = Direct(D, In);
	TestEqual(TEXT("still the winning scene"), S.Type, EScene::MatchWinBoundary);
	TestEqual(TEXT("with the century"), S.Milestone, EMilestone::Hundred);
	TestTrue(TEXT("merged"), S.bMerged);
	TestEqual(TEXT("level 5"), S.Level, 5);
	TestEqual(TEXT("opens on the centurion"), S.Shots[0].Subject, ERole::Striker);
	TestTrue(TEXT("team celebration kept"), HasBeat(S, ERole::BattingPair, EAction::Celebrate));
	TestTrue(TEXT("longer than the plain win"), S.Duration > LevelDuration(5, EPacing::Balanced));
	// A six bringing up a fifty is the fifty's scene.
	FDeliveryInput SixFifty = Ball(0, 6);
	SixFifty.StrikerId = TEXT("0/5");
	SixFifty.StrikerRunsBefore = 46;
	SixFifty.StrikerRunsAfter = 52;
	const FScenePlan F50 = Direct(D, SixFifty);
	TestEqual(TEXT("six to fifty: milestone"), F50.Milestone, EMilestone::Fifty);
	TestTrue(TEXT("six to fifty: scene plays"), F50.Duration > 0.f && HasBeat(F50, ERole::Striker, EAction::RaiseBat));
	// A boundary to end an over: the talk between overs still happens.
	FDeliveryInput FourOver = Ball(0, 4);
	FourOver.bOverComplete = true;
	FourOver.OverRuns = 10;
	TestEqual(TEXT("over talk after a four"), Direct(D, FourOver).Type, EScene::EndOfOver);

	// End of over after a routine ball becomes the over scene; after a wicket the wicket keeps the screen.
	FDeliveryInput Over = Ball();
	Over.bOverComplete = true;
	Over.OverRuns = 0;
	TestEqual(TEXT("maiden"), Direct(D, Over).Type, EScene::Maiden);
	Over.OverRuns = 18;
	TestEqual(TEXT("expensive"), Direct(D, Over).Type, EScene::ExpensiveOver);
	Over.Ctx.Dismissal = EDismissal::Bowled;
	TestEqual(TEXT("wicket wins"), Direct(D, Over).Type, EScene::Wicket);

	// Before the final ball of a chase: the last-ball build-up.
	FDeliveryInput Pen = Ball();
	Pen.Ctx.bChase = true;
	Pen.Ctx.BallsRemaining = 1;
	Pen.Ctx.RunsRequired = 4;
	const FScenePlan Last = Direct(D, Pen);
	TestEqual(TEXT("last ball"), Last.Type, EScene::LastBall);
	TestEqual(TEXT("equation graphic"), Last.Graphic, EGraphic::Equation);
	TestTrue(TEXT("skippable, never blocks"), Last.bSkippable);

	// Last ball of the first innings: a routine ball becomes the break with the target; a wicket keeps the screen.
	FDeliveryInput Break = Ball(1);
	Break.bOverComplete = true;
	Break.Ctx.bInningsBreak = true;
	const FScenePlan Innings = Direct(D, Break);
	TestEqual(TEXT("innings break"), Innings.Type, EScene::InningsBreak);
	TestEqual(TEXT("target graphic"), Innings.Graphic, EGraphic::Target);
	TestTrue(TEXT("batters walk off"), HasBeat(Innings, ERole::BattingPair, EAction::WalkOff));
	Break.Ctx.Boundary = 6;
	const FScenePlan SixBreak = Direct(D, Break);
	TestEqual(TEXT("a six to end the innings: the break"), SixBreak.Type, EScene::InningsBreak);
	TestEqual(TEXT("with the target"), SixBreak.Graphic, EGraphic::Target);
	Break.Ctx.Boundary = 0;
	Break.Ctx.Dismissal = EDismissal::Bowled;
	TestEqual(TEXT("wicket keeps the screen"), Direct(D, Break).Type, EScene::Wicket);

	// Every win ends with the bowler shaking the striker's hand.
	TestTrue(TEXT("handshake"), HasBeat(S, ERole::Bowler, EAction::Handshake));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationWicketHeroes, "CRICKET26.PresentationDirector.WicketHeroes", CricketPresentationDirectorTests::Flags)
bool FPresentationWicketHeroes::RunTest(const FString&)
{
	using namespace CricketPresentationDirectorTests;
	struct FCase { EDismissal How; bool bKeeper, bCandB; ERole Hero; };
	const FCase Cases[] = {
		{ EDismissal::Bowled, false, false, ERole::Bowler }, { EDismissal::LBW, false, false, ERole::Bowler },
		{ EDismissal::Caught, false, false, ERole::Catcher }, { EDismissal::Caught, true, false, ERole::Keeper },
		{ EDismissal::Caught, false, true, ERole::Bowler }, { EDismissal::Stumped, false, false, ERole::Keeper },
		{ EDismissal::RunOut, false, false, ERole::Catcher }, { EDismissal::HitWicket, false, false, ERole::Bowler },
	};
	FDirector D;
	for (const FCase& C : Cases)
	{
		FDeliveryInput In = Ball(0, 0, C.How);
		In.bKeeperCatch = C.bKeeper;
		In.bCaughtAndBowled = C.bCandB;
		const FScenePlan S = Direct(D, In);
		TestEqual(TEXT("wicket scene"), S.Type, EScene::Wicket);
		TestTrue(TEXT("right hero celebrates"), HasBeat(S, C.Hero, EAction::Celebrate));
		TestTrue(TEXT("team converges"), HasBeat(S, ERole::BowlingTeam, EAction::Converge));
		TestTrue(TEXT("batter walks off"), HasBeat(S, ERole::DismissedBatter, EAction::WalkOff));
		// One celebrant: the hero never also converges on themselves.
		TestFalse(TEXT("no duplicate hero"), HasBeat(S, C.Hero, EAction::Converge));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationRepetition, "CRICKET26.PresentationDirector.Repetition", CricketPresentationDirectorTests::Flags)
bool FPresentationRepetition::RunTest(const FString&)
{
	using namespace CricketPresentationDirectorTests;
	FDirector D;
	// 20 wickets: no variant twice in a row, and every variant is seen.
	TSet<int32> Seen;
	int32 Last = -1;
	for (int32 I = 0; I < 20; ++I)
	{
		const FScenePlan S = Direct(D, Ball(0, 0, EDismissal::Bowled));
		TestNotEqual(TEXT("no immediate repeat"), S.Variant, Last);
		Last = S.Variant;
		Seen.Add(S.Variant);
	}
	TestEqual(TEXT("all variants used"), Seen.Num(), VariantCount(EScene::Wicket));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationPacing, "CRICKET26.PresentationDirector.Pacing", CricketPresentationDirectorTests::Flags)
bool FPresentationPacing::RunTest(const FString&)
{
	using namespace CricketPresentationDirectorTests;
	FDirector Full, Bal, Quick;
	Full.Pacing = EPacing::Full;
	Quick.Pacing = EPacing::Quick;
	const FScenePlan F = Direct(Full, Ball(0, 0, EDismissal::Bowled)), B = Direct(Bal, Ball(0, 0, EDismissal::Bowled)), Q = Direct(Quick, Ball(0, 0, EDismissal::Bowled));
	TestTrue(TEXT("full > balanced > quick"), F.Duration > B.Duration && B.Duration > Q.Duration);
	FDeliveryInput Over = Ball();
	Over.bOverComplete = true;
	Over.OverRuns = 8;
	TestEqual(TEXT("quick drops the over talk"), Direct(Quick, Over).Duration, 0.f);
	TestFalse(TEXT("quick skips a level-3 replay"), Q.bReplay);
	TestEqual(TEXT("full still skips routine balls"), Direct(Full, Ball(0, 6)).Duration, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPresentationPlayerOfMatch, "CRICKET26.PresentationDirector.PlayerOfMatch", CricketPresentationDirectorTests::Flags)
bool FPresentationPlayerOfMatch::RunTest(const FString&)
{
	FSuperOverMatch M;
	M.Start(0);
	FInningsState& A = M.Innings[0];
	A.Batters[0].Runs = 14; A.Batters[0].Balls = 4; A.Batters[0].Sixes = 2;
	A.Batters[1].Runs = 2; A.Batters[1].Balls = 2;
	A.Bowler.Balls = 6; A.Bowler.Runs = 16;
	M.Innings.AddDefaulted_GetRef().BattingTeam = 1;
	FInningsState& B = M.Innings[1];
	B.Batters.SetNum(3);
	B.Batters[0].Runs = 5; B.Batters[0].Balls = 6;
	B.Bowler.Balls = 6; B.Bowler.Runs = 5; B.Bowler.Wickets = 1;
	M.Winner = 0;
	const CricketPresentation::FPlayerImpact P = CricketPresentation::PlayerOfMatch(M);
	TestEqual(TEXT("winning side's hitter"), P.Team, 0);
	TestEqual(TEXT("opener"), P.Batter, 0);
	TestFalse(TEXT("batter"), P.bBowler);
	return true;
}

#endif
