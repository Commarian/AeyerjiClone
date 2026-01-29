// AbilityTooltipData.cpp

#include "GUI/AbilityTooltipData.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/AeyerjiAbilityData.h"
#include "UObject/UnrealType.h"

const UAeyerjiAbilityData* FAeyerjiAbilityTooltipData::ResolveAbilityData(TSubclassOf<UGameplayAbility> AbilityClass)
{
	if (!AbilityClass)
	{
		return nullptr;
	}

	const UGameplayAbility* AbilityCDO = AbilityClass->GetDefaultObject<UGameplayAbility>();
	if (!AbilityCDO)
	{
		return nullptr;
	}

	const FObjectProperty* DataProp = nullptr;
	for (TFieldIterator<FObjectProperty> It(AbilityClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		if (It->PropertyClass && It->PropertyClass->IsChildOf(UAeyerjiAbilityData::StaticClass()))
		{
			DataProp = *It;
			break;
		}
	}

	if (!DataProp)
	{
		return nullptr;
	}

	return Cast<UAeyerjiAbilityData>(DataProp->GetObjectPropertyValue_InContainer(AbilityCDO));
}

FAeyerjiAbilityTooltipData FAeyerjiAbilityTooltipData::FromSlot(
	const UAbilitySystemComponent* ASC,
	const FAeyerjiAbilitySlot& Slot,
	EAbilityTooltipSource InSource)
{
	FAeyerjiAbilityTooltipData Data;
	Data.Slot = Slot;
	Data.Source = InSource;

	Data.Icon = Slot.Icon;
	Data.DisplayName = Slot.Description.IsNone() ? FText::GetEmpty() : FText::FromName(Slot.Description);

	if (const UAeyerjiAbilityData* AbilityData = ResolveAbilityData(Slot.Class))
	{
		if (!AbilityData->DisplayName.IsEmpty())
		{
			Data.DisplayName = AbilityData->DisplayName;
		}

		Data.Description = AbilityData->Description;

		if (!Data.Icon && AbilityData->Icon)
		{
			Data.Icon = AbilityData->Icon;
		}

		const FAeyerjiAbilityCost Cost = AbilityData->EvaluateCost(ASC);
		Data.ManaCost = Cost.ManaCost;
		Data.CooldownSeconds = Cost.Cooldown;
	}

	return Data;
}
