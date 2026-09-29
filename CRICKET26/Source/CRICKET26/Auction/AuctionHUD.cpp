#include "AuctionHUD.h"
#include "AuctionGameMode.h"
#include "AuctionCalls.h"
#include "AuctionRoom.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HudPaint.h"
#include "CRICKET26.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

namespace AuctionHudPrivate
{
	using namespace HudPaint;
	using EScreen = AAuctionGameMode::EScreen;
	using EPanel = AAuctionGameMode::EPanel;

	// The 2026 auction broadcast's look, kept spare: one dark green glass, hairlines in place of frames, gold only for
	// money and the one action that matters, franchise colours only as accents on the side they belong to.
	FLinearColor Pane(float A = 0.84f) { return Hex(0x04140F, A); }
	FLinearColor PaneHi(float A = 0.92f) { return Hex(0x0C261C, A); }
	FLinearColor Hair(float A = 0.1f) { return Hex(0xFFFFFF, A); }
	FLinearColor Money() { return Hex(0xECC870); }
	FLinearColor Amber() { return Hex(0xF29A2E); }
	/** A franchise colour sunk K of the way into the glass (mixed as the eye sees it, in sRGB), for surfaces that belong to a side. */
	FLinearColor Tint(const FLinearColor& Team, float K = 0.4f, float A = 0.94f)
	{
		const FColor T = Team.ToFColorSRGB(), B = Pane(1.f).ToFColorSRGB();
		auto Mix = [K](uint8 From, uint8 To) { return uint8(FMath::RoundToInt(FMath::Lerp(float(From), float(To), K))); };
		FLinearColor C = FLinearColor::FromSRGBColor(FColor(Mix(B.R, T.R), Mix(B.G, T.G), Mix(B.B, T.B)));
		C.A = A;
		return C;
	}

	FLinearColor InkOn(const FLinearColor& C) { return C.GetLuminance() > 0.3f ? Hex(0x0E0E0E) : Ink(); }
	const FAuctionFranchise& Fr(int32 T) { return AuctionData::Franchises()[FMath::Clamp(T, 0, 9)]; }

	FLinearColor RoleColour(EAuctionRole R)
	{
		static const uint32 C[] = { 0x3C8DFF, 0x2FC7B5, 0xA86BFF, 0xF0505A, 0x46C16E };
		return Hex(C[FMath::Min(int32(R), 4)]);
	}

	FString Initials(const FString& Name)
	{
		TArray<FString> W;
		Name.ParseIntoArrayWS(W);
		if (W.IsEmpty()) return TEXT("?");
		return (W[0].Left(1) + (W.Num() > 1 ? W.Last().Left(1) : FString())).ToUpper();
	}

	FString StatLine(const FAuctionPlayer& P)
	{
		const FAuctionStats& S = P.Ipl.Matches > 0 ? P.Ipl : P.T20;
		FString L = FString::Printf(TEXT("%s  M %d"), P.Ipl.Matches > 0 ? TEXT("IPL") : TEXT("T20 (IPL DEBUT)"), S.Matches);
		if (S.Runs > 0) L += FString::Printf(TEXT("   RUNS %d   SR %.1f"), S.Runs, S.StrikeRate);
		if (S.Wickets > 0) L += FString::Printf(TEXT("   WKTS %d   ECON %.2f"), S.Wickets, S.Economy);
		return L;
	}

	enum class EBtn : uint8 { Primary, Secondary, Quiet, Danger };

	// Material Icons codepoints the HUD uses beyond FrontendStyle::Glyph (Barlow has no ✕, ★ or −).
	constexpr TCHAR IconClose = 0xe5cd, IconStarLine = 0xe83a, IconAdd = 0xe145, IconRemove = 0xe15b, IconCheck = 0xe5ca;

	/** One paint pass: the painter, the game, the touch targets, the screen in design units (1 unit = 1 px at 1080p). */
	struct FFrame
	{
		FPaint& P;
		AAuctionGameMode& G;
		TArray<SAuctionHUD::FHit>& Hits;
		float W, H, U; // W and H in design units
		double T;
		int32 Scroll;

		FVector2D At(float X, float Y) const { return FVector2D(X, Y) * U; }
		FBox2D Rect(float X, float Y, float Wd, float Ht) const { return FBox2D(At(X, Y), At(X + Wd, Y + Ht)); }
		FSlateFontInfo F(float Px, EWeight Wt = EWeight::Bold, int32 Spacing = 0) const { return HudFont(Px * U, Wt, Spacing); }
		void Box(float X, float Y, float Wd, float Ht, const FLinearColor& Fill, float Radius = 0.f, const FLinearColor& Outline = FLinearColor::Transparent, float Width = 1.f)
		{
			P.Box(At(X, Y), At(Wd, Ht), Fill, Radius * U, Outline, Width * U);
		}
		float Text(const FString& S, float X, float Y, float Px, const FLinearColor& C, float Align = 0.f, EWeight Wt = EWeight::Bold, int32 Spacing = 0)
		{
			return P.Text(S, At(X, Y), F(Px, Wt, Spacing), C, Align) / U;
		}
		/** A small tracked caps label, the broadcast's field names. */
		float Label(const FString& S, float X, float Y, float Align = 0.f, const FLinearColor& C = InkFaint(), float Px = 14.f)
		{
			return Text(S, X, Y, Px, C, Align, EWeight::Bold, 220);
		}
		void Icon(TCHAR Glyph, float X, float Y, float Px, const FLinearColor& C)
		{
			P.Text(FString(1, &Glyph), At(X, Y), IconFont(FMath::Max(1, FMath::RoundToInt(Px * U * 0.75f))), C, 0.5f);
		}
		void HLine(float X, float Y, float Wd, float A = 0.1f) { Box(X, Y, Wd, 1.f, Hair(A)); }
		void VLine(float X, float Y, float Ht, float A = 0.1f) { Box(X, Y, 1.f, Ht, Hair(A)); }
		/** The one surface: glass with a hairline edge. */
		void Card(float X, float Y, float Wd, float Ht, float A = 0.84f) { Box(X, Y, Wd, Ht, Pane(A), 6.f, Hair(0.08f), 1.f); }
		void Hit(const FBox2D& B, TFunction<void()> Do) { Hits.Add({ B, MoveTemp(Do) }); }

		void Button(float X, float Y, float Wd, float Ht, const FString& Label_, EBtn Kind, TFunction<void()> Do, bool bEnabled = true, float Px = 20.f, TCHAR Glyph = 0)
		{
			FLinearColor Fill = Kind == EBtn::Primary ? Money() : Kind == EBtn::Danger ? Danger() : Kind == EBtn::Secondary ? PaneHi() : FLinearColor::Transparent;
			FLinearColor Outline = Kind == EBtn::Primary || Kind == EBtn::Danger ? FLinearColor::Transparent : Hair(Kind == EBtn::Quiet ? 0.2f : 0.1f);
			FLinearColor Ink_ = Kind == EBtn::Primary ? Hex(0x1A1204) : Ink();
			if (!bEnabled) { Fill = Kind == EBtn::Quiet ? FLinearColor::Transparent : Pane(0.6f); Outline = Hair(0.06f); Ink_ = InkFaint() * FLinearColor(1.f, 1.f, 1.f, 0.7f); }
			Box(X, Y, Wd, Ht, Fill, 4.f, Outline, 1.f);
			if (Glyph) Icon(Glyph, X + 0.5f * Wd, Y + 0.5f * Ht, Px * 1.3f, Ink_);
			else Text(Label_, X + 0.5f * Wd, Y + 0.5f * Ht, Px, Ink_, 0.5f, EWeight::Black, 120);
			if (bEnabled && Do) Hit(Rect(X, Y, Wd, Ht), MoveTemp(Do));
		}

		/** A franchise crest: its colours in a ringed disc with its code (drawn, not the real logo). */
		void Crest(float X, float Y, float R, int32 Team)
		{
			const FAuctionFranchise& Fc = Fr(Team);
			P.Circle(At(X, Y), R * U, Fc.Secondary, Hex(0xFFFFFF, 0.35f), 1.f * U);
			P.Circle(At(X, Y), 0.84f * R * U, Fc.Primary);
			Text(Fc.Code, X, Y, (Fc.Code.Len() > 3 ? 0.6f : 0.74f) * R, InkOn(Fc.Primary), 0.5f, EWeight::Black);
		}

		void Scrim(float A = 0.72f)
		{
			Box(0.f, 0.f, W, H, Hex(0x010806, A));
		}

		/** A screen's heading: a gold eyebrow over a large title. */
		void Heading(const FString& Eyebrow, const FString& Title, float X, float Y, float TitlePx = 52.f)
		{
			Label(Eyebrow, X, Y, 0.f, Money(), 16.f);
			Text(Title, X, Y + 48.f, TitlePx, Ink(), 0.f, EWeight::Black);
		}
	};

	// Live-screen geometry shared by the pieces that line up with each other.
	constexpr float Edge = 60.f, RailW = 250.f, TickerH = 34.f, ThirdH = 150.f, TopY = 36.f, RailY = 140.f;
	float ThirdY(const FFrame& F) { return F.H - TickerH - 24.f - ThirdH; }
	float RailX(const FFrame& F) { return F.W - Edge - RailW; }
	float StageMid(const FFrame& F) { return 0.5f * (F.W - RailW); } // the centre of the stage left of the rail

	// ---- Team pick -----------------------------------------------------------------------------------------------

	void PaintPickFormatModal(FFrame& F);

	void PaintPick(FFrame& F)
	{
		AAuctionGameMode& G = F.G;
		F.Scrim(0.7f);
		// Pass the paddle: after the first seat has set the auction up, each other seat takes a table.
		const int32 Seat = G.Picked.Num();
		const FString Title = Seat == 0 ? FString(TEXT("CHOOSE YOUR FRANCHISE")) : FString::Printf(TEXT("PLAYER %d: CHOOSE YOUR FRANCHISE"), Seat + 1);
		F.Heading(TEXT("TATA IPL AUCTION"), Title, 80.f, 72.f, 56.f);
		F.Text(Seat == 0 ? TEXT("Every side starts from its 2026 squad. Choose a franchise, then the auction: a mega auction, or the 2027 mini auction the real calendar holds next.")
			: TEXT("Pass the paddle: the next person picks a table. Tables already taken are marked."), 80.f, 170.f, 19.f, InkDim(), 0.f, EWeight::Medium);
		F.Button(F.W - 80.f - 140.f, 56.f, 140.f, 48.f, TEXT("EXIT"), EBtn::Quiet, [&G]() { G.ExitToMenu(); }, true, 18.f);
		if (Seat == 0)
		{
			// People at the tables, and a saved auction to go back to.
			F.Label(TEXT("PLAYERS"), F.W - 80.f - 140.f - 24.f - 4.f * 52.f - 90.f, 80.f, 0.f, InkFaint(), 13.f);
			for (int32 N = 1; N <= 4; ++N)
				F.Button(F.W - 80.f - 140.f - 24.f - (5 - N) * 52.f, 56.f, 46.f, 48.f, FString::FromInt(N), G.Seats == N ? EBtn::Primary : EBtn::Quiet,
					[&G, N]() { G.Seats = N; }, true, 18.f);
			if (G.bHasSave)
				F.Button(F.W - 80.f - 140.f - 24.f - 4.f * 52.f - 330.f, 56.f, 220.f, 48.f, TEXT("RESUME AUCTION"), EBtn::Secondary, [&G]() { G.ResumeSaved(); }, true, 16.f);
		}
		const TArray<FAuctionFranchise>& All = AuctionData::Franchises();
		const float Gap = 20.f, CardW = (F.W - 160.f - 4.f * Gap) / 5.f, CardH = 356.f;
		for (int32 I = 0; I < All.Num(); ++I)
		{
			const FAuctionFranchise& Fc = All[I];
			const float X = 80.f + (I % 5) * (CardW + Gap), Y = 236.f + (I / 5) * (CardH + Gap), CX = X + 0.5f * CardW;
			F.Box(X, Y, CardW, CardH, Tint(Fc.Primary, 0.22f, 0.9f), 6.f, Hair(0.08f));
			F.Box(X, Y, CardW, 4.f, Fc.Primary, 2.f);
			F.Crest(CX, Y + 104.f, 58.f, I);
			F.Text(Fc.Name.ToUpper(), CX, Y + 206.f, CardW > 300.f ? 25.f : 21.f, Ink(), 0.5f, EWeight::Black);
			F.Text(FString::Printf(TEXT("%s  ·  %s"), *Fc.City, *Fc.Home), CX, Y + 238.f, 15.f, InkFaint(), 0.5f, EWeight::Medium);
			F.HLine(X + 30.f, Y + 268.f, CardW - 60.f, 0.08f);
			int32 Squad = 0;
			for (const FAuctionPlayer& Pl : AuctionData::Players()) Squad += Pl.Team2026 == Fc.Code;
			F.Label(Fc.Titles > 0 ? FString::Printf(TEXT("%d x CHAMPIONS"), Fc.Titles) : FString(TEXT("CHASING A FIRST TITLE")), CX, Y + 296.f, 0.5f, Money(), 15.f);
			F.Label(FString::Printf(TEXT("2026 SQUAD  %d"), Squad), CX, Y + 324.f, 0.5f);
			const int32 Taken = G.Picked.IndexOfByKey(I);
			if (Taken != INDEX_NONE)
			{
				F.Box(X, Y, CardW, CardH, Hex(0x000000, 0.55f), 6.f, Money(), 2.f);
				F.Text(FString::Printf(TEXT("PLAYER %d"), Taken + 1), CX, Y + 0.5f * CardH, 30.f, Money(), 0.5f, EWeight::Black);
			}
			else F.Hit(F.Rect(X, Y, CardW, CardH), [&G, I]() { G.PickTeam(I); });
		}
		PaintPickFormatModal(F);
	}

