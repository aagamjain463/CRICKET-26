// The broadcast sequence in the match: builds the dead-ball sequence and the pre-ball intros from the settled match,
// and plays them (who moves where, the umpire's arms and the celebrations, the presentation camera). The order and
// timing come from CricketBroadcastSequence (pure, tested); this file only maps its beats onto the actors.
// Docs/BROADCAST_SEQUENCE.md describes the whole flow.

#include "SuperOverGameMode.h"
#include "CricketAnimInstance.h"
#include "CricketStadium.h"
#include "CRICKET26.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace SuperOverSequencePrivate
{
	FVector SeqCm(const FVector& M) { return M * 100.f; } // simulation metres -> Unreal cm

	CricketSequence::EOut SeqOutOf(EDismissal D)
	{
		using CricketSequence::EOut;
		switch (D)
		{
		case EDismissal::Bowled: return EOut::Bowled;
		case EDismissal::Caught: return EOut::Caught;
		case EDismissal::LBW: return EOut::LBW;
		case EDismissal::RunOut: return EOut::RunOut;
		case EDismissal::Stumped: return EOut::Stumped;
		case EDismissal::HitWicket: return EOut::HitWicket;
		default: return EOut::None;
		}
	}

	FString SeqStrikeRate(int32 Runs, int32 Balls) { return Balls > 0 ? FString::Printf(TEXT("%.1f"), 100.f * Runs / Balls) : FString(TEXT("-")); }

	/** Where the batters leave the field: the pavilion gate, square of the pitch on the -Y side. */
	FVector SeqPavilionGate() { return SeqCm(FVector(CricketGeo::PitchLength * 0.5f, -CricketGeo::BoundaryRadius, 0.f)); }

	FVector SeqGround(const AActor* A) { FVector P = A->GetActorLocation(); P.Z = 0.f; return P; }

	/**
	 * A stand section for a crowd cutaway: toward where the ball went when it went somewhere, otherwise seeded, and
	 * never behind the sightscreens (the stands there are empty).
	 */
	void SeqCrowdShot(float Seed, const FVector2D& Prefer, FVector& OutFrom, FVector& OutAt)
	{
		float Angle = !Prefer.IsNearlyZero() ? FMath::Atan2(Prefer.Y, Prefer.X) : FMath::Frac(FMath::Sin(Seed * 12.9898f) * 43758.5453f) * 2.f * PI;
		// At least 25 degrees off the pitch's line.
		const float MinOff = FMath::DegreesToRadians(25.f);
		const float S = FMath::Sin(Angle), C = FMath::Cos(Angle);
		if (FMath::Abs(S) < FMath::Sin(MinOff)) Angle = FMath::Atan2((S >= 0.f ? 1.f : -1.f) * FMath::Sin(MinOff), C >= 0.f ? FMath::Cos(MinOff) : -FMath::Cos(MinOff));
		const FVector Dir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
		const FVector Centre = SeqCm(CricketGeo::PitchCentre());
		OutFrom = Centre + Dir * (CricketGeo::BoundaryRadius - 8.f) * 100.f + FVector(0.f, 0.f, 220.f);
		OutAt = Centre + Dir * (CricketGeo::BoundaryRadius + CricketStadium::StandRadiusOffset + 7.f) * 100.f + FVector(0.f, 0.f, 600.f);
	}

	/** The coin's flight in the toss beat: flicked up, hanging in slow motion, falling to the turf (world cm, degrees). */
	void SeqCoinAt(const FVector& Hand, float T, FVector& OutPos, float& OutSpin)
	{
		const float Rise = 0.7f, Hang = 3.3f, Land = 4.f;
		FVector P = Hand;
		if (T < Rise)
		{
			const float K = T / Rise;
			P.Z += 250.f * (1.f - (1.f - K) * (1.f - K));
			OutSpin = 1440.f * K;
		}
		else if (T < Hang)
		{
			const float K = (T - Rise) / (Hang - Rise);
			P.Z += 250.f + 40.f * FMath::Sin(PI * K);
			OutSpin = 1440.f + 160.f * (T - Rise);
		}
		else
		{
			const float K = FMath::Clamp((T - Hang) / (Land - Hang), 0.f, 1.f);
			P.Z = FMath::Lerp(Hand.Z + 250.f, 1.f, K * K);
			OutSpin = K < 1.f ? 1440.f + 160.f * (Hang - Rise) + 900.f * K : 0.f;
		}
		OutPos = P;
	}
}

void ASuperOverGameMode::BuildSequenceProps()
{
	// The toss coin, and for THIS OVER the pitch's length zones (the HUD's LengthName bands) and a marker for each ball.
	Coin = Spawn(CylinderMesh, FVector(CricketGeo::PitchLength * 0.5f, 0.f, 1.3f), FVector(0.16f, 0.16f, 0.012f), FLinearColor(0.83f, 0.66f, 0.26f));
	Coin->SetActorHiddenInGame(true);
	struct FZone { float From, To; FLinearColor Colour; };
	const FZone Zones[] = {
		{ 0.f, 2.5f, FLinearColor(0.78f, 0.12f, 0.16f) },   // yorker
		{ 2.5f, 5.f, FLinearColor(0.92f, 0.5f, 0.1f) },     // full
		{ 5.f, 7.5f, FLinearColor(0.86f, 0.8f, 0.15f) },    // good length
		{ 7.5f, 9.5f, FLinearColor(0.16f, 0.62f, 0.3f) },   // back of a length
		{ 9.5f, 12.f, FLinearColor(0.2f, 0.36f, 0.85f) } }; // short
	for (const FZone& Z : Zones)
	{
		AStaticMeshActor* Band = Spawn(CubeMesh, FVector(0.5f * (Z.From + Z.To), 0.f, 0.004f), FVector(Z.To - Z.From - 0.04f, 2.f * CricketGeo::PitchHalfWidth, 0.004f), Z.Colour);
		Band->SetActorHiddenInGame(true);
		LengthBands.Add(Band);
	}
	for (int32 I = 0; I < 10; ++I)
	{
		AStaticMeshActor* Marker = Spawn(CylinderMesh, FVector(0.f, 0.f, 0.01f), FVector(0.24f, 0.24f, 0.01f), FLinearColor::White);
		Marker->SetActorHiddenInGame(true);
		OverMarkers.Add(Marker);
	}
}

