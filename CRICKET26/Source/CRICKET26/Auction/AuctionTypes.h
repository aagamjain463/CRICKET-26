// IPL auction: the players, the ten franchises and the money rules. Money is always in lakh (100 lakh = 1 crore),
// which is how the auctioneer counts it. Plain C++: the engine and its tests need no UObjects (they also build
// headless, Scripts/auction/sim/run.sh), and the 3D room and HUD only read it.

#pragma once

#include "CoreMinimal.h"

enum class EAuctionRole : uint8 { Batter, Keeper, AllRounder, Pace, Spin, Count };

/** What a player does in a T20 side beyond his role: Players.csv "Tags", written by Scripts/auction/ratings.py. */
namespace AuctionTags
{
	constexpr uint32 Opener = 1u << 0, Anchor = 1u << 1, Finisher = 1u << 2, KeeperBat = 1u << 3, PowerplayPace = 1u << 4,
		DeathPace = 1u << 5, LeftArmPace = 1u << 6, WristSpin = 1u << 7, FingerSpin = 1u << 8, Mystery = 1u << 9, Captain = 1u << 10;
	constexpr int32 Count = 11;
	/** The CSV name of tag bit I (0-based): Opener, Anchor, ... Captain. */
	const TCHAR* Name(int32 I);
	/** "Opener|Captain" -> the bits; unknown names are ignored. */
	uint32 Parse(const FString& Pipe);
}

struct FAuctionStats
{
	int32 Matches = 0, Runs = 0, Wickets = 0;
	float StrikeRate = 0.f, Economy = 0.f;
};

struct FAuctionPlayer
{
	int32 Id = INDEX_NONE;
	FString Name, Short, Country, BowlStyle;
	EAuctionRole Role = EAuctionRole::Batter;
	bool bLeftBat = false;
	int32 Age = 0;        // on 2026-09-28, the 2027 season's roster date
	bool bCapped = true;
	FString Team2026;     // franchise code of the IPL 2026 squad, empty if unattached
	int32 Price2026 = 0;  // lakh
	FAuctionStats Ipl, T20, Last; // IPL career, every other T20, and the last IPL season (empty without Cricsheet)
	int32 Base = 30;      // registered base price, lakh
	int32 BatRating = 0, BowlRating = 0; // 0..99
	uint32 Tags = 0;      // AuctionTags
	bool bRetained2026 = false;

	bool IsOverseas() const { return Country != TEXT("India"); }
	bool Has(uint32 Tag) const { return (Tags & Tag) != 0; }
	/** Bowls seam or pace (a quick, or an all-rounder who bowls fast or medium). */
	bool BowlsPace() const;
	/** Bowls spin. */
	bool BowlsSpin() const;
	/** One 0..99 number for cards and valuation: the player's main skill, with an all-rounder's second one added. */
	int32 Overall() const;
};

struct FAuctionFranchise
{
	FString Code, Name, Short, City, Home, Owner, Captain, Coach;
	FLinearColor Primary, Secondary;
	int32 Titles = 0;
	// How this front office bids (AI): how far past its valuation it will go, what it prizes, how early it enters,
	// how much of its purse it spends on the marquee (Tempo), and how often it bids only to cost a rival (Enforcer).
	float Aggression = 1.f, StarBias = 1.f, YouthBias = 1.f, OverseasBias = 1.f, Loyalty = 1.f, Patience = 0.5f;
	float Tempo = 1.f, Enforcer = 0.3f;
	float RoleBias[int32(EAuctionRole::Count)] = { 1.f, 1.f, 1.f, 1.f, 1.f };
};

/** A player on a franchise's books: bought, retained, kept on his contract (mini auction) or brought back by RTM. */
struct FAuctionSigning
{
	int32 Player = INDEX_NONE;
	int32 Price = 0;   // what the purse paid, lakh
	int32 Fee = 0;     // what the player is paid: the price, except an overseas buy above the mini-auction cap
	bool bRetained = false, bRtm = false;
	int32 Worth = 0;   // what the room expected him to fetch when the hammer fell (retained: his opening estimate)
};