	void PaintPickFormatModal(FFrame& F)
	{
		AAuctionGameMode& G = F.G;
		if (G.PendingPickTeam == INDEX_NONE) return;
		F.Scrim(0.85f);
		const int32 T = G.PendingPickTeam;
		const FAuctionFranchise& Fc = Fr(T);
		const float Wd = 1220.f, Ht = 600.f, X = 0.5f * (F.W - Wd), Y = 0.5f * (F.H - Ht);
		F.Card(X, Y, Wd, Ht, 0.98f);
		F.Box(X, Y, Wd, 4.f, Fc.Primary, 2.f);
		F.Button(X + Wd - 54.f, Y + 14.f, 40.f, 40.f, FString(), EBtn::Quiet, [&G]() { G.PendingPickTeam = INDEX_NONE; }, true, 18.f, IconClose);

		F.Crest(X + 60.f, Y + 60.f, 30.f, T);
		F.Text(Fc.Name.ToUpper(), X + 104.f, Y + 48.f, 30.f, Ink(), 0.f, EWeight::Black);
		F.Label(G.Seats > 1 ? FString::Printf(TEXT("CHOOSE THE AUCTION  ·  %d PLAYERS"), G.Seats) : FString(TEXT("CHOOSE THE AUCTION")), X + 104.f, Y + 80.f, 0.f, Money(), 14.f);
		// Difficulty: how sharp the other front offices are.
		static const TCHAR* Levels[] = { TEXT("CASUAL"), TEXT("PRO"), TEXT("LEGEND") };
		for (int32 L = 0; L < 3; ++L)
			F.Button(X + Wd - 90.f - (3 - L) * 124.f, Y + 36.f, 116.f, 44.f, Levels[L], int32(G.Setup.Difficulty) == L ? EBtn::Primary : EBtn::Quiet,
				[&G, L]() { G.Setup.Difficulty = EAuctionDifficulty(L); }, true, 15.f);
		F.HLine(X + 32.f, Y + 108.f, Wd - 64.f, 0.08f);

		struct FOption { const TCHAR* Eyebrow; const TCHAR* Title; const TCHAR* Lines[5]; const TCHAR* Go; EAuctionMode Mode; bool bFresh; bool bHero; };
		const FOption Options[] = {
			{ TEXT("MEGA AUCTION"), TEXT("WITH RETENTIONS"), { TEXT("• Trade window, then keep up to 6"), TEXT("• Slabs 18 / 14 / 11 / 18 / 14 Cr, uncapped 4 Cr"),
				TEXT("• Stars can ask for more, or refuse"), TEXT("• Right to Match cards for the rest"), TEXT("• Two days: marquee, then uncapped") }, TEXT("RETAIN AND BID"), EAuctionMode::Mega, false, false },
			{ TEXT("MEGA AUCTION"), TEXT("NO RETENTIONS"), { TEXT("• No side keeps anyone"), TEXT("• All ten start with ₹120.00 Cr"),
				TEXT("• Six Right to Match cards each"), TEXT("• Every superstar in the pool"), TEXT("• Build from nothing") }, TEXT("START FRESH"), EAuctionMode::Mega, true, false },
			{ TEXT("THE REAL CALENDAR"), TEXT("IPL 2027 AUCTION"), { TEXT("• Keep your squad on its contracts"), TEXT("• Release the rest into the pool"),
				TEXT("• Top up to 25 under the 120 Cr cap"), TEXT("• No RTM, no marquee"), TEXT("• Overseas pay capped at 18 Cr") }, TEXT("KEEP AND TOP UP"), EAuctionMode::Mini, false, true },
		};
		const float OptW = (Wd - 64.f - 2.f * 24.f) / 3.f, OptH = 440.f, OptY = Y + 128.f;
		for (int32 I = 0; I < 3; ++I)
		{
			const FOption& O = Options[I];
			const float OX = X + 32.f + I * (OptW + 24.f);
			F.Box(OX, OptY, OptW, OptH, O.bHero ? Tint(Fc.Primary, 0.28f, 0.96f) : PaneHi(0.92f), 6.f, O.bHero ? Money() : Hair(0.14f), O.bHero ? 1.5f : 1.f);
			F.Box(OX, OptY, OptW, 3.f, O.bHero ? Money() : Teal(), 1.5f);
			F.Label(O.Eyebrow, OX + 24.f, OptY + 34.f, 0.f, O.bHero ? Money() : Teal(), 12.f);
			F.Text(O.Title, OX + 24.f, OptY + 70.f, 26.f, O.bHero ? Money() : Ink(), 0.f, EWeight::Black);
			for (int32 L = 0; L < 5; ++L) F.Text(O.Lines[L], OX + 24.f, OptY + 116.f + L * 30.f, 15.f, InkDim(), 0.f, EWeight::Medium);
			const EAuctionMode Mode = O.Mode;
			const bool bFresh = O.bFresh;
			F.Button(OX + 20.f, OptY + OptH - 68.f, OptW - 40.f, 52.f, O.Go, O.bHero ? EBtn::Primary : EBtn::Secondary,
				[&G, T, Mode, bFresh]() { G.PickTeamAndSetup(T, Mode, bFresh); }, true, 16.f);
		}
	}

	// ---- Retentions ----------------------------------------------------------------------------------------------

	void RowMark(FFrame& F, float X, float Y, float Wd, float Ht);

	/** The trade window over the summary card: a partner, one of theirs for one of ours, and the answer. */
	void PaintTradePanel(FFrame& F, float SX, float SY, float SideW, float SH)
	{
		AAuctionGameMode& G = F.G;
		const FAuction& A = *G.Auction;
		F.Card(SX, SY, SideW, SH, 0.99f);
		F.Box(SX, SY, SideW, 3.f, Teal(), 1.5f);
		F.Label(TEXT("TRADE WINDOW"), SX + 28.f, SY + 34.f, 0.f, Teal(), 15.f);
		F.Button(SX + SideW - 52.f, SY + 12.f, 40.f, 40.f, FString(), EBtn::Quiet, [&G]() { G.Panel = AAuctionGameMode::EPanel::None; }, true, 18.f, IconClose);
		// Partners: every other franchise.
		int32 Col = 0;
		for (int32 T = 0; T < A.Teams.Num(); ++T)
		{
			if (T == G.Team) continue;
			const float CX = SX + 40.f + (Col % 9) * 44.f, CY = SY + 84.f;
			++Col;
			F.P.Alpha = T == G.TradeWith ? 1.f : 0.55f;
			F.Crest(CX, CY, 18.f, T);
			F.P.Alpha = 1.f;
			if (T == G.TradeWith) F.P.Circle(F.At(CX, CY), 22.f * F.U, FLinearColor::Transparent, Money(), 1.5f * F.U);
			F.Hit(F.Rect(CX - 22.f, CY - 22.f, 44.f, 44.f), [&G, T]() { G.TradeWith = T; G.TradeGet = INDEX_NONE; });
		}
		F.Text(G.TradeGive == INDEX_NONE ? FString(TEXT("Tap one of your players to offer him.")) : FString::Printf(TEXT("You give: %s"), *FAuction::Player(G.TradeGive).Name),
			SX + 28.f, SY + 128.f, 16.f, G.TradeGive == INDEX_NONE ? InkFaint() : Ink(), 0.f, EWeight::Bold);
		if (G.TradeWith != INDEX_NONE)
		{
			const TArray<int32> Theirs = A.RetentionCandidates(G.TradeWith);
			const float RowH = 30.f, Top = SY + 150.f;
			const int32 Fit = FMath::Min(Theirs.Num(), FMath::FloorToInt((SH - 330.f) / RowH));
			for (int32 I = 0; I < Fit; ++I)
			{
				const FAuctionPlayer& P = FAuction::Player(Theirs[I]);
				const float RY = Top + I * RowH, MY = RY + 0.5f * RowH;
				if (Theirs[I] == G.TradeGet) RowMark(F, SX + 12.f, RY, SideW - 24.f, RowH);
				F.P.Circle(F.At(SX + 30.f, MY), 4.f * F.U, RoleColour(P.Role));
				F.Text(P.Name, SX + 44.f, MY, 16.f, Ink(), 0.f, EWeight::Bold);
				F.Text(FString::Printf(TEXT("%d  %s"), P.Overall(), *AuctionRules::Money(A.ContractOf(P.Id))), SX + SideW - 28.f, MY, 14.f, InkDim(), 1.f, EWeight::Bold);
				F.Hit(F.Rect(SX + 12.f, RY, SideW - 24.f, RowH), [&G, Id = P.Id]() { G.TradeGet = Id; });
			}
		}
		const float BY = SY + SH - 150.f;
		if (!G.TradeResult.IsEmpty()) F.Text(G.TradeResult, SX + 28.f, BY - 24.f, 15.f, G.TradeResult.StartsWith(TEXT("Done")) ? Teal() : Amber(), 0.f, EWeight::Bold);
		const bool bReady = G.TradeGive != INDEX_NONE && G.TradeGet != INDEX_NONE && G.TradeWith != INDEX_NONE;
		F.Button(SX + 28.f, BY, SideW - 56.f, 60.f, bReady ? FString::Printf(TEXT("OFFER %s FOR %s"), *FAuction::Player(G.TradeGive).Short.ToUpper(), *FAuction::Player(G.TradeGet).Short.ToUpper())
			: FString(TEXT("PROPOSE TRADE")), EBtn::Primary, [&G]() { G.ProposeTrade(); }, bReady, 16.f);
		int32 Y = 0;
		for (int32 I = A.Trades.Num() - 1; I >= 0 && Y < 2; --I, ++Y)
		{
			const FAuctionTrade& X = A.Trades[I];
			F.Text(FString::Printf(TEXT("%s / %s:  %s for %s"), *Fr(X.From).Code, *Fr(X.To).Code, *FAuction::Player(X.Gave).Short, *FAuction::Player(X.Got).Short),
				SX + 28.f, BY + 84.f + Y * 24.f, 14.f, InkFaint(), 0.f, EWeight::Medium);
		}
	}

