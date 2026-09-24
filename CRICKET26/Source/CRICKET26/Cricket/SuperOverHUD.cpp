#include "SuperOverHUD.h"
#include "SuperOverGameMode.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

namespace
{
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
}

namespace CricketHUD
{
	FString HowOutName(EDismissal D)
	{
		switch (D)
		{
		case EDismissal::Bowled: return TEXT("bowled");
		case EDismissal::Caught: return TEXT("caught");
		case EDismissal::LBW: return TEXT("lbw");
		case EDismissal::RunOut: return TEXT("run out");
		case EDismissal::Stumped: return TEXT("stumped");
		case EDismissal::HitWicket: return TEXT("hit wicket");
		default: return FString();
		}
	}

	FString RoleName(const FCricketPlayer& P, bool bBatting)
	{
		if (bBatting) return P.BatHand == ECricketHand::Right ? TEXT("Right-hand bat") : TEXT("Left-hand bat");
		const TCHAR* Arm = P.BowlHand == ECricketHand::Right ? TEXT("Right-arm") : TEXT("Left-arm");
		const TCHAR* Kind = P.BowlerType == EBowlerType::OffSpin ? TEXT("off spin") : P.BowlerType == EBowlerType::LegSpin ? TEXT("leg spin") : TEXT("fast");
		return FString::Printf(TEXT("%s %s"), Arm, Kind);
	}
}

void ASuperOverHUD::BeginPlay()
{
	Super::BeginPlay();
	if (ASuperOverGameMode* GM = GetWorld()->GetAuthGameMode<ASuperOverGameMode>())
	{
		GM->OnCricketEvent.AddUObject(this, &ASuperOverHUD::OnEvent);
	}
}

void ASuperOverHUD::OnEvent(ECricketEvent E)
{
	ASuperOverGameMode* GM = GetWorld()->GetAuthGameMode<ASuperOverGameMode>();
	FString S;
	int32 P = 0;
	switch (E)
	{
	case ECricketEvent::MatchWon: S = FString::Printf(TEXT("%s WIN"), *GM->Teams[GM->Match.Winner].Name.ToUpper()); P = 9; break;
	case ECricketEvent::MatchTied: S = TEXT("TIED - ANOTHER SUPER OVER"); P = 9; break;
	case ECricketEvent::Wicket: S = TEXT("WICKET"); P = 8; break;
	case ECricketEvent::BoundarySix: S = TEXT("SIX"); P = 7; break;
	case ECricketEvent::BoundaryFour: S = TEXT("FOUR"); P = 6; break;
	case ECricketEvent::NoBall: S = TEXT("NO BALL"); P = 5; break;
	case ECricketEvent::Wide: S = TEXT("WIDE"); P = 4; break;
	case ECricketEvent::TargetSet: S = FString::Printf(TEXT("TARGET %d"), GM->Match.Target); P = 3; break;
	case ECricketEvent::FreeHitNext: S = TEXT("FREE HIT"); P = 2; break;
	default: return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - BannerAt > 0.1 || P > BannerPriority)
	{
		Banner = S;
		BannerPriority = P;
		BannerAt = Now;
	}
}

void ASuperOverHUD::Text(const FString& S, float X, float Y, const FLinearColor& Colour, float Scale, bool bCentre)
{
	UFont* Font = GEngine->GetMediumFont();
	if (bCentre)
	{
		float W, H;
		GetTextSize(S, W, H, Font, Scale);
		X -= W * 0.5f;
	}
	DrawText(S, FLinearColor(0, 0, 0, 0.8f), X + 1.5f, Y + 1.5f, Font, Scale);
	DrawText(S, Colour, X, Y, Font, Scale);
}

