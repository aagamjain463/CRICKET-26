#include "CricketCommentary.h"

namespace CricketCommentary
{
namespace
{
	const TCHAR* Pick(std::initializer_list<const TCHAR*> Lines, int32 Variant)
	{
		return Lines.begin()[((Variant % int32(Lines.size())) + int32(Lines.size())) % int32(Lines.size())];
	}

	FString Fill(const TCHAR* Line, const FNames& N, const FString& Where)
	{
		return FString(Line).Replace(TEXT("{S}"), *N.Striker).Replace(TEXT("{NS}"), *N.NonStriker).Replace(TEXT("{R}"), *Where).Replace(TEXT("{B}"), *N.BattingTeam);
	}

	FString FillLine(const FLine& L, const FNames& N, const FString& Where)
	{
		return FString(L.Text).Replace(TEXT("{S}"), *N.Striker).Replace(TEXT("{NS}"), *N.NonStriker).Replace(TEXT("{R}"), *Where).Replace(TEXT("{B}"), *N.BattingTeam);
	}

	const TCHAR* Verb(EShotType Shot)
	{
		switch (Shot)
		{
		case EShotType::Drive: return TEXT("driven");
		case EShotType::Loft: return TEXT("lofted");
		case EShotType::Punch: return TEXT("punched");
		case EShotType::Cut: return TEXT("cut");
		case EShotType::Pull: return TEXT("pulled");
		case EShotType::Sweep: return TEXT("swept");
		case EShotType::Flick: return TEXT("flicked");
		case EShotType::Hook: return TEXT("hooked");
		case EShotType::SlogSweep: return TEXT("slog-swept");
		case EShotType::ReverseSweep: return TEXT("reverse-swept");
		case EShotType::Scoop: return TEXT("scooped");
		default: return TEXT("pushed");
		}
	}

	bool IsEdge(EContactZone Z)
	{
		return Z == EContactZone::InsideEdge || Z == EContactZone::OutsideEdge || Z == EContactZone::TopEdge || Z == EContactZone::BottomEdge;
	}

	bool IsMiddled(const FDeliveryResult& R) { return R.Contact.Zone == EContactZone::Middle && R.Contact.Quality >= 0.8f; }
	bool IsMistimed(const FDeliveryResult& R) { return R.Contact.HasContact() && R.Contact.Quality <= 0.35f; }
	bool IsDiving(const FDeliveryResult& R)
	{
		return R.Fielding.bDive || R.Fielding.Action == EFieldAction::CatchDiving;
	}
	bool IsCloseRunOut(const FDeliveryResult& R) { return R.BrokenTime >= 0.f && FMath::Abs(R.HomeMargin) < 0.25f; }

	FLine Make(const TCHAR* Text, const TCHAR* Id, ESpeaker Sp, float Exc, int32 Pri, int32 Cool, const TCHAR* Tags, float Sec, bool bIntr = false)
	{
		FLine L;
		L.Text = Text;
		L.Meta.Id = Id;
		L.Meta.Speaker = Sp;
		L.Meta.Excitement = Exc;
		L.Meta.Priority = Pri;
		L.Meta.CooldownBalls = Cool;
		L.Meta.Tags = Tags;
		L.Meta.EstSeconds = Sec;
		L.Meta.bCanInterrupt = bIntr;
		return L;
	}
	using P = ESpeaker;
}

const TArray<FLine>& SixLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("{S} launches it over {R}. SIX!"), TEXT("Six_Launch_01"), P::PlayByPlay, 0.75f, 2, 4, TEXT("six"), 2.2f),
		Make(TEXT("That's huge! Six over {R}."), TEXT("Six_Huge_01"), P::PlayByPlay, 0.8f, 2, 4, TEXT("six"), 1.8f),
		Make(TEXT("Clean strike from {S}, all the way over {R} for six."), TEXT("Six_Clean_01"), P::PlayByPlay, 0.7f, 2, 4, TEXT("six,middled"), 2.6f),
		Make(TEXT("Middled it, and it's gone all the way over {R}. Six!"), TEXT("Six_Middled_01"), P::PlayByPlay, 0.75f, 2, 4, TEXT("six,middled"), 2.4f),
		Make(TEXT("{S} picks the length early and smokes it over {R} for six."), TEXT("Six_Picked_01"), P::PlayByPlay, 0.75f, 2, 4, TEXT("six"), 2.8f),
		Make(TEXT("High towards {R}... and over the rope! Six."), TEXT("Six_High_01"), P::PlayByPlay, 0.8f, 2, 4, TEXT("six"), 2.4f),
		Make(TEXT("Not quite off the middle, but it carries over {R}. Six!"), TEXT("Six_Mistimed_01"), P::PlayByPlay, 0.65f, 2, 4, TEXT("six,mistimed"), 2.6f),
		Make(TEXT("Into the stands over {R}! That's a big six."), TEXT("Six_Big_01"), P::PlayByPlay, 0.85f, 2, 4, TEXT("six,big"), 2.2f),
		Make(TEXT("{S} holds the pose. Six over {R}."), TEXT("Six_Pose_01"), P::PlayByPlay, 0.7f, 2, 4, TEXT("six"), 2.0f),
		Make(TEXT("Launched! {S} sends it sailing over {R} for six."), TEXT("Six_Launched_01"), P::PlayByPlay, 0.8f, 2, 4, TEXT("six"), 2.4f),
		Make(TEXT("Skied it with no timing... and it still clears {R}! Six."), TEXT("Six_Skied_01"), P::PlayByPlay, 0.65f, 2, 4, TEXT("six,mistimed"), 2.6f),
	};
	return L;
}

