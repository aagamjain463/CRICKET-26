#include "AuctionCalls.h"
#include "AuctionRoom.h"
#include "CricketCommentary.h"
#include "CricketAudio.h"

namespace
{
	/** Every phrasing the auctioneer has, by moment; {0}, {1}... are the names and prices said into it. */
	enum class EPhrase : uint8 { Welcome, NextSet, Accelerated, LotOpened, Opening, FirstBid, NewPaddle, Back, Raise, Huddle, Out, GoingOnce, GoingTwice,
		Sold, SoldRtm, Record, Unsold, RtmOffered, RtmUsed, RtmDeclined, FinalRaise, RtmMatched, RtmNotMatched, Finished,
		WelcomeMini, FinishedMini, Timeout, DayEnded, DayStarted, FeeCapped, Sticking, IplRecord, Count };

	const TArray<const TCHAR*>& Phrasings(EPhrase Phrase)
	{
		static const TArray<const TCHAR*> Table[] = {
			{ TEXT("Good afternoon, ladies and gentlemen, and a very warm welcome to the IPL mega auction. Ten franchises, one hundred and twenty crore each. We begin with marquee set one.") },
			{ TEXT("We move now to {0}."), TEXT("Next up, {0}."), TEXT("That completes the set. We move to {0}.") },
			{ TEXT("Ladies and gentlemen, we now move into the accelerated part of the auction. Only the players the franchises have nominated come back to the table.") },
			{ TEXT("Our next player up is number {0}. This is {1}, the {2} from {3}."), TEXT("Player number {0}. {1}, {2}, from {3}."), TEXT("Next, number {0}. {1}, the {2} from {3}.") },
			{ TEXT("Can I open here at {0}, please? Any paddles?"), TEXT("Let's open at {0}. Anybody coming in at {0}?"), TEXT("The opening bid is {0}. At {0}, do I have any teams?") },
			{ TEXT("Thank you, {0}, {1}. {2}."), TEXT("{0} come in straight away, {1}. {2}."), TEXT("Thank you. {0} start me off, {1}, at {2}.") },
			{ TEXT("{0}, a new paddle! {1} come in, {2}. Ahead of you, {3}."), TEXT("{0}, and welcome {1}, {2}. Ahead of you, {3}.") },
			{ TEXT("{0}, back with {1}."), TEXT("{0}, {1} come back."), TEXT("{0}, goes back to {1}."), TEXT("{0}, {1}.") },
			{ TEXT("{0}, with {1}."), TEXT("{0}, say {1}.") },
			{ TEXT("{0} ask for a second. Sure, take your time."), TEXT("A huddle at the {0} table. Give them a second."), TEXT("{0} are thinking about it.") },
			{ TEXT("{0} shake their heads. They're done."), TEXT("{0} are out at {1}."), TEXT("And {0} are done.") },
			{ TEXT("All done at {0}, with {1}, {2}? Last chance for any other team."), TEXT("The bid is {0}, with {1}, {2}. Quick scan of the room... any more?"),
				TEXT("At {0}, {1}, {2}. All done, all through? Fair warning.") },
			{ TEXT("Going once... going twice..."), TEXT("Selling then, if everybody's completely sure. Going once... going twice..."),
				TEXT("Last chance, lift your paddle. Going once... going twice...") },
			{ TEXT("And sold! {0} goes to {1} for {2}. Well done, {3}."), TEXT("Sold, to {1}... {0}, for {2}. Congratulations, {3}."), TEXT("Gone! {0} to {1} at {2}. Well done, {3}.") },
			{ TEXT("Sold to {0}, with the right to match, at {1}. Welcome back, {2}.") },
			{ TEXT(" The most expensive player of the auction so far.") },
			{ TEXT("No paddles at all? {0} goes unsold."), TEXT("No interest. {0} is unsold."), TEXT("Unsold. {0} passes.") },
			{ TEXT("Before the hammer comes down: {0}, would you like to use your right to match at {1}?") },
			{ TEXT("They would! {0} use the RTM. {1}, may I request your final bid, please?") },
			{ TEXT("No, they would not. No right to match from {0}."), TEXT("{0} pass on the RTM.") },
			{ TEXT("{0}, says {1}. That's their final bid. {2}, would you like to match that?") },
			{ TEXT("Yes, they would! {0} match at {1}.") },
			{ TEXT("No, they would not. So {0} stays with {1} at {2}.") },
			{ TEXT("And that brings the IPL mega auction to a close. Ten new squads. Thank you, everyone, and good luck for the season.") },
			{ TEXT("Good afternoon, ladies and gentlemen, and a very warm welcome to the IPL auction. Ten franchises, and the places they have to fill. We begin with {0}.") },
			{ TEXT("And that brings the IPL auction to a close. Thank you, everyone, and good luck for the season.") },
			{ TEXT("{0} ask for a moment. Of course, take your time."), TEXT("A timeout for {0}. We'll wait."), TEXT("{0} would like a word at the table. Take your time.") },
			{ TEXT("And that brings day one to a close. Thank you, everyone. We resume tomorrow with the uncapped players.") },
			{ TEXT("Good afternoon, and welcome back to day two of the IPL mega auction. We begin with the uncapped sets.") },
			{ TEXT("Under the overseas cap he will be paid {0}. The rest of that goes to the board.") },
			{ TEXT("{0} on the table. {1} are thinking about it."), TEXT("{0}. That's a big number. {1} take a moment."), TEXT("We're at {0}. {1}, is that a paddle?") },
			{ TEXT(" The most expensive player in IPL history.") },
		};
		static_assert(UE_ARRAY_COUNT(Table) == int32(EPhrase::Count), "a row per phrase");
		return Table[int32(Phrase)];
	}

