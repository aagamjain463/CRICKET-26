#include "AuctionEngine.h"

namespace
{
	// A 25-man squad the AI builds toward, by role: batters, keepers, all-rounders, quicks, spinners.
	constexpr int32 RoleTarget[] = { 6, 3, 6, 6, 4 };
	constexpr int32 CappedSetSize = 8, UncappedSetSize = 10, MarqueeSize = 6, MaxUnsoldRounds = 3;

	const TCHAR* SetNoun(EAuctionRole Role)
	{
		static const TCHAR* Nouns[] = { TEXT("BATTERS"), TEXT("WICKETKEEPERS"), TEXT("ALL-ROUNDERS"), TEXT("FAST BOWLERS"), TEXT("SPINNERS") };
		return Nouns[int32(Role)];
	}
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

FAuction::FAuction(int32 HumanTeam, int32 InSeed) : Human(HumanTeam), Rng(InSeed), Seed(InSeed)
{
	const int32 N = AuctionData::Franchises().Num();
	Teams.SetNum(N);
	MaxThisLot.Init(0, N);
	EnterAt.Init(0, N);
}

// ---- Retentions ------------------------------------------------------------------------------------------------

TArray<int32> FAuction::RetentionCandidates(int32 Team) const
{
	TArray<int32> Out;
	const FString& Code = AuctionData::Franchises()[Team].Code;
	for (const FAuctionPlayer& P : AuctionData::Players()) if (P.Team2026 == Code) Out.Add(P.Id);
	Out.Sort([this](int32 A, int32 B) { return BaseValue(A) > BaseValue(B); });
	return Out;
}

bool FAuction::CanRetain(int32 Team, const TArray<int32>& Players, FString* Why) const
{
	auto Fail = [Why](const TCHAR* Reason) { if (Why) *Why = Reason; return false; };
	if (!Teams.IsValidIndex(Team)) return Fail(TEXT("no such franchise"));
	if (Teams[Team].bRetained) return Fail(TEXT("retentions are already locked in"));
	if (Players.Num() > AuctionRules::KeepMax) return Fail(TEXT("at most six retentions"));
	const TArray<int32> Squad = RetentionCandidates(Team);
	int32 Capped = 0, Uncapped = 0;
	for (int32 I = 0; I < Players.Num(); ++I)
	{
		if (!Squad.Contains(Players[I])) return Fail(TEXT("only players from your 2026 squad can be retained"));
		for (int32 J = 0; J < I; ++J) if (Players[J] == Players[I]) return Fail(TEXT("a player is listed twice"));
		Player(Players[I]).bCapped ? ++Capped : ++Uncapped;
	}
	if (Capped > AuctionRules::KeepCappedMax) return Fail(TEXT("at most five capped players"));
	if (Uncapped > AuctionRules::KeepUncappedMax) return Fail(TEXT("at most two uncapped players"));
	return true;
}

int32 FAuction::RetentionCost(const TArray<int32>& Players)
{
	int32 Cost = 0, Capped = 0;
	for (int32 P : Players) Cost += Player(P).bCapped ? AuctionRules::CappedRetentionCost(Capped++) : AuctionRules::UncappedRetention;
	return Cost;
}

TArray<int32> FAuction::AiRetentions(int32 Team) const
{
	const FAuctionFranchise& F = AuctionData::Franchises()[Team];
	TArray<int32> Keep;
	int32 Capped = 0, Uncapped = 0;
	for (int32 P : RetentionCandidates(Team))
	{
		if (Keep.Num() >= AuctionRules::KeepMax) break;
		const FAuctionPlayer& X = Player(P);
		const bool bRoom = X.bCapped ? Capped < AuctionRules::KeepCappedMax : Uncapped < AuctionRules::KeepUncappedMax;
		if (!bRoom) continue;
		const int32 Cost = X.bCapped ? AuctionRules::CappedRetentionCost(Capped) : AuctionRules::UncappedRetention;
		// A front office keeps a player when the market would charge about as much, sooner if he is a club man.
		const float Worth = BaseValue(P) * F.Loyalty * (X.bRetained2026 ? 1.1f : 0.9f);
		if (Worth < Cost * 0.75f) continue;
		Keep.Add(P);
		X.bCapped ? ++Capped : ++Uncapped;
	}
	return Keep;
}

bool FAuction::Retain(int32 Team, const TArray<int32>& Players)
{
	if (Phase != EAuctionPhase::Retention || !CanRetain(Team, Players)) return false;
	FAuctionTeam& T = Teams[Team];
	int32 Capped = 0;
	for (int32 P : Players)
	{
		const bool bCapped = Player(P).bCapped;
		AddSigning(Team, P, bCapped ? AuctionRules::CappedRetentionCost(Capped++) : AuctionRules::UncappedRetention, true, false);
		bCapped ? ++T.KeptCapped : ++T.KeptUncapped;
	}
	T.RtmCards = AuctionRules::KeepMax - Players.Num();
	T.bRetained = true;
	return true;
}

void FAuction::BeginAuction()
{
	if (Phase != EAuctionPhase::Retention) return;
	for (int32 T = 0; T < Teams.Num(); ++T)
	{
		if (bNoRetentions)
		{
			Teams[T].Squad.Reset();
			Teams[T].Purse = AuctionRules::Purse;
			Teams[T].RtmCards = AuctionRules::KeepMax;
			Teams[T].KeptCapped = 0;
			Teams[T].KeptUncapped = 0;
			Teams[T].bRetained = true;
		}
		else if (!Teams[T].bRetained)
		{
			Retain(T, IsHuman(T) ? TArray<int32>() : AiRetentions(T));
		}
	}
	if (bNoRetentions) SoldTo.Reset();
	BuildSets();
	SetIndex = 0;
	LotInSet = -1;
	SetPhase(EAuctionPhase::SetIntro);
	Emit(EAuctionEvent::SetOpened);
}

void FAuction::BuildSets()
{
	// The pool: everyone not retained, most valuable first.
	TArray<int32> Pool;
	for (const FAuctionPlayer& P : AuctionData::Players()) if (!SoldTo.Contains(P.Id)) Pool.Add(P.Id);
	Pool.Sort([this](int32 A, int32 B) { return BaseValue(A) > BaseValue(B); });

	auto Shuffle = [this](TArray<int32>& A) { for (int32 I = A.Num() - 1; I > 0; --I) A.Swap(I, Rng.RandRange(0, I)); };

	// Two marquee sets of six: the biggest capped names at the top base price.
	TArray<int32> Marquee;
	for (int32 P : Pool) if (Marquee.Num() < 2 * MarqueeSize && Player(P).bCapped && Player(P).Base >= 200) Marquee.Add(P);
	for (int32 M = 0; M < 2; ++M)
	{
		FAuctionSet& S = Sets.AddDefaulted_GetRef();
		S.Code = FString::Printf(TEXT("M%d"), M + 1);
		S.Name = FString::Printf(TEXT("MARQUEE SET %d"), M + 1);
		for (int32 I = M * MarqueeSize; I < FMath::Min(Marquee.Num(), (M + 1) * MarqueeSize); ++I) S.Players.Add(Marquee[I]);
		Shuffle(S.Players);
	}

	// Then capped sets by role, then uncapped, round after round, as the IPL draws them. The first round is held
	// in full; from the second the auction is accelerated and only nominated players come to the table.
	TArray<int32> ByRole[2][int32(EAuctionRole::Count)];
	for (int32 P : Pool) if (!Marquee.Contains(P)) ByRole[Player(P).bCapped ? 0 : 1][int32(Player(P).Role)].Add(P);
	for (int32 Round = 0;; ++Round)
	{
		bool bAny = false;
		for (int32 Capped = 0; Capped < 2; ++Capped)
			for (int32 R = 0; R < int32(EAuctionRole::Count); ++R)
			{
				const int32 Size = Capped == 0 ? CappedSetSize : UncappedSetSize;
				const TArray<int32>& List = ByRole[Capped][R];
				if (Round * Size >= List.Num()) continue;
				bAny = true;
				FAuctionSet& S = Sets.AddDefaulted_GetRef();
				S.Code = FString::Printf(TEXT("%s%s%d"), Capped ? TEXT("U") : TEXT(""), AuctionRules::RoleCode(EAuctionRole(R)), Round + 1);
				S.Name = FString::Printf(TEXT("%s %s %d"), Capped ? TEXT("UNCAPPED") : TEXT("CAPPED"), SetNoun(EAuctionRole(R)), Round + 1);
				S.bAccelerated = Round > 0;
				for (int32 I = Round * Size; I < FMath::Min(List.Num(), (Round + 1) * Size); ++I) S.Players.Add(List[I]);
				Shuffle(S.Players);
			}
		if (!bAny) break;
	}
}

// ---- Flow ------------------------------------------------------------------------------------------------------

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
				if (MaxThisLot[RtmTeam] >= Price) { Emit(EAuctionEvent::RtmUsed, RtmTeam, Price); SetPhase(EAuctionPhase::RtmRaise); }
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
			if (!bAllFull && UnsoldRounds < MaxUnsoldRounds && !Unsold.IsEmpty() && (bShort || bWanted))
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
		if (!IsHuman(T) && Valuation(T, PlayerId) >= Player(PlayerId).Base) return true;
	return false;
}

