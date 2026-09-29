// The auction's presentation: the auctioneer's calls and voice, and the room's people and seats. The rules and the AI
// are tested in AuctionEngineTests.cpp, which builds without the rest of the game (Scripts/auction/sim/run.sh).

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
#include "AuctionTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

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
