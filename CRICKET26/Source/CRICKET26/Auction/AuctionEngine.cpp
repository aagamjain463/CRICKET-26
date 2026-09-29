#include "AuctionEngine.h"

namespace AuctionEnginePrivate
{
	constexpr int32 CappedSetSize = 8, UncappedSetSize = 10, MarqueeSize = 6, MaxUnsoldRounds = 3;
	// The IPL draws each round of sets in this order: batters, all-rounders, wicketkeepers, fast bowlers, spinners.
	constexpr EAuctionRole SetOrder[] = { EAuctionRole::Batter, EAuctionRole::AllRounder, EAuctionRole::Keeper, EAuctionRole::Pace, EAuctionRole::Spin };
	// Round crore figures a table stops and thinks at. Not ten: 2025 sold a run of players at 10.75 to 12.5 crore.
	constexpr int32 StickingPoints[] = { 1500, 2000, 2500, 3000 };

	const TCHAR* SetNoun(EAuctionRole Role)
	{
		static const TCHAR* Nouns[] = { TEXT("BATTERS"), TEXT("WICKETKEEPERS"), TEXT("ALL-ROUNDERS"), TEXT("FAST BOWLERS"), TEXT("SPINNERS") };
		return Nouns[int32(Role)];
	}

	FAuctionConfig ConfigFor(int32 HumanTeam)
	{
		FAuctionConfig C;
		if (HumanTeam != INDEX_NONE) C.Humans.Add(HumanTeam);
		return C;
	}

	int32 FirstOf(const TArray<int32>& A) { return A.IsEmpty() ? INDEX_NONE : A[0]; }
}

int32 FAuctionTeam::Overseas() const
{
	int32 N = 0;
	for (const FAuctionSigning& S : Squad) N += FAuction::Player(S.Player).IsOverseas();
	return N;
}

int32 FAuctionTeam::CountRole(EAuctionRole Role) const
{
	int32 N = 0;
	for (const FAuctionSigning& S : Squad) N += FAuction::Player(S.Player).Role == Role;
	return N;
}

const FAuctionPlayer& FAuction::Player(int32 Id)
{
	static const FAuctionPlayer None;
	const TArray<FAuctionPlayer>& All = AuctionData::Players();
	return All.IsValidIndex(Id) ? All[Id] : None;
}

FAuction::FAuction(int32 HumanTeam, int32 InSeed) : FAuction(AuctionEnginePrivate::ConfigFor(HumanTeam), InSeed)
{
}

FAuction::FAuction(const FAuctionConfig& InConfig, int32 InSeed)
	: Config(InConfig), bNoRetentions(InConfig.bNoRetentions), Human(AuctionEnginePrivate::FirstOf(InConfig.Humans)), Humans(InConfig.Humans),
	Rng(InSeed), Seed(InSeed)
{
	Init();
}

void FAuction::Init()
{
	const int32 N = AuctionData::Franchises().Num(), P = AuctionData::Players().Num();
	Teams.SetNum(N);
	for (int32 T = 0; T < N; ++T)
	{
		Teams[T].Purse = Config.Purse;
		Teams[T].Timeouts = IsHuman(T) ? TimeoutsPerTable : 0;
	}
	MaxThisLot.Init(0, N);
	EnterAt.Init(0, N);
	StartMax.Init(0, N);
	BidsThisLot.Init(0, N);
	WishList.SetNum(N);
	Plans.SetNum(N);
	TeamValueCache.Init(-1.f, N);
	// Who each player belongs to before the auction: the squads carried from the last season, or the 2026 squads.
	Owner.Init(INDEX_NONE, P);
	Contract.Init(0, P);
	FormerTeam.Init(INDEX_NONE, P);
	if (Config.Carried.Num() > 0)
	{
		for (int32 T = 0; T < FMath::Min(N, Config.Carried.Num()); ++T)
			for (const FAuctionSigning& S : Config.Carried[T])
				if (Owner.IsValidIndex(S.Player)) { Owner[S.Player] = T; Contract[S.Player] = FMath::Max(S.Price, AuctionRules::MinBase); }
	}
	else
	{
		for (const FAuctionPlayer& X : AuctionData::Players())
		{
			const int32 T = AuctionData::FranchiseIndex(X.Team2026);
			if (T == INDEX_NONE) continue;
			Owner[X.Id] = T;
			Contract[X.Id] = X.Price2026 > 0 ? X.Price2026 : X.Base;
		}
	}
	InitMarket();
}

int32 FAuction::AgeOf(int32 PlayerId) const
{
	const FAuctionPlayer& X = Player(PlayerId);
	return X.Age > 0 ? X.Age + (Config.Season - 2027) : 0;
}

// ---- Trades ------------------------------------------------------------------------------------------------------

bool FAuction::ProposeTrade(int32 From, int32 Give, int32 To, int32 Get, FString* Why)
{
	auto Fail = [Why](const TCHAR* Reason) { if (Why) *Why = Reason; return false; };
	if (Phase != EAuctionPhase::Retention) return Fail(TEXT("the trade window has closed"));
	if (!Teams.IsValidIndex(From) || !Teams.IsValidIndex(To) || From == To) return Fail(TEXT("pick another franchise"));
	if (Teams[From].bRetained || Teams[To].bRetained) return Fail(TEXT("retentions are already locked in"));
	if (OwnerOf(Give) != From || OwnerOf(Get) != To) return Fail(TEXT("each side can only trade its own players"));
	// Overseas limits hold for both squads after the swap.
	auto OverseasAfter = [this](int32 Team, int32 Out, int32 In)
	{
		int32 N = 0;
		for (int32 P = 0; P < Owner.Num(); ++P) N += Owner[P] == Team && P != Out && Player(P).IsOverseas();
		return N + Player(In).IsOverseas();
	};
	if (OverseasAfter(From, Give, Get) > AuctionRules::OverseasMax || OverseasAfter(To, Get, Give) > AuctionRules::OverseasMax)
		return Fail(TEXT("that would break the overseas limit"));
	// An AI side takes a trade that makes it better, all things counted; a human table's offer is its consent.
	if (!IsHuman(To) && TradeValue(To, Get, Give) < 1.5f) return Fail(TEXT("they turned it down"));
	Owner[Give] = To;
	Owner[Get] = From;
	Trades.Add({ From, To, Give, Get });
	return true;
}

