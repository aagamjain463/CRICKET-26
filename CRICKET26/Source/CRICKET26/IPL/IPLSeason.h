// IPL season orchestration: auction handoff, the 70-match league schedule, rating-driven AI
// simulation, idempotent result committing, IPL net run rate and table ordering, and the
// four-match playoff bracket. Pure functions over FIPLSeason (plus the auction/player data they
// are built from), so automation tests exercise the same code the game runs.

#pragma once

#include "CoreMinimal.h"
#include "IPLTypes.h"

class FAuction;
struct FAuctionPlayer;

namespace IPLSeason
{
	// ---- Tournament constants (IPL playing conditions for a normal season) ----------------------
	constexpr int32 NumTeams = 10;
	constexpr int32 LeagueFixtures = 70;
	constexpr int32 LeagueMatchesPerTeam = 14;
	constexpr int32 SquadMin = 18;
	constexpr int32 SquadMax = 25;
	constexpr int32 PlayingXI = 11;
	constexpr int32 MatchOvers = 20;
	constexpr int32 MatchBalls = MatchOvers * 6;
	constexpr int32 MaxOversPerBowler = 4;
	constexpr int32 MaxBallsPerBowler = MaxOversPerBowler * 6;
	constexpr int32 PointsWin = 2;
	constexpr int32 PointsNoResult = 1;
	constexpr int32 Qualifiers = 4;

	/** Auction -> season handoff: takes the final auction squads verbatim (no regeneration). */
	FIPLSeason BuildFromAuction(const FAuction& Auction);
	/** Best-eleven default XI from a squad (keeper + five bowling options repaired in). */
	FIPLPlayingXI MakeDefaultXI(const TArray<FIPLSquadPlayer>& Squad, const TArray<FAuctionPlayer>& Players);
	/** Grouped 70-match league schedule: 10 sides, 14 fixtures each (home-and-away inside the group
	 *  plus a cross-group rival home-and-away and single meetings with the rest, 7 home / 7 away). */
	void GenerateLeagueFixtures(FIPLSeason& Season, const TArray<FString>& HomeGrounds);

	bool InvolvesUser(const FIPLSeason& Season, const FIPLFixture& Fixture);
	int32 FixtureIndex(const FIPLSeason& Season, int32 FixtureId);
	const FIPLFixture* FindFixture(const FIPLSeason& Season, int32 FixtureId);

	// ---- Simulation (AI vs AI, and AI playoff sides) --------------------------------------------
	/** Overall team strength 0..99 from the XI that would play (ratings-driven, never a coin flip). */
	float TeamStrength(const TArray<int32>& PlayerIds, const TArray<FAuctionPlayer>& Players);
	/** Full score record for a fixture: toss, both innings ball-by-ball-lite, super-over decider on a
	 *  tie. Deterministic in the fixture id so a fixture can never produce two different results. */
	FIPLResult SimulateFixture(const FIPLSeason& Season, int32 FixtureIdx, const TArray<FAuctionPlayer>& Players);

	// ---- Results (idempotent: a fixture commits exactly once) ------------------------------------
	/** Commits a result to the season. False (no state touched) when the fixture is unknown,
	 *  already completed, or fails validation: reopening a result screen can never double-count. */
	bool CommitResult(FIPLSeason& Season, const FIPLResult& Result);
	void ApplyToTable(FIPLSeason& Season, const FIPLResult& Result);
	void RecomputeNRR(FIPLTableRow& Row);
	/** Rebuilds the whole table from committed fixtures (validation and tests). */
	TArray<FIPLTableRow> RecomputeTable(const FIPLSeason& Season);
	/** IPL ordering: points, then wins, then net run rate. Never alphabetical. */
	TArray<int32> SortedTable(const FIPLSeason& Season);

	// ---- Playoffs --------------------------------------------------------------------------------
	bool LeagueComplete(const FIPLSeason& Season);
	/** Creates Qualifier 1 (1st vs 2nd) and the Eliminator (3rd vs 4th) once the league is done.
	 *  Idempotent: does nothing when playoff fixtures already exist. */
	void BuildPlayoffs(FIPLSeason& Season);
	/** Creates Qualifier 2 then the Final as their predecessors complete; crowns the champion. */
	void AdvancePlayoffs(FIPLSeason& Season);

	// ---- Integrity --------------------------------------------------------------------------------
	/** Catches integration bugs: duplicate players, lost squads, bad fixtures, double commits,
	 *  XI outside the squad, table disagreeing with fixtures. */
	bool Validate(const FIPLSeason& Season, FString& Why);

	// ---- Display -----------------------------------------------------------------------------------
	FString OversText(int32 LegalBalls);
	FString StageName(EIPLStage Stage);
}