	/** One of the phrasings, the same one every time this event is replayed. */
	const TCHAR* Pick(const FAuctionEventRecord& E, EPhrase Phrase)
	{
		const TArray<const TCHAR*>& Options = Phrasings(Phrase);
		const uint32 H = HashCombine(GetTypeHash(int32(E.Time * 1000.0)), GetTypeHash(int32(E.Type) * 31 + E.Team));
		return Options[H % Options.Num()];
	}

	/** Fills the phrasing in, and splits it where the voice stitches its clips: fixed words, then each name or price. */
	void Say(AuctionCalls::FCall& Out, const TCHAR* Format, const TArray<FString>& Args = {})
	{
		FString Rest = Format;
		int32 Open;
		while (Rest.FindChar(TEXT('{'), Open))
		{
			const int32 Close = Rest.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open);
			if (Open > 0) Out.Pieces.Add(Rest.Left(Open));
			Out.Pieces.Add(Args[FCString::Atoi(*Rest.Mid(Open + 1, Close - Open - 1))]);
			Rest.RightChopInline(Close + 1);
		}
		if (!Rest.IsEmpty()) Out.Pieces.Add(Rest);
		Out.Text = FString::Join(Out.Pieces, TEXT(""));
	}

	/** The bids on this player before this event, oldest first. */
	TArray<const FAuctionEventRecord*> BidsBefore(const FAuction& A, const FAuctionEventRecord& E)
	{
		TArray<const FAuctionEventRecord*> Out;
		for (const FAuctionEventRecord& X : A.Events)
		{
			if (&X == &E) break;
			if (X.Type == EAuctionEvent::LotOpened && X.Player == E.Player) Out.Reset(); // an accelerated round starts afresh
			if (X.Type == EAuctionEvent::Bid && X.Player == E.Player) Out.Add(&X);
		}
		return Out;
	}

	/** No earlier sale of the auction went for as much. */
	bool IsRecord(const FAuction& A, const FAuctionEventRecord& E)
	{
		for (const FAuctionEventRecord& X : A.Events)
		{
			if (&X == &E) return true;
			if (X.Type == EAuctionEvent::Sold && X.Amount >= E.Amount) return false;
		}
		return true;
	}
}

namespace AuctionCalls
{
	FString TeamName(int32 Team)
	{
		static const TCHAR* Names[] = { TEXT("Chennai"), TEXT("Mumbai"), TEXT("Bengaluru"), TEXT("Kolkata"), TEXT("Hyderabad"),
			TEXT("Delhi"), TEXT("Punjab"), TEXT("Rajasthan"), TEXT("Gujarat"), TEXT("Lucknow") };
		return Team >= 0 && Team < UE_ARRAY_COUNT(Names) ? Names[Team] : TEXT("the room");
	}

	FString Where(int32 Team)
	{
		if (Team < 0 || Team >= 10) return TEXT("in the room");
		const float Y = AuctionRoomLayout::TableFront(Team).Y;
		return Y < -9.f ? TEXT("on my far left") : Y < 0.f ? TEXT("on my left")
			: Y > 9.f ? TEXT("on my far right") : TEXT("on my right");
	}

	FString Short(int32 Lakh, int32 Previous)
	{
		// "2 crore 20", then "40", "60" while the crore stays the same; "3 crore" when it turns over. Below a crore
		// the lakhs are said bare once the bidding is going: "30 lakh", "35", "40".
		if (Lakh < 100 && Previous > 0 && Previous < 100) return FString::FromInt(Lakh);
		if (Lakh >= 100 && Previous >= 100 && Lakh / 100 == Previous / 100 && Lakh % 100 != 0) return FString::FromInt(Lakh % 100);
		return AuctionRules::Spoken(Lakh);
	}

