// Rolling replay buffer implementation.

#include "CricketReplayBuffer.h"

void FCricketReplayBuffer::Reset()
{
	Frames.Reset();
	Head = 0;
	ActorCount = 0;
	LastRecordedT = -1e9f;
	LastTick = -1;
	EventMarks.Reset();
}

int32 FCricketReplayBuffer::Capacity() const
{
	return FMath::Max(8, int32(Window * Rate) + 2);
}

int64 FCricketReplayBuffer::FootprintBytes() const
{
	// Frame header (ball state) + per-actor pose. Conservative: full capacity.
	const int64 PerActor = int64(sizeof(FVector)) * 2 + int64(sizeof(FQuat));
	const int64 PerFrame = int64(sizeof(float)) + int64(sizeof(FVector)) * 2 + int64(ActorCount) * PerActor;
	return int64(Capacity()) * PerFrame + int64(sizeof(*this));
}

const FReplayBufferFrame* FCricketReplayBuffer::FrameAt(int32 Logical) const
{
	if (Logical < 0 || Logical >= Frames.Num()) return nullptr;
	return &Frames[(Head + Logical) % Frames.Num()];
}

void FCricketReplayBuffer::Record(float BallT, const FVector& BallPos, const FVector& BallVel, const TArray<FReplayActorPose>& Actors)
{
	// Fixed-rate throttle on integer ticks: exact for exact-multiple clocks (2*(1/60) is a hair under
	// 1/30 in float, so a delta accumulator would keep every third sample instead of every second).
	if (BallT < LastRecordedT - 0.25f) LastTick = -1; // a new ball: the first frame always lands
	LastRecordedT = BallT;
	const int64 Tick = FMath::FloorToInt(BallT * FMath::Max(Rate, 1.f) + 1e-3f);
	if (Tick <= LastTick && Frames.Num() > 0) return;
	LastTick = Tick;

	const int32 Keep = FMath::Clamp(FMath::Min(Actors.Num(), MaxActors), 0, MaxActors);
	FReplayBufferFrame F;
	F.BallT = BallT;
	F.BallPos = BallPos;
	F.BallVel = BallVel;
	F.Actors.SetNumUninitialized(Keep);
	for (int32 I = 0; I < Keep; ++I) F.Actors[I] = Actors[I];

	const int32 Cap = Capacity();
	if (Frames.Num() < Cap)
	{
		// Growing phase: append, keep chronological order, Head stays 0.
		Frames.Add(MoveTemp(F));
	}
	else
	{
		// Full: overwrite the oldest, advance the head. No allocation, ever.
		Frames[Head] = MoveTemp(F);
		Head = (Head + 1) % Frames.Num();
	}
	ActorCount = FMath::Max(ActorCount, Keep);
}

bool FCricketReplayBuffer::HasCoverage(float From, float To) const
{
	if (Frames.Num() < 2) return false;
	const float Margin = 0.5f / FMath::Max(Rate, 1.f);
	// Coverage must be CONTIGUOUS: a cut (new ball) inside the range voids it. Frames are stored
	// chronologically, so one backwards step inside the window fails the range.
	const float Lo = EarliestT() - Margin, Hi = LatestT() + Margin;
	if (From < Lo || To > Hi) return false;
	bool bSawFrom = false;
	for (int32 I = 0; I < Frames.Num(); ++I)
	{
		const FReplayBufferFrame* F = FrameAt(I);
		if (I > 0 && F->BallT < FrameAt(I - 1)->BallT - 0.25f)
		{
			// A cut: coverage restarts here.
			bSawFrom = F->BallT <= From + Margin;
			continue;
		}
		if (!bSawFrom && F->BallT >= From - Margin && F->BallT <= To + Margin) bSawFrom = true;
		if (bSawFrom && F->BallT >= To - Margin) return true;
	}
	return false;
}

bool FCricketReplayBuffer::SampleAt(float BallT, FVector& OutBallPos, TArray<FReplayActorPose>& OutActors) const
{
	if (Frames.Num() == 0) return false;
	// Binary search over the chronological ring for the bracketing pair.
	int32 Lo = 0, Hi = Frames.Num() - 1;
	const float First = FrameAt(0)->BallT, Last = FrameAt(Hi)->BallT;
	if (BallT <= First) { const FReplayBufferFrame* F = FrameAt(0); OutBallPos = F->BallPos; OutActors = F->Actors; return true; }
	if (BallT >= Last) { const FReplayBufferFrame* F = FrameAt(Hi); OutBallPos = F->BallPos; OutActors = F->Actors; return true; }
	while (Hi - Lo > 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (FrameAt(Mid)->BallT <= BallT) Lo = Mid; else Hi = Mid;
	}
	const FReplayBufferFrame* A = FrameAt(Lo);
	const FReplayBufferFrame* B = FrameAt(Hi);
	// A cut between the brackets (should not happen inside verified coverage): hold the earlier.
	if (B->BallT < A->BallT) { OutBallPos = A->BallPos; OutActors = A->Actors; return true; }
	const float U = (BallT - A->BallT) / FMath::Max(B->BallT - A->BallT, 1e-6f);
	OutBallPos = FMath::Lerp(A->BallPos, B->BallPos, U);
	const int32 N = FMath::Min(A->Actors.Num(), B->Actors.Num());
	OutActors.SetNumUninitialized(N);
	for (int32 I = 0; I < N; ++I)
	{
		OutActors[I].Position = FMath::Lerp(A->Actors[I].Position, B->Actors[I].Position, U);
		OutActors[I].Rotation = FQuat::Slerp(A->Actors[I].Rotation, B->Actors[I].Rotation, U).GetNormalized();
		OutActors[I].Scale = FMath::Lerp(A->Actors[I].Scale, B->Actors[I].Scale, U);
	}
	return true;
}

void FCricketReplayBuffer::MarkEvent(float BallT, const FString& Tag)
{
	EventMarks.Add(TPair<float, FString>(BallT, Tag));
	if (EventMarks.Num() > 32) EventMarks.RemoveAt(0);
}
