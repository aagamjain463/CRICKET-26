#include "CricketAudioDirector.h"

namespace CricketAudioDirector
{
void FState::Reset()
{
	*this = FState();
}

float PressureLevel(const FSuperOverMatch& M)
{
	if (M.Phase == EMatchPhase::MatchComplete) return 1.f;
	if (!M.IsChase()) return 0.3f + 0.2f * (float(M.Cur().LegalBalls) / float(FMath::Max(1, M.Rules.MaxLegalBalls)));
	if (M.Phase != EMatchPhase::ReadyForDelivery) return 0.5f;
	const int32 Need = M.RunsRequired(), Left = M.BallsRemaining();
	if (Left <= 0) return 1.f;
	if (Left == 1)
	{
		if (Need <= 0) return 0.5f;
		if (Need == 1) return 0.95f;
		if (Need <= 4) return 0.9f;
		if (Need == 6) return 1.f;
		return 0.85f;
	}
	const float Ratio = float(Need) / float(Left);
	if (Ratio >= 2.5f) return 0.85f;
	if (Ratio >= 1.5f) return 0.65f;
	if (Ratio >= 1.f) return 0.5f;
	return 0.35f;
}

float IntensityOf(const FDeliveryResult& R, const FDeliveryOutcome& O, const FSuperOverMatch& After)
{
	float I = 0.25f;
	if (O.RunsRun > 0 && O.Boundary == 0) I = O.RunsRun >= 3 ? 0.45f : 0.35f;
	if (O.Boundary == 4) I = 0.6f;
	if (O.Boundary == 6) I = 0.8f;
	if (O.Dismissal != EDismissal::None) I = 0.85f;
	if (After.Phase == EMatchPhase::MatchComplete || After.bTied) I = 1.f;
	I = FMath::Max(I, PressureLevel(After) * 0.9f);
	(void)R;
	return FMath::Clamp(I, 0.f, 1.f);
}

static void Lift(FState& S, float To, float Now)
{
	S.CrowdTarget = FMath::Max(S.CrowdTarget, To);
	if (To >= 1.f) S.CelebrateUntil = Now + 6.f; // victory swells and sustains
	else if (To >= 0.8f && Now < S.CelebrateUntil) S.CelebrateUntil = Now + 3.f;
}

void OnEvent(FState& S, ECricketEvent E, const FSuperOverMatch& After, float Now)
{
	(void)After;
	switch (E)
	{
	case ECricketEvent::BoundarySix: Lift(S, 1.f, Now); break;
	case ECricketEvent::BoundaryFour:
	case ECricketEvent::Wicket: Lift(S, 0.8f, Now); break;
	case ECricketEvent::MatchWon: Lift(S, 1.f, Now); break;
	case ECricketEvent::MatchTied: Lift(S, 0.9f, Now); break;
	case ECricketEvent::LastBallSituation: S.TensionTarget = FMath::Max(S.TensionTarget, 0.9f); break;
	case ECricketEvent::RequiredRunsChanged: S.TensionTarget = FMath::Max(S.TensionTarget, PressureLevel(After) * 0.7f); break;
	case ECricketEvent::TargetSet:
	case ECricketEvent::InningsCompleted: Lift(S, 0.6f, Now); break;
	default: break;
	}
}

void OnDelivery(FState& S, const FDeliveryResult& R, const FDeliveryOutcome& O, const FSuperOverMatch& After, float Now)
{
	if (O.Boundary == 6) Lift(S, 1.f, Now);
	else if (O.Boundary == 4 || O.Dismissal != EDismissal::None) Lift(S, 0.8f, Now);
	else if (O.RunsRun > 0) Lift(S, 0.5f, Now);
	else if (After.IsChase() && After.Phase == EMatchPhase::ReadyForDelivery && After.BallsRemaining() <= 2 && After.RunsRequired() > 1)
		Lift(S, 0.6f, Now); // dot under pressure: the defending side reacts
	S.TensionTarget = 0.f; // delivered: anticipation resolves into reaction
	// Player vocals (scheduled; silent until studio calls are recorded).
	const FVocalPick V = VocalFor(R, O);
	if (V.Vocal != EVocal::None) QueueVocal(S, V.Vocal, Now + V.AfterContact);
	if (O.Dismissal != EDismissal::None) QueueVocal(S, EVocal::Celebrate, Now + 1.2f);
	else if (After.Phase == EMatchPhase::MatchComplete) QueueVocal(S, EVocal::Celebrate, Now + 1.5f);
}

void PreDelivery(FState& S, const FSuperOverMatch& M)
{
	S.TensionTarget = PressureLevel(M) * 0.8f;
}

void OnRelease(FState& S, float Now)
{
	S.DipUntil = Now + 0.3f; // micro-drop around the release so the contact lands
	S.TensionTarget = 0.f;
}

float TickCrowd(FState& S, float Dt, float Now, bool bCommentaryActive, bool bReplay)
{
	S.Now = Now;
	if (Now < S.CelebrateUntil) S.CrowdTarget = FMath::Max(S.CrowdTarget, 1.f);
	// Rise fast on big moments, settle slowly back to the bed: no instant quiet->screaming cuts.
	const float Target = S.CrowdTarget;
	const float Rate = Target > S.CrowdEnergy ? 1.5f : 0.25f;
	S.CrowdEnergy = FMath::FInterpTo(S.CrowdEnergy, Target, Dt, Rate);
	if (Target < S.CrowdEnergy) S.CrowdTarget = S.CrowdEnergy; // decay the target with the level
	else S.CrowdTarget = FMath::FInterpTo(S.CrowdTarget, 0.3f, Dt, 0.25f);
	S.Tension = FMath::FInterpTo(S.Tension, S.TensionTarget, Dt, 1.5f);
	// Ducking: slight, never a mute; huge moments let the crowd dominate regardless.
	float DuckTarget = bCommentaryActive ? CricketAudio::CommentaryDuck : 1.f;
	if (S.CrowdEnergy > 0.85f) DuckTarget = 1.f;
	S.Duck = FMath::FInterpTo(S.Duck, DuckTarget, Dt, 6.f);
	float Level = S.CrowdEnergy + 0.15f * S.Tension; // anticipation lift before big balls
	if (Now < S.DipUntil) Level *= 0.7f;
	if (bReplay) Level *= CricketAudio::ReplayCrowdScale;
	return FMath::Clamp(Level, 0.f, 1.f) * S.Duck * CricketAudio::MixGain;
}

FSfxPick ContactSfx(const FDeliveryResult& R)
{
	FSfxPick P;
	P.Cue = CricketAudio::SelectBatCue(R.Contact.Zone, R.Contact.Quality);
	P.Volume = CricketAudio::BatVolume(R.Contact.Zone, R.Contact.Quality);
	if (R.bPadImpact && !R.Contact.HasContact()) { P.Cue = CricketAudio::ECue::PadThud; P.Volume = 0.5f; }
	return P;
}

FSfxPick KeeperSfx(const FDeliveryResult& R)
{
	FSfxPick P;
	P.Cue = CricketAudio::ECue::KeeperGlove;
	// Velocity affects intensity (volume), never pitch: fast-ball collection sharper, spin softer.
	P.Volume = FMath::Clamp(0.3f + R.SpeedKph / 400.f, 0.3f, 0.6f);
	return P;
}

FSfxPick CatchSfx(const FDeliveryResult& R)
{
	FSfxPick P;
	P.Cue = CricketAudio::ECue::CatchPop;
	P.Volume = FMath::Clamp(0.35f + 0.3f * R.Fielding.CatchDifficulty, 0.35f, 0.65f);
	return P;
}

float ThrowVolume(const FDeliveryResult& R)
{
	(void)R;
	return 0.25f; // subtle by design
}

bool ShouldPlay(FState& S, CricketAudio::ECue Cue, float Now)
{
	if (!S.bPlayedInit)
	{
		for (float& T : S.LastPlayed) T = -100.f;
		S.bPlayedInit = true;
	}
	float MinGap = 0.1f;
	switch (Cue)
	{
	case CricketAudio::ECue::Footstep: MinGap = 0.25f; break;
	case CricketAudio::ECue::ThrowRelease: MinGap = 0.3f; break;
	case CricketAudio::ECue::KeeperGlove:
	case CricketAudio::ECue::CatchPop: MinGap = 0.2f; break;
	case CricketAudio::ECue::Stumps: MinGap = 0.3f; break;
	case CricketAudio::ECue::Bounce: MinGap = 0.15f; break;
	default: MinGap = 0.08f; break;
	}
	return Now - S.LastPlayed[int32(Cue)] >= MinGap;
}

void MarkPlayed(FState& S, CricketAudio::ECue Cue, float Now)
{
	if (!S.bPlayedInit)
	{
		for (float& T : S.LastPlayed) T = -100.f;
		S.bPlayedInit = true;
	}
	S.LastPlayed[int32(Cue)] = Now;
}

FVocalPick VocalFor(const FDeliveryResult& R, const FDeliveryOutcome& O)
{
	// Appeals first: HOWZAT on any pad-impact LBW shout or edge-to-keeper chance.
	if (R.bPadImpact && (O.Dismissal == EDismissal::LBW || O.Dismissal == EDismissal::None))
		return { EVocal::Howzat, 0.4f };
	if (R.Fielding.bCatchChance && R.Fielding.Action == EFieldAction::CatchKeeper)
		return { EVocal::Howzat, 0.3f };
	if (R.Running.bSentBack) return { EVocal::No, FMath::Max(0.2f, R.Running.SentBackAt) };
	if (R.Running.Attempted > 0) return { EVocal::Run, 0.3f };
	if (R.Fielding.bCatchChance && !R.Fielding.bCaught) return { EVocal::Frustrated, 0.8f };
	if (O.Dismissal == EDismissal::RunOut && R.Running.ThrowRelease > 0.f) return { EVocal::CatchCall, FMath::Max(0.2f, R.Running.ThrowRelease) };
	return { EVocal::None, 0.f };
}

bool QueueVocal(FState& S, EVocal V, float At)
{
	if (V == EVocal::None) return false;
	// Celebrations outrank calls; a pending call never blocks a celebration.
	if (S.bVocalPending && S.PendingVocal == EVocal::Celebrate && V != EVocal::Celebrate) return false;
	S.bVocalPending = true;
	S.PendingVocal = V;
	S.VocalAt = At;
	return true;
}

EVocal PollVocal(FState& S, float Now)
{
	if (!S.bVocalPending || Now < S.VocalAt) return EVocal::None;
	S.bVocalPending = false;
	return S.PendingVocal;
}
}