	void PaintRetain(FFrame& F)
	{
		AAuctionGameMode& G = F.G;
		const FAuction& A = *G.Auction;
		const bool bMini = A.IsMini();
		const FAuctionFranchise& Fc = Fr(G.Team);
		F.Scrim(0.8f);
		F.Crest(116.f, 110.f, 40.f, G.Team);
		FString Eyebrow = bMini ? FString::Printf(TEXT("IPL %d AUCTION  ·  KEEP OR RELEASE"), A.Config.Season) : FString(TEXT("RETENTIONS"));
		if (G.Picked.Num() > 1) Eyebrow += FString::Printf(TEXT("  ·  PLAYER %d OF %d"), G.RetainSeat + 1, G.Picked.Num());
		F.Heading(Eyebrow, Fc.Name.ToUpper(), 180.f, 86.f, 44.f);
		F.Text(bMini ? TEXT("Keep the players you want on their contracts; the rest go into the auction. The contracts come off the 120 Cr cap, and the rest is your purse.")
			: TEXT("Keep up to 6 (5 capped, 2 uncapped). Slabs 18 / 14 / 11 / 18 / 14 Cr, uncapped 4 Cr, or what a star asks if more. Places left open become Right to Match cards."),
			80.f, 190.f, 17.f, InkDim(), 0.f, EWeight::Medium);

		const bool bTrading = G.Panel == AAuctionGameMode::EPanel::Trade;
		const TArray<int32> Squad = A.RetentionCandidates(G.Team);
		const float ListX = 80.f, ListY = 230.f, SideW = 440.f, ColW = (F.W - 160.f - SideW - 40.f - 24.f) / 2.f, RowH = 52.f;
		const int32 PerCol = 14;
		// What each kept player costs, as the engine hands out the slabs.
		TMap<int32, int32> Cost;
		const TArray<int32> Costs = A.RetentionCosts(G.Keep);
		for (int32 I = 0; I < G.Keep.Num(); ++I) Cost.Add(G.Keep[I], Costs[I]);
		for (int32 C = 0; C < 2; ++C) F.Card(ListX + C * (ColW + 24.f), ListY, ColW, PerCol * RowH, 0.7f);
		for (int32 I = 0; I < FMath::Min(Squad.Num(), 2 * PerCol); ++I)
		{
			const FAuctionPlayer& P = FAuction::Player(Squad[I]);
			const float X = ListX + (I / PerCol) * (ColW + 24.f), Y = ListY + (I % PerCol) * RowH, MY = Y + 0.5f * RowH;
			const int32 Slot = G.Keep.IndexOfByKey(P.Id);
			const bool bKept = Slot != INDEX_NONE, bRefuses = !bMini && A.WantsAuction(P.Id);
			if (bKept) { F.Box(X + 1.f, Y + 1.f, ColW - 2.f, RowH - 2.f, Tint(Fc.Primary, 0.45f, 0.9f), 4.f); F.Box(X + 1.f, Y + 8.f, 3.f, RowH - 16.f, Money(), 1.5f); }
			if (bTrading && G.TradeGive == P.Id) RowMark(F, X + 1.f, Y + 1.f, ColW - 2.f, RowH - 2.f);
			if (I % PerCol) F.HLine(X + 16.f, Y, ColW - 32.f, 0.06f);
			F.P.Alpha = bRefuses ? 0.55f : 1.f;
			F.P.Circle(F.At(X + 22.f, MY), 4.f * F.U, RoleColour(P.Role));
			F.Text(FString::FromInt(P.Overall()), X + 54.f, MY, 22.f, bKept ? Money() : InkDim(), 0.5f, EWeight::Black);
			const float NameW = F.Text(P.Name, X + 82.f, MY, 20.f, Ink(), 0.f, EWeight::Bold);
			FString Tags = AuctionRules::RoleCode(P.Role);
			if (P.IsOverseas()) Tags += TEXT("  ·  OS");
			if (!P.bCapped) Tags += TEXT("  ·  UNCAPPED");
			F.Label(Tags, X + 94.f + NameW, MY, 0.f, InkFaint(), 13.f);
			F.P.Alpha = 1.f;
			FString Right;
			FLinearColor RightC = InkFaint();
			if (bKept) { Right = bMini ? AuctionRules::Money(Cost[P.Id]) : FString::Printf(TEXT("#%d   %s"), Slot + 1, *AuctionRules::Money(Cost[P.Id])); RightC = Money(); }
			else if (bRefuses) { Right = TEXT("WANTS THE AUCTION"); RightC = Danger(); }
			else if (!bMini && A.RetentionAsk(P.Id) > AuctionRules::CappedRetentionCost(0)) Right = FString::Printf(TEXT("ASKS %s"), *AuctionRules::Money(A.RetentionAsk(P.Id)));
			else Right = AuctionRules::Money(A.ContractOf(P.Id));
			F.Text(Right, X + ColW - 18.f, MY, bKept ? 19.f : 15.f, RightC, 1.f, bKept ? EWeight::Black : EWeight::Bold);
			if (bTrading) F.Hit(F.Rect(X, Y, ColW, RowH), [&G, Id = P.Id]() { G.TradeGive = Id; });
			else if (!bRefuses) F.Hit(F.Rect(X, Y, ColW, RowH), [&G, Id = P.Id]() { G.ToggleKeep(Id); });
		}

		// The summary card, or the trade window over it.
		const float SX = F.W - 80.f - SideW, SY = ListY, SH = PerCol * RowH;
		if (bTrading) { PaintTradePanel(F, SX, SY, SideW, SH); return; }
		F.Card(SX, SY, SideW, SH, 0.88f);
		const int32 Spend = A.RetentionCost(G.Keep);
		int32 KeptCapped = 0, Overseas = 0;
		for (int32 P : G.Keep) { KeptCapped += FAuction::Player(P).bCapped; Overseas += FAuction::Player(P).IsOverseas(); }
		struct FRowText { FString Label, Value; FLinearColor Colour; };
		TArray<FRowText> Rows;
		if (bMini)
		{
			Rows.Add({ TEXT("KEPT"), FString::Printf(TEXT("%d / 25"), G.Keep.Num()), Ink() });
			Rows.Add({ TEXT("OVERSEAS"), FString::Printf(TEXT("%d / 8"), Overseas), Ink() });
			Rows.Add({ TEXT("RELEASED"), FString::FromInt(Squad.Num() - G.Keep.Num()), Ink() });
			Rows.Add({ TEXT("CONTRACTS KEPT"), AuctionRules::Money(Spend), Money() });
			Rows.Add({ TEXT("AUCTION PURSE"), AuctionRules::Money(A.Config.Purse - Spend), Money() });
			Rows.Add({ TEXT("PLACES TO FILL"), FString::FromInt(AuctionRules::SquadMax - G.Keep.Num()), Teal() });
		}
		else
		{
			Rows.Add({ TEXT("RETAINED"), FString::Printf(TEXT("%d / 6"), G.Keep.Num()), Ink() });
			Rows.Add({ TEXT("CAPPED"), FString::Printf(TEXT("%d / 5"), KeptCapped), Ink() });
			Rows.Add({ TEXT("UNCAPPED"), FString::Printf(TEXT("%d / 2"), G.Keep.Num() - KeptCapped), Ink() });
			Rows.Add({ TEXT("RETENTION COST"), AuctionRules::Money(Spend), Money() });
			Rows.Add({ TEXT("AUCTION PURSE"), AuctionRules::Money(A.Config.Purse - Spend), Money() });
			Rows.Add({ TEXT("RIGHT TO MATCH CARDS"), FString::FromInt(AuctionRules::KeepMax - G.Keep.Num()), Teal() });
		}
		for (int32 I = 0; I < Rows.Num(); ++I)
		{
			const float Y = SY + 48.f + I * 60.f;
			F.Label(Rows[I].Label, SX + 32.f, Y);
			F.Text(Rows[I].Value, SX + SideW - 32.f, Y, 28.f, Rows[I].Colour, 1.f, EWeight::Black);
			if (I + 1 < Rows.Num()) F.HLine(SX + 32.f, Y + 30.f, SideW - 64.f, 0.07f);
		}
		const float BY = SY + SH - 3 * 64.f - 16.f, HalfW = 0.5f * (SideW - 76.f);
		F.Button(SX + 32.f, BY - 96.f, SideW - 64.f, 40.f, FString::Printf(TEXT("TRADE WINDOW  (%d DEALS SO FAR)"), A.Trades.Num()), EBtn::Secondary,
			[&G]() { G.Panel = AAuctionGameMode::EPanel::Trade; G.TradeResult.Reset(); }, true, 14.f);
		if (!bMini)
			F.Button(SX + 32.f, BY - 48.f, SideW - 64.f, 38.f, TEXT("NO RETENTIONS FOR ANYONE (120 CR ALL)"), EBtn::Quiet, [&G]() { G.StartWithNoRetentions(); }, G.Picked.Num() == 1, 13.f);
		F.Button(SX + 32.f, BY, HalfW, 52.f, TEXT("AI PICKS"), EBtn::Secondary, [&G]() { G.SuggestKeep(); }, true, 18.f);
		F.Button(SX + 44.f + HalfW, BY, HalfW, 52.f, TEXT("CLEAR"), EBtn::Quiet, [&G]() { G.Keep.Reset(); }, true, 18.f);
		const bool bLast = G.RetainSeat + 1 >= G.Picked.Num();
		F.Button(SX + 32.f, BY + 66.f, SideW - 64.f, 68.f, bLast ? TEXT("CONFIRM  ·  ENTER THE AUCTION") : TEXT("CONFIRM  ·  NEXT PLAYER"), EBtn::Primary,
			[&G]() { G.ConfirmRetentions(); }, true, 22.f);
		F.Text(bMini ? TEXT("The other franchises announce their releases when you enter.") : TEXT("The other franchises announce their retentions when you enter."),
			SX + 0.5f * SideW, BY + 160.f, 14.f, InkFaint(), 0.5f, EWeight::Medium);
	}

	// ---- Live ------------------------------------------------------------------------------------------------------

	FString BidBlocked(const FAuction& A, int32 Table)
	{
		if (A.Phase == EAuctionPhase::LotIntro) return TEXT("GET READY");
		if (A.Phase != EAuctionPhase::Bidding) return FString();
		if (A.Holder == Table) return TEXT("YOUR BID");
		const FAuctionTeam& T = A.Teams[Table];
		if (T.Squad.Num() >= AuctionRules::SquadMax) return TEXT("SQUAD FULL");
		if (FAuction::Player(A.Lot).IsOverseas() && T.Overseas() >= AuctionRules::OverseasMax) return TEXT("8 OVERSEAS");
		if (A.AskPrice() > A.MaxBid(Table)) return TEXT("PURSE LIMIT");
		return FString();
	}

	/** Ten franchises' purses scrolling along the foot of the screen, as the broadcast runs them. */
	void PaintTicker(FFrame& F, const FAuction& A)
	{
		const float Y = F.H - TickerH, MY = Y + 0.5f * TickerH, LabelW = 180.f;
		const FLinearColor Bar = Hex(0x020A07);
		F.Box(0.f, Y, F.W, TickerH, Bar);
		F.HLine(0.f, Y, F.W, 0.08f);
		// Laid out entry by entry at a fixed gap: measuring one long line with trailing spaces left the loop's seam overlapping.
		TArray<FString> Entries;
		float Wd = 0.f;
		for (int32 T = 0; T < A.Teams.Num(); ++T)
		{
			Entries.Add(FString::Printf(TEXT("%s   %s   ·   %d PLAYERS"), *Fr(T).Code, *AuctionRules::Money(A.Teams[T].Purse), A.Teams[T].Squad.Num()));
			Wd += HudPaint::FPaint::Measure(Entries.Last(), F.F(16.f, EWeight::Bold, 40)).X / F.U + 64.f;
		}
		const float Off = FMath::Fmod(float(F.T) * 60.f, FMath::Max(Wd, 1.f));
		for (float X = LabelW + 20.f - Off; X < F.W;)
			for (const FString& E : Entries) X += F.Text(E, X, MY, 16.f, InkDim(), 0.f, EWeight::Bold, 40) + 64.f;
		F.Box(0.f, Y + 1.f, LabelW, TickerH - 1.f, Bar); // over the ticker's start
		F.VLine(LabelW, Y + 8.f, TickerH - 16.f, 0.2f);
		F.Label(TEXT("PURSE REMAINING"), 0.5f * LabelW, MY, 0.5f, Money(), 13.f);
	}

	/** The broadcast lower third: base price | the player | the current bid, on one strip of glass. */
	void PaintLowerThird(FFrame& F, const FAuction& A)
	{
		const FAuctionPlayer& P = FAuction::Player(A.Lot);
		const float X0 = Edge, Y = ThirdY(F), Ht = ThirdH, Wd = RailX(F) - 20.f - X0, MY = Y + 0.5f * Ht;
		const float Intro = FMath::Clamp(A.PhaseTime() / 0.5f, 0.f, 1.f);
		F.P.Alpha = A.Phase == EAuctionPhase::LotIntro ? Intro : 1.f;
		// The set, lot and RTM status sit above the strip as plain type.
		const FAuctionSet* S = A.CurrentSet();
		if (S)
			F.Label(FString::Printf(TEXT("%s  ·  LOT %d OF %d%s"), *S->Name.ToUpper(), A.LotInSet + 1, S->Players.Num(), S->bAccelerated ? TEXT("  ·  ACCELERATED") : TEXT("")),
				X0 + 2.f, Y - 20.f, 0.f, Money(), 14.f);
		const int32 Former = A.IsMini() ? INDEX_NONE : A.OwnerOf(P.Id);
		if (Former != INDEX_NONE && A.Teams[Former].RtmCards > 0 && A.SoldTo.FindRef(A.Lot, INDEX_NONE) == INDEX_NONE)
		{
			// The former side's crest leads it, as on the broadcast.
			const FString Badge = FString::Printf(TEXT("RTM AVAILABLE  ·  %s  (%d)"), *Fr(Former).Code, A.Teams[Former].RtmCards);
			const float TW = HudPaint::FPaint::Measure(Badge, F.F(13.f, EWeight::Bold, 220)).X / F.U;
			F.Label(Badge, X0 + Wd, Y - 20.f, 1.f, Ink(), 13.f);
			F.Crest(X0 + Wd - TW - 18.f, Y - 20.f, 11.f, Former);
		}
		F.Card(X0, Y, Wd, Ht, 0.9f);
		// Base price.
		const float BaseW = 220.f;
		F.Label(TEXT("BASE PRICE"), X0 + 0.5f * BaseW, Y + 46.f, 0.5f);
		F.Text(AuctionRules::Money(P.Base), X0 + 0.5f * BaseW, Y + 94.f, 38.f, Ink(), 0.5f, EWeight::Black);
		F.VLine(X0 + BaseW, Y + 24.f, Ht - 48.f);
		// The player.
		const float PX = X0 + BaseW + 32.f, R = 48.f;
		F.P.Circle(F.At(PX + R, MY), R * F.U, Tint(RoleColour(P.Role), 0.35f, 1.f), RoleColour(P.Role), 2.f * F.U);
		F.Text(Initials(P.Name), PX + R, MY, 36.f, Ink(), 0.5f, EWeight::Black);
		const float TX = PX + 2.f * R + 26.f;
		F.Text(P.Name.ToUpper(), TX, Y + 40.f, 42.f, Ink(), 0.f, EWeight::Black);
		FString Bio = FString::Printf(TEXT("%s  ·  %s  ·  AGE %d  ·  %s"), AuctionRules::RoleName(P.Role), *P.Country.ToUpper(), P.Age,
			P.bCapped ? TEXT("CAPPED") : TEXT("UNCAPPED"));
		if (!P.BowlStyle.IsEmpty() && P.Role != EAuctionRole::Batter && P.Role != EAuctionRole::Keeper) Bio += TEXT("  ·  ") + P.BowlStyle.ToUpper();
		F.Label(Bio, TX, Y + 80.f, 0.f, Money(), 15.f);
		FString Stats = StatLine(P);
		if (P.Last.Matches > 0)
		{
			// The form strip: last IPL season beside the career.
			Stats += FString::Printf(TEXT("      LAST SEASON  M %d"), P.Last.Matches);
			if (P.Last.Runs > 0) Stats += FString::Printf(TEXT("  R %d  SR %.0f"), P.Last.Runs, P.Last.StrikeRate);
			if (P.Last.Wickets > 0) Stats += FString::Printf(TEXT("  W %d  E %.2f"), P.Last.Wickets, P.Last.Economy);
		}
		F.Text(Stats, TX, Y + 106.f, 16.f, InkDim(), 0.f, EWeight::Bold, 40);
		const int32 Owner = A.OwnerOf(P.Id);
		FString Role;
		for (int32 B = 0; B < AuctionTags::Count; ++B)
			if ((P.Tags & (1u << B)) != 0 && B != 10) Role += FString(TEXT("  ")) + FString(AuctionTags::Name(B)).ToUpper();
		F.Text(FString::Printf(TEXT("LAST SIDE  %s%s      OVR %d   BAT %d   BOWL %d%s"), Owner == INDEX_NONE ? TEXT("UNATTACHED") : *Fr(Owner).Code,
			Owner != INDEX_NONE ? *(TEXT("  ") + AuctionRules::Money(A.ContractOf(P.Id))) : TEXT(""), P.Overall(), P.BatRating, P.BowlRating, *Role),
			TX, Y + 128.f, 14.f, InkFaint(), 0.f, EWeight::Bold, 40);
		// The current bid, sunk in the holder's colours.
		const float BidW = 320.f, BX = X0 + Wd - BidW, CX = BX + 0.5f * BidW;
		const bool bHeld = A.Holder != INDEX_NONE && A.Price > 0;
		const bool bFinal = A.Phase == EAuctionPhase::RtmMatch || A.Phase == EAuctionPhase::RtmRaise;
		if (bHeld)
		{
			const FLinearColor Fill = Tint(Fr(A.Holder).Primary, 0.55f, 1.f);
			F.Box(BX, Y, BidW, Ht, Fill, 6.f);
			F.Box(BX, Y, BidW, 4.f, Fr(A.Holder).Primary, 2.f);
			const FString Head = FString::Printf(TEXT("%s  ·  %s"), *Fr(A.Holder).Code, bFinal ? TEXT("FINAL BID") : TEXT("CURRENT BID"));
			F.Label(Head, CX, Y + 40.f, 0.5f, InkOn(Fill), 14.f);
			F.Text(AuctionRules::Money(A.Price), CX, Y + 88.f, 52.f, bFinal ? Money() : InkOn(Fill), 0.5f, EWeight::Black);
			F.Label(Fr(A.Holder).Name.ToUpper(), CX, Y + 128.f, 0.5f, InkOn(Fill) * FLinearColor(1.f, 1.f, 1.f, 0.7f), 12.f);
		}
		else
		{
			F.VLine(BX, Y + 24.f, Ht - 48.f);
			F.Label(TEXT("CURRENT BID"), CX, Y + 46.f, 0.5f);
			F.Text(TEXT("—"), CX, Y + 94.f, 44.f, InkFaint(), 0.5f, EWeight::Black);
		}
		F.P.Alpha = 1.f;
	}