void FAuction::Nominate(int32 PlayerId, bool bOn)
{
	if (bOn) HumanNominations.Add(PlayerId);
	else HumanNominations.Remove(PlayerId);
}

void FAuction::OpenLot(int32 PlayerId)
{
	Lot = PlayerId;
	Price = 0;
	Holder = RtmTeam = PendingTeam = LastSoldTo = INDEX_NONE;
	HammerStage = 0;
	Bidders.Reset();
	OutCalled.Reset();
	++LotsHeld;
	const int32 Base = Player(PlayerId).Base;
	for (int32 T = 0; T < Teams.Num(); ++T)
	{
		MaxThisLot[T] = IsHuman(T) ? 0 : Valuation(T, PlayerId);
		// Patient sides sit on their paddles and come in once the early bidders have pushed the price up.
		const float Wait = AuctionData::Franchises()[T].Patience * FMath::Square(Rng.FRand());
		EnterAt[T] = Base + FMath::RoundToInt(Wait * 0.7f * FMath::Max(0, MaxThisLot[T] - Base));
	}
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
		if (IsHuman(T) || T == Holder || OutCalled.Contains(T)) continue;
		if (MaxThisLot[T] < Ask || !CanAfford(T, Lot, Ask))
		{
			// A side that was in the bidding and has reached its limit shakes its head.
			if (Bidders.Contains(T) && !OutCalled.Contains(T)) { OutCalled.Add(T); Emit(EAuctionEvent::Out, T, Price); }
			continue;
		}
		// Patient sides wait for the price to climb, but a lot a side wants always gets its opening paddle: with no bid
		// yet the price never climbs, and a star whose every suitor waited would go unsold at his base.
		if (HammerStage == 0 && Holder != INDEX_NONE && Ask < EnterAt[T]) continue;
		// A table can walk away before its absolute ceiling after a long duel; late entrants still get the final calls.
		const float Headroom = float(MaxThisLot[T] - Ask) / FMath::Max(1, MaxThisLot[T]);
		if (Bidders.Contains(T) && Headroom < 0.12f && Rng.FRand() > Headroom / 0.12f)
		{
			OutCalled.Add(T);
			Emit(EAuctionEvent::Out, T, Price);
			continue;
		}
		float W = 0.15f + Headroom;
		if (Bidders.Contains(T)) W += 0.9f; // "back with Kolkata": the sides in the duel keep going
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
	const float Close = float(Ask) / float(MaxThisLot[Chosen]);
	float Delay = FMath::Lerp(0.7f, 1.6f, Rng.FRand());
	if (Holder == INDEX_NONE) Delay = OpeningTime + 1.5f * Rng.FRand();
	else if (Close > 0.75f) Delay += FMath::Square((Close - 0.75f) / 0.25f) * FMath::Lerp(2.f, 6.f, Rng.FRand());
	if (HammerStage > 0) Delay = FMath::Min(Delay, FMath::Lerp(0.4f, 1.4f, Rng.FRand()));
	if (Delay > 3.f) Emit(EAuctionEvent::Huddle, Chosen, Ask);
	PendingTeam = Chosen;
	PendingAt = Clock + Delay * Pace();
}

