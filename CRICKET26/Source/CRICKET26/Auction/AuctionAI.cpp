// The front offices: how each AI side values a player, plans its auction, keeps and trades, and how the analysts
// read a squad. The model, in three steps:
//
//   1. What a player adds. A side is its best twelve (eleven and the Impact Player, at most four overseas), scored as
//      seven batting and five bowling places with the composition a T20 side needs (a keeper, two openers, a
//      finisher, two quicks with one for the death, a spinner, a captain), plus its bench. A player is worth what he
//      adds to that: a fifth overseas star or a third keeper adds little, the only keeper in the pool a lot.
//   2. Over replacement. Skill is counted over the level any side can still find in the pool at base price, so the
//      value of a middling player falls as the pool deepens and rises late when it empties.
//   3. In money. The room's spendable money over what the rest of the pool is worth over replacement gives a price
//      per point (the fantasy-auction method), recomputed at every lot: money left unspent early makes the late lots
//      dearer, and a splurge on the marquee makes the rest cheaper.
//
// Every side then applies its own style (AuctionData.cpp), its plan (targets by slot, first choices worth a little
// more, and more again once a first choice has gone elsewhere), a noise of its own, and prudence about the places it
// still has to fill.

#include "AuctionEngine.h"

namespace AuctionAIPrivate
{
	// A player's contribution to his place, by rating: steep, as the market is (a 90 is worth about twice an 80 and
	// six times a 70). Normalised so a 90 contributes 100.
	constexpr float Curve = 2.4f, CurveFloor = 50.f, CurveScale = 69.98f;
	// Batting places one to seven and bowling places one to five, by weight; the sixth bowler counts a little.
	constexpr float BatWeight[] = { 1.f, 1.f, 1.f, 1.f, 0.9f, 0.75f, 0.55f };
	constexpr float BowlWeight[] = { 1.f, 1.f, 1.f, 1.f, 0.9f };
	constexpr float SixthBowler = 0.35f, BenchWeight = 0.12f;
	constexpr float Reserve = 55.f; // the uncapped reserve a side can always sign at 30 lakh
	constexpr int32 BenchCounted = 6, OverseasCover = 4;
	constexpr float OverseasWeight = 0.85f;
	// What a side is short of, in contribution points (a 90 is 100).
	constexpr float NoKeeper = 22.f, PaceShort = 8.f, NoSpin = 7.f, OpenerShort = 4.f, NoFinisher = 4.f, NoDeath = 6.f,
		NoCaptain = 5.f, Variety = 2.f;
	// The price per point is the room's money over the pool's value, scaled by this: at the table the room bids past
	// the fair split of its money (it chases its first choices and fills places late), where the opening estimates
	// behind retention asks and the set order take the fair split.
	constexpr float MarketKappa = 1.2f, FairKappa = 1.f;
	constexpr int32 SquadAim = 20, OverseasAim = 7; // the places a side expects to fill, and how many from overseas

	/** A player's yearly change in skill by age: growth into the mid-twenties, a plateau, then a fade after 33. */
	float YearDelta(int32 Age)
	{
		return Age <= 23 ? 1.5f : Age <= 26 ? 0.5f : Age <= 33 ? 0.f : Age <= 36 ? -1.f : Age <= 39 ? -2.f : -3.f;
	}

	/** The next mega-auction season after Season. */
	int32 NextMega(int32 Season)
	{
		int32 S = Season + 1;
		while (!AuctionRules::IsMegaSeason(S)) ++S;
		return S;
	}

	/** The contribution curve before replacement is taken off. */
	float Raw(float R) { return R <= CurveFloor ? 0.f : FMath::Pow(R - CurveFloor, Curve) / CurveScale; }

	float Unit(uint32 Hash) { return float(Hash & 0xFFFFFF) / float(0x1000000); }

	/** Ascending insertion sort of a few values (a side is twelve). */
	void SortDesc(float* V, int32 N)
	{
		for (int32 I = 1; I < N; ++I)
		{
			const float X = V[I];
			int32 J = I - 1;
			while (J >= 0 && V[J] < X) { V[J + 1] = V[J]; --J; }
			V[J + 1] = X;
		}
	}

	/** A player as the squad model sees him: value at the crease and with the ball over replacement, and his marks. */
	struct FCand
	{
		int32 Id = INDEX_NONE;
		float Bat = 0.f, Bowl = 0.f, Best = 0.f;
		float Cover = 0.f; // as a reserve: over a base-price uncapped player, not over the market's replacement
		bool bOverseas = false, bKeeper = false, bPace = false, bSpin = false;
		uint32 Tags = 0;
	};

	struct FSideScore
	{
		float Value = 0.f;
		bool bKeeper = false, bSpin = false, bDeath = false, bFinisher = false, bCaptain = false;
		int32 Pace = 0, Openers = 0;
	};