bool ASuperOverGameMode::InSequenceScene() const
{
	using namespace CricketSequence;
	const float T = SeqTime();
	const int32 I = T >= 0.f ? Seq.IndexAt(T) : INDEX_NONE;
	if (I == INDEX_NONE || IsReplaying() || IsReviewing() || ShowingScorecard()) return false;
	switch (Seq.Segments[I].Kind)
	{
	case ESegment::LiveHold:
	case ESegment::Replay:
		return false;
	case ESegment::StingerIn:
		// The bands close over the beat before; a stinger straight after the live follow leaves it the live camera's.
		return I > 0 && Seq.Segments[I - 1].Kind != ESegment::LiveHold;
	case ESegment::StingerOut:
		// The camera is already on the beat after, under the full card; with none, the delivery camera takes it.
		return Seq.Segments.IsValidIndex(I + 1);
	default:
		return true;
	}
}

AStaticMeshActor* ASuperOverGameMode::SeqActor(CricketSequence::ESubject Who) const
{
	using CricketSequence::ESubject;
	switch (Who)
	{
	case ESubject::Striker: case ESubject::BattingCaptain: return Striker.Get();
	case ESubject::NonStriker: return NonStriker.Get();
	case ESubject::Bowler: case ESubject::BowlingCaptain: return Bowler.Get();
	case ESubject::Keeper: return Fielders.IsValidIndex(0) ? Fielders[0].Get() : nullptr;
	case ESubject::Umpire: return Umpires.IsValidIndex(0) ? Umpires[0].Get() : nullptr;
	case ESubject::Hero: return SeqHero.Get();
	case ESubject::Fielder: return SeqFielder.IsValid() ? SeqFielder.Get() : SeqMate.Get();
	case ESubject::DismissedBatter: return SeqDismissed.Get();
	default: return nullptr;
	}
}

ASuperOverGameMode::FSeqCard ASuperOverGameMode::MakeBatterCard(int32 Team, int32 Batter) const
{
	FSeqCard C;
	C.Team = Team;
	if (!Teams.IsValidIndex(Team) || !Teams[Team].Batters.IsValidIndex(Batter)) return C;
	const FCricketPlayer& P = Teams[Team].Batters[Batter];
	C.Title = P.Name.ToUpper();
	C.Subtitle = FString::Printf(TEXT("%s  •  %s"), *Teams[Team].Name.ToUpper(), P.BatHand == ECricketHand::Right ? TEXT("RIGHT-HAND BAT") : TEXT("LEFT-HAND BAT"));
	// This match's figures (the innings this batter is in).
	int32 Runs = 0, Balls = 0, Fours = 0, Sixes = 0;
	for (const FInningsState& In : Match.Innings)
		if (In.BattingTeam == Team && In.Batters.IsValidIndex(Batter))
		{
			Runs += In.Batters[Batter].Runs; Balls += In.Batters[Batter].Balls; Fours += In.Batters[Batter].Fours; Sixes += In.Batters[Batter].Sixes;
		}
	C.Right = FString::Printf(TEXT("%d (%d)"), Runs, Balls);
	// A real player's T20 career when the squads carry it (Cricsheet); otherwise the match so far.
	const FAuctionPlayer* Career = nullptr;
	if (IsIPLMatch())
		for (const FAuctionPlayer& A : XIPlayers())
			if (A.Name == P.Name) { Career = &A; break; }
	const FAuctionStats* S = !Career ? nullptr : Career->T20.Matches > 0 ? &Career->T20 : Career->Ipl.Matches > 0 ? &Career->Ipl : nullptr;
	if (S)
	{
		C.Labels = { TEXT("MATCHES"), TEXT("RUNS"), TEXT("STRIKE RATE"), TEXT("THIS MATCH") };
		C.Values = { FString::FromInt(S->Matches), FString::FromInt(S->Runs), FString::Printf(TEXT("%.1f"), S->StrikeRate), C.Right };
	}
	else
	{
		C.Labels = { TEXT("RUNS"), TEXT("BALLS"), TEXT("FOURS"), TEXT("SIXES"), TEXT("STRIKE RATE") };
		C.Values = { FString::FromInt(Runs), FString::FromInt(Balls), FString::FromInt(Fours), FString::FromInt(Sixes), SuperOverSequencePrivate::SeqStrikeRate(Runs, Balls) };
	}
	return C;
}

ASuperOverGameMode::FSeqCard ASuperOverGameMode::MakeBowlerCard(int32 Team) const
{
	FSeqCard C;
	C.Team = Team;
	if (!Teams.IsValidIndex(Team)) return C;
	const FCricketPlayer& P = Teams[Team].Bowler;
	C.Title = P.Name.ToUpper();
	const TCHAR* Type = P.BowlerType == EBowlerType::Pace ? TEXT("PACE") : TEXT("SPIN");
	C.Subtitle = FString::Printf(TEXT("%s  •  %s-ARM %s"), *Teams[Team].Name.ToUpper(), P.BowlHand == ECricketHand::Right ? TEXT("RIGHT") : TEXT("LEFT"), Type);
	// The current spell's figures: the innings this side is bowling in.
	const FBowlerCard* Fig = Match.Innings.IsValidIndex(Match.CurrentInnings) && Match.BowlingTeam() == Team ? &Match.Cur().Bowler : nullptr;
	const int32 Balls = Fig ? Fig->Balls : 0, Runs = Fig ? Fig->Runs : 0, Wkts = Fig ? Fig->Wickets : 0;
	C.Right = FString::Printf(TEXT("%d-%d"), Wkts, Runs);
	const FAuctionPlayer* Career = nullptr;
	if (IsIPLMatch())
		for (const FAuctionPlayer& A : XIPlayers())
			if (A.Name == P.Name) { Career = &A; break; }
	const FAuctionStats* S = !Career ? nullptr : Career->T20.Matches > 0 ? &Career->T20 : Career->Ipl.Matches > 0 ? &Career->Ipl : nullptr;
	if (S)
	{
		C.Labels = { TEXT("MATCHES"), TEXT("WICKETS"), TEXT("ECONOMY"), TEXT("THIS MATCH") };
		C.Values = { FString::FromInt(S->Matches), FString::FromInt(S->Wickets), FString::Printf(TEXT("%.2f"), S->Economy),
			FString::Printf(TEXT("%d-%d (%d.%d)"), Wkts, Runs, Balls / 6, Balls % 6) };
	}
	else
	{
		C.Labels = { TEXT("OVERS"), TEXT("RUNS"), TEXT("WICKETS"), TEXT("ECONOMY"), TEXT("PACE") };
		C.Values = { FString::Printf(TEXT("%d.%d"), Balls / 6, Balls % 6), FString::FromInt(Runs), FString::FromInt(Wkts),
			Balls > 0 ? FString::Printf(TEXT("%.2f"), 6.f * Runs / Balls) : FString(TEXT("-")), FString::Printf(TEXT("%.0f KM/H"), P.PaceKph) };
	}
	return C;
}

