// The canvas HUD actor. Its old broadcast drawing moved to SCricketMatchHUD (MatchHUDWidget), painted in Slate
// over the viewport; what stays here is the semantic-event relay that lights the Slate HUD's banners, the
// commentary caption and the F1 debug overlay.

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
		BannerEvent = E;
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

	// Commentary subtitles removed per user request (audio commentary only).

	// F1 debug overlay: developer-only, never in shipping. The only thing left on the canvas, so developer
	// state never mixes with the production HUD.
#if !(UE_BUILD_SHIPPING)
	if (GM->bDebug)
	{
		const float W = Canvas->ClipX, H = Canvas->ClipY;
		const float S = FMath::Max(1.f, H / 720.f);
		const FDeliveryResult& R = GM->Result;
		const FString D = FString::Printf(
			TEXT("DEBUG (F1)  F2 flip striker hand  F3 cycle bowler type  F4 trajectory  F5 force wicket%s  F6 AI %s  F7 quality %d  F8 AI vs AI  F9 timing bar\n")
			TEXT("Bowler: %s %s   plan: %s len %.1f line %+.2f effort %.2f move %.2f   AI intent: %s\n")
			TEXT("Release %.0f kph%s (%s)   pitched x=%.2f y=%+.2f   wide=%d\n")
			TEXT("Input: intent %d dir %.0f power %.2f press %.3f s   shot %s   zone %s   timing %+.3f s (%s)%s\n")
			TEXT("Pull: mag %.2f %s %s   mode %d\n")
			TEXT("Pad %d  stumps %d  dismissal %d   fielder %s  catch chance %d (diff %.2f)  boundary %d\n")
			TEXT("Runs attempted %d completed %d  run-out %d  direct hit %d   calls %d back %.2f state %s   seed %d   dead at %.2f s\n")
			TEXT("%s"),
			GM->bForceWicket ? TEXT(" [ARMED]") : TEXT(""), CricketAI::DifficultyName(GM->Difficulty), GM->Quality,
			*GM->BowlerPlayer().Name, *UEnum::GetValueAsString(GM->BowlerPlayer().BowlerType), TypeName(GM->HumanPlan.Type), GM->HumanPlan.Length, GM->HumanPlan.Line,
			GM->HumanPlan.Effort, GM->HumanPlan.Movement, *GM->BowlerIntent,
			R.SpeedKph, R.bNoBall ? TEXT(" NO-BALL") : TEXT(""), CricketControl::ReleaseGradeName(GM->LastReleaseGrade), R.PitchPos.X, R.PitchPos.Y, R.bWide,
			int32(GM->BatInput.Intent), GM->BatInput.DirectionDeg, GM->BatInput.Power, GM->BatInput.PressTime, *CricketDelivery::ShotName(R.Shot.Shot),
			*CricketDelivery::ZoneName(R.Contact.Zone), R.Contact.TimingError, CricketControl::TimingGradeName(GM->LastTimingGrade), R.bTooLate ? TEXT(" TOO LATE") : TEXT(""),
			GM->LiveAim.Magnitude, CricketControl::ZoneName(GM->LiveAim.DirectionDeg), CricketControl::PowerBandName(GM->LiveAim.Magnitude), int32(GM->BatMode),
			R.bPadImpact, R.bStumpsHit, int32(R.Dismissal), R.Fielding.Fielder >= 0 ? *GM->Ctx.Field[R.Fielding.Fielder].Position : TEXT("-"),
			R.Fielding.bCatchChance, R.Fielding.CatchDifficulty, R.Fielding.Boundary,
			R.Running.Attempted, R.Running.Completed, R.Running.bRunOut, R.Running.bDirectHit,
			GM->HumanRunCalls.Go.Num(), GM->HumanRunCalls.Back, CricketControl::RunStateName(GM->LastRunState), GM->Ctx.Seed, R.DeadTime,
			*GM->CameraDebugString());
		DrawRect(FLinearColor(0, 0, 0, 0.6f), W - 760 * S, 20 * S, 740 * S, 168 * S);
		Text(D, W - 750 * S, 26 * S, FLinearColor(0.6f, 1.f, 0.6f), 0.7f * S);
	}
#endif
}