	FCall Call(const FAuction& A, const FAuctionEventRecord& E)
	{
		FCall C;
		const bool bPlayer = E.Player != INDEX_NONE;
		const FString Name = bPlayer ? FAuction::Player(E.Player).Name : FString();
		const FString Team = TeamName(E.Team), Money = AuctionRules::Spoken(E.Amount);
		const FString Full = AuctionData::Franchises().IsValidIndex(E.Team) ? AuctionData::Franchises()[E.Team].Name : Team;
		switch (E.Type)
		{
		case EAuctionEvent::SetOpened:
		{
			const FAuctionSet* S = A.CurrentSet();
			if (!S) break;
			const bool bFirst = !A.Events.ContainsByPredicate([&](const FAuctionEventRecord& X) { return &X != &E && X.Type == EAuctionEvent::SetOpened && X.Time < E.Time; });
			if (bFirst && A.IsMini()) Say(C, Pick(E, EPhrase::WelcomeMini), { S->Name.ToLower() });
			else if (bFirst) Say(C, Pick(E, EPhrase::Welcome));
			else if (A.Events.ContainsByPredicate([&](const FAuctionEventRecord& X) { return X.Type == EAuctionEvent::DayStarted && X.Time == E.Time; })) break; // day two's welcome said it
			else Say(C, Pick(E, EPhrase::NextSet), { S->Name.ToLower() });
			break;
		}
		case EAuctionEvent::Accelerated: Say(C, Pick(E, EPhrase::Accelerated)); break;
		case EAuctionEvent::LotOpened:
		{
			const FAuctionPlayer& P = FAuction::Player(E.Player);
			Say(C, Pick(E, EPhrase::LotOpened), { FString::FromInt(A.LotsHeld), Name, FString(AuctionRules::RoleName(P.Role)).ToLower(), P.Country });
			break;
		}
		case EAuctionEvent::OpeningCall: Say(C, Pick(E, EPhrase::Opening), { Money }); break;
		case EAuctionEvent::Bid:
		{
			const TArray<const FAuctionEventRecord*> Before = BidsBefore(A, E);
			if (Before.IsEmpty()) { Say(C, Pick(E, EPhrase::FirstBid), { Team, Where(E.Team), Money }); break; }
			const FAuctionEventRecord& Last = *Before.Last();
			const FString Amount = Short(E.Amount, Last.Amount);
			const bool bBack = Before.ContainsByPredicate([&](const FAuctionEventRecord* X) { return X->Team == E.Team; });
			if (!bBack && Before.Num() >= 2) Say(C, Pick(E, EPhrase::NewPaddle), { Money, Team, Where(E.Team), TeamName(Last.Team) });
			else Say(C, Pick(E, bBack ? EPhrase::Back : EPhrase::Raise), { Amount, Team });
			break;
		}
		case EAuctionEvent::Huddle:
			// At a round crore figure the whole room feels the pause.
			if (E.Amount >= 1500 && E.Amount % 500 == 0) Say(C, Pick(E, EPhrase::Sticking), { Money, Team });
			else Say(C, Pick(E, EPhrase::Huddle), { Team });
			break;
		case EAuctionEvent::Timeout: Say(C, Pick(E, EPhrase::Timeout), { Team }); break;
		case EAuctionEvent::DayEnded: Say(C, Pick(E, EPhrase::DayEnded)); break;
		case EAuctionEvent::DayStarted: Say(C, Pick(E, EPhrase::DayStarted)); break;
		case EAuctionEvent::FeeCapped: Say(C, Pick(E, EPhrase::FeeCapped), { Money }); break;
		case EAuctionEvent::Out: Say(C, Pick(E, EPhrase::Out), { Team, Money }); break;
		case EAuctionEvent::GoingOnce: Say(C, Pick(E, EPhrase::GoingOnce), { Money, Team, Where(E.Team) }); break;
		case EAuctionEvent::GoingTwice: Say(C, Pick(E, EPhrase::GoingTwice)); break;
		case EAuctionEvent::Sold:
		{
			const bool bRtm = A.Teams.IsValidIndex(E.Team) && A.Teams[E.Team].Squad.ContainsByPredicate([&](const FAuctionSigning& S) { return S.Player == E.Player && S.bRtm; });
			if (bRtm) Say(C, Pick(E, EPhrase::SoldRtm), { Full, Money, Name });
			else Say(C, Pick(E, EPhrase::Sold), { Name, Full, Money, Team });
			if (E.Amount > AuctionRules::IplRecord && IsRecord(A, E)) Say(C, Pick(E, EPhrase::IplRecord));
			else if (E.Amount >= 1500 && IsRecord(A, E)) Say(C, Pick(E, EPhrase::Record));
			break;
		}
		case EAuctionEvent::Unsold: Say(C, Pick(E, EPhrase::Unsold), { Name }); break;
		case EAuctionEvent::RtmOffered: Say(C, Pick(E, EPhrase::RtmOffered), { Full, Money }); break;
		case EAuctionEvent::RtmUsed: Say(C, Pick(E, EPhrase::RtmUsed), { Team, TeamName(E.Other) }); break;
		case EAuctionEvent::RtmDeclined: Say(C, Pick(E, EPhrase::RtmDeclined), { Team }); break;
		case EAuctionEvent::FinalRaise: Say(C, Pick(E, EPhrase::FinalRaise), { Money, Team, TeamName(E.Other) }); break;
		case EAuctionEvent::RtmMatched: Say(C, Pick(E, EPhrase::RtmMatched), { Team, Money }); break;
		case EAuctionEvent::RtmNotMatched: Say(C, Pick(E, EPhrase::RtmNotMatched), { Name, TeamName(E.Other), Money }); break;
		case EAuctionEvent::Finished: Say(C, Pick(E, A.IsMini() ? EPhrase::FinishedMini : EPhrase::Finished)); break;
		default: break;
		}
		return C;
	}