	void PaintYourTeam(FFrame& F, const FAuction& A)
	{
		const int32 T = F.G.Team;
		const FAuctionTeam& Team = A.Teams[T];
		const float Wd = 440.f, Ht = 84.f, X = F.W - Edge - Wd, Y = TopY, MY = Y + 0.5f * Ht;
		F.Card(X, Y, Wd, Ht);
		F.Box(X, Y + 12.f, 3.f, Ht - 24.f, Fr(T).Primary, 1.5f);
		F.Crest(X + 50.f, MY, 28.f, T);
		F.Label(F.G.Picked.Num() > 1 ? FString::Printf(TEXT("%s PURSE  ·  TAP TO SWITCH"), *Fr(T).Code) : FString(TEXT("YOUR PURSE")), X + 94.f, Y + 26.f);
		if (F.G.Picked.Num() > 1) F.Hit(F.Rect(X, Y, Wd, Ht), [&G = F.G]() { G.FocusNext(); });
		F.Text(AuctionRules::Money(Team.Purse), X + 94.f, Y + 56.f, 32.f, Money(), 0.f, EWeight::Black);
		struct FStat { const TCHAR* Label; FString Value; FLinearColor Colour; };
		const FStat Stats[] = {
			{ TEXT("SQUAD"), FString::Printf(TEXT("%d/25"), Team.Squad.Num()), Team.Squad.Num() < AuctionRules::SquadMin ? Amber() : Ink() },
			{ TEXT("OS"), FString::Printf(TEXT("%d/8"), Team.Overseas()), Ink() },
			{ A.IsMini() ? TEXT("T/O") : TEXT("RTM"), FString::FromInt(A.IsMini() ? Team.Timeouts : Team.RtmCards), Teal() },
		};
		for (int32 I = 0; I < 3; ++I)
		{
			const float CX = X + Wd - 40.f - (2 - I) * 64.f;
			F.Label(Stats[I].Label, CX, Y + 26.f, 0.5f, InkFaint(), 12.f);
			F.Text(Stats[I].Value, CX, Y + 56.f, 22.f, Stats[I].Colour, 0.5f, EWeight::Black);
		}
		F.VLine(X + Wd - 40.f - 2 * 64.f - 42.f, Y + 18.f, Ht - 36.f);
	}

	void PaintTopLeft(FFrame& F, const FAuction& A)
	{
		// A card the height of the purse card opposite, so the bug reads over the bright stage.
		const FAuctionSet* S = A.CurrentSet();
		const FString Title = TEXT("IPL MEGA AUCTION"), Sub = FString::Printf(TEXT("%s   ·   LOTS %d   ·   SOLD %d"), S ? *S->Name.ToUpper() : TEXT("THE END"), A.LotsHeld, A.SoldTo.Num());
		const float X = Edge, Pad = 24.f, TitleX = X + Pad + 88.f;
		const float Wd = FMath::Max(TitleX - X + HudPaint::FPaint::Measure(Title, F.F(24.f, EWeight::Black, 60)).X / F.U,
			Pad + HudPaint::FPaint::Measure(Sub, F.F(14.f, EWeight::Bold, 220)).X / F.U) + Pad;
		F.Card(X, TopY, Wd, 84.f);
		const float Blink = 0.55f + 0.45f * FMath::Sin(float(F.T) * 4.f), MY = TopY + 28.f;
		F.P.Circle(F.At(X + Pad + 6.f, MY), 6.f * F.U, Danger() * FLinearColor(1.f, 1.f, 1.f, Blink));
		F.Label(TEXT("LIVE"), X + Pad + 20.f, MY, 0.f, Danger(), 14.f);
		F.VLine(TitleX - 14.f, MY - 11.f, 22.f, 0.25f);
		F.Text(Title, TitleX, MY, 24.f, Ink(), 0.f, EWeight::Black, 60);
		F.Label(Sub, X + Pad, TopY + 60.f);
		// The paddles in this lot, in the order they came in; the holder lit.
		if (A.Lot == INDEX_NONE) return;
		TArray<int32> In;
		for (int32 I = A.Events.Num() - 1; I >= 0 && A.Events[I].Player == A.Lot; --I)
			if (A.Events[I].Type == EAuctionEvent::Bid) In.AddUnique(A.Events[I].Team);
		for (int32 I = 0; I < In.Num(); ++I)
		{
			const float CX = Edge + 22.f + I * 52.f, CY = TopY + 130.f;
			const bool bHolds = In[I] == A.Holder;
			F.P.Alpha = bHolds ? 1.f : 0.6f;
			F.Crest(CX, CY, bHolds ? 22.f : 18.f, In[I]);
			F.P.Alpha = 1.f;
			if (bHolds) F.P.Circle(F.At(CX, CY), 26.f * F.U, FLinearColor::Transparent, Money(), 1.5f * F.U);
		}
	}

	void PaintCaption(FFrame& F)
	{
		const float Age = float(F.G.Now() - F.G.CaptionAt);
		// Her line stays up while she is still saying it, even past the usual hold.
		if (F.G.Caption.IsEmpty() || (Age > 7.f && F.G.Now() >= F.G.VoiceUntil)) return;
		F.P.Alpha = FMath::Clamp(Age / 0.2f, 0.f, 1.f) * FMath::Clamp((7.f - Age) / 0.6f, 0.f, 1.f);
		const float Ht = 44.f, Y = ThirdY(F) - 42.f - Ht;
		const float Wd = HudPaint::FPaint::Measure(F.G.Caption, F.F(21.f, EWeight::Medium)).X / F.U + 170.f;
		const float X = StageMid(F) - 0.5f * Wd;
		F.Box(X, Y, Wd, Ht, Hex(0x000000, 0.55f), 0.5f * Ht);
		F.Label(TEXT("AUCTIONEER"), X + 26.f, Y + 0.5f * Ht, 0.f, Money(), 12.f);
		F.Text(F.G.Caption, X + 144.f, Y + 0.5f * Ht, 21.f, Ink(), 0.f, EWeight::Medium);
		F.P.Alpha = 1.f;
	}

	void PaintHammer(FFrame& F, const FAuction& A)
	{
		const float CX = StageMid(F);
		if (A.Phase == EAuctionPhase::Bidding && A.HammerStage > 0)
		{
			const FString S = A.HammerStage == 1 ? TEXT("GOING ONCE") : TEXT("GOING TWICE");
			const float Pulse = 0.75f + 0.25f * FMath::Sin(float(F.T) * 8.f);
			F.P.Pill(S, F.At(CX, 250.f), F.F(30.f, EWeight::Black, 300), Hex(0x140A02, 0.82f), Amber() * FLinearColor(1.f, 1.f, 1.f, Pulse),
				44.f * F.U, 58.f * F.U, 29.f * F.U, Amber());
		}
		if (A.Phase != EAuctionPhase::Hammer || A.Lot == INDEX_NONE) return;
		const float Age = float(F.G.Now() - F.G.SoldAt);
		const float In = FMath::Clamp(Age / 0.25f, 0.f, 1.f);
		const float Scale = FMath::Lerp(1.15f, 1.f, FMath::Sqrt(In));
		F.P.Alpha = In;
		const float CY = 300.f;
		const FAuctionPlayer& P = FAuction::Player(A.Lot);
		if (A.LastSoldTo == INDEX_NONE)
		{
			const float Wd = 600.f * Scale, Ht = 170.f * Scale;
			F.Card(CX - 0.5f * Wd, CY - 0.5f * Ht, Wd, Ht, 1.f);
			F.Text(TEXT("UNSOLD"), CX, CY - 18.f * Scale, 72.f * Scale, InkDim(), 0.5f, EWeight::Black, 400);
			F.Label(P.Name.ToUpper(), CX, CY + 48.f * Scale, 0.5f, InkFaint(), 18.f);
		}
		else
		{
			const FAuctionFranchise& Fc = Fr(A.LastSoldTo);
			const bool bRtm = A.Teams[A.LastSoldTo].Squad.Num() > 0 && A.Teams[A.LastSoldTo].Squad.Last().bRtm;
			const float Wd = 860.f * Scale, Ht = 220.f * Scale, X = CX - 0.5f * Wd, Y = CY - 0.5f * Ht;
			const FLinearColor Fill = Tint(Fc.Primary, 0.5f, 1.f);
			F.Box(X, Y, Wd, Ht, Fill, 8.f, Hair(0.1f));
			F.Box(X, Y, Wd, 5.f * Scale, Fc.Primary, 2.f);
			const float StampW = 0.3f * Wd;
			F.Text(TEXT("SOLD"), X + 0.5f * StampW, CY, 84.f * Scale, Danger(), 0.5f, EWeight::Black, 200);
			F.VLine(X + StampW, Y + 36.f * Scale, Ht - 72.f * Scale, 0.2f);
			const float RX = X + StampW + 36.f;
			const FLinearColor Ink_ = InkOn(Fill);
			F.Text(P.Name.ToUpper(), RX, CY - 60.f * Scale, 32.f * Scale, Ink_, 0.f, EWeight::Black);
			F.Label(FString::Printf(TEXT("TO %s%s"), *Fc.Name.ToUpper(), bRtm ? TEXT("  ·  RIGHT TO MATCH") : TEXT("")), RX, CY - 22.f * Scale, 0.f,
				Ink_ * FLinearColor(1.f, 1.f, 1.f, 0.75f), 16.f * Scale);
			F.Text(AuctionRules::Money(A.Price), RX, CY + 42.f * Scale, 64.f * Scale, Money(), 0.f, EWeight::Black);
			const int32 Paid = AuctionRules::Fee(A.Config.Mode, P.IsOverseas(), A.Price);
			if (Paid < A.Price)
				F.Label(FString::Printf(TEXT("PAID %s  ·  %s TO THE BOARD"), *AuctionRules::Money(Paid), *AuctionRules::Money(A.Price - Paid)), RX, CY + 88.f * Scale, 0.f, Ink_, 13.f);
			F.Crest(X + Wd - 90.f * Scale, CY, 54.f * Scale, A.LastSoldTo);
			// A record: a gold banner over the card.
			if (!F.G.RecordBanner.IsEmpty() && F.G.Now() - F.G.RecordAt < 6.0)
				F.P.Pill(F.G.RecordBanner, F.At(CX, Y - 34.f * Scale), F.F(22.f, EWeight::Black, 240), Money(), Hex(0x1A1204), 36.f * F.U, 50.f * F.U, 25.f * F.U);
		}
		F.P.Alpha = 1.f;
	}

