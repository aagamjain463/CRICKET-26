// The IPL auction as a state machine: the trade window and retentions (mega) or releases (mini), the sets (two marquee
// sets, then capped and uncapped sets by role, then the accelerated rounds), two auction days, live bidding on the real
// increment ladder, the Right to Match with its final raise, and nine (or fewer, with people at the tables) AI front
// offices. Each AI side values a player by what he adds to its best eleven plus Impact Player, prices that against
// what the rest of the room can still spend, goes in with a plan, and bids like a person: it warms up in a duel,
// stalls at round numbers, pays more for the next keeper after losing one, sometimes bids only to cost a rival, and
// sits out a player it can bring back with its Right to Match card (AuctionAI.cpp).
//
// Time only moves in Tick, so the same rules run a live lot at broadcast pace and a whole simulated auction in a
// test. Everything that happens is appended to Events; the room, HUD and auctioneer voice replay them.

#pragma once

#include "CoreMinimal.h"
#include "AuctionTypes.h"

enum class EAuctionPhase : uint8
{
	Retention, // before BeginAuction: trades, then retentions (mega) or releases (mini)
	SetIntro,  // a new set is announced
	LotIntro,  // the player is presented on the big screen
	Bidding,
	RtmAsk,    // the hammer fell; the player's former franchise is asked about its Right to Match
	RtmRaise,  // RTM used: the highest bidder gets one final raise
	RtmMatch,  // the RTM side matches that final bid or lets him go
	Hammer,    // sold / unsold card on screen
	Break,     // the end of day one: the hall empties until day two
	Finished
};

enum class EAuctionEvent : uint8
{
	SetOpened, LotOpened, OpeningCall, Bid, Huddle, Out, GoingOnce, GoingTwice, Sold, Unsold,
	RtmOffered, RtmUsed, RtmDeclined, FinalRaise, RtmMatched, RtmNotMatched, Accelerated, Finished,
	Timeout,   // a table asks for a moment (Team)
	DayEnded, DayStarted,
	FeeCapped  // mini auction: an overseas buy above the cap; Amount is what he is paid
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

struct FAuctionTeam
{
	int32 Purse = AuctionRules::Purse;
	TArray<FAuctionSigning> Squad;
	int32 RtmCards = 0, KeptCapped = 0, KeptUncapped = 0; // retentions plus RTMs used, against the 5 + 2 limit
	int32 Timeouts = 0; // a human table's timeouts left
	bool bRetained = false;

	int32 Overseas() const;
	int32 CountRole(EAuctionRole Role) const;
};

struct FAuctionSet
{
	FString Code; // M1, BA1, UFA2, UR (accelerated round of the unsold)
	FString Name; // MARQUEE SET 1, CAPPED BATTERS 1 ...
	TArray<int32> Players;
	bool bAccelerated = false;
};

/** Where a player fits in a side, for plans, needs and the war room. */
enum class EAuctionSlot : uint8 { Keeper, Opener, MiddleOrder, Finisher, AllRounder, NewBall, Death, Spin, Count };

/** A human's shortlist entry: the most he will pay, and whether the paddle goes up for him on its own. */
struct FAuctionWish
{
	int32 Max = 0;
	bool bAuto = false;
};

/** One of an AI side's pre-auction targets. Tier 0 is the first choice for its slot. */
struct FAuctionTarget
{
	int32 Player = INDEX_NONE;
	EAuctionSlot Slot = EAuctionSlot::MiddleOrder;
	int32 Tier = 0;
	int32 Budget = 0; // lakh it expects to pay
};

struct FAuctionPlan
{
	TArray<FAuctionTarget> Targets;
	float SlotBoost[int32(EAuctionSlot::Count)] = {}; // raised when a first-choice target at the slot goes elsewhere
};

/** A side as its analysts see it: its best eleven plus Impact Player, what is missing, and the money per place. */
struct FAuctionNeeds
{
	float Strength = 0.f;         // 0..100, the best twelve's quality
	int32 Rank = 0;               // 1 = the strongest of the ten
	TArray<int32> BestXI;         // the twelve
	TArray<FString> Holes;        // "No keeper", "One opener"...
	TArray<EAuctionSlot> OpenSlots;
	int32 Places = 0;             // squad places left (to 25)
	int32 ToMinimum = 0;          // places still needed to reach 18
	int32 OverseasLeft = 0;
	int32 PerPlace = 0;           // purse per place still to fill up to 22
	TArray<int32> Targets;        // an AI side's likeliest remaining targets (the rival radar)
};

/** The analysts' verdict on a finished squad. */
struct FAuctionVerdict
{
	FString Grade;                // A+, A, B+, B, C, D
	float Score = 0.f;            // 0..100
	FAuctionNeeds Needs;
	TArray<int32> Steals, Splurges; // players bought well below, or well above, what the room thought they were worth
	FString Summary;
};

struct FAuctionRecord
{
	FString Title;
	int32 Player = INDEX_NONE, Team = INDEX_NONE, Price = 0;
};

/** A swap in the trade window: From gave Gave to To and received Got. */
struct FAuctionTrade
{
	int32 From = INDEX_NONE, To = INDEX_NONE, Gave = INDEX_NONE, Got = INDEX_NONE;
};

class FAuction
{
public:
	/** HumanTeam is a franchise index, or INDEX_NONE for ten AI sides: a mega auction for 2027 at Pro difficulty. */
	explicit FAuction(int32 HumanTeam = INDEX_NONE, int32 Seed = 2026);
	FAuction(const FAuctionConfig& InConfig, int32 Seed);

