// IPL mega auction rules and AI: the increment ladder, money formatting, retentions, the Right to Match, and
// whole AI-only auctions that must end with every squad legal.

#include "Misc/AutomationTest.h"
#include "AuctionEngine.h"
#include "AuctionCalls.h"
#include "CricketCommentary.h"
#include "CricketAudio.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AuctionRoom.h"
#include "Components/SkeletalMeshComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AuctionTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	// Runs until the auction ends or the human is asked something; false if it never settles.
	bool RunUntil(FAuction& A, TFunctionRef<bool()> Stop, float Step = 0.1f, int32 MaxSteps = 4000000)
	{
		for (int32 I = 0; I < MaxSteps; ++I)
		{
			if (Stop()) return true;
			A.Tick(Step);
		}
		return false;
	}
}

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
	const TArray<int32> Keep = { Capped[0], Capped[1], Uncapped[0] };
	TestTrue(TEXT("3 kept is legal"), A.CanRetain(0, Keep));
	TestEqual(TEXT("cost 18 + 14 + 4"), FAuction::RetentionCost(Keep), 3600);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionCalls, "CRICKET26.Auction.Calls", AuctionTests::Flags)
bool FAuctionCalls::RunTest(const FString&)
{
	TestEqual(TEXT("a new crore in full"), AuctionCalls::Short(300, 280), FString(TEXT("3 crore")));
	TestEqual(TEXT("within the crore, the lakhs"), AuctionCalls::Short(320, 300), FString(TEXT("20")));
	TestEqual(TEXT("below a crore, bare lakhs"), AuctionCalls::Short(45, 40), FString(TEXT("45")));
	TestEqual(TEXT("seat names follow the banks"), AuctionCalls::Where(6) + TEXT("|") + AuctionCalls::Where(9), FString(TEXT("on my far left|on my far right")));

	// Every line of a stretch of AI auction reads as speech: no unfilled placeholders, names where they belong.
	FAuction A(INDEX_NONE, 7);
	A.BeginAuction();
	AuctionTests::RunUntil(A, [&] { return A.LotsHeld >= 60; }, 0.25f);
	int32 Lots = 0, Solds = 0, Backs = 0;
	for (const FAuctionEventRecord& E : A.Events)
	{
		const FString L = AuctionCalls::Line(A, E);
		if (!TestFalse(FString::Printf(TEXT("filled: %s"), *L), L.Contains(TEXT("{")) || L.Contains(TEXT("%")))) return false;
		if (E.Type == EAuctionEvent::LotOpened) { ++Lots; TestTrue(TEXT("lot intro names the player"), L.Contains(FAuction::Player(E.Player).Name) && L.Contains(TEXT("number"))); }
		if (E.Type == EAuctionEvent::Sold) { ++Solds; TestTrue(TEXT("sold names the buyer"), L.Contains(AuctionData::Franchises()[E.Team].Name)); }
		Backs += E.Type == EAuctionEvent::Bid && L.Contains(TEXT("back"));
	}
	TestTrue(TEXT("lots and sales called"), Lots >= 60 && Solds >= 20);
	TestTrue(TEXT("duels called as teams come back"), Backs >= 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionVoice, "CRICKET26.Auction.Voice", AuctionTests::Flags)
bool FAuctionVoice::RunTest(const FString&)
{
	// The voice stitches every line from recorded pieces, and a line with a piece nobody recorded stays a caption.
	// Every piece of a whole auction must be in the script the generator records.
	const TArray<FString> Script = AuctionCalls::Script();
	TSet<FString> Keys;
	for (const FString& Piece : Script) Keys.Add(CricketCommentary::ClipKey(Piece));
	FAuction A(INDEX_NONE, 7);
	A.BeginAuction();
	if (!TestTrue(TEXT("finishes"), AuctionTests::RunUntil(A, [&] { return A.Phase == EAuctionPhase::Finished; }, 0.25f))) return false;
	int32 Spoken = 0;
	for (const FAuctionEventRecord& E : A.Events)
	{
		const AuctionCalls::FCall C = AuctionCalls::Call(A, E);
		TestEqual(TEXT("the pieces make the line"), FString::Join(C.Pieces, TEXT("")), C.Text);
		for (const FString& Piece : C.Pieces)
		{
			const FString Key = CricketCommentary::ClipKey(Piece);
			if (!Key.IsEmpty() && !TestTrue(FString::Printf(TEXT("scripted: \"%s\" in \"%s\""), *Piece, *C.Text), Keys.Contains(Key))) return false;
		}
		Spoken += !C.Text.IsEmpty();
	}
	TestTrue(TEXT("a whole auction's worth of lines"), Spoken > 1000);
	// Stitching: the clips in order, a sentence's pause at a full stop, and silence when a piece is unrecorded.
	const FString Dir = FPaths::ProjectContentDir() / TEXT("Audio/Auction");
	const TArray<uint8> Ten = { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 };
	FFileHelper::SaveArrayToFile(Ten, *(Dir / TEXT("zzqa_test.pcm")));
	FFileHelper::SaveArrayToFile(Ten, *(Dir / TEXT("zzqb.pcm")));
	TestEqual(TEXT("stitched with a full stop's pause"), AuctionCalls::Voice({ TEXT("Zzqa test. Zzqb"), { TEXT("Zzqa test"), TEXT(". "), TEXT("Zzqb") } }).Num(),
		20 + int32(0.3f * CricketAudio::SampleRate));
	TestTrue(TEXT("an unrecorded piece silences the line"), AuctionCalls::Voice({ TEXT("Zzqa test, zzqc"), { TEXT("Zzqa test, "), TEXT("zzqc") } }).IsEmpty());
	IFileManager::Get().Delete(*(Dir / TEXT("zzqa_test.pcm")));
	IFileManager::Get().Delete(*(Dir / TEXT("zzqb.pcm")));
	// The generator's input (Scripts/audio/auctioneer.py).
	return TestTrue(TEXT("script written"), FFileHelper::SaveStringArrayToFile(Script, *(FPaths::ProjectSavedDir() / TEXT("AuctionScript.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
}

#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionRoomFaces, "CRICKET26.Auction.RoomFaces", AuctionTests::Flags)
bool FAuctionRoomFaces::RunTest(const FString&)
{
	// Faces posed only when seen kept their standing pose off screen and showed it, necks stretched up from the seated
	// bodies, on the first frame after the director cut to their table. The body and face must pose off screen.
	AActor* Person = NewObject<AActor>();
	USkeletalMeshComponent* Body = NewObject<USkeletalMeshComponent>(Person, TEXT("Body"));
	USkeletalMeshComponent* Face = NewObject<USkeletalMeshComponent>(Person, TEXT("Face"));
	USkeletalMeshComponent* Torso = NewObject<USkeletalMeshComponent>(Person, TEXT("Torso"));
	for (USkeletalMeshComponent* Part : { Body, Face, Torso }) Part->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	TestEqual(TEXT("returns the body"), AAuctionRoom::PrepareParts(Person), Body);
	TestEqual(TEXT("body poses off screen"), Body->VisibilityBasedAnimTickOption, EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
	TestEqual(TEXT("face poses off screen"), Face->VisibilityBasedAnimTickOption, EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
	TestEqual(TEXT("garments left to follow the body"), Torso->VisibilityBasedAnimTickOption, EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
	for (USkeletalMeshComponent* Part : { Body, Face, Torso })
		TestTrue(TEXT("every part in the tables' face fill"), Part->LightingChannels.bChannel0 && Part->LightingChannels.bChannel1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionRoomSeats, "CRICKET26.Auction.RoomSeats", AuctionTests::Flags)
bool FAuctionRoomSeats::RunTest(const FString&)
{
	using namespace AuctionRoomLayout;
	int32 FrontCount[2] = { 0, 0 }, RearCount[2] = { 0, 0 };
	for (int32 T = 0; T < 10; ++T)
	{
		const FVector Front = TableFront(T);
		const int32 Bank = Front.Y > 0.f ? 1 : 0;
		TestTrue(TEXT("tables face the stage"), TableFacing(T).Equals(-FVector::ForwardVector));
		TestTrue(TEXT("centre aisle clear"), FMath::Abs(Front.Y) > TableHalfLength + 2.f);
		if (FMath::IsNearlyZero(Front.Z)) ++FrontCount[Bank];
		else { ++RearCount[Bank]; TestTrue(TEXT("rear tables on riser"), FMath::IsNearlyEqual(Front.Z, RearRowHeight)); }
		for (int32 Other = 0; Other < T; ++Other)
			TestTrue(TEXT("tables do not overlap"), FVector::Dist2D(Front, TableFront(Other)) > 2.f * TableHalfLength + 0.5f);
		// The table shots and the auctioneer's glances aim halfway from seat 0 to seat 1: that must be the table's middle.
		const FVector Middle = TableFront(T) - TableFacing(T) * (2.f * TableHalfDepth + 0.25f);
		TestTrue(TEXT("seats 0 and 1 straddle the middle"), (0.5f * (SeatAt(T, 0) + SeatAt(T, 1)) - Middle).Size() < 0.01f);
		for (int32 S = 0; S < SeatsPerTable; ++S)
		{
			TestTrue(TEXT("seat along the table"), (SeatAt(T, S) - Middle).Size() < TableHalfLength - 0.3f);
			for (int32 O = 0; O < S; ++O)
			{
				TestTrue(TEXT("room for a chair between seats"), (SeatAt(T, S) - SeatAt(T, O)).Size() > 0.7f);
				// Eleven faces for thirty seats repeat across tables, never at one.
				TestNotEqual(TEXT("different people at one table"), StaffFor(T, S, 11), StaffFor(T, O, 11));
				TestNotEqual(TEXT("different people with a short cast"), StaffFor(T, S, 3), StaffFor(T, O, 3));
			}
		}
	}
	for (int32 Bank = 0; Bank < 2; ++Bank)
	{
		TestEqual(TEXT("three front tables per bank"), FrontCount[Bank], 3);
		TestEqual(TEXT("two raised tables per bank"), RearCount[Bank], 2);
	}
	TestTrue(TEXT("Punjab ahead of Hyderabad in same bank"), TableFront(6).Y * TableFront(4).Y > 0.f && TableFront(6).X < TableFront(4).X);
	TestEqual(TEXT("a cast of one fills every seat"), StaffFor(4, 2, 1), 0);
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