void FAuction::OpenTradeWindow()
{
	if (bTradeWindowDone || Phase != EAuctionPhase::Retention) return;
	bTradeWindowDone = true;
	if (bNoRetentions) return;
	// A handful of swaps between AI sides, the kind that suit both (Samson for Jadeja, Nov 2025). Fewer before a mega
	// auction, when most of each squad goes back into the pool anyway.
	const int32 Want = IsMini() ? 3 : 1;
	int32 Done = 0;
	for (int32 Try = 0; Try < 60 && Done < Want; ++Try)
	{
		const int32 A = Rng.RandRange(0, Teams.Num() - 1), B = Rng.RandRange(0, Teams.Num() - 1);
		if (A == B || IsHuman(A) || IsHuman(B)) continue;
		TArray<int32> Mine = RetentionCandidates(A), Theirs = RetentionCandidates(B);
		if (Mine.Num() < 6 || Theirs.Num() < 6) continue;
		// Not their best three: franchises trade squad players, not the faces of the side.
		const int32 Give = Mine[Rng.RandRange(3, Mine.Num() - 1)];
		for (int32 Get : Theirs)
		{
			const float Ratio = float(MarketEstimate(Get)) / FMath::Max(1.f, float(MarketEstimate(Give)));
			if (Ratio < 0.7f || Ratio > 1.4f || Get == Theirs[0] || Get == Theirs[1] || Get == Theirs[2]) continue;
			if (TradeValue(A, Give, Get) < 2.f || TradeValue(B, Get, Give) < 2.f) continue;
			if (ProposeTrade(A, Give, B, Get)) { ++Done; break; }
		}
	}
}

// ---- Retentions --------------------------------------------------------------------------------------------------

TArray<int32> FAuction::RetentionCandidates(int32 Team) const
{
	TArray<int32> Out;
	for (int32 P = 0; P < Owner.Num(); ++P) if (Owner[P] == Team) Out.Add(P);
	Out.Sort([this](int32 A, int32 B) { return MarketEstimate(A) > MarketEstimate(B); });
	return Out;
}

bool FAuction::CanRetain(int32 Team, const TArray<int32>& Players, FString* Why) const
{
	auto Fail = [Why](const TCHAR* Reason) { if (Why) *Why = Reason; return false; };
	if (!Teams.IsValidIndex(Team)) return Fail(TEXT("no such franchise"));
	if (Teams[Team].bRetained) return Fail(TEXT("retentions are already locked in"));
	for (int32 I = 0; I < Players.Num(); ++I)
	{
		if (OwnerOf(Players[I]) != Team) return Fail(TEXT("only players from your squad can be retained"));
		for (int32 J = 0; J < I; ++J) if (Players[J] == Players[I]) return Fail(TEXT("a player is listed twice"));
	}
	if (IsMini())
	{
		int32 Overseas = 0;
		for (int32 P : Players) Overseas += Player(P).IsOverseas();
		if (Players.Num() > AuctionRules::SquadMax) return Fail(TEXT("at most 25 players"));
		if (Overseas > AuctionRules::OverseasMax) return Fail(TEXT("at most eight overseas players"));
		if (RetentionCost(Players) > Config.Purse) return Fail(TEXT("their contracts are more than the salary cap"));
		return true;
	}
	if (Players.Num() > AuctionRules::KeepMax) return Fail(TEXT("at most six retentions"));
	int32 Capped = 0, Uncapped = 0;
	for (int32 P : Players)
	{
		if (WantsAuction(P)) return Fail(TEXT("he wants to go into the auction"));
		Player(P).bCapped ? ++Capped : ++Uncapped;
	}
	if (Capped > AuctionRules::KeepCappedMax) return Fail(TEXT("at most five capped players"));
	if (Uncapped > AuctionRules::KeepUncappedMax) return Fail(TEXT("at most two uncapped players"));
	return true;
}

TArray<int32> FAuction::RetentionCosts(const TArray<int32>& Players) const
{
	TArray<int32> Cost;
	Cost.Init(0, Players.Num());
	if (IsMini())
	{
		for (int32 I = 0; I < Players.Num(); ++I) Cost[I] = ContractOf(Players[I]);
		return Cost;
	}
	// The first k capped retentions cost the first k slabs. Handing the dearer slabs to the players who ask most keeps
	// the total least (pairing sorted with sorted minimises a sum of maxima).
	TArray<int32> Capped, Slabs;
	for (int32 I = 0; I < Players.Num(); ++I)
	{
		if (Player(Players[I]).bCapped) { Slabs.Add(AuctionRules::CappedRetentionCost(Capped.Num())); Capped.Add(I); }
		else Cost[I] = FMath::Max(AuctionRules::UncappedRetention, RetentionAsk(Players[I]));
	}
	Capped.StableSort([&](int32 A, int32 B) { return RetentionAsk(Players[A]) > RetentionAsk(Players[B]); });
	Slabs.Sort([](int32 A, int32 B) { return A > B; });
	for (int32 K = 0; K < Capped.Num(); ++K) Cost[Capped[K]] = FMath::Max(Slabs[K], RetentionAsk(Players[Capped[K]]));
	return Cost;
}

int32 FAuction::RetentionCost(const TArray<int32>& Players) const
{
	int32 Sum = 0;
	for (int32 C : RetentionCosts(Players)) Sum += C;
	return Sum;
}

bool FAuction::Retain(int32 Team, const TArray<int32>& Players)
{
	if (Phase != EAuctionPhase::Retention || !CanRetain(Team, Players)) return false;
	FAuctionTeam& T = Teams[Team];
	const TArray<int32> Costs = RetentionCosts(Players);
	for (int32 I = 0; I < Players.Num(); ++I)
	{
		AddSigning(Team, Players[I], Costs[I], true, false);
		if (!IsMini()) Player(Players[I]).bCapped ? ++T.KeptCapped : ++T.KeptUncapped;
	}
	T.RtmCards = IsMini() ? 0 : AuctionRules::KeepMax - Players.Num();
	T.bRetained = true;
	return true;
}

void FAuction::BeginAuction()
{
	if (Phase != EAuctionPhase::Retention) return;
	OpenTradeWindow();
	for (int32 T = 0; T < Teams.Num(); ++T)
	{
		if (bNoRetentions && !IsMini())
		{
			Teams[T].Squad.Reset();
			Teams[T].Purse = Config.Purse;
			Teams[T].RtmCards = AuctionRules::KeepMax;
			Teams[T].KeptCapped = 0;
			Teams[T].KeptUncapped = 0;
			Teams[T].bRetained = true;
		}
		else if (!Teams[T].bRetained)
		{
			// A human table that never chose keeps nobody in a mega auction, and keeps the suggested squad in a mini one.
			Retain(T, IsHuman(T) && !IsMini() ? TArray<int32>() : AiRetentions(T));
		}
	}
	if (bNoRetentions && !IsMini()) SoldTo.Reset();
	// The Right to Match belongs to the side a player last played for, and only in a mega auction.
	for (int32 P = 0; P < Owner.Num(); ++P) FormerTeam[P] = IsMini() || SoldTo.Contains(P) ? INDEX_NONE : Owner[P];
	UpdateMarket();
	BuildSets();
	BuildPlans();
	SetIndex = 0;
	LotInSet = -1;
	SetPhase(EAuctionPhase::SetIntro);
	Emit(EAuctionEvent::SetOpened);
}

