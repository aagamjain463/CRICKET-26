// Cricket AI. Uses exactly the same inputs a human has (delivery plan + release timing for the
// bowler; intent, direction and press time for the batter) and the same physics - no cheating.

#pragma once

#include "CoreMinimal.h"
#include "DeliveryResolver.h"

struct FBowlingChoice
{
	FDeliveryPlan Plan;
	float ReleaseTiming = 0.f;
	int32 PlanId = 0; // lets the AI avoid repeating itself
	FString Label;
};

namespace CricketAI
{
	/** 0 = preserve wicket, 1 = all-out attack, from the match situation. */
	float Aggression(const FSuperOverMatch& Match);

	/** Time buffer the batters insist on when running; negative when desperate. */
	float RunMargin(const FSuperOverMatch& Match, float Aggression);

	FBowlingChoice ChooseDelivery(const FCricketPlayer& Bowler, ECricketHand BatHand, const FSuperOverMatch& Match,
		const TArray<int32>& RecentPlans, FRandomStream& Rng);

	FBatInput ChooseShot(const FDeliveryRelease& Release, const FCricketPlayer& Batter, EBowlerType BowlerType,
		float Aggression, const TArray<FFielder>& Field, const FPitchConditions& Conditions, FRandomStream& Rng);
}
