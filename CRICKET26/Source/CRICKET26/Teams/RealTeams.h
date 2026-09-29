// Real teams for quick matches: the twelve ICC full members with their current T20I squads and playing XIs
// (Scripts/teams/International.csv, compiled in by Scripts/teams/international.py), and the ten IPL franchises with
// their original 2026 squads (Team2026 in the IPL roster; auction-built squads never appear here). Plain data and
// pure functions: the frontend picks from it, the match builds its two sides from it, and tests read the same code.

#pragma once

#include "CoreMinimal.h"
#include "AuctionTypes.h"
#include "CricketTypes.h"
#include "IPLTypes.h"

enum class ECompetition : uint8 { International, IPL, Count };

struct FRealTeam
{
	FString Code, Name;
	FLinearColor Primary = FLinearColor::Blue, Secondary = FLinearColor::White;
	/** The squad as player ids into RealTeams::Players(Competition), in the order it was announced. */
	TArray<int32> Squad;
	/** The team's real playing XI in batting order. IPL: the best XI the game picks from the real squad. */
	FIPLPlayingXI RealXI;
	int32 Captain = INDEX_NONE; // player id, or INDEX_NONE when the data names none
};

namespace RealTeams
{
	const TCHAR* CompetitionName(ECompetition C); // "INTERNATIONAL" / "IPL"
	/** Every player of a competition, Id = index. IPL: the auction database; International: the national squads. */
	const TArray<FAuctionPlayer>& Players(ECompetition C);
	/** The competition's teams in a fixed order: India first internationally, CSK first in the IPL. */
	const TArray<FRealTeam>& Teams(ECompetition C);

	/** One International.csv, parsed: its players (Id = row), and per player the team code, XI slot (1-11, 0 when not
	 *  in the XI) and captaincy. Exposed for tests. */
	struct FParsedSquads
	{
		TArray<FAuctionPlayer> Players;
		TArray<FString> Team;
		TArray<int32> XISlot;
		TArray<bool> bCaptain;
	};
	FParsedSquads ParseSquads(const FString& Csv);
	/** Groups parsed players into teams (in the order of Codes) with each team's XI in its batting order. */
	TArray<FRealTeam> BuildTeams(const FParsedSquads& Parsed, const TArray<FRealTeam>& Codes);

	/** Eleven different members of the team's squad. */
	bool ValidXI(const FRealTeam& Team, const FIPLPlayingXI& XI, FString* Why = nullptr);
	/** The team's real XI when it is valid, else the best XI from the squad. */
	FIPLPlayingXI DefaultXI(ECompetition C, int32 TeamIndex);

	/** Legal balls one bowler may bowl in a match of Overs overs: a fifth of the overs, rounded up (a T20's four). */
	int32 MaxBallsPerBowler(int32 Overs);

	/** A team and its XI as a side the match engine plays: the XI in batting order, the best bowler opening. */
	FCricketTeam MatchSide(const FRealTeam& Team, const FIPLPlayingXI& XI, const TArray<FAuctionPlayer>& Players);

	// ---- The toss ------------------------------------------------------------------------------------------------
	/** The coin: true for heads. */
	bool FlipCoin(FRandomStream& Rng);
	/**
	 * What a computer captain who wins the toss does: bats first with the stronger batting side, otherwise chases, as
	 * modern T20 captains mostly do; a fifth of the time they go against that. BatStrength and BowlStrength are 0..99.
	 */
	bool AIElectsToBat(FRandomStream& Rng, float BatStrength, float BowlStrength);
	/** Average batting of the XI's top seven and bowling of its five best bowlers, 0..99, for the toss call. */
	void XIStrength(const FIPLPlayingXI& XI, const TArray<FAuctionPlayer>& Players, float& OutBat, float& OutBowl);
}