	void PaintSetIntro(FFrame& F, const FAuction& A)
	{
		const FAuctionSet* S = A.CurrentSet();
		if (A.Phase != EAuctionPhase::SetIntro || !S) return;
		F.P.Alpha = FMath::Clamp(A.PhaseTime() / 0.4f, 0.f, 1.f);
		const int32 N = FMath::Min(S->Players.Num(), 10);
		const float Wd = 820.f, RowH = 42.f, Ht = 150.f + N * RowH, X = StageMid(F) - 0.5f * Wd, Y = 170.f;
		F.Card(X, Y, Wd, Ht, 0.97f);
		F.Box(X, Y, Wd, 3.f, Money(), 1.5f);
		F.Label(S->bAccelerated ? TEXT("ACCELERATED ROUND") : TEXT("NEXT SET"), X + 40.f, Y + 44.f, 0.f, Money(), 15.f);
		F.Text(S->Name.ToUpper(), X + 40.f, Y + 88.f, 40.f, Ink(), 0.f, EWeight::Black);
		for (int32 I = 0; I < N; ++I)
		{
			const FAuctionPlayer& P = FAuction::Player(S->Players[I]);
			const float RY = Y + 130.f + I * RowH, MY = RY + 0.5f * RowH;
			F.HLine(X + 40.f, RY, Wd - 80.f, 0.07f);
			F.P.Circle(F.At(X + 46.f, MY), 4.f * F.U, RoleColour(P.Role));
			F.Text(P.Name, X + 62.f, MY, 20.f, Ink(), 0.f, EWeight::Bold);
			F.Label(FString::Printf(TEXT("%s  ·  %s"), AuctionRules::RoleCode(P.Role), *P.Country.ToUpper()), X + 420.f, MY, 0.f, InkFaint(), 13.f);
			F.Text(AuctionRules::Money(P.Base), X + Wd - 40.f, MY, 20.f, Money(), 1.f, EWeight::Black);
		}
		F.P.Alpha = 1.f;
	}

	/** The Right to Match questions to the human, with the time left to answer. */
	void PaintRtm(FFrame& F, const FAuction& A)
	{
		if (!A.AwaitingHuman() || F.G.bPaused) return;
		AAuctionGameMode& G = F.G;
		F.Box(0.f, 0.f, F.W, F.H, Hex(0x000000, 0.55f));
		const float Wd = 780.f, Ht = 380.f, X = 0.5f * (F.W - Wd), Y = 0.5f * (F.H - Ht) - 80.f, CX = X + 0.5f * Wd;
		const FAuctionPlayer& P = FAuction::Player(A.Lot);
		F.Card(X, Y, Wd, Ht, 1.f);
		// The time left to answer runs down along the top edge.
		const float Left = FMath::Clamp(1.f - A.PhaseTime() / FAuction::HumanRtmTimeout, 0.f, 1.f);
		F.Box(X, Y, Wd * Left, 3.f, Left > 0.3f ? Money() : Danger(), 1.5f);
		const int32 Asked = A.Phase == EAuctionPhase::RtmRaise ? A.Holder : A.RtmTeam;
		F.Label(G.Picked.Num() > 1 ? FString::Printf(TEXT("RIGHT TO MATCH  ·  %s TO ANSWER"), *Fr(Asked).Code) : FString(TEXT("RIGHT TO MATCH")), CX, Y + 50.f, 0.5f, Money(), 16.f);
		F.Text(P.Name.ToUpper(), CX, Y + 98.f, 42.f, Ink(), 0.5f, EWeight::Black);
		const float BY = Y + Ht - 108.f, BW = 0.5f * (Wd - 96.f);
		switch (A.Phase)
		{
		case EAuctionPhase::RtmAsk:
			F.Text(FString::Printf(TEXT("%s won him at %s. Use one of your %d RTM cards to bring him back?"), *Fr(A.Holder).Name, *AuctionRules::Money(A.Price), A.Teams[A.RtmTeam].RtmCards),
				CX, Y + 158.f, 19.f, InkDim(), 0.5f, EWeight::Medium);
			F.Text(FString::Printf(TEXT("%s will get one final raise first."), *Fr(A.Holder).Code), CX, Y + 190.f, 16.f, InkFaint(), 0.5f, EWeight::Medium);
			F.Button(X + 40.f, BY, BW, 68.f, TEXT("USE RTM"), EBtn::Primary, [&G]() { G.Auction->HumanRtm(true); }, true, 22.f);
			F.Button(X + 56.f + BW, BY, BW, 68.f, TEXT("LET HIM GO"), EBtn::Secondary, [&G]() { G.Auction->HumanRtm(false); }, true, 22.f);
			break;
		case EAuctionPhase::RtmRaise:
		{
			F.Text(FString::Printf(TEXT("%s have used their RTM. Make your final bid: they must match it to take him."), *Fr(A.RtmTeam).Name),
				CX, Y + 158.f, 19.f, InkDim(), 0.5f, EWeight::Medium);
			const int32 Max = A.MaxBid(A.Holder);
			G.RaiseTo = FMath::Clamp(G.RaiseTo, A.Price, FMath::Max(A.Price, Max));
			F.Button(CX - 210.f, Y + 186.f, 60.f, 60.f, FString(), EBtn::Secondary, [&G]()
			{
				int32 Down = G.Auction->Price;
				while (AuctionRules::NextBid(Down) < G.RaiseTo) Down = AuctionRules::NextBid(Down);
				G.RaiseTo = Down;
			}, G.RaiseTo > A.Price, 22.f, IconRemove);
			F.Text(AuctionRules::Money(G.RaiseTo), CX, Y + 216.f, 44.f, Money(), 0.5f, EWeight::Black);
			F.Button(CX + 150.f, Y + 186.f, 60.f, 60.f, FString(), EBtn::Secondary, [&G, Max]()
			{
				if (AuctionRules::NextBid(G.RaiseTo) <= Max) G.RaiseTo = AuctionRules::NextBid(G.RaiseTo);
			}, AuctionRules::NextBid(G.RaiseTo) <= Max, 22.f, IconAdd);
			F.Button(X + 40.f, BY + 10.f, BW, 68.f, TEXT("SUBMIT FINAL BID"), EBtn::Primary, [&G]() { G.Auction->HumanFinalRaise(G.RaiseTo); }, true, 21.f);
			F.Button(X + 56.f + BW, BY + 10.f, BW, 68.f, FString::Printf(TEXT("STAY AT %s"), *AuctionRules::Money(A.Price)), EBtn::Secondary,
				[&G]() { G.Auction->HumanFinalRaise(G.Auction->Price); }, true, 21.f);
			break;
		}
		case EAuctionPhase::RtmMatch:
			F.Text(FString::Printf(TEXT("%s's final bid is %s. Match it and he is yours."), *Fr(A.Holder).Name, *AuctionRules::Money(A.Price)),
				CX, Y + 158.f, 19.f, InkDim(), 0.5f, EWeight::Medium);
			F.Text(FString::Printf(TEXT("Your purse: %s"), *AuctionRules::Money(A.Teams[A.RtmTeam].Purse)), CX, Y + 190.f, 16.f, InkFaint(), 0.5f, EWeight::Medium);
			F.Button(X + 40.f, BY, BW, 68.f, FString::Printf(TEXT("MATCH %s"), *AuctionRules::Money(A.Price)), EBtn::Primary, [&G]() { G.Auction->HumanMatch(true); },
				A.CanAfford(A.RtmTeam, A.Lot, A.Price), 21.f);
			F.Button(X + 56.f + BW, BY, BW, 68.f, TEXT("DECLINE"), EBtn::Secondary, [&G]() { G.Auction->HumanMatch(false); }, true, 21.f);
			break;
		default:
			break;
		}
	}

	// ---- Panels ----------------------------------------------------------------------------------------------------

	void PanelFrame(FFrame& F, float& X, float& Y, float& Wd, float& Ht, const FString& Title)
	{
		Wd = 720.f; X = F.W - Edge - Wd; Y = TopY + 84.f + 14.f; Ht = ThirdY(F) - 18.f - Y;
		F.Card(X, Y, Wd, Ht, 0.98f);
		F.Text(Title, X + 28.f, Y + 36.f, 22.f, Ink(), 0.f, EWeight::Black, 120);
		F.Button(X + Wd - 56.f, Y + 14.f, 44.f, 44.f, FString(), EBtn::Quiet, [&G = F.G]() { G.Panel = EPanel::None; }, true, 18.f, IconClose);
		F.HLine(X + 28.f, Y + 70.f, Wd - 56.f, 0.08f);
	}

	/** The row the viewer cares about: a faint lift and a gold edge. */
	void RowMark(FFrame& F, float X, float Y, float Wd, float Ht)
	{
		F.Box(X, Y, Wd, Ht, Hex(0xECC870, 0.07f), 4.f);
		F.Box(X, Y + 8.f, 3.f, Ht - 16.f, Money(), 1.5f);
	}

	void PaintPursePanel(FFrame& F, const FAuction& A)
	{
		float X, Y, Wd, Ht;
		PanelFrame(F, X, Y, Wd, Ht, TEXT("PURSE REMAINING"));
		F.Label(TEXT("SQUAD 18-25  ·  OVERSEAS MAX 8"), X + Wd - 76.f, Y + 36.f, 1.f, InkFaint(), 12.f);
		const TCHAR* Heads[] = { TEXT("TEAM"), TEXT("PURSE"), TEXT("PLAYERS"), TEXT("OS"), TEXT("RTM") };
		const float Cols[] = { X + 90.f, X + 420.f, X + 515.f, X + 585.f, X + 650.f };
		for (int32 C = 0; C < 5; ++C) F.Label(Heads[C], Cols[C], Y + 94.f, C == 0 ? 0.f : 1.f, InkFaint(), 12.f);
		const float RowH = FMath::Min(56.f, (Ht - 120.f) / 10.f);
		for (int32 T = 0; T < A.Teams.Num(); ++T)
		{
			const float RY = Y + 112.f + T * RowH, MY = RY + 0.5f * RowH;
			const FAuctionTeam& Team = A.Teams[T];
			if (T == F.G.Team) RowMark(F, X + 12.f, RY, Wd - 24.f, RowH);
			else if (T) F.HLine(X + 28.f, RY, Wd - 56.f, 0.06f);
			F.Crest(X + 50.f, MY, 0.34f * RowH, T);
			F.Text(Fr(T).Name, Cols[0], MY, 19.f, Ink(), 0.f, EWeight::Bold);
			F.Text(AuctionRules::Money(Team.Purse), Cols[1], MY, 22.f, Money(), 1.f, EWeight::Black);
			F.Text(FString::FromInt(Team.Squad.Num()), Cols[2], MY, 20.f, Team.Squad.Num() < 18 ? Amber() : Ink(), 1.f, EWeight::Black);
			F.Text(FString::FromInt(Team.Overseas()), Cols[3], MY, 20.f, Ink(), 1.f, EWeight::Black);
			F.Text(FString::FromInt(Team.RtmCards), Cols[4], MY, 20.f, Teal(), 1.f, EWeight::Black);
			F.Hit(F.Rect(X + 12.f, RY, Wd - 24.f, RowH), [&G = F.G, T]() { G.PanelTeam = T; G.Panel = EPanel::Squad; });
		}
	}

	void PaintPlayersPanel(FFrame& F, const FAuction& A)
	{
		float X, Y, Wd, Ht;
		PanelFrame(F, X, Y, Wd, Ht, TEXT("PLAYER LIST"));
		F.Button(X + 28.f, Y + 76.f, Wd - 56.f, 40.f, F.G.bCurrentSetOnly ? TEXT("SHOWING: THIS SET  ·  TAP FOR ALL") : TEXT("SHOWING: ALL REMAINING  ·  TAP FOR THIS SET"),
			EBtn::Quiet, [&G = F.G]() { G.bCurrentSetOnly = !G.bCurrentSetOnly; }, true, 15.f);
		// This set and the ones after it; in accelerated sets the human nominates who comes to the table.
		// With the toggle on, only the set under the hammer.
		TArray<TPair<int32, int32>> Rows; // set, player
		for (int32 S = A.SetIndex; S < A.Sets.Num() && Rows.Num() < 200; ++S)
		{
			if (F.G.bCurrentSetOnly && S != A.SetIndex) break;
			for (int32 P : A.Sets[S].Players) Rows.Add({ S, P });
		}
		const float RowH = 44.f, Top = Y + 124.f;
		const int32 Fit = FMath::FloorToInt((Ht - 174.f) / RowH);
		const int32 First = FMath::Clamp(F.Scroll, 0, FMath::Max(0, Rows.Num() - Fit));
		for (int32 I = First; I < FMath::Min(Rows.Num(), First + Fit); ++I)
		{
			const FAuctionSet& S = A.Sets[Rows[I].Key];
			const FAuctionPlayer& P = FAuction::Player(Rows[I].Value);
			const float RY = Top + (I - First) * RowH, MY = RY + 0.5f * RowH;
			const int32* To = A.SoldTo.Find(P.Id);
			const bool bNow = P.Id == A.Lot;
			if (bNow) RowMark(F, X + 12.f, RY, Wd - 24.f, RowH);
			else if (I > First) F.HLine(X + 28.f, RY, Wd - 56.f, 0.05f);
			F.Label(S.Code, X + 30.f, MY, 0.f, InkFaint(), 12.f);
			F.Text(P.Name, X + 100.f, MY, 19.f, Ink(), 0.f, EWeight::Bold);
			F.Label(FString::Printf(TEXT("%s  %d"), AuctionRules::RoleCode(P.Role), P.Overall()), X + 330.f, MY, 0.f, InkDim(), 13.f);
			FString Status = AuctionRules::Money(P.Base);
			FLinearColor C = InkDim();
			if (To) { Status = FString::Printf(TEXT("%s  %s"), *Fr(*To).Code, *AuctionRules::Money(A.Teams[*To].Squad.FindByPredicate([&](const FAuctionSigning& X_) { return X_.Player == P.Id; })->Price)); C = Money(); }
			else if (bNow) { Status = TEXT("ON THE BLOCK"); C = Amber(); }
			else if (A.Unsold.Contains(P.Id)) { Status = TEXT("UNSOLD"); C = InkFaint(); }
			F.Text(Status, X + Wd - 30.f, MY, 17.f, C, 1.f, EWeight::Black);
			if (!To)
			{
				// On the shortlist, at about what the room expects him to go for; the war room tunes the limit.
				const bool bWanted = A.WishFor(F.G.Team, P.Id) != nullptr;
				F.Button(X + 400.f, RY + 5.f, 40.f, RowH - 10.f, FString(), bWanted ? EBtn::Primary : EBtn::Quiet,
					[&G = F.G, Id = P.Id, bWanted]()
					{
						G.SetWish(Id, bWanted ? 0 : AuctionRules::OnLadder(G.Auction->ExpectedPrice(Id), FAuction::Player(Id).Base), false);
					}, true, 16.f, bWanted ? IconCheck : IconAdd);
			}
			if (S.bAccelerated && !To && !bNow && Rows[I].Key > A.SetIndex)
			{
				const bool bOn = A.IsNominated(P.Id);
				F.Button(X + 446.f, RY + 5.f, 40.f, RowH - 10.f, FString(), bOn ? EBtn::Primary : EBtn::Quiet,
					[&G = F.G, Id = P.Id, bOn]() { G.Auction->Nominate(Id, !bOn); }, true, 16.f, bOn ? Glyph::Star : IconStarLine);
			}
		}
		F.HLine(X + 28.f, Y + Ht - 44.f, Wd - 56.f, 0.08f);
		F.Text(FString::Printf(TEXT("%d-%d of %d   ·   scroll for more   ·   + shortlists   ·   the star nominates for the accelerated round"), First + 1, FMath::Min(Rows.Num(), First + Fit), Rows.Num()),
			X + 0.5f * Wd, Y + Ht - 22.f, 13.f, InkFaint(), 0.5f, EWeight::Medium);
	}