	const FAuctionConfig Config;
	bool IsMini() const { return Config.Mode == EAuctionMode::Mini; }

	// ---- Trade window (Retention phase) ----------------------------------------------------------------------
	/** Offers From's player Give for To's player Get. An AI side takes a trade that improves it; a human side's
	 *  consent is the offer itself (hot seat). False, with Why, if refused or not allowed. */
	bool ProposeTrade(int32 From, int32 Give, int32 To, int32 Get, FString* Why = nullptr);
	/** The AI sides trade among themselves (a few swaps that suit both); runs once, from BeginAuction if not before. */
	void OpenTradeWindow();
	TArray<FAuctionTrade> Trades;
	/** The franchise a player belongs to before the auction (his 2026 or carried side, after trades), or INDEX_NONE. */
	int32 OwnerOf(int32 PlayerId) const { return Owner.IsValidIndex(PlayerId) ? Owner[PlayerId] : INDEX_NONE; }
	/** What he is contracted at with that side (lakh). */
	int32 ContractOf(int32 PlayerId) const { return Contract.IsValidIndex(PlayerId) ? Contract[PlayerId] : 0; }

	// ---- Retentions (mega) or releases (mini), before BeginAuction ---------------------------------------------
	/** Players of this franchise's squad, best first. */
	TArray<int32> RetentionCandidates(int32 Team) const;
	/** Mega: 2026 (or carried) squad members, at most 6, at most 5 capped and 2 uncapped, none who wants the auction.
	 *  Mini: any of the squad, within 25 players, 8 overseas and the purse. */
	bool CanRetain(int32 Team, const TArray<int32>& Players, FString* Why = nullptr) const;
	/** Purse cost of keeping each of these players. Mega: the capped slabs (18/14/11/18/14 Cr) are handed out so the
	 *  total is least, and a player whose market is above his slab costs what he asks; uncapped 4 Cr or his ask.
	 *  Mini: his contract. */
	TArray<int32> RetentionCosts(const TArray<int32>& Players) const;
	int32 RetentionCost(const TArray<int32>& Players) const;
	/** What a player would fetch in the opening market, with every purse full (orders the sets, prices retentions). */
	int32 MarketEstimate(int32 PlayerId) const;
	/** Mega: what a player asks to be retained (his cost is the larger of this and his slab). */
	int32 RetentionAsk(int32 PlayerId) const;
	/** Mega: a star who will not re-sign and goes into the auction (Pant, Iyer and Rahul in 2025). */
	bool WantsAuction(int32 PlayerId) const;
	/** What an AI front office would keep. */
	TArray<int32> AiRetentions(int32 Team) const;
	bool Retain(int32 Team, const TArray<int32>& Players);
	/** Retains for every side that has not, builds the pool, the AI plans and the sets, and opens the first set. */
	void BeginAuction();
	bool bNoRetentions = false;

	// ---- Live ------------------------------------------------------------------------------------------------
	void Tick(float Dt);