void FAuction::BuildSets()
{
	using namespace AuctionEnginePrivate;
	// The pool: everyone not retained, most valuable first.
	TArray<int32> Pool;
	for (const FAuctionPlayer& P : AuctionData::Players()) if (!SoldTo.Contains(P.Id)) Pool.Add(P.Id);
	Pool.Sort([this](int32 A, int32 B) { return MarketEstimate(A) > MarketEstimate(B); });

	auto Shuffle = [this](TArray<int32>& A) { for (int32 I = A.Num() - 1; I > 0; --I) A.Swap(I, Rng.RandRange(0, I)); };

	// A mega auction opens with two marquee sets of six: the biggest capped names at the top base price.
	TArray<int32> Marquee;
	if (!IsMini())
	{
		for (int32 P : Pool) if (Marquee.Num() < 2 * MarqueeSize && Player(P).bCapped && Player(P).Base >= 200) Marquee.Add(P);
		for (int32 M = 0; M < 2; ++M)
		{
			FAuctionSet& S = Sets.AddDefaulted_GetRef();
			S.Code = FString::Printf(TEXT("M%d"), M + 1);
			S.Name = FString::Printf(TEXT("MARQUEE SET %d"), M + 1);
			for (int32 I = M * MarqueeSize; I < FMath::Min(Marquee.Num(), (M + 1) * MarqueeSize); ++I) S.Players.Add(Marquee[I]);
			Shuffle(S.Players);
		}
	}

	// Then capped sets by role, then uncapped, round after round, as the IPL draws them. The first round is held in
	// full; from the second the auction is accelerated and only nominated players come to the table. A mega auction's
	// second day opens with the first uncapped set.
	TArray<int32> ByRole[2][int32(EAuctionRole::Count)];
	for (int32 P : Pool) if (!Marquee.Contains(P)) ByRole[Player(P).bCapped ? 0 : 1][int32(Player(P).Role)].Add(P);
	FirstDayTwoSet = INDEX_NONE;
	for (int32 Round = 0;; ++Round)
	{
		bool bAny = false;
		for (int32 Capped = 0; Capped < 2; ++Capped)
			for (EAuctionRole Role : SetOrder)
			{
				const int32 R = int32(Role);
				const int32 Size = Capped == 0 ? CappedSetSize : UncappedSetSize;
				const TArray<int32>& List = ByRole[Capped][R];
				if (Round * Size >= List.Num()) continue;
				bAny = true;
				if (!IsMini() && Round == 0 && Capped == 1 && FirstDayTwoSet == INDEX_NONE) FirstDayTwoSet = Sets.Num();
				FAuctionSet& S = Sets.AddDefaulted_GetRef();
				S.Code = FString::Printf(TEXT("%s%s%d"), Capped ? TEXT("U") : TEXT(""), AuctionRules::RoleCode(Role), Round + 1);
				S.Name = FString::Printf(TEXT("%s %s %d"), Capped ? TEXT("UNCAPPED") : TEXT("CAPPED"), SetNoun(Role), Round + 1);
				S.bAccelerated = Round > 0;
				for (int32 I = Round * Size; I < FMath::Min(List.Num(), (Round + 1) * Size); ++I) S.Players.Add(List[I]);
				Shuffle(S.Players);
			}
		if (!bAny) break;
	}
}

// ---- Flow --------------------------------------------------------------------------------------------------------

void FAuction::Emit(EAuctionEvent Type, int32 Team, int32 Amount)
{
	Events.Add({ Type, Team, Lot, Amount, Clock, RtmTeam == INDEX_NONE ? INDEX_NONE : Team == RtmTeam ? Holder : RtmTeam });
}

void FAuction::SetPhase(EAuctionPhase P)
{
	Phase = P;
	PhaseStart = Clock;
}

float FAuction::Pace() const
{
	const FAuctionSet* S = CurrentSet();
	return (S && S->bAccelerated ? AccelPace : 1.f) * (IsFast() ? 0.02f : 1.f);
}

void FAuction::Tick(float Dt)
{
	if (Phase == EAuctionPhase::Retention || Phase == EAuctionPhase::Finished) return;
	// Skipping runs the same rules on a coarse clock until the lot (or set) is decided or the human is asked.
	const int32 Steps = IsFast() ? 20000 : 1;
	for (int32 I = 0; I < Steps && Phase != EAuctionPhase::Finished && (I == 0 || (IsFast() && !AwaitingHuman())); ++I)
	{
		Clock += IsFast() ? 0.25 : Dt;
		switch (Phase)
		{
		case EAuctionPhase::SetIntro:
			if (PhaseTime() >= SetIntroTime * Pace()) NextLot();
			break;
		case EAuctionPhase::LotIntro:
			if (PhaseTime() >= LotIntroTime * Pace())
			{
				SetPhase(EAuctionPhase::Bidding);
				LastBidAt = Clock;
				Emit(EAuctionEvent::OpeningCall, INDEX_NONE, Player(Lot).Base);
				ScheduleAi();
			}
			break;
		case EAuctionPhase::Bidding:
			TickBidding();
			break;
		case EAuctionPhase::RtmAsk:
			if (IsHuman(RtmTeam)) { if (PhaseTime() >= HumanRtmTimeout) HumanRtm(false); }
			else if (PhaseTime() >= AiRtmThink * Pace())
			{
				if (AiUsesRtm(RtmTeam)) { Emit(EAuctionEvent::RtmUsed, RtmTeam, Price); SetPhase(EAuctionPhase::RtmRaise); }
				else { Emit(EAuctionEvent::RtmDeclined, RtmTeam, Price); Sell(Holder, Price, false); }
			}
			break;
		case EAuctionPhase::RtmRaise:
			if (IsHuman(Holder)) { if (PhaseTime() >= HumanRtmTimeout) HumanFinalRaise(Price); }
			else if (PhaseTime() >= AiRtmThink * Pace())
			{
				Price = AiFinalRaise(Holder);
				Emit(EAuctionEvent::FinalRaise, Holder, Price);
				SetPhase(EAuctionPhase::RtmMatch);
			}
			break;
		case EAuctionPhase::RtmMatch:
			if (IsHuman(RtmTeam)) { if (PhaseTime() >= HumanRtmTimeout) HumanMatch(false); }
			else if (PhaseTime() >= AiRtmThink * Pace())
			{
				if (MaxThisLot[RtmTeam] >= Price && CanAfford(RtmTeam, Lot, Price)) { Emit(EAuctionEvent::RtmMatched, RtmTeam, Price); Sell(RtmTeam, Price, true); }
				else { Emit(EAuctionEvent::RtmNotMatched, RtmTeam, Price); Sell(Holder, Price, false); }
			}
			break;
		case EAuctionPhase::Hammer:
			if (PhaseTime() >= HammerTime * Pace()) NextLot();
			break;
		case EAuctionPhase::Break:
			if (PhaseTime() >= BreakTime * Pace())
			{
				Day = 2;
				Emit(EAuctionEvent::DayStarted);
				SetPhase(EAuctionPhase::SetIntro);
				Emit(EAuctionEvent::SetOpened);
			}
			break;
		default:
			break;
		}
	}
}

