#include "MatchHUDWidget.h"
#include "CRICKET26.h"
#include "CricketPresentation.h"
#include "SuperOverGameMode.h"
#include "SuperOverHUD.h"
#include "FrontendStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"

namespace CricketHUD
{
	FString HowOutName(EDismissal D);
}

namespace MatchHudPrivate
{
	using namespace FrontendStyle;
	using CricketTouch::EButton;
	using CricketTouch::EMode;

	// The match HUD's own shades of the frontend palette: one dark glass for every panel, amber for extras.
	FLinearColor Glass(float A = 0.86f) { return Hex(0x0B1426, A); }
	FLinearColor Hair() { return FrontendStyle::Line(); }
	FLinearColor Amber() { return Hex(0xE39B3A); }
	FLinearColor Turf() { return Hex(0x1F3524, 0.94f); }
	FLinearColor TurfHi() { return Hex(0x294430, 0.94f); }
	FLinearColor Clay() { return Hex(0xC9B489); }

	/** Text Px tall in local units (Slate sizes fonts in points at 96 DPI). */
	FSlateFontInfo HudFont(float Px, EWeight W = EWeight::Bold, int32 Spacing = 0)
	{
		FSlateFontInfo Info = Font(1, W, Spacing);
		Info.Size = FMath::Max(1.f, Px * 0.75f);
		return Info;
	}

	const TCHAR* TypeName(EDeliveryType T)
	{
		switch (T)
		{
		case EDeliveryType::Outswing: return TEXT("Outswinger");
		case EDeliveryType::Inswing: return TEXT("Inswinger");
		case EDeliveryType::Cutter: return TEXT("Off-cutter");
		case EDeliveryType::Slower: return TEXT("Slower ball");
		case EDeliveryType::Seam: return TEXT("Seam up");
		case EDeliveryType::CrossSeam: return TEXT("Cross-seam");
		case EDeliveryType::OffBreak: return TEXT("Off break");
		case EDeliveryType::ArmBall: return TEXT("Arm ball");
		case EDeliveryType::LegBreak: return TEXT("Leg break");
		case EDeliveryType::Googly: return TEXT("Googly");
		case EDeliveryType::TopSpinner: return TEXT("Top-spinner");
		case EDeliveryType::Slider: return TEXT("Slider");
		default: return TEXT("Stock");
		}
	}

	// Where a length pitches, in the terms the AI's plans use (metres from the striker's stumps).
	const TCHAR* LengthName(float Length)
	{
		return Length <= 2.5f ? TEXT("YORKER") : Length <= 5.f ? TEXT("FULL") : Length <= 7.5f ? TEXT("GOOD LENGTH")
			: Length <= 9.5f ? TEXT("BACK OF A LENGTH") : TEXT("SHORT");
	}

	/**
	 * Paints in call order: consecutive primitives of one kind share a layer, and a change of kind starts a new one
	 * (Slate draws a layer's boxes, lines and text as separate batches, so sharing layers across kinds would reorder
	 * them). Alpha fades whatever is drawn while it is set.
	 */
	struct FPaint
	{
		const FGeometry& G;
		FSlateWindowElementList& Out;
		int32 Layer;
		int32 Kind = -1;
		float Alpha = 1.f;