void FAuction::PlaceBid(int32 Team, int32 Amount)
{
	Price = Amount;
	Holder = Team;
	Bidders.Add(Team);
	OutCalled.Remove(Team);
	LastBidAt = Clock;
	HammerStage = 0;
	Emit(EAuctionEvent::Bid, Team, Amount);
}

bool FAuction::CanHumanBid() const
{
	return Teams.IsValidIndex(Human) && Phase == EAuctionPhase::Bidding && Holder != Human && CanAfford(Human, Lot, AskPrice());
}

bool FAuction::HumanBid()
{
	if (!CanHumanBid()) return false;
	PendingTeam = INDEX_NONE; // the human's paddle beat whoever was about to bid
	PlaceBid(Human, AskPrice());
	ScheduleAi();
	return true;
}

void FAuction::Hammer()
{
	const int32 R = AuctionData::FranchiseIndex(Player(Lot).Team2026);
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
	if (T.RtmCards <= 0) return false;
	const bool bRoom = Player(Lot).bCapped ? T.KeptCapped < AuctionRules::KeepCappedMax : T.KeptUncapped < AuctionRules::KeepUncappedMax;
	return bRoom && CanAfford(Team, Lot, Price);
}

int32 FAuction::AiFinalRaise(int32 Team) const
{
	const int32 Max = MaxThisLot[Team];
	if (Max <= Price) return Price;
	FRandomStream RaiseRng(Seed + Lot * 31 + Team * 7919);
	if (RaiseRng.FRand() < 0.2f) return Price; // Some buyers hold their bid and risk the match.
	const int32 Target = Price + FMath::RoundToInt((Max - Price) * FMath::Lerp(0.25f, 0.85f, RaiseRng.FRand()));
	int32 Raise = Price;
	while (AuctionRules::NextBid(Raise) <= Target && CanAfford(Team, Lot, AuctionRules::NextBid(Raise))) Raise = AuctionRules::NextBid(Raise);
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
	if (Phase != EAuctionPhase::Retention && Phase != EAuctionPhase::Finished) bFastSet = true;
}

