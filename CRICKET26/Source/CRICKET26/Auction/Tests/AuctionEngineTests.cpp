// IPL auction rules and AI: the increment ladder, money formatting, retentions, the Right to Match, and whole AI-only
// auctions that must end with every squad legal. Engine only: these also build and run headless, without the editor,
// through Scripts/auction/sim/run.sh.

#include "AuctionTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionIncrements, "CRICKET26.Auction.Increments", AuctionTests::Flags)
bool FAuctionIncrements::RunTest(const FString&)
{
	using namespace AuctionRules;
	TestEqual(TEXT("30L +5"), NextBid(30), 35);
	TestEqual(TEXT("95L +5"), NextBid(95), 100);
	TestEqual(TEXT("1Cr +10"), NextBid(100), 110);
	TestEqual(TEXT("1.9Cr +10"), NextBid(190), 200);
	TestEqual(TEXT("2Cr +20"), NextBid(200), 220);
	TestEqual(TEXT("4.8Cr +20"), NextBid(480), 500);
	TestEqual(TEXT("5Cr +25"), NextBid(500), 525);
	TestEqual(TEXT("27Cr +25"), NextBid(2700), 2725);
	TestEqual(TEXT("broadcast crore"), Money(240), FString(TEXT("₹2.40Cr")));
	TestEqual(TEXT("broadcast lakh"), Money(75), FString(TEXT("₹75L")));
	TestEqual(TEXT("spoken crore"), Spoken(240), FString(TEXT("2 crore 40")));
	TestEqual(TEXT("spoken round"), Spoken(1400), FString(TEXT("14 crore")));
	TestEqual(TEXT("spoken lakh"), Spoken(75), FString(TEXT("75 lakh")));
	TestEqual(TEXT("slabs"), CappedRetentionCost(0) + CappedRetentionCost(1) + CappedRetentionCost(2) + CappedRetentionCost(3) + CappedRetentionCost(4), 7500);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionCsv, "CRICKET26.Auction.Roster", AuctionTests::Flags)