const TArray<FLine>& FourLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Beautifully %s through {R}. FOUR."), TEXT("Four_Beauty_01"), P::PlayByPlay, 0.6f, 2, 3, TEXT("four"), 2.2f),
		Make(TEXT("%s away past {R}, and nobody is stopping that. Four."), TEXT("Four_NoStop_01"), P::PlayByPlay, 0.6f, 2, 3, TEXT("four"), 2.8f),
		Make(TEXT("Timed sweetly, no need to run. Four past {R}."), TEXT("Four_Timed_01"), P::PlayByPlay, 0.55f, 2, 3, TEXT("four,middled"), 2.4f),
		Make(TEXT("Threaded through {R}. Lovely shot, four."), TEXT("Four_Threaded_01"), P::PlayByPlay, 0.55f, 2, 3, TEXT("four"), 2.2f),
		Make(TEXT("Short and punished, pulled away to {R} for four."), TEXT("Four_Punished_01"), P::PlayByPlay, 0.6f, 2, 3, TEXT("four,short"), 2.4f),
		Make(TEXT("Overpitched, and {S} doesn't miss out. Four through {R}."), TEXT("Four_Full_01"), P::PlayByPlay, 0.55f, 2, 3, TEXT("four,full"), 2.6f),
		Make(TEXT("Crashed away through {R}. Four!"), TEXT("Four_Crashed_01"), P::PlayByPlay, 0.65f, 2, 3, TEXT("four"), 1.8f),
		Make(TEXT("Lovely timing from {S}, four through {R}."), TEXT("Four_Timing_01"), P::PlayByPlay, 0.55f, 2, 3, TEXT("four"), 2.2f),
	};
	return L;
}

const TArray<FLine>& DotLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Straight to the fielder at {R}. No run."), TEXT("Dot_Fielder_01"), P::PlayByPlay, 0.2f, 1, 2, TEXT("dot"), 2.2f),
		Make(TEXT("Well stopped at {R}. Dot ball."), TEXT("Dot_Stopped_01"), P::PlayByPlay, 0.2f, 1, 2, TEXT("dot"), 2.0f),
		Make(TEXT("Good ball, {S} can only push it to {R}. Dot."), TEXT("Dot_Pushed_01"), P::PlayByPlay, 0.25f, 1, 2, TEXT("dot"), 2.4f),
		Make(TEXT("No room at all, squeezed to {R}. No run."), TEXT("Dot_NoRoom_01"), P::PlayByPlay, 0.25f, 1, 2, TEXT("dot"), 2.2f),
		Make(TEXT("{S} finds {R}, and there's no single. Dot."), TEXT("Dot_NoSingle_01"), P::PlayByPlay, 0.2f, 1, 2, TEXT("dot"), 2.2f),
		Make(TEXT("Tight lines, punched to {R}. No run."), TEXT("Dot_Tight_01"), P::PlayByPlay, 0.25f, 1, 2, TEXT("dot"), 2.0f),
	};
	return L;
}

const TArray<FLine>& SingleLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("%s towards {R} for a single."), TEXT("Single_Pushed_01"), P::PlayByPlay, 0.25f, 1, 2, TEXT("single"), 2.2f),
		Make(TEXT("%s out to {R}, quick single taken."), TEXT("Single_Quick_01"), P::PlayByPlay, 0.3f, 1, 2, TEXT("single,quick"), 2.2f),
		Make(TEXT("Worked away to {R} for one."), TEXT("Single_Worked_01"), P::PlayByPlay, 0.25f, 1, 2, TEXT("single"), 1.8f),
		Make(TEXT("Dropped short of {R}, they hurry through. Single."), TEXT("Single_Hurry_01"), P::PlayByPlay, 0.3f, 1, 2, TEXT("single,quick"), 2.4f),
		Make(TEXT("Nudged towards {R}, easy single."), TEXT("Single_Nudged_01"), P::PlayByPlay, 0.2f, 1, 2, TEXT("single"), 2.0f),
		Make(TEXT("Placed into the gap at {R}, one run."), TEXT("Single_Placed_01"), P::PlayByPlay, 0.25f, 1, 2, TEXT("single"), 2.0f),
	};
	return L;
}

