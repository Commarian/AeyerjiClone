#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Abilities/GameplayAbility.h"
#include "Abilities/AeyerjiAbilityTuning.h"
#include "Abilities/GravitonPull/GA_AGGravitonPull.h"
#include "AeyerjiGameplayTags.h"
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

		Table->AddRow(Row.AbilityTag.GetTagName(), Row);
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
	UAeyerjiAbilityTuningSettings* Settings = GetMutableDefault<UAeyerjiAbilityTuningSettings>();
	const TSoftObjectPtr<UDataTable> PreviousTable = Settings->AbilityTuningTable;
	Settings->AbilityTuningTable = TSoftObjectPtr<UDataTable>(Table);

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

	const UGE_AeyerjiAbilityCostMana* CostCDO = GetDefault<UGE_AeyerjiAbilityCostMana>();
	TestEqual(TEXT("Standard mana cost GE is instant."), CostCDO->DurationPolicy, EGameplayEffectDurationType::Instant);
	TestEqual(TEXT("Standard mana cost GE has one modifier."), CostCDO->Modifiers.Num(), 1);

	const UGE_AeyerjiAbilityCooldown* CooldownCDO = GetDefault<UGE_AeyerjiAbilityCooldown>();
	TestEqual(TEXT("Standard cooldown GE has duration."), CooldownCDO->DurationPolicy, EGameplayEffectDurationType::HasDuration);

	Settings->AbilityTuningTable = PreviousTable;
	return true;
}

#endif
