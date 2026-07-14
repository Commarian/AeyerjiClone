#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AeyerjiGameplayTags.h"
#include "Items/LootSourceRuleSet.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiLootSourceRuleSetProfileTest,
	"Aeyerji.Loot.SourceRuleSetProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiLootSourceRuleSetProfileTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	ULootSourceRuleSet* RuleSet = NewObject<ULootSourceRuleSet>(GetTransientPackage());
	TestNotNull(TEXT("Rule set can be constructed."), RuleSet);
	if (!RuleSet)
	{
		return false;
	}

	FLootSourceRule& Rule = RuleSet->Rules.AddDefaulted_GetRef();
	Rule.Priority = 10;
	Rule.MatchQuery = FGameplayTagQuery::MakeQuery_MatchTag(FGameplayTag(AeyerjiTags::Loot_Source_Boss));
	Rule.Profile.SourceTag = AeyerjiTags::Loot_Source_Boss;
	Rule.Profile.PityGroup = AeyerjiTags::Loot_Pity_BossUnique;
	Rule.Profile.BaseLegendaryChance = 0.35f;
	Rule.Profile.MinimumRarity = EItemRarity::Rare;
	Rule.Profile.PitySuccessRarity = EItemRarity::Epic;
	Rule.Profile.PitySoftStartOverride = 5;
	Rule.Profile.PitySoftSlopeOverride = 0.02f;
	Rule.Profile.PityHardAttemptsOverride = 25;
	Rule.Profile.PityMaxChanceOverride = 0.75f;

	FLootContext BaseContext;
	BaseContext.EnemyLevel = 42;
	BaseContext.PlayerLevel = 17;
	BaseContext.WorldTier = 4;
	BaseContext.SourceTag = AeyerjiTags::Loot_Source_NormalEnemy;
	BaseContext.PityGroup = AeyerjiTags::Loot_Pity_GenericLegendary;

	FGameplayTagContainer SourceTags;
	SourceTags.AddTag(AeyerjiTags::Loot_Source_Boss);

	const FLootContext Resolved = RuleSet->ResolveContext(BaseContext, SourceTags);

	TestEqual(TEXT("Dynamic enemy level is preserved."), Resolved.EnemyLevel, 42);
	TestEqual(TEXT("Dynamic player level is preserved."), Resolved.PlayerLevel, 17);
	TestEqual(TEXT("Dynamic world tier is preserved."), Resolved.WorldTier, 4);
	TestTrue(TEXT("Profile source tag is applied."), Resolved.SourceTag == FGameplayTag(AeyerjiTags::Loot_Source_Boss));
	TestTrue(TEXT("Profile pity group is applied."), Resolved.PityGroup == FGameplayTag(AeyerjiTags::Loot_Pity_BossUnique));
	TestEqual(TEXT("Profile legendary chance is applied."), Resolved.BaseLegendaryChance, 0.35f);
	TestEqual(TEXT("Profile minimum rarity is applied."), Resolved.MinimumRarity, EItemRarity::Rare);
	TestEqual(TEXT("Profile pity success rarity is applied."), Resolved.PitySuccessRarity, EItemRarity::Epic);
	TestEqual(TEXT("Profile soft pity start is applied."), Resolved.PitySoftStartOverride, 5);
	TestEqual(TEXT("Profile soft pity slope is applied."), Resolved.PitySoftSlopeOverride, 0.02f);
	TestEqual(TEXT("Profile hard pity is applied."), Resolved.PityHardAttemptsOverride, 25);
	TestEqual(TEXT("Profile pity max chance is applied."), Resolved.PityMaxChanceOverride, 0.75f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiLootSourceRuleSetSanitizesTest,
	"Aeyerji.Loot.SourceRuleSetSanitizes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiLootSourceRuleSetSanitizesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	ULootSourceRuleSet* RuleSet = NewObject<ULootSourceRuleSet>(GetTransientPackage());
	TestNotNull(TEXT("Rule set can be constructed."), RuleSet);
	if (!RuleSet)
	{
		return false;
	}

	RuleSet->DefaultProfile.BaseLegendaryChance = 2.0f;
	RuleSet->DefaultProfile.DifficultyScale = -3.0f;
	RuleSet->DefaultProfile.PitySoftStartOverride = -20;
	RuleSet->DefaultProfile.PitySoftSlopeOverride = -0.2f;
	RuleSet->DefaultProfile.PityHardAttemptsOverride = -30;
	RuleSet->DefaultProfile.PityMaxChanceOverride = 2.0f;
	RuleSet->DefaultProfile.RarityWeights.Add(EItemRarity::Common, -5.f);
	RuleSet->DefaultProfile.RarityWeights.Add(EItemRarity::Legendary, 8.f);

	FLootContext BaseContext;
	BaseContext.SourceTag = AeyerjiTags::Loot_Source_NormalEnemy;
	BaseContext.PityGroup = AeyerjiTags::Loot_Pity_GenericLegendary;

	const FLootContext Resolved = RuleSet->ResolveContext(BaseContext, FGameplayTagContainer());

	TestEqual(TEXT("Legendary chance clamps to probability."), Resolved.BaseLegendaryChance, 1.0f);
	TestEqual(TEXT("Invalid difficulty scale falls back to 1."), Resolved.DifficultyScale, 1.0f);
	TestEqual(TEXT("Negative soft pity start normalizes to default sentinel."), Resolved.PitySoftStartOverride, -1);
	TestEqual(TEXT("Negative soft pity slope normalizes to default sentinel."), Resolved.PitySoftSlopeOverride, -1.0f);
	TestEqual(TEXT("Negative hard pity normalizes to default sentinel."), Resolved.PityHardAttemptsOverride, -1);
	TestEqual(TEXT("Pity max chance clamps to probability."), Resolved.PityMaxChanceOverride, 1.0f);
	TestEqual(TEXT("Negative rarity weight clamps to zero."), Resolved.RarityWeights.FindRef(EItemRarity::Common), 0.0f);
	TestEqual(TEXT("Positive rarity weight is preserved."), Resolved.RarityWeights.FindRef(EItemRarity::Legendary), 8.0f);
	TestTrue(TEXT("Base source tag is preserved when profile source is unset."), Resolved.SourceTag == FGameplayTag(AeyerjiTags::Loot_Source_NormalEnemy));
	TestTrue(TEXT("Base pity group is preserved when profile pity group is unset."), Resolved.PityGroup == FGameplayTag(AeyerjiTags::Loot_Pity_GenericLegendary));

	return true;
}

#endif
