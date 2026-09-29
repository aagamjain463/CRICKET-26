// IPL tournament vocabulary: the single persistent season state every system shares.
// The auction writes franchise squads here once; fixtures, the points table, playoffs and the
// champion all live here; gameplay reads its teams from here and commits results back here.
// Player identity is always the stable auction player Id (FAuctionPlayer::Id = index into
// AuctionData::Players()), never a display string, so the same player flows database -> auction ->
// squad -> XI -> match -> scorecard -> season stats.

#pragma once

#include "CoreMinimal.h"
#include "IPLTypes.generated.h"

UENUM(BlueprintType)
enum class EIPLStage : uint8
{
	League,
	Qualifier1,
	Eliminator,
	Qualifier2,
	Final,
	Complete
};

UENUM(BlueprintType)
enum class EIPLFixtureStatus : uint8
{
	Upcoming,
	Completed
};

/** One owned player: the stable auction Id plus what the franchise paid and how he arrived. */
USTRUCT(BlueprintType)
struct FIPLSquadPlayer
{
	GENERATED_BODY()

	UPROPERTY() int32 PlayerId = INDEX_NONE;
	UPROPERTY() int32 Price = 0; // lakh
	UPROPERTY() bool bRetained = false;
	UPROPERTY() bool bRtm = false;
};

USTRUCT(BlueprintType)
struct FIPLSquad
{
	GENERATED_BODY()

	UPROPERTY() TArray<FIPLSquadPlayer> Players;
};

/**
 * A team's selected XI: eleven owned player Ids in batting order. The first two open;
 * after a wicket the user picks any remaining eligible member; any member may bowl
 * subject to the over limits the match enforces.
 */
USTRUCT(BlueprintType)
struct FIPLPlayingXI
{
	GENERATED_BODY()

	UPROPERTY() TArray<int32> BattingOrder;
};

/** Everything a result needs to update the table: scores, overs, winner and margin. */
USTRUCT(BlueprintType)
struct FIPLResult
{
	GENERATED_BODY()

	UPROPERTY() int32 FixtureId = INDEX_NONE;
	UPROPERTY() int32 Winner = INDEX_NONE; // franchise index, INDEX_NONE for no-result
	UPROPERTY() bool bNoResult = false;
	UPROPERTY() int32 BatFirst = INDEX_NONE; // 0: home batted first, 1: away batted first
	UPROPERTY() int32 HomeRuns = 0;
	UPROPERTY() int32 HomeWickets = 0;
	UPROPERTY() int32 HomeBalls = 0; // legal balls faced
	UPROPERTY() int32 AwayRuns = 0;
	UPROPERTY() int32 AwayWickets = 0;
	UPROPERTY() int32 AwayBalls = 0; // legal balls faced
	UPROPERTY() FString Margin;      // "MI won by 7 runs"
};

USTRUCT(BlueprintType)
struct FIPLFixture
{
	GENERATED_BODY()

	UPROPERTY() int32 FixtureId = INDEX_NONE;
	UPROPERTY() int32 MatchNumber = 0;
	UPROPERTY() EIPLStage Stage = EIPLStage::League;
	UPROPERTY() int32 Home = INDEX_NONE;
	UPROPERTY() int32 Away = INDEX_NONE; // franchise indices into Squads/Table
	UPROPERTY() FString Venue;
	UPROPERTY() int32 BatFirst = INDEX_NONE; // 0: home bats first, 1: away bats first
	UPROPERTY() EIPLFixtureStatus Status = EIPLFixtureStatus::Upcoming;
	UPROPERTY() bool bHasResult = false;
	UPROPERTY() FIPLResult Result;
};

USTRUCT(BlueprintType)
struct FIPLTableRow
{
	GENERATED_BODY()

	UPROPERTY() int32 Team = INDEX_NONE;
	UPROPERTY() int32 Played = 0;
	UPROPERTY() int32 Won = 0;
	UPROPERTY() int32 Lost = 0;
	UPROPERTY() int32 NoResult = 0;
	UPROPERTY() int32 Points = 0;
	UPROPERTY() int32 RunsFor = 0;
	UPROPERTY() int32 BallsFaced = 0;
	UPROPERTY() int32 RunsAgainst = 0;
	UPROPERTY() int32 BallsBowled = 0;
	UPROPERTY() double NetRunRate = 0.0;
};

/**
 * The authoritative IPL season. One instance, persisted in UIPLSeasonSave; the tournament
 * model owns fixture truth, the table and the bracket, never the UI widgets.
 */
USTRUCT(BlueprintType)
struct FIPLSeason
{
	GENERATED_BODY()

	UPROPERTY() FString SeasonId;
	UPROPERTY() int32 UserTeam = INDEX_NONE;
	UPROPERTY() TArray<FIPLSquad> Squads;      // 10, franchise order = AuctionData::Franchises()
	UPROPERTY() TArray<FIPLFixture> Fixtures;  // league (70) then playoffs as they are created
	UPROPERTY() TArray<FIPLTableRow> Table;    // 10, index = franchise
	UPROPERTY() EIPLStage Stage = EIPLStage::League;
	UPROPERTY() int32 Champion = INDEX_NONE;
	UPROPERTY() bool bComplete = false;
	UPROPERTY() TArray<FIPLPlayingXI> LastXI;  // 10, remembered default/selected XI per team
	UPROPERTY() int32 NextMatchNumber = 1;
};