const TArray<FLine>& MultiRunLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("%s to {R}, and they come back for "), TEXT("Multi_Back_01"), P::PlayByPlay, 0.3f, 1, 2, TEXT("multi"), 2.4f),
		Make(TEXT("%s into the gap at {R}; they push for "), TEXT("Multi_Gap_01"), P::PlayByPlay, 0.3f, 1, 2, TEXT("multi"), 2.4f),
		Make(TEXT("%s past {R}, and they sprint back for "), TEXT("Multi_Sprint_01"), P::PlayByPlay, 0.35f, 1, 2, TEXT("multi"), 2.4f),
		Make(TEXT("%s hard to {R}, and they run hard for "), TEXT("Multi_Hard_01"), P::PlayByPlay, 0.35f, 1, 2, TEXT("multi"), 2.4f),
	};
	return L;
}

const TArray<FLine>& BowledLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Bowled him! {S} misses and the stumps are sent flying."), TEXT("Bowled_Miss_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,bowled"), 2.6f, true),
		Make(TEXT("Clean bowled. Through the gate, and {S} has to go."), TEXT("Bowled_Gate_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,bowled"), 2.6f, true),
		Make(TEXT("Played on! {S} drags it back onto the stumps."), TEXT("Bowled_PlayedOn_01"), P::PlayByPlay, 0.8f, 3, 3, TEXT("wicket,bowled,played-on"), 2.4f, true),
		Make(TEXT("Too quick, too straight. The stumps are shattered, {S} goes."), TEXT("Bowled_Quick_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,bowled"), 2.8f, true),
		Make(TEXT("Yorker, right at the base! {S} had no answer."), TEXT("Bowled_Yorker_01"), P::PlayByPlay, 0.9f, 3, 3, TEXT("wicket,bowled,yorker"), 2.4f, true),
		Make(TEXT("Dragged on! {S} can't believe it."), TEXT("Bowled_Dragged_01"), P::PlayByPlay, 0.8f, 3, 3, TEXT("wicket,bowled,played-on"), 2.0f, true),
	};
	return L;
}

const TArray<FLine>& CaughtLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("{S} goes for it towards {R}... and it's caught!"), TEXT("Caught_Goes_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,caught"), 2.6f, true),
		Make(TEXT("Up in the air towards {R}, and taken. {S} is gone."), TEXT("Caught_Up_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,caught"), 2.6f, true),
		Make(TEXT("Off the edge... and taken! {S} is caught."), TEXT("Caught_Edge_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,caught,edge"), 2.4f, true),
		Make(TEXT("Straight up, the fielder settles under it at {R}... taken! {S} goes."), TEXT("Caught_Skied_01"), P::PlayByPlay, 0.8f, 3, 3, TEXT("wicket,caught,skied"), 3.0f, true),
		Make(TEXT("Flat and hard, but straight to {R}. Caught, and {S} has to go."), TEXT("Caught_Flat_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,caught"), 2.6f, true),
		Make(TEXT("Sliced in the air to {R}, and taken. {S} is gone."), TEXT("Caught_Sliced_01"), P::PlayByPlay, 0.8f, 3, 3, TEXT("wicket,caught"), 2.4f, true),
		Make(TEXT("Full stretch, what a grab at {R}! {S} is stunned."), TEXT("Caught_Dive_01"), P::PlayByPlay, 0.9f, 3, 3, TEXT("wicket,caught,diving"), 2.4f, true),
		Make(TEXT("Diving across at {R}, and held! Brilliant catch, {S} goes."), TEXT("Caught_Dive_02"), P::PlayByPlay, 0.9f, 3, 3, TEXT("wicket,caught,diving"), 2.6f, true),
	};
	return L;
}

const TArray<FLine>& CaughtBehindLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Thin edge, and the keeper makes no mistake. {S} is caught behind."), TEXT("CaughtBehind_Thin_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,caught-behind,edge"), 2.8f, true),
		Make(TEXT("Through to the keeper! {S} feathered it."), TEXT("CaughtBehind_Feather_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,caught-behind,edge"), 2.2f, true),
		Make(TEXT("Big appeal for caught behind... given! {S} walks."), TEXT("CaughtBehind_Appeal_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,caught-behind"), 2.6f, true),
	};
	return L;
}

const TArray<FLine>& LbwLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Struck on the pad, and the finger goes up. {S} is LBW."), TEXT("Lbw_Pad_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,lbw"), 2.6f, true),
		Make(TEXT("Plumb in front! {S} has to go, leg before."), TEXT("Lbw_Plumb_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,lbw"), 2.2f, true),
		Make(TEXT("Pinned on the crease, no shot offered. LBW, {S} goes."), TEXT("Lbw_Pinned_01"), P::PlayByPlay, 0.8f, 3, 3, TEXT("wicket,lbw"), 2.6f, true),
		Make(TEXT("That looked dead in front, and the umpire agrees. {S} is LBW."), TEXT("Lbw_Dead_01"), P::PlayByPlay, 0.8f, 3, 3, TEXT("wicket,lbw"), 2.8f, true),
	};
	return L;
}

