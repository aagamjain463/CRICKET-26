#include "MatchHUDWidget.h"
#include "CRICKET26.h"
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
#include "Widgets/SWeakWidget.h"

namespace CricketHUD
{
	FString HowOutName(EDismissal D);
	FString RoleName(const FCricketPlayer& P, bool bBatting);
}

namespace MatchHudPrivate
{
	using namespace FrontendStyle;
	using CricketTouch::EButton;
	using CricketTouch::EMode;

	// The match HUD's own shades of the frontend palette: one dark glass for every panel, amber for extras.
	FLinearColor Glass(float A = 0.86f) { return Hex(0x0B100D, A); }
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
		const float X0 = R.Min.X, Y0 = R.Min.Y, SW = R.GetSize().X, SH = R.GetSize().Y, Mid = Y0 + 0.5f * SH;

		// Which blocks fit, most important first: the score, this over, the equation, the batters, the bowler.
		const int32 Slots = FMath::Max(In.BallLog.Num() + FMath::Max(0, M.BallsRemaining()), 6);
		const float Step = 44.f * U, OverW = 30.f * U + Step * Slots;
		const float TeamW = 186.f * U, CtxW = 236.f * U, BatW = 270.f * U, BowlW = 190.f * U;
		float Left = SW - TeamW - OverW;
		const bool bCtx = Left >= CtxW; if (bCtx) Left -= CtxW;
		const bool bBat = Left >= BatW; if (bBat) Left -= BatW;
		const bool bBowl = Left >= BowlW; if (bBowl) Left -= BowlW;

		P.Box(R, Glass(0.9f), 4.f * U, Hair(), U);

		// A boundary, a wicket or the result lights the strip's top edge for as long as the banner shows.
		const double Since = F.Hud ? F.Now - F.Hud->BannerAt : 100.0;
		if (Since < 2.2 && F.Hud->BannerPriority >= 6)
		{
			P.Alpha = FMath::Clamp(float(2.2 - Since) / 0.5f, 0.f, 1.f);
			P.Box(FVector2D(X0, Y0), FVector2D(SW, 4.f * U), EventColour(F.Hud->BannerEvent), 2.f * U);
			P.Alpha = 1.f;
		}

		// The batting side.
		float X = X0;
		P.Box(FVector2D(X, Y0), FVector2D(6.f * U, SH), Bat.Colour, 3.f * U);
		P.Text(Bat.Short.ToUpper(), FVector2D(X + 22.f * U, Y0 + 24.f * U), HudFont(17.f * U, EWeight::Bold, 120), InkDim());
		const float ScoreW = P.Text(FString::Printf(TEXT("%d/%d"), In.Runs, In.Wickets), FVector2D(X + 22.f * U, Y0 + 58.f * U), HudFont(40.f * U, EWeight::Black), Ink());
		P.Text(FString::Printf(TEXT("%d.%d"), In.LegalBalls / 6, In.LegalBalls % 6), FVector2D(X + 32.f * U + ScoreW, Y0 + 63.f * U), HudFont(20.f * U, EWeight::Medium), InkDim());
		X += TeamW;
		auto Divider = [&]() { P.Box(FVector2D(X, Y0 + 16.f * U), FVector2D(U, SH - 32.f * U), Hair()); };

