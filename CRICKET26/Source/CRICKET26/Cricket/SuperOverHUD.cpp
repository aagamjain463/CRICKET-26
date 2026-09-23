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

	// Score bug.
	DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.75f), 20 * S, 20 * S, 420 * S, 118 * S);
	DrawRect(BatT.Colour, 20 * S, 20 * S, 8 * S, 118 * S);
	Text(FString::Printf(TEXT("%s  %d/%d   (%d.%d)"), *BatT.Short, In.Runs, In.Wickets, In.LegalBalls / 6, In.LegalBalls % 6), 38 * S, 26 * S, FLinearColor::White, 1.5f * S);
	Text(FString::Printf(TEXT("SUPER OVER %d  -  %s"), M.SuperOverNumber, M.IsChase() ? *FString::Printf(TEXT("TARGET %d"), M.Target) : TEXT("1ST INNINGS")),
		300 * S, 30 * S, FLinearColor(1, 0.85f, 0.3f), 0.8f * S);
	for (int32 I : { In.Striker, In.NonStriker })
	{
		const FBatterCard& C = In.Batters[I];
		const bool bOnStrike = I == In.Striker;
		Text(FString::Printf(TEXT("%s%s  %d (%d)"), bOnStrike ? TEXT("> ") : TEXT("  "), *BatT.Batters[I].Name, C.Runs, C.Balls),
			38 * S, (bOnStrike ? 62 : 82) * S, FLinearColor::White, 0.9f * S);
	}
	Text(FString::Printf(TEXT("%s  %d-%d  (%d.%d)%s"), *BowlT.Bowler.Name, In.Bowler.Wickets, In.Bowler.Runs, In.Bowler.Balls / 6, In.Bowler.Balls % 6,
		M.bFreeHit ? TEXT("   FREE HIT") : TEXT("")), 38 * S, 106 * S, FLinearColor(0.8f, 0.85f, 1.f), 0.9f * S);
	if (M.IsChase() && M.Phase != EMatchPhase::MatchComplete)
	{
		DrawRect(FLinearColor(0.6f, 0.05f, 0.05f, 0.85f), 20 * S, 142 * S, 420 * S, 30 * S);
		Text(M.PressureText(), 230 * S, 146 * S, FLinearColor::White, 1.1f * S, true);
	}

	// This over, ball by ball.
	FString Over;
	for (const FString& B : In.BallLog) Over += B + TEXT("  ");
	Text(Over, 20 * S, 178 * S, FLinearColor(0.9f, 0.9f, 0.9f), 0.9f * S);

	// Prompts.
	FString Prompt;
	const bool bTouch = GM->bTouchUI;
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
	Text(Prompt, W * 0.5f, H - 70 * S, FLinearColor::White, 0.9f * S, true);

	// Release meter.
	if (GM->HumanBowls() && GM->DPhase == EDeliveryPhase::RunUp)
	{
		const float X0 = W * 0.5f - 200 * S, Y0 = H - 120 * S, MW = 400 * S;
		DrawRect(FLinearColor(0, 0, 0, 0.7f), X0, Y0, MW, 18 * S);
		DrawRect(FLinearColor(0.1f, 0.8f, 0.2f, 0.9f), X0 + MW * 0.5f * (1.f - 0.15f), Y0, MW * 0.15f, 18 * S);
		DrawRect(FLinearColor(0.85f, 0.1f, 0.1f, 0.9f), X0 + MW * 0.5f * 1.85f, Y0, MW * 0.075f, 18 * S);
		DrawRect(FLinearColor::White, X0 + MW * 0.5f * (GM->Meter + 1.f) - 2 * S, Y0 - 4 * S, 4 * S, 26 * S);
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

	// Last ball (raised clear of the touch buttons when they are shown).
	if (!GM->Commentary.IsEmpty()) Text(GM->Commentary, W * 0.5f, bTouch ? H * 0.64f : H - 100 * S, FLinearColor(1, 0.9f, 0.5f), 1.f * S, true);
	if (GM->bDebug && !GM->LastSummary.IsEmpty()) Text(GM->LastSummary, W * 0.5f, H - 80 * S, FLinearColor(0.7f, 0.7f, 0.7f), 0.8f * S, true);

	if (GM->IsReplaying())
	{
		DrawRect(FLinearColor(0.7f, 0.05f, 0.05f, 0.85f), W - 150 * S, 20 * S, 130 * S, 30 * S);
		Text(TEXT("REPLAY"), W - 85 * S, 24 * S, FLinearColor::White, 1.1f * S, true);
	}

	// Event banner, until the next ball is on its way.
	if (GetWorld()->GetTimeSeconds() - BannerAt < 2.2 && !GM->IsReplaying() && GM->DPhase != EDeliveryPhase::RunUp && GM->DPhase != EDeliveryPhase::BallInPlay)
	{
		DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.7f), 0, H * 0.36f, W, 70 * S);
		Text(Banner, W * 0.5f, H * 0.36f + 12 * S, FLinearColor(1, 0.85f, 0.2f), 2.5f * S, true);
	}

	// F1 debug overlay.
	if (GM->bDebug)
	{
		const FDeliveryResult& R = GM->Result;
		const FString D = FString::Printf(
			TEXT("DEBUG (F1)  F2 flip striker hand  F3 cycle bowler type  F4 trajectory  F5 force wicket%s  F6 AI %s  F7 quality %d  F8 AI vs AI\n")
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
