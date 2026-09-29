#include "CricketPresentation.h"

namespace CricketPresentation
{
namespace
{
	// Shot and beat times are authored as fractions of the scene, then scaled to its pacing length.
	void Shot(FScenePlan& S, EShot Kind, ERole Subject, float From, float To, ERole Second = ERole::None)
	{
		S.Shots.Add({ Kind, Subject, Second, From, To - From });
	}

	void Beat(FScenePlan& S, ERole Who, EAction Action, ERole LookAt, float From = 0.f, float To = 1.f)
	{
		S.Beats.Add({ Who, Action, LookAt, From, To - From });
	}

	void Scale(FScenePlan& S)
	{
		for (FShot& X : S.Shots) { X.Start *= S.Duration; X.Duration *= S.Duration; }
		for (FBeat& X : S.Beats) { X.Start *= S.Duration; X.Duration *= S.Duration; }
	}

	/** The player who completed the dismissal: the one who must celebrate first. */
	ERole WicketHero(const FDeliveryInput& In)
	{
		switch (In.Ctx.Dismissal)
		{
		case EDismissal::Stumped: return ERole::Keeper;
		case EDismissal::Caught:
			return In.bCaughtAndBowled ? ERole::Bowler : In.bKeeperCatch ? ERole::Keeper : ERole::Catcher;
		case EDismissal::RunOut: return ERole::Catcher;
		default: return ERole::Bowler; // bowled, LBW, hit wicket
		}
	}