void FAuction::NextLot()
{
	bFastLot = false;
	for (;;)
	{
		bool bAllFull = true;
		for (const FAuctionTeam& T : Teams) bAllFull &= T.Squad.Num() >= AuctionRules::SquadMax;
		const FAuctionSet* S = CurrentSet();
		if (bAllFull || !S)
		{
			// The sets are done: unsold players come back in accelerated rounds while any side is still short of 18
			// or wants them.
			bool bShort = false;
			for (const FAuctionTeam& T : Teams) bShort |= T.Squad.Num() < AuctionRules::SquadMin;
			const bool bWanted = Unsold.ContainsByPredicate([this](int32 P) { return Nominated(P); });
			if (!bAllFull && UnsoldRounds < AuctionEnginePrivate::MaxUnsoldRounds && !Unsold.IsEmpty() && (bShort || bWanted))
			{
				FAuctionSet& R = Sets.AddDefaulted_GetRef();
				R.Code = TEXT("UR");
				R.Name = FString::Printf(TEXT("ACCELERATED ROUND %d"), ++UnsoldRounds);
				R.bAccelerated = true;
				R.Players = MoveTemp(Unsold);
				Unsold.Reset();
				SetIndex = Sets.Num() - 1;
				LotInSet = -1;
				bFastSet = false;
				Lot = INDEX_NONE;
				SetPhase(EAuctionPhase::SetIntro);
				Emit(EAuctionEvent::SetOpened);
				return;
			}
			Lot = INDEX_NONE;
			SetPhase(EAuctionPhase::Finished);
			Emit(EAuctionEvent::Finished);
			return;
		}
		if (++LotInSet >= S->Players.Num())
		{
			const bool bWasAccelerated = S->bAccelerated;
			++SetIndex;
			LotInSet = -1;
			bFastSet = false;
			Lot = INDEX_NONE;
			if (const FAuctionSet* Next = CurrentSet())
			{
				// A mega auction runs over two days: the first ends before the first uncapped set.
				if (SetIndex == FirstDayTwoSet && Day == 1)
				{
					SetPhase(EAuctionPhase::Break);
					Emit(EAuctionEvent::DayEnded);
					return;
				}
				SetPhase(EAuctionPhase::SetIntro);
				Emit(EAuctionEvent::SetOpened);
				if (Next->bAccelerated && !bWasAccelerated && Next->Code != TEXT("UR")) Emit(EAuctionEvent::Accelerated);
				return;
			}
			continue;
		}
		const int32 P = S->Players[LotInSet];
		// Accelerated: a player nobody nominates is passed over and joins the unsold list.
		if (S->bAccelerated && !Nominated(P)) { Unsold.Add(P); continue; }
		OpenLot(P);
		return;
	}
}

bool FAuction::Nominated(int32 PlayerId) const
{
	if (HumanNominations.Contains(PlayerId)) return true;
	for (int32 T = 0; T < Teams.Num(); ++T)
	{
		if (IsHuman(T)) { if (WishList[T].Contains(PlayerId)) return true; }
		else if (Valuation(T, PlayerId) >= Player(PlayerId).Base) return true;
	}
	return false;
}

void FAuction::Nominate(int32 PlayerId, bool bOn)
{
	if (bOn) HumanNominations.Add(PlayerId);
	else HumanNominations.Remove(PlayerId);
}

void FAuction::SetWish(int32 Team, int32 PlayerId, int32 Max, bool bAuto)
{
	if (!WishList.IsValidIndex(Team)) return;
	if (Max <= 0) { WishList[Team].Remove(PlayerId); return; }
	WishList[Team].Add(PlayerId, { Max, bAuto });
	// Switched on while he is under the hammer: the table's paddle joins in.
	if (PlayerId == Lot && (Phase == EAuctionPhase::LotIntro || Phase == EAuctionPhase::Bidding) && IsHuman(Team))
	{
		MaxThisLot[Team] = bAuto ? Max : 0;
		if (Phase == EAuctionPhase::Bidding) ScheduleAi();
	}
}

const FAuctionWish* FAuction::WishFor(int32 Team, int32 PlayerId) const
{
	return WishList.IsValidIndex(Team) ? WishList[Team].Find(PlayerId) : nullptr;
}

const TMap<int32, FAuctionWish>& FAuction::Wishes(int32 Team) const
{
	static const TMap<int32, FAuctionWish> None;
	return WishList.IsValidIndex(Team) ? WishList[Team] : None;
}

void FAuction::OpenLot(int32 PlayerId)
{
	Lot = PlayerId;
	Price = 0;
	Holder = RtmTeam = PendingTeam = LastSoldTo = INDEX_NONE;
	HammerStage = 0;
	Bidders.Reset();
	OutCalled.Reset();
	Enforcing.Reset();
	TimeoutThisLot.Reset();
	++LotsHeld;
	UpdateMarket();
	const int32 Base = Player(PlayerId).Base;
	for (int32 T = 0; T < Teams.Num(); ++T)
	{
		BidsThisLot[T] = 0;
		if (IsHuman(T))
		{
			// A human table's paddle only goes up on its own for a shortlisted player on auto-bid.
			const FAuctionWish* W = WishFor(T, PlayerId);
			MaxThisLot[T] = W && W->bAuto ? W->Max : 0;
			EnterAt[T] = Base;
		}
		else
		{
			MaxThisLot[T] = Valuation(T, PlayerId);
			// Patient sides sit on their paddles and come in once the early bidders have pushed the price up.
			const float Wait = AuctionData::Franchises()[T].Patience * FMath::Square(Rng.FRand());
			EnterAt[T] = Base + FMath::RoundToInt(Wait * 0.7f * FMath::Max(0, MaxThisLot[T] - Base));
		}
	}
	PlanLot();
	StartMax = MaxThisLot;
	SetPhase(EAuctionPhase::LotIntro);
	Emit(EAuctionEvent::LotOpened, INDEX_NONE, Base);
}

void FAuction::TickBidding()
{
	const float P = Pace();
	if (PendingTeam != INDEX_NONE && Clock >= PendingAt)
	{
		const int32 T = PendingTeam;
		PendingTeam = INDEX_NONE;
		const int32 Ask = AskPrice();
		if (T != Holder && MaxThisLot[T] >= Ask && CanAfford(T, Lot, Ask)) PlaceBid(T, Ask);
		ScheduleAi();
	}
	if (PendingTeam != INDEX_NONE) return; // a paddle is about to go up: the hammer waits
	const double Quiet = Clock - LastBidAt;
	if (Holder == INDEX_NONE)
	{
		if (Quiet >= NoBidTime * P)
		{
			Unsold.Add(Lot);
			LastSoldTo = INDEX_NONE;
			bFastLot = false;
			Emit(EAuctionEvent::Unsold);
			SetPhase(EAuctionPhase::Hammer);
		}
		return;
	}
	if (HammerStage == 0 && Quiet >= OnceAfter * P)
	{
		HammerStage = 1;
		Emit(EAuctionEvent::GoingOnce, Holder, Price);
		ScheduleAi(); // last chance: a late paddle
	}
	else if (HammerStage == 1 && Quiet >= (OnceAfter + TwiceAfter) * P)
	{
		HammerStage = 2;
		Emit(EAuctionEvent::GoingTwice, Holder, Price);
		ScheduleAi();
	}
	else if (HammerStage == 2 && Quiet >= (OnceAfter + TwiceAfter + SoldAfter) * P)
	{
		Hammer();
	}
}