	FSideScore ScoreSide(const TArray<FCand>& C, const int32* Side, int32 N)
	{
		FSideScore S;
		float Bats[AuctionRules::Side + 1], Bowls[AuctionRules::Side + 1];
		int32 Bowlers[AuctionRules::Side + 1];
		for (int32 I = 0; I < N; ++I)
		{
			const FCand& X = C[Side[I]];
			Bats[I] = X.Bat;
			Bowls[I] = X.Bowl;
			Bowlers[I] = Side[I];
			S.bKeeper |= X.bKeeper;
			S.bFinisher |= (X.Tags & AuctionTags::Finisher) != 0;
			S.bCaptain |= (X.Tags & AuctionTags::Captain) != 0;
			S.Openers += (X.Tags & AuctionTags::Opener) != 0;
		}
		SortDesc(Bats, N);
		for (int32 I = 0; I < FMath::Min(N, 7); ++I) S.Value += BatWeight[I] * Bats[I];
		// The bowlers, best first, carrying who they are for the pace and spin count.
		for (int32 I = 1; I < N; ++I)
		{
			const float X = Bowls[I];
			const int32 Who = Bowlers[I];
			int32 J = I - 1;
			while (J >= 0 && Bowls[J] < X) { Bowls[J + 1] = Bowls[J]; Bowlers[J + 1] = Bowlers[J]; --J; }
			Bowls[J + 1] = X;
			Bowlers[J + 1] = Who;
		}
		bool bLeftArm = false, bWrist = false;
		for (int32 I = 0; I < FMath::Min(N, 5); ++I)
		{
			S.Value += BowlWeight[I] * Bowls[I];
			if (Bowls[I] <= 0.f) continue;
			const FCand& X = C[Bowlers[I]];
			S.Pace += X.bPace;
			S.bSpin |= X.bSpin;
			S.bDeath |= (X.Tags & AuctionTags::DeathPace) != 0;
			bLeftArm |= (X.Tags & AuctionTags::LeftArmPace) != 0;
			bWrist |= (X.Tags & (AuctionTags::WristSpin | AuctionTags::Mystery)) != 0;
		}
		if (N > 5) S.Value += SixthBowler * Bowls[5];
		if (!S.bKeeper) S.Value -= NoKeeper;
		S.Value -= PaceShort * FMath::Max(0, 2 - S.Pace);
		if (!S.bSpin) S.Value -= NoSpin;
		S.Value -= OpenerShort * FMath::Max(0, 2 - S.Openers);
		if (!S.bFinisher) S.Value -= NoFinisher;
		if (!S.bDeath) S.Value -= NoDeath;
		if (!S.bCaptain) S.Value -= NoCaptain;
		S.Value += Variety * (int32(bLeftArm) + int32(bWrist));
		return S;
	}
}

// ---- Skill and the market ----------------------------------------------------------------------------------------

float FAuction::Contribution(float Rating, bool bOverseas) const
{
	using namespace AuctionAIPrivate;
	return FMath::Max(0.f, Raw(Rating) - Raw(bOverseas ? ReplOverseas : ReplIndian));
}

void FAuction::PriceMarket(const TArray<int32>& Left, int32 IndianDemand, int32 OverseasDemand, float Money, float Kappa)
{
	// Replacement is counted separately for Indian and overseas players: seven of every eleven must be Indian, so the
	// Indian pool runs thin sooner and its replacement level is lower (capped Indians are the scarce currency of every
	// auction), while overseas quality is deep and only four can play.
	TArray<float> Level[2];
	for (int32 P : Left) Level[Player(P).IsOverseas()].Add(FMath::Max(BatNow[P], BowlNow[P]));
	const int32 Demand[2] = { FMath::Max(IndianDemand, 5), FMath::Max(OverseasDemand, 3) };
	float Repl[2];
	for (int32 G = 0; G < 2; ++G)
	{
		Level[G].Sort([](float A, float B) { return A > B; });
		Repl[G] = FMath::Clamp(Level[G].IsValidIndex(Demand[G]) ? Level[G][Demand[G]] : (Level[G].IsEmpty() ? 55.f : Level[G].Last()), 50.f, 76.f);
	}
	if (!FMath::IsNearlyEqual(Repl[0], ReplIndian, 0.01f) || !FMath::IsNearlyEqual(Repl[1], ReplOverseas, 0.01f))
		for (float& V : TeamValueCache) V = -1.f;
	ReplIndian = Repl[0];
	ReplOverseas = Repl[1];
	// The room's money over what the players it will buy are worth over replacement: the price of a point.
	TArray<float> Values[2];
	for (int32 P : Left) Values[Player(P).IsOverseas()].Add(GenericValue(P));
	float Worth = 0.f;
	for (int32 G = 0; G < 2; ++G)
	{
		Values[G].Sort([](float A, float B) { return A > B; });
		for (int32 I = 0; I < FMath::Min(Demand[G], Values[G].Num()); ++I) Worth += Values[G][I];
	}
	LakhPerPoint = Kappa * FMath::Max(0.f, Money - (Demand[0] + Demand[1]) * AuctionRules::MinBase) / FMath::Max(1.f, Worth);
}

void FAuction::InitMarket()
{
	using namespace AuctionAIPrivate;
	const TArray<FAuctionPlayer>& All = AuctionData::Players();
	BatNow.Init(0.f, All.Num());
	BowlNow.Init(0.f, All.Num());
	// Skill over the contract: a mega auction buys three seasons, a mini auction the seasons to the next mega.
	const int32 Horizon = IsMini() ? FMath::Max(1, NextMega(Config.Season) - Config.Season) : AuctionRules::MegaCycle;
	for (const FAuctionPlayer& X : All)
	{
		const int32 Now = AgeOf(X.Id);
		float Shift = 0.f; // since the roster was rated
		for (int32 A = X.Age + 1; A <= Now; ++A) Shift += YearDelta(A);
		// Each season of the contract counts at the skill he will have then, and a veteran may not be there at all:
		// from 38 a side stops counting on him, and his place falls back to a reserve's.
		float Bat = 0.f, Bowl = 0.f, Ahead = 0.f;
		for (int32 Y = 0; Y < Horizon; ++Y)
		{
			if (Y > 0) Ahead += YearDelta(Now + Y);
			const float Here = X.Age > 0 && Y > 0 ? FMath::Clamp(1.f - 0.15f * float(Now + Y - 37), 0.25f, 1.f) : 1.f;
			const float Drift = X.Age > 0 ? Shift + Ahead : 0.f;
			Bat += Here * FMath::Max(0.f, X.BatRating + Drift) + (1.f - Here) * FMath::Min(float(X.BatRating), Reserve);
			Bowl += Here * FMath::Max(0.f, X.BowlRating + Drift) + (1.f - Here) * FMath::Min(float(X.BowlRating), Reserve);
		}
		BatNow[X.Id] = Bat / Horizon;
		BowlNow[X.Id] = Bowl / Horizon;
	}
	// The opening market: everyone in the pool, every purse full. What a player would fetch in it is his estimate,
	// which orders the sets and prices retentions.
	TArray<int32> Everyone;
	for (const FAuctionPlayer& X : All) Everyone.Add(X.Id);
	const float Money = float(Teams.Num()) * float(Config.Purse - AuctionRules::SquadMin * AuctionRules::MinBase);
	PriceMarket(Everyone, Teams.Num() * (SquadAim - OverseasAim), Teams.Num() * OverseasAim, Money, FairKappa);
	Estimate.Init(0, All.Num());
	for (const FAuctionPlayer& X : All) Estimate[X.Id] = ExpectedPrice(X.Id);
}

