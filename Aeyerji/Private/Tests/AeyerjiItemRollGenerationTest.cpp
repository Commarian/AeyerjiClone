#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AeyerjiGameplayTags.h"
#include "Abilities/GameplayAbility.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "GameplayEffect.h"
#include "Items/ItemAffixDefinition.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemGenerator.h"
#include "Items/ItemInstance.h"
#include "Systems/AeyerjiDifficultyTuning.h"

#include <limits>

namespace
{
	UItemAffixDefinition* MakeRollAffix(
		UObject* Outer,
		const FName AffixId,
		const FGameplayAttribute& Attribute,
		const float Magnitude)
	{
		UItemAffixDefinition* Affix = NewObject<UItemAffixDefinition>(Outer);
		Affix->AffixId = AffixId;
		Affix->DisplayName = FText::FromName(AffixId);

		FAffixTier Tier;
		Tier.Weight = 1;
		Tier.MinItemLevel = 1;
		Tier.MinRoll = Magnitude;
		Tier.MaxRoll = Magnitude;
		Affix->Tiers.Add(Tier);

		FAttributeRoll Contribution;
		Contribution.Attribute = Attribute;
		Contribution.Scale = 1.f;
		Contribution.Op = EItemModOp::Additive;
		Affix->AttributeContributions.Add(Contribution);

		return Affix;
	}

	UItemDefinition* MakeRollDefinition(UObject* Outer)
	{
		UItemDefinition* Definition = NewObject<UItemDefinition>(Outer);
		Definition->ItemCategory = EItemCategory::Assault;
		Definition->DefaultSlot = EEquipmentSlot::Assault;
		Definition->RarityAffixRanges.Reset();
		Definition->RarityAffixRanges.Add(FItemRarityAffixRange(EItemRarity::Common, 1, 1));
		return Definition;
	}

	bool HasRolledAffix(const UAeyerjiItemInstance* Item, const FName AffixId)
	{
		if (!Item)
		{
			return false;
		}

		return Item->RolledAffixes.ContainsByPredicate(
			[AffixId](const FRolledAffix& Rolled)
			{
				return Rolled.AffixId == AffixId;
			});
	}