	void PaintSquadList(FFrame& F, const FAuction& A, int32 T, float X, float Y, float Wd, float Ht)
	{
		const FAuctionTeam& Team = A.Teams[T];
		TArray<FAuctionSigning> Squad = Team.Squad;
		Squad.StableSort([](const FAuctionSigning& L, const FAuctionSigning& R) { return int32(FAuction::Player(L.Player).Role) < int32(FAuction::Player(R.Player).Role); });
		const float RowH = FMath::Min(40.f, Ht / FMath::Max(13, (Squad.Num() + 1) / 2));
		const float ColW = 0.5f * (Wd - 32.f);
		for (int32 I = 0; I < Squad.Num(); ++I)
		{
			const FAuctionPlayer& P = FAuction::Player(Squad[I].Player);
			const int32 Half = (Squad.Num() + 1) / 2;
			const float RX = X + (I / Half) * (ColW + 32.f), RY = Y + (I % Half) * RowH, MY = RY + 0.5f * RowH;
			if (I % Half) F.HLine(RX, RY, ColW, 0.05f);
			F.P.Circle(F.At(RX + 5.f, MY), 4.f * F.U, RoleColour(P.Role));
			F.Text(P.Short.IsEmpty() ? P.Name : P.Short, RX + 20.f, MY, 17.f, Ink(), 0.f, EWeight::Bold);
			const TCHAR* Tag = Squad[I].bRetained ? TEXT("RET  ") : Squad[I].bRtm ? TEXT("RTM  ") : TEXT("");
			F.Text(FString::Printf(TEXT("%s%s%s"), P.IsOverseas() ? TEXT("OS  ") : TEXT(""), Tag, *AuctionRules::Money(Squad[I].Price)),
				RX + ColW, MY, 16.f, Squad[I].bRetained ? Teal() : Money(), 1.f, EWeight::Black);
		}
	}

	void PaintSquadPanel(FFrame& F, const FAuction& A)
	{
		float X, Y, Wd, Ht;
		const int32 T = F.G.PanelTeam;
		PanelFrame(F, X, Y, Wd, Ht, FString::Printf(TEXT("%s SQUAD"), *Fr(T).Code));
		for (int32 I = 0; I < A.Teams.Num(); ++I)
		{
			const float CX = X + 52.f + I * 66.f, CY = Y + 112.f;
			F.P.Alpha = I == T ? 1.f : 0.5f;
			F.Crest(CX, CY, I == T ? 24.f : 20.f, I);
			F.P.Alpha = 1.f;
			if (I == T) F.P.Circle(F.At(CX, CY), 28.f * F.U, FLinearColor::Transparent, Money(), 1.5f * F.U);
			F.Hit(F.Rect(CX - 30.f, CY - 30.f, 60.f, 60.f), [&G = F.G, I]() { G.PanelTeam = I; });
		}
		const FAuctionTeam& Team = A.Teams[T];
		int32 Roles[5] = {};
		for (const FAuctionSigning& S : Team.Squad) ++Roles[int32(FAuction::Player(S.Player).Role)];
		F.Label(FString::Printf(TEXT("%d PLAYERS  ·  %d OVERSEAS  ·  BAT %d  WK %d  AR %d  PACE %d  SPIN %d  ·  %s LEFT"), Team.Squad.Num(), Team.Overseas(),
			Roles[0], Roles[1], Roles[2], Roles[3], Roles[4], *AuctionRules::Money(Team.Purse)), X + 28.f, Y + 170.f, 0.f, InkDim(), 13.f);
		PaintSquadList(F, A, T, X + 28.f, Y + 196.f, Wd - 56.f, Ht - 216.f);
	}

	/** A money dial: minus, the figure, plus, stepping along the bid ladder. */
	void Dial(FFrame& F, float X, float Y, int32 Value, int32 Floor, TFunction<void(int32)> Set)
	{
		F.Button(X, Y, 34.f, 30.f, FString(), EBtn::Quiet, [Value, Floor, Set]()
		{
			int32 Down = Floor;
			while (AuctionRules::NextBid(Down) < Value) Down = AuctionRules::NextBid(Down);
			Set(Down);
		}, Value > Floor, 14.f, IconRemove);
		F.Text(AuctionRules::Money(Value), X + 94.f, Y + 15.f, 16.f, Money(), 0.5f, EWeight::Black);
		F.Button(X + 154.f, Y, 34.f, 30.f, FString(), EBtn::Quiet, [Value, Set]() { Set(AuctionRules::NextBid(Value)); }, true, 14.f, IconAdd);
	}

	/**
	 * The war room: what the side needs (its best twelve, the holes, the money per place), the shortlist with a limit
	 * and auto-bid for each and when he comes up, and the rival radar: every other side's purse, places, overseas room
	 * and, as the analysts read them, whom they are after.
	 */
	void PaintWarRoom(FFrame& F, const FAuction& A)
	{
		AAuctionGameMode& G = F.G;
		float X, Y, Wd, Ht;
		PanelFrame(F, X, Y, Wd, Ht, FString::Printf(TEXT("%s WAR ROOM"), *Fr(G.Team).Code));
		const FAuctionNeeds N = A.Needs(G.Team);
		const FAuctionTeam& Team = A.Teams[G.Team];
		// Needs.
		float RY = Y + 96.f;
		F.Label(TEXT("BEST TWELVE"), X + 28.f, RY, 0.f, InkFaint(), 12.f);
		F.Text(FString::Printf(TEXT("%.0f"), N.Strength), X + 28.f, RY + 34.f, 36.f, Money(), 0.f, EWeight::Black);
		F.Label(FString::Printf(TEXT("#%d OF 10"), N.Rank), X + 90.f, RY + 38.f, 0.f, Ink(), 14.f);
		F.Label(TEXT("HOLES"), X + 210.f, RY, 0.f, InkFaint(), 12.f);
		const FString Holes = N.Holes.IsEmpty() ? FString(TEXT("None: a complete side")) : FString::Join(N.Holes, TEXT("  ·  "));
		F.Text(Holes.Len() > 70 ? Holes.Left(67) + TEXT("...") : Holes, X + 210.f, RY + 24.f, 15.f, N.Holes.IsEmpty() ? Teal() : Amber(), 0.f, EWeight::Bold);
		F.Text(FString::Printf(TEXT("%s left  ·  %d places (%d to 18)  ·  %s a place  ·  %d overseas slots  ·  %d timeouts"), *AuctionRules::Money(Team.Purse),
			N.Places, N.ToMinimum, *AuctionRules::Money(N.PerPlace), N.OverseasLeft, Team.Timeouts), X + 210.f, RY + 50.f, 13.f, InkDim(), 0.f, EWeight::Medium);
		// The shortlist.
		RY += 84.f;
		F.HLine(X + 28.f, RY, Wd - 56.f, 0.08f);
		F.Label(TEXT("SHORTLIST  ·  + IN THE PLAYER LIST ADDS"), X + 28.f, RY + 20.f, 0.f, Money(), 12.f);
		TArray<int32> List;
		for (const auto& W : A.Wishes(G.Team)) List.Add(W.Key);
		List.Sort([&A](int32 L, int32 R)
		{
			const int32 LL = A.LotsUntil(L), LR = A.LotsUntil(R);
			return (LL < 0 ? 100000 : LL) < (LR < 0 ? 100000 : LR);
		});
		const float RowH = 38.f;
		for (int32 I = 0; I < FMath::Min(List.Num(), 6); ++I)
		{
			const int32 Id = List[I];
			const FAuctionPlayer& P = FAuction::Player(Id);
			const FAuctionWish W = *A.WishFor(G.Team, Id);
			const float Row = RY + 40.f + I * RowH, MY = Row + 0.5f * RowH;
			F.P.Circle(F.At(X + 34.f, MY), 4.f * F.U, RoleColour(P.Role));
			F.Text(P.Short.IsEmpty() ? P.Name : P.Short, X + 46.f, MY, 16.f, Ink(), 0.f, EWeight::Bold);
			const int32 Until = A.LotsUntil(Id);
			const int32* To = A.SoldTo.Find(Id);
			const FString When = To ? FString::Printf(TEXT("TO %s"), *Fr(*To).Code) : Until == 0 ? FString(TEXT("ON THE BLOCK")) : Until > 0 ? FString::Printf(TEXT("IN %d LOTS"), Until) : FString(TEXT("NOT DRAWN"));
			F.Label(When, X + 230.f, MY, 0.f, Until == 0 ? Amber() : InkFaint(), 12.f);
			F.Label(FString::Printf(TEXT("ROOM %s"), *AuctionRules::Money(A.ExpectedPrice(Id))), X + 350.f, MY, 0.f, InkFaint(), 11.f);
			Dial(F, X + 450.f, Row + 4.f, W.Max, P.Base, [&G, Id, W](int32 V) { G.SetWish(Id, V, W.bAuto); });
			F.Button(X + 648.f, Row + 4.f, 44.f, 30.f, TEXT("AUTO"), W.bAuto ? EBtn::Primary : EBtn::Quiet, [&G, Id, W]() { G.SetWish(Id, W.Max, !W.bAuto); }, !To, 11.f);
		}
		if (List.IsEmpty()) F.Text(TEXT("No one yet. Shortlist the players you want: auto-bid goes to your limit for you."), X + 28.f, RY + 58.f, 15.f, InkFaint(), 0.f, EWeight::Medium);
		// The rival radar.
		RY += 40.f + 6.f * RowH + 8.f;
		F.HLine(X + 28.f, RY, Wd - 56.f, 0.08f);
		F.Label(TEXT("RIVAL RADAR"), X + 28.f, RY + 20.f, 0.f, Money(), 12.f);
		const float Rh = FMath::Min(30.f, (Y + Ht - RY - 44.f) / 9.f);
		int32 Row = 0;
		for (int32 T = 0; T < A.Teams.Num(); ++T)
		{
			if (T == G.Team) continue;
			const float TY = RY + 40.f + Row++ * Rh, MY = TY + 0.5f * Rh;
			const FAuctionNeeds RN = A.Needs(T);
			F.Crest(X + 40.f, MY, 0.4f * Rh, T);
			F.Text(AuctionRules::Money(A.Teams[T].Purse), X + 64.f, MY, 14.f, Money(), 0.f, EWeight::Black);
			F.Text(FString::Printf(TEXT("%d pl  %d os"), RN.Places, RN.OverseasLeft), X + 160.f, MY, 13.f, InkDim(), 0.f, EWeight::Bold);
			FString After;
			for (int32 K = 0; K < FMath::Min(3, RN.Targets.Num()); ++K) After += (K ? TEXT(", ") : TEXT("")) + FAuction::Player(RN.Targets[K]).Short;
			if (After.IsEmpty() && !RN.Holes.IsEmpty()) After = RN.Holes[0];
			F.Text(After, X + 260.f, MY, 13.f, InkFaint(), 0.f, EWeight::Medium);
		}
	}