int32 FAuction::Aim() const
{
	return IsMini() ? AuctionRules::SquadMax - 2 : AuctionAIPrivate::SquadAim;
}

TArray<int32> FAuction::Pool() const
{
	TArray<int32> Out;
	for (const FAuctionPlayer& X : AuctionData::Players())
		if (!SoldTo.Contains(X.Id) && (Phase != EAuctionPhase::Retention || OwnerOf(X.Id) == INDEX_NONE)) Out.Add(X.Id);
	return Out;
}

void FAuction::UpdateMarket()
{
	using namespace AuctionAIPrivate;
	int32 Indian = 0, Overseas = 0;
	float Money = 0.f;
	for (int32 T = 0; T < Teams.Num(); ++T)
	{
		const int32 Os = FMath::Max(0, OverseasAim - Teams[T].Overseas());
		Overseas += Os;
		Indian += FMath::Max(0, Aim() - Teams[T].Squad.Num() - Os);
		Money += float(FMath::Max(0, MaxBid(T)));
	}
	PriceMarket(Pool(), Indian, Overseas, Money, MarketKappa);
}

float FAuction::GenericValue(int32 PlayerId) const
{
	// What he adds to a side with the place open: his better suit, and an all-rounder's second one in part, with the
	// marks sides pay a little more for.
	const FAuctionPlayer& X = Player(PlayerId);
	const float B = Contribution(BatNow[PlayerId], X.IsOverseas()), W = Contribution(BowlNow[PlayerId], X.IsOverseas());
	float V = 0.f;
	switch (X.Role)
	{
	case EAuctionRole::Batter: V = B; break;
	case EAuctionRole::Keeper: V = B + 6.f; break;
	case EAuctionRole::Pace:
	case EAuctionRole::Spin: V = W; break;
	default: V = FMath::Max(B, W) + 0.6f * FMath::Min(B, W); break;
	}
	if (V <= 0.f) return 0.f;
	if (X.Has(AuctionTags::Captain)) V += 4.f;
	if (X.Has(AuctionTags::DeathPace)) V += 3.f;
	if (X.Has(AuctionTags::Mystery | AuctionTags::WristSpin)) V += 2.f;
	if (X.Has(AuctionTags::Opener | AuctionTags::Finisher)) V += 1.5f;
	return V;
}

int32 FAuction::ExpectedPrice(int32 PlayerId) const
{
	return FMath::Max(Player(PlayerId).Base, AuctionRules::MinBase + FMath::RoundToInt(GenericValue(PlayerId) * LakhPerPoint));
}

int32 FAuction::MarketEstimate(int32 PlayerId) const
{
	return Estimate.IsValidIndex(PlayerId) ? Estimate[PlayerId] : 0;
}

// ---- The squad model ---------------------------------------------------------------------------------------------

float FAuction::SquadValue(const TArray<int32>& Squad, TArray<int32>* OutBest, TArray<FString>* OutHoles) const
{
	using namespace AuctionAIPrivate;
	TArray<FCand> C;
	C.Reserve(Squad.Num());
	for (int32 P : Squad)
	{
		const FAuctionPlayer& X = Player(P);
		FCand& Y = C.AddDefaulted_GetRef();
		Y.Id = P;
		Y.Bat = Contribution(BatNow[P], X.IsOverseas());
		Y.Bowl = X.Role == EAuctionRole::Batter || X.Role == EAuctionRole::Keeper ? 0.f : Contribution(BowlNow[P], X.IsOverseas());
		Y.Best = FMath::Max(Y.Bat, Y.Bowl);
		const float Skill = X.Role == EAuctionRole::Batter || X.Role == EAuctionRole::Keeper ? BatNow[P]
			: X.Role == EAuctionRole::Pace || X.Role == EAuctionRole::Spin ? BowlNow[P] : FMath::Max(BatNow[P], BowlNow[P]);
		Y.Cover = FMath::Max(0.f, Raw(Skill) - Raw(Reserve));
		Y.bOverseas = X.IsOverseas();
		Y.bKeeper = X.Role == EAuctionRole::Keeper;
		Y.bPace = X.BowlsPace();
		Y.bSpin = X.BowlsSpin();
		Y.Tags = X.Tags;
	}
	// The best twelve, greedily, then one pass of swaps with the bench.
	int32 Side[AuctionRules::Side + 1];
	int32 N = 0, Overseas = 0;
	TArray<uint8> In;
	In.Init(0, C.Num());
	const int32 Want = FMath::Min(AuctionRules::Side, C.Num());
	while (N < Want)
	{
		int32 Best = INDEX_NONE;
		float BestScore = -1e9f;
		for (int32 I = 0; I < C.Num(); ++I)
		{
			if (In[I] || (C[I].bOverseas && Overseas >= AuctionRules::XiOverseasMax)) continue;
			Side[N] = I;
			const float S = ScoreSide(C, Side, N + 1).Value;
			if (S > BestScore) { BestScore = S; Best = I; }
		}
		if (Best == INDEX_NONE) break;
		Side[N++] = Best;
		In[Best] = 1;
		Overseas += C[Best].bOverseas;
	}
	float Score = ScoreSide(C, Side, N).Value;
	for (int32 K = 0; K < N; ++K)
	{
		for (int32 I = 0; I < C.Num(); ++I)
		{
			if (In[I]) continue;
			const int32 Was = Side[K];
			if (C[I].bOverseas && !C[Was].bOverseas && Overseas >= AuctionRules::XiOverseasMax) continue;
			Side[K] = I;
			const float S = ScoreSide(C, Side, N).Value;
			if (S > Score + 0.01f)
			{
				Score = S;
				In[Was] = 0;
				In[I] = 1;
				Overseas += int32(C[I].bOverseas) - int32(C[Was].bOverseas);
			}
			else Side[K] = Was;
		}
	}
	// The bench: cover for injuries and form, the best six counting a little, and overseas cover much more: the four
	// overseas places are the side's sharpest, overseas players miss games for their countries, and sides carry
	// seven or eight to fill them.
	TArray<float> Bench, OverseasBench;
	int32 Keepers = 0;
	for (int32 I = 0; I < C.Num(); ++I)
	{
		Keepers += C[I].bKeeper;
		if (!In[I]) (C[I].bOverseas ? OverseasBench : Bench).Add(C[I].Cover);
	}
	OverseasBench.Sort([](float A, float B) { return A > B; });
	float Depth = 0.f;
	for (int32 I = 0; I < OverseasBench.Num(); ++I)
	{
		if (I < OverseasCover) Depth += OverseasWeight * OverseasBench[I];
		else Bench.Add(OverseasBench[I]);
	}
	Bench.Sort([](float A, float B) { return A > B; });
	for (int32 I = 0; I < FMath::Min(BenchCounted, Bench.Num()); ++I) Depth += BenchWeight * Bench[I];
	if (Keepers == 1) Depth -= 1.5f;

	if (OutBest)
	{
		OutBest->Reset();
		for (int32 K = 0; K < N; ++K) OutBest->Add(C[Side[K]].Id);
	}
	if (OutHoles)
	{
		OutHoles->Reset();
		const FSideScore S = ScoreSide(C, Side, N);
		if (!S.bKeeper) OutHoles->Add(TEXT("No wicketkeeper"));
		if (S.Openers < 2) OutHoles->Add(S.Openers == 0 ? TEXT("No specialist opener") : TEXT("Needs a second opener"));
		if (!S.bFinisher) OutHoles->Add(TEXT("No finisher"));
		if (S.Pace < 2) OutHoles->Add(S.Pace == 0 ? TEXT("No fast bowlers") : TEXT("Short of pace"));
		if (!S.bDeath) OutHoles->Add(TEXT("No death bowler"));
		if (!S.bSpin) OutHoles->Add(TEXT("No spinner"));
		if (!S.bCaptain) OutHoles->Add(TEXT("No proven captain"));
		if (N < AuctionRules::Side) OutHoles->Add(FString::Printf(TEXT("Only %d for the twelve"), N));
		if (Keepers == 1) OutHoles->Add(TEXT("No reserve keeper"));
	}
	return Score + Depth;
}

