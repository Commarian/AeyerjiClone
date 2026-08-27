#include "Items/LootSourceRuleSet.h"

namespace
{
	constexpr int32 MaxLootSourceRules = 1024;
	constexpr int32 MaxLootSourcePityAttempts = 1000000;
	constexpr float MaxLootSourceScalar = 1000000.f;
	constexpr float MaxLootSourceWeight = 1000000000.f;

	bool IsValidLootSourceRarity(const EItemRarity Rarity)
	{
		const UEnum* Enum = StaticEnum<EItemRarity>();
		return Enum && Enum->IsValidEnumValue(static_cast<int64>(Rarity));
	}
}

FLootContext ULootSourceRuleSet::ResolveContext(const FLootContext& BaseContext, const FGameplayTagContainer& SourceTags) const
{
	FLootContext Result = BaseContext;

	const FLootSourceRule* BestRule = nullptr;
	const int32 RuleCount = FMath::Min(Rules.Num(), MaxLootSourceRules);
	for (int32 RuleIndex = 0; RuleIndex < RuleCount; ++RuleIndex)
	{
		const FLootSourceRule& Rule = Rules[RuleIndex];
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
	if (IsValid(ResolvedProfile.ForcedItemDefinition))
	{
		Result.ForcedItemDefinition = ResolvedProfile.ForcedItemDefinition;
	}

	Result.BaseLegendaryChance = ResolvedProfile.BaseLegendaryChance;
	Result.MinimumRarity = ResolvedProfile.MinimumRarity;
	Result.PitySuccessRarity = ResolvedProfile.PitySuccessRarity;
	Result.DifficultyScale = ResolvedProfile.DifficultyScale;
	Result.RewardQualityMultiplier = ResolvedProfile.RewardQualityMultiplier;
	Result.RarityWeights = ResolvedProfile.RarityWeights;
	Result.PitySoftStartOverride = ResolvedProfile.PitySoftStartOverride;
	Result.PitySoftSlopeOverride = ResolvedProfile.PitySoftSlopeOverride;
	Result.PityHardAttemptsOverride = ResolvedProfile.PityHardAttemptsOverride;
	Result.PityMaxChanceOverride = ResolvedProfile.PityMaxChanceOverride;

	// Safety: clamp obvious bad data.
	Result.BaseLegendaryChance = FMath::Clamp(
		FMath::IsFinite(Result.BaseLegendaryChance) ? Result.BaseLegendaryChance : 0.f, 0.f, 1.f);
	Result.DifficultyScale = FMath::IsFinite(Result.DifficultyScale) && Result.DifficultyScale > 0.f
		? FMath::Min(Result.DifficultyScale, MaxLootSourceScalar)
		: 1.f;
	Result.RewardQualityMultiplier = FMath::Clamp(
		FMath::IsFinite(Result.RewardQualityMultiplier) ? Result.RewardQualityMultiplier : 1.f,
		0.f,
		MaxLootSourceScalar);
	Result.MinimumRarity = IsValidLootSourceRarity(Result.MinimumRarity) ? Result.MinimumRarity : EItemRarity::Common;
	Result.PitySuccessRarity = IsValidLootSourceRarity(Result.PitySuccessRarity) ? Result.PitySuccessRarity : EItemRarity::Legendary;
	Result.PitySoftStartOverride = Result.PitySoftStartOverride < 0
		? -1
		: FMath::Min(Result.PitySoftStartOverride, MaxLootSourcePityAttempts);
	Result.PitySoftSlopeOverride = !FMath::IsFinite(Result.PitySoftSlopeOverride) || Result.PitySoftSlopeOverride < 0.f
		? -1.f
		: FMath::Min(Result.PitySoftSlopeOverride, 1.f);
	Result.PityHardAttemptsOverride = Result.PityHardAttemptsOverride < 0
		? -1
		: FMath::Min(Result.PityHardAttemptsOverride, MaxLootSourcePityAttempts);
	Result.PityMaxChanceOverride = !FMath::IsFinite(Result.PityMaxChanceOverride) || Result.PityMaxChanceOverride < 0.f
		? -1.f
		: FMath::Clamp(Result.PityMaxChanceOverride, 0.f, 1.f);

	// Clamp weights to non-negative (do not normalize; let your drop logic decide).
	for (auto WeightIt = Result.RarityWeights.CreateIterator(); WeightIt; ++WeightIt)
	{
		if (!IsValidLootSourceRarity(WeightIt.Key()) || !FMath::IsFinite(WeightIt.Value()))
		{
			WeightIt.RemoveCurrent();
		}
		else
		{
			WeightIt.Value() = FMath::Clamp(WeightIt.Value(), 0.f, MaxLootSourceWeight);
		}
	}

	return Result;
}