		// What the innings needs: the chase equation, or the rate so far.
		if (bCtx)
		{
			Divider();
			const bool bChase = M.IsChase() && M.Phase != EMatchPhase::MatchComplete;
			const float Rate = In.LegalBalls > 0 ? 6.f * In.Runs / In.LegalBalls : 0.f;
			FString Top, Under;
			if (bChase)
			{
				const int32 Balls = M.BallsRemaining();
				Top = Balls == 1 ? FString::Printf(TEXT("%d TO WIN • LAST BALL"), M.RunsRequired())
					: FString::Printf(TEXT("%d TO WIN • %d BALLS"), M.RunsRequired(), Balls);
				Under = Balls > 0 ? FString::Printf(TEXT("TARGET %d   RRR %.1f"), M.Target, 6.f * M.RunsRequired() / Balls) : FString::Printf(TEXT("TARGET %d"), M.Target);
			}
			else if (M.Phase == EMatchPhase::MatchComplete)
			{
				Top = M.bTied ? FString(TEXT("SCORES LEVEL")) : FString::Printf(TEXT("%s WIN"), *G.Teams[M.Winner].Short.ToUpper());
				Under = FString::Printf(TEXT("SUPER OVER %d"), M.SuperOverNumber);
			}
			else
			{
				Top = FString::Printf(TEXT("SUPER OVER • %s"), M.IsChase() ? TEXT("2ND INNINGS") : TEXT("1ST INNINGS"));
				Under = FString::Printf(TEXT("RR %.2f   P'SHIP %d (%d)"), Rate, In.PartnershipRuns, In.PartnershipBalls);
			}
			P.Text(Top, FVector2D(X + 20.f * U, Y0 + 32.f * U), HudFont((bChase ? 21.f : 17.f) * U, EWeight::Bold, 40), bChase ? Gold() : Ink());
			P.Text(Under, FVector2D(X + 20.f * U, Y0 + 62.f * U), HudFont(15.f * U, EWeight::Medium, 60), InkDim());
			X += CtxW;
		}

		// The batters, the striker marked with a bar as well as brighter type.
		if (bBat)
		{
			Divider();
			for (const int32 I : { In.Striker, In.NonStriker })
			{
				if (!In.Batters.IsValidIndex(I) || !Bat.Batters.IsValidIndex(I)) continue;
				const bool bOn = I == In.Striker;
				const float Y = Y0 + (bOn ? 30.f : 63.f) * U;
				if (bOn) P.Box(FVector2D(X + 16.f * U, Y - 10.f * U), FVector2D(4.f * U, 20.f * U), Gold(), U);
				P.Text(Bat.Batters[I].Name, FVector2D(X + 28.f * U, Y), HudFont(18.f * U, bOn ? EWeight::Bold : EWeight::Medium), bOn ? Ink() : InkDim());
				P.Text(FString::Printf(TEXT("%d (%d)"), In.Batters[I].Runs, In.Batters[I].Balls), FVector2D(X + BatW - 18.f * U, Y),
					HudFont(18.f * U, bOn ? EWeight::Bold : EWeight::Medium), bOn ? Ink() : InkDim(), 1.f);
			}
			X += BatW;
		}

		if (bBowl)
		{
			Divider();
			P.Text(Bowl.Bowler.Name, FVector2D(X + 20.f * U, Y0 + 30.f * U), HudFont(18.f * U, EWeight::Bold), Ink());
			P.Text(FString::Printf(TEXT("%d-%d  (%d.%d)"), In.Bowler.Wickets, In.Bowler.Runs, In.Bowler.Balls / 6, In.Bowler.Balls % 6),
				FVector2D(X + 20.f * U, Y0 + 63.f * U), HudFont(18.f * U, EWeight::Medium), InkDim());
			X += BowlW;
		}

		// This over, with a ring for each legal ball still to come.
		X = R.Max.X - OverW;
		Divider();
		P.Text(TEXT("THIS OVER"), FVector2D(X + 20.f * U, Y0 + 20.f * U), HudFont(12.f * U, EWeight::Bold, 140), InkFaint());
		const int32 Key = M.CurrentInnings * 100 + In.BallLog.Num();
		if (SeenBalls >= 0 && Key != SeenBalls && In.BallLog.Num() > 0) NewBallAt = F.Now;
		SeenBalls = Key;
		const float Rd = 17.f * U, CY = Mid + 9.f * U;
		for (int32 I = 0; I < Slots; ++I)
		{
			const FVector2D C(X + 20.f * U + Rd + I * Step, CY);
			if (I >= In.BallLog.Num()) { P.Circle(C, Rd, FLinearColor::Transparent, Hair(), 1.5f * U); continue; }
			BallDisc(F, C, Rd, In.BallLog[I]);
			const float T = float(F.Now - NewBallAt) / 1.2f;
			if (I == In.BallLog.Num() - 1 && T >= 0.f && T < 1.f)
			{
				P.Alpha = 1.f - T;
				P.Circle(C, Rd + 12.f * U * T, FLinearColor::Transparent, Ink(), 2.f * U);
				P.Alpha = 1.f;
			}
		}