TArray<int32> FAuction::SquadIds(int32 Team) const
{
	TArray<int32> Out;
	if (Phase == EAuctionPhase::Retention && !Teams[Team].bRetained)
	{
		for (int32 P = 0; P < Owner.Num(); ++P) if (Owner[P] == Team) Out.Add(P);
		return Out;
	}
	for (const FAuctionSigning& S : Teams[Team].Squad) Out.Add(S.Player);
	return Out;
}

float FAuction::TeamValue(int32 Team) const
{
	if (!TeamValueCache.IsValidIndex(Team)) return 0.f;
	if (TeamValueCache[Team] < 0.f) TeamValueCache[Team] = FMath::Max(0.f, SquadValue(SquadIds(Team)));
	return TeamValueCache[Team];
}

float FAuction::Marginal(int32 Team, int32 PlayerId) const
{
	TArray<int32> With = SquadIds(Team);
	if (With.Contains(PlayerId)) return 0.f;
	With.Add(PlayerId);
	return FMath::Max(0.f, SquadValue(With) - TeamValue(Team));
}

// ---- A side's view -----------------------------------------------------------------------------------------------

float FAuction::StyleFactor(int32 Team, int32 PlayerId) const
{
	const FAuctionFranchise& F = AuctionData::Franchises()[Team];
	const FAuctionPlayer& X = Player(PlayerId);
	float V = F.RoleBias[int32(X.Role)] * F.Aggression;
	if (X.IsOverseas()) V *= F.OverseasBias;
	if (X.Overall() >= 85) V *= F.StarBias;
	const int32 Age = AgeOf(PlayerId);
	if (Age > 0 && Age < 25) V *= F.YouthBias;
	if (FormerTeam.IsValidIndex(PlayerId) && FormerTeam[PlayerId] == Team) V *= F.Loyalty;
	// The marquee sets are where sides buy their captains, with full purses: the 2025 records all fell there. Early
	// spenders go harder at them.
	if (const FAuctionSet* S = CurrentSet(); S && S->Code.StartsWith(TEXT("M")) && S->Players.Contains(PlayerId)) V *= 1.05f + 0.12f * F.Tempo;
	static const float Level[] = { 0.93f, 1.f, 1.02f };
	return V * Level[int32(Config.Difficulty)];
}

float FAuction::Noise(int32 Team, int32 PlayerId) const
{
	// Each front office rates each player a little differently, the same way every time it looks at him; the sharper
	// the difficulty, the closer the offices agree with the market.
	FRandomStream S(int32(HashCombine(GetTypeHash(Team * 7919 + PlayerId), GetTypeHash(Seed))));
	const float Gauss = (S.FRand() + S.FRand() + S.FRand() - 1.5f) * 2.f;
	static const float Sigma[] = { 0.24f, 0.18f, 0.12f };
	return FMath::Exp(Sigma[int32(Config.Difficulty)] * Gauss);
}

float FAuction::Scarcity(int32 Team, int32 PlayerId) const
{
	// The last of his kind in the pool costs more: a side that needs a keeper and sees one left pays for it.
	const float Mine = GenericValue(PlayerId);
	if (Mine <= 5.f) return 1.f;
	const EAuctionSlot Slot = SlotOf(PlayerId);
	int32 Like = 0;
	for (int32 P : Pool()) Like += P != PlayerId && SlotOf(P) == Slot && GenericValue(P) >= 0.6f * Mine;
	return 1.f + 0.2f * FMath::Clamp(1.f - float(Like) / 4.f, 0.f, 1.f);
}

float FAuction::PlanFactor(int32 Team, int32 PlayerId) const
{
	const FAuctionPlan& Plan = Plans[Team];
	const FAuctionTarget* T = Plan.Targets.FindByPredicate([PlayerId](const FAuctionTarget& X) { return X.Player == PlayerId; });
	const float Tier = !T ? 0.94f : T->Tier == 0 ? 1.12f : T->Tier == 1 ? 1.06f : 1.f;
	return Tier * (1.f + Plan.SlotBoost[int32(SlotOf(PlayerId))]);
}

