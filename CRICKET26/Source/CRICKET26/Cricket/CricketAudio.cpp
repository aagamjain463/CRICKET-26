#include "CricketAudio.h"

namespace CricketAudio
{
namespace
{
	// A struck resonance: a sine at Hz decaying with time constant Tau.
	float Mode(float T, float Hz, float Tau, float Phase = 0.f) { return FMath::Exp(-T / Tau) * FMath::Sin(2.f * PI * Hz * T + Phase); }

	TArray<int16> ToPcm(TArray<float>& S, float Peak)
	{
		float Max = KINDA_SMALL_NUMBER;
		for (float V : S) Max = FMath::Max(Max, FMath::Abs(V));
		TArray<int16> Out;
		Out.SetNumUninitialized(S.Num());
		for (int32 I = 0; I < S.Num(); ++I) Out[I] = int16(FMath::RoundToInt(S[I] / Max * Peak * 32767.f));
		return Out;
	}
}

TArray<int16> Synthesize(ECue Cue, int32 Seed)
{
	FRandomStream Rng(Seed * 31 + int32(Cue));
	const float Dt = 1.f / SampleRate;
	TArray<float> S;
	switch (Cue)
	{
	case ECue::BatCrack:
		// Willow on leather: a sharp click of noise over two stiff, fast-dying blade modes.
		S.SetNumZeroed(SampleRate * 15 / 100);
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const float T = I * Dt;
			S[I] = 0.6f * Mode(T, 1150.f, 0.020f) + 0.4f * Mode(T, 2700.f, 0.012f, 0.7f) + 0.5f * Rng.FRandRange(-1.f, 1.f) * FMath::Exp(-T / 0.003f);
		}
		return ToPcm(S, 0.9f);
	case ECue::EdgeTick:
		// Off the edge: thin and high, gone almost at once.
		S.SetNumZeroed(SampleRate * 6 / 100);
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const float T = I * Dt;
			S[I] = Mode(T, 3600.f, 0.007f) + 0.3f * Rng.FRandRange(-1.f, 1.f) * FMath::Exp(-T / 0.002f);
		}
		return ToPcm(S, 0.5f);
	case ECue::Bounce:
		// The ball pitching: a dull low thud.
		S.SetNumZeroed(SampleRate / 10);
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const float T = I * Dt;
			S[I] = Mode(T, 170.f, 0.025f) + 0.2f * Rng.FRandRange(-1.f, 1.f) * FMath::Exp(-T / 0.004f);
		}
		return ToPcm(S, 0.6f);
	case ECue::Stumps:
		// The stumps and bails going: three wooden knocks as the stumps and bails are struck in turn.
		S.SetNumZeroed(SampleRate * 4 / 10);
		for (const float At : { 0.f, 0.06f, 0.14f })
			for (int32 I = FMath::RoundToInt(At * SampleRate); I < S.Num(); ++I)
			{
				const float T = I * Dt - At;
				S[I] += (At == 0.f ? 1.f : 0.5f) * (Mode(T, 820.f + At * 2000.f, 0.04f) + 0.5f * Mode(T, 1530.f, 0.02f));
			}
		return ToPcm(S, 0.9f);
	case ECue::Crowd:
	{
		// ponytail: low-passed noise with slow random swells reads as a distant crowd, not as voices;
		// licensed stadium recordings replace it when audio assets are sourced.
		const int32 Loop = SampleRate * 4, Fade = SampleRate / 2;
		TArray<float> Raw;
		Raw.SetNumZeroed(Loop + Fade);
		float Lp = 0.f, Swell = 0.5f, SwellTo = 0.5f;
		for (int32 I = 0; I < Raw.Num(); ++I)
		{
			if (I % (SampleRate / 4) == 0) SwellTo = Rng.FRandRange(0.35f, 1.f);
			Swell += (SwellTo - Swell) * 0.0005f;
			Lp += (Rng.FRandRange(-1.f, 1.f) - Lp) * 0.2f; // about 800 Hz one-pole low-pass
			Raw[I] = Lp * Swell;
		}
		// Crossfade the tail into the head so the loop has no seam.
		S.SetNumZeroed(Loop);
		for (int32 I = 0; I < Loop; ++I)
		{
			const float A = I < Fade ? float(I) / Fade : 1.f;
			S[I] = Raw[I] * A + (I < Fade ? Raw[Loop + I] * (1.f - A) : 0.f);
		}
		return ToPcm(S, 0.5f);
	}
	default:
		return {};
	}
}
}
