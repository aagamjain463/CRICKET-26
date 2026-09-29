// IPL mega auction: the players, the ten franchises and the money rules. Money is always in lakh
// (100 lakh = 1 crore), which is how the auctioneer counts it. Plain C++: the engine and its tests need no
// UObjects, and the 3D room and HUD only read it.

#pragma once

#include "CoreMinimal.h"

enum class EAuctionRole : uint8 { Batter, Keeper, AllRounder, Pace, Spin, Count };

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
	int32 Age = 0;
	bool bCapped = true;
	FString Team2026;     // franchise code of the IPL 2026 squad, empty if unattached
	int32 Price2026 = 0;  // lakh
	FAuctionStats Ipl, T20;
	int32 Base = 30;      // registered base price, lakh
	int32 BatRating = 0, BowlRating = 0; // 0..99
	bool bRetained2026 = false;

	bool IsOverseas() const { return Country != TEXT("India"); }
	/** One 0..99 number for cards and valuation: the player's main skill, with an all-rounder's second one added. */
	int32 Overall() const;
};

struct FAuctionFranchise
{
	FString Code, Name, Short, City, Home, Owner, Captain, Coach;
	FLinearColor Primary, Secondary;
	int32 Titles = 0;
	// How this front office bids (AI): how far past its valuation it will go, what it prizes, how early it enters.
	float Aggression = 1.f, StarBias = 1.f, YouthBias = 1.f, OverseasBias = 1.f, Loyalty = 1.f, Patience = 0.5f;
	float RoleBias[int32(EAuctionRole::Count)] = { 1.f, 1.f, 1.f, 1.f, 1.f };
};

namespace AuctionRules
{
	constexpr int32 Purse = 12000;        // 120 crore
	constexpr int32 SquadMin = 18, SquadMax = 25, OverseasMax = 8;
	constexpr int32 KeepMax = 6, KeepCappedMax = 5, KeepUncappedMax = 2; // retentions plus RTM cards
	constexpr int32 UncappedRetention = 400;
	constexpr int32 MinBase = 30;

	/** Purse cost of the Nth (0-based) capped retention: 18, 14, 11, 18, 14 crore. */
	int32 CappedRetentionCost(int32 Index);
	/** The next bid above Current: 5 lakh up to 1 crore, 10 to 2 crore, 20 to 5 crore, 25 above. */
	int32 NextBid(int32 Current);
	/** "₹2.40 Cr" / "₹75 L" as the broadcast shows it. */
	FString Money(int32 Lakh);
	/** "2 crore 40" / "75 lakh" as the auctioneer says it. */
	FString Spoken(int32 Lakh);
	const TCHAR* RoleName(EAuctionRole Role);  // BATTER, WICKETKEEPER, ALL-ROUNDER, FAST BOWLER, SPINNER
	const TCHAR* RoleCode(EAuctionRole Role);  // BA, WK, AL, FA, SP (set names)
}

namespace AuctionData
{
	/** The ten franchises in a fixed order (CSK, MI, RCB, KKR, SRH, DC, PBKS, RR, GT, LSG). */
	const TArray<FAuctionFranchise>& Franchises();
	int32 FranchiseIndex(const FString& Code);
	/** Every player in the database, Id = index. Parsed once from the roster compiled into the game. */
	const TArray<FAuctionPlayer>& Players();
	/** Parses the roster CSV (header row first); exposed for tests. */
	TArray<FAuctionPlayer> ParseCsv(const FString& Csv);
}