int32 FAuction::Valuation(int32 Team, int32 PlayerId) const
{
	if (IsHuman(Team) || !Teams.IsValidIndex(Team)) return 0;
	const FAuctionPlayer& X = Player(PlayerId);
	const FAuctionTeam& T = Teams[Team];
	if (!CanAfford(Team, PlayerId, X.Base)) return 0;
	const float Add = Marginal(Team, PlayerId);
	// Style, opinion, plan and scarcity each move the price, but together they move it within bounds: what a player
	// adds to the side stays the main thing, so the dearest buys are the best players.
	float Lean = StyleFactor(Team, PlayerId) * Noise(Team, PlayerId) * PlanFactor(Team, PlayerId);
	if (Add >= 0.5f * GenericValue(PlayerId)) Lean *= Scarcity(Team, PlayerId);
	float V = float(AuctionRules::MinBase) + Add * LakhPerPoint * FMath::Clamp(Lean, 0.6f, 1.7f);
	// Sides fill their twenty-odd places and stop: the last few are worth little (2025 squads averaged 22.8).
	const int32 Have = T.Squad.Num();
	// (A mini auction fills to the full 25: its few places are the whole point.)
	const int32 Full = Aim() + 2;
	if (Have >= Full && Add < 3.f) return 0;
	if (Have >= Full) V = float(AuctionRules::MinBase) + (V - AuctionRules::MinBase) * 0.45f;
	else if (Have >= Aim()) V = float(AuctionRules::MinBase) + (V - AuctionRules::MinBase) * 0.75f;
	// Prudence: a side with many places still to fill keeps money for them.
	const int32 Places = FMath::Max(0, Aim() - T.Squad.Num());
	const float Share = Places >= 12 ? 0.45f : Places >= 8 ? 0.55f : Places >= 4 ? 0.7f : 0.9f;
	V = FMath::Min(V, Share * float(MaxBid(Team)));
	if (V < X.Base)
	{
		// A side short of 18 in the accelerated rounds takes a useful player at his base price.
		const FAuctionSet* S = CurrentSet();
		const bool bMustFill = T.Squad.Num() < AuctionRules::SquadMin && S && S->bAccelerated;
		if (!bMustFill || V < 0.6f * X.Base) return 0;
		V = float(X.Base);
	}
	return FMath::Min(FMath::RoundToInt(V), MaxBid(Team));
}

int32 FAuction::EstimateCeiling(int32 Team, int32 PlayerId) const
{
	if (!Teams.IsValidIndex(Team)) return 0;
	// A rival's limit as another table reads it: what the player adds to that side, at the room's price, in its style.
	const float Style = IsHuman(Team) ? 1.f : StyleFactor(Team, PlayerId);
	const int32 V = AuctionRules::MinBase + FMath::RoundToInt(Marginal(Team, PlayerId) * LakhPerPoint * Style);
	return FMath::Min(V, MaxBid(Team));
}

// ---- Plans -------------------------------------------------------------------------------------------------------

void FAuction::BuildPlans()
{
	const TArray<int32> Left = Pool();
	for (int32 Team = 0; Team < Teams.Num(); ++Team)
	{
		FAuctionPlan& Plan = Plans[Team];
		Plan = FAuctionPlan();
		if (IsHuman(Team)) continue;
		// Every player the side would pay his price for, ranked within his slot: the first two are its first choices.
		struct FPick { int32 Player; float Worth; };
		TArray<FPick> BySlot[int32(EAuctionSlot::Count)];
		for (int32 P : Left)
		{
			const float Add = Marginal(Team, P);
			if (Add < 3.f) continue;
			const float Worth = float(AuctionRules::MinBase) + Add * LakhPerPoint * StyleFactor(Team, P) * Noise(Team, P);
			const int32 Expect = ExpectedPrice(P);
			if (Worth < 0.9f * Expect || Worth < Player(P).Base) continue;
			BySlot[int32(SlotOf(P))].Add({ P, Worth });
		}
		for (int32 S = 0; S < int32(EAuctionSlot::Count); ++S)
		{
			BySlot[S].Sort([](const FPick& A, const FPick& B) { return A.Worth > B.Worth; });
			for (int32 I = 0; I < FMath::Min(4, BySlot[S].Num()); ++I)
				Plan.Targets.Add({ BySlot[S][I].Player, EAuctionSlot(S), I < 2 ? I : 2, FMath::Min(FMath::RoundToInt(BySlot[S][I].Worth), ExpectedPrice(BySlot[S][I].Player)) });
		}
	}
}

void FAuction::OnSold(int32 Team, int32 PlayerId)
{
	for (int32 T = 0; T < Plans.Num(); ++T)
	{
		FAuctionPlan& Plan = Plans[T];
		const int32 I = Plan.Targets.IndexOfByPredicate([PlayerId](const FAuctionTarget& X) { return X.Player == PlayerId; });
		if (I == INDEX_NONE) continue;
		const int32 Slot = int32(Plan.Targets[I].Slot);
		// Bought: that need is met. Lost a first choice: the next man in that slot is worth more to us now.
		if (T == Team) Plan.SlotBoost[Slot] = 0.f;
		else if (Plan.Targets[I].Tier == 0) Plan.SlotBoost[Slot] = FMath::Min(0.3f, Plan.SlotBoost[Slot] + 0.15f);
		Plan.Targets.RemoveAt(I);
	}
}