void FAuction::ScheduleAi()
{
	if (PendingTeam != INDEX_NONE || Phase != EAuctionPhase::Bidding) return;
	const int32 Ask = AskPrice();
	TArray<int32> Candidates;
	TArray<float> Weights;
	float Total = 0.f;
	for (int32 T = 0; T < Teams.Num(); ++T)
	{
		if (T == Holder || OutCalled.Contains(T)) continue;
		const bool bPerson = IsHuman(T);
		if (bPerson && MaxThisLot[T] <= 0) continue; // a person bids with the paddle, not here
		if (MaxThisLot[T] < Ask || !CanAfford(T, Lot, Ask))
		{
			// A side that was in the bidding and has reached its limit shakes its head.
			if (Bidders.Contains(T) && !OutCalled.Contains(T)) { OutCalled.Add(T); Emit(EAuctionEvent::Out, T, Price); }
			continue;
		}
		if (!bPerson)
		{
			// Patient sides wait for the price to climb, but a lot a side wants always gets its opening paddle: with no
			// bid yet the price never climbs, and a star whose every suitor waited would go unsold at his base.
			if (HammerStage == 0 && Holder != INDEX_NONE && Ask < EnterAt[T]) continue;
			if (Holder == INDEX_NONE && EnterAt[T] == MAX_int32) continue; // sitting out for the Right to Match
			// A table can walk away before its absolute ceiling after a long duel; late entrants still get the final calls.
			const float Headroom = float(MaxThisLot[T] - Ask) / FMath::Max(1, MaxThisLot[T]);
			if (Bidders.Contains(T) && Headroom < 0.12f && Rng.FRand() > Headroom / 0.12f)
			{
				OutCalled.Add(T);
				Emit(EAuctionEvent::Out, T, Price);
				continue;
			}
		}
		const float Headroom = float(MaxThisLot[T] - Ask) / FMath::Max(1, MaxThisLot[T]);
		float W = 0.15f + Headroom;
		if (bPerson) W += 1.5f; // an auto-bid answers at once
		else if (Bidders.Contains(T)) W += 0.9f; // "back with Kolkata": the sides in the duel keep going
		else if (Bidders.Num() >= 2 && HammerStage == 0) W *= 0.25f; // a third paddle mostly waits for one to drop
		Candidates.Add(T);
		Weights.Add(W);
		Total += W;
	}
	if (Candidates.IsEmpty()) return;
	float Pick = Rng.FRand() * Total;
	int32 Chosen = Candidates.Last();
	for (int32 I = 0; I < Candidates.Num(); ++I) if ((Pick -= Weights[I]) <= 0.f) { Chosen = Candidates[I]; break; }

	// Rapid paddles early; near its limit a table huddles before it goes again.
	const float Close = float(Ask) / float(FMath::Max(1, MaxThisLot[Chosen]));
	float Delay = FMath::Lerp(0.7f, 1.6f, Rng.FRand());
	if (Holder == INDEX_NONE) Delay = OpeningTime + 1.5f * Rng.FRand();
	else if (Close > 0.75f) Delay += FMath::Square((Close - 0.75f) / 0.25f) * FMath::Lerp(2.f, 6.f, Rng.FRand());
	if (IsHuman(Chosen)) Delay = FMath::Lerp(0.6f, 1.1f, Rng.FRand());
	else
	{
		// A round crore figure stops a table: 20 crore is a different number from 19.75. It thinks, and sometimes that
		// is where it ends.
		for (int32 Mark : AuctionEnginePrivate::StickingPoints)
		{
			if (Price >= Mark || Ask < Mark) continue;
			Delay += FMath::Lerp(1.5f, 3.5f, Rng.FRand());
			if (Close > 0.85f && Rng.FRand() < 0.35f && !Enforcing.Contains(Chosen))
			{
				OutCalled.Add(Chosen);
				if (Bidders.Contains(Chosen)) Emit(EAuctionEvent::Out, Chosen, Price);
				ScheduleAi();
				return;
			}
			break;
		}
	}
	if (HammerStage > 0) Delay = FMath::Min(Delay, FMath::Lerp(0.4f, 1.4f, Rng.FRand()));
	if (Delay > 3.f && !IsHuman(Chosen)) Emit(EAuctionEvent::Huddle, Chosen, Ask);
	PendingTeam = Chosen;
	PendingAt = Clock + Delay * Pace();
}

void FAuction::PlaceBid(int32 Team, int32 Amount)
{
	// The side just outbid in a two-way duel warms to it: each round it has stayed in lifts its limit a little, up to
	// a fifth over where it started (the heat that took Pant from 20 to 27 crore).
	const int32 Was = Holder;
	if (Was != INDEX_NONE && !IsHuman(Was) && !Enforcing.Contains(Was) && Bidders.Num() == 2 && BidsThisLot[Was] >= 2)
	{
		const float Aggression = AuctionData::Franchises()[Was].Aggression;
		const int32 Cap = FMath::RoundToInt(StartMax[Was] * (1.f + 0.2f * Aggression));
		MaxThisLot[Was] = FMath::Min(Cap, MaxThisLot[Was] + FMath::RoundToInt(StartMax[Was] * 0.03f * Aggression));
	}
	Price = Amount;
	Holder = Team;
	Bidders.Add(Team);
	++BidsThisLot[Team];
	OutCalled.Remove(Team);
	LastBidAt = Clock;
	HammerStage = 0;
	Emit(EAuctionEvent::Bid, Team, Amount);
}

bool FAuction::CanHumanBid(int32 Team) const
{
	const int32 T = Team == INDEX_NONE ? Human : Team;
	return IsHuman(T) && Phase == EAuctionPhase::Bidding && Holder != T && CanAfford(T, Lot, AskPrice());
}

bool FAuction::HumanBid(int32 Team)
{
	const int32 T = Team == INDEX_NONE ? Human : Team;
	if (!CanHumanBid(T)) return false;
	PendingTeam = INDEX_NONE; // the human's paddle beat whoever was about to bid
	PlaceBid(T, AskPrice());
	ScheduleAi();
	return true;
}

bool FAuction::HumanJumpBid(int32 Team, int32 Amount)
{
	if (!CanHumanBid(Team)) return false;
	const int32 At = AuctionRules::OnLadder(FMath::Max(Amount, AskPrice()), AskPrice());
	if (!CanAfford(Team, Lot, At)) return false;
	PendingTeam = INDEX_NONE;
	PlaceBid(Team, At);
	ScheduleAi();
	return true;
}

bool FAuction::CanRequestTimeout(int32 Team) const
{
	return IsHuman(Team) && Phase == EAuctionPhase::Bidding && Teams[Team].Timeouts > 0 && !TimeoutThisLot.Contains(Team);
}