const TArray<FLine>& RunOutLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Run out! {S} is short of the crease."), TEXT("RunOut_Short_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,run-out"), 2.2f, true),
		Make(TEXT("They went for it, and {S} is run out."), TEXT("RunOut_Went_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,run-out"), 2.2f, true),
		Make(TEXT("Direct hit! {S} is well short."), TEXT("RunOut_Direct_01"), P::PlayByPlay, 0.9f, 3, 3, TEXT("wicket,run-out,direct-hit"), 2.0f, true),
		Make(TEXT("Hesitation, and it costs {S} their wicket."), TEXT("RunOut_Hesitation_01"), P::PlayByPlay, 0.8f, 3, 3, TEXT("wicket,run-out,close"), 2.2f, true),
		Make(TEXT("Brilliant work in the deep, and {S} can't get home."), TEXT("RunOut_Fielding_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,run-out"), 2.4f, true),
	};
	return L;
}

const TArray<FLine>& StumpedLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Down the track, beaten, and stumped! {S} is out."), TEXT("Stumped_Beaten_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,stumped"), 2.4f, true),
		Make(TEXT("Beaten in the flight, the keeper whips the bails off. Stumped, {S} goes."), TEXT("Stumped_Flight_01"), P::PlayByPlay, 0.85f, 3, 3, TEXT("wicket,stumped"), 2.8f, true),
		Make(TEXT("Missed it completely, and the keeper does the rest. {S} is stumped."), TEXT("Stumped_Missed_01"), P::PlayByPlay, 0.8f, 3, 3, TEXT("wicket,stumped"), 2.6f, true),
	};
	return L;
}

const TArray<FLine>& HitWicketLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("{S} treads on the stumps. Hit wicket!"), TEXT("HitWicket_Treads_01"), P::PlayByPlay, 0.8f, 3, 3, TEXT("wicket,hit-wicket"), 2.0f, true),
		Make(TEXT("Stepping back, {S} has clipped the stumps. Hit wicket, unfortunate."), TEXT("HitWicket_Clip_01"), P::PlayByPlay, 0.75f, 3, 3, TEXT("wicket,hit-wicket"), 2.6f, true),
	};
	return L;
}

const TArray<FLine>& WideLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Wide. Too far from the batter."), TEXT("Wide_Far_01"), P::PlayByPlay, 0.25f, 1, 2, TEXT("wide"), 1.8f),
		Make(TEXT("Called wide: an extra run and the ball again."), TEXT("Wide_Called_01"), P::PlayByPlay, 0.25f, 1, 2, TEXT("wide"), 2.4f),
		Make(TEXT("Sprayed down the side. Wide."), TEXT("Wide_Sprayed_01"), P::PlayByPlay, 0.25f, 1, 2, TEXT("wide"), 1.8f),
	};
	return L;
}

const TArray<FLine>& EdgeFourLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Off the edge, and it races away past {R} for four."), TEXT("EdgeFour_Races_01"), P::PlayByPlay, 0.55f, 2, 3, TEXT("four,edge"), 2.4f),
		Make(TEXT("Flashed hard, thick edge past {R} for four. Fortunate."), TEXT("EdgeFour_Thick_01"), P::PlayByPlay, 0.55f, 2, 3, TEXT("four,edge,thick"), 2.6f),
		Make(TEXT("Inside edge, and it sneaks past {R} for four."), TEXT("EdgeFour_Inside_01"), P::PlayByPlay, 0.5f, 2, 3, TEXT("four,edge,inside"), 2.4f),
	};
	return L;
}

const TArray<FLine>& OverthrowLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Overthrows! The throw gets away and runs to the boundary."), TEXT("Overthrow_Away_01"), P::PlayByPlay, 0.5f, 2, 3, TEXT("overthrow"), 2.6f),
		Make(TEXT("No backup there, and it's away for four. Overthrows."), TEXT("Overthrow_Backup_01"), P::PlayByPlay, 0.5f, 2, 3, TEXT("overthrow"), 2.4f),
	};
	return L;
}

const TArray<FLine>& BeatenLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Beaten! {S} swings and misses."), TEXT("Beaten_Swing_01"), P::PlayByPlay, 0.35f, 1, 2, TEXT("beaten"), 2.0f),
		Make(TEXT("Past the bat. Good ball."), TEXT("Beaten_GoodBall_01"), P::PlayByPlay, 0.3f, 1, 2, TEXT("beaten"), 1.8f),
		Make(TEXT("Past the outside edge. Lovely ball."), TEXT("Beaten_Outside_01"), P::PlayByPlay, 0.3f, 1, 2, TEXT("beaten"), 2.0f),
		Make(TEXT("Short ball climbs past the edge!"), TEXT("Beaten_Bouncer_01"), P::PlayByPlay, 0.4f, 1, 2, TEXT("beaten,bouncer"), 1.8f),
	};
	return L;
}