	FString Line(const FAuction& A, const FAuctionEventRecord& E) { return Call(A, E).Text; }

	TArray<FString> Script()
	{
		TArray<FString> Out;
		TSet<FString> Keys;
		auto Add = [&](const FString& Piece) { const FString Key = CricketCommentary::ClipKey(Piece); if (!Key.IsEmpty() && !Keys.Contains(Key)) { Keys.Add(Key); Out.Add(Piece); } };
		// The fixed words of every phrasing.
		for (int32 P = 0; P < int32(EPhrase::Count); ++P)
			for (const TCHAR* Format : Phrasings(EPhrase(P)))
			{
				FCall C;
				TArray<FString> Blanks;
				Blanks.SetNum(4);
				Say(C, Format, Blanks);
				for (const FString& Piece : C.Pieces) Add(Piece);
			}
		// The names said into them: franchises, where they sit, sets, and every player with his role and country.
		for (int32 T = 0; T < AuctionData::Franchises().Num(); ++T) { Add(TeamName(T)); Add(Where(T)); Add(AuctionData::Franchises()[T].Name); }
		FAuction Sets(INDEX_NONE, 0);
		Sets.BeginAuction();
		for (const FAuctionSet& S : Sets.Sets) Add(S.Name.ToLower());
		FAuctionConfig MiniConfig;
		MiniConfig.Mode = EAuctionMode::Mini;
		FAuction Mini(MiniConfig, 0);
		Mini.BeginAuction();
		for (const FAuctionSet& S : Mini.Sets) Add(S.Name.ToLower());
		for (int32 R = 1; R <= 5; ++R) Add(FString::Printf(TEXT("accelerated round %d"), R));
		TSet<int32> Bases;
		for (const FAuctionPlayer& P : AuctionData::Players())
		{
			Add(P.Name);
			Add(P.Country);
			Add(FString(AuctionRules::RoleName(P.Role)).ToLower());
			Bases.Add(P.Base);
		}
		// Every price up the ladder from every base, and the bare numbers of lot numbers and short raises.
		for (int32 Base : Bases)
			for (int32 L = Base; L <= AuctionRules::Purse; L = AuctionRules::NextBid(L)) Add(AuctionRules::Spoken(L));
		for (int32 N = 1; N <= AuctionData::Players().Num(); ++N) Add(FString::FromInt(N));
		return Out;
	}

	TArray<int16> Voice(const FCall& Call)
	{
		TArray<int16> Out;
		FString Between; // the punctuation since the last clip
		for (const FString& Piece : Call.Pieces)
		{
			int32 First = 0, Last = Piece.Len() - 1;
			while (First < Piece.Len() && !FChar::IsAlnum(Piece[First])) ++First;
			while (Last >= First && !FChar::IsAlnum(Piece[Last])) --Last;
			Between += Piece.Left(First);
			if (First == Piece.Len()) continue;
			const TArray<int16> Clip = CricketAudio::LoadClip(TEXT("Auction/") + CricketCommentary::ClipKey(Piece) + TEXT(".pcm"), false);
			if (Clip.IsEmpty()) return {};
			if (!Out.IsEmpty())
			{
				const float Pause = Between.Contains(TEXT(".")) || Between.Contains(TEXT("!")) || Between.Contains(TEXT("?")) ? 0.3f
					: Between.Contains(TEXT(",")) || Between.Contains(TEXT(":")) ? 0.12f : 0.04f;
				Out.AddZeroed(int32(Pause * CricketAudio::SampleRate));
			}
			Out.Append(Clip);
			Between = Piece.RightChop(Last + 1);
		}
		return Out;
	}
}