bool FAuctionCsv::RunTest(const FString&)
{
	const TArray<FAuctionPlayer> P = AuctionData::ParseCsv(TEXT(
		"Name,Country,Role,Capped,Team2026,BasePriceLakh,BatRating,BowlRating\n"
		"A Batter,India,BAT,1,CSK,200,88,10\n"
		"B Quick,Australia,PACE,1,XYZ,10,20,90\n"
		",India,BAT,1,,50,50,50\n"
		"C Kid,India,AR,0,,30,60,60\n"));
	TestEqual(TEXT("blank names skipped"), P.Num(), 3);
	if (P.Num() != 3) return false;
	TestEqual(TEXT("ids are indices"), P[2].Id, 2);
	TestTrue(TEXT("overseas"), P[1].IsOverseas() && !P[0].IsOverseas());
	TestEqual(TEXT("unknown franchise cleared"), P[1].Team2026, FString());
	TestEqual(TEXT("base floor 30 lakh"), P[1].Base, 30);
	TestFalse(TEXT("uncapped"), P[2].bCapped);
	TestEqual(TEXT("all-rounder overall counts both skills"), P[2].Overall(), 64);

	const TArray<FAuctionPlayer>& All = AuctionData::Players();
	TestTrue(TEXT("mega auction pool"), All.Num() >= 300);
	TestEqual(TEXT("ten franchises"), AuctionData::Franchises().Num(), 10);
	for (const FAuctionFranchise& F : AuctionData::Franchises())
	{
		int32 Squad = 0;
		for (const FAuctionPlayer& X : All) Squad += X.Team2026 == F.Code;
		TestTrue(FString::Printf(TEXT("%s has its 2026 squad (%d)"), *F.Code, Squad), Squad >= 18 && Squad <= 25);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionRetention, "CRICKET26.Auction.Retention", AuctionTests::Flags)
bool FAuctionRetention::RunTest(const FString&)
{
	FAuction A(0);
	const TArray<int32> Squad = A.RetentionCandidates(0);
	TArray<int32> Capped, Uncapped;
	for (int32 P : Squad) (FAuction::Player(P).bCapped ? Capped : Uncapped).Add(P);
	if (!TestTrue(TEXT("CSK has capped and uncapped players"), Capped.Num() >= 6 && Uncapped.Num() >= 3)) return false;

	FString Why;
	TestFalse(TEXT("six capped refused"), A.CanRetain(0, { Capped[0], Capped[1], Capped[2], Capped[3], Capped[4], Capped[5] }, &Why));
	TestFalse(TEXT("three uncapped refused"), A.CanRetain(0, { Uncapped[0], Uncapped[1], Uncapped[2] }));
	TestFalse(TEXT("another side's player refused"), A.CanRetain(0, { A.RetentionCandidates(1)[0] }));
	TestFalse(TEXT("duplicate refused"), A.CanRetain(0, { Capped[0], Capped[0] }));
	// The slabs, for players who do not ask for more than them.
	TArray<int32> Modest;
	for (int32 P : Capped) if (!A.WantsAuction(P) && A.RetentionAsk(P) <= 1400) Modest.Add(P);
	const int32* Cheap = Uncapped.FindByPredicate([&](int32 P) { return A.RetentionAsk(P) <= 400; });
	if (!TestTrue(TEXT("CSK has two modest capped asks and a modest uncapped one"), Modest.Num() >= 2 && Cheap)) return false;
	const TArray<int32> Keep = { Modest[0], Modest[1], *Cheap };
	TestTrue(TEXT("3 kept is legal"), A.CanRetain(0, Keep));
	TestEqual(TEXT("cost 18 + 14 + 4"), A.RetentionCost(Keep), 3600);
	TestTrue(TEXT("retain"), A.Retain(0, Keep));
	TestEqual(TEXT("purse"), A.Teams[0].Purse, 12000 - 3600);
	TestEqual(TEXT("three RTM cards"), A.Teams[0].RtmCards, 3);
	TestFalse(TEXT("retentions lock"), A.Retain(0, {}));

	A.BeginAuction();
	for (int32 T = 1; T < A.Teams.Num(); ++T)
	{
		const FAuctionTeam& Team = A.Teams[T];
		TestTrue(TEXT("AI retentions legal"), Team.Squad.Num() <= 6 && Team.KeptCapped <= 5 && Team.KeptUncapped <= 2);
		TestEqual(TEXT("AI RTM cards"), Team.RtmCards, 6 - Team.Squad.Num());
	}
	TestTrue(TEXT("sets start with the marquee"), A.Sets.Num() > 10 && A.Sets[0].Code == TEXT("M1") && A.Sets[1].Code == TEXT("M2"));
	for (const FAuctionSet& S : A.Sets)
		for (int32 P : S.Players) TestFalse(TEXT("no retained player in the pool"), A.SoldTo.Contains(P));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionRetentionAsks, "CRICKET26.Auction.RetentionAsks", AuctionTests::Flags)
bool FAuctionRetentionAsks::RunTest(const FString&)
{
	// A star whose market is above his slab costs what he asks (Klaasen 23 Cr in 2025), the slabs go to whoever keeps
	// the bill least, and a star who wants the auction cannot be kept (Pant, Iyer, Rahul).
	FAuction A(INDEX_NONE, 2026);
	int32 Star = INDEX_NONE, Refusals = 0, Stars = 0;
	for (int32 T = 0; T < A.Teams.Num(); ++T)
		for (int32 P : A.RetentionCandidates(T))
		{
			Refusals += A.WantsAuction(P);
			Stars += A.MarketEstimate(P) >= 1000;
			if (Star == INDEX_NONE && FAuction::Player(P).bCapped && !A.WantsAuction(P) && A.RetentionAsk(P) > 1800) Star = P;
		}
	AddInfo(FString::Printf(TEXT("%d stars at 10 Cr+, %d want the auction"), Stars, Refusals));
	if (!TestTrue(TEXT("some star asks for more than the top slab"), Star != INDEX_NONE)) return false;
	TestEqual(TEXT("a star alone costs his ask"), A.RetentionCost({ Star }), A.RetentionAsk(Star));
	TestTrue(TEXT("some stars test the market"), Refusals >= 1 && Refusals <= Stars / 3);
	for (int32 T = 0; T < A.Teams.Num(); ++T)
		for (int32 P : A.RetentionCandidates(T))
			if (A.WantsAuction(P)) { TestFalse(TEXT("a refusing star cannot be retained"), A.CanRetain(T, { P })); break; }
	// Order does not matter: the bill is the least over every way of handing out the slabs.
	const int32 Owner = A.OwnerOf(Star);
	TArray<int32> Two = { Star };
	for (int32 P : A.RetentionCandidates(Owner)) if (P != Star && FAuction::Player(P).bCapped && !A.WantsAuction(P)) { Two.Add(P); break; }
	const TArray<int32> Swapped = { Two.Last(), Two[0] };
	TestEqual(TEXT("slab order does not change the bill"), A.RetentionCost(Two), A.RetentionCost(Swapped));
	// The AI keeps about what the real sides did: 46 players across the ten in 2025.
	A.BeginAuction();
	int32 Kept = 0, Above = 0;
	for (const FAuctionTeam& T : A.Teams)
		for (const FAuctionSigning& S : T.Squad) { Kept += S.bRetained; Above += S.Price > 1800; }
	AddInfo(FString::Printf(TEXT("AI kept %d, %d above the top slab"), Kept, Above));
	TestTrue(FString::Printf(TEXT("30-60 retained (%d)"), Kept), Kept >= 30 && Kept <= 60);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionHumanBid, "CRICKET26.Auction.HumanBid", AuctionTests::Flags)
bool FAuctionHumanBid::RunTest(const FString&)
{
	FAuction A(2);
	A.BeginAuction();
	TestTrue(TEXT("first lot comes up"), AuctionTests::RunUntil(A, [&] { return A.Phase == EAuctionPhase::Bidding; }));
	const int32 Base = FAuction::Player(A.Lot).Base;
	TestTrue(TEXT("human can open"), A.CanHumanBid());
	TestTrue(TEXT("opens at base"), A.HumanBid() && A.Price == Base && A.Holder == 2);
	TestFalse(TEXT("cannot outbid yourself"), A.CanHumanBid());
	// Keep bidding whenever outbid until the lot is decided: the human must win or be priced out by the purse rule.
	const int32 Lot = A.Lot;
	AuctionTests::RunUntil(A, [&]
	{
		if (A.CanHumanBid() && A.Price < 2000) A.HumanBid();
		if (A.Phase == EAuctionPhase::RtmAsk && A.IsHuman(A.RtmTeam)) A.HumanRtm(false);
		if (A.Phase == EAuctionPhase::RtmRaise && A.IsHuman(A.Holder)) A.HumanFinalRaise(A.Price);
		return A.Phase == EAuctionPhase::Hammer;
	});
	const int32* Buyer = A.SoldTo.Find(Lot);
	TestNotNull(TEXT("sold"), Buyer);
	int32 Spent = 0;
	for (const FAuctionSigning& S : A.Teams[2].Squad) Spent += S.Price;
	TestEqual(TEXT("purse is 120 Cr less spending"), A.Teams[2].Purse, 12000 - Spent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionRtm, "CRICKET26.Auction.RightToMatch", AuctionTests::Flags)
bool FAuctionRtm::RunTest(const FString&)
{
	// The human is every player's former side at some point: find a lot where the human is asked about RTM.
	for (int32 Human = 0; Human < 10; ++Human)
	{
		FAuction A(Human, 7);
		A.Retain(Human, {});
		A.BeginAuction();
		const bool bAsked = AuctionTests::RunUntil(A, [&] { return A.Phase == EAuctionPhase::Finished || A.AwaitingHuman(); });
		if (!bAsked || A.Phase != EAuctionPhase::RtmAsk) continue;
		const int32 Lot = A.Lot, Bidder = A.Holder, Hammer = A.Price;
		TestEqual(TEXT("asked as the 2026 side"), AuctionData::Franchises()[Human].Code, FAuction::Player(Lot).Team2026);
		A.HumanRtm(true);
		TestEqual(TEXT("final raise goes to the highest bidder"), A.Phase, EAuctionPhase::RtmRaise);
		AuctionTests::RunUntil(A, [&] { return A.Phase != EAuctionPhase::RtmRaise; });
		TestEqual(TEXT("then the RTM side is asked to match"), A.Phase, EAuctionPhase::RtmMatch);
		TestTrue(TEXT("final bid never lower"), A.Price >= Hammer);
		const int32 Final = A.Price;
		A.HumanMatch(true);
		TestEqual(TEXT("matched: he is ours"), A.SoldTo.FindRef(Lot), Human);
		TestEqual(TEXT("at the final price"), A.Teams[Human].Squad.Last().Price, Final);
		TestTrue(TEXT("flagged RTM"), A.Teams[Human].Squad.Last().bRtm);
		TestEqual(TEXT("one card used"), A.Teams[Human].RtmCards, 5);
		TestNotEqual(TEXT("bidder lost him"), A.SoldTo.FindRef(Lot), Bidder);
		return true;
	}
	AddError(TEXT("no RTM question reached a human side"));
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionFullSim, "CRICKET26.Auction.FullAuction", AuctionTests::Flags)
bool FAuctionFullSim::RunTest(const FString&)
{
	for (int32 Seed : { 2026, 11, 99 })
	{
		FAuction A(INDEX_NONE, Seed);
		A.BeginAuction();
		for (int32 T = 0; T < A.Teams.Num(); ++T)
			AddInfo(FString::Printf(TEXT("seed %d %s kept %d, purse %s"), Seed, *AuctionData::Franchises()[T].Code, A.Teams[T].Squad.Num(), *AuctionRules::Money(A.Teams[T].Purse)));
		TArray<int32> Marquee;
		for (const FAuctionSet& S : A.Sets) if (S.Code.StartsWith(TEXT("M"))) Marquee.Append(S.Players);
		if (!TestTrue(TEXT("finishes"), AuctionTests::RunUntil(A, [&] { return A.Phase == EAuctionPhase::Finished; }, 0.25f))) return false;
		// Every marquee name sold in 2025; the sets exist because sides fight over them.
		for (int32 P : Marquee) TestTrue(FString::Printf(TEXT("seed %d marquee %s sold"), Seed, *FAuction::Player(P).Name), A.SoldTo.Contains(P));
		int32 Top = 0, Sold = 0, Rtm = 0, Big = 0;
		for (int32 T = 0; T < A.Teams.Num(); ++T)
		{
			const FAuctionTeam& Team = A.Teams[T];
			const FString Code = AuctionData::Franchises()[T].Code;
			TestTrue(FString::Printf(TEXT("%s squad %d in 18..25"), *Code, Team.Squad.Num()), Team.Squad.Num() >= 18 && Team.Squad.Num() <= 25);
			TestTrue(FString::Printf(TEXT("%s overseas %d <= 8"), *Code, Team.Overseas()), Team.Overseas() <= 8);
			TestTrue(FString::Printf(TEXT("%s purse %d >= 0"), *Code, Team.Purse), Team.Purse >= 0);
			TestTrue(FString::Printf(TEXT("%s kept %d+%d"), *Code, Team.KeptCapped, Team.KeptUncapped), Team.KeptCapped <= 5 && Team.KeptUncapped <= 2);
			TestTrue(FString::Printf(TEXT("%s spent %d"), *Code, 12000 - Team.Purse), 12000 - Team.Purse >= 8000);
			for (const FAuctionSigning& S : Team.Squad)
			{
				if (!S.bRetained) { Top = FMath::Max(Top, S.Price); ++Sold; Big += S.Price >= 1200; }
				Rtm += S.bRtm;
				TestTrue(TEXT("never below base"), S.bRetained || S.Price >= FAuction::Player(S.Player).Base);
			}
			AddInfo(FString::Printf(TEXT("seed %d %s: %d players, %d overseas, %s left"), Seed, *Code, Team.Squad.Num(), Team.Overseas(), *AuctionRules::Money(Team.Purse)));
		}
		AddInfo(FString::Printf(TEXT("seed %d: %d sold, top %s, %d RTM, %d lots held, %.0f min"), Seed, Sold, *AuctionRules::Money(Top), Rtm, A.LotsHeld, A.Clock / 60.0));
		// The 2025 mega auction: Pant 27 Cr, Iyer 26.75, Venkatesh 23.75, and eleven more at 12 Cr or above.
		TestTrue(FString::Printf(TEXT("a star goes for 18-32 Cr (%d)"), Top), Top >= 1800 && Top <= 3200);
		TestTrue(FString::Printf(TEXT("a bidding war for several stars (%d at 12 Cr+)"), Big), Big >= 5);
		TestTrue(TEXT("a mega auction's worth of players sold"), Sold >= 120);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionSkip, "CRICKET26.Auction.Skip", AuctionTests::Flags)
bool FAuctionSkip::RunTest(const FString&)
{
	FAuction A(4);
	A.BeginAuction();
	AuctionTests::RunUntil(A, [&] { return A.Phase == EAuctionPhase::Bidding; });
	const int32 Lot = A.Lot;
	A.SkipLot();
	A.Tick(0.01f);
	TestTrue(TEXT("skipped lot decided in one tick (or waiting on our RTM)"), A.Phase == EAuctionPhase::Hammer || A.AwaitingHuman());
	TestTrue(TEXT("human did not bid"), !A.SoldTo.Contains(Lot) || A.SoldTo[Lot] != 4 || A.AwaitingHuman());
	const int32 Set = A.SetIndex;
	A.SkipSet();
	for (int32 I = 0; I < 50 && A.SetIndex == Set; ++I)
	{
		A.Tick(0.01f);
		if (A.Phase == EAuctionPhase::RtmAsk && A.IsHuman(A.RtmTeam)) A.HumanRtm(false);
	}
	TestTrue(TEXT("set skipped"), A.SetIndex > Set);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionNoRetentions, "CRICKET26.Auction.NoRetentions", AuctionTests::Flags)
bool FAuctionNoRetentions::RunTest(const FString&)
{
	FAuction A(INDEX_NONE, 2026);
	A.bNoRetentions = true;
	A.BeginAuction();
	TestEqual(TEXT("auction opened to set intro"), A.Phase, EAuctionPhase::SetIntro);
	TestEqual(TEXT("no players retained by anyone"), A.SoldTo.Num(), 0);
	for (int32 T = 0; T < 10; ++T)
	{
		TestEqual(TEXT("empty squad when no retentions"), A.Teams[T].Squad.Num(), 0);
		TestEqual(TEXT("full 120 Cr purse for all teams"), A.Teams[T].Purse, AuctionRules::Purse);
		TestEqual(TEXT("all 6 RTM cards available"), A.Teams[T].RtmCards, AuctionRules::KeepMax);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionFastResolveLot, "CRICKET26.Auction.FastResolveLot", AuctionTests::Flags)
bool FAuctionFastResolveLot::RunTest(const FString&)
{
	FAuction A(INDEX_NONE, 2026);
	A.BeginAuction();
	// Run until first lot opens
	AuctionTests::RunUntil(A, [&A]() { return A.Phase == EAuctionPhase::LotIntro || A.Phase == EAuctionPhase::Bidding; });
	TestTrue(TEXT("lot opened"), A.Lot != INDEX_NONE);
	const int32 LotPlayer = A.Lot;
	A.FastResolveCurrentLot();
	TestEqual(TEXT("lot resolved directly to hammer"), A.Phase, EAuctionPhase::Hammer);
	TestTrue(TEXT("player resolved as sold or unsold"), A.LastSoldTo != INDEX_NONE || A.Unsold.Contains(LotPlayer));
	TestTrue(TEXT("last event is sold or unsold"), !A.Events.IsEmpty()
		&& (A.Events.Last().Type == EAuctionEvent::Sold || A.Events.Last().Type == EAuctionEvent::Unsold));
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionCalibration, "CRICKET26.Auction.Calibration", AuctionTests::Flags)
bool FAuctionCalibration::RunTest(const FString&)
{
	// Whole AI auctions against the IPL 2025 mega auction (Jeddah, Nov 2024): 46 players retained for 558.5 Cr, then
	// 182 sold for 639.15 Cr, 62 of them from overseas, 8 Right to Match cards used, Pant the top price at 27 Cr, about
	// eighteen buys at 10 Cr or more, and every marquee name sold. Each seed's numbers must land in the band around
	// those, and so must the average.
	struct FBand { const TCHAR* Name; float Real, Lo, Hi; };
	const FBand Bands[] = {
		{ TEXT("retained"), 46.f, 32.f, 60.f },
		{ TEXT("retention spend (Cr)"), 558.f, 420.f, 700.f },
		{ TEXT("sold"), 182.f, 160.f, 205.f },
		{ TEXT("auction spend (Cr)"), 639.f, 540.f, 740.f },
		{ TEXT("overseas sold"), 62.f, 45.f, 80.f },
		{ TEXT("top price (Cr)"), 27.f, 20.f, 32.f },
		{ TEXT("buys at 10 Cr+"), 18.f, 10.f, 28.f },
		{ TEXT("RTM used"), 8.f, 2.f, 16.f },
	};
	constexpr int32 NB = UE_ARRAY_COUNT(Bands);
	const int32 Seeds[] = { 2026, 11, 99, 7, 123, 555, 31, 4242, 90210, 17, 64, 808 };
	float Sum[NB] = {};
	for (int32 Seed : Seeds)
	{
		FAuction A(INDEX_NONE, Seed);
		A.BeginAuction();
		TArray<int32> Marquee;
		for (const FAuctionSet& S : A.Sets) if (S.Code.StartsWith(TEXT("M"))) Marquee.Append(S.Players);
		if (!TestTrue(TEXT("finishes"), AuctionTests::RunUntil(A, [&] { return A.Phase == EAuctionPhase::Finished; }, 0.25f))) return false;
		float V[NB] = {};
		for (const FAuctionTeam& T : A.Teams)
			for (const FAuctionSigning& S : T.Squad)
			{
				if (S.bRetained) { V[0] += 1.f; V[1] += S.Price / 100.f; continue; }
				V[2] += 1.f;
				V[3] += S.Price / 100.f;
				V[4] += FAuction::Player(S.Player).IsOverseas();
				V[5] = FMath::Max(V[5], S.Price / 100.f);
				V[6] += S.Price >= 1000;
				V[7] += S.bRtm;
			}
		int32 MarqueeUnsold = 0;
		for (int32 P : Marquee) MarqueeUnsold += !A.SoldTo.Contains(P);
		FString Line = FString::Printf(TEXT("seed %5d:"), Seed);
		for (int32 B = 0; B < NB; ++B)
		{
			Line += FString::Printf(TEXT("  %s %.0f"), Bands[B].Name, V[B]);
			Sum[B] += V[B];
			TestTrue(FString::Printf(TEXT("seed %d %s %.1f in %.0f..%.0f"), Seed, Bands[B].Name, V[B], Bands[B].Lo, Bands[B].Hi), V[B] >= Bands[B].Lo && V[B] <= Bands[B].Hi);
		}
		AddInfo(Line + FString::Printf(TEXT("  marquee unsold %d"), MarqueeUnsold));
		TestTrue(FString::Printf(TEXT("seed %d: every marquee name sells"), Seed), MarqueeUnsold == 0);
		for (int32 T = 0; T < A.Teams.Num(); ++T)
		{
			const FAuctionTeam& Team = A.Teams[T];
			TestTrue(TEXT("squad 18..25"), Team.Squad.Num() >= AuctionRules::SquadMin && Team.Squad.Num() <= AuctionRules::SquadMax);
			TestTrue(TEXT("overseas <= 8"), Team.Overseas() <= AuctionRules::OverseasMax);
			TestTrue(TEXT("purse >= 0"), Team.Purse >= 0);
			TestTrue(TEXT("a keeper in every squad"), Team.CountRole(EAuctionRole::Keeper) >= 1);
		}
	}
	for (int32 B = 0; B < NB; ++B)
	{
		const float Mean = Sum[B] / UE_ARRAY_COUNT(Seeds);
		AddInfo(FString::Printf(TEXT("mean %-20s %7.1f   (2025: %.0f)"), Bands[B].Name, Mean, Bands[B].Real));
		// The average sits in the inner half of the band.
		const float Q = 0.25f * (Bands[B].Hi - Bands[B].Lo);
		TestTrue(FString::Printf(TEXT("mean %s %.1f near 2025's %.0f"), Bands[B].Name, Mean, Bands[B].Real), Mean >= Bands[B].Lo + Q && Mean <= Bands[B].Hi - Q);
	}
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

namespace AuctionTests
{
	FAuction RunAll(FAuction&& A)
	{
		A.BeginAuction();
		RunUntil(A, [&]
		{
			if (A.Phase == EAuctionPhase::RtmAsk && A.IsHuman(A.RtmTeam)) A.HumanRtm(false);
			if (A.Phase == EAuctionPhase::RtmRaise && A.IsHuman(A.Holder)) A.HumanFinalRaise(A.Price);
			if (A.Phase == EAuctionPhase::RtmMatch && A.IsHuman(A.RtmTeam)) A.HumanMatch(false);
			return A.Phase == EAuctionPhase::Finished;
		}, 0.25f);
		return MoveTemp(A);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionMini, "CRICKET26.Auction.MiniAuction", AuctionTests::Flags)
bool FAuctionMini::RunTest(const FString&)
{
	// The IPL 2027 mini auction: sides keep what they like of their 2026 squads on their contracts, release the rest,
	// and buy to fill up to 25 with the purse left under the 120 Cr cap. No marquee sets, no Right to Match, and an
	// overseas buy above 18 Cr is paid 18 with the rest going to the board (Cameron Green, 25.20 Cr, Dec 2025).
	FAuctionConfig C;
	C.Mode = EAuctionMode::Mini;
	C.Season = 2027;
	FAuction A(C, 2026);
	TestTrue(TEXT("2027 is not a mega season"), !AuctionRules::IsMegaSeason(2027) && AuctionRules::IsMegaSeason(2028) && AuctionRules::IsMegaSeason(2031));
	const TArray<int32> Kept = A.AiRetentions(0);
	int32 Contracts = 0;
	for (int32 P : Kept) Contracts += A.ContractOf(P);
	TestTrue(FString::Printf(TEXT("CSK keeps most of its squad (%d)"), Kept.Num()), Kept.Num() >= 12 && Kept.Num() <= 21);
	TestEqual(TEXT("keeping costs the contracts"), A.RetentionCost(Kept), Contracts);
	TestTrue(TEXT("more than six can be kept"), A.CanRetain(0, Kept));
	A.BeginAuction();
	for (const FAuctionSet& S : A.Sets) TestFalse(TEXT("no marquee in a mini auction"), S.Code.StartsWith(TEXT("M")));
	for (const FAuctionTeam& T : A.Teams) TestEqual(TEXT("no RTM cards"), T.RtmCards, 0);
	int32 Released = 0;
	for (int32 P = 0; P < AuctionData::Players().Num(); ++P) Released += A.OwnerOf(P) != INDEX_NONE && !A.SoldTo.Contains(P);
	AddInfo(FString::Printf(TEXT("%d released into the pool"), Released));
	TestTrue(TEXT("sides release players"), Released >= 30);
	AuctionTests::RunUntil(A, [&] { return A.Phase == EAuctionPhase::Finished; }, 0.25f);
	int32 Bought = 0, Spend = 0;
	for (const FAuctionEventRecord& E : A.Events) TestTrue(TEXT("never a Right to Match"), E.Type != EAuctionEvent::RtmOffered);
	for (const FAuctionTeam& T : A.Teams)
	{
		TestTrue(TEXT("squad 18..25"), T.Squad.Num() >= 18 && T.Squad.Num() <= 25);
		TestTrue(TEXT("purse >= 0"), T.Purse >= 0);
		for (const FAuctionSigning& S : T.Squad)
		{
			if (S.bRetained) continue;
			++Bought;
			Spend += S.Price;
			TestEqual(TEXT("overseas fee capped"), S.Fee, AuctionRules::Fee(EAuctionMode::Mini, FAuction::Player(S.Player).IsOverseas(), S.Price));
		}
	}
	AddInfo(FString::Printf(TEXT("mini auction: %d bought for %s"), Bought, *AuctionRules::Money(Spend)));
	// Dec 2025: 77 places filled for 215.45 Cr.
	TestTrue(FString::Printf(TEXT("a mini auction's worth of buys (%d, 2025: 77)"), Bought), Bought >= 45 && Bought <= 115);
	TestTrue(FString::Printf(TEXT("a mini auction's spend (%s, 2025: 215 Cr)"), *AuctionRules::Money(Spend)), Spend >= 12000 && Spend <= 34000);
	TestEqual(TEXT("fee rule"), AuctionRules::Fee(EAuctionMode::Mini, true, 2520), 1800);
	TestEqual(TEXT("no cap for Indians"), AuctionRules::Fee(EAuctionMode::Mini, false, 2520), 2520);
	TestEqual(TEXT("no cap in a mega auction"), AuctionRules::Fee(EAuctionMode::Mega, true, 2700), 2700);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionTrades, "CRICKET26.Auction.Trades", AuctionTests::Flags)
bool FAuctionTrades::RunTest(const FString&)
{
	FAuction A(0, 2026);
	const TArray<int32> Mine = A.RetentionCandidates(0), Theirs = A.RetentionCandidates(1);
	FString Why;
	TestFalse(TEXT("cannot trade another side's player"), A.ProposeTrade(0, Theirs[0], 1, Mine[0], &Why));
	// A lopsided offer is turned down: our worst for their best.
	TestFalse(TEXT("a bad offer is refused"), A.ProposeTrade(0, Mine.Last(), 1, Theirs[0], &Why));
	TestEqual(TEXT("with a reason"), Why, FString(TEXT("they turned it down")));
	// A generous one is taken: our best for one of their Indian squad players.
	const int32* Squaddie = Theirs.FindByPredicate([&](int32 P) { return !FAuction::Player(P).IsOverseas() && P != Theirs[0]; });
	if (!TestNotNull(TEXT("an Indian squad player to ask for"), Squaddie)) return false;
	const int32 Get = *Squaddie;
	const bool bTaken = A.ProposeTrade(0, Mine[0], 1, Get, &Why);
	TestTrue(FString::Printf(TEXT("a generous offer is taken (%s)"), *Why), bTaken);
	if (bTaken)
	{
		TestEqual(TEXT("he is theirs now"), A.OwnerOf(Mine[0]), 1);
		TestTrue(TEXT("and theirs is ours"), A.RetentionCandidates(0).Contains(Get));
		TestEqual(TEXT("logged"), A.Trades.Num(), 1);
	}
	A.OpenTradeWindow();
	A.BeginAuction();
	TestFalse(TEXT("the window closes when the auction begins"), A.ProposeTrade(0, Mine[1], 2, A.RetentionCandidates(2).IsEmpty() ? 0 : A.RetentionCandidates(2)[0]));
	// A traded player's Right to Match belongs to his new side.
	if (bTaken) TestTrue(TEXT("the new side holds his RTM, or kept him"), A.SoldTo.FindRef(Mine[0], INDEX_NONE) == 1 || A.SoldTo.FindRef(Mine[0], INDEX_NONE) == INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionDays, "CRICKET26.Auction.Days", AuctionTests::Flags)
bool FAuctionDays::RunTest(const FString&)
{
	// Jeddah ran over two days: day one the marquee and the capped sets, day two from the first uncapped set.
	FAuction A(INDEX_NONE, 11);
	A.BeginAuction();
	TestTrue(TEXT("day one ends"), AuctionTests::RunUntil(A, [&] { return A.Phase == EAuctionPhase::Break; }, 0.25f));
	TestTrue(TEXT("before the first uncapped set"), A.CurrentSet() && A.CurrentSet()->Name.StartsWith(TEXT("UNCAPPED")));
	TestEqual(TEXT("still day one"), A.Day, 1);
	TestTrue(TEXT("day two starts"), AuctionTests::RunUntil(A, [&] { return A.Phase == EAuctionPhase::SetIntro; }, 0.25f));
	TestEqual(TEXT("day two"), A.Day, 2);
	int32 Ended = 0, Started = 0;
	for (const FAuctionEventRecord& E : A.Events) { Ended += E.Type == EAuctionEvent::DayEnded; Started += E.Type == EAuctionEvent::DayStarted; }
	TestTrue(TEXT("called once each"), Ended == 1 && Started == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionSaveResume, "CRICKET26.Auction.SaveResume", AuctionTests::Flags)
bool FAuctionSaveResume::RunTest(const FString&)
{
	FAuctionConfig C;
	C.Humans = { 3, 6 };
	C.Difficulty = EAuctionDifficulty::Legend;
	FAuction A(C, 4242);
	A.BeginAuction();
	A.SetWish(3, A.Sets[2].Players[0], 900, true);
	AuctionTests::RunUntil(A, [&]
	{
		if (A.AwaitingHuman()) { A.HumanRtm(false); A.HumanFinalRaise(A.Price); A.HumanMatch(false); }
		return A.LotsHeld >= 40 && A.Phase == EAuctionPhase::Hammer;
	}, 0.25f);
	const FString Saved = A.SaveState();
	FAuctionConfig Read;
	int32 Seed = 0;
	if (!TestTrue(TEXT("header reads back"), FAuction::ReadSaveHeader(Saved, Read, Seed))) return false;
	TestEqual(TEXT("seed"), Seed, 4242);
	TestTrue(TEXT("both human tables"), Read.Humans == C.Humans);
	TestEqual(TEXT("difficulty"), int32(Read.Difficulty), int32(EAuctionDifficulty::Legend));
	FAuction B(Read, Seed);
	if (!TestTrue(TEXT("loads"), B.LoadState(Saved))) return false;
	TestEqual(TEXT("same lots held"), B.LotsHeld, A.LotsHeld);
	TestEqual(TEXT("same events"), B.Events.Num(), A.Events.Num());
	TestEqual(TEXT("same sets"), B.Sets.Num(), A.Sets.Num());
	for (int32 T = 0; T < A.Teams.Num(); ++T)
	{
		TestEqual(TEXT("same purse"), B.Teams[T].Purse, A.Teams[T].Purse);
		TestEqual(TEXT("same squad"), B.Teams[T].Squad.Num(), A.Teams[T].Squad.Num());
		TestEqual(TEXT("same RTM cards"), B.Teams[T].RtmCards, A.Teams[T].RtmCards);
	}
	TestEqual(TEXT("the shortlist comes back"), B.Wishes(3).Num(), A.Wishes(3).Num());
	TestEqual(TEXT("the save of a load is the save"), B.SaveState(), Saved);
	TestFalse(TEXT("a save needs the same tables"), FAuction(INDEX_NONE, 4242).LoadState(Saved));
	// And the resumed auction runs to a legal end.
	AuctionTests::RunUntil(B, [&]
	{
		if (B.AwaitingHuman()) { B.HumanRtm(false); B.HumanFinalRaise(B.Price); B.HumanMatch(false); }
		return B.Phase == EAuctionPhase::Finished;
	}, 0.25f);
	TestEqual(TEXT("resumed auction finishes"), B.Phase, EAuctionPhase::Finished);
	for (const FAuctionTeam& T : B.Teams) TestTrue(TEXT("legal squad"), T.Squad.Num() <= 25 && T.Purse >= 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionHumanTools, "CRICKET26.Auction.HumanTools", AuctionTests::Flags)
bool FAuctionHumanTools::RunTest(const FString&)
{
	FAuction A(5, 2026);
	A.BeginAuction();
	AuctionTests::RunUntil(A, [&] { return A.Phase == EAuctionPhase::Bidding; });
	const int32 Lot = A.Lot, Base = FAuction::Player(Lot).Base;
	// A jump lands on the ladder.
	TestTrue(TEXT("jump bid"), A.HumanJumpBid(5, Base + 33));
	TestEqual(TEXT("on the ladder"), A.Price, AuctionRules::OnLadder(Base + 33, Base));
	TestFalse(TEXT("not over your own bid"), A.HumanJumpBid(5, A.Price + 500));
	// A timeout holds the hammer.
	AuctionTests::RunUntil(A, [&] { return A.Holder != 5 || A.Phase != EAuctionPhase::Bidding; });
	if (A.Phase == EAuctionPhase::Bidding)
	{
		const int32 Left = A.Teams[5].Timeouts;
		TestTrue(TEXT("a timeout"), A.RequestTimeout(5));
		TestFalse(TEXT("one a lot"), A.RequestTimeout(5));
		TestEqual(TEXT("one fewer left"), A.Teams[5].Timeouts, Left - 1);
		const double Asked = A.Clock;
		const int32 Holding = A.Holder;
		AuctionTests::RunUntil(A, [&] { return A.Phase != EAuctionPhase::Bidding || A.Holder != Holding; });
		if (A.Holder == Holding) TestTrue(TEXT("no hammer inside the timeout"), A.Clock - Asked >= FAuction::TimeoutTime);
	}
	// Auto-bid: the paddle goes up for us up to the limit, never past it.
	AuctionTests::RunUntil(A, [&] { return A.Phase == EAuctionPhase::LotIntro && A.Lot != Lot; });
	const int32 Next = A.Lot, Limit = FMath::Max(FAuction::Player(Next).Base * 3, 300);
	A.SetWish(5, Next, Limit, true);
	int32 MaxOurs = 0;
	AuctionTests::RunUntil(A, [&]
	{
		if (A.Holder == 5) MaxOurs = FMath::Max(MaxOurs, A.Price);
		if (A.AwaitingHuman()) { A.HumanRtm(false); A.HumanFinalRaise(A.Price); A.HumanMatch(false); }
		return A.Phase == EAuctionPhase::Hammer;
	});
	TestTrue(TEXT("auto-bid bid"), MaxOurs > 0);
	TestTrue(TEXT("never past the limit"), MaxOurs <= Limit);
	TestFalse(TEXT("an analyst has something to say"), A.Whisper(5).IsEmpty() && A.Lot != INDEX_NONE);
	const FAuctionNeeds N = A.Needs(5);
	TestTrue(TEXT("needs name the best twelve"), N.BestXI.Num() <= AuctionRules::Side && N.Rank >= 1 && N.Rank <= 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionHotSeat, "CRICKET26.Auction.HotSeat", AuctionTests::Flags)
bool FAuctionHotSeat::RunTest(const FString&)
{
	// Pass the paddle: two people at two tables, the AI at the other eight.
	FAuctionConfig C;
	C.Humans = { 0, 2 };
	FAuction A(C, 99);
	TestTrue(TEXT("both tables are people"), A.IsHuman(0) && A.IsHuman(2) && !A.IsHuman(1));
	TestEqual(TEXT("the first is the lead"), A.Human, 0);
	A.BeginAuction();
	AuctionTests::RunUntil(A, [&] { return A.Phase == EAuctionPhase::Bidding; });
	TestTrue(TEXT("table 0 opens"), A.HumanBid(0));
	TestTrue(TEXT("table 2 raises"), A.HumanBid(2));
	TestEqual(TEXT("table 2 holds"), A.Holder, 2);
	TestFalse(TEXT("the AI cannot use a human paddle"), A.HumanBid(1));
	FAuction Done = AuctionTests::RunAll(MoveTemp(A));
	TestEqual(TEXT("finishes"), Done.Phase, EAuctionPhase::Finished);
	for (int32 T : C.Humans) TestTrue(TEXT("a human side that passes still ends legal"), Done.Teams[T].Purse >= 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionVerdicts, "CRICKET26.Auction.Verdicts", AuctionTests::Flags)
bool FAuctionVerdicts::RunTest(const FString&)
{
	FAuction A = AuctionTests::RunAll(FAuction(INDEX_NONE, 2026));
	TSet<FString> Grades;
	int32 Ranks = 0;
	for (int32 T = 0; T < A.Teams.Num(); ++T)
	{
		const FAuctionVerdict V = A.Verdict(T);
		Grades.Add(V.Grade);
		Ranks += V.Needs.Rank;
		AddInfo(FString::Printf(TEXT("%s %s (%.0f): %s"), *AuctionData::Franchises()[T].Code, *V.Grade, V.Score, *V.Summary));
		TestTrue(TEXT("a grade"), !V.Grade.IsEmpty() && V.Score >= 0.f && V.Score <= 100.f);
		TestEqual(TEXT("a best twelve"), V.Needs.BestXI.Num(), AuctionRules::Side);
	}
	TestTrue(TEXT("the grades spread"), Grades.Num() >= 3);
	TestEqual(TEXT("ranks 1..10"), Ranks, 55);
	const TArray<FAuctionRecord> R = A.Records();
	TestTrue(TEXT("records"), R.Num() >= 3 && R[0].Title == TEXT("MOST EXPENSIVE") && R[0].Price >= 1500);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionCareer, "CRICKET26.Auction.Career", AuctionTests::Flags)
bool FAuctionCareer::RunTest(const FString&)
{
	// A career's next mega auction (2028): the squads of the season just played carry in, players are a year older,
	// and a man's Right to Match belongs to the side he played for last.
	FAuction First = AuctionTests::RunAll(FAuction(INDEX_NONE, 7));
	FAuctionConfig C;
	C.Season = 2028;
	C.Humans = { 4 };
	for (const FAuctionTeam& T : First.Teams) C.Carried.Add(T.Squad);
	FAuction A(C, 8);
	const int32 Signed = First.Teams[4].Squad[0].Player;
	TestEqual(TEXT("carried squads are the owners"), A.OwnerOf(Signed), 4);
	TestEqual(TEXT("a year older"), A.AgeOf(Signed), FAuction::Player(Signed).Age + 1);
	TestTrue(TEXT("retention candidates come from the carried squad"), A.RetentionCandidates(4).Contains(Signed));
	A.BeginAuction();
	TestTrue(TEXT("a mega auction has marquee sets"), A.Sets[0].Code == TEXT("M1"));
	return true;
}

#endif