void ASuperOverGameMode::BuildDeadBallSequence(const FDeliveryOutcome& Outcome, const TArray<ECricketEvent>& Events, int32 PreInnings, int32 PreOverLogStart)
{
	using namespace CricketSequence;
	using namespace SuperOverSequencePrivate;
	Seq = FSequence();
	bSeqIntro = false;
	SeqLastIndex = INDEX_NONE;
	bStrikerFree = false;
	SeqMoves.Reset();
	if (InReel() || !Match.Innings.IsValidIndex(PreInnings)) return;
	const FInningsState& Was = Match.Innings[PreInnings];

	FBallFacts B;
	B.RunsRun = Outcome.RunsRun;
	B.Boundary = Outcome.Boundary;
	B.bBatContact = Outcome.bBatContact;
	B.bWide = Outcome.bWide;
	B.bNoBall = Outcome.bNoBall;
	B.bLegBye = Outcome.bLegBye;
	B.Out = SeqOutOf(Outcome.Dismissal);
	const FFieldingOutcome& Fd = Result.Fielding;
	const bool bFielder = Ctx.Field.IsValidIndex(Fd.Fielder);
	B.bKeeperCatch = Outcome.Dismissal == EDismissal::Caught && bFielder && Ctx.Field[Fd.Fielder].bKeeper;
	B.bCaughtAndBowled = Outcome.Dismissal == EDismissal::Caught && bFielder && Ctx.Field[Fd.Fielder].bBowler;
	const EContactZone Zone = Result.Contact.Zone;
	B.bEdge = Result.Contact.HasContact() && (Zone == EContactZone::InsideEdge || Zone == EContactZone::OutsideEdge
		|| Zone == EContactZone::TopEdge || Zone == EContactZone::BottomEdge);
	B.bBeaten = !Result.Contact.HasContact() && Outcome.Dismissal == EDismissal::None && !Outcome.bWide && Outcome.RunsRun == 0;
	B.bDroppedCatch = Fd.bCatchChance && !Fd.bCaught;
	B.bMatchOver = Match.Phase == EMatchPhase::MatchComplete;
	B.bTied = Match.bTied;
	B.bInningsOver = B.bMatchOver || Match.Phase == EMatchPhase::InningsBreak;
	B.bOverComplete = !B.bInningsOver && Was.OverLogStart != PreOverLogStart;
	// A review (ball tracking) plays in the replay's slot, after the replay if there is one, between the stingers.
	B.bReplay = bReplayThis || bReviewThis;
	B.ReplaySeconds = (bReplayThis ? ReplayTotalTime() : 0.f) + (bReviewThis ? ReviewTime : 0.f);
	B.bFieldedDeep = bFielder && FVector2D::Distance(FVector2D(Fd.FieldPos), FVector2D(CricketGeo::PitchCentre())) > 35.f;
	B.bMilestone = Events.Contains(ECricketEvent::MatchWon);
	Seq = BuildDeadBall(SeqDirector, B);
	// The replay starts where the sequence puts it; without one the legacy gap stands (the third umpire's verdict uses it).
	ReplayDelay = B.bReplay ? Seq.ReplayStart() : ReplayDelayMin;

	// Who the beats are about.
	AStaticMeshActor* Taker = !bFielder ? nullptr : Ctx.Field[Fd.Fielder].bBowler ? Bowler.Get() : Fielders.IsValidIndex(Fd.Fielder) ? Fielders[Fd.Fielder].Get() : nullptr;
	AStaticMeshActor* Hero = Bowler.Get();
	switch (Outcome.Dismissal)
	{
	case EDismissal::Caught: case EDismissal::RunOut: Hero = Taker ? Taker : Bowler.Get(); break;
	case EDismissal::Stumped: Hero = Fielders.IsValidIndex(0) ? Fielders[0].Get() : Bowler.Get(); break;
	default: break;
	}
	bSeqBattingWon = B.bMatchOver && !B.bTied && Match.Winner == Was.BattingTeam;
	if (bSeqBattingWon) Hero = Striker.Get();
	SeqHero = Hero;
	// The team-mate nearest the hero shares the celebration.
	AStaticMeshActor* Mate = nullptr;
	float Best = TNumericLimits<float>::Max();
	auto Consider = [&](AStaticMeshActor* A)
	{
		if (!A || A == Hero || A->IsHidden()) return;
		const float D = FVector::Dist(A->GetActorLocation(), Hero->GetActorLocation());
		if (D < Best) { Best = D; Mate = A; }
	};
	if (bSeqBattingWon) Consider(NonStriker.Get());
	else
	{
		for (const TObjectPtr<AStaticMeshActor>& F : Fielders) Consider(F.Get());
		Consider(Bowler.Get());
	}
	SeqMate = Mate;
	SeqFielder = B.Out != EOut::None || bSeqBattingWon ? Mate : Taker;
	SeqDismissed = Outcome.Dismissal == EDismissal::RunOut && !Outcome.bRunOutStriker ? NonStriker.Get() : Striker.Get();

	// What the stinger and the strip say, in whose colours.
	EventWord = B.Out != EOut::None ? TEXT("WICKET") : B.Boundary == 6 ? TEXT("SIX") : B.Boundary == 4 ? TEXT("FOUR")
		: B.bDroppedCatch ? TEXT("DROPPED") : bReviewThis ? TEXT("REVIEW") : B.bEdge ? TEXT("EDGE") : TEXT("REPLAY");
	EventTeam = B.Out != EOut::None ? 1 - Was.BattingTeam : Was.BattingTeam;

	// The dismissal card: name, how out, runs (balls), then the innings in numbers.
	if (B.Out != EOut::None)
	{
		const int32 OutIdx = Outcome.Dismissal == EDismissal::RunOut && !Outcome.bRunOutStriker ? PreNonStrikerIdx : PreStrikerIdx;
		CardDismissal = FSeqCard();
		CardDismissal.Team = Was.BattingTeam;
		if (Teams.IsValidIndex(Was.BattingTeam) && Teams[Was.BattingTeam].Batters.IsValidIndex(OutIdx) && Was.Batters.IsValidIndex(OutIdx))
		{
			const FBatterCard& Card = Was.Batters[OutIdx];
			const FString BowlerName = Teams[1 - Was.BattingTeam].Bowler.Name;
			CardDismissal.Title = Teams[Was.BattingTeam].Batters[OutIdx].Name.ToUpper();
			switch (Outcome.Dismissal)
			{
			case EDismissal::Bowled: CardDismissal.Subtitle = FString::Printf(TEXT("b %s"), *BowlerName); break;
			case EDismissal::LBW: CardDismissal.Subtitle = FString::Printf(TEXT("lbw b %s"), *BowlerName); break;
			case EDismissal::Stumped: CardDismissal.Subtitle = FString::Printf(TEXT("st (wk) b %s"), *BowlerName); break;
			case EDismissal::HitWicket: CardDismissal.Subtitle = FString::Printf(TEXT("hit wicket b %s"), *BowlerName); break;
			case EDismissal::RunOut: CardDismissal.Subtitle = TEXT("run out"); break;
			default:
				CardDismissal.Subtitle = B.bCaughtAndBowled ? FString::Printf(TEXT("c & b %s"), *BowlerName)
					: B.bKeeperCatch ? FString::Printf(TEXT("c (wk) b %s"), *BowlerName) : FString::Printf(TEXT("c b %s"), *BowlerName);
				break;
			}
			CardDismissal.Right = FString::Printf(TEXT("%d (%d)"), Card.Runs, Card.Balls);
			CardDismissal.Labels = { TEXT("BALLS"), TEXT("STRIKE RATE"), TEXT("FOURS"), TEXT("SIXES"), TEXT("FALL OF WICKET") };
			CardDismissal.Values = { FString::FromInt(Card.Balls), SeqStrikeRate(Card.Runs, Card.Balls), FString::FromInt(Card.Fours),
				FString::FromInt(Card.Sixes), FString::Printf(TEXT("%d/%d"), Was.Wickets, Was.Runs) };
		}
	}

	// The result bar under the closing beats.
	if (B.bMatchOver)
	{
		CardResult = FSeqCard();
		if (B.bTied || !Teams.IsValidIndex(Match.Winner))
		{
			CardResult.Title = TEXT("SCORES LEVEL");
			CardResult.Team = Was.BattingTeam;
		}
		else
		{
			CardResult.Team = Match.Winner;
			const FString Winner = Teams[Match.Winner].Name.ToUpper();
			if (Match.Winner == Was.BattingTeam)
			{
				const int32 Left = Match.Rules.MaxWickets - Was.Wickets;
				CardResult.Title = FString::Printf(TEXT("%s WON BY %d WICKET%s"), *Winner, Left, Left == 1 ? TEXT("") : TEXT("S"));
			}
			else
			{
				const int32 By = FMath::Max(1, Match.Target - 1 - Was.Runs);
				CardResult.Title = FString::Printf(TEXT("%s WON BY %d RUN%s"), *Winner, By, By == 1 ? TEXT("") : TEXT("S"));
			}
		}
	}

	// THIS OVER: the finished over's log, and where each ball pitched on the length bands.
	ThisOverLog.Reset();
	ThisOverLengths.Reset();
	if (B.bOverComplete)
	{
		for (int32 I = PreOverLogStart; I < Was.BallLog.Num(); ++I) ThisOverLog.Add(Was.BallLog[I]);
		const int32 First = FMath::Max(0, Marks.Num() - ThisOverLog.Num());
		for (int32 I = First; I < Marks.Num(); ++I)
		{
			const FBallMark& M = Marks[I];
			ThisOverLengths.Add(M.bPitched ? float(M.Pitch.X) : -1.f);
			const int32 K = I - First;
			if (OverMarkers.IsValidIndex(K) && M.bPitched)
			{
				OverMarkers[K]->SetActorLocation(SeqCm(FVector(M.Pitch.X, M.Pitch.Y, 0.012f)));
				Paint(OverMarkers[K], M.bWicket ? FLinearColor(0.9f, 0.1f, 0.12f) : M.Runs >= 4 ? FLinearColor(0.95f, 0.75f, 0.1f) : FLinearColor(0.95f, 0.95f, 0.95f));
			}
		}
	}
}

