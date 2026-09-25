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
	/**
	 * AI difficulty is decision quality, 0..1: how well the AI reads the ball and picks a shot, finds a
	 * gap, judges a run and plans an over. It never touches physics or a player's execution attributes
	 * (timing, accuracy, pace), which stay the same at every level.
	 */
	enum class EDifficulty : uint8 { Easy, Medium, Hard, Legend };
	float SkillOf(EDifficulty D);
	const TCHAR* DifficultyName(EDifficulty D);
	/** Hard: the tuning the soak-test scoring bands were calibrated against. */
	constexpr float DefaultSkill = 0.75f;

	/** 0 = preserve wicket, 1 = all-out attack, from the match situation. */
	float Aggression(const FSuperOverMatch& Match);

	/** Time buffer the batters insist on when running; negative when desperate. */
	float RunMargin(const FSuperOverMatch& Match, float Aggression, float Skill = DefaultSkill);

	FBowlingChoice ChooseDelivery(const FCricketPlayer& Bowler, ECricketHand BatHand, const FSuperOverMatch& Match,
		const TArray<int32>& RecentPlans, FRandomStream& Rng, float Skill = DefaultSkill);

	/** How a batter classes a delivery from their read of it. */
	enum class EBallClass : uint8 { FullToss, BlockHole, Slot, Length, Short };
	EBallClass ClassOf(const FBallRead& Seen, bool bSpin);
	/** What an experienced batter expects a stroke (Defend/Ground/Loft) to yield against a class of ball. */
	void ShotValue(EBallClass Class, bool bSpin, EBatIntent Intent, float& Runs, float& Out);
	/** Runs a wicket is worth at this aggression. */
	float WicketCost(float Aggression);
	/** Plays a given intent to the delivery the way the AI would (gap, timing), without choosing it; aimed at Direction if set. */
	FBatInput PlayIntent(EBatIntent Intent, const FDeliveryRelease& Release, const FCricketPlayer& Batter, EBowlerType BowlerType,
		const TArray<FFielder>& Field, const FPitchConditions& Conditions, FRandomStream& Rng, float Skill = DefaultSkill,
		TOptional<float> Direction = {});

	FBatInput ChooseShot(const FDeliveryRelease& Release, const FCricketPlayer& Batter, EBowlerType BowlerType,
		float Aggression, const TArray<FFielder>& Field, const FPitchConditions& Conditions, FRandomStream& Rng,
		float Skill = DefaultSkill);
}