const TArray<FLine>& DefendLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Solid defence from {S}."), TEXT("Defend_Solid_01"), P::PlayByPlay, 0.2f, 1, 2, TEXT("dot,defend"), 1.8f),
		Make(TEXT("Blocked, and no run."), TEXT("Defend_Blocked_01"), P::PlayByPlay, 0.2f, 1, 2, TEXT("dot,defend"), 1.6f),
		Make(TEXT("Watchfully blocked by {S}."), TEXT("Defend_Watchful_01"), P::PlayByPlay, 0.2f, 1, 2, TEXT("dot,defend"), 1.8f),
		Make(TEXT("Dead-batted into the pitch. No run."), TEXT("Defend_Dead_01"), P::PlayByPlay, 0.2f, 1, 2, TEXT("dot,defend"), 2.0f),
	};
	return L;
}

const TArray<FLine>& LeaveLines()
{
	static const TArray<FLine> L = {
		Make(TEXT("Left alone."), TEXT("Leave_Alone_01"), P::PlayByPlay, 0.15f, 1, 2, TEXT("dot,leave"), 1.2f),
		Make(TEXT("Shoulders arms, lets it go through."), TEXT("Leave_Shoulders_01"), P::PlayByPlay, 0.15f, 1, 2, TEXT("dot,leave"), 2.0f),
	};
	return L;
}

const TArray<FLine>& AnalysisLines()
{
	// Analyst follow-ups: only spoken as handoffs after the play-by-play call, and only when the
	// NeedsTag matches authoritative match context (director enforces; never guessed).
	static const TArray<FLine> L = {
		Make(TEXT("The plan is clear: attack the stumps and deny room."), TEXT("An_PlanStumps_01"), P::Analyst, 0.3f, 0, 4, TEXT("analysis"), 2.6f),
		Make(TEXT("The field is up, so anything overpitched is asking to be driven."), TEXT("An_FieldUp_01"), P::Analyst, 0.3f, 0, 4, TEXT("analysis"), 2.8f),
		Make(TEXT("A change of pace would not surprise me here."), TEXT("An_Pace_01"), P::Analyst, 0.3f, 0, 4, TEXT("analysis"), 2.2f),
		Make(TEXT("Yorker length from here; anything short is disappearing."), TEXT("An_Yorker_01"), P::Analyst, 0.35f, 0, 4, TEXT("analysis,pressure"), 2.6f),
		Make(TEXT("Good over so far, but one boundary changes the whole equation."), TEXT("An_Equation_01"), P::Analyst, 0.35f, 0, 4, TEXT("analysis,chase"), 2.8f),
		Make(TEXT("The ring is spread now, so the hard-run two becomes the danger."), TEXT("An_Twos_01"), P::Analyst, 0.3f, 0, 4, TEXT("analysis"), 2.8f),
		Make(TEXT("Pressure does strange things to execution at the death."), TEXT("An_Pressure_01"), P::Analyst, 0.35f, 0, 4, TEXT("analysis,pressure"), 2.4f),
		Make(TEXT("That previous ball has set this next one up nicely."), TEXT("An_Setup_01"), P::Analyst, 0.3f, 0, 4, TEXT("analysis"), 2.4f),
	};
	return L;
}

const TArray<FLine>& ResultLines()
{
	// Voiced match-end calls (director-selected; caption path appends Situation() separately).
	static const TArray<FLine> L = {
		Make(TEXT("It's all over! {S} seals it, and {B} win the Super Over."), TEXT("Res_ChaseWin_01"), P::PlayByPlay, 1.0f, 4, 99, TEXT("result,chase-win"), 3.0f),
		Make(TEXT("{S} holds their nerve. {B} home with balls to spare."), TEXT("Res_ChaseWin_02"), P::PlayByPlay, 0.9f, 4, 99, TEXT("result,chase-win"), 2.8f),
		Make(TEXT("Off the last ball, {B} win it!"), TEXT("Res_FinalWin_01"), P::PlayByPlay, 1.0f, 4, 99, TEXT("result,final-ball"), 2.2f, true),
		Make(TEXT("{S} does it! {B} win off the final ball!"), TEXT("Res_FinalWin_02"), P::PlayByPlay, 1.0f, 4, 99, TEXT("result,final-ball"), 2.4f, true),
		Make(TEXT("Defended! {B} hold their nerve and take the Super Over."), TEXT("Res_DefendWin_01"), P::PlayByPlay, 1.0f, 4, 99, TEXT("result,defense"), 2.8f),
		Make(TEXT("They couldn't get there. {B} defend the total."), TEXT("Res_DefendWin_02"), P::PlayByPlay, 0.9f, 4, 99, TEXT("result,defense"), 2.4f),
		Make(TEXT("Scores level! We're going again, another Super Over."), TEXT("Res_Tie_01"), P::PlayByPlay, 1.0f, 4, 99, TEXT("result,tie"), 2.8f, true),
		Make(TEXT("Tied! Nothing between them, and we'll have another Super Over."), TEXT("Res_Tie_02"), P::PlayByPlay, 1.0f, 4, 99, TEXT("result,tie"), 3.0f, true),
	};
	return L;
}