	/** Authors the shot list and beats for a scene type and variant, in scene fractions. */
	void Author(FScenePlan& S, const FDeliveryInput* In)
	{
		const int32 V = S.Variant;
		const ERole Hero = In ? WicketHero(*In) : ERole::Bowler;
		switch (S.Type)
		{
		case EScene::Drop:
			Shot(S, EShot::CloseUp, ERole::Catcher, 0.f, 0.5f);
			Shot(S, EShot::CloseUp, ERole::Bowler, 0.5f, 1.f);
			Beat(S, ERole::Catcher, EAction::Dejected, ERole::None);
			Beat(S, ERole::Bowler, EAction::Dejected, ERole::Catcher);
			break;
		case EScene::Wicket:
			// The correct fielder celebrates first, the team runs in from where they stand, the batter walks off.
			Shot(S, V == 2 ? EShot::Medium : EShot::CloseUp, Hero, 0.f, 0.35f);
			Shot(S, EShot::Group, Hero, 0.35f, 0.7f);
			Shot(S, V == 1 ? EShot::Medium : EShot::CloseUp, ERole::DismissedBatter, 0.7f, 1.f);
			Beat(S, Hero, EAction::Celebrate, ERole::None);
			if (Hero != ERole::Bowler) Beat(S, ERole::Bowler, EAction::Converge, Hero, 0.1f, 1.f);
			Beat(S, ERole::BowlingTeam, EAction::Converge, Hero, 0.15f, 1.f);
			Beat(S, ERole::DismissedBatter, EAction::WalkOff, ERole::None, 0.25f, 1.f);
			Beat(S, ERole::NonStriker, EAction::Dejected, ERole::None);
			break;
		case EScene::EndOfOver:
		case EScene::Maiden:
		case EScene::ExpensiveOver:
			Shot(S, EShot::TwoShot, V == 1 ? ERole::Bowler : ERole::Striker, 0.f, 0.55f, V == 1 ? ERole::Captain : ERole::NonStriker);
			Shot(S, EShot::TwoShot, V == 1 ? ERole::Striker : ERole::Bowler, 0.55f, 1.f, V == 1 ? ERole::NonStriker : ERole::Captain);
			Beat(S, ERole::BattingPair, EAction::Talk, ERole::None);
			Beat(S, ERole::Bowler, S.Type == EScene::ExpensiveOver ? EAction::Dejected : EAction::Talk, ERole::Captain);
			Beat(S, ERole::Captain, S.Type == EScene::Maiden ? EAction::Clap : EAction::Talk, ERole::Bowler);
			break;
		case EScene::LastBall:
			Shot(S, EShot::Establishing, ERole::Ground, 0.f, 0.3f);
			Shot(S, EShot::TwoShot, ERole::Bowler, 0.3f, 0.65f, ERole::Captain);
			Shot(S, EShot::CloseUp, ERole::Striker, 0.65f, 1.f);
			Beat(S, ERole::Bowler, EAction::Talk, ERole::Captain);
			Beat(S, ERole::Captain, EAction::Talk, ERole::Bowler);
			Beat(S, ERole::Striker, EAction::LookAround, ERole::None);
			break;
		case EScene::MatchWinBoundary:
		case EScene::MatchWinRuns:
			Shot(S, V == 1 ? EShot::Crowd : EShot::Medium, V == 1 ? ERole::Crowd : ERole::Striker, 0.f, 0.3f);
			Shot(S, EShot::Group, ERole::BattingPair, 0.3f, 0.6f);
			Shot(S, EShot::CloseUp, ERole::Bowler, 0.6f, 0.8f);
			Shot(S, EShot::Establishing, ERole::Ground, 0.8f, 1.f);
			Beat(S, ERole::Striker, EAction::RaiseBat, ERole::None, 0.f, 0.5f);
			Beat(S, ERole::BattingPair, EAction::Celebrate, ERole::Striker, 0.2f, 1.f);
			Beat(S, ERole::Bowler, EAction::Dejected, ERole::None);
			Beat(S, ERole::BowlingTeam, EAction::Dejected, ERole::None);
			Beat(S, ERole::Bowler, EAction::Handshake, ERole::Striker, 0.75f, 1.f);
			break;
		case EScene::MatchWinWicket:
			Shot(S, EShot::CloseUp, Hero, 0.f, 0.3f);
			Shot(S, EShot::Group, Hero, 0.3f, 0.65f);
			Shot(S, EShot::CloseUp, ERole::DismissedBatter, 0.65f, 0.85f);
			Shot(S, EShot::Crowd, ERole::Crowd, 0.85f, 1.f);
			Beat(S, Hero, EAction::Celebrate, ERole::None);
			Beat(S, ERole::BowlingTeam, EAction::Converge, Hero, 0.1f, 1.f);
			if (Hero != ERole::Bowler) Beat(S, ERole::Bowler, EAction::Converge, Hero, 0.1f, 1.f);
			Beat(S, ERole::DismissedBatter, EAction::Dejected, ERole::None);
			Beat(S, ERole::NonStriker, EAction::Dejected, ERole::None);
			break;
		case EScene::MatchTied:
			Shot(S, EShot::Establishing, ERole::Ground, 0.f, 0.4f);
			Shot(S, EShot::TwoShot, ERole::Striker, 0.4f, 1.f, ERole::NonStriker);
			Beat(S, ERole::BattingPair, EAction::Talk, ERole::None);
			Beat(S, ERole::Bowler, EAction::LookAround, ERole::None);
			break;
		case EScene::InningsBreak:
			Shot(S, EShot::Establishing, ERole::Ground, 0.f, 0.4f);
			Shot(S, EShot::TwoShot, ERole::Striker, 0.4f, 1.f, ERole::NonStriker);
			Beat(S, ERole::BattingPair, EAction::WalkOff, ERole::None);
			Beat(S, ERole::BowlingTeam, EAction::Clap, ERole::None);
			break;
		case EScene::Toss:
			Shot(S, EShot::Establishing, ERole::Ground, 0.f, 0.35f);
			Shot(S, EShot::TwoShot, ERole::Captain, 0.35f, 1.f, ERole::Umpire);
			Beat(S, ERole::Captain, EAction::Talk, ERole::Umpire);
			break;
		case EScene::PreMatch:
			Shot(S, EShot::Establishing, ERole::Ground, 0.f, 0.5f);
			Shot(S, EShot::Crowd, ERole::Crowd, 0.5f, 1.f);
			break;
		case EScene::Result:
			Shot(S, EShot::Establishing, ERole::Ground, 0.f, 0.3f);
			Shot(S, EShot::Group, ERole::BattingPair, 0.3f, 0.7f);
			Shot(S, EShot::Crowd, ERole::Crowd, 0.7f, 1.f);
			Beat(S, ERole::BattingPair, EAction::Handshake, ERole::Bowler);
			Beat(S, ERole::Bowler, EAction::Handshake, ERole::Striker);
			break;
		default: break;
		}
	}

