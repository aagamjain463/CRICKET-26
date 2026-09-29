// What the auctioneer says for each auction event, in the cadence of the real room ("Two crore twenty, with
// Chennai", "Going once... going twice... sold"). The HUD shows it as the caption; the voice speaks the same line.

#pragma once

#include "CoreMinimal.h"
#include "AuctionEngine.h"

namespace AuctionCalls
{
	/** How the auctioneer names a franchise out loud: "Chennai", "Punjab". */
	FString TeamName(int32 Team);
	/** Where the franchise's table is, from the podium: "on my left", "up front". */
	FString Where(int32 Team);
	/** A raise as she says it after Previous: "3 crore", then "20", "40" within the same crore. */
	FString Short(int32 Lakh, int32 Previous);
	/** A line and the pieces the voice stitches it from, in order: fixed words, and each name or price said into them. */
	struct FCall
	{
		FString Text;
		TArray<FString> Pieces;
	};
	FCall Call(const FAuction& A, const FAuctionEventRecord& E);
	/** The line for this event, or empty when the auctioneer stays quiet. */
	FString Line(const FAuction& A, const FAuctionEventRecord& E);
	/** Every piece the auctioneer can say, one per clip (CricketCommentary::ClipKey): what
	 *  Scripts/audio/auctioneer.py records into Content/Audio/Auction. */
	TArray<FString> Script();
	/** The line spoken: its recorded pieces (Content/Audio/Auction) joined with the pauses its punctuation asks for,
	 *  22.05 kHz mono. Empty, the line a caption only, when any piece is unrecorded. */
	TArray<int16> Voice(const FCall& Call);
}