void FAuction::PlanLot()
{
	const int32 P = Lot;
	const float Expect = float(ExpectedPrice(P));
	// The side with his Right to Match card often lets the room set the price and matches it at the end.
	const int32 R = FormerTeam.IsValidIndex(P) ? FormerTeam[P] : INDEX_NONE;
	int32 SittingOut = INDEX_NONE;
	if (R != INDEX_NONE && !IsHuman(R) && Teams[R].RtmCards > 0 && Expect >= 600.f && MaxThisLot[R] >= 0.9f * Expect)
	{
		const FAuctionTeam& T = Teams[R];
		const bool bRoom = Player(P).bCapped ? T.KeptCapped < AuctionRules::KeepCappedMax : T.KeptUncapped < AuctionRules::KeepUncappedMax;
		if (bRoom && Rng.FRand() < 0.4f) { EnterAt[R] = MAX_int32; SittingOut = R; }
	}
	// A side that does not want him but has money may push the price for a rival that does. Never more than one a lot.
	if (Expect < 400.f) return;
	static const float Appetite[] = { 0.4f, 1.f, 1.3f };
	for (int32 T = 0; T < Teams.Num(); ++T)
	{
		if (IsHuman(T) || T == SittingOut || MaxThisLot[T] >= 0.5f * Expect || MaxBid(T) < 2500) continue;
		int32 Rival = INDEX_NONE, RivalCeiling = 0;
		for (int32 O = 0; O < Teams.Num(); ++O)
		{
			if (O == T) continue;
			const int32 C = IsHuman(O) ? EstimateCeiling(O, P) : MaxThisLot[O];
			if (C > RivalCeiling) { RivalCeiling = C; Rival = O; }
		}
		if (Rival == INDEX_NONE || RivalCeiling < 1.2f * Expect) continue;
		float Chance = AuctionData::Franchises()[T].Enforcer * 0.25f * Appetite[int32(Config.Difficulty)];
		if (Config.Difficulty == EAuctionDifficulty::Legend && IsHuman(Rival)) Chance *= 1.8f;
		if (Rng.FRand() >= Chance) continue;
		// It stops well short of the rival's limit, and never goes where owning him would hurt.
		const int32 Push = FMath::Min(FMath::RoundToInt(0.7f * RivalCeiling), FMath::RoundToInt(0.22f * MaxBid(T)));
		if (Push <= Player(P).Base) continue;
		Enforcing.Add(T);
		MaxThisLot[T] = FMath::Max(MaxThisLot[T], Push);
		EnterAt[T] = Player(P).Base;
		break;
	}
}

// ---- Retentions and trades ---------------------------------------------------------------------------------------

float FAuction::RetentionWorth(int32 Team, int32 PlayerId) const
{
	const FAuctionFranchise& F = AuctionData::Franchises()[Team];
	TArray<int32> Best;
	SquadValue(SquadIds(Team), &Best);
	const float Fit = Best.Contains(PlayerId) ? 1.f : 0.55f;
	return float(MarketEstimate(PlayerId)) * Fit * F.Loyalty * (Player(PlayerId).bRetained2026 ? 1.05f : 1.f);
}

int32 FAuction::RetentionAsk(int32 PlayerId) const
{
	const int32 T = OwnerOf(PlayerId);
	if (IsMini() || T == INDEX_NONE) return 0;
	// He asks for most of what the open market would pay, less the more the club means to him.
	const float Loyalty = AuctionData::Franchises()[T].Loyalty;
	const float Ask = 0.72f * float(MarketEstimate(PlayerId)) * (1.5f - 0.4f * Loyalty);
	return FMath::FloorToInt(Ask / 25.f) * 25;
}

bool FAuction::WantsAuction(int32 PlayerId) const
{
	const int32 T = OwnerOf(PlayerId);
	if (IsMini() || T == INDEX_NONE) return false;
	const FAuctionPlayer& X = Player(PlayerId);
	const int32 Age = AgeOf(PlayerId);
	if (!X.bCapped || Age < 24 || Age > 33 || MarketEstimate(PlayerId) < 1000) return false;
	// Stars in their prime at a side that does not hold them close test the market, captains most of all.
	const float Loyalty = AuctionData::Franchises()[T].Loyalty;
	const float Chance = FMath::Clamp(0.3f * (1.4f - Loyalty) + (X.Has(AuctionTags::Captain) ? 0.08f : 0.f), 0.f, 0.3f);
	return AuctionAIPrivate::Unit(HashCombine(GetTypeHash(PlayerId * 104729), GetTypeHash(Seed))) < Chance;
}

TArray<int32> FAuction::AiRetentions(int32 Team) const
{
	TArray<int32> Keep;
	const TArray<int32> Squad = RetentionCandidates(Team);
	if (IsMini())
	{
		// A mini auction only tops a squad up: a side keeps everyone who plays or is worth near his contract (letting
		// a star go only means buying one back), and releases the dead weight it can replace for less. December 2025:
		// the ten went in with about 24 crore each.
		struct FKeep { int32 Player; float Ratio; };
		TArray<FKeep> Kept;
		TArray<int32> Best;
		SquadValue(SquadIds(Team), &Best);
		for (int32 P : Squad)
		{
			const float Worth = RetentionWorth(Team, P);
			const int32 Cost = FMath::Max(1, ContractOf(P));
			if (Best.Contains(P) || Worth >= 0.7f * Cost || Cost <= 50) Kept.Add({ P, Worth / Cost });
		}
		Kept.Sort([](const FKeep& A, const FKeep& B) { return A.Ratio > B.Ratio; });
		int32 Overseas = 0, Cost = 0;
		for (const FKeep& K : Kept)
		{
			const bool bOverseas = Player(K.Player).IsOverseas();
			const int32 C = ContractOf(K.Player);
			if (Keep.Num() >= 18 || (bOverseas && Overseas >= AuctionRules::OverseasMax) || Cost + C > Config.Purse - 800) continue;
			Keep.Add(K.Player);
			Overseas += bOverseas;
			Cost += C;
		}
		return Keep;
	}
	int32 Capped = 0, Uncapped = 0;
	for (int32 P : Squad)
	{
		if (Keep.Num() >= AuctionRules::KeepMax) break;
		const FAuctionPlayer& X = Player(P);
		const bool bRoom = X.bCapped ? Capped < AuctionRules::KeepCappedMax : Uncapped < AuctionRules::KeepUncappedMax;
		if (!bRoom || WantsAuction(P)) continue;
		TArray<int32> Try = Keep;
		Try.Add(P);
		const int32 Extra = RetentionCost(Try) - RetentionCost(Keep);
		// Keeping a man costs his slab now; letting him go leaves an RTM card and the chance of him cheaper. The more
		// already kept, the higher the bar.
		if (RetentionWorth(Team, P) < float(Extra) * (0.62f + 0.05f * Keep.Num())) continue;
		Keep = MoveTemp(Try);
		X.bCapped ? ++Capped : ++Uncapped;
	}
	return Keep;
}

