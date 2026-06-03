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

	if (ResolvedProfile.SourceTag.IsValid())
	{
		Result.SourceTag = ResolvedProfile.SourceTag;
	}
	if (ResolvedProfile.PityGroup.IsValid())
	{
		Result.PityGroup = ResolvedProfile.PityGroup;
	}
	if (ResolvedProfile.ForcedItemDefinition)
	{
		Result.ForcedItemDefinition = ResolvedProfile.ForcedItemDefinition;
	}

	Result.BaseLegendaryChance = ResolvedProfile.BaseLegendaryChance;
	Result.MinimumRarity = ResolvedProfile.MinimumRarity;
	Result.PitySuccessRarity = ResolvedProfile.PitySuccessRarity;
	Result.DifficultyScale = ResolvedProfile.DifficultyScale;
	Result.RarityWeights = ResolvedProfile.RarityWeights;
	Result.ItemLevelJitterMin = ResolvedProfile.ItemLevelJitterMin;
	Result.ItemLevelJitterMax = ResolvedProfile.ItemLevelJitterMax;
	Result.PitySoftStartOverride = ResolvedProfile.PitySoftStartOverride;
	Result.PitySoftSlopeOverride = ResolvedProfile.PitySoftSlopeOverride;
	Result.PityHardAttemptsOverride = ResolvedProfile.PityHardAttemptsOverride;
	Result.PityMaxChanceOverride = ResolvedProfile.PityMaxChanceOverride;

	// Safety: clamp obvious bad data.
	Result.BaseLegendaryChance = FMath::Clamp(Result.BaseLegendaryChance, 0.f, 1.f);
	Result.DifficultyScale = (Result.DifficultyScale <= 0.0f) ? 1.0f : Result.DifficultyScale;
	Result.PitySoftStartOverride = Result.PitySoftStartOverride < 0 ? -1 : Result.PitySoftStartOverride;
	Result.PitySoftSlopeOverride = Result.PitySoftSlopeOverride < 0.f ? -1.f : Result.PitySoftSlopeOverride;
	Result.PityHardAttemptsOverride = Result.PityHardAttemptsOverride < 0 ? -1 : Result.PityHardAttemptsOverride;
	Result.PityMaxChanceOverride = Result.PityMaxChanceOverride < 0.f ? -1.f : FMath::Clamp(Result.PityMaxChanceOverride, 0.f, 1.f);

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