	void PaintTopControlBar(FFrame& F, const FAuction& A)
	{
		AAuctionGameMode& G = F.G;
		const FAuctionSet* S = A.CurrentSet();
		const FString Title = TEXT("IPL MEGA AUCTION"), Sub = FString::Printf(TEXT("%s   ·   LOTS %d   ·   SOLD %d"), S ? *S->Name.ToUpper() : TEXT("THE END"), A.LotsHeld, A.SoldTo.Num());
		const float Pad = 24.f, TitleX = Edge + Pad + 88.f;
		const float LeftWd = FMath::Max(TitleX - Edge + HudPaint::FPaint::Measure(Title, F.F(24.f, EWeight::Black, 60)).X / F.U,
			Pad + HudPaint::FPaint::Measure(Sub, F.F(14.f, EWeight::Bold, 220)).X / F.U) + Pad;

		const float TopLeftEnd = Edge + LeftWd;
		const float YourTeamStart = F.W - Edge - 440.f;
		const float Gap = 14.f;
		const float BarX = TopLeftEnd + Gap;
		const float BarW = YourTeamStart - Gap - BarX;
		const float BarH = 50.f;
		const float BarY = TopY + 17.f;

		if (BarW < 400.f) return;

		// Minimalist, clean, organized horizontal bar beside live IPL auction
		F.Box(BarX, BarY, BarW, BarH, PaneHi(0.95f), 6.f, Hair(0.12f), 1.f);

		const bool bLive = A.Phase != EAuctionPhase::Finished && !A.AwaitingHuman() && !G.bPaused;
		const bool bCanPause = A.Phase != EAuctionPhase::Finished;

		auto Toggle = [&G](EPanel P) { return [&G, P]() { G.Panel = G.Panel == P ? EPanel::None : P; if (P == EPanel::Squad && G.Panel == P) G.PanelTeam = G.Team; }; };

		struct FTopItem
		{
			FString Label;
			TFunction<void()> Action;
			bool bActive;
			bool bEnabled;
			float Width;
			bool bDividerAfter;
		};

		using EPace = AAuctionGameMode::EPace;
		static const TCHAR* PaceNames[] = { TEXT("PACE: WATCH"), TEXT("PACE: MY TARGETS"), TEXT("PACE: SIM") };
		const EPace NextPace = EPace((int32(G.PaceMode) + 1) % 3);
		const bool bHeld = A.IsHuman(A.Holder);
		const FTopItem Items[] = {
			{ TEXT("PURSE"), Toggle(EPanel::Purse), G.Panel == EPanel::Purse, true, 70.f, false },
			{ TEXT("PLAYERS"), Toggle(EPanel::Players), G.Panel == EPanel::Players, true, 84.f, false },
			{ TEXT("SQUADS"), Toggle(EPanel::Squad), G.Panel == EPanel::Squad, true, 78.f, false },
			{ TEXT("WAR ROOM"), Toggle(EPanel::WarRoom), G.Panel == EPanel::WarRoom, true, 96.f, true },
			{ TEXT("SKIP"), [&G]() { G.SkipCurrentLot(); }, false, bLive && !bHeld, 60.f, false },
			{ TEXT("SKIP SET"), [&G]() { G.Auction->SkipSet(); }, false, bLive && !bHeld, 84.f, true },
			{ PaceNames[int32(G.PaceMode)], [&G, NextPace]() { G.SetPace(NextPace); }, G.PaceMode != EPace::Watch, bCanPause, 150.f, true },
			{ G.bPaused ? TEXT("RESUME") : TEXT("PAUSE"), [&G]() { G.TogglePause(); }, G.bPaused, bCanPause, 76.f, false },
			{ TEXT("EXIT"), [&G]() { G.ExitToMenu(); }, false, true, 56.f, false }
		};

		float TotalItemsW = 0.f;
		for (const FTopItem& It : Items) TotalItemsW += It.Width + 4.f + (It.bDividerAfter ? 12.f : 0.f);

		float CurX = BarX + FMath::Max(6.f, 0.5f * (BarW - TotalItemsW));
		for (const FTopItem& It : Items)
		{
			const float BX = CurX, BY = BarY + 5.f, BW = It.Width, BH = BarH - 10.f;
			if (It.bActive)
			{
				F.Box(BX, BY, BW, BH, Hex(0xECC870, 0.15f), 4.f);
				F.Box(BX + 4.f, BY + BH - 3.f, BW - 8.f, 3.f, Money(), 1.5f);
			}
			const FLinearColor Ink_ = !It.bEnabled ? InkFaint() * FLinearColor(1.f, 1.f, 1.f, 0.35f) : It.bActive ? Money() : Ink();
			F.Text(It.Label, BX + 0.5f * BW, BY + 0.5f * BH, 14.f, Ink_, 0.5f, EWeight::Black, 80);
			if (It.bEnabled && It.Action) F.Hit(F.Rect(BX, BY, BW, BH), It.Action);
			CurX += BW + 4.f;
			if (It.bDividerAfter)
			{
				F.VLine(CurX + 5.f, BarY + 12.f, BarH - 24.f, 0.12f);
				CurX += 12.f;
			}
		}
	}

	/** One table's paddle: BID at the asking price, its own colours while it holds the bid. */
	void Paddle(FFrame& F, const FAuction& A, int32 Table, float BX, float BY, float BW, float BH, bool bLabelTeam)
	{
		AAuctionGameMode& G = F.G;
		const FString Blocked = G.bPaused || G.bDayBreak ? FString(TEXT("HELD")) : BidBlocked(A, Table);
		const bool bCan = A.CanHumanBid(Table) && !G.bPaused && !G.bDayBreak;
		const bool bMine = A.Holder == Table && A.Phase == EAuctionPhase::Bidding;
		if (bCan)
		{
			const float Glow = 0.5f + 0.5f * FMath::Sin(float(F.T) * 4.f);
			F.Box(BX - 5.f, BY - 5.f, BW + 10.f, BH + 10.f, FLinearColor::Transparent, 10.f, Hex(0xECC870, 0.2f + 0.3f * Glow), 2.f);
			F.Box(BX, BY, BW, BH, Money(), 6.f);
		}
		else if (bMine)
		{
			F.Box(BX, BY, BW, BH, Tint(Fr(Table).Primary, 0.55f, 0.96f), 6.f);
			F.Box(BX, BY, BW, 4.f, Fr(Table).Primary, 2.f);
		}
		else F.Card(BX, BY, BW, BH, 0.95f);
		const FLinearColor Ink_ = bCan ? Hex(0x1A1204) : bMine ? InkOn(Tint(Fr(Table).Primary, 0.55f, 1.f)) : InkFaint();
		const bool bPrice = A.Phase == EAuctionPhase::Bidding || A.Phase == EAuctionPhase::LotIntro;
		const float Big = FMath::Min(52.f, 0.36f * BH);
		const FString Word = bLabelTeam ? FString::Printf(TEXT("%s BID"), *Fr(Table).Code) : FString(TEXT("BID"));
		const float LabelY = bPrice ? BY + 0.4f * BH : BY + 0.5f * BH;
		if (bCan) F.Text(Word, BX + 0.5f * BW, LabelY, Big, Ink_, 0.5f, EWeight::Black, 300);
		else F.Label(Blocked.IsEmpty() ? Word : (bLabelTeam ? Fr(Table).Code + TEXT("  ") + Blocked : Blocked), BX + 0.5f * BW, LabelY, 0.5f, Ink_, FMath::Min(20.f, 0.2f * BH));
		if (bPrice)
			F.Text(bMine ? AuctionRules::Money(A.Price) : AuctionRules::Money(A.Lot != INDEX_NONE ? A.AskPrice() : 0), BX + 0.5f * BW, BY + 0.76f * BH,
				FMath::Min(30.f, 0.22f * BH), Ink_, 0.5f, EWeight::Black);
		if (bCan) F.Hit(F.Rect(BX, BY, BW, BH), [&G, Table]() { G.Bid(Table); });
	}

	void PaintControls(FFrame& F, const FAuction& A)
	{
		AAuctionGameMode& G = F.G;
		// The paddle: the one big action on the screen, level with the lower third. With people at several tables,
		// a paddle for each, stacked.
		const float BX = RailX(F), BY = ThirdY(F), BW = RailW, BH = ThirdH;
		const int32 N = FMath::Max(1, G.Picked.Num());
		if (N == 1) Paddle(F, A, G.Team, BX, BY, BW, BH, false);
		else
		{
			const float Gap = 6.f, H = (BH - (N - 1) * Gap) / N;
			for (int32 I = 0; I < N; ++I) Paddle(F, A, G.Picked[I], BX, BY + I * (H + Gap), BW, H, true);
		}
		if (A.Phase != EAuctionPhase::Bidding && A.Phase != EAuctionPhase::LotIntro) return;
		// The strip above: jump, time-out and auto-bid for the table in focus.
		const float SY = BY - 52.f, SW = (BW - 12.f) / 3.f;
		const int32 Jump = A.Lot != INDEX_NONE ? AuctionRules::NextBid(AuctionRules::NextBid(A.AskPrice())) : 0;
		const bool bLive = !G.bPaused && !G.bDayBreak;
		F.Button(BX, SY, SW, 44.f, FString::Printf(TEXT("JUMP %s"), *AuctionRules::Money(Jump)), EBtn::Secondary,
			[&G, Jump]() { G.JumpTo = Jump; G.JumpBid(G.Team); }, bLive && A.CanHumanBid(G.Team) && A.CanAfford(G.Team, A.Lot, Jump), 11.f);
		F.Button(BX + SW + 6.f, SY, SW, 44.f, FString::Printf(TEXT("TIME-OUT %d"), A.Teams[G.Team].Timeouts), EBtn::Secondary,
			[&G]() { G.Timeout(G.Team); }, bLive && A.CanRequestTimeout(G.Team), 12.f);
		const FAuctionWish* W = A.Lot != INDEX_NONE ? A.WishFor(G.Team, A.Lot) : nullptr;
		const bool bAutoOn = W && W->bAuto;
		const int32 Limit = W ? W->Max : (A.Lot != INDEX_NONE ? AuctionRules::OnLadder(A.ExpectedPrice(A.Lot), FAuction::Player(A.Lot).Base) : 0);
		F.Button(BX + 2.f * (SW + 6.f), SY, SW, 44.f, bAutoOn ? FString::Printf(TEXT("AUTO TO %s"), *AuctionRules::Money(Limit)) : FString(TEXT("AUTO-BID")),
			bAutoOn ? EBtn::Primary : EBtn::Secondary, [&G, Lot = A.Lot, Limit, bAutoOn]() { G.SetWish(Lot, Limit, !bAutoOn); }, bLive && A.Lot != INDEX_NONE, 12.f);
		// The analyst at the table leans over.
		const FString Whisper = A.Whisper(G.Team);
		if (!Whisper.IsEmpty() && A.Phase == EAuctionPhase::Bidding)
		{
			const float Wd = FMath::Min(F.W - 2.f * Edge - RailW - 40.f, HudPaint::FPaint::Measure(Whisper, F.F(16.f, EWeight::Medium)).X / F.U + 140.f);
			const float X = StageMid(F) - 0.5f * Wd, Y = TopY + 84.f + 26.f;
			F.Box(X, Y, Wd, 36.f, Hex(0x000000, 0.5f), 18.f);
			F.Label(TEXT("ANALYST"), X + 20.f, Y + 18.f, 0.f, Teal(), 11.f);
			F.Text(Whisper, X + 108.f, Y + 18.f, 16.f, InkDim(), 0.f, EWeight::Medium);
		}
	}

	/** The end of day one: the day's summary, and the room waits until the tables come back. */
	void PaintDayBreak(FFrame& F, const FAuction& A)
	{
		AAuctionGameMode& G = F.G;
		if (!G.bDayBreak) return;
		F.Box(0.f, 0.f, F.W, F.H, Hex(0x000000, 0.62f));
		const float Wd = 820.f, Ht = 520.f, X = 0.5f * (F.W - Wd), Y = 0.5f * (F.H - Ht) - 30.f, CX = X + 0.5f * Wd;
		F.Card(X, Y, Wd, Ht, 1.f);
		F.Box(X, Y, Wd, 3.f, Money(), 1.5f);
		F.Label(TEXT("END OF DAY ONE"), CX, Y + 50.f, 0.5f, Money(), 16.f);
		F.Text(FString::Printf(TEXT("%d SOLD  ·  %d LOTS"), A.SoldTo.Num(), A.LotsHeld), CX, Y + 104.f, 40.f, Ink(), 0.5f, EWeight::Black);
		const TArray<FAuctionRecord> Records = A.Records();
		for (int32 I = 0; I < FMath::Min(3, Records.Num()); ++I)
			F.Text(FString::Printf(TEXT("%s:  %s, %s, %s"), *Records[I].Title, *FAuction::Player(Records[I].Player).Name, *Fr(Records[I].Team).Code, *AuctionRules::Money(Records[I].Price)),
				CX, Y + 170.f + I * 32.f, 17.f, InkDim(), 0.5f, EWeight::Medium);
		for (int32 I = 0; I < G.Picked.Num(); ++I)
		{
			const int32 T = G.Picked[I];
			const FAuctionNeeds N = A.Needs(T);
			F.Text(FString::Printf(TEXT("%s: %d players, %s left, best twelve #%d%s"), *Fr(T).Code, A.Teams[T].Squad.Num(), *AuctionRules::Money(A.Teams[T].Purse), N.Rank,
				N.Holes.IsEmpty() ? TEXT("") : *(FString(TEXT("  ·  ")) + N.Holes[0])), CX, Y + 290.f + I * 28.f, 16.f, Money(), 0.5f, EWeight::Bold);
		}
		const float BY = Y + Ht - 96.f, BW = 0.5f * (Wd - 96.f);
		F.Button(X + 32.f, BY, BW, 64.f, TEXT("DAY TWO"), EBtn::Primary, [&G]() { G.EndDayBreak(); }, true, 20.f);
		F.Button(X + 64.f + BW, BY, BW, 64.f, TEXT("SAVE AND EXIT"), EBtn::Secondary, [&G]() { G.ExitToMenu(); }, true, 20.f);
	}

	/** The analyst desk between sets: two voices over the league, for a few seconds. */
	void PaintDesk(FFrame& F)
	{
		AAuctionGameMode& G = F.G;
		const float Age = float(G.Now() - G.AnalystAt);
		if (G.AnalystLines.IsEmpty() || Age > 9.f) return;
		F.P.Alpha = FMath::Clamp(Age / 0.3f, 0.f, 1.f) * FMath::Clamp((9.f - Age) / 0.6f, 0.f, 1.f);
		const float Wd = 760.f, Ht = 60.f + 30.f * G.AnalystLines.Num(), X = Edge, Y = TopY + 84.f + 80.f;
		F.Card(X, Y, Wd, Ht, 0.92f);
		F.Box(X, Y, 3.f, Ht, Teal(), 1.5f);
		F.Label(TEXT("ANALYST DESK"), X + 24.f, Y + 26.f, 0.f, Teal(), 13.f);
		for (int32 I = 0; I < G.AnalystLines.Num(); ++I)
			F.Text(G.AnalystLines[I], X + 24.f, Y + 58.f + I * 30.f, 16.f, I % 2 ? InkDim() : Ink(), 0.f, EWeight::Medium);
		F.P.Alpha = 1.f;
	}

