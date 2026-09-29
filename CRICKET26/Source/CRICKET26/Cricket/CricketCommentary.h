// Text commentary library + pronunciation layer. All lines are written for this project (original,
// spoken-style, concise); voiced commentary keys recorded clips off the same line ids.
// The simulation decides what happened; this library only describes the umpire-given outcome and the
// match situation afterwards. Pure: the game mode shows the picked line as a caption, and the
// CommentaryDirector (scored selection, memory, queue) picks which line id to speak.

#pragma once

#include "CoreMinimal.h"
#include "DeliveryResolver.h"
#include "SuperOverMatch.h"

namespace CricketCommentary
{
	/** The fielding region a ball hit with this velocity heads for, e.g. "cover" or "fine leg". */
	FString Region(const FVector& ExitVel, float OffSign);

	struct FNames
	{
		FString Striker, NonStriker, BattingTeam;
		FString BowlingTeam; // names the winner when a defence wins it
	};

	/** Commentary voice roles. A: play-by-play (immediate action); B: analyst (technique/tactics). */
	enum class ESpeaker : uint8 { PlayByPlay, Analyst };

	/** Structured asset metadata for every commentary line (spec: EventType/Speaker/Excitement/
	 *  Priority/Context/Cooldown/Tags/Duration/Interrupt). */
	struct FLineMeta
	{
		FString Id;               // stable id, e.g. "Six_Big_03" (voiced clips key off this)
		ESpeaker Speaker = ESpeaker::PlayByPlay;
		float Excitement = 0.3f;  // 0 calm .. 1 climax (voice performance level, not just volume)
		int32 Priority = 1;       // 0 ambient .. 4 match-deciding
		int32 CooldownBalls = 2;  // balls before this exact line may repeat
		FString Tags;             // comma list, e.g. "six,momentum,final-ball"
		float EstSeconds = 2.5f;  // estimated speech duration (word-count based at import)
		bool bCanInterrupt = false;
		bool bCanBeInterrupted = true;
	};

	struct FLine
	{
		FString Text; // with {S} {NS} {R} slots; Situation() suffix appended by Describe
		FLineMeta Meta;
	};

	/** Full variation pools, grouped by event. Describe() rotates deterministically (Variant);
	 *  the director scores across the same pools. */
	const TArray<FLine>& SixLines();
	const TArray<FLine>& FourLines();
	const TArray<FLine>& DotLines();
	const TArray<FLine>& SingleLines();
	const TArray<FLine>& MultiRunLines();
	const TArray<FLine>& BowledLines();
	const TArray<FLine>& CaughtLines();
	const TArray<FLine>& CaughtBehindLines();
	const TArray<FLine>& LbwLines();
	const TArray<FLine>& RunOutLines();
	const TArray<FLine>& StumpedLines();
	const TArray<FLine>& HitWicketLines();
	const TArray<FLine>& WideLines();
	const TArray<FLine>& EdgeFourLines();
	const TArray<FLine>& OverthrowLines();
	const TArray<FLine>& BeatenLines();
	const TArray<FLine>& DefendLines();
	const TArray<FLine>& LeaveLines();
	const TArray<FLine>& AnalysisLines();
	const TArray<FLine>& ResultLines();

	/** Repeat-safe pick: Variant-th rotation that also avoids `AvoidId` when the pool allows. */
	const FLine& PickLine(const TArray<FLine>& Pool, int32 Variant, const FString& AvoidId = FString());

	/**
	 * Line for a finished delivery. Outcome is what the umpire gave; After is the match once it was
	 * applied. Variant rotates between phrasings so repeated events do not repeat word for word.
	 */
	FString Describe(const FDeliveryResult& R, const FDeliveryOutcome& Outcome, const FSuperOverMatch& After,
		const FNames& Names, float OffSign, int32 Variant);

	/** Short analyst follow-up after a big moment (handoff), or empty when none fits. Pure. */
	FString Analyse(const FDeliveryResult& R, const FDeliveryOutcome& Outcome, const FSuperOverMatch& After,
		const FNames& Names, float OffSign, int32 Variant);

	// ---- Pronunciation layer ----
	/** Spoken form + plain-English phonetic hint for a display name. Unknown names fall back to the
	 *  display text (never guessed blindly) so QA can flag them. */
	struct FSpokenName
	{
		FString Display, Spoken, Phonetic;
	};
	/** Every default-squad name plus the cricket vocabulary the lines use (yorker, googly,
	 *  midwicket, Super Over, ...). */
	const TArray<FSpokenName>& Lexicon();
	FString SpokenFor(const FString& Display);
	/** False when `Display` has no lexicon entry (log + add it before voicing). */
	bool HasPronunciation(const FString& Display);
	/** File name for a voiced clip of `Text`: lower-case words joined by '_', apostrophes dropped.
	 *  Scripts/audio/eleven.py builds the same key when it generates the clip. */
	FString ClipKey(const FString& Text);
}
