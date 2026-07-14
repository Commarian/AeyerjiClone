#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/AeyerjiAbilityTuning.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "Abilities/GravitonPull/GA_AGGravitonPull.h"
#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "GameFramework/Actor.h"
#include "GAS/GE_AeyerjiAbilityCooldown.h"
#include "GAS/GE_AeyerjiAbilityCostMana.h"
#include "GUI/AbilityTooltipData.h"

namespace
{
	UDataTable* MakeAbilityTable(UObject* Outer)
	{
		UDataTable* Table = NewObject<UDataTable>(Outer);
		Table->RowStruct = FAeyerjiAbilityTableRow::StaticStruct();

		FAeyerjiAbilityTableRow Row;
		Row.AbilityClass = TSoftClassPtr<UGameplayAbility>(UGA_AGGravitonPull::StaticClass());
		Row.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.GravitonPull"), false);
		Row.TargetMode = EAeyerjiTargetMode::EnemyActor;
		Row.DisplayName = FText::FromString(TEXT("Automation Graviton"));
		Row.Description = FText::FromString(TEXT("Automation row tooltip."));
		Row.RequiredLevel = 4;
		Row.bUnlockedByDefault = true;
		Row.Cost.ManaCost = 12.f;
		Row.Cost.Cooldown = 3.f;
		Row.CooldownTag = FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Graviton"), false);
		Row.PreviewRange = 900.f;
		Row.MaxRange = 1000.f;
		Row.Shape = EAeyerjiAbilityTargetShape::SingleActor;
		Row.TargetTeam = EAeyerjiAbilityTargetTeam::Enemy;
		Row.Damage.FlatValue = 42.f;
		Row.Damage.SetByCallerTag = AeyerjiTags::SBC_Damage_Instant;
		Row.Damage.DamageTypeTag = AeyerjiTags::DamageType_Physical;

		FAeyerjiAbilityFloatTunable PullDistance;
		PullDistance.Key = AeyerjiTags::SBC_Damage_Instant;
		PullDistance.Value = 1200.f;
		Row.FloatTunables.Add(PullDistance);

		FAeyerjiAbilityBoolTunable bApplyAilment;
		bApplyAilment.Key = AeyerjiTags::DamageType_Physical;
		bApplyAilment.Value = true;
		Row.BoolTunables.Add(bApplyAilment);

		FAeyerjiAbilityIntTunable AilmentStacks;
		AilmentStacks.Key = AeyerjiTags::State_Dead;
		AilmentStacks.Value = 2;
		Row.IntTunables.Add(AilmentStacks);

		FAeyerjiAbilityTagTunable PullDamageType;
		PullDamageType.Key = AeyerjiTags::Ability_Potion_Heal;
		PullDamageType.Value = AeyerjiTags::DamageType_Physical;
		Row.TagTunables.Add(PullDamageType);

		FAeyerjiAbilityAssetTunable PullImpactAsset;
		PullImpactAsset.Key = AeyerjiTags::State_Dead_Cleansed;
		PullImpactAsset.Value = FSoftObjectPath(TEXT("/Game/Abilities/Stomp/StompImage.StompImage"));
		Row.AssetTunables.Add(PullImpactAsset);

		Table->AddRow(Row.AbilityTag.GetTagName(), Row);
		return Table;
	}

