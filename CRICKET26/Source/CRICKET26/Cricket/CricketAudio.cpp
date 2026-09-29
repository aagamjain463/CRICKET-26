#include "CricketAudio.h"
#include "CricketCommentary.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

bool EnsureClip(const FString& Text)
{
	const FString Key = CricketCommentary::ClipKey(Text);
	if (Key.IsEmpty()) return false;
	const FString AudioDir = FPaths::ProjectContentDir() / TEXT("Audio/Commentary");
	const FString PcmPath = AudioDir / (Key + TEXT(".pcm"));
	if (FPaths::FileExists(PcmPath)) return true;

#if PLATFORM_MAC
	IFileManager::Get().MakeDirectory(*AudioDir, true);
	FString CleanText = Text;
	CleanText.ReplaceInline(TEXT("\""), TEXT(""));
	CleanText.ReplaceInline(TEXT("`"), TEXT(""));
	CleanText.ReplaceInline(TEXT("$"), TEXT(""));
	CleanText.ReplaceInline(TEXT("\\"), TEXT(""));

	const FString AiffPath = FPaths::ProjectIntermediateDir() / (Key + TEXT(".aiff"));
	const FString Cmd = FString::Printf(TEXT("say -v Daniel -r 185 \"%s\" -o \"%s\" && ffmpeg -y -i \"%s\" -f s16le -ar 22050 -ac 1 \"%s\" > /dev/null 2>&1; rm -f \"%s\""),
		*CleanText, *AiffPath, *AiffPath, *PcmPath, *AiffPath);

	const int32 Ret = system(TCHAR_TO_UTF8(*Cmd));
	return Ret == 0 && FPaths::FileExists(PcmPath);
#else
	return false;
#endif
}

TArray<int16> LoadClip(const FString& Rel, bool bKeep)
{
	static TMap<FString, TArray<int16>> ClipCache;
	if (const TArray<int16>* Cached = ClipCache.Find(Rel)) return *Cached;
	TArray<uint8> Bytes;
	TArray<int16> Pcm;
	if (FFileHelper::LoadFileToArray(Bytes, *(FPaths::ProjectContentDir() / TEXT("Audio") / Rel), FILEREAD_Silent) && Bytes.Num() >= 2)
	{
		Pcm.SetNumUninitialized(Bytes.Num() / 2);
		FMemory::Memcpy(Pcm.GetData(), Bytes.GetData(), Pcm.Num() * sizeof(int16));
	}
	return bKeep ? ClipCache.Add(Rel, MoveTemp(Pcm)) : Pcm;
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
	case ECue::BatMiddle:
		// Perfect middle: brighter and cleaner than BatCrack, with a longer blade ring.
		S.SetNumZeroed(SampleRate * 18 / 100);
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const float T = I * Dt;
			S[I] = 0.65f * Mode(T, 1300.f, 0.030f) + 0.45f * Mode(T, 3100.f, 0.016f, 0.4f) + 0.55f * Rng.FRandRange(-1.f, 1.f) * FMath::Exp(-T / 0.0025f);
		}
		return ToPcm(S, 0.95f);
	case ECue::BatToe:
		// Toe-end: duller, lower, less ring; reads as mishit without any cartoon layer.
		S.SetNumZeroed(SampleRate * 12 / 100);
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const float T = I * Dt;
			S[I] = 0.7f * Mode(T, 620.f, 0.014f) + 0.3f * Mode(T, 1400.f, 0.009f, 0.9f) + 0.35f * Rng.FRandRange(-1.f, 1.f) * FMath::Exp(-T / 0.003f);
		}
		return ToPcm(S, 0.7f);
	case ECue::PadThud:
		// Ball into pad/body: soft and low, no blade ring at all.
		S.SetNumZeroed(SampleRate * 8 / 100);
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const float T = I * Dt;
			S[I] = Mode(T, 220.f, 0.018f) + 0.25f * Rng.FRandRange(-1.f, 1.f) * FMath::Exp(-T / 0.004f);
		}
		return ToPcm(S, 0.55f);
	case ECue::KeeperGlove:
		// Keeper take: short leather snap; sharper for pace (volume, not pitch, carries velocity).
		S.SetNumZeroed(SampleRate * 7 / 100);
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const float T = I * Dt;
			S[I] = 0.6f * Mode(T, 900.f, 0.010f) + 0.5f * Rng.FRandRange(-1.f, 1.f) * FMath::Exp(-T / 0.002f);
		}
		return ToPcm(S, 0.55f);
	case ECue::CatchPop:
		// Outfield catch: duller hand pop with a little give after the snap.
		S.SetNumZeroed(SampleRate * 9 / 100);
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const float T = I * Dt;
			S[I] = 0.6f * Mode(T, 700.f, 0.014f) + 0.3f * Mode(T, 350.f, 0.020f, 0.5f) + 0.4f * Rng.FRandRange(-1.f, 1.f) * FMath::Exp(-T / 0.0025f);
		}
		return ToPcm(S, 0.6f);
	case ECue::ThrowRelease:
		// Throw release: short and subtle by design; never a whoosh.
		S.SetNumZeroed(SampleRate * 8 / 100);
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const float T = I * Dt;
			S[I] = Rng.FRandRange(-1.f, 1.f) * FMath::Exp(-T / 0.012f) * FMath::Sin(PI * I / S.Num());
		}
		return ToPcm(S, 0.3f);
	case ECue::Footstep:
		// Single footfall: very short, very quiet; footsteps must never outrank cricket.
		S.SetNumZeroed(SampleRate * 5 / 100);
		for (int32 I = 0; I < S.Num(); ++I)
		{
			const float T = I * Dt;
			S[I] = Mode(T, 130.f, 0.012f) + 0.2f * Rng.FRandRange(-1.f, 1.f) * FMath::Exp(-T / 0.003f);
		}
		return ToPcm(S, 0.25f);
	default:
		return {};
	}
}