void ASuperOverGameMode::UpdateIntro(float Dt)
{
	using namespace CricketSequence;
	if (DPhase != EDeliveryPhase::Waiting)
	{
		if (bSeqIntro) { Seq = FSequence(); bSeqIntro = false; bStrikerFree = false; SeqMoves.Reset(); }
		return;
	}
	if (bSeqIntro && Seq.IsValid())
	{
		const float Was = SeqClock;
		SeqClock += Dt;
		if (Was < Seq.Duration() && SeqClock >= Seq.Duration()) EndIntro();
		return;
	}
	if (!bIntros || Match.Phase != EMatchPhase::ReadyForDelivery || IsAwaitingPick() || InReel() || !Match.Innings.IsValidIndex(Match.CurrentInnings)) return;
	const FInningsState& In = Match.Cur();
	const int32 InningsKey = Match.SuperOverNumber * 16 + Match.CurrentInnings;
	// The new batter is whichever of the pair has not faced yet (the batters may have crossed as the wicket fell).
	const int32 NewIdx = In.Batters.IsValidIndex(In.Striker) && In.Batters[In.Striker].Balls == 0 && In.Batters[In.Striker].HowOut == EDismissal::None ? In.Striker
		: In.Batters.IsValidIndex(In.NonStriker) && In.Batters[In.NonStriker].Balls == 0 && In.Batters[In.NonStriker].HowOut == EDismissal::None ? In.NonStriker : INDEX_NONE;
	const int32 BatterKey = NewIdx == INDEX_NONE ? INDEX_NONE : InningsKey * 64 + NewIdx;
	const int32 BowlerKey = InningsKey * 1000 + In.LegalBalls / 6;
	FIntroFacts F;
	F.bToss = !bTossShown && Match.CurrentInnings == 0 && In.Deliveries == 0;
	F.bInningsStart = In.Deliveries == 0 && InningsKey != IntroInningsKey;
	F.bNewBatter = !F.bInningsStart && In.Wickets > 0 && BatterKey != INDEX_NONE && BatterKey != IntroBatterKey;
	F.bNewBowler = !F.bInningsStart && In.LegalBalls > 0 && In.LegalBalls % 6 == 0 && BowlerKey != IntroBowlerKey;
	IntroInningsKey = InningsKey;
	if (BatterKey != INDEX_NONE || F.bInningsStart) IntroBatterKey = BatterKey;
	IntroBowlerKey = BowlerKey;
	if (!F.bToss && !F.bInningsStart && !F.bNewBatter && !F.bNewBowler) return;

	if (F.bToss)
	{
		// The toss always agrees with who bats first: the winner chose to bat, or the loser was sent in.
		bTossShown = true;
		FRandomStream TossRng(MatchSeed * 7 + 11);
		TossWinner = TossRng.RandRange(0, 1);
		bTossChoseBat = TossWinner == Match.BattingTeam();
		const CricketStadium::FVenue& V = CricketStadium::Venue(VenueIndex);
		CardToss = FSeqCard();
		CardToss.Team = TossWinner;
		CardToss.Title = Teams.IsValidIndex(TossWinner) ? FString::Printf(TEXT("%s WON THE TOSS"), *Teams[TossWinner].Name.ToUpper()) : FString();
		CardToss.Subtitle = bTossChoseBat ? TEXT("AND CHOSE TO BAT") : TEXT("AND CHOSE TO BOWL");
		CardToss.Right = FString(V.Name).ToUpper();
		CardToss.Labels = { TEXT("PITCH"), TEXT("WEAR"), TEXT("SKY"), TEXT("PLAY") };
		CardToss.Values = { V.Pitch == EPitchType::Green ? TEXT("GREEN") : V.Pitch == EPitchType::Dusty ? TEXT("DUSTY") : TEXT("FLAT"),
			V.Wear < 0.25f ? TEXT("FRESH") : V.Wear < 0.6f ? TEXT("WORN") : TEXT("WEARING"),
			V.Cloud < 0.3f ? TEXT("CLEAR") : V.Cloud < 0.7f ? TEXT("CLOUDY") : TEXT("OVERCAST"), V.bNight ? TEXT("NIGHT") : TEXT("DAY") };
	}
	Seq = BuildIntro(SeqDirector, F);
	if (!Seq.IsValid()) return;
	// The intro's batter is whoever walks in (at the non-striker's end if the pair crossed).
	for (FSegment& G : Seq.Segments)
		if (G.Kind == ESegment::BatterIntro && !F.bInningsStart && NewIdx == In.NonStriker) G.Subject = ESubject::NonStriker;
	CardBatter = MakeBatterCard(Match.BattingTeam(), F.bInningsStart ? In.Striker : NewIdx);
	CardBowler = MakeBowlerCard(Match.BowlingTeam());
	bSeqIntro = true;
	SeqClock = 0.f;
	SeqLastIndex = INDEX_NONE;
	SeqMoves.Reset();
}