		int32 On(int32 K) { if (K != Kind) { ++Layer; Kind = K; } return Layer; }
		FPaintGeometry At(const FVector2D& Pos, const FVector2D& Size) const
		{
			return G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(Pos)));
		}
		FLinearColor A(FLinearColor C) const { C.A *= Alpha; return C; }

		void Box(const FVector2D& Pos, const FVector2D& Size, const FLinearColor& Fill, float Radius = 0.f,
			const FLinearColor& Outline = FLinearColor::Transparent, float Width = 1.f)
		{
			const FSlateBrush B = Rounded(FLinearColor::White, Radius, A(Outline), Width);
			FSlateDrawElement::MakeBox(Out, On(0), At(Pos, Size), &B, ESlateDrawEffect::None, A(Fill));
		}
		void Box(const FBox2D& R, const FLinearColor& Fill, float Radius = 0.f, const FLinearColor& Outline = FLinearColor::Transparent, float Width = 1.f)
		{
			Box(R.Min, R.GetSize(), Fill, Radius, Outline, Width);
		}
		void Image(const FBox2D& R, const TCHAR* Name)
		{
			const FSlateBrush B = Art(Name);
			if (B.DrawAs != ESlateBrushDrawType::NoDrawType)
				FSlateDrawElement::MakeBox(Out, On(3), At(R.Min, R.GetSize()), &B, ESlateDrawEffect::None, A(FLinearColor::White));
		}
		/** A box sheared into a blade about its centre, the frontend's sports signature (text on it stays upright). */
		void Blade(const FBox2D& R, const FLinearColor& Fill, float Radius = 0.f, const FLinearColor& Outline = FLinearColor::Transparent, float Width = 1.f)
		{
			const FSlateBrush B = Rounded(FLinearColor::White, Radius, A(Outline), Width);
			const FPaintGeometry PG = G.ToPaintGeometry(FVector2f(R.GetSize()), FSlateLayoutTransform(FVector2f(R.Min)),
				FSlateRenderTransform(FShear2D::FromShearAngles(FVector2f(-12.f, 0.f))));
			FSlateDrawElement::MakeBox(Out, On(0), PG, &B, ESlateDrawEffect::None, A(Fill));
		}
		/** A box whose top and bottom edges slope by Deg about its centre (the stinger's bands); its sides stay upright. */
		void Slant(const FBox2D& R, const FLinearColor& Fill, float Deg)
		{
			const FSlateBrush B = Rounded(FLinearColor::White, 0.f);
			const FPaintGeometry PG = G.ToPaintGeometry(FVector2f(R.GetSize()), FSlateLayoutTransform(FVector2f(R.Min)),
				FSlateRenderTransform(FShear2D::FromShearAngles(FVector2f(0.f, Deg))));
			FSlateDrawElement::MakeBox(Out, On(0), PG, &B, ESlateDrawEffect::None, A(Fill));
		}
		void Circle(const FVector2D& C, float R, const FLinearColor& Fill, const FLinearColor& Outline = FLinearColor::Transparent, float Width = 1.f)
		{
			Box(C - FVector2D(R), FVector2D(2.f * R), Fill, R, Outline, Width);
		}
		void Lines(TArray<FVector2f> Points, const FLinearColor& C, float Thick)
		{
			FSlateDrawElement::MakeLines(Out, On(1), G.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, A(C), true, Thick);
		}
		void Line(const FVector2D& P0, const FVector2D& P1, const FLinearColor& C, float Thick)
		{
			Lines({ FVector2f(P0), FVector2f(P1) }, C, Thick);
		}
		static FVector2D Measure(const FString& S, const FSlateFontInfo& Font)
		{
			return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(S, Font);
		}
		/** Text with its middle at P.Y, placed at P.X by Align (0 left, 0.5 centre, 1 right). Returns its width. */
		float Text(const FString& S, const FVector2D& P, const FSlateFontInfo& Font, const FLinearColor& C, float Align = 0.f)
		{
			const FVector2D Sz = Measure(S, Font);
			FSlateDrawElement::MakeText(Out, On(2), At(FVector2D(P.X - Align * Sz.X, P.Y - 0.5f * Sz.Y), Sz), S, Font, ESlateDrawEffect::None, A(C));
			return Sz.X;
		}
		/** A pill sized to its text, centred on C. Returns its rectangle. */
		FBox2D Pill(const FString& S, const FVector2D& C, const FSlateFontInfo& Font, const FLinearColor& Fill, const FLinearColor& Ink,
			float PadX, float Height, float Radius, const FLinearColor& Outline = FLinearColor::Transparent)
		{
			const float W = Measure(S, Font).X + 2.f * PadX;
			const FBox2D R(C - FVector2D(0.5f * W, 0.5f * Height), C + FVector2D(0.5f * W, 0.5f * Height));
			Box(R, Fill, Radius, Outline, 1.f);
			Text(S, C, Font, Ink, 0.5f);
			return R;
		}
	};

	/** Everything a section needs: the painter, the game, the screen in local units and the design unit U (1 px at 1080p). */
	struct FMatchFrame
	{
		FPaint& P;
		ASuperOverGameMode& G;
		const ASuperOverHUD* Hud;
		float W, H, U, Aspect;
		CricketTouch::FInsets Safe;
		double Now;

		FBox2D Px(const FBox2D& B) const { return FBox2D(B.Min * H, B.Max * H); }
		FVector2D Px(const FVector2D& V) const { return V * H; }
	};

	bool Pressed(const FMatchFrame& F, const FBox2D& Rect)
	{
		for (const CricketTouch::FFinger& Finger : F.G.TouchFingers)
			if (Rect.IsInside(Finger.Pos)) return true;
		return false;
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Score strip: team and score, what the innings needs, the batters, the bowler, and this over ball by ball.

	enum class EBallKind : uint8 { Dot, Runs, Four, Six, Extra, Wicket };

	EBallKind KindOf(const FString& B)
	{
		if (B.EndsWith(TEXT("W"))) return EBallKind::Wicket;
		if (B.Contains(TEXT("wd")) || B.Contains(TEXT("nb"))) return EBallKind::Extra;
		if (B == TEXT("6")) return EBallKind::Six;
		if (B == TEXT("4")) return EBallKind::Four;
		return B == TEXT(".") ? EBallKind::Dot : EBallKind::Runs;
	}

	/** A ball of the over as a disc: colour for the eye and always a label, so it never relies on colour alone. */
	void BallDisc(FMatchFrame& F, const FVector2D& C, float R, const FString& B)
	{
		FPaint& P = F.P;
		const EBallKind K = KindOf(B);
		const FString Label = K == EBallKind::Dot ? FString(TEXT("•")) : B.ToUpper();
		const float Size = R * (Label.Len() > 2 ? 0.62f : Label.Len() > 1 ? 0.78f : 1.f);
		switch (K)
		{
		case EBallKind::Wicket: P.Circle(C, R, Danger()); P.Text(Label, C, HudFont(Size, EWeight::Black), Ink(), 0.5f); break;
		case EBallKind::Six: P.Circle(C, R, GoldHi()); P.Text(Label, C, HudFont(Size, EWeight::Black), GoldInk(), 0.5f); break;
		case EBallKind::Four: P.Circle(C, R, Teal()); P.Text(Label, C, HudFont(Size, EWeight::Black), GoldInk(), 0.5f); break;
		case EBallKind::Extra: P.Circle(C, R, Glass(0.9f), Amber(), 2.f * F.U); P.Text(Label, C, HudFont(Size, EWeight::Bold), Amber(), 0.5f); break;
		case EBallKind::Dot: P.Circle(C, R, CardHi()); P.Text(Label, C, HudFont(Size, EWeight::Bold), InkDim(), 0.5f); break;
		default: P.Circle(C, R, CardHi(), Hair(), F.U); P.Text(Label, C, HudFont(Size, EWeight::Bold), Ink(), 0.5f); break;
		}
	}

	FLinearColor EventColour(ECricketEvent E)
	{
		switch (E)
		{
		case ECricketEvent::Wicket: return Danger();
		case ECricketEvent::BoundarySix: return GoldHi();
		case ECricketEvent::BoundaryFour: return Teal();
		case ECricketEvent::Wide: case ECricketEvent::NoBall: return Amber();
		default: return Gold();
		}
	}

	void DrawStrip(FMatchFrame& F, int32& SeenBalls, double& NewBallAt)
	{
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const FSuperOverMatch& M = G.Match;
		const FInningsState& In = M.Cur();
		const FCricketTeam& Bat = G.Teams[M.BattingTeam()];
		const FCricketTeam& Bowl = G.Teams[M.BowlingTeam()];
		const float U = F.U;
		const FBox2D R = F.Px(CricketTouch::ScoreStrip(F.Aspect, F.Safe));
		const float X = R.Min.X, Y = R.Min.Y, W = R.GetSize().X, H = R.GetSize().Y;
		const float Wing = FMath::Min(160.f * U, W * 0.09f);
		const float ScoreW = W * 0.28f;
		const float SideW = (W - 2.f * Wing - ScoreW) * 0.5f;
		const float LeftX = X + Wing, ScoreX = LeftX + SideW, RightX = ScoreX + ScoreW;
		const float S = U * FMath::Min(1.f, SideW / (470.f * U));
		const FLinearColor Black = Hex(0x080A10, 0.94f);
		const FLinearColor Orange = Hex(0xEFA151);

		// Broadcast layout: crests at the ends, two batter rows, score, then bowler and this over.
		P.Box(R, Black);
		P.Box(FVector2D(X, Y), FVector2D(Wing, H), Bat.Colour * FLinearColor(1.f, 1.f, 1.f, 0.7f));
		P.Box(FVector2D(R.Max.X - Wing, Y), FVector2D(Wing, H), Bowl.Colour * FLinearColor(1.f, 1.f, 1.f, 0.7f));
		P.Box(FVector2D(LeftX, Y), FVector2D(SideW, H), Hex(0x101018, 0.85f));
		P.Box(FVector2D(RightX, Y), FVector2D(SideW, H), Hex(0x101018, 0.85f));
		P.Blade(FBox2D(FVector2D(ScoreX - 16.f * U, Y), FVector2D(ScoreX + ScoreW + 16.f * U, Y + H)),
			Hex(0x32121D, 0.97f));
		const float Crest = FMath::Min(H * 0.95f, Wing - 8.f * U);
		P.Image(FBox2D(FVector2D(X + (Wing - Crest) * 0.5f, Y + (H - Crest) * 0.5f),
			FVector2D(X + (Wing + Crest) * 0.5f, Y + (H + Crest) * 0.5f)),
			M.BattingTeam() == 0 ? TEXT("T_CRICKET26_Crest_Home") : TEXT("T_CRICKET26_Crest_Away"));
		P.Image(FBox2D(FVector2D(R.Max.X - (Wing + Crest) * 0.5f, Y + (H - Crest) * 0.5f),
			FVector2D(R.Max.X - (Wing - Crest) * 0.5f, Y + (H + Crest) * 0.5f)),
			M.BowlingTeam() == 0 ? TEXT("T_CRICKET26_Crest_Home") : TEXT("T_CRICKET26_Crest_Away"));

		for (int32 Row = 0; Row < 2; ++Row)
		{
			const int32 I = Row == 0 ? In.Striker : In.NonStriker;
			if (!In.Batters.IsValidIndex(I) || !Bat.Batters.IsValidIndex(I)) continue;
			const float Mid = Y + H * (Row == 0 ? 0.27f : 0.72f);
			const FString Name = Bat.Batters[I].Name.ToUpper();
			const FBatterCard& Card = In.Batters[I];
			P.Text((Row == 0 ? TEXT("> ") : TEXT("  ")) + Name,
				FVector2D(LeftX + 22.f * S, Mid), HudFont(22.f * S, EWeight::Bold, 30), Ink());
			P.Text(FString::Printf(TEXT("%d  %d"), Card.Runs, Card.Balls),
				FVector2D(LeftX + SideW - 26.f * S, Mid), HudFont(22.f * S, EWeight::Bold), Ink(), 1.f);
			P.Box(FVector2D(LeftX + 22.f * S, Y + H * (Row == 0 ? 0.43f : 0.89f)),
				FVector2D(SideW - 48.f * S, 3.f * S), Orange);
		}

		const float Centre = ScoreX + 0.5f * ScoreW;
		P.Text(FString::Printf(TEXT("%d-%d"), In.Runs, In.Wickets),
			FVector2D(Centre - 13.f * S, Y + H * 0.37f), HudFont(36.f * S, EWeight::Black), Ink(), 1.f);
		P.Text(FString::Printf(TEXT("%d.%d OVERS"), In.LegalBalls / 6, In.LegalBalls % 6),
			FVector2D(Centre + 13.f * S, Y + H * 0.38f), HudFont(17.f * S, EWeight::Medium), InkDim());
		const float Rate = In.LegalBalls > 0 ? 6.f * In.Runs / In.LegalBalls : 0.f;
		const FString Context = M.IsChase() && M.Phase != EMatchPhase::MatchComplete
			? FString::Printf(TEXT("%d REQUIRED FROM %d"), M.RunsRequired(), M.BallsRemaining())
			: M.Phase == EMatchPhase::MatchComplete
				? (M.bTied ? FString(TEXT("SCORES LEVEL")) : FString::Printf(TEXT("%s WIN"), *G.Teams[M.Winner].Short.ToUpper()))
				: FString::Printf(TEXT("RUN RATE %.2f"), Rate);
		P.Text(Context, FVector2D(Centre, Y + H * 0.76f), HudFont(17.f * S, EWeight::Bold, 40), Ink(), 0.5f);

		P.Text(Bowl.Bowler.Name.ToUpper(), FVector2D(RightX + 30.f * S, Y + H * 0.31f),
			HudFont(22.f * S, EWeight::Bold, 30), Ink());
		P.Text(FString::Printf(TEXT("%d-%d  (%d.%d)"), In.Bowler.Wickets, In.Bowler.Runs,
			In.Bowler.Balls / 6, In.Bowler.Balls % 6),
			FVector2D(RightX + SideW - 24.f * S, Y + H * 0.31f), HudFont(21.f * S, EWeight::Bold), Ink(), 1.f);
		P.Box(FVector2D(RightX + 30.f * S, Y + H * 0.46f), FVector2D(SideW - 54.f * S, 3.f * S), Orange);

		const int32 OverBalls = FMath::Max(0, In.BallLog.Num() - In.OverLogStart);
		const int32 First = FMath::Max(0, OverBalls - 6);
		const int32 Key = int32(HashCombine(GetTypeHash(M.SuperOverNumber),
			HashCombine(GetTypeHash(M.CurrentInnings), GetTypeHash(In.BallLog.Num()))));
		if (SeenBalls >= 0 && Key != SeenBalls && OverBalls > 0) NewBallAt = F.Now;
		SeenBalls = Key;
		for (int32 I = First; I < OverBalls; ++I)
		{
			const FVector2D C(RightX + SideW - (OverBalls - I) * 32.f * S - 12.f * S, Y + H * 0.77f);
			BallDisc(F, C, 12.f * S, In.BallLog[In.OverLogStart + I]);
			if (I == OverBalls - 1)
			{
				const float T = float(F.Now - NewBallAt) / 1.2f;
				if (T >= 0.f && T < 1.f)
				{
					P.Alpha = 1.f - T;
					P.Circle(C, (12.f + 10.f * T) * S, FLinearColor::Transparent, Ink(), 2.f * S);
					P.Alpha = 1.f;
				}
			}
		}
		if (M.bFreeHit)
			P.Pill(TEXT("FREE HIT"), FVector2D(Centre, Y - 15.f * U), HudFont(15.f * U, EWeight::Bold),
				Gold(), GoldInk(), 14.f * U, 30.f * U, 3.f * U);
	}
	// ---------------------------------------------------------------------------------------------------------------
	// Field radar: the ground as the camera behind the bowler sees it (the striker at the top), with everyone where
	// they stand this frame.

	void DrawGround(FMatchFrame& F, const FBox2D& MapUnits, ECricketHand BatHand, bool bBig)
	{
		using namespace CricketGeo;
		FPaint& P = F.P;
		const float U = F.U;
		const FBox2D Map = F.Px(MapUnits);
		const FVector2D C = Map.GetCenter();
		const float R = 0.5f * Map.GetSize().Y, K = R / BoundaryRadius;
		auto To = [&](const FVector2D& Home) { return F.Px(CricketTouch::FieldToMap(MapUnits, Home)); };

		P.Circle(C, R + 6.f * U, Glass(0.82f), Hair(), U);
		P.Circle(C, R, Turf(), Ink() * FLinearColor(1, 1, 1, 0.45f), (bBig ? 2.f : 1.5f) * U);
		// The 30-yard circle: two arcs about the stumps joined along the pitch, a capsule.
		const float Ring = CricketField::InnerRing * K;
		const FVector2D Centre = To(FVector2D(PitchCentre()));
		P.Box(Centre - FVector2D(Ring, Ring + 0.5f * PitchLength * K), FVector2D(2.f * Ring, 2.f * Ring + PitchLength * K), TurfHi(), Ring,
			Ink() * FLinearColor(1, 1, 1, 0.22f), U);
		const float PW = FMath::Max(2.f * PitchHalfWidth * K, 3.f * U);
		P.Box(Centre - FVector2D(0.5f * PW, 0.5f * PitchLength * K), FVector2D(PW, PitchLength * K), Clay(), U);

		// Off and leg as the camera shows them: a right-hander's off side is on the left.
		const float Side = BatHand == ECricketHand::Right ? -1.f : 1.f;
		const FSlateFontInfo Fn = HudFont((bBig ? 15.f : 11.f) * U, EWeight::Bold, 140);
		P.Text(bBig ? TEXT("OFF SIDE") : TEXT("OFF"), C + FVector2D(Side * R * 0.74f, 0.f), Fn, InkDim(), 0.5f);
		P.Text(bBig ? TEXT("LEG SIDE") : TEXT("LEG"), C - FVector2D(Side * R * 0.74f, 0.f), Fn, InkDim(), 0.5f);
	}

	/** A fielder's marker: the keeper square, the bowler a ring, everyone else a dot; shape as well as colour. */
	void Fielder(FPaint& P, const FVector2D& C, float R, const FFielder& Who, const FLinearColor& Fill)
	{
		if (Who.bKeeper) P.Box(C - FVector2D(R), FVector2D(2.f * R), Fill, 0.25f * R, Glass(0.9f), 0.25f * R);
		else if (Who.bBowler) P.Circle(C, R, Glass(0.9f), Fill, 0.45f * R);
		else P.Circle(C, R, Fill, Glass(0.9f), 0.25f * R);
	}

	void DrawRadar(FMatchFrame& F, EMode TM)
	{
		using namespace CricketGeo;
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const float U = F.U;
		const FBox2D Units = CricketTouch::Radar(F.Aspect, F.Safe);
		const FBox2D Map = F.Px(Units);
		const FVector2D C = Map.GetCenter();
		const float R = 0.5f * Map.GetSize().Y;
		const ECricketHand Hand = G.StrikerPlayer().BatHand;
		auto To = [&](const FVector2D& Home) { return F.Px(CricketTouch::FieldToMap(Units, Home)); };
		DrawGround(F, Units, Hand, false);

		const ASuperOverGameMode::FLiveField Live = G.LiveField();
		// The aim of a held pull, from the striker: where the stroke would go.
		if (TM == EMode::Batting && G.bBatPullActive && G.LiveAim.Magnitude > 0.f)
		{
			const float D = FMath::DegreesToRadians(G.LiveAim.DirectionDeg);
			const FVector2D From = To(FVector2D::ZeroVector);
			const FVector2D Dir(-FMath::Sin(D) * OffSideSign(Hand), FMath::Cos(D));
			const FVector2D End = From + Dir * R * (0.3f + 0.65f * G.LiveAim.Magnitude);
			P.Line(From, End, Gold(), 2.5f * U);
			P.Circle(End, 4.f * U, Gold());
		}
		const float Dot = 5.f * U;
		for (int32 I = 0; I < G.Ctx.Field.Num() && I < Live.Field.Num(); ++I)
			Fielder(P, To(Live.Field[I]), Dot, G.Ctx.Field[I], Ink());
		for (const FVector2D& B : { Live.Striker, Live.NonStriker }) P.Circle(To(B), Dot, Gold(), Glass(0.9f), 1.5f * U);
		if (Live.bBall) P.Circle(To(Live.Ball), 3.5f * U, Ink(), Teal(), 1.5f * U);

		// The field is the bowling captain's: a button to set it for the human, a note that it is the AI's otherwise.
		const FVector2D Tab(C.X, Map.Max.Y + 2.f * U);
		const FSlateFontInfo Fn = HudFont(14.f * U, EWeight::Bold, 120);
		if (TM == EMode::Bowling && G.CanEditField())
			P.Pill(TEXT("SET FIELD"), Tab, Fn, Pressed(F, Units) ? GoldHi() : Gold(), GoldInk(), 16.f * U, 34.f * U, 3.f * U);
		else if (G.HumanBowls())
			P.Pill(TEXT("FIELD LOCKED"), Tab, Fn, Glass(0.94f), InkFaint(), 14.f * U, 30.f * U, 3.f * U, Hair());
		else if (G.HumanBats())
			P.Pill(TEXT("AI FIELD"), Tab, Fn, Glass(0.94f), InkDim(), 14.f * U, 30.f * U, 3.f * U, Hair());
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Touch controls. Visible tiles sit inside larger hit rectangles.

	FBox2D Inset(const FBox2D& R, float Share)
	{
		const FVector2D D = R.GetSize() * Share * 0.5f;
		return FBox2D(R.Min + D, R.Max - D);
	}

	/** A square tile centred in a hit rect: its rect in local units. */
	FBox2D Tile(const FMatchFrame& F, const FBox2D& Hit, float Share, bool bDown)
	{
		const FBox2D R = F.Px(Hit);
		const float S = FMath::Min(R.GetSize().X, R.GetSize().Y) * (1.f - Share) * (bDown ? 0.95f : 1.f);
		const FVector2D C = R.GetCenter() + FVector2D(0.f, bDown ? 2.f * F.U : 0.f);
		return FBox2D(C - FVector2D(0.5f * S), C + FVector2D(0.5f * S));
	}

	// Line icons for the three shot modes, drawn about C in a box of S.
	void ShotIcon(FPaint& P, EButton B, const FVector2D& C, float S, const FLinearColor& Col, float Thick)
	{
		auto Pt = [&](float X, float Y) { return FVector2f(C + FVector2D(X, Y) * S); };
		if (B == EButton::Defend)
		{
			// A shield: the block.
			P.Lines({ Pt(-0.4f, -0.45f), Pt(0.4f, -0.45f), Pt(0.4f, 0.05f), Pt(0.f, 0.5f), Pt(-0.4f, 0.05f), Pt(-0.4f, -0.45f) }, Col, Thick);
		}
		else if (B == EButton::Ground)
		{
			// Along the turf: a flat arrow over the ground line.
			P.Lines({ Pt(-0.5f, 0.15f), Pt(0.45f, 0.15f) }, Col, Thick);
			P.Lines({ Pt(0.2f, -0.1f), Pt(0.47f, 0.15f), Pt(0.2f, 0.4f) }, Col, Thick);
			P.Lines({ Pt(-0.5f, 0.5f), Pt(0.5f, 0.5f) }, Col * FLinearColor(1, 1, 1, 0.5f), Thick * 0.7f);
		}
		else
		{
			// In the air: an arc rising over the ground line.
			TArray<FVector2f> Arc;
			for (int32 I = 0; I <= 12; ++I)
			{
				const float T = I / 12.f;
				Arc.Add(Pt(-0.5f + 0.9f * T, 0.4f - 3.2f * T * (1.f - T) * 0.8f + 0.1f * T));
			}
			P.Lines(Arc, Col, Thick);
			P.Lines({ Pt(0.18f, 0.2f), Pt(0.42f, 0.44f), Pt(0.5f, 0.12f) }, Col, Thick);
			P.Lines({ Pt(-0.5f, 0.55f), Pt(0.5f, 0.55f) }, Col * FLinearColor(1, 1, 1, 0.5f), Thick * 0.7f);
		}
	}

	/** A clean, sporty, minimalistic meter slider: athletic track, sporty title & badge readout, glowing thumb. */
	void Slider(FMatchFrame& F, const FBox2D& Hit, const TCHAR* Title, const FString& Value, float Fill, bool bCentred, bool bDown)
	{
		FPaint& P = F.P;
		const float U = F.U;
		const FBox2D R = F.Px(Hit);
		const float TH = 4.f * U;
		const float CY = R.Max.Y - 16.f * U;
		const float HY = R.Min.Y + 16.f * U;

		// Clean, minimalist translucent card backing with fine hairline border
		P.Box(R, Glass(bDown ? 0.90f : 0.82f), 5.f * U, bDown ? Teal() : Hair(), U);

		// Clean header: Title on the left in subtle muted ink, crisp Value on the right
		const FLinearColor HeaderCol = bCentred ? Teal() : Gold();
		P.Text(Title, FVector2D(R.Min.X + 14.f * U, HY), HudFont(10.5f * U, EWeight::Bold, 140), HeaderCol);

		// Value cleanly right-aligned without heavy badge boxes
		const FSlateFontInfo ValFont = HudFont(12.f * U, EWeight::Bold, 40);
		const float ValW = FPaint::Measure(Value, ValFont).X;
		P.Text(Value, FVector2D(R.Max.X - ValW - 14.f * U, HY), ValFont, Ink());

		// Track: slim minimalist groove
		const float X0 = R.Min.X + 14.f * U, X1 = R.Max.X - 14.f * U;
		const FBox2D Track(FVector2D(X0, CY - 0.5f * TH), FVector2D(X1, CY + 0.5f * TH));
		P.Box(Track, Hex(0x06090f, 0.75f), 0.5f * TH);

		// Active filled bar
		const float Level = X0 + FMath::Clamp(Fill, 0.f, 1.f) * (X1 - X0);
		const float From = bCentred ? 0.5f * (X0 + X1) : X0;
		const FLinearColor FillCol = bCentred ? Teal() : Gold();
		P.Box(FVector2D(FMath::Min(Level, From), Track.Min.Y), FVector2D(FMath::Max(FMath::Abs(Level - From), 2.f * U), TH), FillCol, 0.5f * TH);

		if (bCentred)
		{
			// Subtle center zero mark
			P.Box(FVector2D(0.5f * (X0 + X1) - 0.5f * U, CY - 4.f * U), FVector2D(U, 8.f * U), InkFaint());
		}

		// Minimalist, elegant circular thumb puck
		const float ThumbR = (bDown ? 8.5f : 7.f) * U;
		P.Circle(FVector2D(Level, CY), ThumbR + U, FLinearColor::Transparent, FillCol, U);
		P.Circle(FVector2D(Level, CY), ThumbR, Ink());
	}

	void DrawControls(FMatchFrame& F, EMode TM)
	{
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const float U = F.U;
		const TArray<EDeliveryType> Rep = CricketBowling::Repertoire(G.BowlerPlayer().BowlerType);
		const bool bDeadBall = G.DPhase == EDeliveryPhase::DeadBall;
		const bool bBallLive = G.DPhase == EDeliveryPhase::BallInPlay || bDeadBall;
		P.Alpha = 1.f;
		// IPL: the selector sizes itself by the candidate count, not the bowling repertoire.
		const int32 NumOpts = TM == EMode::Pick ? G.IPLAwaitingCandidates.Num() : Rep.Num();
		for (const CricketTouch::FButton& B : CricketTouch::Layout(TM, NumOpts, F.Aspect, F.Safe))
		{
			const bool bDown = Pressed(F, B.Rect);
			switch (B.Button)
			{
			case EButton::Defend: case EButton::Ground: case EButton::Loft:
			{
				const EBatIntent Mode = B.Button == EButton::Defend ? EBatIntent::Defend : B.Button == EButton::Ground ? EBatIntent::Ground : EBatIntent::Loft;
				const bool bOn = G.BatMode == Mode;
				const FBox2D T = Tile(F, B.Rect, 0.1f, bDown);
				const float S = T.GetSize().X;
				P.Box(T, bOn ? (bDown ? GoldHi() : Gold()) : Glass(bDown ? 0.95f : 0.8f), 4.f * U, bOn ? FLinearColor::Transparent : Hair(), U);
				const FLinearColor Col = bOn ? GoldInk() : Ink();
				ShotIcon(P, B.Button, T.GetCenter() - FVector2D(0.f, 0.12f * S), 0.3f * S, Col, 2.5f * U);
				P.Text(B.Button == EButton::Defend ? TEXT("DEFEND") : B.Button == EButton::Ground ? TEXT("GROUND") : TEXT("LOFT"),
					T.GetCenter() + FVector2D(0.f, 0.26f * S), HudFont(0.12f * S, EWeight::Bold, 100), bOn ? GoldInk() : InkDim(), 0.5f);
				break;
			}
			case EButton::Run:
			{
				const FBox2D T = Tile(F, B.Rect, 0.1f, bDown);
				const float S = T.GetSize().X;
				const bool bCalled = G.HumanRunCalls.Go.Num() > 0;
				const float Was = P.Alpha;
				P.Alpha *= bBallLive ? 1.f : 0.5f;
				P.Box(T, bBallLive ? (bDown ? Ink() : Teal()) : Glass(0.8f), 4.f * U, bBallLive ? FLinearColor::Transparent : Hair(), U);
				const FLinearColor Col = bBallLive ? GoldInk() : Ink();
				for (const float DX : { -0.07f, 0.05f })
				{
					const FVector2D C = T.GetCenter() + FVector2D(DX * S, -0.13f * S);
					P.Lines({ FVector2f(C + FVector2D(-0.04f, -0.08f) * S), FVector2f(C + FVector2D(0.04f, 0.f) * S), FVector2f(C + FVector2D(-0.04f, 0.08f) * S) }, Col, 3.f * U);
				}
				P.Text(TEXT("RUN"), T.GetCenter() + FVector2D(0.f, 0.1f * S), HudFont(0.2f * S, EWeight::Black, 80), Col, 0.5f);
				if (bCalled)
					P.Text(FString(CricketControl::RunStateName(G.LastRunState)).ToUpper(), T.GetCenter() + FVector2D(0.f, 0.32f * S), HudFont(0.075f * S, EWeight::Bold, 60), Col, 0.5f);
				P.Alpha = Was;
				break;
			}
			case EButton::Cancel:
			{
				const FBox2D T = Tile(F, B.Rect, 0.1f, bDown);
				const float Was = P.Alpha;
				P.Alpha *= G.HumanRunCalls.Go.Num() > 0 ? 1.f : 0.5f;
				P.Box(T, Glass(bDown ? 0.95f : 0.8f), 4.f * U, Danger(), 2.f * U);
				P.Text(TEXT("CANCEL"), T.GetCenter(), HudFont(0.15f * T.GetSize().X, EWeight::Bold, 80), Ink(), 0.5f);
				P.Alpha = Was;
				break;
			}
			case EButton::Bowl:
			{
				const FBox2D T = Tile(F, B.Rect, 0.06f, bDown);
				const float S = T.GetSize().X;
				const bool bRunUp = G.DPhase == EDeliveryPhase::RunUp;
				const FLinearColor Hue = bRunUp ? Teal() : Gold();
				const FVector2D C = T.GetCenter();

				// Minimalist, modern action button: crisp circle with subtle elegant rim
				P.Circle(C, 0.5f * S, bDown ? Ink() : Hue);
				P.Circle(C, 0.5f * S, FLinearColor::Transparent, bDown ? Hue : Hair(), 1.5f * U);

				P.Text(bRunUp ? TEXT("RELEASE") : TEXT("BOWL"), C - FVector2D(0.f, 0.07f * S), HudFont((bRunUp ? 0.15f : 0.19f) * S, EWeight::Black, 80), GoldInk(), 0.5f);
				P.Text(FString(TypeName(G.HumanPlan.Type)).ToUpper(), C + FVector2D(0.f, 0.20f * S), HudFont(0.075f * S, EWeight::Bold, 80), GoldInk() * FLinearColor(1, 1, 1, 0.85f), 0.5f);
				break;
			}
			case EButton::Play:
			{
				const FBox2D T = Tile(F, B.Rect, 0.1f, bDown);
				const float S = T.GetSize().X;
				P.Box(T, bDown ? Ink() : Gold(), 4.f * U);
				P.Text(TEXT("PLAY"), T.GetCenter() - FVector2D(0.f, 0.05f * S), HudFont(0.19f * S, EWeight::Black, 80), GoldInk(), 0.5f);
				P.Text(TEXT("NEXT BALL"), T.GetCenter() + FVector2D(0.f, 0.22f * S), HudFont(0.07f * S, EWeight::Bold, 80), GoldInk() * FLinearColor(1, 1, 1, 0.8f), 0.5f);
				break;
			}
			case EButton::Delivery:
			{
				const bool bOn = Rep.IsValidIndex(B.Index) && Rep[B.Index] == G.HumanPlan.Type;
				const FBox2D T = F.Px(B.Rect);
				// Minimalist delivery chip: clean, organized segmented pill
				if (bOn)
				{
					// Active: solid clean gold pill, dark bold text
					P.Box(T, Gold(), 4.f * U);
					if (Rep.IsValidIndex(B.Index))
						P.Text(FString(TypeName(Rep[B.Index])).ToUpper(), T.GetCenter(), HudFont(FMath::Min(13.f * U, 0.28f * T.GetSize().Y), EWeight::Black, 60), GoldInk(), 0.5f);
				}
				else
				{
					// Inactive: subtle dark glass chip with clean hairline border
					P.Box(T, Glass(bDown ? 0.90f : 0.75f), 4.f * U, bDown ? Teal() : Hair(), U);
					if (Rep.IsValidIndex(B.Index))
						P.Text(FString(TypeName(Rep[B.Index])).ToUpper(), T.GetCenter(), HudFont(FMath::Min(13.f * U, 0.26f * T.GetSize().Y), EWeight::Bold, 60), InkDim(), 0.5f);
				}
				break;
			}
			case EButton::Effort:
				Slider(F, B.Rect, TEXT("PACE"), FString::Printf(TEXT("%.0f%%"), 100.f * G.HumanPlan.Effort), G.HumanPlan.Effort, false, bDown);
				break;
			case EButton::Dial:
			{
				const float Dial = CricketControl::DialFor(G.HumanPlan, G.StrikerPlayer().BatHand);
				const bool bSwing = CricketControl::IsSwingFamily(G.HumanPlan.Type);
				// Swing is signed (up moves the ball to the right of the screen); anything else is an amount.
				const FString Value = bSwing ? FString::Printf(TEXT("%s %.0f"), Dial >= 0.f ? TEXT("R") : TEXT("L"), 100.f * FMath::Abs(Dial))
					: FString::Printf(TEXT("%.0f%%"), 50.f * (Dial + 1.f));
				Slider(F, B.Rect, bSwing ? TEXT("SWING") : TEXT("SPIN"), Value, 0.5f * (Dial + 1.f), bSwing, bDown);
				break;
			}
			case EButton::Review: case EButton::Accept:
			{
				const bool bReview = B.Button == EButton::Review;
				const FBox2D T = F.Px(Inset(B.Rect, 0.06f));
				P.Box(T, bReview ? (bDown ? GoldHi() : Gold()) : Glass(bDown ? 0.95f : 0.85f), 3.f * U, bReview ? FLinearColor::Transparent : Hair(), U);
				P.Text(bReview ? FString::Printf(TEXT("REVIEW (%d)"), G.Match.ReviewsLeft[G.ReviewingTeam]) : FString(TEXT("ACCEPT")), T.GetCenter(),
					HudFont(FMath::Min(22.f * U, 0.3f * T.GetSize().Y), EWeight::Black, 80), bReview ? GoldInk() : Ink(), 0.5f);
				break;
			}
		case EButton::GuardLeft: case EButton::GuardRight:
		{
			// Crease arrows: quiet glass tiles with a single chevron, gold only while pressed.
			const FBox2D T = Tile(F, B.Rect, 0.1f, bDown);
			const float S = T.GetSize().X;
			P.Box(T, Glass(bDown ? 0.95f : 0.8f), 4.f * U, bDown ? Gold() : Hair(), bDown ? 2.f * U : U);
			const FVector2D C = T.GetCenter();
			const float Dir = B.Button == EButton::GuardLeft ? -1.f : 1.f;
			const FLinearColor Col = bDown ? Gold() : Ink();
			P.Lines({ FVector2f(C + FVector2D(Dir * 0.10f * S, -0.16f * S)), FVector2f(C + FVector2D(-Dir * 0.08f * S, 0.f)),
				FVector2f(C + FVector2D(Dir * 0.10f * S, 0.16f * S)) }, Col, 3.f * U);
			break;
		}
		case EButton::Pick:
		{
			// IPL selector row: number-key hint on the left, the candidate's name beside it.
			const FBox2D T = F.Px(B.Rect);
			const TArray<FString> Names = G.IPLPickNames();
			const FString Label = Names.IsValidIndex(B.Index) ? Names[B.Index] : FString::Printf(TEXT("PICK %d"), B.Index + 1);
			P.Box(T, bDown ? Gold() : Glass(0.92f), 3.f * U, bDown ? FLinearColor::Transparent : Hair(), U);
			const FString Key = B.Index < 9 ? FString::Printf(TEXT("%d"), B.Index + 1) : TEXT("0");
			P.Text(Key, FVector2D(T.Min.X + 12.f * U, T.GetCenter().Y), HudFont(16.f * U, EWeight::Black, 60), Gold(), 0.f);
			P.Text(Label.ToUpper(), FVector2D(T.Min.X + 44.f * U, T.GetCenter().Y), HudFont(16.f * U, EWeight::Bold, 60), Ink(), 0.f);
			break;
		}
			default: break; // the Field button is the radar
			}
		}
		P.Alpha = 1.f;

		// Crease guard readout: a minimal track above the arrows with a centre tick and a gold dot
		// where the striker stands. Label names the guard so it never relies on position alone.
		if (TM == EMode::Batting || TM == EMode::Ready)
		{
			FBox2D GL, GR;
			bool bL = false, bR = false;
			for (const CricketTouch::FButton& Bb : CricketTouch::Layout(TM, Rep.Num(), F.Aspect, F.Safe))
			{
				if (Bb.Button == EButton::GuardLeft) { GL = Bb.Rect; bL = true; }
				else if (Bb.Button == EButton::GuardRight) { GR = Bb.Rect; bR = true; }
			}
			if (bL && bR)
			{
				const FBox2D Units(FVector2D(FMath::Min(GL.Min.X, GR.Min.X), FMath::Min(GL.Min.Y, GR.Min.Y)),
					FVector2D(FMath::Max(GL.Max.X, GR.Max.X), FMath::Max(GL.Max.Y, GR.Max.Y)));
				const FBox2D PxR = F.Px(Units);
				const float TW = PxR.GetSize().X, TX = PxR.GetCenter().X, TY = PxR.Min.Y - 16.f * U;
				P.Box(FVector2D(TX - 0.5f * TW, TY - 3.f * U), FVector2D(TW, 6.f * U), Hex(0x000000, 0.35f), 3.f * U);
				P.Box(FVector2D(TX - 0.5f * U, TY - 6.f * U), FVector2D(U, 12.f * U), InkFaint());
				const float Max = FMath::Max(G.ControlTuning.GuardMax, 0.01f);
				const float Off = OffSideSign(G.StrikerPlayer().BatHand);
				// Screen-left is world +Y: the dot follows the batter on screen, not the batter-relative sign.
				const float DotX = TX - Off * FMath::Clamp(G.StrikerGuard / Max, -1.f, 1.f) * (0.5f * TW - 6.f * U);
				P.Circle(FVector2D(DotX, TY), 6.f * U, Gold(), GoldInk(), U);
				const float A = FMath::Abs(G.StrikerGuard);
				const FString Where = A < 0.03f ? FString(TEXT("MIDDLE"))
					: FString::Printf(TEXT("%.1f %s"), A, G.StrikerGuard > 0.f ? TEXT("OFF") : TEXT("LEG"));
				P.Text(FString::Printf(TEXT("CREASE • %s"), *Where), FVector2D(TX, TY - 16.f * U), HudFont(11.f * U, EWeight::Bold, 140), InkDim(), 0.5f);
			}
		}

		// Pull and release. At rest, a big pad left of centre says where and how; held, a slingshot from
		// where the finger went down: the full-power ring, a band to the finger with an arrow the way the ball will go,
		// and what that stroke is. The pull maps 1:1 to the finger with no smoothing, so it never lags behind it.
		const float Full = G.ControlTuning.PullMax * F.H;
		if (TM == EMode::Batting && !G.bBatPullActive)
		{
			const FVector2D O = F.Px(CricketTouch::GestureZone(TM, F.Aspect, F.Safe).GetCenter());
			const float Breathe = 0.5f + 0.5f * FMath::Sin(3.f * float(F.Now));
			P.Alpha = 0.55f + 0.2f * Breathe;
			P.Circle(O, 0.78f * Full, Glass(0.35f), Ink() * FLinearColor(1, 1, 1, 0.5f), 2.f * U);
			P.Circle(O, 12.f * U, Gold());
			for (int32 I = 0; I < 3; ++I)
			{
				const FVector2D C = O + FVector2D(0.f, (30.f + 20.f * I + 8.f * Breathe) * U);
				P.Lines({ FVector2f(C + FVector2D(-15.f, -7.5f) * U), FVector2f(C + FVector2D(0.f, 7.5f) * U), FVector2f(C + FVector2D(15.f, -7.5f) * U) },
					Gold() * FLinearColor(1, 1, 1, 1.f - 0.3f * I), 3.5f * U);
			}
			P.Text(TEXT("PULL & RELEASE"), O + FVector2D(0.f, 0.78f * Full + 26.f * U), HudFont(16.f * U, EWeight::Bold, 160), Ink(), 0.5f);
			P.Alpha = 1.f;
		}
		if (TM == EMode::Batting && G.bBatPullActive)
		{
			const CricketTouch::FGesture& Gs = G.Gesture();
			for (const CricketTouch::FFinger& Finger : G.TouchFingers)
			{
				if (Finger.Id != Gs.Finger) continue;
				const FVector2D O = F.Px(Gs.Origin), At = F.Px(Finger.Pos);
				const FVector2D Dir = (At - O).GetSafeNormal();
				const float Power = G.LiveAim.Magnitude;
				const FLinearColor Band = Power >= 0.99f ? GoldHi() : Gold();
				P.Circle(O, Full, Glass(0.3f), Ink() * FLinearColor(1, 1, 1, 0.45f), 2.f * U);
				P.Circle(O, FMath::Max(Power * Full, 14.f * U), Gold() * FLinearColor(1, 1, 1, 0.18f), Band, 2.5f * U);
				P.Circle(O, 14.f * U, Ink(), GoldInk(), 2.f * U);
				if (!Dir.IsZero())
				{
					P.Line(O, At, Band, 8.f * U);
					const FVector2D Side(-Dir.Y, Dir.X), Tip = At + Dir * 38.f * U;
					P.Lines({ FVector2f(Tip - Dir * 20.f * U + Side * 16.f * U), FVector2f(Tip), FVector2f(Tip - Dir * 20.f * U - Side * 16.f * U) }, Band, 5.5f * U);
				}
				P.Circle(At, 26.f * U, Band, GoldInk(), 3.f * U);
				const FString What = FString::Printf(TEXT("%s • %s • %d%%"), CricketControl::ZoneName(G.LiveAim.DirectionDeg), CricketControl::PowerBandName(Power),
					FMath::RoundToInt(100.f * Power)).ToUpper();
				P.Pill(What, O - FVector2D(0.f, Full + 34.f * U), HudFont(19.f * U, EWeight::Bold, 80), Glass(0.92f), Gold(), 16.f * U, 42.f * U, 3.f * U, Hair());
			}
		}
	}

	/**
	 * IPL selector header: what is being picked, over the candidate column, in the broadcast voice.
	 */
	void DrawIPLPickTitle(FMatchFrame& F)
	{
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const float U = F.U;
		const TArray<CricketTouch::FButton> Col = CricketTouch::Layout(EMode::Pick, G.IPLAwaitingCandidates.Num(), F.Aspect, F.Safe);
		if (Col.Num() == 0) return;
		FBox2D All = Col[0].Rect;
		for (int32 I = 1; I < Col.Num(); ++I) All += Col[I].Rect;
		const FBox2D Px = F.Px(All);
		P.Pill(G.IPLPickTitle(), FVector2D(Px.GetCenter().X, Px.Min.Y - 34.f * U),
			HudFont(19.f * U, EWeight::Black, 80), Gold(), GoldInk(), 16.f * U, 44.f * U, 3.f * U, FLinearColor::Transparent);
		P.Text(TEXT("TAP A PLAYER  •  OR PRESS 1-9, 0"), FVector2D(Px.GetCenter().X, Px.Max.Y + 20.f * U),
			HudFont(13.f * U, EWeight::Bold, 140), InkDim(), 0.5f);
	}

	/**
	 * Where the bowled ball will pitch, for a human batter: a ring flat on the pitch (each point projected, so it
	 * lies in perspective), shown after a difficulty-set delay and gone once the ball lands.
	 */
	void DrawPitchMarker(FMatchFrame& F, const FGeometry& Geometry)
	{
		ASuperOverGameMode& G = F.G;
		// The clock runs from the start of the run-up.
		const float Since = G.DPhase == EDeliveryPhase::RunUp ? G.PhaseTime : G.ReleaseAt() + G.PhaseTime;
		const float Alpha = CricketControl::PitchMarkerAlpha(G.ControlTuning, int32(G.Difficulty), Since, G.PitchPreviewAt);
		APlayerController* PC = G.GetWorld()->GetFirstPlayerController();
		if (Alpha <= 0.f || !PC) return;
		const float Radius = G.ControlTuning.PitchMarkerRadius[FMath::Clamp(int32(G.Difficulty), 0, 3)];
		const float Scale = FMath::Max(Geometry.Scale, KINDA_SMALL_NUMBER);
		auto Ring = [&](float R, TArray<FVector2f>& Out)
		{
			constexpr int32 N = 24;
			for (int32 I = 0; I <= N; ++I)
			{
				const float A = 2.f * PI * I / N;
				FVector2D S;
				const FVector M(G.PitchPreview.X + R * FMath::Cos(A), G.PitchPreview.Y + R * FMath::Sin(A), 0.01f);
				if (!PC->ProjectWorldLocationToScreen(M * 100.f, S, true)) return false;
				Out.Add(FVector2f(S / Scale));
			}
			return true;
		};
		TArray<FVector2f> Outer, Inner;
		if (!Ring(Radius, Outer) || !Ring(0.45f * Radius, Inner)) return;
		FPaint& P = F.P;
		const float Pulse = 0.85f + 0.15f * FMath::Sin(8.f * Since);
		P.Alpha = Alpha * Pulse;
		P.Lines(MoveTemp(Outer), Ink() * FLinearColor(1, 1, 1, 0.9f), 2.f * F.U);
		P.Lines(MoveTemp(Inner), Gold() * FLinearColor(1, 1, 1, 0.5f), 1.5f * F.U);
		P.Alpha = 1.f;
	}

	/** The pitch target where the bowler is aiming, over the world, with its length in words. */
	void DrawReticle(FMatchFrame& F, const FGeometry& Geometry)
	{
		ASuperOverGameMode& G = F.G;
		const FVector World = G.TargetMarkerLocation();
		APlayerController* PC = G.GetWorld()->GetFirstPlayerController();
		FVector2D Screen;
		if (World.IsZero() || !PC || !PC->ProjectWorldLocationToScreen(World, Screen, true)) return;
		FPaint& P = F.P;
		const float U = F.U;
		const FVector2D C = Screen / FMath::Max(Geometry.Scale, KINDA_SMALL_NUMBER);
		const bool bLocked = G.DPhase == EDeliveryPhase::RunUp;
		const float R = 24.f * U;

		// Minimalist, clean pitch target: single crisp ring, center dot, subtle tick marks
		P.Circle(C, R, FLinearColor::Transparent, Gold(), 1.5f * U);
		P.Circle(C, 2.5f * U, Gold());

		// Clean delicate crosshair ticks
		for (const FVector2D& D : { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) })
			P.Line(C + D * (R + 2.f * U), C + D * (R + 7.f * U), Gold() * FLinearColor(1, 1, 1, 0.8f), 1.5f * U);

		// Minimalist floating length badge
		const FString What = FString::Printf(TEXT("%s  •  %.1f M"), LengthName(G.HumanPlan.Length), G.HumanPlan.Length);
		const FSlateFontInfo Fn = HudFont(11.5f * U, EWeight::Bold, 60);
		const float PW = FPaint::Measure(What, Fn).X + 16.f * U;
		P.Pill(What, C + FVector2D(R + 14.f * U + 0.5f * PW, 0.f), Fn, Hex(0x06090f, 0.85f), Gold(), 10.f * U, 22.f * U, 2.f * U, Hair());
		P.Alpha = 1.f;
	}

	/** A timing track: a zone either side of the ideal, the needle, the grade over it, and words at the ends. */
	void Meter(FMatchFrame& F, const FVector2D& C, float Width, float Good, float Perfect, float Needle, const FString& Grade,
		const FLinearColor& GradeCol, float NoBallFrom = -1.f)
	{
		FPaint& P = F.P;
		const float U = F.U, TH = 14.f * U, X0 = C.X - 0.5f * Width;
		auto At = [&](float V) { return X0 + Width * 0.5f * (FMath::Clamp(V, -1.f, 1.f) + 1.f); };
		P.Box(FVector2D(X0 - 6.f * U, C.Y - 0.5f * TH - 6.f * U), FVector2D(Width + 12.f * U, TH + 12.f * U), Glass(0.9f), 4.f * U, Hair(), U);
		P.Box(FVector2D(X0, C.Y - 0.5f * TH), FVector2D(Width, TH), Hex(0x000000, 0.4f), 2.f * U);
		P.Box(FVector2D(At(-Good), C.Y - 0.5f * TH), FVector2D(At(Good) - At(-Good), TH), Teal() * FLinearColor(1, 1, 1, 0.45f), 2.f * U);
		P.Box(FVector2D(At(-Perfect), C.Y - 0.5f * TH), FVector2D(At(Perfect) - At(-Perfect), TH), Teal(), 2.f * U);
		if (NoBallFrom > 0.f)
		{
			P.Box(FVector2D(At(NoBallFrom), C.Y - 0.5f * TH), FVector2D(At(1.f) - At(NoBallFrom), TH), Danger(), 2.f * U);
			P.Text(TEXT("NB"), FVector2D(0.5f * (At(NoBallFrom) + At(1.f)), C.Y + TH + 6.f * U), HudFont(11.f * U, EWeight::Bold, 100), Danger(), 0.5f);
		}
		P.Box(FVector2D(At(Needle) - 2.5f * U, C.Y - 17.f * U), FVector2D(5.f * U, 34.f * U), Ink(), 2.f * U, Glass(1.f), U);
		const FSlateFontInfo End = HudFont(11.f * U, EWeight::Bold, 140);
		P.Text(TEXT("EARLY"), FVector2D(X0, C.Y - 26.f * U), End, InkFaint());
		P.Text(TEXT("LATE"), FVector2D(X0 + Width, C.Y - 26.f * U), End, InkFaint(), 1.f);
		P.Text(Grade, FVector2D(C.X, C.Y - 34.f * U), HudFont(20.f * U, EWeight::Black, 80), GradeCol, 0.5f);
	}

	// ---------------------------------------------------------------------------------------------------------------
	// The captain's field editor: the ground large, the outfielders draggable, the rules counted beside it.

	void DrawFieldEditor(FMatchFrame& F)
	{
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const float U = F.U;
		const ECricketHand Hand = G.StrikerPlayer().BatHand;
		P.Box(FVector2D::ZeroVector, FVector2D(F.W, F.H), Scrim() * FLinearColor(1, 1, 1, 0.85f));

		const FBox2D Units = CricketTouch::FieldMap(F.Aspect, F.Safe);
		const FBox2D Map = F.Px(Units);
		auto To = [&](const FVector2D& Home) { return F.Px(CricketTouch::FieldToMap(Units, Home)); };
		DrawGround(F, Units, Hand, true);

		// The field as it would stand with the dragged fielder dropped here, for the counters.
		const bool bDragging = G.EditField.IsValidIndex(G.EditPick);
		TArray<FFielder> Shown = G.EditField;
		if (bDragging) Shown[G.EditPick].Home = G.EditDrag;
		const float R = 11.f * U;
		for (int32 I = 0; I < G.EditField.Num(); ++I)
		{
			const FFielder& Who = G.EditField[I];
			if (I == G.EditPick) { P.Circle(To(Who.Home), R, FLinearColor::Transparent, Ink() * FLinearColor(1, 1, 1, 0.4f), 1.5f * U); continue; }
			const FVector2D C = To(Who.Home);
			Fielder(P, C, R, Who, Who.bBowler ? Gold() : Ink());
			bool bSure = false;
			const FString Name = Who.bKeeper ? FString(TEXT("Keeper")) : Who.bBowler ? FString(TEXT("Bowler")) : CricketField::PositionName(Who.Home, Hand, &bSure);
			if (bSure || Who.bKeeper || Who.bBowler) P.Text(Name, C + FVector2D(0.f, R + 11.f * U), HudFont(13.f * U, EWeight::Medium), InkDim(), 0.5f);
		}
		for (const FVector2D& B : { FVector2D(-0.5f, 0.f), FVector2D(CricketGeo::PitchLength + 0.5f, 0.f) }) P.Circle(To(B), 7.f * U, Gold(), Glass(0.9f), 1.5f * U);
		if (bDragging)
		{
			const bool bOk = G.EditDragWhy.IsEmpty();
			const FVector2D C = To(G.EditDrag);
			P.Circle(C, R * 1.8f, (bOk ? Teal() : Danger()) * FLinearColor(1, 1, 1, 0.25f), bOk ? Teal() : Danger(), 2.f * U);
			P.Circle(C, R, bOk ? Teal() : Danger(), Glass(0.9f), 2.f * U);
			bool bSure = false;
			const FString Name = CricketField::PositionName(G.EditDrag, Hand, &bSure);
			if (bOk && bSure) P.Pill(Name.ToUpper(), C - FVector2D(0.f, R * 1.8f + 22.f * U), HudFont(14.f * U, EWeight::Bold, 80), Glass(0.94f), Teal(), 12.f * U, 30.f * U, 3.f * U);
			if (!bOk) P.Pill(FString::Printf(TEXT("✕  %s"), *G.EditDragWhy), FVector2D(Map.GetCenter().X, Map.Min.Y + 34.f * U), HudFont(16.f * U, EWeight::Bold),
				Glass(0.96f), Danger(), 16.f * U, 38.f * U, 3.f * U, Danger());
		}

		// Beside the map: the title, the presets, the counted rules and the actions.
		const float PX = Map.Max.X + 0.04f * F.H, PW = 0.34f * F.H;
		P.Text(TEXT("SET FIELD"), FVector2D(PX, Map.Min.Y + 24.f * U), HudFont(30.f * U, EWeight::Black, 60), Ink());
		if (G.ShowCoach(EMode::FieldEdit) && G.EditPick == INDEX_NONE)
			P.Pill(TEXT("DRAG A FIELDER TO MOVE THEM"), FVector2D(Map.GetCenter().X, Map.Max.Y + 34.f * U), HudFont(14.f * U, EWeight::Bold, 140),
				Glass(0.8f), InkDim(), 18.f * U, 36.f * U, 3.f * U, Hair());
		int32 Outside = 0, BehindLeg = 0;
		for (const FFielder& Who : Shown)
		{
			if (Who.bKeeper || Who.bBowler) continue;
			Outside += CricketField::IsOutsideCircle(Who.Home);
			BehindLeg += CricketField::IsBehindSquareLeg(Who.Home, Hand);
		}
		// The counted rules in the map's empty top corners, clear of the ground.
		auto Count = [&](float X, float Align, const TCHAR* Label, int32 N, int32 Max)
		{
			const FLinearColor Col = N > Max ? Danger() : N == Max ? Gold() : Ink();
			P.Text(Label, FVector2D(X, Map.Min.Y + 14.f * U), HudFont(12.f * U, EWeight::Bold, 120), InkDim(), Align);
			P.Text(FString::Printf(TEXT("%d / %d%s"), N, Max, N > Max ? TEXT("  OVER") : TEXT("")), FVector2D(X, Map.Min.Y + 38.f * U), HudFont(20.f * U, EWeight::Black), Col, Align);
		};
		Count(Map.Min.X + 6.f * U, 0.f, TEXT("OUTSIDE CIRCLE"), Outside, CricketField::MaxOutside);
		Count(Map.Max.X - 6.f * U, 1.f, TEXT("BEHIND SQUARE LEG"), BehindLeg, CricketField::MaxBehindSquareLeg);
		(void)PW;

		for (const CricketTouch::FButton& B : CricketTouch::Layout(EMode::FieldEdit, 0, F.Aspect, F.Safe))
		{
			const bool bDown = Pressed(F, B.Rect);
			const FBox2D T = F.Px(B.Rect);
			const bool bApply = B.Button == EButton::FieldApply;
			P.Box(T, bApply ? (bDown ? GoldHi() : Gold()) : Glass(bDown ? 0.98f : 0.88f), 3.f * U, bApply ? FLinearColor::Transparent : Hair(), U);
			const FSlateFontInfo Fn = HudFont(18.f * U, EWeight::Bold, 100);
			const FVector2D C = T.GetCenter();
			switch (B.Button)
			{
			case EButton::FieldPreset:
			{
				// The preset now loaded, if the field still matches it, stands out by fill and weight, not colour alone.
				const bool bOn = G.EditPreset == B.Index;
				if (bOn) P.Box(T, Ink(), 3.f * U);
				P.Text(CricketField::PresetName(EFieldPreset(B.Index)), C, HudFont(FMath::Min(15.f * U, 0.3f * T.GetSize().Y), bOn ? EWeight::Black : EWeight::Bold, 60),
					bOn ? GoldInk() : Ink(), 0.5f);
				break;
			}
			case EButton::FieldReset: P.Text(TEXT("RESET"), C, Fn, Ink(), 0.5f); break;
			case EButton::FieldCancel: P.Text(TEXT("CANCEL"), C, Fn, InkDim(), 0.5f); break;
			case EButton::FieldApply: P.Text(TEXT("APPLY FIELD"), C, HudFont(20.f * U, EWeight::Black, 100), GoldInk(), 0.5f); break;
			default: break;
			}
		}

		const float Since = float(F.Now - G.EditErrorAt);
		if (!G.EditError.IsEmpty() && Since < 2.5f && !bDragging)
		{
			P.Alpha = FMath::Clamp((2.5f - Since) / 0.4f, 0.f, 1.f);
			P.Pill(FString::Printf(TEXT("✕  %s"), *G.EditError), FVector2D(Map.GetCenter().X, Map.Max.Y - 34.f * U), HudFont(16.f * U, EWeight::Bold),
				Danger(), Ink(), 18.f * U, 40.f * U, 3.f * U);
			P.Alpha = 1.f;
		}
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Review, replay and result panels.

	/** A centred panel with a titled header row; returns the y under it. */
	float Header(FMatchFrame& F, float X, float Y, float W, const FString& Title, const FLinearColor& Accent)
	{
		FPaint& P = F.P;
		const float U = F.U;
		P.Box(FVector2D(X, Y), FVector2D(W, 40.f * U), Glass(0.94f), 3.f * U, Hair(), U);
		P.Box(FVector2D(X, Y), FVector2D(5.f * U, 40.f * U), Accent, 2.f * U);
		P.Text(Title, FVector2D(X + W * 0.5f, Y + 20.f * U), HudFont(16.f * U, EWeight::Bold, 160), Accent, 0.5f);
		return Y + 44.f * U;
	}

	void Verdict(FMatchFrame& F, float X, float Y, float W, const FString& S, const FLinearColor& Fill, const FLinearColor& InkC)
	{
		F.P.Box(FVector2D(X, Y), FVector2D(W, 62.f * F.U), Fill, 3.f * F.U);
		F.P.Text(S, FVector2D(X + 0.5f * W, Y + 31.f * F.U), HudFont(34.f * F.U, EWeight::Black, 80), InkC, 0.5f);
	}

	void DrawReviews(FMatchFrame& F)
	{
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const float U = F.U, PW = 480.f * U, PX = 0.5f * (F.W - PW), PY = F.Safe.T * F.H + 0.1f * F.H;

		if (G.bAwaitingReview)
		{
			float Y = Header(F, PX, PY, PW, TEXT("LBW APPEAL"), Gold());
			Verdict(F, PX, Y, PW, G.bOnFieldOut ? TEXT("UMPIRE: OUT") : TEXT("UMPIRE: NOT OUT"), G.bOnFieldOut ? Danger() : Teal(), G.bOnFieldOut ? Ink() : GoldInk());
			Y += 66.f * U;
			const FString Ask = G.HumanReviews() ? FString::Printf(TEXT("REVIEW (%d LEFT) OR ACCEPT"), G.Match.ReviewsLeft[G.ReviewingTeam])
				: FString::Printf(TEXT("%s CONSIDERING A REVIEW"), *G.Teams[G.ReviewingTeam].Name.ToUpper());
			P.Box(FVector2D(PX, Y), FVector2D(PW, 38.f * U), Glass(0.94f), 3.f * U);
			P.Text(Ask, FVector2D(PX + 0.5f * PW, Y + 19.f * U), HudFont(15.f * U, EWeight::Bold, 100), InkDim(), 0.5f);
			if (G.HumanReviews())
				P.Box(FVector2D(PX, Y + 36.f * U), FVector2D(PW * FMath::Max(0.f, 1.f - G.PhaseTime / ASuperOverGameMode::ReviewWindow), 2.f * U), Gold());
		}

		const bool bVerdict = G.bReferredThis && G.DPhase == EDeliveryPhase::DeadBall && G.PhaseTime < G.ReplayDelay;
		if (G.bAwaitingThirdUmpire || bVerdict)
		{
			const bool bStumping = G.Result.Running.Attempted == 0, bOut = G.Result.Dismissal != EDismissal::None;
			const float Y = Header(F, PX, PY, PW, bStumping ? TEXT("THIRD UMPIRE • STUMPING") : TEXT("THIRD UMPIRE • RUN OUT"), Gold());
			if (!bVerdict) Verdict(F, PX, Y, PW, TEXT("DECISION PENDING"), Glass(0.94f), Gold());
			else Verdict(F, PX, Y, PW, bOut ? TEXT("OUT") : TEXT("NOT OUT"), bOut ? Danger() : Teal(), bOut ? Ink() : GoldInk());
			if (G.bAwaitingThirdUmpire)
			{
				const FVector2D Tag(F.W - F.Safe.R * F.H - 0.03f * F.H - 100.f * U, F.Safe.T * F.H + 0.03f * F.H + 18.f * U);
				P.Pill(G.ThirdUmpireAngle() == 0 ? TEXT("SIDE-ON") : TEXT("FRONT-ON"), Tag, HudFont(16.f * U, EWeight::Bold, 140), Glass(0.94f), Ink(), 18.f * U, 36.f * U, 3.f * U, Hair());
			}
		}
	}

constexpr float ReplayWipeOpacity(float T)
{
	return T <= -0.45f || T >= 0.4f ? 0.f : T < -0.08f ? (T + 0.45f) / 0.37f : T <= 0.08f ? 1.f : (0.4f - T) / 0.32f;
}
static_assert(ReplayWipeOpacity(-0.45f) == 0.f && ReplayWipeOpacity(0.f) == 1.f && ReplayWipeOpacity(0.4f) == 0.f);

void DrawReplay(FMatchFrame& F)
	{
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const float U = F.U;
		const FString Tag = G.InReel() ? FString::Printf(TEXT("HIGHLIGHTS %d/%d"), G.ReelClip + 1, G.Highlights.Num())
			: G.IsReplaySlowAngle() ? FString(TEXT("SUPER SLOW-MO")) : FString(TEXT("REPLAY"));
		const FSlateFontInfo Fn = HudFont(16.f * U, EWeight::Bold, 160);
		const float TW = FPaint::Measure(Tag, Fn).X + 64.f * U, Right = F.W - F.Safe.R * F.H - 0.03f * F.H, Top = F.Safe.T * F.H + 0.03f * F.H;
		// A live ball's replay is marked by its stingers alone, as the reference's is; the reel keeps its counter.
		if (G.InReel() || !G.Seq.IsValid() || G.bSeqIntro)
		{
			P.Box(FVector2D(Right - TW, Top), FVector2D(TW, 38.f * U), Glass(0.94f), 3.f * U, Hair(), U);
			P.Alpha = 0.6f + 0.4f * FMath::Abs(FMath::Cos(float(F.Now) * 3.f));
			P.Circle(FVector2D(Right - TW + 22.f * U, Top + 19.f * U), 6.f * U, Danger());
			P.Alpha = 1.f;
			P.Text(Tag, FVector2D(Right - TW + 38.f * U, Top + 19.f * U), Fn, Ink());
		}

		// Edge detector under the super slow-motion replay of a ball that passed the bat: the sound trace drawn as
		// the replay plays it, a sharp spike if the bat touched the ball, a dull thud if only the pad did.
		const FDeliveryResult& Heard = G.Result;
		if (!G.IsReplaySlowAngle() || !(Heard.Contact.HasContact() || Heard.bPadImpact)) return;
		const float PW = 460.f * U, PH = 150.f * U, PX = F.Safe.L * F.H + 0.03f * F.H, PY = Top + 70.f * U;
		const float Y = Header(F, PX, PY, PW, TEXT("EDGE DETECTOR"), Gold());
		P.Box(FVector2D(PX, Y), FVector2D(PW, PH - 44.f * U), Glass(0.94f), 3.f * U, Hair(), U);
		const float From = G.ReplayAngleStartTp(G.ReplayAngle()), Span = G.ReplayAngleBallSpan(G.ReplayAngle()), Now = G.ReplayBallTime();
		const float Mid = Y + 0.5f * (PH - 44.f * U), Amp = 0.42f * (PH - 44.f * U);
		constexpr int32 Bars = 160;
		for (int32 I = 0; I < Bars; ++I)
		{
			const float At = From + Span * I / Bars;
			if (At > Now) break;
			const float Noise = 0.04f + 0.04f * FMath::Frac(FMath::Sin(I * 12.9898f) * 43758.5453f);
			const float V = FMath::Max(Noise, CricketDelivery::EdgeSignal(Heard, At));
			P.Box(FVector2D(PX + 14.f * U + (PW - 28.f * U) * I / Bars, Mid - V * Amp), FVector2D(1.6f * U, 2.f * V * Amp), V > 0.2f ? Gold() : Teal());
		}
		if (Now > Heard.ContactTime + 0.05f)
		{
			const bool bBat = Heard.Contact.HasContact();
			P.Pill(bBat ? TEXT("BAT") : TEXT("NO BAT"), FVector2D(PX + PW - 64.f * U, Y + 22.f * U), HudFont(15.f * U, EWeight::Black, 120),
				bBat ? Danger() : Teal(), bBat ? Ink() : GoldInk(), 14.f * U, 30.f * U, 3.f * U);
		}
	}

	void DrawTracking(FMatchFrame& F)
	{
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const float U = F.U;
		const FDeliveryResult& Last = G.Result;
		const FBallTracking& T = Last.Tracking;
		const float Prog = G.ReviewProgress(), PW = 380.f * U, PX = F.Safe.L * F.H + 0.03f * F.H;
		float Y = Header(F, PX, F.Safe.T * F.H + 0.03f * F.H + 70.f * U, PW, TEXT("BALL TRACKING"), Gold());
		auto Side = [](float Line) { return Line > FBallTracking::InLine ? TEXT("OUTSIDE OFF") : Line < -FBallTracking::InLine ? TEXT("OUTSIDE LEG") : TEXT("IN LINE"); };
		// Each call in words and colour: green with it, red against, amber umpire's call.
		auto Row = [&](const TCHAR* Label, const FString& Value, int32 Verdict)
		{
			const FLinearColor Col = Verdict > 0 ? Teal() : Verdict < 0 ? Danger() : Amber();
			P.Box(FVector2D(PX, Y), FVector2D(PW, 46.f * U), Glass(0.94f), 3.f * U, Hair(), U);
			P.Text(Label, FVector2D(PX + 18.f * U, Y + 23.f * U), HudFont(13.f * U, EWeight::Bold, 140), InkDim());
			P.Pill(Value, FVector2D(PX + PW - 100.f * U, Y + 23.f * U), HudFont(15.f * U, EWeight::Black, 60), Col, Verdict < 0 ? Ink() : GoldInk(), 12.f * U, 32.f * U, 3.f * U);
			Y += 50.f * U;
		};
		if (Prog >= 0.25f) Row(TEXT("PITCHING"), Last.PitchTime < 0.f ? FString(TEXT("FULL TOSS")) : FString(Side(T.PitchLine)), T.bPitchedOutsideLeg ? -1 : 1);
		if (Prog >= 0.42f) Row(TEXT("IMPACT"), Side(T.ImpactLine), T.bImpactInLine ? 1 : -1);
		if (Prog >= 0.72f) Row(TEXT("WICKETS"), T.bUmpiresCall ? TEXT("UMPIRE'S CALL") : T.bWouldHit ? TEXT("HITTING") : TEXT("MISSING"), T.bUmpiresCall ? 0 : T.bWouldHit ? 1 : -1);
		if (Prog >= 0.82f)
		{
			const bool bOut = Last.Dismissal == EDismissal::LBW;
			Verdict(F, PX, Y + 6.f * U, PW, bOut ? TEXT("OUT") : TEXT("NOT OUT"), bOut ? Danger() : Teal(), bOut ? Ink() : GoldInk());
			using CricketUmpire::EReview;
			const FString How = !G.bReviewTaken ? FString(TEXT("UMPIRE'S DECISION • NOT REVIEWED"))
				: FString::Printf(TEXT("%s REVIEW: %s"), *G.Teams[G.ReviewingTeam].Short,
					G.ReviewResult == EReview::Overturned ? TEXT("OVERTURNED") : G.ReviewResult == EReview::UmpiresCall ? TEXT("UMPIRE'S CALL, RETAINED") : TEXT("STANDS, REVIEW LOST"));
			P.Text(How, FVector2D(PX + 0.5f * PW, Y + 94.f * U), HudFont(13.f * U, EWeight::Bold, 100), InkDim(), 0.5f);
		}
	}

	void DrawScorecard(FMatchFrame& F)
	{
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const FSuperOverMatch& M = G.Match;
		const float U = F.U, CW = 700.f * U, X0 = 0.5f * (F.W - CW);
		P.Box(FVector2D::ZeroVector, FVector2D(F.W, F.H), Scrim() * FLinearColor(1, 1, 1, 0.6f));
		float Y = F.Safe.T * F.H + 0.12f * F.H;
		const float Top = Y;
		P.Text(M.Phase == EMatchPhase::InningsBreak ? FString(TEXT("INNINGS BREAK")) :
			(M.Rules.MaxLegalBalls == 6 ? FString::Printf(TEXT("SUPER OVER %d • RESULT"), M.SuperOverNumber) : FString(TEXT("MATCH RESULT"))),
			FVector2D(X0, Y), HudFont(15.f * U, EWeight::Bold, 180), Gold());
		Y += 26.f * U;
		for (const FInningsState& Inn : M.Innings)
		{
			const FCricketTeam& T = G.Teams[Inn.BattingTeam];
			P.Box(FVector2D(X0, Y), FVector2D(CW, 56.f * U), Glass(0.95f), 3.f * U, Hair(), U);
			P.Box(FVector2D(X0, Y), FVector2D(6.f * U, 56.f * U), T.Colour, 2.f * U);
			P.Text(T.Name.ToUpper(), FVector2D(X0 + 24.f * U, Y + 28.f * U), HudFont(22.f * U, EWeight::Black, 60), Ink());
			P.Text(FString::Printf(TEXT("%d/%d"), Inn.Runs, Inn.Wickets), FVector2D(X0 + CW - 100.f * U, Y + 28.f * U), HudFont(30.f * U, EWeight::Black), Ink(), 1.f);
			P.Text(FString::Printf(TEXT("(%d.%d)"), Inn.LegalBalls / 6, Inn.LegalBalls % 6), FVector2D(X0 + CW - 24.f * U, Y + 30.f * U), HudFont(17.f * U, EWeight::Medium), InkDim(), 1.f);
			Y += 58.f * U;
			auto Row = [&](const FString& Name, const FString& Figures, const FLinearColor& Col)
			{
				P.Box(FVector2D(X0, Y), FVector2D(CW, 34.f * U), Glass(0.86f), 2.f * U);
				P.Text(Name, FVector2D(X0 + 24.f * U, Y + 17.f * U), HudFont(16.f * U, EWeight::Medium), Col);
				P.Text(Figures, FVector2D(X0 + CW - 24.f * U, Y + 17.f * U), HudFont(16.f * U, EWeight::Bold), Col, 1.f);
				Y += 36.f * U;
			};
			for (int32 I = 0; I < Inn.Batters.Num() && I < T.Batters.Num(); ++I)
			{
				const FBatterCard& C = Inn.Batters[I];
				if (C.Balls == 0 && C.HowOut == EDismissal::None && I != Inn.Striker && I != Inn.NonStriker) continue;
				const FString How = C.HowOut == EDismissal::None ? FString(TEXT(" *")) : TEXT("   ") + CricketHUD::HowOutName(C.HowOut);
				Row(T.Batters[I].Name + How, FString::Printf(TEXT("%d (%d)    4s %d   6s %d"), C.Runs, C.Balls, C.Fours, C.Sixes), Ink());
			}
			const FBowlerCard& B = Inn.Bowler;
			Row(G.Teams[1 - Inn.BattingTeam].Bowler.Name, FString::Printf(TEXT("%d-%d (%d.%d)    wd %d   nb %d"), B.Wickets, B.Runs, B.Balls / 6, B.Balls % 6, B.Wides, B.NoBalls), InkDim());
			Y += 10.f * U;
		}
		const FCricketTeam& Chasing = G.Teams[M.BowlingTeam()];
		const FString Line = M.Phase == EMatchPhase::InningsBreak ? FString::Printf(TEXT("%s NEED %d TO WIN FROM %d BALLS"), *Chasing.Name.ToUpper(), M.Target, M.Rules.MaxLegalBalls)
			: M.bTied ? FString(TEXT("SCORES LEVEL • ANOTHER SUPER OVER")) : FString::Printf(TEXT("%s WIN"), *G.Teams[M.Winner].Name.ToUpper());
		const bool bWon = M.Phase == EMatchPhase::MatchComplete && !M.bTied;
		P.Box(FVector2D(X0, Y), FVector2D(CW, 58.f * U), bWon ? Gold() : Glass(0.96f), 3.f * U, bWon ? FLinearColor::Transparent : Gold(), 1.5f * U);
		P.Text(Line, FVector2D(X0 + 0.5f * CW, Y + 29.f * U), HudFont(22.f * U, EWeight::Black, 80), bWon ? GoldInk() : Gold(), 0.5f);
		Y += 64.f * U;
		// Player of the match, from the cards (runs and wickets, the winners weighted up).
		if (bWon)
		{
			const CricketPresentation::FPlayerImpact Best = CricketPresentation::PlayerOfMatch(M);
			if (G.Teams.IsValidIndex(Best.Team))
			{
				const FCricketTeam& T = G.Teams[Best.Team];
				const FString Who = Best.bBowler ? T.Bowler.Name : T.Batters.IsValidIndex(Best.Batter) ? T.Batters[Best.Batter].Name : FString();
				P.Box(FVector2D(X0, Y), FVector2D(CW, 40.f * U), Glass(0.95f), 3.f * U, Hair(), U);
				P.Box(FVector2D(X0, Y), FVector2D(6.f * U, 40.f * U), T.Colour, 2.f * U);
				P.Text(TEXT("PLAYER OF THE MATCH"), FVector2D(X0 + 24.f * U, Y + 20.f * U), HudFont(13.f * U, EWeight::Bold, 160), Gold());
				P.Text(FString::Printf(TEXT("%s  •  %s"), *Who.ToUpper(), *T.Short.ToUpper()), FVector2D(X0 + CW - 24.f * U, Y + 20.f * U), HudFont(18.f * U, EWeight::Black, 60), Ink(), 1.f);
				Y += 42.f * U;
			}
		}
		Y += 16.f * U;
		P.Pill(M.Phase == EMatchPhase::InningsBreak ? TEXT("TAP TO START THE CHASE") : M.bTied ? TEXT("TAP FOR THE NEXT SUPER OVER") : TEXT("TAP TO PLAY AGAIN"),
			FVector2D(X0 + 0.5f * CW, Y), HudFont(15.f * U, EWeight::Bold, 140), Glass(0.9f), Ink(), 18.f * U, 38.f * U, 3.f * U, Hair());

		// Either side of the card when the screen has room: a wagon wheel of every stroke (seen from behind the
		// striker, a right-hander's off side to the right) and a pitch map of where each ball landed.
		const float PS = 300.f * U, Pad = 28.f * U;
		if (F.W < CW + 2.f * (PS + Pad) + 2.f * (F.Safe.L + 0.03f) * F.H) return;
		const int32 Inn = M.Innings.Num() - 1;
		auto Colour = [](const ASuperOverGameMode::FBallMark& B)
		{
			return B.bWicket ? Danger() : B.Runs >= 6 ? GoldHi() : B.Runs >= 4 ? Teal() : B.Runs > 0 ? Ink() : InkFaint();
		};
		auto Panel = [&](float PX, const TCHAR* Title)
		{
			P.Text(FString::Printf(TEXT("%s • %s"), Title, *G.Teams[M.Innings[Inn].BattingTeam].Short.ToUpper()), FVector2D(PX, Top), HudFont(15.f * U, EWeight::Bold, 180), Gold());
			P.Box(FVector2D(PX, Top + 26.f * U), FVector2D(PS, PS), Glass(0.95f), 3.f * U, Hair(), U);
		};
		const float WX = X0 - Pad - PS;
		Panel(WX, TEXT("WAGON WHEEL"));
		{
			const float K = (PS * 0.5f - 16.f * U) / CricketGeo::BoundaryRadius, CX = WX + PS * 0.5f, CY = Top + 26.f * U + PS * 0.5f;
			auto ToScreen = [&](FVector2D Q) { return FVector2D(CX + Q.Y * K, CY - (Q.X - CricketGeo::PitchLength * 0.5f) * K); };
			P.Circle(FVector2D(CX, CY), CricketGeo::BoundaryRadius * K, Turf(), Hair(), 1.5f * U);
			const FVector2D Striker = ToScreen(FVector2D::ZeroVector), Bowler = ToScreen(FVector2D(CricketGeo::PitchLength, 0.f));
			P.Box(FVector2D(Striker.X - 2.f * U, Bowler.Y), FVector2D(4.f * U, Striker.Y - Bowler.Y), Clay());
			for (const ASuperOverGameMode::FBallMark& B : G.Marks)
				if (B.SuperOver == M.SuperOverNumber && B.Innings == Inn && B.bHit) P.Line(Striker, ToScreen(B.End), Colour(B), 2.f * U);
		}
		const float MX = X0 + CW + Pad;
		Panel(MX, TEXT("PITCH MAP"));
		{
			// The first 12 m of the pitch fill the panel's height: nothing that matters pitches further up.
			const float Len = 12.f, K = (PS - 32.f * U) / Len, CX = MX + PS * 0.5f, T0 = Top + 26.f * U + 16.f * U;
			auto ToScreen = [&](FVector2D Q) { return FVector2D(CX - Q.Y * K, T0 + Q.X * K); };
			const float HW = CricketGeo::PitchHalfWidth * K;
			P.Box(FVector2D(CX - HW, T0), FVector2D(2.f * HW, Len * K), Clay() * FLinearColor(1, 1, 1, 0.9f), 2.f * U);
			P.Box(FVector2D(CX - HW, T0 + CricketGeo::PoppingCrease * K), FVector2D(2.f * HW, 1.5f * U), Ink());
			for (int32 I = -1; I <= 1; ++I) P.Box(FVector2D(CX + I * CricketGeo::StumpsHalfWidth * K - 1.5f * U, T0 - 8.f * U), FVector2D(3.f * U, 11.f * U), Ink());
			for (const ASuperOverGameMode::FBallMark& B : G.Marks)
				if (B.SuperOver == M.SuperOverNumber && B.Innings == Inn && B.bPitched && B.Pitch.X <= Len)
					P.Circle(ToScreen(B.Pitch), 5.f * U, Colour(B), Glass(1.f), 1.5f * U);
		}
	}

	// ---------------------------------------------------------------------------------------------------------------
	// Broadcast sequence graphics (Docs/BROADCAST_SEQUENCE.md): the full-screen stingers round every replay, the live
	// event strip in place of the score strip, and the lower-third cards. Timed by CricketBroadcastSequence; drawn in the
	// CRICKET 26 design system with the project's own crests and logo.

	const TCHAR* CrestOf(int32 Team) { return Team == 0 ? TEXT("T_CRICKET26_Crest_Home") : TEXT("T_CRICKET26_Crest_Away"); }

	FLinearColor TeamColour(const ASuperOverGameMode& G, int32 Team) { return G.Teams.IsValidIndex(Team) ? G.Teams[Team].Colour : Teal(); }

	// The stinger's card: a deep teal from the live palette, its leading edges catching the light.
	FLinearColor StingDeep() { return Hex(0x0C4656); }
	FLinearColor StingEdge() { return Hex(0x1C8FA6); }

	/** The logo, or its wordmark when the texture has not been imported, Height tall centred on C. */
	void Logo(FPaint& P, const FVector2D& C, float Height)
	{
		const FSlateBrush B = Art(TEXT("T_CRICKET26_Logo"));
		if (B.DrawAs != ESlateBrushDrawType::NoDrawType && B.ImageSize.Y > 0.f)
		{
			const float W = Height * B.ImageSize.X / B.ImageSize.Y;
			P.Image(FBox2D(C - FVector2D(0.5f * W, 0.5f * Height), C + FVector2D(0.5f * W, 0.5f * Height)), TEXT("T_CRICKET26_Logo"));
		}
		else P.Text(TEXT("CRICKET 26"), C, HudFont(0.6f * Height, EWeight::Black, 60), Ink(), 0.5f);
	}

	/** The event stinger into a replay and the logo stinger out of it, over everything. */
	void DrawStinger(FMatchFrame& F)
	{
		using namespace CricketSequence;
		ASuperOverGameMode& G = F.G;
		const float ST = G.SeqTime();
		if (ST < 0.f) return;
		float Local = 0.f;
		const EStinger Kind = StingerAt(G.Seq, ST, Local);
		if (Kind == EStinger::None) return;
		const FStingerLook L = StingerLook(Kind, Local);
		FPaint& P = F.P;
		const float W = F.W, H = F.H, U = F.U;
		if (L.Dim > 0.f) P.Box(FVector2D::ZeroVector, FVector2D(W, H), FLinearColor(0.f, 0.f, 0.f, L.Dim));
		if (L.Cover > 0.f)
		{
			// Two bands close from the top and the bottom at a slant, their edges parallel, overlapping once shut.
			const float TopEdge = FMath::Lerp(-0.14f * H, 0.53f * H, L.Cover), BottomEdge = FMath::Lerp(1.14f * H, 0.47f * H, L.Cover);
			P.Slant(FBox2D(FVector2D(-0.1f * W, TopEdge - 0.8f * H), FVector2D(1.1f * W, TopEdge)), StingDeep(), L.TiltDeg);
			P.Slant(FBox2D(FVector2D(-0.1f * W, BottomEdge), FVector2D(1.1f * W, BottomEdge + 0.8f * H)), StingDeep(), L.TiltDeg);
			P.Slant(FBox2D(FVector2D(-0.1f * W, TopEdge - 5.f * U), FVector2D(1.1f * W, TopEdge)), StingEdge(), L.TiltDeg);
			P.Slant(FBox2D(FVector2D(-0.1f * W, BottomEdge), FVector2D(1.1f * W, BottomEdge + 5.f * U)), StingEdge(), L.TiltDeg);
		}
		if (L.CardAlpha > 0.f)
		{
			P.Alpha = L.CardAlpha;
			P.Box(FVector2D::ZeroVector, FVector2D(W, H), StingDeep());
			P.Slant(FBox2D(FVector2D(-0.1f * W, 0.32f * H), FVector2D(1.1f * W, 0.68f * H)), StingEdge() * FLinearColor(1.f, 1.f, 1.f, 0.35f), L.TiltDeg);
			P.Alpha = 1.f;
		}
		if (L.LabelAlpha > 0.f)
		{
			P.Alpha = L.LabelAlpha;
			const FVector2D C(0.5f * W, 0.5f * H);
			const float S = L.LabelScale;
			if (Kind == EStinger::Event)
			{
				const float Crest = 76.f * U * S;
				const FVector2D CrestAt = C - FVector2D(0.f, 100.f * U * S);
				P.Image(FBox2D(CrestAt - FVector2D(0.5f * Crest), CrestAt + FVector2D(0.5f * Crest)), CrestOf(G.EventTeam));
				P.Text(G.EventWord, C + FVector2D(0.f, 14.f * U * S), HudFont(118.f * U * S, EWeight::Black, 50), Ink(), 0.5f);
			}
			else Logo(P, C, 150.f * U * S);
			P.Alpha = 1.f;
		}
	}

	/** WICKET / SIX / FOUR across the bottom in place of the score strip, the word repeated faintly either side. */
	void DrawEventStrip(FMatchFrame& F, float T, float Duration)
	{
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const float U = F.U;
		const FBox2D R = F.Px(CricketTouch::ScoreStrip(F.Aspect, F.Safe));
		const float In = FMath::SmoothStep(0.f, 0.25f, T);
		const float Y = R.Min.Y + (1.f - In) * R.GetSize().Y;
		P.Alpha = 1.f - FMath::SmoothStep(Duration - 0.2f, Duration, T);
		P.Box(FVector2D(R.Min.X, Y), R.GetSize(), Hex(0x080A10, 0.95f));
		P.Box(FVector2D(R.Min.X, Y), R.GetSize(), TeamColour(G, G.EventTeam) * FLinearColor(0.8f, 0.8f, 0.8f, 0.92f));
		P.Box(FVector2D(R.Min.X, Y), FVector2D(R.GetSize().X, 3.f * U), Ink() * FLinearColor(1.f, 1.f, 1.f, 0.5f));
		const FSlateFontInfo Big = HudFont(R.GetSize().Y * 0.6f, EWeight::Black, 60);
		const float WordW = FPaint::Measure(G.EventWord, Big).X + 90.f * U;
		const FVector2D C(R.GetCenter().X, Y + 0.5f * R.GetSize().Y);
		const float Drift = FMath::Fmod(T * 70.f * U, WordW);
		const FLinearColor Faint = Ink() * FLinearColor(1.f, 1.f, 1.f, 0.16f);
		for (int32 K = 1; K <= 5; ++K)
		{
			P.Text(G.EventWord, C + FVector2D(K * WordW + Drift, 0.f), Big, Faint, 0.5f);
			P.Text(G.EventWord, C - FVector2D(K * WordW + Drift, 0.f), Big, Faint, 0.5f);
		}
		P.Text(G.EventWord, C, Big, Ink(), 0.5f);
		P.Alpha = 1.f;
	}

	/**
	 * A lower-third card: the crest in a block on the left, a header bar in the side's colour growing out of it (name
	 * left, detail and figure right), then a light row of labelled numbers.
	 */
	void DrawSeqCard(FMatchFrame& F, const ASuperOverGameMode::FSeqCard& C, const CricketSequence::FCardLook& L, float Lift = 0.f)
	{
		if (L.Alpha <= 0.f || C.Title.IsEmpty()) return;
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const float U = F.U;
		const float CW = FMath::Min(960.f * U, F.W - 2.f * (F.Safe.L + 0.04f) * F.H);
		const float HeadH = 42.f * U, BodyH = C.Labels.Num() ? 58.f * U : 0.f, Block = HeadH + FMath::Max(BodyH, 30.f * U);
		const float X0 = 0.5f * (F.W - CW), Y0 = F.H * (1.f - F.Safe.B) - 0.075f * F.H - Block - Lift;
		const FLinearColor Side = TeamColour(G, C.Team);
		P.Alpha = L.Alpha;
		P.Box(FVector2D(X0, Y0), FVector2D(Block, Block), Hex(0x0B1426, 0.97f));
		P.Box(FVector2D(X0, Y0 + Block - 4.f * U), FVector2D(Block, 4.f * U), Side);
		const float Pad = 10.f * U;
		if (G.Teams.IsValidIndex(C.Team)) P.Image(FBox2D(FVector2D(X0 + Pad, Y0 + Pad), FVector2D(X0 + Block - Pad, Y0 + Block - Pad)), CrestOf(C.Team));
		else Logo(P, FVector2D(X0 + 0.5f * Block, Y0 + 0.5f * Block), 0.45f * Block);
		const float BarX = X0 + Block, BarW = (CW - Block) * L.Bar;
		P.Box(FVector2D(BarX, Y0), FVector2D(BarW, HeadH), Side * FLinearColor(0.82f, 0.82f, 0.82f, 0.97f));
		P.Box(FVector2D(BarX, Y0), FVector2D(BarW, 2.f * U), Ink() * FLinearColor(1.f, 1.f, 1.f, 0.35f));
		if (L.Bar > 0.7f)
		{
			P.Alpha = L.Alpha * FMath::SmoothStep(0.7f, 1.f, L.Bar);
			const float Mid = Y0 + 0.5f * HeadH, Right = BarX + (CW - Block) - 18.f * U;
			P.Text(C.Title, FVector2D(BarX + 18.f * U, Mid), HudFont(21.f * U, EWeight::Black, 50), Ink());
			float RX = Right;
			if (!C.Right.IsEmpty()) RX -= P.Text(C.Right, FVector2D(Right, Mid), HudFont(23.f * U, EWeight::Black), Ink(), 1.f) + 16.f * U;
			if (!C.Subtitle.IsEmpty()) P.Text(C.Subtitle, FVector2D(RX, Mid), HudFont(15.f * U, EWeight::Bold, 40), Ink() * FLinearColor(1.f, 1.f, 1.f, 0.85f), 1.f);
		}
		if (BodyH > 0.f && L.Body > 0.f)
		{
			P.Alpha = L.Alpha * L.Body;
			const float BX = BarX, BW = CW - Block, BY = Y0 + HeadH;
			P.Box(FVector2D(BX, BY), FVector2D(BW, BodyH), Hex(0xF3F5F9, 0.97f));
			const int32 N = C.Labels.Num();
			const float ColW = (BW - 24.f * U) / FMath::Max(N, 1);
			for (int32 I = 0; I < N; ++I)
			{
				const float CX = BX + 12.f * U + (I + 0.5f) * ColW;
				P.Text(C.Labels[I], FVector2D(CX, BY + 17.f * U), HudFont(12.f * U, EWeight::Bold, 80), Hex(0x55607A), 0.5f);
				if (C.Values.IsValidIndex(I)) P.Text(C.Values[I], FVector2D(CX, BY + 40.f * U), HudFont(21.f * U, EWeight::Black), Hex(0x0B1426), 0.5f);
			}
		}
		P.Alpha = 1.f;
	}

	/** The result across the bottom: crest block and one bar in the winner's colour. */
	void DrawResultBar(FMatchFrame& F, const ASuperOverGameMode::FSeqCard& C, const CricketSequence::FCardLook& L)
	{
		if (L.Alpha <= 0.f || C.Title.IsEmpty()) return;
		FPaint& P = F.P;
		const float U = F.U;
		const float CW = FMath::Min(1080.f * U, F.W - 2.f * (F.Safe.L + 0.04f) * F.H), BH = 60.f * U;
		const float X0 = 0.5f * (F.W - CW), Y0 = F.H * (1.f - F.Safe.B) - 0.07f * F.H - BH;
		const FLinearColor Side = TeamColour(F.G, C.Team);
		P.Alpha = L.Alpha;
		P.Box(FVector2D(X0, Y0), FVector2D(BH, BH), Hex(0x0B1426, 0.97f));
		if (F.G.Teams.IsValidIndex(C.Team)) P.Image(FBox2D(FVector2D(X0 + 6.f * U, Y0 + 6.f * U), FVector2D(X0 + BH - 6.f * U, Y0 + BH - 6.f * U)), CrestOf(C.Team));
		P.Box(FVector2D(X0 + BH, Y0), FVector2D((CW - BH) * L.Bar, BH), Side * FLinearColor(0.85f, 0.85f, 0.85f, 0.97f));
		P.Box(FVector2D(X0 + BH, Y0 + BH - 4.f * U), FVector2D((CW - BH) * L.Bar, 4.f * U), Gold());
		P.Alpha = L.Alpha * L.Body;
		P.Text(C.Title, FVector2D(X0 + BH + 26.f * U, Y0 + 0.5f * BH), HudFont(26.f * U, EWeight::Black, 60), Ink());
		P.Alpha = 1.f;
	}

	/** THIS OVER: the over's balls in order, numbered, each under a bar in the colour of the band it pitched in. */
	void DrawThisOver(FMatchFrame& F, const CricketSequence::FCardLook& L)
	{
		ASuperOverGameMode& G = F.G;
		if (L.Alpha <= 0.f || G.ThisOverLog.Num() == 0) return;
		FPaint& P = F.P;
		const float U = F.U;
		const int32 N = G.ThisOverLog.Num();
		const float R = 17.f * U, Step = 52.f * U, PW = FMath::Max(260.f * U, N * Step + 40.f * U), PH = 128.f * U;
		const float X0 = F.Safe.L * F.H + 0.045f * F.H, Y0 = F.H * (1.f - F.Safe.B) - 0.07f * F.H - PH;
		// The bands' colours (BuildSequenceProps), yorker to short.
		auto Band = [](float Len)
		{
			return Len < 0.f ? Hex(0x6A7390) : Len <= 2.5f ? Hex(0xC71F29) : Len <= 5.f ? Hex(0xEB801A) : Len <= 7.5f ? Hex(0xDBCB26) : Len <= 9.5f ? Hex(0x299E4D) : Hex(0x335CD9);
		};
		P.Alpha = L.Alpha;
		P.Box(FVector2D(X0, Y0), FVector2D(PW * L.Bar, 34.f * U), Hex(0xF3F5F9, 0.97f));
		P.Text(TEXT("THIS OVER"), FVector2D(X0 + 16.f * U, Y0 + 17.f * U), HudFont(15.f * U, EWeight::Black, 140), Hex(0x0B1426));
		P.Alpha = L.Alpha * L.Body;
		P.Box(FVector2D(X0, Y0 + 34.f * U), FVector2D(PW, PH - 34.f * U), Glass(0.94f));
		for (int32 I = 0; I < N; ++I)
		{
			const FVector2D C(X0 + 20.f * U + R + I * Step, Y0 + 34.f * U + 36.f * U);
			BallDisc(F, C, R, G.ThisOverLog[I]);
			const float Len = G.ThisOverLengths.IsValidIndex(I) ? G.ThisOverLengths[I] : -1.f;
			P.Box(FVector2D(C.X - R, C.Y + R + 8.f * U), FVector2D(2.f * R, 5.f * U), Band(Len), 2.f * U);
			P.Text(FString::FromInt(I + 1), FVector2D(C.X, C.Y + R + 24.f * U), HudFont(12.f * U, EWeight::Bold), InkDim(), 0.5f);
		}
		P.Alpha = 1.f;
	}

	/** The toss call: BAT and BOWL either side of the coin's countdown, the winner's choice lit once it lands. */
	void DrawTossCall(FMatchFrame& F, float T, float Duration)
	{
		ASuperOverGameMode& G = F.G;
		FPaint& P = F.P;
		const float U = F.U;
		const CricketSequence::FCardLook L = CricketSequence::CardLook(T, Duration);
		if (L.Alpha <= 0.f) return;
		const bool bLanded = T > 4.f;
		const FVector2D C(0.5f * F.W, F.H * (1.f - F.Safe.B) - 0.12f * F.H);
		P.Alpha = L.Alpha;
		const FSlateFontInfo Fn = HudFont(17.f * U, EWeight::Black, 120);
		P.Pill(TEXT("BAT"), C - FVector2D(110.f * U, 0.f), Fn, bLanded && G.bTossChoseBat ? Gold() : Glass(0.92f), bLanded && G.bTossChoseBat ? GoldInk() : Ink(), 26.f * U, 40.f * U, 4.f * U, Hair());
		P.Pill(TEXT("BOWL"), C + FVector2D(110.f * U, 0.f), Fn, bLanded && !G.bTossChoseBat ? Gold() : Glass(0.92f), bLanded && !G.bTossChoseBat ? GoldInk() : Ink(), 26.f * U, 40.f * U, 4.f * U, Hair());
		P.Circle(C, 24.f * U, Ink(), Hair(), U);
		const int32 Count = FMath::Clamp(3 - int32(T / 1.2f), 1, 3);
		P.Text(bLanded ? FString(TEXT("•")) : FString::FromInt(Count), C, HudFont(22.f * U, EWeight::Black), Hex(0x0B1426), 0.5f);
		P.Alpha = 1.f;
	}

	/** The card a beat carries, if any, timed to the beat. */
	void DrawBeatCard(FMatchFrame& F)
	{
		using namespace CricketSequence;
		ASuperOverGameMode& G = F.G;
		const FSegment* Beat = G.SeqSegment();
		if (!Beat) return;
		const float T = G.SeqTime() - Beat->Start;
		const FCardLook L = CardLook(T, Beat->Duration);
		switch (Beat->Card)
		{
		case ECard::EventStrip: DrawEventStrip(F, T, Beat->Duration); break;
		case ECard::Dismissal: DrawSeqCard(F, G.CardDismissal, L); break;
		case ECard::Batter: DrawSeqCard(F, G.CardBatter, L); break;
		case ECard::Bowler: DrawSeqCard(F, G.CardBowler, L); break;
		case ECard::Result: DrawResultBar(F, G.CardResult, L); break;
		case ECard::ThisOver: DrawThisOver(F, L); break;
		case ECard::TossCall: DrawTossCall(F, T, Beat->Duration); break;
		case ECard::TossResult: DrawSeqCard(F, G.CardToss, L); break;
		case ECard::PitchConditions:
		{
			ASuperOverGameMode::FSeqCard Pitch = G.CardToss;
			Pitch.Title = TEXT("PITCH CONDITIONS");
			Pitch.Subtitle.Reset();
			Pitch.Team = -1;
			DrawSeqCard(F, Pitch, L);
			break;
		}
		default: break;
		}
	}
}

void SCricketMatchHUD::Construct(const FArguments& Args, ASuperOverGameMode* InGame, ASuperOverHUD* InHUD)
{
	Game = InGame;
	HUD = InHUD;
	SetVisibility(EVisibility::HitTestInvisible); // the viewport keeps every touch: the game mode reads them
}

void SCricketMatchHUD::AddToGameViewport(ASuperOverGameMode* InGame, ASuperOverHUD* InHUD)
{
	// A fullscreen overlay over the game viewport, under the frontend's pause and result cards (z 50).
	if (GEngine && GEngine->GameViewport)
	{
		// The viewport must own the HUD: wrapped in an SWeakWidget nothing held it and it was freed before its first
		// paint. The viewport's widgets are cleared on map travel.
		GEngine->GameViewport->AddViewportWidgetContent(SNew(SCricketMatchHUD, InGame, InHUD), 20);
		UE_LOG(LogCRICKET26, Display, TEXT("Match HUD added to the viewport"));
	}
	else
	{
		UE_LOG(LogCRICKET26, Warning, TEXT("Match HUD could not attach: no game viewport"));
	}
}

int32 SCricketMatchHUD::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling, FSlateWindowElementList& Out,
	int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	using namespace MatchHudPrivate;
	static bool bEnteredPaint = false;
	if (!bEnteredPaint) { UE_LOG(LogCRICKET26, Display, TEXT("Match HUD OnPaint entered")); bEnteredPaint = true; }
	ASuperOverGameMode* G = Game.Get();
	if (!G || !G->GetWorld() || G->Match.Innings.Num() == 0) return Layer;
	const FVector2D Size = Geometry.GetLocalSize();
	static bool bPaintedOnce = false;
	if (!bPaintedOnce) { UE_LOG(LogCRICKET26, Display, TEXT("Match HUD painting: %dx%d"), int32(Size.X), int32(Size.Y)); bPaintedOnce = true; }
	if (Size.Y <= 1.f) return Layer;
	FPaint P{ Geometry, Out, Layer };
	FMatchFrame F{ P, *G, HUD.Get(), float(Size.X), float(Size.Y), float(Size.Y) / 1080.f, float(Size.X / Size.Y), G->TouchSafe, G->GetWorld()->GetTimeSeconds() };
	const FSuperOverMatch& M = G->Match;
	const float U = F.U;

	const EMode TM = G->TouchMode();
	const bool bReplay = G->IsReplaying(), bReview = G->IsReviewing(), bCard = G->ShowingScorecard();
	const bool bThird = G->bAwaitingThirdUmpire || (G->bReferredThis && G->DPhase == EDeliveryPhase::DeadBall && G->PhaseTime < G->ReplayDelay);
	// The broadcast sequence: close-ups and scene shots carry no score strip (only their card), and neither does the
	// live event strip's moment, which takes the strip's place; the stingers cover everything.
	const CricketSequence::FSegment* Beat = G->SeqSegment();
	float StingLocal = 0.f;
	const bool bStinger = G->SeqTime() >= 0.f && CricketSequence::StingerAt(G->Seq, G->SeqTime(), StingLocal) != CricketSequence::EStinger::None;
	const bool bScene = G->InSequenceScene() || G->IntroPlaying() || bStinger || (Beat && Beat->Card == CricketSequence::ECard::EventStrip);
	const bool bSequenceBall = G->Seq.IsValid() && !G->bSeqIntro && G->DPhase == EDeliveryPhase::DeadBall;
	const bool bBroadcast = !bReplay && !bReview && !bCard && TM != EMode::FieldEdit && !bScene;
	const bool bInPlay = M.Phase != EMatchPhase::InningsBreak && M.Phase != EMatchPhase::MatchComplete;
	const FVector2D TopMid(0.5f * F.W, F.Safe.T * F.H + 0.03f * F.H + 20.f * U);

	if (bBroadcast)
	{
		DrawStrip(F, SeenBalls, NewBallAt);
		if (bInPlay && !bThird && !G->bAwaitingReview && G->DPhase != EDeliveryPhase::DeadBall) DrawRadar(F, TM);

		// Top centre: first-use coaching for the controls in play (until each has been used a few times), the speed
		// gun once the ball is bowled.
		const FSlateFontInfo Hint = HudFont(14.f * U, EWeight::Bold, 140);
		FString Say;
		if (G->bAutoPlay) Say = G->DPhase <= EDeliveryPhase::RunUp ? TEXT("AI VS AI") : TEXT("");
		else if (!G->ShowCoach(TM)) {}
		else if (TM == EMode::Batting && G->DPhase <= EDeliveryPhase::RunUp) Say = TEXT("PULL BACK AND RELEASE TO PLAY THE SHOT");
		else if (TM == EMode::Ready) Say = TEXT("PICK A SHOT MODE  •  TAP PLAY WHEN YOU ARE READY FOR THE BALL");
		else if (TM == EMode::Bowling) Say = TEXT("DRAG PITCH TARGET  •  SELECT VARIATION  •  TAP BOWL");
		else if (TM == EMode::Release) Say = TEXT("TAP RELEASE AT THE IDEAL POINT");
		else if (TM == EMode::Running) Say = TEXT("TAP RUN  •  AGAIN FOR ANOTHER  •  CANCEL TO SEND THEM BACK");
		const bool bSpeed = (G->DPhase == EDeliveryPhase::BallInPlay || G->DPhase == EDeliveryPhase::DeadBall) && G->Result.SpeedKph > 0.f && !bThird;
		if (!Say.IsEmpty()) P.Pill(Say, bSpeed ? TopMid + FVector2D(0.f, 50.f * U) : TopMid, Hint, Glass(0.8f), InkDim(), 18.f * U, 36.f * U, 3.f * U, Hair());
		if ((G->DPhase == EDeliveryPhase::BallInPlay || G->DPhase == EDeliveryPhase::DeadBall) && G->Result.SpeedKph > 0.f && !bThird)
		{
			const FString Speed = FString::Printf(TEXT("%.1f KM/H"), G->Result.SpeedKph);
			const FBox2D Chip = P.Pill(Speed, TopMid, HudFont(20.f * U, EWeight::Black, 60), Glass(0.9f), Ink(), 22.f * U, 40.f * U, 3.f * U, Hair());
			P.Box(Chip.Min, FVector2D(5.f * U, Chip.GetSize().Y), G->Teams[M.BowlingTeam()].Colour, 2.f * U);
		}

		// No player cards under the pause button: the score strip already names the batters and the bowler.
		if (G->HumanBowls() && (G->DPhase == EDeliveryPhase::Waiting || G->DPhase == EDeliveryPhase::RunUp) && bInPlay) DrawReticle(F, Geometry);
		if (G->HumanBats() && (G->DPhase == EDeliveryPhase::RunUp || G->DPhase == EDeliveryPhase::BallInPlay)) DrawPitchMarker(F, Geometry);

		// The release meter, over the score strip: timing zones from the control tuning, a no-ball past 0.85.
		const FBox2D Strip = F.Px(CricketTouch::ScoreStrip(F.Aspect, F.Safe));
		if (G->HumanBowls() && G->DPhase == EDeliveryPhase::RunUp)
		{
			const EReleaseGrade Grade = CricketControl::GradeRelease(G->Meter, G->ControlTuning);
			const FLinearColor Col = Grade == EReleaseGrade::Perfect ? Teal() : Grade == EReleaseGrade::Good ? Ink() : Grade == EReleaseGrade::NoBall ? Danger() : Amber();
			Meter(F, FVector2D(Strip.GetCenter().X, Strip.Min.Y - 58.f * U), FMath::Min(560.f * U, Strip.GetSize().X - 40.f * U), G->ControlTuning.GoodRelease,
				G->ControlTuning.PerfectRelease, G->Meter, FString(CricketControl::ReleaseGradeName(Grade)).ToUpper(), Col, 0.85f);
		}

		// Timing after a stroke: a small card in the top-right corner (the radar's place, hidden while the ball is
		// dead) until the next ball, so it can be read without covering the play or the result.
		const FDeliveryResult& Last = G->Result;
		if (G->bTimingFeedback && Last.Contact.Shot != EShotType::Leave && G->DPhase == EDeliveryPhase::DeadBall && !bThird && !G->bAwaitingReview)
		{
			using namespace CricketDelivery;
			constexpr float Span = 0.12f; // s either side of ideal: the swing misses beyond it
			const float T = Last.Contact.TimingError;
			const FLinearColor Col = FMath::Abs(T) <= PerfectTiming ? Teal() : FMath::Abs(T) <= GoodTiming ? Ink() : Amber();
			const FBox2D Corner = F.Px(CricketTouch::Radar(F.Aspect, F.Safe));
			const float W = 230.f * U, H = 58.f * U, X0 = Corner.Max.X - W, Y0 = Corner.Min.Y;
			P.Box(FVector2D(X0, Y0), FVector2D(W, H), Glass(0.85f), 3.f * U, Hair(), U);
			P.Box(FVector2D(X0, Y0), FVector2D(4.f * U, H), Col, 2.f * U);
			P.Text(TimingName(T).ToUpper(), FVector2D(X0 + 14.f * U, Y0 + 18.f * U), HudFont(15.f * U, EWeight::Black, 80), Col);
			P.Text(FString::Printf(TEXT("%s • %s"), *ShotName(Last.Shot.Shot), *ZoneName(Last.Contact.Zone)).ToUpper(), FVector2D(X0 + 14.f * U, Y0 + 36.f * U),
				HudFont(10.f * U, EWeight::Bold, 60), InkDim());
			// The window as a thin track: perfect band, good band, and where this stroke landed.
			const float TX = X0 + 14.f * U, TW = W - 28.f * U, TY = Y0 + H - 11.f * U;
			auto At = [&](float V) { return TX + TW * 0.5f * (FMath::Clamp(V, -1.f, 1.f) + 1.f); };
			P.Box(FVector2D(TX, TY), FVector2D(TW, 4.f * U), Hex(0x000000, 0.4f), 2.f * U);
			P.Box(FVector2D(At(-GoodTiming / Span), TY), FVector2D(At(GoodTiming / Span) - At(-GoodTiming / Span), 4.f * U), Teal() * FLinearColor(1, 1, 1, 0.45f), 2.f * U);
			P.Box(FVector2D(At(-PerfectTiming / Span), TY), FVector2D(At(PerfectTiming / Span) - At(-PerfectTiming / Span), 4.f * U), Teal(), 2.f * U);
			P.Box(FVector2D(At(T / Span) - 1.5f * U, TY - 4.f * U), FVector2D(3.f * U, 12.f * U), Ink(), U);
		}

		// Commentary subtitles removed per user request (audio commentary only).

		if (TM == EMode::Batting || TM == EMode::Ready || TM == EMode::Running || TM == EMode::Bowling || TM == EMode::Release || TM == EMode::Review || TM == EMode::Pick) DrawControls(F, TM);
		if (TM == EMode::Pick) DrawIPLPickTitle(F);
	}
	if (TM == EMode::FieldEdit) DrawFieldEditor(F);
	if (bReplay) DrawReplay(F);
	if (bReview) DrawTracking(F);
	if (!bReplay) DrawReviews(F);
	if (bCard) DrawScorecard(F);

	// Event banner, until the next ball is on its way: in from below, out with a fade.
	const ASuperOverHUD* Hud = HUD.Get();
	const float Since = Hud ? float(F.Now - Hud->BannerAt) : 100.f;
	// (A ball with a broadcast sequence says it with the umpire, the event strip and the stingers instead.)
	if (Since < 2.2f && !bReplay && !bReview && !bCard && !bSequenceBall && !G->IntroPlaying() && G->DPhase != EDeliveryPhase::RunUp && G->DPhase != EDeliveryPhase::BallInPlay)
	{
		const float In = FMath::Clamp(Since / 0.18f, 0.f, 1.f);
		P.Alpha = FMath::Min(In, FMath::Clamp((2.2f - Since) / 0.35f, 0.f, 1.f));
		const FSlateFontInfo Fn = HudFont(76.f * U, EWeight::Black, 80);
		const float TW = FPaint::Measure(Hud->Banner, Fn).X, BW = TW + 140.f * U, BH = 124.f * U;
		const FVector2D C(0.5f * F.W, 0.36f * F.H + (1.f - In) * 18.f * U);
		const FLinearColor Accent = EventColour(Hud->BannerEvent);
		P.Box(C - FVector2D(0.5f * BW, 0.5f * BH), FVector2D(BW, BH), Glass(0.92f), 4.f * U, Hair(), U);
		P.Box(C - FVector2D(0.5f * BW, 0.5f * BH), FVector2D(8.f * U, BH), Accent, 2.f * U);
		P.Box(C + FVector2D(-0.5f * TW, 0.5f * BH - 22.f * U), FVector2D(TW * In, 4.f * U), Accent, 2.f * U);
		P.Text(Hud->Banner, C - FVector2D(0.f, 4.f * U), Fn, Ink(), 0.5f);
		P.Alpha = 1.f;
	}
	// The highlights reel's angle cuts land under a small lower-third stinger (a live ball's replay has the broadcast
	// sequence's full-screen stingers instead).
	if (G->bReplayThis && G->DPhase == EDeliveryPhase::DeadBall && !bSequenceBall)
	{
		const int32 Angle = G->ReplayAngle();
		float Start = 0.f;
		for (int32 I = 0; I < Angle; ++I) Start += G->ReplayAngleDuration(I);
		const float T = G->PhaseTime - G->ReplayDelay;
		const float FromStart = T - Start;
		const float FromEnd = T - Start - G->ReplayAngleDuration(Angle);
		const float CutTime = FMath::Abs(FromStart) < FMath::Abs(FromEnd) ? FromStart : FromEnd;
		if (const float Opacity = ReplayWipeOpacity(CutTime); Opacity > 0.f)
		{
			P.Alpha = FMath::SmoothStep(0.f, 1.f, Opacity);
			// In names the event, as the reference's FOUR stinger does; every later cut carries the logo.
			const bool bEntry = Angle == 0 && CutTime == FromStart && !G->InReel() && G->ReplayEvent() != EReplayEventType::None;
			const FString StingLabel = bEntry ? FString(CricketBroadcast::ReplayEventName(G->ReplayEvent())).Replace(TEXT("-"), TEXT(" ")) : FString(TEXT("CRICKET 26"));
			const FSlateFontInfo StingFn = HudFont(30.f * U, EWeight::Black, 70);
			const float StingW = FPaint::Measure(StingLabel, StingFn).X + 72.f * U, StingH = 56.f * U;
			const FBox2D Strip = F.Px(CricketTouch::ScoreStrip(F.Aspect, F.Safe));
			const FVector2D StingC(0.5f * F.W, Strip.Min.Y - 46.f * U);
			P.Box(StingC - 0.5f * FVector2D(StingW, StingH), FVector2D(StingW, StingH), Glass(0.94f), 4.f * U, Hair(), U);
			P.Box(StingC - 0.5f * FVector2D(StingW, 5.f * U), FVector2D(StingW, 5.f * U), Gold(), 2.f * U);
			P.Text(StingLabel, StingC, StingFn, Ink(), 0.5f);
			P.Alpha = 1.f;
		}
	}
	// The broadcast sequence: the beat's card, then the stingers over everything.
	if (!bCard && !bReview) DrawBeatCard(F);
	DrawStinger(F);
	return P.Layer;
}