ECue SelectBatCue(EContactZone Zone, float Quality)
{
	switch (Zone)
	{
	case EContactZone::Middle: return Quality >= 0.8f ? ECue::BatMiddle : ECue::BatCrack;
	case EContactZone::InnerHalf:
	case EContactZone::OuterHalf:
	case EContactZone::Upper: return ECue::BatCrack;
	case EContactZone::Toe:
	case EContactZone::BottomEdge: return ECue::BatToe;
	case EContactZone::InsideEdge:
	case EContactZone::OutsideEdge:
	case EContactZone::TopEdge: return ECue::EdgeTick;
	default: return ECue::BatCrack;
	}
}

float BatVolume(EContactZone Zone, float Quality)
{
	const float Q = FMath::Clamp(Quality, 0.f, 1.f);
	switch (Zone)
	{
	case EContactZone::Middle: return 0.5f + 0.5f * Q;
	case EContactZone::InnerHalf:
	case EContactZone::OuterHalf:
	case EContactZone::Upper: return 0.4f + 0.5f * Q;
	case EContactZone::Toe:
	case EContactZone::BottomEdge: return 0.35f + 0.35f * Q;
	case EContactZone::InsideEdge:
	case EContactZone::OutsideEdge:
	case EContactZone::TopEdge: return 0.3f + 0.4f * Q;
	default: return 0.5f;
	}
}

float PitchVolume(float SpeedKph)
{
	// Audible across 80-150 kph without cartoon scaling: ~0.4 to ~0.6.
	return FMath::Clamp(0.25f + SpeedKph / 500.f, 0.3f, 0.65f);
}

float BusTrim(EMixBus Bus)
{
	switch (Bus)
	{
	case EMixBus::Master: return 1.f;
	case EMixBus::Commentary: return 1.f;
	case EMixBus::Crowd: return 0.8f; // the background bed, a little under the voice and the play
	case EMixBus::FieldSfx: return 0.9f;
	case EMixBus::BatBall: return 1.f;
	case EMixBus::PlayerVocal: return 0.8f;
	case EMixBus::Ambience: return 0.5f;
	case EMixBus::Ui: return 0.7f;
	case EMixBus::Music: return 0.6f;
	default: return 1.f;
	}
}

bool ValidateCue(const TArray<int16>& Pcm, float MinPeak)
{
	if (Pcm.Num() < SampleRate / 50) return false;
	int32 Peak = 0;
	for (int16 V : Pcm)
	{
		const int32 A = FMath::Abs(int32(V));
		if (A > 32767) return false;
		Peak = FMath::Max(Peak, A);
	}
	return Peak >= int32(MinPeak * 32767.f);
}
}