void ASuperOverGameMode::EndIntro()
{
	// Everyone back to their marks under a cut to the delivery camera; the bowler sets off a moment later.
	SeqClock = FMath::Max(SeqClock, Seq.Duration());
	bStrikerFree = false;
	SeqMoves.Reset();
	if (Coin) Coin->SetActorHiddenInGame(true);
	PlaceForDelivery();
	bCutCamera = true;
	BroadcastDirector.Reset(EBroadcastShot::StandardDelivery);
	PhaseTime = 0.f;
}

void ASuperOverGameMode::ApplySequenceMovement()
{
	using namespace CricketSequence;
	using namespace SuperOverSequencePrivate;
	const float ST = SeqTime();
	const int32 Index = ST >= 0.f ? Seq.IndexAt(ST) : INDEX_NONE;
	const FSegment* G = Index != INDEX_NONE ? &Seq.Segments[Index] : nullptr;
	bStrikerFree = false;
	const bool bBands = G && G->Kind == ESegment::ThisOver;
	for (const TObjectPtr<AStaticMeshActor>& Band : LengthBands) if (Band) Band->SetActorHiddenInGame(!bBands);
	for (int32 I = 0; I < OverMarkers.Num(); ++I)
		if (OverMarkers[I]) OverMarkers[I]->SetActorHiddenInGame(!(bBands && ThisOverLengths.IsValidIndex(I) && ThisOverLengths[I] >= 0.f));
	if (Coin && !(G && G->Kind == ESegment::TossCoin)) Coin->SetActorHiddenInGame(true);
	// The replay poses everyone from the buffer; a review owns the scene. Moves start afresh after it.
	if (!G || IsReplaying() || IsReviewing() || G->Kind == ESegment::Replay)
	{
		SeqMoves.Reset();
		SeqMoveIndex = INDEX_NONE;
		return;
	}
	const float T = ST - G->Start;

	// New beat: register who moves where (from where they stand now, so a walk carries on across beats).
	if (Index != SeqMoveIndex)
	{
		SeqMoveIndex = Index;
		auto Move = [&](AStaticMeshActor* A, const FVector& To, float Speed, AActor* FaceAt, bool bKeep = false)
		{
			if (!A || A->IsHidden()) return;
			if (bKeep && SeqMoves.Contains(A)) return;
			FSeqMove M;
			M.From = SeqGround(A);
			M.To = FVector(To.X, To.Y, 0.f);
			M.Speed = Speed;
			M.Start = ST;
			M.FaceAt = FaceAt;
			SeqMoves.Add(A, M);
		};
		auto MoveFrom = [&](AStaticMeshActor* A, const FVector& From, const FVector& To, float Speed, AActor* FaceAt)
		{
			if (!A) return;
			FSeqMove M;
			M.From = FVector(From.X, From.Y, 0.f);
			M.To = FVector(To.X, To.Y, 0.f);
			M.Speed = Speed;
			M.Start = ST;
			M.FaceAt = FaceAt;
			SeqMoves.Add(A, M);
		};
		AStaticMeshActor* Who = SeqActor(G->Subject);
		AStaticMeshActor* Second = SeqActor(G->Second);
		const float Arm = BowlerPlayer().BowlHand == ECricketHand::Right ? 1.f : -1.f;
		switch (G->Kind)
		{
		case ESegment::Celebration:
		case ESegment::TeamHuddle:
		case ESegment::WinCaptain:
		{
			AStaticMeshActor* Hero = SeqHero.Get();
			if (!Hero) break;
			const FVector At = SeqGround(Hero);
			if (bSeqBattingWon)
			{
				// The pair run together; the fielders stand.
				const FVector Across = FVector(0.f, 1.f, 0.f);
				Move(NonStriker.Get(), At + Across * 110.f, 420.f, Striker.Get(), true);
				Move(Striker.Get(), At, 0.f, NonStriker.Get(), true);
				break;
			}
			// The hero runs a few strides; the team runs in from wherever they are and rings them.
			const FVector Away = (At - SeqCm(CricketGeo::PitchCentre())).GetSafeNormal2D();
			Move(Hero, At + (Away.IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : Away) * 180.f, 320.f, nullptr, true);
			TArray<AStaticMeshActor*> Team;
			for (const TObjectPtr<AStaticMeshActor>& F : Fielders) if (F && F.Get() != Hero && !F->IsHidden()) Team.Add(F.Get());
			if (Bowler.Get() != Hero) Team.Add(Bowler.Get());
			int32 Slot = 0;
			for (AStaticMeshActor* A : Team)
			{
				if (FVector::Dist(A->GetActorLocation(), Hero->GetActorLocation()) > 4500.f) continue;
				const float Ang = 2.f * PI * Slot / 8.f + 0.4f;
				const float R = 150.f + 45.f * (Slot % 2);
				Move(A, At + Away * 180.f + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * R, 480.f, Hero, true);
				++Slot;
			}
			break;
		}
		case ESegment::BattersConfer:
		{
			if (!Who || !Second) break;
			const FVector A = SeqGround(Who), B2 = SeqGround(Second);
			const FVector Mid = (A + B2) * 0.5f, Dir = (A - B2).GetSafeNormal2D();
			Move(Who, Mid + Dir * 65.f, 140.f, Second);
			Move(Second, Mid - Dir * 65.f, 140.f, Who);
			break;
		}
		case ESegment::WalkOff:
			Move(Who, SeqPavilionGate(), 140.f, nullptr);
			break;
		case ESegment::PairWalkOff:
		{
			const FVector Gate = SeqPavilionGate();
			Move(Striker.Get(), Gate + FVector(60.f, 0.f, 0.f), 140.f, nullptr);
			Move(NonStriker.Get(), Gate - FVector(60.f, 0.f, 0.f), 140.f, nullptr);
			break;
		}
		case ESegment::BowlerReaction:
			// Walking back to the mark.
			Move(Who, SeqCm(FVector(RunUpX(0.f), 0.5f * Arm, 0.f)), 120.f, nullptr);
			break;
		case ESegment::FielderReaction:
			if (Who) Move(Who, SeqGround(Who) + (SeqCm(CricketGeo::PitchCentre()) - SeqGround(Who)).GetSafeNormal2D() * 300.f, 120.f, nullptr);
			break;
		case ESegment::BatterReaction:
			if (Who)
			{
				const float Off = OffSideSign(StrikerPlayer().BatHand);
				Move(Who, SeqGround(Who) + FVector(-0.4f, -0.9f * Off, 0.f).GetSafeNormal() * 160.f, 100.f, nullptr);
			}
			break;
		case ESegment::Handshake:
		{
			if (!Who || !Second) break;
			const FVector A = SeqGround(Who), B2 = SeqGround(Second);
			const FVector Mid = (A + B2) * 0.5f, Dir = (A - B2).GetSafeNormal2D();
			Move(Who, Mid + Dir * 55.f, 220.f, Second);
			Move(Second, Mid - Dir * 55.f, 220.f, Who);
			break;
		}
		case ESegment::BatterIntro:
			if (Who && (G->Shot == EShot::WalkHigh || G->Shot == EShot::WalkFront))
			{
				// Walking in the last few metres to the crease.
				const FVector Home = SeqGround(Who);
				const FVector From = Home + (SeqPavilionGate() - Home).GetSafeNormal2D() * 650.f;
				MoveFrom(Who, From, Home, 650.f / FMath::Max(0.85f * G->Duration, 0.5f), nullptr);
			}
			break;
		case ESegment::BowlerIntro:
			if (Who && G->Shot != EShot::LowWide)
			{
				// Back to the top of the mark.
				const FVector Mark = SeqGround(Who);
				MoveFrom(Who, Mark - FVector(500.f, 0.f, 0.f), Mark, 500.f / FMath::Max(0.9f * G->Duration, 0.5f), nullptr);
			}
			break;
		case ESegment::TossCaptains:
		{
			// The captains side by side at the middle, facing the camera's side.
			const FVector Mid = SeqCm(CricketGeo::PitchCentre());
			const FVector Face = FVector(0.f, G->Side >= 0.f ? 1.f : -1.f, 0.f);
			MoveFrom(Striker.Get(), Mid - FVector(55.f, 0.f, 0.f), Mid - FVector(55.f, 0.f, 0.f), 0.f, nullptr);
			MoveFrom(Bowler.Get(), Mid + FVector(55.f, 0.f, 0.f), Mid + FVector(55.f, 0.f, 0.f), 0.f, nullptr);
			SeqTossFace = Face;
			break;
		}
		default:
			break;
		}
	}

	// Play the moves: walk, then face whoever they walked to (or keep facing the way they walked).
	for (TPair<AStaticMeshActor*, FSeqMove>& Pair : SeqMoves)
	{
		AStaticMeshActor* A = Pair.Key;
		const FSeqMove& M = Pair.Value;
		if (!A) continue;
		const FVector P = CricketSequence::WalkTo(M.From, M.To, M.Speed, ST - M.Start);
		A->SetActorLocation(FVector(P.X, P.Y, A->GetActorLocation().Z));
		const FVector Left = FVector(M.To - P);
		FVector Face = Left.Size2D() > 5.f ? Left : (M.To - M.From);
		if (Left.Size2D() <= 5.f && M.FaceAt.IsValid()) Face = M.FaceAt->GetActorLocation() - A->GetActorLocation();
		if (G->Kind == ESegment::TossCaptains || G->Kind == ESegment::TossCoin || G->Kind == ESegment::TossResult) Face = SeqTossFace;
		if (!FVector2D(Face).IsNearlyZero()) A->SetActorRotation(FRotator(0.f, Face.Rotation().Yaw, 0.f));
		FigureStates.FindOrAdd(A).bHeld = true;
		if (A == Striker.Get()) bStrikerFree = true;
	}
	if (G->Kind == ESegment::BatterReaction || G->Kind == ESegment::WalkOff || G->Kind == ESegment::PairWalkOff || G->Kind == ESegment::BattersConfer)
		if (SeqMoves.Contains(Striker.Get())) bStrikerFree = true;

	// The toss coin in the air.
	if (Coin && G->Kind == ESegment::TossCoin)
	{
		float Spin = 0.f;
		const FVector Hand = SeqGround(Striker.Get()) + FVector(25.f, 20.f * SeqTossFace.Y, 125.f);
		SeqCoinAt(Hand, T, CoinAt, Spin);
		Coin->SetActorHiddenInGame(false);
		Coin->SetActorLocationAndRotation(CoinAt, FRotator(0.f, 0.f, Spin));
	}
}