float FAuction::TradeValue(int32 Team, int32 Out, int32 In) const
{
	TArray<int32> Squad;
	for (int32 P = 0; P < Owner.Num(); ++P) if (Owner[P] == Team) Squad.Add(P);
	const float Before = SquadValue(Squad);
	Squad.Remove(Out);
	Squad.Add(In);
	float Gain = SquadValue(Squad) - Before;
	// In a mini auction the purse is the salary cap, so a cheaper contract is worth its saving.
	if (IsMini()) Gain += float(ContractOf(Out) - ContractOf(In)) / FMath::Max(1.f, LakhPerPoint);
	return Gain;
}

// ---- The analysts ------------------------------------------------------------------------------------------------

EAuctionSlot FAuction::SlotOf(int32 PlayerId) const
{
	const FAuctionPlayer& X = Player(PlayerId);
	switch (X.Role)
	{
	case EAuctionRole::Keeper: return EAuctionSlot::Keeper;
	case EAuctionRole::AllRounder: return EAuctionSlot::AllRounder;
	case EAuctionRole::Pace: return X.Has(AuctionTags::DeathPace) ? EAuctionSlot::Death : EAuctionSlot::NewBall;
	case EAuctionRole::Spin: return EAuctionSlot::Spin;
	default: return X.Has(AuctionTags::Opener) ? EAuctionSlot::Opener : X.Has(AuctionTags::Finisher) ? EAuctionSlot::Finisher : EAuctionSlot::MiddleOrder;
	}
}

const TCHAR* FAuction::SlotName(EAuctionSlot Slot)
{
	static const TCHAR* Names[] = { TEXT("KEEPER"), TEXT("OPENER"), TEXT("MIDDLE ORDER"), TEXT("FINISHER"), TEXT("ALL-ROUNDER"),
		TEXT("NEW BALL"), TEXT("DEATH BOWLER"), TEXT("SPINNER") };
	static_assert(UE_ARRAY_COUNT(Names) == int32(EAuctionSlot::Count), "a name per slot");
	return Names[FMath::Clamp(int32(Slot), 0, int32(EAuctionSlot::Count) - 1)];
}

FAuctionNeeds FAuction::Needs(int32 Team) const
{
	FAuctionNeeds N;
	if (!Teams.IsValidIndex(Team)) return N;
	const TArray<int32> Squad = SquadIds(Team);
	const float Value = SquadValue(Squad, &N.BestXI, &N.Holes);
	// Out of 100: a strong side scores in the eighties.
	const float Ideal = 10.5f * Contribution(80.f, false);
	N.Strength = FMath::Clamp(100.f * Value / FMath::Max(1.f, Ideal), 0.f, 100.f);
	N.Rank = 1;
	for (int32 T = 0; T < Teams.Num(); ++T) if (T != Team && TeamValue(T) > Value) ++N.Rank; // cached: the HUD asks every frame
	int32 Overseas = 0;
	for (int32 P : Squad) Overseas += Player(P).IsOverseas();
	N.Places = FMath::Max(0, AuctionRules::SquadMax - Squad.Num());
	N.ToMinimum = FMath::Max(0, AuctionRules::SquadMin - Squad.Num());
	N.OverseasLeft = FMath::Max(0, AuctionRules::OverseasMax - Overseas);
	N.PerPlace = FMath::Max(0, MaxBid(Team)) / FMath::Max(1, 22 - Squad.Num());
	for (const FString& H : N.Holes)
	{
		if (H.Contains(TEXT("keeper"))) N.OpenSlots.AddUnique(EAuctionSlot::Keeper);
		else if (H.Contains(TEXT("opener"))) N.OpenSlots.AddUnique(EAuctionSlot::Opener);
		else if (H.Contains(TEXT("finisher"))) N.OpenSlots.AddUnique(EAuctionSlot::Finisher);
		else if (H.Contains(TEXT("death"))) N.OpenSlots.AddUnique(EAuctionSlot::Death);
		else if (H.Contains(TEXT("pace")) || H.Contains(TEXT("fast"))) N.OpenSlots.AddUnique(EAuctionSlot::NewBall);
		else if (H.Contains(TEXT("spinner"))) N.OpenSlots.AddUnique(EAuctionSlot::Spin);
	}
	if (!IsHuman(Team))
	{
		TArray<FAuctionTarget> Left = Plans[Team].Targets.FilterByPredicate([this](const FAuctionTarget& X) { return !SoldTo.Contains(X.Player); });
		Left.StableSort([](const FAuctionTarget& A, const FAuctionTarget& B) { return A.Tier < B.Tier || (A.Tier == B.Tier && A.Budget > B.Budget); });
		for (int32 I = 0; I < FMath::Min(4, Left.Num()); ++I) N.Targets.Add(Left[I].Player);
	}
	return N;
}