const FLine& PickLine(const TArray<FLine>& Pool, int32 Variant, const FString& AvoidId)
{
	check(Pool.Num() > 0);
	if (Pool.Num() == 1 || AvoidId.IsEmpty()) return Pool[((Variant % Pool.Num()) + Pool.Num()) % Pool.Num()];
	// Step through the rotation until a non-avoided line is found (pools always hold alternatives).
	for (int32 K = 0; K < Pool.Num(); ++K)
	{
		const FLine& L = Pool[(((Variant + K) % Pool.Num()) + Pool.Num()) % Pool.Num()];
		if (L.Meta.Id != AvoidId) return L;
	}
	return Pool[0];
}

namespace
{
	FString HappenedNoDrop(const FDeliveryResult& R, const FDeliveryOutcome& O, const FNames& N, const FString& Where, int32 V);

	// Deterministic rotation tables that keep tag-specific lines (played-on, edge, diving,
	// middled, mistimed) out of the generic rotation so commentary never claims what did not happen.
	int32 RotIdx(const int32* Table, int32 N, int32 V) { return Table[((V % N) + N) % N]; }

	FString Happened(const FDeliveryResult& R, const FDeliveryOutcome& O, const FNames& N, const FString& Where, int32 V)
	{
		const bool bBat = R.Contact.HasContact();
		switch (O.Dismissal)
		{
		case EDismissal::Bowled:
		{
			if (bBat && (R.Contact.Zone == EContactZone::InsideEdge || R.Contact.Zone == EContactZone::BottomEdge))
				return FillLine(PickLine(BowledLines(), 2, FString()), N, Where); // played on
			static const int32 Table[] = { 0, 1, 3, 4, 5 };
			return FillLine(BowledLines()[RotIdx(Table, 5, V)], N, Where);
		}
		case EDismissal::Caught:
			if (R.Fielding.Action == EFieldAction::CatchKeeper)
				return FillLine(PickLine(CaughtBehindLines(), V, FString()), N, Where);
			if (IsDiving(R))
				return FillLine(CaughtLines()[6 + (FMath::Abs(V) % 2)], N, Where);
			if (IsEdge(R.Contact.Zone))
				return FillLine(PickLine(CaughtLines(), 2, FString()), N, Where);
			{
				static const int32 Table[] = { 0, 1, 3, 4, 5 };
				return FillLine(CaughtLines()[RotIdx(Table, 5, V)], N, Where);
			}
		case EDismissal::LBW:
			return FillLine(PickLine(LbwLines(), V, FString()), N, Where);
		case EDismissal::RunOut:
		{
			FNames Out = N;
			Out.Striker = O.bRunOutStriker ? N.Striker : N.NonStriker;
			if (R.Running.bDirectHit)
				return FillLine(PickLine(RunOutLines(), 2, FString()), Out, Where);
			if (IsCloseRunOut(R))
				return FillLine(PickLine(RunOutLines(), 3 + (FMath::Abs(V) % 2), FString()), Out, Where);
			return FillLine(PickLine(RunOutLines(), V % 2 == 0 ? 0 : (V % 4 == 1 ? 1 : 4), FString()), Out, Where);
		}
		case EDismissal::Stumped: return FillLine(PickLine(StumpedLines(), V, FString()), N, Where);
		case EDismissal::HitWicket: return FillLine(PickLine(HitWicketLines(), V, FString()), N, Where);
		default: break;
		}
		if (O.bWide) return FillLine(PickLine(WideLines(), V, FString()), N, Where);

		if (R.Fielding.bCatchChance && !R.Fielding.bCaught)
		{
			static const FString Prefix = TEXT("Put down at {R}! ");
			return Fill(*Prefix, N, Where) + HappenedNoDrop(R, O, N, Where, V);
		}
		const FString V1 = Verb(R.Contact.Shot);
		if (O.bOverthrow) return FillLine(PickLine(OverthrowLines(), V, FString()), N, Where);
		if (O.Boundary == 6)
		{
			if (IsMistimed(R)) return FillLine(SixLines()[6 + (FMath::Abs(V) % 2 == 0 ? 0 : 4)], N, Where);
			if (IsMiddled(R)) return FillLine(SixLines()[2 + (FMath::Abs(V) % 2)], N, Where);
			static const int32 Table[] = { 0, 1, 4, 5, 7, 8, 9 };
			return FillLine(SixLines()[RotIdx(Table, 7, V)], N, Where);
		}
		if (O.Boundary == 4)
		{
			if (bBat && IsEdge(R.Contact.Zone))
			{
				if (R.Contact.Zone == EContactZone::InsideEdge)
					return FillLine(PickLine(EdgeFourLines(), 2, FString()), N, Where);
				return FillLine(PickLine(EdgeFourLines(), FMath::Abs(V) % 2, FString()), N, Where);
			}
			return FillLine(PickLine(FourLines(), V, FString()), N, Where).Replace(TEXT("%s"), *V1);
		}
		if (O.RunsRun > 0)
		{
			const FString Runs = O.RunsRun == 1 ? TEXT("a single") : O.RunsRun == 2 ? TEXT("two") : O.RunsRun == 3 ? TEXT("three") : FString::Printf(TEXT("%d"), O.RunsRun);
			if (!bBat) return (O.bLegBye ? TEXT("Off the pad, and they run ") : TEXT("Past everyone, and they run ")) + Runs + (O.bLegBye ? TEXT(" leg bye.") : TEXT(" bye."));
			if (O.RunsRun == 1) return FillLine(PickLine(SingleLines(), V, FString()), N, Where).Replace(TEXT("%s"), *V1);
			return FillLine(PickLine(MultiRunLines(), V, FString()), N, Where).Replace(TEXT("%s"), *V1) + Runs + TEXT(".");
		}
		if (R.Fielding.bCatchChance && !R.Fielding.bCaught) return TEXT("Put down! No run.");
		if (!bBat)
		{
			if (R.Shot.Shot == EShotType::Leave) return FillLine(PickLine(LeaveLines(), V, FString()), N, Where);
			if (R.bBouncer) return FillLine(PickLine(BeatenLines(), 3, FString()), N, Where);
			return FillLine(PickLine(BeatenLines(), FMath::Abs(V) % 3, FString()), N, Where);
		}
		if (R.Contact.Shot == EShotType::Defend) return FillLine(PickLine(DefendLines(), V, FString()), N, Where);
		return FillLine(PickLine(DotLines(), V, FString()), N, Where);
	}

