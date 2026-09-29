// The IPL mega auction as a state machine: retentions, the sets (two marquee sets, then capped and uncapped sets
// by role, then the accelerated rounds), live bidding on the real increment ladder, the Right to Match with its
// final raise, and nine AI front offices that value every player against their own squad, purse and style.
//
// Time only moves in Tick, so the same rules run a live lot at broadcast pace and a whole simulated auction in a
// test. Everything that happens is appended to Events; the room, HUD and auctioneer voice replay them.

#pragma once

#include "CoreMinimal.h"
#include "AuctionTypes.h"

enum class EAuctionPhase : uint8
{
	Retention, // before BeginAuction
	SetIntro,  // a new set is announced
	LotIntro,  // the player is presented on the big screen
	Bidding,
	RtmAsk,    // the hammer fell; the player's 2026 franchise is asked about its Right to Match
	RtmRaise,  // RTM used: the highest bidder gets one final raise
	RtmMatch,  // the RTM side matches that final bid or lets him go
	Hammer,    // sold / unsold card on screen
	Finished
};

enum class EAuctionEvent : uint8
{
	SetOpened, LotOpened, OpeningCall, Bid, Huddle, Out, GoingOnce, GoingTwice, Sold, Unsold,
	RtmOffered, RtmUsed, RtmDeclined, FinalRaise, RtmMatched, RtmNotMatched, Accelerated, Finished
};

struct FAuctionEventRecord
{
	EAuctionEvent Type = EAuctionEvent::Bid;
	int32 Team = INDEX_NONE;
	int32 Player = INDEX_NONE;
	int32 Amount = 0; // lakh
	double Time = 0.0;
	int32 Other = INDEX_NONE; // in a right-to-match exchange, the side facing Team (the buyer, or the side with the card)
};

struct FAuctionSigning
{
	int32 Player = INDEX_NONE;
	int32 Price = 0;
	bool bRetained = false, bRtm = false;
};

struct FAuctionTeam
{
	int32 Purse = AuctionRules::Purse;
	TArray<FAuctionSigning> Squad;
	int32 RtmCards = 0, KeptCapped = 0, KeptUncapped = 0; // retentions plus RTMs used, against the 5 + 2 limit
	bool bRetained = false;

	int32 Overseas() const;
	int32 CountRole(EAuctionRole Role) const;
};

struct FAuctionSet
{
	FString Code; // M1, BA1, UFA2, AR (accelerated), UR (unsold round)
	FString Name; // MARQUEE SET 1, CAPPED BATTERS 1 ...
	TArray<int32> Players;
	bool bAccelerated = false;
};

class FAuction
{
public:
	/** HumanTeam is a franchise index, or INDEX_NONE for ten AI sides. */
	explicit FAuction(int32 HumanTeam = INDEX_NONE, int32 Seed = 2026);

	// ---- Retentions (before BeginAuction) --------------------------------------------------------------------
	/** Players of this franchise's 2026 squad, best first. */
	TArray<int32> RetentionCandidates(int32 Team) const;
	/** Validates a retention list: 2026 squad members, at most 6, at most 5 capped and 2 uncapped. */
	bool CanRetain(int32 Team, const TArray<int32>& Players, FString* Why = nullptr) const;
	/** Purse cost of keeping these players in this order (capped slabs 18/14/11/18/14 Cr, uncapped 4 Cr). */
	static int32 RetentionCost(const TArray<int32>& Players);
	/** What an AI front office would keep. */
	TArray<int32> AiRetentions(int32 Team) const;
	bool Retain(int32 Team, const TArray<int32>& Players);
	/** Retains for every AI side that has not, builds the pool and sets, and opens the first set. */
	void BeginAuction();

	// ---- Live ------------------------------------------------------------------------------------------------
	void Tick(float Dt);

	/** The human raises (or opens at the base price). False if not allowed now. */
	bool HumanBid();
	bool CanHumanBid() const;
	/** The human's Right to Match answer (RtmAsk), the final raise (RtmRaise, Amount = current price for none)
	 *  and the match answer (RtmMatch). */
	void HumanRtm(bool bUse);
	void HumanFinalRaise(int32 Amount);
	void HumanMatch(bool bMatch);
	bool AwaitingHuman() const;
	bool bNoRetentions = false;
	/** Resolves the current lot (or the whole set) at once, the human passing; RTM questions to the human still wait. */
	void SkipLot();
	void SkipSet();
	/** Fast-resolves the current lot instantly until Hammer (Sold or Unsold) is reached. */
	void FastResolveCurrentLot();
	/** Accelerated rounds: the human's nominations (players the human wants brought back to the table). */
	void Nominate(int32 Player, bool bOn);
	bool IsNominated(int32 Player) const { return HumanNominations.Contains(Player); }

