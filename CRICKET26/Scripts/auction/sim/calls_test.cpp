// The auctioneer's lines, headless: every event of whole auctions (mega and mini, with people at the tables) reads as
// speech, and every piece of every line is in the script Scripts/audio/auctioneer.py records. The editor runs the
// same checks as CRICKET26.Auction.Calls and CRICKET26.Auction.Voice.
#include "AuctionTestUtil.h"
#include "AuctionCalls.h"
#include "CricketCommentary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionCallsHeadless, "Headless.Auction.CallsAndScript", AuctionTests::Flags)
bool FAuctionCallsHeadless::RunTest(const FString&)
{
	TSet<FString> Keys;
	for (const FString& Piece : AuctionCalls::Script()) Keys.Add(CricketCommentary::ClipKey(Piece));
	AddInfo(FString::Printf(TEXT("%d pieces in the script"), Keys.Num()));
	FAuctionConfig Mini;
	Mini.Mode = EAuctionMode::Mini;
	Mini.Humans = { 1 };
	FAuctionConfig Mega;
	Mega.Humans = { 0, 5 };
	for (const FAuctionConfig& C : { FAuctionConfig(), Mini, Mega })
	{
		FAuction A(C, 7);
		A.BeginAuction();
		int32 Asked = 0;
		AuctionTests::RunUntil(A, [&]
		{
			// People at the tables: bid now and then, call a timeout, answer the RTM questions.
			for (int32 T : C.Humans)
			{
				if (A.Phase == EAuctionPhase::Bidding && A.CanHumanBid(T) && A.Price < 300 && (A.LotsHeld % 7) == T) A.HumanBid(T);
				if (A.Phase == EAuctionPhase::Bidding && (A.LotsHeld % 11) == 0) A.RequestTimeout(T);
			}
			if (A.AwaitingHuman()) { ++Asked; A.HumanRtm(true); A.HumanFinalRaise(A.Price); A.HumanMatch(Asked % 2 == 0); }
			return A.Phase == EAuctionPhase::Finished;
		}, 0.25f);
		int32 Spoken = 0;
		TSet<int32> Types;
		for (const FAuctionEventRecord& E : A.Events)
		{
			const AuctionCalls::FCall Call = AuctionCalls::Call(A, E);
			if (!TestFalse(FString::Printf(TEXT("filled: %s"), *Call.Text), Call.Text.Contains(TEXT("{")) || Call.Text.Contains(TEXT("%")))) return false;
			TestEqual(TEXT("the pieces make the line"), FString::Join(Call.Pieces, TEXT("")), Call.Text);
			for (const FString& Piece : Call.Pieces)
			{
				const FString Key = CricketCommentary::ClipKey(Piece);
				if (!Key.IsEmpty() && !TestTrue(FString::Printf(TEXT("scripted: \"%s\" in \"%s\""), *Piece, *Call.Text), Keys.Contains(Key))) return false;
			}
			Spoken += !Call.Text.IsEmpty();
			if (!Call.Text.IsEmpty()) Types.Add(int32(E.Type));
		}
		AddInfo(FString::Printf(TEXT("%s: %d lines, %d kinds of event voiced"), C.Mode == EAuctionMode::Mini ? TEXT("mini") : TEXT("mega"), Spoken, Types.Num()));
		TestTrue(TEXT("a whole auction's worth of lines"), Spoken > 500);
		TestTrue(TEXT("the day's end is called in a mega auction"), C.Mode == EAuctionMode::Mini || Types.Contains(int32(EAuctionEvent::DayEnded)));
	}
	return true;
}