	UDataTable* MakeAbilityRankTable(UObject* Outer)
	{
		UDataTable* Table = NewObject<UDataTable>(Outer);
		Table->RowStruct = FAeyerjiAbilityRankTableRow::StaticStruct();

		FAeyerjiAbilityRankTableRow RankTwo;
		RankTwo.AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.GravitonPull"), false);
		RankTwo.Rank = 2;
		RankTwo.RequiredPlayerLevel = 6;
		RankTwo.PointCost = 1;
		RankTwo.RequiredOtherPointSpendsSinceLastUpgrade = 1;
		RankTwo.bOverrideCost = true;
		RankTwo.Cost.ManaCost = 20.f;
		RankTwo.Cost.Cooldown = 5.f;
		RankTwo.bOverrideDamage = true;
		RankTwo.Damage.FlatValue = 64.f;
		RankTwo.Damage.SetByCallerTag = AeyerjiTags::SBC_Damage_Instant;
		RankTwo.Damage.DamageTypeTag = AeyerjiTags::DamageType_Physical;
		RankTwo.bOverridePreviewRange = true;
		RankTwo.PreviewRange = 1200.f;

		FAeyerjiAbilityFloatTunable PullDistance;
		PullDistance.Key = AeyerjiTags::SBC_Damage_Instant;
		PullDistance.Value = 1800.f;
		RankTwo.FloatTunables.Add(PullDistance);

		Table->AddRow(FName(TEXT("Ability.AG.GravitonPull.2")), RankTwo);
		return Table;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiAbilityTuningRowLookupTest,
	"Aeyerji.Abilities.Tuning.RowLookupAndSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiAbilityTuningRowLookupTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UDataTable* Table = MakeAbilityTable(GetTransientPackage());
	const FGameplayTag AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.GravitonPull"), false);
	const FAeyerjiAbilityTableRow* Row = UAeyerjiAbilityTuningSubsystem::FindAbilityRowInTable(Table, AbilityTag);

	TestNotNull(TEXT("Ability row resolves by ability tag."), Row);
	if (!Row)
	{
		return false;
	}

	TestEqual(TEXT("Row damage magnitude is table-authored."), Row->Damage.FlatValue, 42.f);
	TestEqual(TEXT("Row target mode is preserved."), Row->TargetMode, EAeyerjiTargetMode::EnemyActor);

	float PullDistance = 0.f;
	bool bApplyAilment = false;
	int32 AilmentStacks = 0;
	FGameplayTag PullDamageType;
	FSoftObjectPath PullImpactAsset;
	TestTrue(TEXT("Float tunable resolves by key."), Row->TryGetFloatTunable(AeyerjiTags::SBC_Damage_Instant, PullDistance));
	TestEqual(TEXT("Float tunable value is preserved."), PullDistance, 1200.f);
	TestTrue(TEXT("Bool tunable resolves by key."), Row->TryGetBoolTunable(AeyerjiTags::DamageType_Physical, bApplyAilment));
	TestTrue(TEXT("Bool tunable value is preserved."), bApplyAilment);
	TestTrue(TEXT("Int tunable resolves by key."), Row->TryGetIntTunable(AeyerjiTags::State_Dead, AilmentStacks));
	TestEqual(TEXT("Int tunable value is preserved."), AilmentStacks, 2);
	TestTrue(TEXT("Tag tunable resolves by key."), Row->TryGetTagTunable(AeyerjiTags::Ability_Potion_Heal, PullDamageType));
	TestTrue(TEXT("Tag tunable value is preserved."), PullDamageType == AeyerjiTags::DamageType_Physical.GetTag());
	TestTrue(TEXT("Asset tunable resolves by key."), Row->TryGetAssetTunable(AeyerjiTags::State_Dead_Cleansed, PullImpactAsset));
	TestEqual(TEXT("Asset tunable value is preserved."), PullImpactAsset.ToString(), FString(TEXT("/Game/Abilities/Stomp/StompImage.StompImage")));
	TestFalse(TEXT("Missing tunable key returns false."), Row->TryGetFloatTunable(AeyerjiTags::Ability_Potion_Heal, PullDistance));

	FAeyerjiAbilitySlot Slot;
	TestTrue(TEXT("Slot builds from row."), UAeyerjiAbilityTuningSubsystem::BuildAbilitySlotFromRow(*Row, Slot));
	TestTrue(TEXT("Slot contains row ability tag."), Slot.Tag.HasTagExact(AbilityTag));
	TestEqual(TEXT("Slot target mode follows row."), Slot.TargetMode, EAeyerjiTargetMode::EnemyActor);
	TestEqual(TEXT("Slot class resolves."), Slot.Class.Get(), UGA_AGGravitonPull::StaticClass());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiAbilityTuningTooltipAndGETest,
	"Aeyerji.Abilities.Tuning.TooltipAndStandardEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiAbilityTuningTooltipAndGETest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UDataTable* Table = MakeAbilityTable(GetTransientPackage());
	UDataTable* RankTable = MakeAbilityRankTable(GetTransientPackage());
	UAeyerjiAbilityTuningSettings* Settings = GetMutableDefault<UAeyerjiAbilityTuningSettings>();
	const TSoftObjectPtr<UDataTable> PreviousTable = Settings->AbilityTuningTable;
	const TSoftObjectPtr<UDataTable> PreviousRankTable = Settings->AbilityRankTuningTable;
	Settings->AbilityTuningTable = TSoftObjectPtr<UDataTable>(Table);
	Settings->AbilityRankTuningTable = TSoftObjectPtr<UDataTable>(RankTable);

	const FGameplayTag AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.GravitonPull"), false);
	FAeyerjiAbilitySlot Slot;
	const FAeyerjiAbilityTableRow* Row = UAeyerjiAbilityTuningSubsystem::FindAbilityRowInTable(Table, AbilityTag);
	TestTrue(TEXT("Slot builds for tooltip."), Row && UAeyerjiAbilityTuningSubsystem::BuildAbilitySlotFromRow(*Row, Slot));

	const FAeyerjiAbilityTooltipData Tooltip = FAeyerjiAbilityTooltipData::FromSlot(nullptr, Slot, EAbilityTooltipSource::AbilityPicker);
	TestEqual(TEXT("Tooltip display name comes from table."), Tooltip.DisplayName.ToString(), FString(TEXT("Automation Graviton")));
	TestEqual(TEXT("Tooltip mana cost comes from table."), Tooltip.ManaCost, 12.f);
	TestEqual(TEXT("Tooltip cooldown comes from table."), Tooltip.CooldownSeconds, 3.f);
	TestEqual(TEXT("Tooltip required level comes from table."), Tooltip.RequiredLevel, 4);
	TestTrue(TEXT("Tooltip unlock flag comes from table."), Tooltip.bUnlockedByDefault);

	Slot.Level = 2;
	const FAeyerjiAbilityTooltipData RankTwoTooltip = FAeyerjiAbilityTooltipData::FromSlot(nullptr, Slot, EAbilityTooltipSource::AbilityPicker);
	TestEqual(TEXT("Ranked tooltip mana cost uses merged rank config."), RankTwoTooltip.ManaCost, 20.f);
	TestEqual(TEXT("Ranked tooltip cooldown uses merged rank config."), RankTwoTooltip.CooldownSeconds, 5.f);

	const UGE_AeyerjiAbilityCostMana* CostCDO = GetDefault<UGE_AeyerjiAbilityCostMana>();
	TestEqual(TEXT("Standard mana cost GE is instant."), CostCDO->DurationPolicy, EGameplayEffectDurationType::Instant);
	TestEqual(TEXT("Standard mana cost GE has one modifier."), CostCDO->Modifiers.Num(), 1);

	const UGE_AeyerjiAbilityCooldown* CooldownCDO = GetDefault<UGE_AeyerjiAbilityCooldown>();
	TestEqual(TEXT("Standard cooldown GE has duration."), CooldownCDO->DurationPolicy, EGameplayEffectDurationType::HasDuration);

	Settings->AbilityTuningTable = PreviousTable;
	Settings->AbilityRankTuningTable = PreviousRankTable;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiAbilityRankMergeTest,
	"Aeyerji.Abilities.Tuning.RankMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiAbilityRankMergeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UDataTable* BaseTable = MakeAbilityTable(GetTransientPackage());
	UDataTable* RankTable = MakeAbilityRankTable(GetTransientPackage());

	UAeyerjiAbilityTuningSubsystem* Tuning = NewObject<UAeyerjiAbilityTuningSubsystem>(GetTransientPackage());
	Tuning->SetRuntimeAbilityTuningTable(BaseTable);
	Tuning->SetRuntimeAbilityRankTuningTable(RankTable);

	const FGameplayTag AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.GravitonPull"), false);
	FAeyerjiAbilityResolvedConfig Config;
	TestTrue(TEXT("Ranked ability config resolves."), Tuning->ResolveAbilityConfig(AbilityTag, 2, Config));
	TestEqual(TEXT("Resolved rank is preserved."), Config.Rank, 2);
	TestEqual(TEXT("Rank override replaces mana cost."), Config.Cost.ManaCost, 20.f);
	TestEqual(TEXT("Rank override replaces cooldown."), Config.Cost.Cooldown, 5.f);
	TestEqual(TEXT("Rank override replaces preview range."), Config.PreviewRange, 1200.f);
	TestEqual(TEXT("Rank override replaces damage."), Config.Damage.FlatValue, 64.f);
	TestEqual(TEXT("Authored max rank resolves."), Tuning->GetMaxAbilityRank(AbilityTag), 2);

	float PullDistance = 0.f;
	TestTrue(TEXT("Rank tunable overlay resolves by key."), Config.TryGetFloatTunable(AeyerjiTags::SBC_Damage_Instant, PullDistance));
	TestEqual(TEXT("Rank tunable overlay replaces the base value."), PullDistance, 1800.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiAbilityCooldownReductionTest,
	"Aeyerji.Abilities.CooldownReduction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiAbilityCooldownReductionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UDataTable* Table = MakeAbilityTable(GetTransientPackage());
	UDataTable* RankTable = MakeAbilityRankTable(GetTransientPackage());
	UAeyerjiAbilityTuningSettings* Settings = GetMutableDefault<UAeyerjiAbilityTuningSettings>();
	const TSoftObjectPtr<UDataTable> PreviousTable = Settings->AbilityTuningTable;
	const TSoftObjectPtr<UDataTable> PreviousRankTable = Settings->AbilityRankTuningTable;
	Settings->AbilityTuningTable = TSoftObjectPtr<UDataTable>(Table);
	Settings->AbilityRankTuningTable = TSoftObjectPtr<UDataTable>(RankTable);

	UGA_AGGravitonPull* Ability = NewObject<UGA_AGGravitonPull>(GetTransientPackage());
	Ability->AbilityTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.GravitonPull"), false);

	float ManaCost = 0.f;
	float Cooldown = 0.f;
	Ability->EvaluateAbilityCostAndCooldown(nullptr, ManaCost, Cooldown);

	TestEqual(TEXT("Mana cost remains table-authored."), ManaCost, 12.f);
	TestTrue(TEXT("Base cooldown remains table-authored."), FMath::IsNearlyEqual(Cooldown, 3.f));
	TestTrue(TEXT("10% cooldown reduction turns 3.0 into 2.7."),
		FMath::Abs(Ability->ResolveCooldownWithReduction(Cooldown, 0.10f) - 2.7f) < 0.001f);

	TestTrue(TEXT("Cooldown reduction clamps at 40%."),
		FMath::Abs(Ability->ResolveCooldownWithReduction(Cooldown, 1.0f) - 1.8f) < 0.001f);

	Ability->EvaluateAbilityCostAndCooldown(nullptr, 2, ManaCost, Cooldown);
	TestEqual(TEXT("Ranked mana cost resolves from rank config."), ManaCost, 20.f);
	TestEqual(TEXT("Ranked cooldown resolves from rank config."), Cooldown, 5.f);

	Settings->AbilityTuningTable = PreviousTable;
	Settings->AbilityRankTuningTable = PreviousRankTable;
	return true;
}

#endif