	float SumModifierMagnitude(const UAeyerjiItemInstance* Item, const FGameplayAttribute& Attribute)
	{
		float Total = 0.f;
		if (!Item)
		{
			return Total;
		}

		for (const FItemStatModifier& Modifier : Item->FinalAggregatedModifiers)
		{
			if (Modifier.Attribute == Attribute)
			{
				Total += Modifier.Magnitude;
			}
		}

		return Total;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiItemGenerationGuaranteedAndOptionalTest,
	"Aeyerji.Items.Generation.GuaranteedAndOptionalAffixes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiItemGenerationGuaranteedAndOptionalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UItemDefinition* Definition = MakeRollDefinition(GetTransientPackage());
	UItemAffixDefinition* Guaranteed = MakeRollAffix(
		Definition,
		TEXT("Guaranteed_AttackDamage"),
		UAeyerjiAttributeSet::GetAttackDamageAttribute(),
		8.f);
	UItemAffixDefinition* Optional = MakeRollAffix(
		Definition,
		TEXT("Optional_AttackSpeed"),
		UAeyerjiAttributeSet::GetAttackSpeedAttribute(),
		3.f);

	Definition->GuaranteedAffixes.Add(Guaranteed);
	Definition->OptionalAffixPool.Add(Optional);

	FItemGrantedEffect GrantedEffect;
	GrantedEffect.EffectClass = UGameplayEffect::StaticClass();
	GrantedEffect.EffectLevel = 2.f;
	Definition->GrantedEffects.Add(GrantedEffect);

	FItemGrantedAbility GrantedAbility;
	GrantedAbility.AbilityClass = UGameplayAbility::StaticClass();
	GrantedAbility.AbilityLevel = 3;
	Definition->GrantedAbilities.Add(GrantedAbility);

	UAeyerjiItemInstance* Item = UItemGenerator::RollItemInstance(
		GetTransientPackage(),
		Definition,
		1,
		EItemRarity::Common,
		123,
		EEquipmentSlot::Assault);

	TestNotNull(TEXT("Item rolls successfully."), Item);
	if (!Item)
	{
		return false;
	}

	TestEqual(TEXT("Guaranteed plus one common optional affix are stored."), Item->RolledAffixes.Num(), 2);
	TestTrue(TEXT("Guaranteed affix rolled."), HasRolledAffix(Item, Guaranteed->AffixId));
	TestTrue(TEXT("Optional affix rolled."), HasRolledAffix(Item, Optional->AffixId));
	TestTrue(TEXT("Guaranteed modifier contributes to final stats."), FMath::IsNearlyEqual(SumModifierMagnitude(Item, UAeyerjiAttributeSet::GetAttackDamageAttribute()), 8.f));
	TestTrue(TEXT("Optional modifier contributes to final stats."), FMath::IsNearlyEqual(SumModifierMagnitude(Item, UAeyerjiAttributeSet::GetAttackSpeedAttribute()), 3.f));
	TestEqual(TEXT("Definition-level granted effect remains present."), Item->GrantedEffects.Num(), 1);
	TestEqual(TEXT("Definition-level granted ability remains present."), Item->GrantedAbilities.Num(), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiItemGenerationRarityCountsOptionalOnlyTest,
	"Aeyerji.Items.Generation.RarityCountsOptionalOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiItemGenerationRarityCountsOptionalOnlyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UItemDefinition* Definition = MakeRollDefinition(GetTransientPackage());
	UItemAffixDefinition* GuaranteedAttack = MakeRollAffix(
		Definition,
		TEXT("Guaranteed_AttackDamage"),
		UAeyerjiAttributeSet::GetAttackDamageAttribute(),
		6.f);
	UItemAffixDefinition* GuaranteedHealth = MakeRollAffix(
		Definition,
		TEXT("Guaranteed_HPMax"),
		UAeyerjiAttributeSet::GetHPMaxAttribute(),
		20.f);
	UItemAffixDefinition* OptionalMana = MakeRollAffix(
		Definition,
		TEXT("Optional_ManaMax"),
		UAeyerjiAttributeSet::GetManaMaxAttribute(),
		15.f);

	Definition->GuaranteedAffixes.Add(GuaranteedAttack);
	Definition->GuaranteedAffixes.Add(GuaranteedHealth);
	Definition->OptionalAffixPool.Add(OptionalMana);

	UAeyerjiItemInstance* Item = UItemGenerator::RollItemInstance(
		GetTransientPackage(),
		Definition,
		1,
		EItemRarity::Common,
		456,
		EEquipmentSlot::Assault);

	TestNotNull(TEXT("Item rolls successfully."), Item);
	if (!Item)
	{
		return false;
	}

	TestEqual(TEXT("Common range adds one optional affix in addition to both guaranteed affixes."), Item->RolledAffixes.Num(), 3);
	TestTrue(TEXT("First guaranteed affix rolled."), HasRolledAffix(Item, GuaranteedAttack->AffixId));
	TestTrue(TEXT("Second guaranteed affix rolled."), HasRolledAffix(Item, GuaranteedHealth->AffixId));
	TestTrue(TEXT("Optional affix rolled."), HasRolledAffix(Item, OptionalMana->AffixId));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiItemGenerationGuaranteedSeedsOptionalExclusionsTest,
	"Aeyerji.Items.Generation.GuaranteedSeedsOptionalExclusions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiItemGenerationGuaranteedSeedsOptionalExclusionsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UItemDefinition* Definition = MakeRollDefinition(GetTransientPackage());
	UItemAffixDefinition* Guaranteed = MakeRollAffix(
		Definition,
		TEXT("Guaranteed_AttackDamage"),
		UAeyerjiAttributeSet::GetAttackDamageAttribute(),
		5.f);
	UItemAffixDefinition* BlockedOptional = MakeRollAffix(
		Definition,
		TEXT("Optional_BlockedDuplicate"),
		UAeyerjiAttributeSet::GetAttackDamageAttribute(),
		50.f);
	UItemAffixDefinition* AllowedOptional = MakeRollAffix(
		Definition,
		TEXT("Optional_AllowedMana"),
		UAeyerjiAttributeSet::GetManaMaxAttribute(),
		12.f);

	Guaranteed->ExclusionTags.AddTag(AeyerjiTags::Ability_Primary);
	BlockedOptional->AffixTags.AddTag(AeyerjiTags::Ability_Primary);

	Definition->GuaranteedAffixes.Add(Guaranteed);
	Definition->OptionalAffixPool.Add(BlockedOptional);
	Definition->OptionalAffixPool.Add(AllowedOptional);

	UAeyerjiItemInstance* Item = UItemGenerator::RollItemInstance(
		GetTransientPackage(),
		Definition,
		1,
		EItemRarity::Common,
		789,
		EEquipmentSlot::Assault);

	TestNotNull(TEXT("Item rolls successfully."), Item);
	if (!Item)
	{
		return false;
	}

	TestEqual(TEXT("Guaranteed plus one compatible optional affix are stored."), Item->RolledAffixes.Num(), 2);
	TestTrue(TEXT("Guaranteed affix rolled."), HasRolledAffix(Item, Guaranteed->AffixId));
	TestFalse(TEXT("Conflicting optional affix is blocked by guaranteed exclusion tags."), HasRolledAffix(Item, BlockedOptional->AffixId));
	TestTrue(TEXT("Compatible optional affix rolled instead."), HasRolledAffix(Item, AllowedOptional->AffixId));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiItemGenerationClampsAboveMaxLevelTest,
	"Aeyerji.Items.Generation.ClampsAboveMaxLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiItemGenerationClampsAboveMaxLevelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UItemDefinition* Definition = MakeRollDefinition(GetTransientPackage());
	UAeyerjiItemInstance* Item = UItemGenerator::RollItemInstance(
		GetTransientPackage(),
		Definition,
		UAeyerjiDifficultySettings::GetMaxGameplayLevel() + 2,
		EItemRarity::Common,
		321,
		EEquipmentSlot::Assault);

	TestNotNull(TEXT("Item rolls successfully above max requested level."), Item);
	if (!Item)
	{
		return false;
	}

	TestEqual(TEXT("Generated item level is clamped to max gameplay level."), Item->ItemLevel, UAeyerjiDifficultySettings::GetMaxGameplayLevel());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiItemRarityTintUsesArgumentTest,
	"Aeyerji.Items.Display.RarityTintUsesArgument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiItemRarityTintUsesArgumentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UAeyerjiItemInstance* Item = NewObject<UAeyerjiItemInstance>(GetTransientPackage());
	TestNotNull(TEXT("Item instance can be constructed."), Item);
	if (!Item)
	{
		return false;
	}

	Item->Rarity = EItemRarity::Common;
	const FLinearColor CommonTint = Item->RarityTint(EItemRarity::Common);
	const FLinearColor LegendaryTint = Item->RarityTint(EItemRarity::Legendary);
	TestFalse(TEXT("The requested rarity controls the tint instead of the instance field."),
		CommonTint.Equals(LegendaryTint));
	TestTrue(TEXT("Legendary tint matches the authored display color."),
		LegendaryTint.Equals(FLinearColor(1.f, 0.6f, 0.2f, 1.f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiDifficultyFloatLevelConversionTest,
	"Aeyerji.Difficulty.FloatLevelConversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiDifficultyFloatLevelConversionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestEqual(TEXT("Finite fractional levels use the shared rounding rule."),
		UAeyerjiDifficultySettings::FloatToGameplayLevel(4.6f), 5);
	TestEqual(TEXT("NaN levels fall back to level one."),
		UAeyerjiDifficultySettings::FloatToGameplayLevel(std::numeric_limits<float>::quiet_NaN()), 1);
	TestEqual(TEXT("Infinite levels fall back to level one."),
		UAeyerjiDifficultySettings::FloatToGameplayLevel(std::numeric_limits<float>::infinity()), 1);
	TestEqual(TEXT("Levels above the authored cap clamp globally."),
		UAeyerjiDifficultySettings::FloatToGameplayLevel(1000000.f),
		UAeyerjiDifficultySettings::GetMaxGameplayLevel());
	return true;
}

#endif