bool FAuction::RequestTimeout(int32 Team)
{
	if (!CanRequestTimeout(Team)) return false;
	--Teams[Team].Timeouts;
	TimeoutThisLot.Add(Team);
	// The room waits: no hammer and no paddle until the table has talked, then the calls start again.
	LastBidAt = Clock + TimeoutTime * Pace();
	HammerStage = 0;
	if (PendingTeam != INDEX_NONE) PendingAt = FMath::Max(PendingAt, LastBidAt);
	Emit(EAuctionEvent::Timeout, Team);
	return true;
}

void FAuction::Hammer()
{
	const int32 R = FormerTeam.IsValidIndex(Lot) ? FormerTeam[Lot] : INDEX_NONE;
	if (R != INDEX_NONE && R != Holder && RtmEligible(R))
	{
		RtmTeam = R;
		Emit(EAuctionEvent::RtmOffered, R, Price);
		SetPhase(EAuctionPhase::RtmAsk);
		return;
	}
	Sell(Holder, Price, false);
}

bool FAuction::RtmEligible(int32 Team) const
{
	const FAuctionTeam& T = Teams[Team];
	if (IsMini() || T.RtmCards <= 0) return false;
	const bool bRoom = Player(Lot).bCapped ? T.KeptCapped < AuctionRules::KeepCappedMax : T.KeptUncapped < AuctionRules::KeepUncappedMax;
	return bRoom && CanAfford(Team, Lot, Price);
}

bool FAuction::AiUsesRtm(int32 Team) const
{
	// A card is scarce (six for retentions and RTMs together): spent on a player the side values past the price with
	// room for the final raise, never to bring back a fringe player it could buy at his base.
	if (MaxThisLot[Team] < Price || Price < 100) return false;
	const float Headroom = float(MaxThisLot[Team]) / float(FMath::Max(1, Price));
	return Headroom >= 1.f || Plans[Team].Targets.ContainsByPredicate([this](const FAuctionTarget& X) { return X.Player == Lot && X.Tier <= 1; });
}

int32 FAuction::AiFinalRaise(int32 Team) const
{
	const int32 Max = MaxThisLot[Team];
	if (Max <= Price) return Price;
	FRandomStream RaiseRng(Seed + Lot * 31 + Team * 7919);
	// Raise to just past what the side with the card can pay, if we can: then it has to walk away. If we cannot,
	// sometimes push toward our own limit to make them pay for him, sometimes hold and hope.
	const int32 Guess = EstimateCeiling(RtmTeam, Lot);
	int32 Target = Price;
	if (AuctionRules::NextBid(Guess) <= Max) Target = AuctionRules::NextBid(Guess);
	else if (RaiseRng.FRand() < 0.5f) Target = Price + FMath::RoundToInt((Max - Price) * FMath::Lerp(0.5f, 0.95f, RaiseRng.FRand()));
	int32 Raise = Price;
	while (AuctionRules::NextBid(Raise) <= FMath::Min(Target, Max) && CanAfford(Team, Lot, AuctionRules::NextBid(Raise))) Raise = AuctionRules::NextBid(Raise);
	return Raise;
}

void FAuction::HumanRtm(bool bUse)
{
	if (Phase != EAuctionPhase::RtmAsk || !IsHuman(RtmTeam)) return;
	if (bUse) { Emit(EAuctionEvent::RtmUsed, RtmTeam, Price); SetPhase(EAuctionPhase::RtmRaise); }
	else { Emit(EAuctionEvent::RtmDeclined, RtmTeam, Price); Sell(Holder, Price, false); }
}

void FAuction::HumanFinalRaise(int32 Amount)
{
	if (Phase != EAuctionPhase::RtmRaise || !IsHuman(Holder)) return;
	if (Amount > Price && CanAfford(Holder, Lot, Amount)) Price = Amount;
	Emit(EAuctionEvent::FinalRaise, Holder, Price);
	SetPhase(EAuctionPhase::RtmMatch);
}

void FAuction::HumanMatch(bool bMatch)
{
	if (Phase != EAuctionPhase::RtmMatch || !IsHuman(RtmTeam)) return;
	if (bMatch && CanAfford(RtmTeam, Lot, Price)) { Emit(EAuctionEvent::RtmMatched, RtmTeam, Price); Sell(RtmTeam, Price, true); }
	else { Emit(EAuctionEvent::RtmNotMatched, RtmTeam, Price); Sell(Holder, Price, false); }
}

bool FAuction::AwaitingHuman() const
{
	return (Phase == EAuctionPhase::RtmAsk && IsHuman(RtmTeam)) || (Phase == EAuctionPhase::RtmRaise && IsHuman(Holder))
		|| (Phase == EAuctionPhase::RtmMatch && IsHuman(RtmTeam));
}

void FAuction::SkipLot()
{
	if (Phase == EAuctionPhase::LotIntro || Phase == EAuctionPhase::Bidding || Phase == EAuctionPhase::SetIntro) bFastLot = true;
}

void FAuction::FastResolveCurrentLot()
{
	if (Phase != EAuctionPhase::LotIntro && Phase != EAuctionPhase::Bidding
		&& Phase != EAuctionPhase::RtmAsk && Phase != EAuctionPhase::RtmRaise && Phase != EAuctionPhase::RtmMatch)
	{
		return;
	}

	bFastLot = true;
	int32 Safety = 0;
	while ((Phase == EAuctionPhase::LotIntro || Phase == EAuctionPhase::Bidding
		|| Phase == EAuctionPhase::RtmAsk || Phase == EAuctionPhase::RtmRaise || Phase == EAuctionPhase::RtmMatch)
		&& ++Safety < 1000)
	{
		if (Phase == EAuctionPhase::RtmAsk && IsHuman(RtmTeam)) HumanRtm(false);
		else if (Phase == EAuctionPhase::RtmRaise && IsHuman(Holder)) HumanFinalRaise(Price);
		else if (Phase == EAuctionPhase::RtmMatch && IsHuman(RtmTeam)) HumanMatch(false);

		Tick(0.25f);
	}
	bFastLot = false;
	PhaseStart = Clock;
}

void FAuction::SkipSet()
{
	if (Phase != EAuctionPhase::Retention && Phase != EAuctionPhase::Finished && Phase != EAuctionPhase::Break) bFastSet = true;
}

void FAuction::Sell(int32 Team, int32 Amount, bool bRtm)
{
	const int32 Worth = ExpectedPrice(Lot);
	AddSigning(Team, Lot, Amount, false, bRtm);
	Teams[Team].Squad.Last().Worth = Worth;
	if (bRtm)
	{
		FAuctionTeam& T = Teams[Team];
		--T.RtmCards;
		Player(Lot).bCapped ? ++T.KeptCapped : ++T.KeptUncapped;
	}
	Holder = Team;
	Price = Amount;
	LastSoldTo = Team;
	bFastLot = false;
	Emit(EAuctionEvent::Sold, Team, Amount);
	const int32 Paid = AuctionRules::Fee(Config.Mode, Player(Lot).IsOverseas(), Amount);
	if (Paid < Amount) Emit(EAuctionEvent::FeeCapped, Team, Paid);
	OnSold(Team, Lot);
	SetPhase(EAuctionPhase::Hammer);
}

