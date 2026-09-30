// Broadcast sequence automation tests: the dead-ball and intro sequences follow the Cricket 26 reference's beat order
// (Docs/BROADCAST_REFERENCE_GAME_MP4.md), every sequence is contiguous, the stingers cover the frame at each camera
// cut, the umpire's signals read, and every presentation camera stands above the turf looking at its subject.
// Pure logic, no world.

#include "Misc/AutomationTest.h"
#include "CricketBroadcastSequence.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CricketBroadcastSequenceTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	using namespace CricketSequence;

	FBallFacts Boundary(int32 Runs, float Replay)
	{
		FBallFacts B;
		B.Boundary = Runs;
		B.bBatContact = true;
		B.bReplay = Replay > 0.f;
		B.ReplaySeconds = Replay;
		return B;
	}

	FBallFacts Wicket(EOut Out, float Replay = 9.f)
	{
		FBallFacts B;
		B.Out = Out;
		B.bBatContact = Out == EOut::Caught;
		B.bReplay = true;
		B.ReplaySeconds = Replay;
		return B;
	}

	/** No gaps, no overlaps, no empty beats. */
	bool Contiguous(const FSequence& S)
	{
		float T = 0.f;
		for (const FSegment& G : S.Segments)
		{
			if (FMath::Abs(G.Start - T) > 1e-4f || G.Duration <= 0.f) return false;
			T = G.End();
		}
		return true;
	}

	int32 IndexOf(const FSequence& S, ESegment Kind)
	{
		for (int32 I = 0; I < S.Segments.Num(); ++I)
			if (S.Segments[I].Kind == Kind) return I;
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSequenceSignals, "CRICKET26.BroadcastSequence.Signals", CricketBroadcastSequenceTests::Flags)
bool FSequenceSignals::RunTest(const FString&)
{
	using namespace CricketBroadcastSequenceTests;
	TestEqual(TEXT("four"), SignalFor(Boundary(4, 0.f)), ESignal::Four);
	TestEqual(TEXT("six"), SignalFor(Boundary(6, 0.f)), ESignal::Six);
	TestEqual(TEXT("out before anything"), SignalFor(Wicket(EOut::Caught)), ESignal::Out);
	FBallFacts Wide;
	Wide.bWide = true;
	TestEqual(TEXT("wide"), SignalFor(Wide), ESignal::Wide);
	FBallFacts NoBall = Boundary(4, 0.f);
	NoBall.bNoBall = true;
	NoBall.Boundary = 0;
	NoBall.RunsRun = 1;
	TestEqual(TEXT("no-ball"), SignalFor(NoBall), ESignal::NoBall);
	FBallFacts Bye;
	Bye.RunsRun = 1;
	TestEqual(TEXT("bye"), SignalFor(Bye), ESignal::Bye);
	Bye.bLegBye = true;
	TestEqual(TEXT("leg bye"), SignalFor(Bye), ESignal::LegBye);
	TestEqual(TEXT("a dot needs no signal"), SignalFor(FBallFacts()), ESignal::None);

	// The umpire's arms: a six raises both hands above the shoulders and holds them; out raises the right hand only;
	// wide spreads them; every signal is down again by the end of its beat.
	const FVector SL(2250.f, 60.f, 145.f), SR(2250.f, 40.f, 145.f), Fwd(-1.f, 0.f, 0.f), Right(0.f, -1.f, 0.f);
	FArms A = SignalArms(ESignal::Six, 1.5f, 3.f, SL, SR, Fwd, Right);
	TestTrue(TEXT("six: both arms up"), A.Weight[0] > 0.99f && A.Weight[1] > 0.99f && A.Hand[0].Z > SL.Z + 50.f && A.Hand[1].Z > SR.Z + 50.f);
	A = SignalArms(ESignal::Out, 1.5f, 3.f, SL, SR, Fwd, Right);
	TestTrue(TEXT("out: the right hand only"), A.Weight[0] == 0.f && A.Weight[1] > 0.99f && A.Hand[1].Z > SR.Z + 45.f);
	A = SignalArms(ESignal::Wide, 1.5f, 3.f, SL, SR, Fwd, Right);
	TestTrue(TEXT("wide: arms spread"), FMath::Abs(A.Hand[0].Y - A.Hand[1].Y) > 100.f);
	A = SignalArms(ESignal::Six, 2.999f, 3.f, SL, SR, Fwd, Right);
	TestTrue(TEXT("arms down at the end"), A.Weight[0] < 0.05f && A.Weight[1] < 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSequenceDeadBall, "CRICKET26.BroadcastSequence.DeadBall", CricketBroadcastSequenceTests::Flags)
bool FSequenceDeadBall::RunTest(const FString&)
{
	using namespace CricketBroadcastSequenceTests;
	for (const EPacing Pacing : { EPacing::Broadcast, EPacing::Quick, EPacing::Full })
	{
		FDirector D;
		D.Pacing = Pacing;
		FBallFacts Dot, OverEnd;
		OverEnd.RunsRun = 1;
		OverEnd.bBatContact = true;
		OverEnd.bOverComplete = true;
		FBallFacts Win = Boundary(4, 6.f);
		Win.bMatchOver = Win.bInningsOver = true;
		const FBallFacts Balls[] = { Dot, Boundary(4, 6.5f), Boundary(6, 7.f), Wicket(EOut::Bowled), Wicket(EOut::Caught), OverEnd, Win };
		for (const FBallFacts& Ball : Balls)
		{
			const FSequence S = BuildDeadBall(D, Ball);
			TestTrue(TEXT("contiguous"), Contiguous(S));
			TestTrue(TEXT("opens on the live follow"), S.Segments.Num() > 0 && S.Segments[0].Kind == ESegment::LiveHold);
			if (!Ball.bReplay) { TestFalse(TEXT("no replay slot without a replay"), S.Has(ESegment::Replay)); continue; }
			TestTrue(TEXT("replay slot sized"), FMath::IsNearlyEqual(S.ReplayDuration(), Ball.ReplaySeconds, 1e-4f));
			const int32 In = IndexOf(S, ESegment::StingerIn), Rp = IndexOf(S, ESegment::Replay), Out = IndexOf(S, ESegment::StingerOut);
			TestTrue(TEXT("stinger, replay, stinger in order"), In != INDEX_NONE && Rp == In + 1 && Out == Rp + 1);
			// The stingers cover the whole frame at each cut: into the replay, and out of it.
			float L = 0.f;
			EStinger K = StingerAt(S, S.ReplayStart() - 0.001f, L);
			TestTrue(TEXT("event stinger closed at the cut"), K == EStinger::Event && StingerLook(K, L).Cover > 0.99f);
			K = StingerAt(S, S.ReplayStart() + 0.001f, L);
			TestTrue(TEXT("the card covers the first replay frame"), K == EStinger::Event && StingerLook(K, L).CardAlpha > 0.99f);
			K = StingerAt(S, S.AfterReplay() - 0.001f, L);
			TestTrue(TEXT("logo stinger closed at the replay's end"), K == EStinger::Logo && StingerLook(K, L).Cover > 0.99f);
			K = StingerAt(S, S.AfterReplay() + 0.001f, L);
			TestTrue(TEXT("the card covers the next shot's first frame"), K == EStinger::Logo && StingerLook(K, L).CardAlpha > 0.99f);
			TestEqual(TEXT("the replay itself is clear"), StingerAt(S, S.ReplayStart() + 2.f, L), EStinger::None);
			// Resizing the replay moves every later beat with it.
			FSequence R = S;
			R.SetReplayDuration(3.f);
			TestTrue(TEXT("resized contiguous"), Contiguous(R));
			TestTrue(TEXT("resized total"), FMath::IsNearlyEqual(R.Duration(), S.Duration() - Ball.ReplaySeconds + 3.f, 1e-3f));
		}
	}

	// The reference's beats for each event.
	FDirector D;
	const FSequence Caught = BuildDeadBall(D, Wicket(EOut::Caught));
	const FSegment* Finger = Caught.Find(ESegment::UmpireSignal);
	TestTrue(TEXT("caught: the umpire's finger"), Finger && Finger->Signal == ESignal::Out);
	TestTrue(TEXT("caught: the celebration"), Caught.Has(ESegment::Celebration));
	const FSegment* Off = Caught.Find(ESegment::WalkOff);
	TestTrue(TEXT("caught: the walk off under the dismissal card, after the replay"), Off && Off->Card == ECard::Dismissal && Off->Start >= Caught.AfterReplay());
	const FSequence Bowled = BuildDeadBall(D, Wicket(EOut::Bowled));
	TestFalse(TEXT("bowled: no finger needed"), Bowled.Has(ESegment::UmpireSignal));
	TestTrue(TEXT("bowled: the broken stumps"), Bowled.Has(ESegment::StumpsClose));
	TestEqual(TEXT("bowled: the WICKET strip over the live picture"), Bowled.Segments[0].Card, ECard::EventStrip);
	const FSequence Six = BuildDeadBall(D, Boundary(6, 7.f));
	const FSegment* SixSignal = Six.Find(ESegment::UmpireSignal);
	TestTrue(TEXT("six: signalled before the replay"), SixSignal && SixSignal->Signal == ESignal::Six && SixSignal->Start < Six.ReplayStart());
	FBallFacts OverEnd;
	OverEnd.RunsRun = 1;
	OverEnd.bOverComplete = true;
	const FSequence Over = BuildDeadBall(D, OverEnd);
	TestTrue(TEXT("end of over: THIS OVER"), Over.Has(ESegment::ThisOver) && Over.Find(ESegment::ThisOver)->Card == ECard::ThisOver);
	FBallFacts Last = Boundary(4, 6.f);
	Last.bMatchOver = Last.bInningsOver = true;
	const FSequence Won = BuildDeadBall(D, Last);
	TestTrue(TEXT("match won: the captain, then the result bar"), Won.Has(ESegment::WinCaptain) && Won.Segments.Last().Card == ECard::Result);
	FBallFacts InningsEnd = Boundary(4, 6.f);
	InningsEnd.bInningsOver = true;
	InningsEnd.bOverComplete = true;
	const FSequence Break = BuildDeadBall(D, InningsEnd);
	TestTrue(TEXT("innings end: the pair walk off, no THIS OVER"), Break.Has(ESegment::PairWalkOff) && !Break.Has(ESegment::ThisOver));

	// Variety: a dozen fours do not all play the same way (anti-repetition), but every one signals.
	int32 Reactions = 0;
	for (int32 I = 0; I < 12; ++I)
	{
		const FSequence S = BuildDeadBall(D, Boundary(4, 6.f));
		TestTrue(TEXT("every four is signalled"), S.Has(ESegment::UmpireSignal));
		Reactions += S.Has(ESegment::BatterReaction) || S.Has(ESegment::BowlerReaction);
	}
	TestTrue(TEXT("fours vary"), Reactions >= 4 && Reactions <= 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSequenceIntro, "CRICKET26.BroadcastSequence.Intro", CricketBroadcastSequenceTests::Flags)
bool FSequenceIntro::RunTest(const FString&)
{
	using namespace CricketBroadcastSequenceTests;
	FDirector D;
	FIntroFacts Start;
	Start.bToss = Start.bInningsStart = true;
	const FSequence S = BuildIntro(D, Start);
	TestTrue(TEXT("toss contiguous"), Contiguous(S));
	const int32 Captains = IndexOf(S, ESegment::TossCaptains), Coin = IndexOf(S, ESegment::TossCoin), Result = IndexOf(S, ESegment::TossResult);
	TestTrue(TEXT("captains, coin, result in order"), Captains != INDEX_NONE && Coin > Captains && Result > Coin);
	TestEqual(TEXT("pitch conditions with the captains"), S.Segments[Captains].Card, ECard::PitchConditions);
	TestTrue(TEXT("then the opener and the bowler under their cards"), S.Has(ESegment::BatterIntro) && S.Has(ESegment::BowlerIntro)
		&& S.Find(ESegment::BatterIntro)->Card == ECard::Batter && S.Find(ESegment::BowlerIntro)->Card == ECard::Bowler);
	FIntroFacts Mid;
	Mid.bNewBatter = Mid.bNewBowler = true;
	TestEqual(TEXT("a new batter and a new bowler"), BuildIntro(D, Mid).Segments.Num(), 2);
	TestFalse(TEXT("nothing to introduce"), BuildIntro(D, FIntroFacts()).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSequenceCameras, "CRICKET26.BroadcastSequence.Cameras", CricketBroadcastSequenceTests::Flags)
bool FSequenceCameras::RunTest(const FString&)
{
	using namespace CricketBroadcastSequenceTests;
	FShotFrame F;
	F.Subject = FVector(2250.f, 50.f, 0.f);
	F.Facing = FVector(-1.f, 0.f, 0.f);
	F.Second = FVector(2000.f, -150.f, 0.f);
	F.From = FVector(1000.f, -5000.f, 300.f);
	F.Target = FVector(0.f, 0.f, 0.f);
	F.PitchCentre = FVector(1006.f, 0.f, 0.f);
	F.Duration = 3.f;
	for (int32 Shot = 1; Shot < int32(EShot::Count); ++Shot)
		for (const float T : { 0.f, 1.5f, 3.f })
		{
			F.T = T;
			F.Side = Shot % 2 ? 1.f : -1.f;
			const FShotSolution Sol = SolveShot(EShot(Shot), F);
			TestTrue(TEXT("above the turf"), Sol.Location.Z >= 30.f);
			TestTrue(TEXT("looking at something in front of it"), FVector::Dist(Sol.Location, Sol.LookAt) > 80.f);
			TestTrue(TEXT("a broadcast lens"), Sol.FOV > 5.f && Sol.FOV < 70.f);
			TestTrue(TEXT("focus only with an aperture"), Sol.FocusCm <= 0.f || Sol.Aperture > 0.f);
		}
	// The umpire's shot is from in front of them (between them and the striker); a push-in narrows the lens over the beat.
	F.T = 0.f;
	const FShotSolution Start = SolveShot(EShot::UmpireFront, F);
	F.T = 3.f;
	const FShotSolution End = SolveShot(EShot::UmpireFront, F);
	TestTrue(TEXT("umpire from the front"), Start.Location.X < F.Subject.X);
	TestTrue(TEXT("the close-up creeps in"), End.FOV < Start.FOV);
	// Walking stops on arrival, at the pace asked.
	const FVector Arrived = WalkTo(FVector(0.f, 0.f, 90.f), FVector(300.f, 400.f, 90.f), 140.f, 10.f);
	TestTrue(TEXT("arrives"), FMath::IsNearlyEqual(float(Arrived.X), 300.f, 1e-2f) && FMath::IsNearlyEqual(float(Arrived.Y), 400.f, 1e-2f));
	// Cards: the bar grows first, the numbers after it, and the card has faded by the end of the beat.
	const FCardLook C0 = CardLook(0.f, 3.f), C1 = CardLook(1.f, 3.f), C2 = CardLook(2.99f, 3.f);
	TestTrue(TEXT("card reveal"), C0.Bar == 0.f && C1.Bar == 1.f && C1.Body == 1.f && C1.Alpha == 1.f && C2.Alpha < 0.1f);
	return true;
}

#endif