	// Dropped-catch prefix path: same outcome routing without re-checking the drop.
	FString HappenedNoDrop(const FDeliveryResult& R, const FDeliveryOutcome& O, const FNames& N, const FString& Where, int32 V)
	{
		FDeliveryResult Clean = R;
		Clean.Fielding.bCatchChance = false;
		return Happened(Clean, O, N, Where, V);
	}

	FString Situation(const FDeliveryOutcome& O, const FSuperOverMatch& M, const FNames& N)
	{
		if (M.Phase == EMatchPhase::MatchComplete)
			return M.Winner == M.BattingTeam() ? FString::Printf(TEXT(" And that wins it for %s!"), *N.BattingTeam)
				: FString::Printf(TEXT(" %s fall short, and the match is over."), *N.BattingTeam);
		if (M.bTied && M.Phase == EMatchPhase::InningsBreak) return TEXT(" Scores level! Another Super Over.");
		if (M.Phase == EMatchPhase::InningsBreak) return FString::Printf(TEXT(" %s set a target of %d."), *N.BattingTeam, M.Target);
		FString S = M.bFreeHit ? TEXT(" Free hit to come.") : TEXT("");
		if (M.IsChase() && M.Phase == EMatchPhase::ReadyForDelivery)
			S += M.BallsRemaining() == 1 ? FString::Printf(TEXT(" %d needed off the last ball."), M.RunsRequired())
				: FString::Printf(TEXT(" %d needed from %d."), M.RunsRequired(), M.BallsRemaining());
		return S;
	}
}

FString Region(const FVector& ExitVel, float OffSign)
{
	const float Deg = FMath::RadiansToDegrees(FMath::Atan2(ExitVel.Y * OffSign, ExitVel.X)); // + off side, 0 straight
	const float A = FMath::Abs(Deg);
	if (Deg >= 0.f) return A < 35.f ? TEXT("long-off") : A < 65.f ? TEXT("cover") : A < 100.f ? TEXT("point") : A < 130.f ? TEXT("backward point") : TEXT("third man");
	return A < 35.f ? TEXT("long-on") : A < 70.f ? TEXT("midwicket") : A < 105.f ? TEXT("square leg") : TEXT("fine leg");
}

FString Describe(const FDeliveryResult& R, const FDeliveryOutcome& Outcome, const FSuperOverMatch& After, const FNames& Names, float OffSign, int32 Variant)
{
	FString Line = Happened(R, Outcome, Names, Region(R.Contact.ExitVel, OffSign), Variant);
	if (!Line.IsEmpty()) Line[0] = FChar::ToUpper(Line[0]);
	return (Outcome.bNoBall ? TEXT("No ball! ") : TEXT("")) + Line + Situation(Outcome, After, Names);
}