	// ---- State -----------------------------------------------------------------------------------------------
	EAuctionPhase Phase = EAuctionPhase::Retention;
	double Clock = 0.0;
	double PhaseStart = 0.0;
	TArray<FAuctionTeam> Teams;
	TArray<FAuctionSet> Sets;
	int32 SetIndex = 0, LotInSet = -1, LotsHeld = 0;
	int32 Lot = INDEX_NONE;      // player under the hammer
	int32 Price = 0;             // current bid, 0 before the first paddle
	int32 Holder = INDEX_NONE;   // team holding the bid
	int32 RtmTeam = INDEX_NONE;
	int32 HammerStage = 0;       // 1 going once, 2 going twice
	int32 LastSoldTo = INDEX_NONE; // after Sold: buyer (Hammer phase), INDEX_NONE if unsold
	TArray<FAuctionEventRecord> Events;
	TArray<int32> Unsold;
	TMap<int32, int32> SoldTo;   // player -> team
	const int32 Human;

	bool IsHuman(int32 Team) const { return Team != INDEX_NONE && Team == Human; }
	const FAuctionSet* CurrentSet() const { return Sets.IsValidIndex(SetIndex) ? &Sets[SetIndex] : nullptr; }
	/** Can this side pay Amount for the player and still fill 18 at base price, with a squad and overseas slot. */
	bool CanAfford(int32 Team, int32 Player, int32 Amount) const;
	/** The next bid the auctioneer will take. */
	int32 AskPrice() const { return Price == 0 ? Player(Lot).Base : AuctionRules::NextBid(Price); }
	/** An AI side's walk-away price for a player, now (0 if it does not want him). */
	int32 Valuation(int32 Team, int32 Player) const;
	/** The purse left after keeping the minimum squad fillable. */
	int32 MaxBid(int32 Team) const;
	int32 TeamsBiddingThisLot() const { return Bidders.Num(); }
	/** Seconds the current phase has run. */
	float PhaseTime() const { return float(Clock - PhaseStart); }
	bool IsFast() const { return bFastLot || bFastSet; }

	static const FAuctionPlayer& Player(int32 Id);

	// Pace, seconds. Accelerated lots run at AccelPace of these.
	static constexpr float SetIntroTime = 4.f, LotIntroTime = 4.5f, OpeningTime = 2.2f, NoBidTime = 6.f,
		OnceAfter = 2.8f, TwiceAfter = 1.8f, SoldAfter = 1.8f, HammerTime = 4.2f, AiRtmThink = 2.5f, HumanRtmTimeout = 25.f,
		AccelPace = 0.45f;

private:
	FRandomStream Rng;
	int32 Seed;
	TArray<int32> MaxThisLot, EnterAt; // per team, for the lot under the hammer
	TSet<int32> Bidders;               // teams that have bid on this lot
	TSet<int32> OutCalled;
	TSet<int32> HumanNominations;
	int32 PendingTeam = INDEX_NONE;
	double PendingAt = 0.0, LastBidAt = 0.0;
	bool bFastLot = false, bFastSet = false;
	int32 UnsoldRounds = 0;

	void Emit(EAuctionEvent Type, int32 Team = INDEX_NONE, int32 Amount = 0);
	void SetPhase(EAuctionPhase P);
	float Pace() const;
	void BuildSets();
	void NextLot();
	bool Nominated(int32 PlayerId) const; // accelerated: does anyone want this player on the table
	void OpenLot(int32 PlayerId);
	void TickBidding();
	void PlaceBid(int32 Team, int32 Amount);
	void ScheduleAi();
	void Hammer();
	void Sell(int32 Team, int32 Amount, bool bRtm);
	void AddSigning(int32 Team, int32 PlayerId, int32 Amount, bool bRetained, bool bRtm);
	bool RtmEligible(int32 Team) const;
	int32 AiFinalRaise(int32 Team) const;
	float Need(int32 Team, EAuctionRole Role) const;
	float Noise(int32 Team, int32 PlayerId) const;
	int32 BaseValue(int32 PlayerId) const;
};