	/** A human table raises (or opens at the base price). False if not allowed now. No team: the first human. */
	bool HumanBid(int32 Team = INDEX_NONE);
	bool CanHumanBid(int32 Team = INDEX_NONE) const;
	/** A jump: straight to Amount (on the ladder, at least the asking price). */
	bool HumanJumpBid(int32 Team, int32 Amount);
	/** "May we have a moment?": the hammer waits TimeoutTime for this table. Three per human table for the auction. */
	bool RequestTimeout(int32 Team);
	bool CanRequestTimeout(int32 Team) const;
	/** The human's Right to Match answer (RtmAsk), the final raise (RtmRaise, Amount = current price for none)
	 *  and the match answer (RtmMatch), for whichever table is being asked. */
	void HumanRtm(bool bUse);
	void HumanFinalRaise(int32 Amount);
	void HumanMatch(bool bMatch);
	bool AwaitingHuman() const;
	/** Resolves the current lot (or the whole set) at once, the humans passing (auto-bids still bid); RTM questions to
	 *  a human still wait. */
	void SkipLot();
	void SkipSet();
	/** Fast-resolves the current lot instantly until Hammer (Sold or Unsold) is reached. */
	void FastResolveCurrentLot();
	/** Accelerated rounds: the human's nominations (players the human wants brought back to the table). */
	void Nominate(int32 PlayerId, bool bOn);
	bool IsNominated(int32 PlayerId) const { return HumanNominations.Contains(PlayerId); }
	/** A human's shortlist: Max 0 removes him. An auto entry bids for the table up to Max. */
	void SetWish(int32 Team, int32 PlayerId, int32 Max, bool bAuto);
	const FAuctionWish* WishFor(int32 Team, int32 PlayerId) const;
	const TMap<int32, FAuctionWish>& Wishes(int32 Team) const;

	// ---- The war room ----------------------------------------------------------------------------------------
	FAuctionNeeds Needs(int32 Team) const;
	/** What an analyst leans over to say about the lot under the hammer, for this table. */
	FString Whisper(int32 Team) const;
	/** The price the room expects for a player now (lakh): the market's consensus, not any one side's. */
	int32 ExpectedPrice(int32 PlayerId) const;
	/** Lots from now until this player is presented (0 on the block, -1 sold, gone or not in a set). */
	int32 LotsUntil(int32 PlayerId) const;
	EAuctionSlot SlotOf(int32 PlayerId) const;
	static const TCHAR* SlotName(EAuctionSlot Slot);
	const FAuctionPlan& PlanOf(int32 Team) const { return Plans[Team]; }
	/** After the auction: every side's grade, steals and splurges. */
	FAuctionVerdict Verdict(int32 Team) const;
	/** The auction's records so far: most expensive, by role, uncapped, overseas. */
	TArray<FAuctionRecord> Records() const;

	// ---- Save and resume -------------------------------------------------------------------------------------
	/** The whole auction as text. Written between lots; resuming presents the next lot. */
	FString SaveState() const;
	/** Restores SaveState into an auction built with the same config and seed (ReadSaveHeader gives both). */
	bool LoadState(const FString& Text);
	static bool ReadSaveHeader(const FString& Text, FAuctionConfig& OutConfig, int32& OutSeed);

	// ---- State -----------------------------------------------------------------------------------------------
	EAuctionPhase Phase = EAuctionPhase::Retention;
	double Clock = 0.0;
	double PhaseStart = 0.0;
	TArray<FAuctionTeam> Teams;
	TArray<FAuctionSet> Sets;
	int32 SetIndex = 0, LotInSet = -1, LotsHeld = 0;
	int32 Day = 1;
	int32 Lot = INDEX_NONE;      // player under the hammer
	int32 Price = 0;             // current bid, 0 before the first paddle
	int32 Holder = INDEX_NONE;   // team holding the bid
	int32 RtmTeam = INDEX_NONE;
	int32 HammerStage = 0;       // 1 going once, 2 going twice
	int32 LastSoldTo = INDEX_NONE; // after Sold: buyer (Hammer phase), INDEX_NONE if unsold
	TArray<FAuctionEventRecord> Events;
	TArray<int32> Unsold;
	TMap<int32, int32> SoldTo;   // player -> team
	const int32 Human;           // the first human table, INDEX_NONE for none
	const TArray<int32> Humans;

	bool IsHuman(int32 Team) const { return Team != INDEX_NONE && Humans.Contains(Team); }
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
	/** The player's age in the auction's season. */
	int32 AgeOf(int32 PlayerId) const;

	static const FAuctionPlayer& Player(int32 Id);