void FAuction::AddSigning(int32 Team, int32 PlayerId, int32 Amount, bool bRetained, bool bRtm)
{
	FAuctionTeam& T = Teams[Team];
	const int32 Paid = bRetained ? Amount : AuctionRules::Fee(Config.Mode, Player(PlayerId).IsOverseas(), Amount);
	T.Squad.Add({ PlayerId, Amount, Paid, bRetained, bRtm, bRetained ? MarketEstimate(PlayerId) : Amount });
	T.Purse -= Amount;
	SoldTo.Add(PlayerId, Team);
	Invalidate(Team);
}

// ---- Money -------------------------------------------------------------------------------------------------------

int32 FAuction::MaxBid(int32 Team) const
{
	const FAuctionTeam& T = Teams[Team];
	return T.Purse - FMath::Max(0, AuctionRules::SquadMin - T.Squad.Num() - 1) * AuctionRules::MinBase;
}

bool FAuction::CanAfford(int32 Team, int32 PlayerId, int32 Amount) const
{
	if (!Teams.IsValidIndex(Team)) return false;
	const FAuctionTeam& T = Teams[Team];
	if (T.Squad.Num() >= AuctionRules::SquadMax) return false;
	if (Player(PlayerId).IsOverseas() && T.Overseas() >= AuctionRules::OverseasMax) return false;
	return Amount <= MaxBid(Team);
}

int32 FAuction::LotsUntil(int32 PlayerId) const
{
	if (SoldTo.Contains(PlayerId)) return -1;
	if (PlayerId == Lot) return 0;
	int32 N = 0;
	for (int32 S = SetIndex; S < Sets.Num(); ++S)
	{
		const TArray<int32>& List = Sets[S].Players;
		for (int32 I = S == SetIndex ? FMath::Max(0, LotInSet + 1) : 0; I < List.Num(); ++I)
		{
			++N;
			if (List[I] == PlayerId) return N;
		}
	}
	return -1;
}

// ---- Save and resume ---------------------------------------------------------------------------------------------
//
// Line-based text: a keyword, then numbers. Written between lots (the game saves on every hammer and at the day's
// end), so nothing of a lot in progress needs keeping: resuming presents the next lot.

namespace AuctionEnginePrivate
{
	void Line(FString& Out, const TCHAR* Key, std::initializer_list<int64> Values)
	{
		Out += Key;
		for (int64 V : Values) Out += FString::Printf(TEXT(" %lld"), (long long)V);
		Out += TEXT("\n");
	}
	void Line(FString& Out, const TCHAR* Key, const TArray<int32>& Values)
	{
		Out += Key;
		Out += FString::Printf(TEXT(" %d"), Values.Num());
		for (int32 V : Values) Out += FString::Printf(TEXT(" %d"), V);
		Out += TEXT("\n");
	}
	/** The numbers after the keyword, and the keyword. */
	struct FLine
	{
		FString Key;
		TArray<int64> N;
		int64 operator[](int32 I) const { return N.IsValidIndex(I) ? N[I] : 0; }
		TArray<int32> List(int32 From = 0) const
		{
			TArray<int32> L;
			const int32 Count = int32((*this)[From]);
			for (int32 I = 0; I < Count; ++I) L.Add(int32((*this)[From + 1 + I]));
			return L;
		}
	};
	TArray<FLine> Parse(const FString& Text)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines);
		TArray<FLine> Out;
		for (const FString& L : Lines)
		{
			TArray<FString> W;
			L.ParseIntoArrayWS(W);
			if (W.IsEmpty()) continue;
			FLine& X = Out.AddDefaulted_GetRef();
			X.Key = W[0];
			for (int32 I = 1; I < W.Num(); ++I) X.N.Add(int64(FCString::Atoi64(*W[I])));
		}
		return Out;
	}
	constexpr int32 SaveVersion = 1;
}

FString FAuction::SaveState() const
{
	using namespace AuctionEnginePrivate;
	FString O = TEXT("CRICKET26AUCTION\n");
	Line(O, TEXT("version"), { SaveVersion });
	Line(O, TEXT("config"), { int64(Config.Mode), int64(Config.Difficulty), Config.Season, Config.Purse, Config.bNoRetentions ? 1 : 0, Seed });
	Line(O, TEXT("humans"), Config.Humans);
	for (int32 T = 0; T < Config.Carried.Num(); ++T)
	{
		TArray<int32> Flat;
		for (const FAuctionSigning& S : Config.Carried[T]) { Flat.Add(S.Player); Flat.Add(S.Price); }
		O += FString::Printf(TEXT("carried %d"), T);
		Line(O, TEXT(""), Flat);
	}
	Line(O, TEXT("state"), { int64(Phase), int64(Clock * 1000.0), SetIndex, LotInSet, LotsHeld, Day, Lot, Price, Holder, LastSoldTo,
		UnsoldRounds, FirstDayTwoSet, bNoRetentions ? 1 : 0, bTradeWindowDone ? 1 : 0, Rng.GetCurrentSeed() });
	Line(O, TEXT("owner"), Owner);
	Line(O, TEXT("contract"), Contract);
	Line(O, TEXT("former"), FormerTeam);
	for (const FAuctionTrade& X : Trades) Line(O, TEXT("trade"), { X.From, X.To, X.Gave, X.Got });
	for (int32 T = 0; T < Teams.Num(); ++T)
	{
		const FAuctionTeam& Tm = Teams[T];
		Line(O, TEXT("team"), { T, Tm.Purse, Tm.RtmCards, Tm.KeptCapped, Tm.KeptUncapped, Tm.Timeouts, Tm.bRetained ? 1 : 0 });
		for (const FAuctionSigning& S : Tm.Squad) Line(O, TEXT("signing"), { T, S.Player, S.Price, S.Fee, S.bRetained ? 1 : 0, S.bRtm ? 1 : 0, S.Worth });
		for (const auto& W : WishList[T]) Line(O, TEXT("wish"), { T, W.Key, W.Value.Max, W.Value.bAuto ? 1 : 0 });
		for (const FAuctionTarget& X : Plans[T].Targets) Line(O, TEXT("target"), { T, X.Player, int64(X.Slot), X.Tier, X.Budget });
		for (int32 S = 0; S < int32(EAuctionSlot::Count); ++S)
			if (Plans[T].SlotBoost[S] > 0.f) Line(O, TEXT("boost"), { T, S, FMath::RoundToInt(Plans[T].SlotBoost[S] * 1000.f) });
	}
	for (const FAuctionSet& S : Sets)
	{
		O += FString::Printf(TEXT("set %s %s %d"), *S.Code, *S.Name.Replace(TEXT(" "), TEXT("_")), S.bAccelerated ? 1 : 0);
		Line(O, TEXT(""), S.Players);
	}
	Line(O, TEXT("unsold"), Unsold);
	Line(O, TEXT("nominated"), HumanNominations.Array());
	for (const FAuctionEventRecord& E : Events)
		Line(O, TEXT("event"), { int64(E.Type), E.Team, E.Player, E.Amount, int64(E.Time * 1000.0), E.Other });
	return O;
}