	/** The pause card: the clock is held, so resume, restart or leave from here. */
	void PaintPaused(FFrame& F)
	{
		AAuctionGameMode& G = F.G;
		if (!G.bPaused) return;
		F.Box(0.f, 0.f, F.W, F.H, Hex(0x000000, 0.6f));
		const float Wd = 620.f, Ht = 400.f, X = 0.5f * (F.W - Wd), Y = 0.5f * (F.H - Ht) - 40.f, CX = X + 0.5f * Wd;
		F.Card(X, Y, Wd, Ht, 1.f);
		F.Box(X, Y, Wd, 3.f, Money(), 1.5f);
		F.Label(TEXT("AUCTION HELD"), CX, Y + 52.f, 0.5f, Money(), 16.f);
		F.Text(TEXT("PAUSED"), CX, Y + 110.f, 56.f, Ink(), 0.5f, EWeight::Black);
		F.Text(TEXT("The clock is stopped. Nothing moves until you resume."), CX, Y + 168.f, 17.f, InkDim(), 0.5f, EWeight::Medium);
		const float BY = Y + Ht - 96.f, BW = (Wd - 96.f) / 3.f;
		F.Button(X + 32.f, BY, BW, 64.f, TEXT("RESUME"), EBtn::Primary, [&G]() { G.TogglePause(); }, true, 20.f);
		F.Button(X + 48.f + BW, BY, BW, 64.f, TEXT("RESTART"), EBtn::Secondary, [&G]() { G.RestartAuction(); }, true, 20.f);
		F.Button(X + 64.f + 2.f * BW, BY, BW, 64.f, TEXT("EXIT"), EBtn::Quiet, [&G]() { G.ExitToMenu(); }, true, 20.f);
	}

	/**
	 * The IPL broadcast's split screen of a bidding duel: the two tables side by side in thin frames over the
	 * broadcast green, each captioned with its franchise, the side holding the bid framed in gold.
	 */
	void PaintSplit(FFrame& F, const FAuction& A)
	{
		const AAuctionRoom* Room = F.G.GetRoom();
		if (!Room || !Room->IsSplitShot() || A.Phase != EAuctionPhase::Bidding) return;
		const float Gap = 32.f, X0 = 120.f, Wd = (F.W - 2.f * X0 - RailW - Gap) / 2.f, Ht = Wd / 1.6f;
		// Centred vertically for symmetry: the two tables sit in the middle of the screen, clear of the
		// top cards and the lower third alike.
		const float Y0 = 0.5f * (F.H - Ht) - 10.f, CY0 = Y0 + 0.5f * Ht;
		F.Box(0.f, 0.f, F.W, F.H, Pane(1.f));
		for (int32 R = 1; R <= 3; ++R) F.P.Circle(F.At(0.5f * F.W, CY0), 260.f * R * F.U, FLinearColor::Transparent, Hex(0xD9B25A, 0.05f), 1.5f * F.U);
		for (int32 I = 0; I < 2; ++I)
		{
			const int32 T = Room->Split[I];
			UTextureRenderTarget2D* Target = Room->SplitTargets[I];
			if (!Target) continue;
			const float X = X0 + I * (Wd + Gap);
			FSlateBrush Picture;
			Picture.SetResourceObject(Target);
			Picture.ImageSize = FVector2D(Target->SizeX, Target->SizeY);
			FSlateDrawElement::MakeBox(F.P.Out, F.P.On(7), F.P.At(F.At(X, Y0), F.At(Wd, Ht)), &Picture, ESlateDrawEffect::None, F.P.A(FLinearColor::White));
			const bool bHolds = A.Holder == T;
			F.Box(X - 2.f, Y0 - 2.f, Wd + 4.f, Ht + 4.f, FLinearColor::Transparent, 4.f, bHolds ? Money() : Hair(0.15f), bHolds ? 3.f : 1.f);
			// The caption: the franchise under a bar in its colour.
			const float CY = Y0 + Ht + 34.f;
			F.Box(X + 0.5f * Wd - 40.f, Y0 + Ht + 12.f, 80.f, 3.f, Fr(T).Primary, 1.5f);
			F.Label(Fr(T).Short.ToUpper(), X + 0.5f * Wd, CY, 0.5f, bHolds ? Money() : Ink(), 20.f);
		}
	}

	void PaintLive(FFrame& F)
	{
		const FAuction& A = *F.G.Auction;
		PaintSplit(F, A);
		PaintTopLeft(F, A);
		PaintTopControlBar(F, A);
		PaintYourTeam(F, A);
		if (A.Lot != INDEX_NONE && A.Phase != EAuctionPhase::SetIntro) PaintLowerThird(F, A);
		PaintSetIntro(F, A);
		PaintHammer(F, A);
		PaintCaption(F);
		switch (F.G.Panel)
		{
		case EPanel::Purse: PaintPursePanel(F, A); break;
		case EPanel::Players: PaintPlayersPanel(F, A); break;
		case EPanel::Squad: PaintSquadPanel(F, A); break;
		case EPanel::WarRoom: PaintWarRoom(F, A); break;
		default: break;
		}
		PaintDesk(F);
		PaintControls(F, A);
		PaintTicker(F, A);
		PaintRtm(F, A);
		PaintDayBreak(F, A);
		PaintPaused(F);
	}

	// ---- Results ---------------------------------------------------------------------------------------------------

	void PaintResults(FFrame& F)
	{
		const FAuction& A = *F.G.Auction;
		const int32 T = F.G.PanelTeam;
		F.Scrim(0.86f);
		F.Crest(116.f, 110.f, 40.f, T);
		F.Heading(A.IsMini() ? FString::Printf(TEXT("THE IPL %d AUCTION IS COMPLETE"), A.Config.Season) : FString(TEXT("THE MEGA AUCTION IS COMPLETE")), Fr(T).Name.ToUpper(), 180.f, 86.f, 44.f);
		const FAuctionTeam& Team = A.Teams[T];
		int32 Bought = 0;
		for (const FAuctionSigning& S : Team.Squad) Bought += S.bRetained ? 0 : 1;
		F.Label(FString::Printf(TEXT("%d PLAYERS   ·   %d OVERSEAS   ·   %d BOUGHT   ·   SPENT %s   ·   %s LEFT"), Team.Squad.Num(), Team.Overseas(), Bought,
			*AuctionRules::Money(A.Config.Purse - Team.Purse), *AuctionRules::Money(Team.Purse)), 80.f, 196.f, 0.f, InkDim(), 15.f);
		const float Top = 230.f, SideW = 540.f, LW = F.W - 160.f - SideW - 32.f;
		F.Card(80.f, Top, LW, F.H - Top - 60.f, 0.95f);
		// The analysts' verdict: the grade, the reasons, the steals and the splurges.
		const FAuctionVerdict V = A.Verdict(T);
		F.Text(V.Grade, 140.f, Top + 64.f, 72.f, Money(), 0.5f, EWeight::Black);
		F.Label(FString::Printf(TEXT("SCORE %.0f  ·  BEST TWELVE %.0f, #%d OF 10"), V.Score, V.Needs.Strength, V.Needs.Rank), 208.f, Top + 44.f, 0.f, Money(), 13.f);
		F.Text(V.Summary, 208.f, Top + 76.f, 17.f, Ink(), 0.f, EWeight::Medium);
		auto Names = [](const TArray<int32>& Ids)
		{
			TArray<FString> Out;
			for (int32 Id : Ids) Out.Add(FAuction::Player(Id).Short.IsEmpty() ? FAuction::Player(Id).Name : FAuction::Player(Id).Short);
			return FString::Join(Out, TEXT(", "));
		};
		if (V.Steals.Num() > 0) F.Text(FString::Printf(TEXT("Steals: %s"), *Names(V.Steals)), 208.f, Top + 104.f, 15.f, Teal(), 0.f, EWeight::Bold);
		if (V.Splurges.Num() > 0) F.Text(FString::Printf(TEXT("Splurges: %s"), *Names(V.Splurges)), 208.f + LW * 0.45f, Top + 104.f, 15.f, Amber(), 0.f, EWeight::Bold);
		F.HLine(112.f, Top + 130.f, LW - 64.f, 0.08f);
		PaintSquadList(F, A, T, 112.f, Top + 150.f, LW - 64.f, F.H - Top - 230.f);

		// The league: every side's grade and top buy; tap one to see its squad.
		const float RX = F.W - 80.f - SideW, RowH = 54.f;
		F.Card(RX, Top, SideW, 10.f * RowH + 24.f, 0.95f);
		for (int32 I = 0; I < A.Teams.Num(); ++I)
		{
			const float RY = Top + 12.f + I * RowH;
			int32 Best = INDEX_NONE, TopPrice = 0;
			for (const FAuctionSigning& S : A.Teams[I].Squad) if (!S.bRetained && S.Price > TopPrice) { TopPrice = S.Price; Best = S.Player; }
			if (I == T) RowMark(F, RX + 10.f, RY, SideW - 20.f, RowH);
			else if (I) F.HLine(RX + 24.f, RY, SideW - 48.f, 0.06f);
			F.Crest(RX + 44.f, RY + 0.5f * RowH, 18.f, I);
			F.Text(FString::Printf(TEXT("%d players  ·  %s left"), A.Teams[I].Squad.Num(), *AuctionRules::Money(A.Teams[I].Purse)), RX + 80.f, RY + 18.f, 16.f, Ink(), 0.f, EWeight::Bold);
			if (Best != INDEX_NONE)
				F.Text(FString::Printf(TEXT("Top buy: %s %s"), *FAuction::Player(Best).Name, *AuctionRules::Money(TopPrice)), RX + 80.f, RY + 38.f, 13.f, Money(), 0.f, EWeight::Bold);
			F.Text(A.Verdict(I).Grade, RX + SideW - 36.f, RY + 0.5f * RowH, 28.f, Money(), 1.f, EWeight::Black);
			F.Hit(F.Rect(RX + 10.f, RY, SideW - 20.f, RowH), [&G = F.G, I]() { G.PanelTeam = I; });
		}
		// The records of the auction.
		const TArray<FAuctionRecord> Records = A.Records();
		const float RecY = Top + 10.f * RowH + 44.f;
		for (int32 I = 0; I < FMath::Min(2, Records.Num()); ++I)
			F.Text(FString::Printf(TEXT("%s: %s (%s) %s"), *Records[I].Title, *FAuction::Player(Records[I].Player).Short, *Fr(Records[I].Team).Code, *AuctionRules::Money(Records[I].Price)),
				RX, RecY + I * 24.f, 13.f, InkFaint(), 0.f, EWeight::Bold);
		// The handoff: these exact squads become the IPL season. Back to menu stays below it.
		F.Button(RX, F.H - 60.f - 76.f - 88.f, SideW, 76.f, FString::Printf(TEXT("START IPL %d SEASON"), A.Config.Season), EBtn::Primary, [&G = F.G]() { G.StartSeason(); }, true, 22.f);
		F.Button(RX, F.H - 60.f - 76.f, SideW, 76.f, TEXT("BACK TO MENU"), EBtn::Secondary, [&G = F.G]() { G.ExitToMenu(); }, true, 22.f);
	}
}

void SAuctionHUD::Construct(const FArguments&, AAuctionGameMode* InGame)
{
	Game = InGame;
}

void SAuctionHUD::AddToGameViewport(AAuctionGameMode* InGame)
{
	if (GEngine && GEngine->GameViewport)
	{
		// The viewport owns the widget (see SCricketMatchHUD::AddToGameViewport); it is cleared on map travel.
		GEngine->GameViewport->AddViewportWidgetContent(SNew(SAuctionHUD, InGame), 20);
		UE_LOG(LogCRICKET26, Display, TEXT("Auction HUD added to the viewport"));
	}
}

int32 SAuctionHUD::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle&, bool) const
{
	using namespace AuctionHudPrivate;
	Hits.Reset();
	AAuctionGameMode* G = Game.Get();
	if (!G || !G->GetWorld()) return Layer;
	const FVector2D Size = Geometry.GetLocalSize();
	const float U = FMath::Max(0.1f, float(Size.Y) / 1080.f);
	FPaint P{ Geometry, Out, Layer };
	AuctionHudPrivate::FFrame F{ P, *G, Hits, float(Size.X) / U, float(Size.Y) / U, U, G->Now(), Scroll };
	switch (G->Screen)
	{
	case EScreen::PickTeam: PaintPick(F); break;
	case EScreen::Retain: if (G->Auction) PaintRetain(F); break;
	case EScreen::Live: if (G->Auction) PaintLive(F); break;
	case EScreen::Results: if (G->Auction) PaintResults(F); break;
	}
	return P.Layer;
}

FReply SAuctionHUD::OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const FVector2D L = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
	for (int32 I = Hits.Num() - 1; I >= 0; --I)
	{
		if (!Hits[I].Box.IsInsideOrOn(L)) continue;
		const TFunction<void()> Do = Hits[I].Do; // the action may repaint and reset the list
		Do();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SAuctionHUD::OnMouseWheel(const FGeometry&, const FPointerEvent& Event)
{
	Scroll = FMath::Max(0, Scroll - FMath::RoundToInt(Event.GetWheelDelta() * 3.f));
	return FReply::Handled();
}
