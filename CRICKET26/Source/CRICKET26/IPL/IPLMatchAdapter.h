// Auction -> match bridge. Converts the auction's players into the match engine's players with
// stable identity (name, hand, shirt number all derive from the auction record), builds the two
// 20-over sides from the fixture's Playing XIs in batting order, and converts a finished match
// back into a season result. No gameplay lives here: only data mapping.

#pragma once

#include "CoreMinimal.h"
#include "IPLTypes.h"
#include "CricketTypes.h"

struct FAuctionPlayer;
struct FAuctionFranchise;
struct FSuperOverMatch;

namespace IPLMatchAdapter
{
	/** Number on the shirt: stable in the player id, unique inside a squad. */
	int32 ShirtNumber(int32 PlayerId);

	/** Auction record -> engine player. Ratings scale 0..99 to the engine's 0..1 attributes. */
	FCricketPlayer FromAuctionPlayer(const FAuctionPlayer& P);

	/** Is this XI legal: eleven unique players, all owned by the franchise? */
	bool ValidateXI(const FIPLSeason& Season, int32 Team, const FIPLPlayingXI& XI, FString* Why = nullptr);

	/**
	 * The two 20-over sides for a fixture: eleven batters each in the XI's batting order
	 * (index 0/1 open), the opening bowler set to the side's best bowling option.
	 * Returns { Home, Away }.
	 */
	TArray<FCricketTeam> BuildMatchTeams(const FIPLSeason& Season, const FIPLFixture& Fixture,
		const FIPLPlayingXI& HomeXI, const FIPLPlayingXI& AwayXI,
		const TArray<FAuctionPlayer>& Players, const TArray<FAuctionFranchise>& Franchises);

	/** Finished engine match -> season result (scores, overs, winner, margin). */
	FIPLResult ResultFromMatch(const FSuperOverMatch& Match, const FIPLFixture& Fixture);

	// ---- In-match selection ---------------------------------------------------------------
	/** XI slots that may walk out next: unused (no balls, not out), not at the crease. */
	TArray<int32> EligibleBatters(const FSuperOverMatch& Match);
	/**
	 * XI slots that may bowl the next over: under the 4-over T20 limit and not the bowler of
	 * the over just bowled (consecutive overs are illegal). BallsBowled is legal balls per XI slot.
	 */
	TArray<int32> EligibleBowlers(const FIPLPlayingXI& XI, const TArray<int32>& BallsBowled, int32 LastBowlerSlot,
		const TArray<FAuctionPlayer>& Players);
	/** The AI's new-ball / next-over pick: best eligible bowling rating, pace/spin alternating. */
	int32 ChooseAIBowler(const FIPLPlayingXI& XI, const TArray<int32>& BallsBowled, int32 LastBowlerSlot,
		const TArray<FAuctionPlayer>& Players);
	/** Legal balls bowled per XI slot, read back from the innings' per-bowler cards. */
	TArray<int32> BowlerBallsFromMatch(const FSuperOverMatch& Match);
}