void ASuperOverHUD::DrawHUD()
{
	Super::DrawHUD();
	ASuperOverGameMode* GM = GetWorld()->GetAuthGameMode<ASuperOverGameMode>();
	if (!GM || GM->Match.Innings.Num() == 0) return;
	const FSuperOverMatch& M = GM->Match;
	const FInningsState& In = M.Cur();
	const FCricketTeam& BatT = GM->Teams[M.BattingTeam()];
	const FCricketTeam& BowlT = GM->Teams[M.BowlingTeam()];
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const float S = FMath::Max(1.f, H / 720.f);

	// Broadcast score bar along the bottom (the top when the touch controls own the bottom): the batting side,
	// the two batters, the bowler, this over ball by ball and what the chase needs.
	const bool bTouch = GM->bTouchUI;
	const float BarH = 46 * S, BX0 = 20 * S, BarW = W - 40 * S;
	const float BarY = bTouch ? 12 * S : H - BarH - 12 * S;
	const FLinearColor Dim(0.8f, 0.85f, 1.f);
	DrawRect(FLinearColor(0.02f, 0.03f, 0.07f, 0.88f), BX0, BarY, BarW, BarH);
	DrawRect(BatT.Colour * 0.55f + FLinearColor(0, 0, 0, 0.9f), BX0, BarY, 220 * S, BarH);
	DrawRect(BatT.Colour, BX0, BarY + BarH - 4 * S, 220 * S, 4 * S);
	Text(BatT.Short, BX0 + 12 * S, BarY + 11 * S, FLinearColor::White, 1.2f * S);
	Text(FString::Printf(TEXT("%d-%d"), In.Runs, In.Wickets), BX0 + 80 * S, BarY + 5 * S, FLinearColor::White, 1.7f * S);
	Text(FString::Printf(TEXT("%d.%d"), In.LegalBalls / 6, In.LegalBalls % 6), BX0 + 172 * S, BarY + 14 * S, Dim, 1.f * S);
	for (int32 I : { In.Striker, In.NonStriker })
	{
		const FBatterCard& C = In.Batters[I];
		const bool bOnStrike = I == In.Striker;
		const float Y = BarY + (bOnStrike ? 4 : 24) * S;
		Text(FString::Printf(TEXT("%s%s"), bOnStrike ? TEXT("> ") : TEXT("  "), *BatT.Batters[I].Name), BX0 + 232 * S, Y, bOnStrike ? FLinearColor::White : Dim, 0.85f * S);
		Text(FString::Printf(TEXT("%d (%d)"), C.Runs, C.Balls), BX0 + 400 * S, Y, bOnStrike ? FLinearColor::White : Dim, 0.85f * S);
	}
	DrawRect(FLinearColor(1, 1, 1, 0.15f), BX0 + 470 * S, BarY + 6 * S, 1.5f * S, BarH - 12 * S);
	Text(BowlT.Bowler.Name, BX0 + 482 * S, BarY + 4 * S, FLinearColor::White, 0.85f * S);
	Text(FString::Printf(TEXT("%d-%d  (%d.%d)"), In.Bowler.Wickets, In.Bowler.Runs, In.Bowler.Balls / 6, In.Bowler.Balls % 6), BX0 + 482 * S, BarY + 24 * S, Dim, 0.85f * S);

	// This over: a disc per ball, coloured the way broadcasts do (wicket red, boundaries bright, extras amber).
	float BX = BX0 + 660 * S;
	for (const FString& B : In.BallLog)
	{
		const FLinearColor Disc = B.EndsWith(TEXT("W")) ? FLinearColor(0.8f, 0.08f, 0.08f)
			: B == TEXT("6") ? FLinearColor(0.55f, 0.2f, 0.85f) : B == TEXT("4") ? FLinearColor(0.1f, 0.45f, 0.9f)
			: B.Len() > 1 ? FLinearColor(0.85f, 0.6f, 0.1f) : FLinearColor(0.25f, 0.27f, 0.32f);
		Canvas->K2_DrawPolygon(nullptr, FVector2D(BX, BarY + BarH * 0.5f), FVector2D(14 * S, 14 * S), 24, Disc);
		Text(B == TEXT(".") ? TEXT("0") : B, BX, BarY + BarH * 0.5f - 8 * S, FLinearColor::White, (B.Len() > 1 ? 0.6f : 0.8f) * S, true);
		BX += 33 * S;
	}

	// The equation, or where the match stands.
	const float RX = BX0 + BarW - 240 * S;
	if (M.IsChase() && M.Phase != EMatchPhase::MatchComplete)
	{
		DrawRect(FLinearColor(0.6f, 0.05f, 0.05f, 0.9f), RX, BarY, 240 * S, BarH);
		Text(M.PressureText(), RX + 120 * S, BarY + 5 * S, FLinearColor::White, 0.85f * S, true);
		Text(FString::Printf(TEXT("TARGET %d"), M.Target), RX + 120 * S, BarY + 25 * S, FLinearColor(1, 0.85f, 0.3f), 0.75f * S, true);
	}
	else
	{
		Text(FString::Printf(TEXT("SUPER OVER %d"), M.SuperOverNumber), RX + 120 * S, BarY + 5 * S, FLinearColor(1, 0.85f, 0.3f), 0.85f * S, true);
		Text(M.IsChase() ? TEXT("2ND INNINGS") : TEXT("1ST INNINGS"), RX + 120 * S, BarY + 25 * S, Dim, 0.75f * S, true);
	}
	if (M.bFreeHit)
	{
		DrawRect(FLinearColor(0.95f, 0.75f, 0.1f, 0.95f), RX - 110 * S, BarY + 10 * S, 100 * S, 26 * S);
		Text(TEXT("FREE HIT"), RX - 60 * S, BarY + 13 * S, FLinearColor(0.05f, 0.05f, 0.05f), 0.85f * S, true);
	}

	// Speed gun, from release until the next ball.
	const float Above = bTouch ? BarY + BarH + 8 * S : BarY - 38 * S;
	if ((GM->DPhase == EDeliveryPhase::BallInPlay || GM->DPhase == EDeliveryPhase::DeadBall) && GM->Result.SpeedKph > 0.f && !GM->ShowingScorecard())
	{
		DrawRect(FLinearColor(0.02f, 0.03f, 0.07f, 0.88f), BX0 + BarW - 170 * S, Above, 170 * S, 30 * S);
		DrawRect(BowlT.Colour, BX0 + BarW - 170 * S, Above, 5 * S, 30 * S);
		Text(FString::Printf(TEXT("%.1f km/h"), GM->Result.SpeedKph), BX0 + BarW - 82 * S, Above + 5 * S, FLinearColor::White, 1.f * S, true);
	}

	// Player cards: a batter walking in and a bowler starting the over, until the ball is bowled.
	if (GM->DPhase == EDeliveryPhase::Waiting && M.Phase != EMatchPhase::MatchComplete && M.Phase != EMatchPhase::InningsBreak)
	{
		auto Card = [&](float CX, const FLinearColor& Colour, const FString& Name, const FString& Role)
		{
			DrawRect(FLinearColor(0.02f, 0.03f, 0.07f, 0.88f), CX, Above - 26 * S, 250 * S, 56 * S);
			DrawRect(Colour, CX, Above - 26 * S, 5 * S, 56 * S);
			Text(Name.ToUpper(), CX + 16 * S, Above - 22 * S, FLinearColor::White, 1.f * S);
			Text(Role, CX + 16 * S, Above + 4 * S, Dim, 0.8f * S);
		};
		const FBatterCard& C = In.Batters[In.Striker];
		if (C.Balls == 0) Card(BX0, BatT.Colour, BatT.Batters[In.Striker].Name, CricketHUD::RoleName(BatT.Batters[In.Striker], true));
		if (In.Bowler.Balls == 0 && In.Deliveries == 0) Card(BX0 + BarW - 250 * S, BowlT.Colour, BowlT.Bowler.Name, CricketHUD::RoleName(BowlT.Bowler, false));
	}

	// Prompts.
	FString Prompt;
	const TCHAR* Go = bTouch ? TEXT("Tap") : TEXT("Press Enter");
	if (M.Phase == EMatchPhase::InningsBreak) Prompt = FString::Printf(TEXT("INNINGS BREAK - %s need %d. %s to continue."), *BowlT.Name, M.Target, Go);
	else if (M.Phase == EMatchPhase::MatchComplete) Prompt = M.bTied ? FString::Printf(TEXT("TIED! %s for another Super Over."), Go)
		: FString::Printf(TEXT("%s win. %s to play again."), *GM->Teams[M.Winner].Name, Go);
	else if (GM->bAutoPlay) Prompt = TEXT("AI vs AI (F8 to take control)");
	else if (GM->HumanBowls())
	{
		const TArray<EDeliveryType> Rep = CricketBowling::Repertoire(GM->BowlerPlayer().BowlerType);
		FString Types;
		for (int32 I = 0; I < Rep.Num(); ++I) Types += FString::Printf(TEXT("%s%d %s  "), Rep[I] == GM->HumanPlan.Type ? TEXT(">") : TEXT(""), I + 1, TypeName(Rep[I]));
		Prompt = bTouch ? FString::Printf(TEXT("YOU BOWL   stick: target (length %.1f m, line %+.2f m)   BOWL, then BOWL again in the green"), GM->HumanPlan.Length, GM->HumanPlan.Line)
			: FString::Printf(TEXT("YOU BOWL   %s\nWASD/arrows move the target (length %.1f m, line %+.2f m)   Space: run up, Space again: release in the green"),
			*Types, GM->HumanPlan.Length, GM->HumanPlan.Line);
	}
	else if (GM->HumanBats())
	{
		const TCHAR* Run = GM->HumanRunMargin > 0.5f ? TEXT("safe") : GM->HumanRunMargin > 0.f ? TEXT("normal") : TEXT("aggressive");
		Prompt = bTouch ? FString::Printf(TEXT("YOU BAT   stick: %s   running: %s"), *GM->DirectionName(), Run)
			: FString::Printf(TEXT("YOU BAT   hold WASD for direction: %s   J ground  K loft  L defend  (no key = leave)   R running: %s"),
			*GM->DirectionName(), Run);
	}
	Text(Prompt, W * 0.5f, bTouch ? H - 70 * S : BarY - 104 * S, FLinearColor::White, 0.9f * S, true);

	// Release meter.
	if (GM->HumanBowls() && GM->DPhase == EDeliveryPhase::RunUp)
	{
		const float MX = W * 0.5f - 200 * S, Y0 = bTouch ? H - 120 * S : BarY - 132 * S, MW = 400 * S;
		DrawRect(FLinearColor(0, 0, 0, 0.7f), MX, Y0, MW, 18 * S);
		DrawRect(FLinearColor(0.1f, 0.8f, 0.2f, 0.9f), MX + MW * 0.5f * (1.f - 0.15f), Y0, MW * 0.15f, 18 * S);
		DrawRect(FLinearColor(0.85f, 0.1f, 0.1f, 0.9f), MX + MW * 0.5f * 1.85f, Y0, MW * 0.075f, 18 * S);
		DrawRect(FLinearColor::White, MX + MW * 0.5f * (GM->Meter + 1.f) - 2 * S, Y0 - 4 * S, 4 * S, 26 * S);
	}

	// Touch controls: the same layout the game mode reads, so what is drawn is what is pressed.
	const CricketTouch::EMode TM = GM->TouchMode();
	if (TM == CricketTouch::EMode::Batting || TM == CricketTouch::EMode::Bowling)
	{
		using CricketTouch::EButton;
		const TArray<EDeliveryType> Rep = CricketBowling::Repertoire(GM->BowlerPlayer().BowlerType);
		for (const CricketTouch::FButton& B : CricketTouch::Layout(TM, Rep.Num(), W / H))
		{
			FString Label;
			bool bLit = false;
			switch (B.Button)
			{
			case EButton::Defend: Label = TEXT("DEFEND"); break;
			case EButton::Ground: Label = TEXT("GROUND"); break;
			case EButton::Loft: Label = TEXT("LOFT"); break;
			case EButton::Run: Label = TEXT("RUN"); bLit = GM->HumanRunMargin <= 0.f; break;
			case EButton::Bowl: Label = GM->DPhase == EDeliveryPhase::RunUp ? TEXT("RELEASE") : TEXT("BOWL"); break;
			case EButton::Delivery: Label = TypeName(Rep[B.Index]); bLit = Rep[B.Index] == GM->HumanPlan.Type; break;
			}
			const FVector2D Size = B.Rect.GetSize() * H, Centre = B.Rect.GetCenter() * H;
			DrawRect(bLit ? FLinearColor(0.85f, 0.6f, 0.1f, 0.75f) : FLinearColor(0.05f, 0.05f, 0.12f, 0.55f), B.Rect.Min.X * H, B.Rect.Min.Y * H, Size.X, Size.Y);
			Text(Label, Centre.X, Centre.Y - 9 * S, FLinearColor::White, 0.9f * S, true);
		}
		const FVector2D C = CricketTouch::StickCentre() * H;
		const float R = CricketTouch::StickRadius * H;
		DrawRect(FLinearColor(0.05f, 0.05f, 0.12f, 0.4f), C.X - R, C.Y - R, 2 * R, 2 * R);
		Text(TEXT("^"), C.X, C.Y - R + 4 * S, FLinearColor::White, 1.f * S, true);
		Text(TEXT("v"), C.X, C.Y + R - 22 * S, FLinearColor::White, 1.f * S, true);
		Text(TEXT("<"), C.X - R + 10 * S, C.Y - 9 * S, FLinearColor::White, 1.f * S, true);
		Text(TEXT(">"), C.X + R - 10 * S, C.Y - 9 * S, FLinearColor::White, 1.f * S, true);
	}

	// Timing bar after a stroke, as long as the result is on screen: where the swing fell between early and late,
	// with the grade, the shot and where it came off the bat, so the window can be learnt.
	const FDeliveryResult& Last = GM->Result;
	if (GM->bTimingFeedback && Last.Contact.Shot != EShotType::Leave && GM->DPhase == EDeliveryPhase::DeadBall && GM->PhaseTime < 3.f
		&& !GM->IsReplaying() && !GM->IsReviewing() && !GM->ShowingScorecard())
	{
		using namespace CricketDelivery;
		constexpr float Span = 0.12f; // s either side of ideal: the swing misses beyond it
		const float T = Last.Contact.TimingError, TW = 360 * S, TX = W * 0.5f - TW * 0.5f, TY = bTouch ? H * 0.58f : BarY - 200 * S;
		auto At = [&](float Sec) { return TX + TW * 0.5f * (1.f + FMath::Clamp(Sec / Span, -1.f, 1.f)); };
		const FLinearColor Grade = FMath::Abs(T) <= PerfectTiming ? FLinearColor(0.2f, 0.95f, 0.3f)
			: FMath::Abs(T) <= GoodTiming ? FLinearColor(0.75f, 0.95f, 0.25f) : FLinearColor(1.f, 0.55f, 0.15f);
		Text(FString::Printf(TEXT("%s   %s, %s"), *TimingName(T), *ShotName(Last.Shot.Shot), *ZoneName(Last.Contact.Zone).ToLower()),
			W * 0.5f, TY - 26 * S, Grade, 1.f * S, true);
		DrawRect(FLinearColor(0.02f, 0.03f, 0.07f, 0.85f), TX, TY, TW, 12 * S);
		DrawRect(FLinearColor(0.4f, 0.75f, 0.2f, 0.6f), At(-GoodTiming), TY, At(GoodTiming) - At(-GoodTiming), 12 * S);
		DrawRect(FLinearColor(0.2f, 0.95f, 0.3f, 0.9f), At(-PerfectTiming), TY, At(PerfectTiming) - At(-PerfectTiming), 12 * S);
		DrawRect(FLinearColor::White, At(T) - 2 * S, TY - 5 * S, 4 * S, 22 * S);
		Text(TEXT("EARLY"), TX - 36 * S, TY - 3 * S, Dim, 0.65f * S, true);
		Text(TEXT("LATE"), TX + TW + 32 * S, TY - 3 * S, Dim, 0.65f * S, true);
	}

	// Last ball (raised clear of the touch buttons when they are shown).
	if (!GM->Commentary.IsEmpty()) Text(GM->Commentary, W * 0.5f, bTouch ? H * 0.64f : BarY - 160 * S, FLinearColor(1, 0.9f, 0.5f), 1.f * S, true);
	if (GM->bDebug && !GM->LastSummary.IsEmpty()) Text(GM->LastSummary, W * 0.5f, bTouch ? H - 80 * S : BarY - 182 * S, FLinearColor(0.7f, 0.7f, 0.7f), 0.8f * S, true);

	if (GM->IsReplaying())
	{
		DrawRect(FLinearColor(0.7f, 0.05f, 0.05f, 0.85f), W - 150 * S, 20 * S, 130 * S, 30 * S);
		Text(TEXT("REPLAY"), W - 85 * S, 24 * S, FLinearColor::White, 1.1f * S, true);
	}

	// Ball tracking: the three LBW calls, each as the trail reaches it, then the decision.
	if (GM->IsReviewing())
	{
		const FBallTracking& T = Last.Tracking;
		const float P = GM->ReviewProgress(), PW = 300 * S, PX = 20 * S;
		float Y = H * 0.2f;
		const FLinearColor Good(0.1f, 0.6f, 0.2f, 0.9f), Bad(0.75f, 0.08f, 0.08f, 0.9f), Call(0.9f, 0.6f, 0.05f, 0.9f);
		auto Side = [](float Line) { return Line > FBallTracking::InLine ? TEXT("OUTSIDE OFF") : Line < -FBallTracking::InLine ? TEXT("OUTSIDE LEG") : TEXT("IN LINE"); };
		auto Row = [&](const TCHAR* Label, const FString& Value, const FLinearColor& Colour)
		{
			DrawRect(FLinearColor(0.02f, 0.03f, 0.07f, 0.88f), PX, Y, PW, 34 * S);
			Text(Label, PX + 12 * S, Y + 8 * S, Dim, 0.85f * S);
			DrawRect(Colour, PX + PW - 160 * S, Y + 4 * S, 156 * S, 26 * S);
			Text(Value, PX + PW - 82 * S, Y + 7 * S, FLinearColor::White, 0.85f * S, true);
			Y += 38 * S;
		};
		DrawRect(FLinearColor(0.1f, 0.4f, 0.95f, 0.9f), PX, Y, PW, 30 * S);
		Text(TEXT("BALL TRACKING"), PX + PW * 0.5f, Y + 5 * S, FLinearColor::White, 1.f * S, true);
		Y += 34 * S;
		if (P >= 0.25f) Row(TEXT("PITCHING"), Last.PitchTime < 0.f ? FString(TEXT("FULL TOSS")) : FString(Side(T.PitchLine)), T.bPitchedOutsideLeg ? Bad : Good);
		if (P >= 0.42f) Row(TEXT("IMPACT"), Side(T.ImpactLine), T.bImpactInLine ? Good : Bad);
		if (P >= 0.72f) Row(TEXT("WICKETS"), T.bUmpiresCall ? TEXT("UMPIRE'S CALL") : T.bWouldHit ? TEXT("HITTING") : TEXT("MISSING"),
			T.bUmpiresCall ? Call : T.bWouldHit ? Good : Bad);
		if (P >= 0.82f)
		{
			const bool bOut = Last.Dismissal == EDismissal::LBW;
			DrawRect(bOut ? Bad : Good, PX, Y + 6 * S, PW, 44 * S);
			Text(bOut ? TEXT("OUT") : TEXT("NOT OUT"), PX + PW * 0.5f, Y + 11 * S, FLinearColor::White, 1.6f * S, true);
		}
	}

	// Scorecard at the innings break and the result, once the banner and the replay have had their moment.
	if (GM->ShowingScorecard())
	{
		const float CW = 560 * S, X0 = W * 0.5f - CW * 0.5f;
		float Y = H * 0.22f;
		DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.85f), X0, Y, CW, 34 * S);
		Text(M.Phase == EMatchPhase::InningsBreak ? TEXT("INNINGS BREAK") : FString::Printf(TEXT("SUPER OVER %d  -  RESULT"), M.SuperOverNumber),
			W * 0.5f, Y + 5 * S, FLinearColor(1, 0.85f, 0.3f), 1.2f * S, true);
		Y += 34 * S;
		for (const FInningsState& Inn : M.Innings)
		{
			const FCricketTeam& T = GM->Teams[Inn.BattingTeam];
			DrawRect(FLinearColor(0.04f, 0.04f, 0.08f, 0.8f), X0, Y, CW, 30 * S);
			DrawRect(T.Colour, X0, Y, 8 * S, 30 * S);
			Text(T.Name.ToUpper(), X0 + 20 * S, Y + 4 * S, FLinearColor::White, 1.1f * S);
			Text(FString::Printf(TEXT("%d/%d  (%d.%d)"), Inn.Runs, Inn.Wickets, Inn.LegalBalls / 6, Inn.LegalBalls % 6), X0 + CW - 150 * S, Y + 4 * S, FLinearColor::White, 1.1f * S);
			Y += 30 * S;
			for (int32 I = 0; I < Inn.Batters.Num(); ++I)
			{
				const FBatterCard& C = Inn.Batters[I];
				if (C.Balls == 0 && C.HowOut == EDismissal::None && I != Inn.Striker && I != Inn.NonStriker) continue;
				DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.7f), X0, Y, CW, 22 * S);
				Text(FString::Printf(TEXT("%s%s"), *T.Batters[I].Name, C.HowOut == EDismissal::None ? TEXT(" *") : *(TEXT("   ") + CricketHUD::HowOutName(C.HowOut))), X0 + 20 * S, Y + 3 * S, FLinearColor(0.9f, 0.9f, 0.9f), 0.9f * S);
				Text(FString::Printf(TEXT("%d (%d)   4s %d  6s %d"), C.Runs, C.Balls, C.Fours, C.Sixes), X0 + CW - 230 * S, Y + 3 * S, FLinearColor(0.9f, 0.9f, 0.9f), 0.9f * S);
				Y += 22 * S;
			}
			const FBowlerCard& B = Inn.Bowler;
			DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.7f), X0, Y, CW, 22 * S);
			Text(GM->Teams[1 - Inn.BattingTeam].Bowler.Name, X0 + 20 * S, Y + 3 * S, FLinearColor(0.8f, 0.85f, 1.f), 0.9f * S);
			Text(FString::Printf(TEXT("%d-%d (%d.%d)   wd %d  nb %d"), B.Wickets, B.Runs, B.Balls / 6, B.Balls % 6, B.Wides, B.NoBalls), X0 + CW - 230 * S, Y + 3 * S, FLinearColor(0.8f, 0.85f, 1.f), 0.9f * S);
			Y += 30 * S;
		}
		const FString Line = M.Phase == EMatchPhase::InningsBreak ? FString::Printf(TEXT("%s NEED %d TO WIN FROM 6 BALLS"), *BowlT.Name.ToUpper(), M.Target)
			: M.bTied ? FString(TEXT("SCORES LEVEL - ANOTHER SUPER OVER")) : FString::Printf(TEXT("%s WIN"), *GM->Teams[M.Winner].Name.ToUpper());
		const bool bWon = M.Phase == EMatchPhase::MatchComplete && !M.bTied;
		DrawRect(bWon ? GM->Teams[M.Winner].Colour * 0.6f + FLinearColor(0, 0, 0, 0.85f) : FLinearColor(0.6f, 0.05f, 0.05f, 0.85f), X0, Y, CW, 32 * S);
		Text(Line, W * 0.5f, Y + 5 * S, FLinearColor::White, 1.1f * S, true);
	}

	// Event banner, until the next ball is on its way.
	if (GetWorld()->GetTimeSeconds() - BannerAt < 2.2 && !GM->IsReplaying() && !GM->IsReviewing() && GM->DPhase != EDeliveryPhase::RunUp && GM->DPhase != EDeliveryPhase::BallInPlay && !GM->ShowingScorecard())
	{
		DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.7f), 0, H * 0.36f, W, 70 * S);
		Text(Banner, W * 0.5f, H * 0.36f + 12 * S, FLinearColor(1, 0.85f, 0.2f), 2.5f * S, true);
	}

	// F1 debug overlay.
	if (GM->bDebug)
	{
		const FDeliveryResult& R = GM->Result;
		const FString D = FString::Printf(
			TEXT("DEBUG (F1)  F2 flip striker hand  F3 cycle bowler type  F4 trajectory  F5 force wicket%s  F6 AI %s  F7 quality %d  F8 AI vs AI  F9 timing bar\n")
			TEXT("Bowler: %s %s   plan: %s len %.1f line %+.2f   AI intent: %s\n")
			TEXT("Release %.0f kph%s   pitched x=%.2f y=%+.2f   wide=%d\n")
			TEXT("Input: intent %d dir %.0f press %.3f s   shot %s   zone %s   timing %+.3f s%s\n")
			TEXT("Pad %d  stumps %d  dismissal %d   fielder %s  catch chance %d (diff %.2f)  boundary %d\n")
			TEXT("Runs attempted %d completed %d  run-out %d  direct hit %d   seed %d   dead at %.2f s"),
			GM->bForceWicket ? TEXT(" [ARMED]") : TEXT(""), CricketAI::DifficultyName(GM->Difficulty), GM->Quality,
			*GM->BowlerPlayer().Name, *UEnum::GetValueAsString(GM->BowlerPlayer().BowlerType), TypeName(GM->HumanPlan.Type), GM->HumanPlan.Length, GM->HumanPlan.Line,
			*GM->BowlerIntent, R.SpeedKph, R.bNoBall ? TEXT(" NO-BALL") : TEXT(""), R.PitchPos.X, R.PitchPos.Y, R.bWide,
			int32(GM->BatInput.Intent), GM->BatInput.DirectionDeg, GM->BatInput.PressTime, *CricketDelivery::ShotName(R.Shot.Shot),
			*CricketDelivery::ZoneName(R.Contact.Zone), R.Contact.TimingError, R.bTooLate ? TEXT(" TOO LATE") : TEXT(""),
			R.bPadImpact, R.bStumpsHit, int32(R.Dismissal), R.Fielding.Fielder >= 0 ? *GM->Ctx.Field[R.Fielding.Fielder].Position : TEXT("-"),
			R.Fielding.bCatchChance, R.Fielding.CatchDifficulty, R.Fielding.Boundary,
			R.Running.Attempted, R.Running.Completed, R.Running.bRunOut, R.Running.bDirectHit, GM->Ctx.Seed, R.DeadTime);
		DrawRect(FLinearColor(0, 0, 0, 0.6f), W - 760 * S, 20 * S, 740 * S, 130 * S);
		Text(D, W - 750 * S, 26 * S, FLinearColor(0.6f, 1.f, 0.6f), 0.7f * S);
	}
}