bool FAuction::ReadSaveHeader(const FString& Text, FAuctionConfig& OutConfig, int32& OutSeed)
{
	using namespace AuctionEnginePrivate;
	if (!Text.StartsWith(TEXT("CRICKET26AUCTION"))) return false;
	bool bConfig = false;
	OutConfig = FAuctionConfig();
	for (const FLine& L : Parse(Text))
	{
		if (L.Key == TEXT("version") && L[0] != SaveVersion) return false;
		if (L.Key == TEXT("config"))
		{
			OutConfig.Mode = EAuctionMode(L[0]);
			OutConfig.Difficulty = EAuctionDifficulty(L[1]);
			OutConfig.Season = int32(L[2]);
			OutConfig.Purse = int32(L[3]);
			OutConfig.bNoRetentions = L[4] != 0;
			OutSeed = int32(L[5]);
			bConfig = true;
		}
		else if (L.Key == TEXT("humans")) OutConfig.Humans = L.List();
		else if (L.Key == TEXT("carried"))
		{
			const int32 T = int32(L[0]);
			if (T < 0 || T > 64) return false;
			if (OutConfig.Carried.Num() <= T) OutConfig.Carried.SetNum(T + 1);
			const TArray<int32> Flat = L.List(1);
			for (int32 I = 0; I + 1 < Flat.Num(); I += 2) OutConfig.Carried[T].Add({ Flat[I], Flat[I + 1], Flat[I + 1], false, false, 0 });
		}
	}
	return bConfig;
}

bool FAuction::LoadState(const FString& Text)
{
	using namespace AuctionEnginePrivate;
	FAuctionConfig Check;
	int32 SavedSeed = 0;
	if (!ReadSaveHeader(Text, Check, SavedSeed) || SavedSeed != Seed || Check.Humans != Config.Humans) return false;
	const int32 NP = AuctionData::Players().Num();
	for (FAuctionTeam& T : Teams) { T.Squad.Reset(); }
	for (FAuctionPlan& P : Plans) P = FAuctionPlan();
	for (TMap<int32, FAuctionWish>& W : WishList) W.Reset();
	Sets.Reset();
	Events.Reset();
	Trades.Reset();
	SoldTo.Reset();
	Unsold.Reset();
	HumanNominations.Reset();
	for (const FLine& L : Parse(Text))
	{
		if (L.Key == TEXT("state"))
		{
			Phase = EAuctionPhase(L[0]);
			Clock = double(L[1]) / 1000.0;
			SetIndex = int32(L[2]); LotInSet = int32(L[3]); LotsHeld = int32(L[4]); Day = int32(L[5]);
			Lot = int32(L[6]); Price = int32(L[7]); Holder = int32(L[8]); LastSoldTo = int32(L[9]);
			UnsoldRounds = int32(L[10]); FirstDayTwoSet = int32(L[11]); bNoRetentions = L[12] != 0; bTradeWindowDone = L[13] != 0;
			Rng.Initialize(int32(L[14]));
		}
		else if (L.Key == TEXT("owner") && L.List().Num() == NP) Owner = L.List();
		else if (L.Key == TEXT("contract") && L.List().Num() == NP) Contract = L.List();
		else if (L.Key == TEXT("former") && L.List().Num() == NP) FormerTeam = L.List();
		else if (L.Key == TEXT("trade")) Trades.Add({ int32(L[0]), int32(L[1]), int32(L[2]), int32(L[3]) });
		else if (L.Key == TEXT("team") && Teams.IsValidIndex(int32(L[0])))
		{
			FAuctionTeam& T = Teams[int32(L[0])];
			T.Purse = int32(L[1]); T.RtmCards = int32(L[2]); T.KeptCapped = int32(L[3]); T.KeptUncapped = int32(L[4]);
			T.Timeouts = int32(L[5]); T.bRetained = L[6] != 0;
		}
		else if (L.Key == TEXT("signing") && Teams.IsValidIndex(int32(L[0])))
		{
			Teams[int32(L[0])].Squad.Add({ int32(L[1]), int32(L[2]), int32(L[3]), L[4] != 0, L[5] != 0, int32(L[6]) });
			SoldTo.Add(int32(L[1]), int32(L[0]));
		}
		else if (L.Key == TEXT("wish") && WishList.IsValidIndex(int32(L[0]))) WishList[int32(L[0])].Add(int32(L[1]), { int32(L[2]), L[3] != 0 });
		else if (L.Key == TEXT("target") && Plans.IsValidIndex(int32(L[0])))
			Plans[int32(L[0])].Targets.Add({ int32(L[1]), EAuctionSlot(L[2]), int32(L[3]), int32(L[4]) });
		else if (L.Key == TEXT("boost") && Plans.IsValidIndex(int32(L[0])) && L[1] >= 0 && L[1] < int32(EAuctionSlot::Count))
			Plans[int32(L[0])].SlotBoost[L[1]] = float(L[2]) / 1000.f;
		else if (L.Key == TEXT("unsold")) Unsold = L.List();
		else if (L.Key == TEXT("nominated")) for (int32 P : L.List()) HumanNominations.Add(P);
		else if (L.Key == TEXT("event"))
			Events.Add({ EAuctionEvent(L[0]), int32(L[1]), int32(L[2]), int32(L[3]), double(L[4]) / 1000.0, int32(L[5]) });
	}
	// Sets carry their names, which have spaces: parsed apart from the numbers.
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	for (const FString& Raw : Lines)
	{
		if (!Raw.StartsWith(TEXT("set "))) continue;
		TArray<FString> W;
		Raw.ParseIntoArrayWS(W);
		if (W.Num() < 5) return false;
		FAuctionSet& S = Sets.AddDefaulted_GetRef();
		S.Code = W[1];
		S.Name = W[2].Replace(TEXT("_"), TEXT(" "));
		S.bAccelerated = W[3] == TEXT("1");
		const int32 Count = FCString::Atoi(*W[4]);
		for (int32 I = 0; I < Count && 5 + I < W.Num(); ++I) S.Players.Add(FCString::Atoi(*W[5 + I]));
	}
	// Between lots: whatever was under way resumes at the next lot.
	if (Phase != EAuctionPhase::Finished && Phase != EAuctionPhase::Break && Phase != EAuctionPhase::SetIntro) Phase = EAuctionPhase::Hammer;
	PhaseStart = Clock;
	RtmTeam = PendingTeam = INDEX_NONE;
	bFastLot = bFastSet = false;
	for (float& V : TeamValueCache) V = -1.f;
	UpdateMarket();
	return true;
}