	FScenePlan Build(FDirector& D, EScene Type, float Pressure01, const FDeliveryInput* In)
	{
		FScenePlan S;
		S.Type = Type;
		S.Level = LevelOf(Type, Pressure01);
		S.Duration = LevelDuration(S.Level, D.Pacing);
		if (S.Duration <= 0.f) return S;
		S.Variant = PickVariant(D, Type);
		S.Crowd = FMath::Clamp(0.2f * S.Level, 0.f, 1.f);
		S.bReplay = D.Pacing != EPacing::Quick || S.Level >= 4;
		Author(S, In);
		return S;
	}
}

const FShot* FScenePlan::ShotAt(float T) const
{
	for (const FShot& X : Shots)
		if (T >= X.Start && T < X.Start + X.Duration) return &X;
	return nullptr;
}

const FBeat* FScenePlan::BeatFor(ERole Who, float T) const
{
	const FBeat* Best = nullptr;
	for (const FBeat& X : Beats)
		if (X.Who == Who && T >= X.Start && T < X.Start + X.Duration && (!Best || X.Start >= Best->Start)) Best = &X;
	return Best;
}

float Pressure(const CricketCommentaryDirector::FContext& C, int32 MaxWickets)
{
	if (C.bMatchComplete || C.bTied) return 1.f;
	const bool bLastWicket = C.Wickets >= MaxWickets - 1;
	if (!C.bChase) return FMath::Clamp(0.2f + 0.1f * C.LegalBalls / FMath::Max(1, C.LegalBalls + C.BallsRemaining) + (bLastWicket ? 0.15f : 0.f), 0.f, 1.f);
	const float PerBall = float(C.RunsRequired) / float(FMath::Max(1, C.BallsRemaining));
	float P = 0.2f + 0.25f * PerBall;
	if (bLastWicket) P += 0.15f;
	if (C.BallsRemaining <= 2) P += 0.2f;
	return FMath::Clamp(P, 0.f, 1.f);
}

EMilestone FMilestoneTracker::Batting(const FString& Batter, int32 Before, int32 After, int32& OutThreshold)
{
	static const EMilestone Kinds[] = { EMilestone::Fifty, EMilestone::Hundred, EMilestone::OneFifty, EMilestone::TwoHundred };
	OutThreshold = 0;
	EMilestone Hit = EMilestone::None;
	// ponytail: one ball can cross at most one threshold (the steps are 50 apart); the highest wins.
	for (int32 I = 0; I < BattingThresholds.Num(); ++I)
	{
		const int32 T = BattingThresholds[I];
		const FString Key = FString::Printf(TEXT("%s/bat%d"), *Batter, T);
		if (Before < T && After >= T && !Fired.Contains(Key))
		{
			Fired.Add(Key);
			OutThreshold = T;
			Hit = I < UE_ARRAY_COUNT(Kinds) ? Kinds[I] : EMilestone::TwoHundred;
		}
	}
	return Hit;
}

EMilestone FMilestoneTracker::Bowling(const FString& Bowler, int32 WicketsAfter, bool bBowlerWicket, bool bLegalBall)
{
	int32& Streak = WicketStreak.FindOrAdd(Bowler);
	if (bBowlerWicket) ++Streak;
	else if (bLegalBall) Streak = 0;
	const FString Hat = Bowler + TEXT("/hattrick");
	if (bBowlerWicket && Streak >= 3 && !Fired.Contains(Hat))
	{
		Fired.Add(Hat);
		return EMilestone::HatTrick;
	}
	const FString Haul = Bowler + TEXT("/haul");
	if (bBowlerWicket && WicketsAfter >= BowlingHaul && !Fired.Contains(Haul))
	{
		Fired.Add(Haul);
		return EMilestone::FiveWickets;
	}
	return EMilestone::None;
}

int32 VariantCount(EScene Type)
{
	switch (Type)
	{
	case EScene::Wicket: return 3;
	case EScene::EndOfOver: case EScene::Maiden: case EScene::ExpensiveOver: case EScene::MatchWinBoundary:
	case EScene::MatchWinRuns: return 2;
	default: return 1;
	}
}

int32 PickVariant(FDirector& D, EScene Type)
{
	const int32 N = VariantCount(Type);
	TArray<int32>& Recent = D.RecentVariants.FindOrAdd(uint8(Type));
	// Fresh = not among the last N-1 used, so every variant plays before any repeats.
	TArray<int32> Fresh;
	for (int32 V = 0; V < N; ++V)
		if (!Recent.Contains(V)) Fresh.Add(V);
	const int32 Pick = Fresh.Num() ? Fresh[D.Rng.RandRange(0, Fresh.Num() - 1)] : D.Rng.RandRange(0, N - 1);
	Recent.Add(Pick);
	while (Recent.Num() > FMath::Max(0, N - 1)) Recent.RemoveAt(0);
	return Pick;
}

float LevelDuration(int32 Level, EPacing Pacing)
{
	static const float Balanced[] = { 0.f, 1.4f, 2.4f, 3.6f, 5.f, 7.5f };
	const float Base = Balanced[FMath::Clamp(Level, 0, 5)];
	switch (Pacing)
	{
	case EPacing::Full: return Base * 1.35f;
	case EPacing::Quick: return Level < 3 ? 0.f : Base * 0.5f;
	default: return Base;
	}
}

int32 LevelOf(EScene Type, float P)
{
	switch (Type)
	{
	// Routine play (dots, runs, edges, play-and-miss, fours, sixes) gets no live scene: nobody stops to
	// celebrate a boundary. The banner, commentary and replay cover it.
	case EScene::EndOfOver: return 1;
	case EScene::Maiden: case EScene::ExpensiveOver: case EScene::PreMatch: return 2;
	case EScene::Drop: case EScene::Toss: return 3;
	case EScene::Wicket: return P > 0.6f ? 4 : 3;
	case EScene::LastBall: case EScene::InningsBreak: return 4;
	case EScene::MatchWinBoundary: case EScene::MatchWinWicket: case EScene::MatchWinRuns: case EScene::MatchTied:
	case EScene::Result: return 5;
	default: return 0;
	}
}

FScenePlan Direct(FDirector& D, const FDeliveryInput& In)
{
	const CricketCommentaryDirector::FContext& C = In.Ctx;
	const float P = Pressure(C, In.MaxWickets);
	const bool bWicket = C.Dismissal != EDismissal::None;
	const bool bLegal = !C.bWide && !C.bNoBall;

	// Milestones first: the tracker must see every ball so a streak or a crossing is never missed.
	int32 Threshold = 0;
	EMilestone Bat = In.Ctx.bWide ? EMilestone::None : D.Milestones.Batting(In.StrikerId, In.StrikerRunsBefore, In.StrikerRunsAfter, Threshold);
	const bool bBowlerWicket = bWicket && C.Dismissal != EDismissal::RunOut;
	const EMilestone Bowl = D.Milestones.Bowling(In.BowlerId, In.BowlerWicketsAfter, bBowlerWicket, bLegal);

	// The primary moment of the ball, from the most decisive fact down.
	EScene Type = EScene::Dot;
	if (C.bMatchComplete && C.Winner >= 0)
		Type = bWicket ? EScene::MatchWinWicket : C.Boundary > 0 ? EScene::MatchWinBoundary : EScene::MatchWinRuns;
	else if (C.bTied) Type = EScene::MatchTied;
	else if (bWicket) Type = EScene::Wicket;
	else if (C.Boundary == 6) Type = EScene::Six;
	else if (C.Boundary == 4) Type = EScene::Four;
	else if (C.bDroppedCatch) Type = EScene::Drop;
	else if (In.bEdge) Type = EScene::Edge;
	else if (In.bBeaten) Type = EScene::Beaten;
	else if (C.RunsRun > 0 || C.bWide || C.bNoBall) Type = EScene::Runs;

	// Moments that follow the ball fold in only when the ball itself was routine.
	int32 Level = LevelOf(Type, P);
	const bool bLastBallNext = C.bChase && !C.bMatchComplete && !C.bTied && C.BallsRemaining == 1;
	if (bLastBallNext && Level < 4) Type = EScene::LastBall;
	else if (C.bInningsBreak && Level < 3) Type = EScene::InningsBreak;
	else if (In.bOverComplete && !C.bInningsBreak && Level < 2)
		Type = In.OverRuns == 0 ? EScene::Maiden : In.OverRuns >= 15 ? EScene::ExpensiveOver : EScene::EndOfOver;

	FScenePlan S = Build(D, Type, P, &In);
	const EMilestone M = Bat != EMilestone::None ? Bat : Bowl;
	if (M != EMilestone::None)
	{
		// A milestone lifts the scene and adds its own beat; a match-winning century stays one scene.
		S.Milestone = M;
		S.MilestoneRuns = Threshold;
		S.bMerged = S.Type != EScene::None && S.Level > 0;
		S.Level = FMath::Max(S.Level, 4);
		if (S.Type == EScene::Dot || S.Type == EScene::Runs || S.Type == EScene::EndOfOver || S.Duration <= 0.f)
		{
			S.Type = Bat != EMilestone::None ? EScene::Runs : EScene::Wicket;
			S.Shots.Reset();
			S.Beats.Reset();
			S.bMerged = false;
		}
		const float Before = S.Duration;
		S.Duration = FMath::Max(S.Duration, LevelDuration(4, D.Pacing)) + (S.bMerged ? LevelDuration(2, D.Pacing) * 0.6f : 0.f);
		S.Duration = FMath::Max(S.Duration, D.Pacing == EPacing::Quick ? 2.5f : 4.f);
		S.Crowd = FMath::Max(S.Crowd, 0.85f);
		S.bReplay = true;
		// The milestone beat opens the scene: the hero, then everything already authored shifted after it.
		const float Lead = S.Duration - Before;
		const float LeadFrac = Lead / S.Duration;
		for (FShot& X : S.Shots) { X.Start = LeadFrac + X.Start * (1.f - LeadFrac); X.Duration *= 1.f - LeadFrac; }
		for (FBeat& X : S.Beats) { X.Start = LeadFrac + X.Start * (1.f - LeadFrac); X.Duration *= 1.f - LeadFrac; }
		const ERole Hero = Bat != EMilestone::None ? ERole::Striker : ERole::Bowler;
		S.Shots.Insert({ EShot::CloseUp, Hero, ERole::None, 0.f, LeadFrac * 0.55f }, 0);
		S.Shots.Insert({ EShot::Medium, Hero, ERole::None, LeadFrac * 0.55f, LeadFrac * 0.45f }, 1);
		S.Beats.Insert({ Hero, Bat != EMilestone::None ? EAction::RaiseBat : EAction::Celebrate, ERole::Crowd, 0.f, LeadFrac }, 0);
		if (Bat != EMilestone::None) S.Beats.Add({ ERole::NonStriker, EAction::Clap, ERole::Striker, 0.f, LeadFrac });
		else S.Beats.Add({ ERole::BowlingTeam, EAction::Converge, ERole::Bowler, 0.f, 1.f });
		if (LeadFrac >= 1.f - KINDA_SMALL_NUMBER) S.Beats.Add({ ERole::BowlingTeam, EAction::Clap, ERole::Striker, 0.f, 1.f });
	}
	if (S.Duration <= 0.f) return S;
	Scale(S);

	// Graphic: the most specific card for what the viewer needs next.
	if (S.Milestone != EMilestone::None) S.Graphic = EGraphic::Milestone;
	else if (S.Type == EScene::MatchWinBoundary || S.Type == EScene::MatchWinWicket || S.Type == EScene::MatchWinRuns || S.Type == EScene::MatchTied) S.Graphic = EGraphic::Result;
	else if (S.Type == EScene::Wicket) S.Graphic = EGraphic::Wicket;
	else if (S.Type == EScene::LastBall) S.Graphic = EGraphic::Equation;
	else if (S.Type == EScene::InningsBreak || C.bInningsBreak) S.Graphic = EGraphic::Target;
	else if (S.Type == EScene::EndOfOver || S.Type == EScene::Maiden || S.Type == EScene::ExpensiveOver) S.Graphic = EGraphic::OverSummary;

	if (S.Shots.Num()) D.RecentOpeners.Add(S.Shots[0].Kind);
	if (D.RecentOpeners.Num() > 8) D.RecentOpeners.RemoveAt(0);
	return S;
}

FScenePlan TossScene(FDirector& D)
{
	FScenePlan S = Build(D, EScene::Toss, 0.f, nullptr);
	S.Graphic = EGraphic::Toss;
	Scale(S);
	return S;
}

FScenePlan InningsBreakScene(FDirector& D)
{
	FScenePlan S = Build(D, EScene::InningsBreak, 0.f, nullptr);
	S.Graphic = EGraphic::Target;
	Scale(S);
	return S;
}

FScenePlan ResultScene(FDirector& D, bool bTied)
{
	FScenePlan S = Build(D, bTied ? EScene::MatchTied : EScene::Result, 1.f, nullptr);
	S.Graphic = bTied ? EGraphic::Result : EGraphic::PlayerOfMatch;
	Scale(S);
	return S;
}

FPlayerImpact PlayerOfMatch(const FSuperOverMatch& M)
{
	// Impact from real cards only: runs at pace for batters, wickets and economy for the bowler.
	TMap<FString, FPlayerImpact> All;
	for (const FInningsState& I : M.Innings)
	{
		for (int32 B = 0; B < I.Batters.Num(); ++B)
		{
			const FBatterCard& Card = I.Batters[B];
			if (Card.Balls == 0 && Card.Runs == 0) continue;
			FPlayerImpact& P = All.FindOrAdd(FString::Printf(TEXT("%d/%d"), I.BattingTeam, B));
			P.Team = I.BattingTeam;
			P.Batter = B;
			const float StrikeRate = Card.Balls > 0 ? float(Card.Runs) / Card.Balls : 0.f;
			P.Score += Card.Runs + 2.f * Card.Sixes + Card.Fours + 3.f * FMath::Max(0.f, StrikeRate - 1.f);
		}
		if (I.Bowler.Balls > 0)
		{
			FPlayerImpact& P = All.FindOrAdd(FString::Printf(TEXT("%d/bowl"), 1 - I.BattingTeam));
			P.Team = 1 - I.BattingTeam;
			P.bBowler = true;
			P.Score += 12.f * I.Bowler.Wickets + FMath::Max(0.f, 12.f - I.Bowler.Runs);
		}
	}
	FPlayerImpact Best;
	Best.Score = -1.f;
	for (const TPair<FString, FPlayerImpact>& Pair : All)
	{
		const float S = Pair.Value.Score * (Pair.Value.Team == M.Winner ? 1.25f : 1.f);
		if (S > Best.Score) { Best = Pair.Value; Best.Score = S; }
	}
	return Best;
}
}