FString Analyse(const FDeliveryResult& R, const FDeliveryOutcome& Outcome, const FSuperOverMatch& After, const FNames& Names, float OffSign, int32 Variant)
{
	// Analyst handoff: pressure-aware pick, never a factual claim about the ball just bowled.
	const bool bPressure = After.IsChase() && After.Phase == EMatchPhase::ReadyForDelivery && After.RunsRequired() > 6 && After.BallsRemaining() <= 3;
	if (bPressure)
		return FillLine(PickLine(AnalysisLines(), 3 + (FMath::Abs(Variant) % 2 == 0 ? 0 : 3), FString()), Names, Region(R.Contact.ExitVel, OffSign));
	if (Outcome.Boundary == 6 || Outcome.Boundary == 4)
		return FillLine(PickLine(AnalysisLines(), 4 + (FMath::Abs(Variant) % 2), FString()), Names, Region(R.Contact.ExitVel, OffSign));
	return FillLine(PickLine(AnalysisLines(), FMath::Abs(Variant), FString()), Names, Region(R.Contact.ExitVel, OffSign));
}

const TArray<FSpokenName>& Lexicon()
{
	static const TArray<FSpokenName> L = {
		// Default squads (display / spoken / phonetic hint for the voice director).
		{ TEXT("Opener"), TEXT("Opener"), TEXT("OH-puh-nuh") },
		{ TEXT("Finisher"), TEXT("Finisher"), TEXT("FIN-ish-uh") },
		{ TEXT("Allrounder"), TEXT("Allrounder"), TEXT("AWL-rown-duh") },
		{ TEXT("Quick"), TEXT("Quick"), TEXT("KWIK") },
		{ TEXT("Hitter"), TEXT("Hitter"), TEXT("HIT-uh") },
		{ TEXT("Anchor"), TEXT("Anchor"), TEXT("ANG-kuh") },
		{ TEXT("Keeper-bat"), TEXT("Keeper bat"), TEXT("KEE-puh BAT") },
		{ TEXT("Wrist spinner"), TEXT("Wrist spinner"), TEXT("RIST SPIN-uh") },
		{ TEXT("Home XI"), TEXT("Home Eleven"), TEXT("HOHM ih-LEV-un") },
		{ TEXT("Away XI"), TEXT("Away Eleven"), TEXT("uh-WAY ih-LEV-un") },
		// Cricket vocabulary used across the lines.
		{ TEXT("yorker"), TEXT("yorker"), TEXT("YOR-kuh") },
		{ TEXT("googly"), TEXT("googly"), TEXT("GOOG-lee") },
		{ TEXT("leg break"), TEXT("leg break"), TEXT("LEG BRAYK") },
		{ TEXT("off break"), TEXT("off break"), TEXT("OFF BRAYK") },
		{ TEXT("midwicket"), TEXT("midwicket"), TEXT("mid-WIK-it") },
		{ TEXT("third man"), TEXT("third man"), TEXT("THURD MAN") },
		{ TEXT("wicketkeeper"), TEXT("wicketkeeper"), TEXT("WIK-it KEE-puh") },
		{ TEXT("Super Over"), TEXT("Super Over"), TEXT("SOO-puh OH-vuh") },
		{ TEXT("bouncer"), TEXT("bouncer"), TEXT("BOWN-suh") },
		{ TEXT("slower ball"), TEXT("slower ball"), TEXT("SLOH-uh BAWL") },
		{ TEXT("outswing"), TEXT("outswing"), TEXT("OWT-swing") },
		{ TEXT("inswing"), TEXT("inswing"), TEXT("IN-swing") },
		{ TEXT("cover drive"), TEXT("cover drive"), TEXT("KUV-uh DRYV") },
		{ TEXT("LBW"), TEXT("leg before"), TEXT("LEG bee-FOR") },
	};
	return L;
}

FString SpokenFor(const FString& Display)
{
	for (const FSpokenName& E : Lexicon())
		if (E.Display.Equals(Display, ESearchCase::IgnoreCase)) return E.Spoken;
	return Display; // unknown: fall back to display text; HasPronunciation() flags it for QA
}

FString ClipKey(const FString& Text)
{
	FString Key;
	bool bGap = false;
	for (const TCHAR Ch : Text)
	{
		if (Ch == TEXT('\'')) continue;
		if (FChar::IsAlnum(Ch) && Ch < 128)
		{
			if (bGap && !Key.IsEmpty()) Key += TEXT('_');
			Key += FChar::ToLower(Ch);
			bGap = false;
		}
		else bGap = true;
	}
	return Key;
}

bool HasPronunciation(const FString& Display)
{
	for (const FSpokenName& E : Lexicon())
		if (E.Display.Equals(Display, ESearchCase::IgnoreCase)) return true;
	return false;
}
}