void FAuction::Sell(int32 Team, int32 Amount, bool bRtm)
{
	AddSigning(Team, Lot, Amount, false, bRtm);
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
	SetPhase(EAuctionPhase::Hammer);
}

void FAuction::AddSigning(int32 Team, int32 PlayerId, int32 Amount, bool bRetained, bool bRtm)
{
	FAuctionTeam& T = Teams[Team];
	T.Squad.Add({ PlayerId, Amount, bRetained, bRtm });
	T.Purse -= Amount;
	SoldTo.Add(PlayerId, Team);
}

// ---- Money and valuation ---------------------------------------------------------------------------------------

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

int32 FAuction::BaseValue(int32 PlayerId) const
{
	// What the open market pays for this quality: steeply convex, as the IPL is (a 90 costs five times a 75).
	const FAuctionPlayer& P = Player(PlayerId);
	const float Q = FMath::Clamp((P.Overall() - 50.f) / 45.f, 0.f, 1.2f);
	float V = 30.f + 3400.f * FMath::Pow(Q, 2.6f);
	if (P.IsOverseas()) V *= 0.8f;
	else if (P.bCapped) V *= 1.3f; // capped Indians are the scarce currency of every auction
	if (!P.bCapped) V *= 0.75f;
	if (P.Age >= 35) V *= 0.65f;
	else if (P.Age >= 32) V *= 0.85f;
	else if (P.Age > 0 && P.Age < 25) V *= 1.1f;
	return FMath::RoundToInt(V);
}

