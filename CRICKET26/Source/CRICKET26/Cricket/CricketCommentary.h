// Text commentary: one original line per delivery, built from what the simulation decided (shot, where
// it went, how the batter was out, a dropped chance) and the match situation afterwards. Pure: the
// game mode shows it as a caption. Lines are written for this project; voiced commentary would key
// recorded clips off the same decisions.

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
	};

	/**
	 * Line for a finished delivery. Outcome is what the umpire gave; After is the match once it was
	 * applied. Variant rotates between phrasings so repeated events do not repeat word for word.
	 */
	FString Describe(const FDeliveryResult& R, const FDeliveryOutcome& Outcome, const FSuperOverMatch& After,
		const FNames& Names, float OffSign, int32 Variant);
}