void ASuperOverGameMode::PoseSequenceBodies()
{
	using namespace CricketSequence;
	const float ST = SeqTime();
	const FSegment* G = ST >= 0.f ? Seq.At(ST) : nullptr;
	if (!G || IsReplaying() || IsReviewing() || G->Kind == ESegment::Replay) return;
	const float T = ST - G->Start;
	// A body's shoulders and axes from its skeleton: right from the left shoulder to the right, forward square to it.
	auto Axes = [this](AStaticMeshActor* A, FVector& SL, FVector& SR, FVector& Fwd, FVector& Rt) -> bool
	{
		const USkeletalMeshComponent* Body = A ? BodyOf(A) : nullptr;
		if (!Body || A->IsHidden()) return false;
		SL = Body->GetSocketLocation(TEXT("upperarm_l"));
		SR = Body->GetSocketLocation(TEXT("upperarm_r"));
		Rt = FVector(SR.X - SL.X, SR.Y - SL.Y, 0.f).GetSafeNormal();
		if (Rt.IsNearlyZero()) return false;
		Fwd = FVector(Rt.Y, -Rt.X, 0.f);
		return true;
	};
	auto Apply = [this](AStaticMeshActor* A, const FArms& Arms)
	{
		UCricketAnimInstance* Anim = A ? AnimOf(A) : nullptr;
		if (!Anim) return;
		for (int32 H = 0; H < 2; ++H)
		{
			if (Arms.Weight[H] <= 0.f) continue;
			Anim->Pose.Hand[H] = Arms.Hand[H];
			Anim->Pose.Elbow[H] = Arms.Elbow[H];
			Anim->Pose.HandWeight[H] = Arms.Weight[H];
			Anim->Pose.PalmFacing[H] = FVector::ZeroVector;
			Anim->Pose.FingerFacing[H] = FVector::ZeroVector;
		}
	};
	auto DoGesture = [&](AStaticMeshActor* A, EGesture Kind)
	{
		FVector SL, SR, Fwd, Rt;
		if (Axes(A, SL, SR, Fwd, Rt)) Apply(A, GestureArms(Kind, T, G->Duration, SL, SR, Fwd, Rt));
	};
	auto LookAt = [this](AStaticMeshActor* A, const FVector& At, float Bend = 0.f)
	{
		UCricketAnimInstance* Anim = A ? AnimOf(A) : nullptr;
		if (!Anim) return;
		Anim->Pose.LookAt = At;
		Anim->Pose.LookWeight = 1.f;
		if (Bend > 0.f) Anim->Pose.ChestBend = FMath::Max(Anim->Pose.ChestBend, Bend);
	};

	switch (G->Kind)
	{
	case ESegment::UmpireSignal:
	{
		AStaticMeshActor* Ump = SeqActor(ESubject::Umpire);
		FVector SL, SR, Fwd, Rt;
		if (Axes(Ump, SL, SR, Fwd, Rt))
		{
			Apply(Ump, SignalArms(G->Signal, T, G->Duration, SL, SR, Fwd, Rt));
			LookAt(Ump, (SL + SR) * 0.5f + Fwd * 800.f + FVector(0.f, 0.f, 10.f));
		}
		break;
	}
	case ESegment::Celebration:
	case ESegment::TeamHuddle:
	case ESegment::WinCaptain:
	{
		AStaticMeshActor* Hero = SeqHero.Get();
		const bool bBig = Result.Dismissal == EDismissal::Bowled || bSeqBattingWon || G->Kind == ESegment::WinCaptain;
		DoGesture(Hero, bBig ? EGesture::ArmsUp : EGesture::FistPump);
		if (G->Kind == ESegment::Celebration) DoGesture(SeqMate.Get(), EGesture::HighFive);
		else
		{
			int32 N = 0;
			for (const TPair<AStaticMeshActor*, FSeqMove>& Pair : SeqMoves)
				if (Pair.Key != Hero && (N++ % 2) == 0) DoGesture(Pair.Key, EGesture::Clap);
		}
		// The dismissed batter and their partner, heads down.
		if (!bSeqBattingWon)
		{
			if (AStaticMeshActor* Out = SeqDismissed.Get()) LookAt(Out, Out->GetActorLocation() + Out->GetActorForwardVector() * 250.f - FVector(0.f, 0.f, 150.f), 14.f);
		}
		break;
	}
	case ESegment::BowlerReaction:
		DoGesture(Bowler.Get(), Result.Fielding.Boundary > 0 ? EGesture::HandsOnHips : EGesture::None);
		LookAt(Bowler.Get(), Striker->GetActorLocation() + FVector(0.f, 0.f, 60.f));
		break;
	case ESegment::FielderReaction:
		if (Result.Fielding.bCatchChance && !Result.Fielding.bCaught) DoGesture(SeqActor(ESubject::Fielder), EGesture::HandsOnHead);
		break;
	case ESegment::BatterReaction:
		if (AStaticMeshActor* Who = SeqActor(G->Subject))
		{
			// Watching where it went.
			const FVector Went = Result.Fielding.Boundary > 0 ? SuperOverSequencePrivate::SeqCm(Result.BallAt(Result.ContactTime + Result.Fielding.BoundaryTime)) : Ball->GetActorLocation();
			LookAt(Who, Went + FVector(0.f, 0.f, 300.f));
		}
		break;
	case ESegment::WalkOff:
		if (AStaticMeshActor* Out = SeqActor(G->Subject)) LookAt(Out, Out->GetActorLocation() + Out->GetActorForwardVector() * 300.f - FVector(0.f, 0.f, 140.f), 10.f);
		break;
	case ESegment::Handshake:
		if (T > 0.55f * G->Duration)
		{
			DoGesture(SeqActor(G->Subject), EGesture::Point);
			DoGesture(SeqActor(G->Second), EGesture::Point);
		}
		break;
	case ESegment::TossCoin:
		LookAt(Striker.Get(), CoinAt);
		LookAt(Bowler.Get(), CoinAt);
		break;
	default:
		break;
	}
}