	// Pace, seconds. Accelerated lots run at AccelPace of these.
	static constexpr float SetIntroTime = 4.f, LotIntroTime = 4.5f, OpeningTime = 2.2f, NoBidTime = 6.f,
		OnceAfter = 2.8f, TwiceAfter = 1.8f, SoldAfter = 1.8f, HammerTime = 4.2f, AiRtmThink = 2.5f, HumanRtmTimeout = 25.f,
		AccelPace = 0.45f, TimeoutTime = 20.f, BreakTime = 6.f;
	static constexpr int32 TimeoutsPerTable = 3;

private:
	FRandomStream Rng;
	int32 Seed;
	TArray<int32> Owner, Contract;     // per player, before the auction
	TArray<int32> FormerTeam;          // per player: the side holding his Right to Match (mega), INDEX_NONE if none
	TArray<int32> MaxThisLot, EnterAt, StartMax; // per team, for the lot under the hammer
	TArray<int32> BidsThisLot;         // per team
	TSet<int32> Bidders;               // teams that have bid on this lot
	TSet<int32> OutCalled;
	TSet<int32> Enforcing;             // sides bidding on this lot only to cost a rival
	TSet<int32> HumanNominations;
	TArray<TMap<int32, FAuctionWish>> WishList; // per team
	TArray<FAuctionPlan> Plans;        // per team (AI)
	TSet<int32> TimeoutThisLot;
	int32 PendingTeam = INDEX_NONE;
	double PendingAt = 0.0, LastBidAt = 0.0;
	bool bFastLot = false, bFastSet = false, bTradeWindowDone = false;
	int32 UnsoldRounds = 0;
	int32 FirstDayTwoSet = INDEX_NONE;

	// The market (AuctionAI.cpp): each player's skill over the contract's horizon, and the room's money.
	TArray<float> BatNow, BowlNow;     // per player, the ratings the auction values (age-adjusted, horizon-averaged)
	float ReplIndian = 60.f, ReplOverseas = 60.f; // the rating a side can always find in the pool for the base price
	float LakhPerPoint = 10.f;         // what the room pays for a point of value over replacement
	TArray<int32> Estimate;            // per player, what he would fetch in the opening market
	mutable TArray<float> TeamValueCache; // per team, -1 when stale

	void Emit(EAuctionEvent Type, int32 Team = INDEX_NONE, int32 Amount = 0);
	void SetPhase(EAuctionPhase P);
	float Pace() const;
	void Init();
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
	bool AiUsesRtm(int32 Team) const; // an AI side with the card: is he worth one of them at this price

	// ---- AuctionAI.cpp ---------------------------------------------------------------------------------------
	void InitMarket();
	void UpdateMarket();
	TArray<int32> Pool() const;         // players still to be sold or passed over
	int32 Aim() const;                  // the squad size a side sets out to reach
	float Contribution(float Rating, bool bOverseas) const;
	void PriceMarket(const TArray<int32>& Left, int32 IndianDemand, int32 OverseasDemand, float Money, float Kappa);
	float GenericValue(int32 PlayerId) const;
	/** How good a squad is: its best twelve (with the composition a T20 side needs) and its depth. */
	float SquadValue(const TArray<int32>& Squad, TArray<int32>* OutBest = nullptr, TArray<FString>* OutHoles = nullptr) const;
	float TeamValue(int32 Team) const;
	TArray<int32> SquadIds(int32 Team) const;
	float Marginal(int32 Team, int32 PlayerId) const;
	float StyleFactor(int32 Team, int32 PlayerId) const;
	float Noise(int32 Team, int32 PlayerId) const;
	float Scarcity(int32 Team, int32 PlayerId) const;
	float PlanFactor(int32 Team, int32 PlayerId) const;
	void BuildPlans();
	void OnSold(int32 Team, int32 PlayerId);
	void PlanLot(); // enforcers and RTM sit-outs for the lot just opened
	int32 EstimateCeiling(int32 Team, int32 PlayerId) const; // what a rival would pay, as another side guesses it
	float TradeValue(int32 Team, int32 Out, int32 In) const; // what swapping Out for In does for Team (points)
	float RetentionWorth(int32 Team, int32 PlayerId) const;
	void Invalidate(int32 Team) { if (TeamValueCache.IsValidIndex(Team)) TeamValueCache[Team] = -1.f; }
};