FString FAuction::Whisper(int32 Team) const
{
	if (Lot == INDEX_NONE || !Teams.IsValidIndex(Team)) return FString();
	const FAuctionPlayer& X = Player(Lot);
	const TArray<FAuctionFranchise>& Fr = AuctionData::Franchises();
	TArray<FString> Lines;
	const int32 R = FormerTeam.IsValidIndex(Lot) ? FormerTeam[Lot] : INDEX_NONE;
	if (R == Team && Teams[Team].RtmCards > 0) Lines.Add(TEXT("He was ours: we can sit out and match him with an RTM card."));
	else if (R != INDEX_NONE && Teams[R].RtmCards > 0) Lines.Add(FString::Printf(TEXT("%s hold an RTM card for him: whoever wins faces a final raise."), *Fr[R].Short));
	// The rival most likely to go big, as our analysts read the room.
	int32 Rival = INDEX_NONE, Ceiling = 0;
	for (int32 T = 0; T < Teams.Num(); ++T)
	{
		if (T == Team) continue;
		const int32 C = EstimateCeiling(T, Lot);
		if (C > Ceiling) { Ceiling = C; Rival = T; }
	}
	const int32 Expect = ExpectedPrice(Lot);
	if (Rival != INDEX_NONE && Ceiling > X.Base)
	{
		// The sharper the difficulty, the vaguer the read: the other tables give less away.
		const int32 Blur = Config.Difficulty == EAuctionDifficulty::Legend ? 250 : 100;
		const int32 Around = FMath::Max(X.Base, FMath::RoundToInt(float(Ceiling) / Blur) * Blur);
		Lines.Add(FString::Printf(TEXT("%s want him most (%s left): they could go to about %s."), *Fr[Rival].Short,
			*AuctionRules::Money(Teams[Rival].Purse), *AuctionRules::Money(Around)));
	}
	const float Mine = GenericValue(Lot);
	int32 Like = 0;
	for (int32 P : Pool()) Like += P != Lot && SlotOf(P) == SlotOf(Lot) && GenericValue(P) >= 0.6f * Mine;
	if (Mine > 5.f && Like <= 1) Lines.Add(FString::Printf(TEXT("Only %d %s of his quality left after him."), Like, *FString(SlotName(SlotOf(Lot))).ToLower()));
	if (const FAuctionWish* W = WishFor(Team, Lot)) Lines.Insert(FString::Printf(TEXT("Your limit: %s%s."), *AuctionRules::Money(W->Max), W->bAuto ? TEXT(" on auto-bid") : TEXT("")), 0);
	else if (Marginal(Team, Lot) < 0.25f * FMath::Max(1.f, Mine)) Lines.Insert(TEXT("He would not make our twelve."), 0);
	else Lines.Add(FString::Printf(TEXT("The room has him at about %s."), *AuctionRules::Money(Expect)));
	while (Lines.Num() > 2) Lines.Pop();
	return FString::Join(Lines, TEXT("  "));
}

FAuctionVerdict FAuction::Verdict(int32 Team) const
{
	FAuctionVerdict V;
	V.Needs = Needs(Team);
	if (!Teams.IsValidIndex(Team)) return V;
	// Value for money: what the room thought each buy was worth before the hammer, against what was paid.
	float Surplus = 0.f, Spent = 0.f;
	struct FDeal { int32 Player; float Gap; };
	TArray<FDeal> Deals;
	for (const FAuctionSigning& S : Teams[Team].Squad)
	{
		if (S.bRetained) continue;
		const float Worth = float(S.Worth);
		Surplus += Worth - S.Price;
		Spent += S.Price;
		Deals.Add({ S.Player, Worth - S.Price });
		if (Worth >= 200.f && S.Price <= 0.6f * Worth) V.Steals.Add(S.Player);
		if (S.Price >= 300 && S.Price >= 1.5f * Worth) V.Splurges.Add(S.Player);
	}
	auto ByGap = [&](TArray<int32>& List, bool bBest)
	{
		List.Sort([&](int32 A, int32 B)
		{
			const FDeal* DA = Deals.FindByPredicate([A](const FDeal& D) { return D.Player == A; });
			const FDeal* DB = Deals.FindByPredicate([B](const FDeal& D) { return D.Player == B; });
			return bBest ? DA->Gap > DB->Gap : DA->Gap < DB->Gap;
		});
		while (List.Num() > 3) List.Pop();
	};
	ByGap(V.Steals, true);
	ByGap(V.Splurges, false);
	const float Value = 50.f + 50.f * FMath::Clamp(Surplus / FMath::Max(1.f, Spent), -1.f, 1.f);
	V.Score = FMath::Clamp(0.75f * V.Needs.Strength + 0.25f * Value, 0.f, 100.f);
	// Graded against the league as well as the scale: the side ranked first is at least an A.
	const float Rel = V.Score + (V.Needs.Rank <= 2 ? 6.f : V.Needs.Rank >= 9 ? -6.f : 0.f);
	V.Grade = Rel >= 82.f ? TEXT("A+") : Rel >= 74.f ? TEXT("A") : Rel >= 66.f ? TEXT("B+") : Rel >= 58.f ? TEXT("B") : Rel >= 48.f ? TEXT("C") : TEXT("D");
	V.Summary = FString::Printf(TEXT("Best twelve ranked %d of %d."), V.Needs.Rank, Teams.Num());
	if (V.Needs.Holes.Num() > 0) V.Summary += FString::Printf(TEXT(" Watch: %s."), *V.Needs.Holes[0].ToLower());
	if (V.Steals.Num() > 0) V.Summary += FString::Printf(TEXT(" Steal of the auction: %s."), *Player(V.Steals[0]).Name);
	return V;
}

TArray<FAuctionRecord> FAuction::Records() const
{
	TArray<FAuctionRecord> Out;
	auto Best = [&](const TCHAR* Title, TFunction<bool(const FAuctionPlayer&)> Keep)
	{
		FAuctionRecord R;
		R.Title = Title;
		for (const FAuctionEventRecord& E : Events)
			if (E.Type == EAuctionEvent::Sold && E.Amount > R.Price && Keep(Player(E.Player))) { R.Player = E.Player; R.Team = E.Team; R.Price = E.Amount; }
		if (R.Player != INDEX_NONE) Out.Add(R);
	};
	Best(TEXT("MOST EXPENSIVE"), [](const FAuctionPlayer&) { return true; });
	Best(TEXT("MOST EXPENSIVE OVERSEAS"), [](const FAuctionPlayer& X) { return X.IsOverseas(); });
	Best(TEXT("MOST EXPENSIVE UNCAPPED"), [](const FAuctionPlayer& X) { return !X.bCapped; });
	Best(TEXT("MOST EXPENSIVE BOWLER"), [](const FAuctionPlayer& X) { return X.Role == EAuctionRole::Pace || X.Role == EAuctionRole::Spin; });
	Best(TEXT("MOST EXPENSIVE KEEPER"), [](const FAuctionPlayer& X) { return X.Role == EAuctionRole::Keeper; });
	return Out;
}