enum class EAuctionMode : uint8 { Mega, Mini };
enum class EAuctionDifficulty : uint8 { Casual, Pro, Legend };

/** How an auction is set up. The defaults are a mega auction for the 2027 squads with one human-free room. */
struct FAuctionConfig
{
	EAuctionMode Mode = EAuctionMode::Mega;
	EAuctionDifficulty Difficulty = EAuctionDifficulty::Pro;
	int32 Season = 2027;             // the IPL season the squads are for; ages count on from the roster date
	int32 Purse = 12000;             // lakh: a mega auction's purse, a mini auction's salary cap
	TArray<int32> Humans;            // franchise indices at the tables of people (pass-the-paddle), none for all AI
	bool bNoRetentions = false;      // mega: everyone goes back into the pool
	/** Career: each franchise's squad from the season just played (player, contract), in franchise order. Empty: the
	 *  2026 squads from the roster. */
	TArray<TArray<FAuctionSigning>> Carried;
};

namespace AuctionRules
{
	constexpr int32 Purse = 12000;        // 120 crore
	constexpr int32 SquadMin = 18, SquadMax = 25, OverseasMax = 8;
	constexpr int32 KeepMax = 6, KeepCappedMax = 5, KeepUncappedMax = 2; // mega: retentions plus RTM cards
	constexpr int32 UncappedRetention = 400;
	constexpr int32 MinBase = 30;
	constexpr int32 XiOverseasMax = 4, Side = 12; // four overseas in a playing eleven; eleven and the Impact Player
	constexpr int32 OverseasFeeCap = 1800; // mini auction: an overseas buy is paid at most 18 Cr, the rest goes to the board
	constexpr int32 IplRecord = 2700;      // Rishabh Pant, 2025 mega auction
	constexpr int32 FirstMega = 2025, MegaCycle = 3;

	/** Purse cost of the Nth (0-based) capped retention slab: 18, 14, 11, 18, 14 crore. */
	int32 CappedRetentionCost(int32 Index);
	/** The next bid above Current: 5 lakh up to 1 crore, 10 to 2 crore, 20 to 5 crore, 25 above. */
	int32 NextBid(int32 Current);
	/** The first price on the ladder from From that is at least Amount (a jump bid lands on the ladder). */
	int32 OnLadder(int32 Amount, int32 From);
	/** "₹2.40 Cr" / "₹75 L" as the broadcast shows it. */
	FString Money(int32 Lakh);
	/** "2 crore 40" / "75 lakh" as the auctioneer says it. */
	FString Spoken(int32 Lakh);
	const TCHAR* RoleName(EAuctionRole Role);  // BATTER, WICKETKEEPER, ALL-ROUNDER, FAST BOWLER, SPINNER
	const TCHAR* RoleCode(EAuctionRole Role);  // BA, WK, AL, FA, SP (set names)
	/** Mega auctions come every three years from 2025 (2028, 2031); the seasons between have a mini auction. */
	bool IsMegaSeason(int32 Season);
	/** What the player is paid for a price: capped for an overseas player bought in a mini auction. */
	int32 Fee(EAuctionMode Mode, bool bOverseas, int32 Price);
}

namespace AuctionData
{
	/** The ten franchises in a fixed order (CSK, MI, RCB, KKR, SRH, DC, PBKS, RR, GT, LSG), from Teams.csv. */
	const TArray<FAuctionFranchise>& Franchises();
	int32 FranchiseIndex(const FString& Code);
	/** Every player in the database, Id = index. Parsed once from the roster compiled into the game. */
	const TArray<FAuctionPlayer>& Players();
	/** Parses the roster CSV (header row first); exposed for tests. */
	TArray<FAuctionPlayer> ParseCsv(const FString& Csv);
	/** Parses the franchise CSV and gives each its front office's style; exposed for tests. */
	TArray<FAuctionFranchise> ParseTeams(const FString& Csv);
}