float FAuction::Need(int32 Team, EAuctionRole Role) const
{
	const FAuctionTeam& T = Teams[Team];
	const float Ratio = float(T.CountRole(Role)) / RoleTarget[int32(Role)];
	float M = FMath::Clamp(1.45f - 1.1f * Ratio, 0.3f, 1.45f);
	if (T.Squad.Num() >= AuctionRules::SquadMax - 2) M *= 0.6f;
	return M;
}

float FAuction::Noise(int32 Team, int32 PlayerId) const
{
	// Each front office rates each player a little differently, the same way every time it looks at him.
	FRandomStream S(int32(HashCombine(GetTypeHash(Team * 7919 + PlayerId), GetTypeHash(Seed))));
	const float Gauss = (S.FRand() + S.FRand() + S.FRand() - 1.5f) * 2.f;
	return FMath::Exp(0.2f * Gauss);
}

int32 FAuction::Valuation(int32 Team, int32 PlayerId) const
{
	if (IsHuman(Team) || !Teams.IsValidIndex(Team)) return 0;
	const FAuctionFranchise& F = AuctionData::Franchises()[Team];
	const FAuctionPlayer& P = Player(PlayerId);
	const FAuctionTeam& T = Teams[Team];
	if (!CanAfford(Team, PlayerId, P.Base)) return 0;
	float V = BaseValue(PlayerId) * F.RoleBias[int32(P.Role)] * Need(Team, P.Role) * F.Aggression * Noise(Team, PlayerId);
	if (P.IsOverseas()) V *= F.OverseasBias;
	if (P.Overall() >= 85) V *= F.StarBias;
	if (P.Age > 0 && P.Age < 25) V *= F.YouthBias;
	if (P.Team2026 == F.Code) V *= F.Loyalty;
	// The marquee sets are where sides buy their captains, with full purses: the 2025 records all fell there.
	if (const FAuctionSet* S = CurrentSet(); S && S->Code.StartsWith(TEXT("M")) && S->Players.Contains(PlayerId)) V *= 1.2f;
	// Money in hand per place still to fill sets how freely a side spends: rich early, frugal late.
	const float Fair = float(MaxBid(Team)) / FMath::Max(1, 22 - T.Squad.Num());
	V *= FMath::Clamp(FMath::Sqrt(Fair / 330.f), 0.6f, 1.6f);
	if (P.IsOverseas() && P.Role == EAuctionRole::Batter && P.Age >= 37) V = FMath::Min(V, 1.25f * P.Base);
	if (P.Price2026 > 0 && P.Ipl.Matches < 20) V = FMath::Min(V, FMath::Max(2.f * P.Base, 3.f * P.Price2026));
	V = FMath::Min(V, FMath::Min(3000.f, 0.4f * MaxBid(Team)));
	if (V < P.Base)
	{
		// A side short of 18 in the accelerated rounds takes a useful player at his base price.
		const FAuctionSet* S = CurrentSet();
		const bool bMustFill = T.Squad.Num() < AuctionRules::SquadMin && S && S->bAccelerated;
		if (!bMustFill || V < 0.6f * P.Base) return 0;
		V = P.Base;
	}
	return FMath::Min(FMath::RoundToInt(V), MaxBid(Team));
}