bool ASuperOverGameMode::SequenceCamera(FVector& OutLoc, FVector& OutLook, float& OutFov, float& OutFocus, float& OutAperture, bool& bOutCut)
{
	using namespace CricketSequence;
	using namespace SuperOverSequencePrivate;
	bOutCut = false;
	if (!InSequenceScene())
	{
		SeqLastIndex = INDEX_NONE;
		return false;
	}
	const float ST = SeqTime();
	const int32 Index = Seq.IndexAt(ST);
	// The stingers change the camera under the full card: the one in rides the beat before, the one out the beat after.
	int32 CamIndex = Index;
	float T = ST - Seq.Segments[Index].Start;
	if (Seq.Segments[Index].Kind == ESegment::StingerIn && Index > 0) { CamIndex = Index - 1; T = ST - Seq.Segments[CamIndex].Start; }
	else if (Seq.Segments[Index].Kind == ESegment::StingerOut && Seq.Segments.IsValidIndex(Index + 1)) { CamIndex = Index + 1; T = 0.f; }
	const FSegment& G = Seq.Segments[CamIndex];
	if (CamIndex != SeqLastIndex) bOutCut = true;
	SeqLastIndex = CamIndex;

	FShotFrame F;
	F.T = T;
	F.Duration = G.Duration;
	F.Side = G.Side;
	F.OffSign = OffSideSign(StrikerPlayer().BatHand);
	F.PitchLength = CricketGeo::PitchLength * 100.f;
	F.PitchCentre = SeqCm(CricketGeo::PitchCentre());
	const AStaticMeshActor* Who = SeqActor(G.Subject);
	const AStaticMeshActor* Second = SeqActor(G.Second);
	F.Subject = Who ? SeqGround(Who) : F.PitchCentre;
	F.Facing = Who ? Who->GetActorForwardVector() : FVector(1.f, 0.f, 0.f);
	F.Second = Second ? SeqGround(Second) : F.Subject + F.Facing * 200.f;
	switch (G.Shot)
	{
	case EShot::Crowd:
	{
		const FVector2D Went = Result.Fielding.Boundary > 0 ? FVector2D(Result.BallAt(Result.ContactTime + Result.Fielding.BoundaryTime) - CricketGeo::PitchCentre()) : FVector2D::ZeroVector;
		SeqCrowdShot(G.Start * 13.7f + BallsPlayed, Went, F.From, F.Target);
		break;
	}
	case EShot::StumpsClose:
		F.Target = FVector::ZeroVector;
		break;
	case EShot::TossCoin:
		F.Target = CoinAt;
		F.Facing = FVector(1.f, 0.f, 0.f);
		break;
	default:
		break;
	}
	const FShotSolution Sol = SolveShot(G.Shot, F);
	OutLoc = Sol.Location;
	OutLook = Sol.LookAt;
	OutFov = Sol.FOV;
	OutFocus = Sol.FocusCm;
	OutAperture = Sol.Aperture;
	return true;
}
