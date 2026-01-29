#include "Items/LootSourceRuleSet.h"

FLootContext ULootSourceRuleSet::ResolveContext(const FLootContext& BaseContext, const FGameplayTagContainer& SourceTags) const
{
	FLootContext Result = BaseContext;

	const FLootSourceRule* BestRule = nullptr;
	for (const FLootSourceRule& Rule : Rules)
	{
		if (!Rule.MatchQuery.IsEmpty() && Rule.MatchQuery.Matches(SourceTags))
		{
			if (!BestRule || Rule.Priority > BestRule->Priority)
			{
				BestRule = &Rule;
			}
		}
	}

	const FLootContext& ResolvedProfile = BestRule ? BestRule->Profile : DefaultProfile;

	Result.BaseLegendaryChance = ResolvedProfile.BaseLegendaryChance;
	Result.MinimumRarity = ResolvedProfile.MinimumRarity;
	Result.DifficultyScale = ResolvedProfile.DifficultyScale;
	Result.RarityWeights = ResolvedProfile.RarityWeights;
	Result.ItemLevelJitterMin = ResolvedProfile.ItemLevelJitterMin;
	Result.ItemLevelJitterMax = ResolvedProfile.ItemLevelJitterMax;

	// Safety: clamp obvious bad data.
	Result.BaseLegendaryChance = ClampNonNegative(Result.BaseLegendaryChance);
	Result.DifficultyScale = (Result.DifficultyScale <= 0.0f) ? 1.0f : Result.DifficultyScale;

	// Clamp weights to non-negative (do not normalize; let your drop logic decide).
	for (TPair<EItemRarity, float>& Kvp : Result.RarityWeights)
	{
		Kvp.Value = ClampNonNegative(Kvp.Value);
	}

	// Ensure jitter min <= max
	if (Result.ItemLevelJitterMin > Result.ItemLevelJitterMax)
	{
		Swap(Result.ItemLevelJitterMin, Result.ItemLevelJitterMax);
	}

	return Result;
}