		// Above the strip: the free hit, and the last ball of a chase.
		float PillX = X0;
		auto Flag = [&](const TCHAR* S, const FLinearColor& Fill, const FLinearColor& InkC)
		{
			const FSlateFontInfo Fn = HudFont(15.f * U, EWeight::Bold, 100);
			const float PW = FPaint::Measure(S, Fn).X + 28.f * U;
			P.Pill(S, FVector2D(PillX + 0.5f * PW, Y0 - 24.f * U), Fn, Fill, InkC, 14.f * U, 32.f * U, 3.f * U);
			PillX += PW + 8.f * U;
		};
		if (M.bFreeHit) Flag(TEXT("FREE HIT"), Gold(), GoldInk());
		if (M.IsChase() && M.Phase != EMatchPhase::MatchComplete && M.BallsRemaining() == 1) Flag(TEXT("LAST BALL"), Danger(), Ink());
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

	void Slider(FMatchFrame& F, const FBox2D& Hit, const TCHAR* Title, const FString& Value, float Fill, bool bCentred, bool bDown)
	{
		FPaint& P = F.P;
		const float U = F.U;
		const FBox2D R = F.Px(Hit);
		const float TW = 12.f * U, CX = R.GetCenter().X;
		const FBox2D Track(FVector2D(CX - 0.5f * TW, R.Min.Y), FVector2D(CX + 0.5f * TW, R.Max.Y));
		P.Box(Inset(R, 0.f), Glass(bDown ? 0.9f : 0.72f), 4.f * U, bDown ? Teal() : Hair(), U);
		P.Box(Track, Hex(0x000000, 0.35f), 0.5f * TW);
		const float Level = R.Max.Y - FMath::Clamp(Fill, 0.f, 1.f) * R.GetSize().Y;
		const float From = bCentred ? R.GetCenter().Y : R.Max.Y;
		P.Box(FVector2D(Track.Min.X, FMath::Min(Level, From)), FVector2D(TW, FMath::Abs(From - Level)), Teal(), 0.5f * TW);
		if (bCentred) P.Box(FVector2D(R.Min.X + 8.f * U, R.GetCenter().Y - 0.5f * U), FVector2D(R.GetSize().X - 16.f * U, U), InkFaint());
		P.Box(FVector2D(R.Min.X + 6.f * U, Level - 4.f * U), FVector2D(R.GetSize().X - 12.f * U, 8.f * U), Ink(), 3.f * U);
		P.Text(Title, FVector2D(CX, R.Min.Y - 44.f * U), HudFont(12.f * U, EWeight::Bold, 140), InkDim(), 0.5f);
		P.Text(Value, FVector2D(CX, R.Min.Y - 20.f * U), HudFont(19.f * U, EWeight::Black), Ink(), 0.5f);
	}

	void DrawControls(FMatchFrame& F, EMode TM)
	{
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const float U = F.U;
		const TArray<EDeliveryType> Rep = CricketBowling::Repertoire(G.BowlerPlayer().BowlerType);
		const bool bDeadBall = G.DPhase == EDeliveryPhase::DeadBall;
		const bool bBallLive = G.DPhase == EDeliveryPhase::BallInPlay || bDeadBall;
		P.Alpha = TM == EMode::Batting && bDeadBall ? 0.55f : 1.f;
		for (const CricketTouch::FButton& B : CricketTouch::Layout(TM, Rep.Num(), F.Aspect, F.Safe))
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
				const FBox2D T = Tile(F, B.Rect, 0.1f, bDown);
				const float S = T.GetSize().X;
				const bool bRunUp = G.DPhase == EDeliveryPhase::RunUp;
				P.Box(T, bDown ? Ink() : bRunUp ? Teal() : Gold(), 4.f * U);
				P.Text(bRunUp ? TEXT("RELEASE") : TEXT("BOWL"), T.GetCenter() - FVector2D(0.f, 0.05f * S), HudFont((bRunUp ? 0.14f : 0.19f) * S, EWeight::Black, 80), GoldInk(), 0.5f);
				P.Text(FString(TypeName(G.HumanPlan.Type)).ToUpper(), T.GetCenter() + FVector2D(0.f, 0.22f * S), HudFont(0.07f * S, EWeight::Bold, 80), GoldInk() * FLinearColor(1, 1, 1, 0.8f), 0.5f);
				break;
			}
			case EButton::Delivery:
			{
				const bool bOn = Rep.IsValidIndex(B.Index) && Rep[B.Index] == G.HumanPlan.Type;
				const FBox2D T = F.Px(Inset(B.Rect, 0.06f));
				P.Box(T, bOn ? Gold() : Glass(bDown ? 0.95f : 0.8f), 3.f * U, bOn ? FLinearColor::Transparent : Hair(), U);
				if (Rep.IsValidIndex(B.Index))
					P.Text(TypeName(Rep[B.Index]), T.GetCenter(), HudFont(FMath::Min(19.f * U, 0.3f * T.GetSize().Y), EWeight::Bold), bOn ? GoldInk() : Ink(), 0.5f);
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
			default: break; // the Field button is the radar
			}
		}
		P.Alpha = 1.f;

		// The held pull: from where the finger went down to where it is, and what that stroke is.
		if (TM == EMode::Batting && G.bBatPullActive)
		{
			const CricketTouch::FGesture& Gs = G.Gesture();
			for (const CricketTouch::FFinger& Finger : G.TouchFingers)
			{
				if (Finger.Id != Gs.Finger) continue;
				const FVector2D O = F.Px(Gs.Origin), At = F.Px(Finger.Pos);
				P.Circle(O, 16.f * U, FLinearColor::Transparent, Ink() * FLinearColor(1, 1, 1, 0.6f), 2.f * U);
				P.Line(O, At, Gold(), 3.f * U);
				P.Circle(At, 11.f * U, Gold(), GoldInk(), 2.f * U);
				const FString What = FString::Printf(TEXT("%s • %s"), CricketControl::ZoneName(G.LiveAim.DirectionDeg), CricketControl::PowerBandName(G.LiveAim.Magnitude)).ToUpper();
				P.Pill(What, At - FVector2D(0.f, 52.f * U), HudFont(17.f * U, EWeight::Bold, 80), Glass(0.92f), Gold(), 16.f * U, 38.f * U, 3.f * U, Hair());
			}
		}
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
		P.Alpha = bLocked ? 0.6f : 1.f;
		const float R = 24.f * U;
		P.Circle(C, R, FLinearColor::Transparent, Gold(), 2.f * U);
		P.Circle(C, 3.f * U, Gold());
		for (const FVector2D& D : { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) })
			P.Line(C + D * (R + 4.f * U), C + D * (R + 12.f * U), Gold(), 2.f * U);
		const FString What = FString::Printf(TEXT("%s • %.1f M"), LengthName(G.HumanPlan.Length), G.HumanPlan.Length);
		const FSlateFontInfo Fn = HudFont(15.f * U, EWeight::Bold, 80);
		const float PW = FPaint::Measure(What, Fn).X + 28.f * U;
		P.Pill(What, C + FVector2D(R + 24.f * U + 0.5f * PW, 0.f), Fn, Glass(0.9f), Ink(), 14.f * U, 32.f * U, 3.f * U, Hair());
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
		P.Text(TEXT("SET FIELD"), FVector2D(PX, Map.Min.Y + 26.f * U), HudFont(30.f * U, EWeight::Black, 60), Ink());
		P.Text(TEXT("Drag an outfielder. Locks when the run-up starts."), FVector2D(PX, Map.Min.Y + 62.f * U), HudFont(14.f * U, EWeight::Medium), InkDim());
		int32 Outside = 0, BehindLeg = 0;
		for (const FFielder& Who : Shown)
		{
			if (Who.bKeeper || Who.bBowler) continue;
			Outside += CricketField::IsOutsideCircle(Who.Home);
			BehindLeg += CricketField::IsBehindSquareLeg(Who.Home, Hand);
		}
		auto Count = [&](float Y, const TCHAR* Label, int32 N, int32 Max)
		{
			const FLinearColor Col = N > Max ? Danger() : N == Max ? Gold() : Ink();
			P.Text(Label, FVector2D(PX, Y), HudFont(13.f * U, EWeight::Bold, 120), InkDim());
			P.Text(FString::Printf(TEXT("%d / %d%s"), N, Max, N > Max ? TEXT("  OVER") : TEXT("")), FVector2D(PX + PW, Y), HudFont(18.f * U, EWeight::Black), Col, 1.f);
		};
		const float CountY = Units.Min.Y * F.H + (0.1f + 2.f * 0.115f) * F.H + 30.f * U;
		Count(CountY, TEXT("OUTSIDE THE CIRCLE"), Outside, CricketField::MaxOutside);
		Count(CountY + 40.f * U, TEXT("BEHIND SQUARE, LEG SIDE"), BehindLeg, CricketField::MaxBehindSquareLeg);

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
				P.Text(TEXT("PRESET"), FVector2D(T.Min.X + 18.f * U, C.Y - 12.f * U), HudFont(11.f * U, EWeight::Bold, 140), InkFaint());
				P.Text(B.Index == 0 ? TEXT("DEATH OVERS") : TEXT("SPIN, DEFENSIVE"), FVector2D(T.Min.X + 18.f * U, C.Y + 10.f * U), Fn, Ink());
				break;
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

		const bool bVerdict = G.bReferredThis && G.DPhase == EDeliveryPhase::DeadBall && G.PhaseTime < ASuperOverGameMode::ReplayDelay;
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

	void DrawReplay(FMatchFrame& F)
	{
		FPaint& P = F.P;
		ASuperOverGameMode& G = F.G;
		const float U = F.U;
		const FString Tag = G.InReel() ? FString::Printf(TEXT("HIGHLIGHTS %d/%d"), G.ReelClip + 1, G.Highlights.Num())
			: G.IsReplaySlowAngle() ? FString(TEXT("SUPER SLOW-MO")) : FString(TEXT("REPLAY"));
		const FSlateFontInfo Fn = HudFont(16.f * U, EWeight::Bold, 160);
		const float TW = FPaint::Measure(Tag, Fn).X + 64.f * U, Right = F.W - F.Safe.R * F.H - 0.03f * F.H, Top = F.Safe.T * F.H + 0.03f * F.H;
		P.Box(FVector2D(Right - TW, Top), FVector2D(TW, 38.f * U), Glass(0.94f), 3.f * U, Hair(), U);
		P.Alpha = 0.6f + 0.4f * FMath::Abs(FMath::Cos(float(F.Now) * 3.f));
		P.Circle(FVector2D(Right - TW + 22.f * U, Top + 19.f * U), 6.f * U, Danger());
		P.Alpha = 1.f;
		P.Text(Tag, FVector2D(Right - TW + 38.f * U, Top + 19.f * U), Fn, Ink());

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
		P.Text(M.Phase == EMatchPhase::InningsBreak ? FString(TEXT("INNINGS BREAK")) : FString::Printf(TEXT("SUPER OVER %d • RESULT"), M.SuperOverNumber),
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
		const FString Line = M.Phase == EMatchPhase::InningsBreak ? FString::Printf(TEXT("%s NEED %d TO WIN FROM 6 BALLS"), *Chasing.Name.ToUpper(), M.Target)
			: M.bTied ? FString(TEXT("SCORES LEVEL • ANOTHER SUPER OVER")) : FString::Printf(TEXT("%s WIN"), *G.Teams[M.Winner].Name.ToUpper());
		const bool bWon = M.Phase == EMatchPhase::MatchComplete && !M.bTied;
		P.Box(FVector2D(X0, Y), FVector2D(CW, 58.f * U), bWon ? Gold() : Glass(0.96f), 3.f * U, bWon ? FLinearColor::Transparent : Gold(), 1.5f * U);
		P.Text(Line, FVector2D(X0 + 0.5f * CW, Y + 29.f * U), HudFont(22.f * U, EWeight::Black, 80), bWon ? GoldInk() : Gold(), 0.5f);
		Y += 80.f * U;
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
		GEngine->GameViewport->AddViewportWidgetContent(
			SNew(SWeakWidget).PossiblyNullContent(TSharedRef<SCricketMatchHUD>(SNew(SCricketMatchHUD, InGame, InHUD))), 20);
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
	const bool bThird = G->bAwaitingThirdUmpire || (G->bReferredThis && G->DPhase == EDeliveryPhase::DeadBall && G->PhaseTime < ASuperOverGameMode::ReplayDelay);
	const bool bBroadcast = !bReplay && !bReview && !bCard && TM != EMode::FieldEdit;
	const bool bInPlay = M.Phase != EMatchPhase::InningsBreak && M.Phase != EMatchPhase::MatchComplete;
	const FVector2D TopMid(0.5f * F.W, F.Safe.T * F.H + 0.03f * F.H + 20.f * U);

	if (bBroadcast)
	{
		DrawStrip(F, SeenBalls, NewBallAt);
		if (bInPlay && !bThird && !G->bAwaitingReview && G->DPhase != EDeliveryPhase::DeadBall) DrawRadar(F, TM);

		// Top centre: what to do now before the ball, the speed gun after it.
		const FSlateFontInfo Hint = HudFont(14.f * U, EWeight::Bold, 140);
		FString Say;
		if (G->bAutoPlay) Say = TEXT("AI VS AI");
		else if (TM == EMode::Batting && G->DPhase <= EDeliveryPhase::RunUp) Say = TEXT("PULL TO AIM  •  RELEASE TO PLAY");
		else if (TM == EMode::Bowling) Say = G->DPhase == EDeliveryPhase::RunUp ? TEXT("RELEASE IN THE GREEN") : TEXT("DRAG TO SET LINE AND LENGTH  •  TAP BOWL");
		if (!Say.IsEmpty() && G->DPhase <= EDeliveryPhase::RunUp) P.Pill(Say, TopMid, Hint, Glass(0.8f), InkDim(), 18.f * U, 36.f * U, 3.f * U, Hair());
		if ((G->DPhase == EDeliveryPhase::BallInPlay || G->DPhase == EDeliveryPhase::DeadBall) && G->Result.SpeedKph > 0.f && !bThird)
		{
			const FString Speed = FString::Printf(TEXT("%.1f KM/H"), G->Result.SpeedKph);
			const FBox2D Chip = P.Pill(Speed, TopMid, HudFont(20.f * U, EWeight::Black, 60), Glass(0.9f), Ink(), 22.f * U, 40.f * U, 3.f * U, Hair());
			P.Box(Chip.Min, FVector2D(5.f * U, Chip.GetSize().Y), G->Teams[M.BowlingTeam()].Colour, 2.f * U);
		}

		// Player cards: a batter walking in and a bowler starting the over, until the ball is bowled.
		if (G->DPhase == EDeliveryPhase::Waiting && bInPlay)
		{
			const FInningsState& In = M.Cur();
			float CY = F.Safe.T * F.H + 0.03f * F.H + 78.f * U;
			auto Card = [&](const FLinearColor& Colour, const FString& Name, const FString& Role)
			{
				const float CX = F.Safe.L * F.H + 0.03f * F.H;
				P.Box(FVector2D(CX, CY), FVector2D(300.f * U, 70.f * U), Glass(0.9f), 3.f * U, Hair(), U);
				P.Box(FVector2D(CX, CY), FVector2D(5.f * U, 70.f * U), Colour, 2.f * U);
				P.Text(Name.ToUpper(), FVector2D(CX + 20.f * U, CY + 24.f * U), HudFont(20.f * U, EWeight::Black, 60), Ink());
				P.Text(Role, FVector2D(CX + 20.f * U, CY + 50.f * U), HudFont(14.f * U, EWeight::Medium), InkDim());
				CY += 78.f * U;
			};
			const FCricketTeam& Bat = G->Teams[M.BattingTeam()];
			const FCricketTeam& Bowl = G->Teams[M.BowlingTeam()];
			if (In.Batters.IsValidIndex(In.Striker) && In.Batters[In.Striker].Balls == 0)
				Card(Bat.Colour, Bat.Batters[In.Striker].Name, CricketHUD::RoleName(Bat.Batters[In.Striker], true));
			if (In.Bowler.Balls == 0 && In.Deliveries == 0) Card(Bowl.Colour, Bowl.Bowler.Name, CricketHUD::RoleName(Bowl.Bowler, false));
		}

		if (G->HumanBowls() && (G->DPhase == EDeliveryPhase::Waiting || G->DPhase == EDeliveryPhase::RunUp) && bInPlay) DrawReticle(F, Geometry);

		// The release meter, over the score strip: timing zones from the control tuning, a no-ball past 0.85.
		const FBox2D Strip = F.Px(CricketTouch::ScoreStrip(F.Aspect, F.Safe));
		if (G->HumanBowls() && G->DPhase == EDeliveryPhase::RunUp)
		{
			const EReleaseGrade Grade = CricketControl::GradeRelease(G->Meter, G->ControlTuning);
			const FLinearColor Col = Grade == EReleaseGrade::Perfect ? Teal() : Grade == EReleaseGrade::Good ? Ink() : Grade == EReleaseGrade::NoBall ? Danger() : Amber();
			Meter(F, FVector2D(Strip.GetCenter().X, Strip.Min.Y - 58.f * U), FMath::Min(560.f * U, Strip.GetSize().X - 40.f * U), G->ControlTuning.GoodRelease,
				G->ControlTuning.PerfectRelease, G->Meter, FString(CricketControl::ReleaseGradeName(Grade)).ToUpper(), Col, 0.85f);
		}

		// Timing after a stroke, as long as the result is on screen, so the window can be learnt.
		const FDeliveryResult& Last = G->Result;
		if (G->bTimingFeedback && Last.Contact.Shot != EShotType::Leave && G->DPhase == EDeliveryPhase::DeadBall && G->PhaseTime < 3.f && !bThird && !G->bAwaitingReview)
		{
			using namespace CricketDelivery;
			constexpr float Span = 0.12f; // s either side of ideal: the swing misses beyond it
			const float T = Last.Contact.TimingError;
			const FLinearColor Col = FMath::Abs(T) <= PerfectTiming ? Teal() : FMath::Abs(T) <= GoodTiming ? Ink() : Amber();
			Meter(F, FVector2D(0.5f * F.W, 0.56f * F.H), 420.f * U, GoodTiming / Span, PerfectTiming / Span, T / Span,
				FString::Printf(TEXT("%s • %s, %s"), *TimingName(T), *ShotName(Last.Shot.Shot), *ZoneName(Last.Contact.Zone).ToLower()).ToUpper(), Col);
		}

		// The last ball in words, a caption over the strip.
		if (!G->Commentary.IsEmpty() && !G->bAwaitingReview && !bThird && G->DPhase == EDeliveryPhase::DeadBall)
		{
			FSlateFontInfo Fn = HudFont(19.f * U, EWeight::Medium);
			const float Room = Strip.GetSize().X - 40.f * U, Need = FPaint::Measure(G->Commentary, Fn).X;
			if (Need > Room) Fn.Size *= Room / Need;
			P.Pill(G->Commentary, FVector2D(Strip.GetCenter().X, Strip.Min.Y - (M.bFreeHit ? 80.f : 40.f) * U), Fn, Glass(0.9f), Ink(), 20.f * U, 42.f * U, 3.f * U, Hair());
		}

		if (TM == EMode::Batting || TM == EMode::Bowling || TM == EMode::Review) DrawControls(F, TM);
	}
	if (TM == EMode::FieldEdit) DrawFieldEditor(F);
	if (bReplay) DrawReplay(F);
	if (bReview) DrawTracking(F);
	if (!bReplay) DrawReviews(F);
	if (bCard) DrawScorecard(F);

	// Event banner, until the next ball is on its way: in from below, out with a fade.
	const ASuperOverHUD* Hud = HUD.Get();
	const float Since = Hud ? float(F.Now - Hud->BannerAt) : 100.f;
	if (Since < 2.2f && !bReplay && !bReview && !bCard && G->DPhase != EDeliveryPhase::RunUp && G->DPhase != EDeliveryPhase::BallInPlay)
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
	return P.Layer;
}
