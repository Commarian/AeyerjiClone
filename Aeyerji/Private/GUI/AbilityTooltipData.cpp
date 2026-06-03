// AbilityTooltipData.cpp

#include "GUI/AbilityTooltipData.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/AeyerjiAbilityTuning.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "UObject/UnrealType.h"

const FAeyerjiAbilityTableRow* FAeyerjiAbilityTooltipData::ResolveAbilityRow(const FAeyerjiAbilitySlot& Slot)
{
	FGameplayTag AbilityTag;
	if (Slot.Tag.Num() > 0)
	{
		for (const FGameplayTag& Tag : Slot.Tag)
		{
			if (Tag.IsValid() && Tag.ToString().StartsWith(TEXT("Ability.")))
			{
				AbilityTag = Tag;
				break;
			}
		}
	}

	if (!AbilityTag.IsValid() && Slot.Class)
	{
		if (const UGameplayAbility* AbilityCDO = Slot.Class->GetDefaultObject<UGameplayAbility>())
		{
			int32 BestDepth = -1;
			for (const FGameplayTag& Tag : AbilityCDO->GetAssetTags())
			{
				const FString TagString = Tag.ToString();
				if (!TagString.StartsWith(TEXT("Ability.")))
				{
					continue;
				}

				int32 Depth = 1;
				for (const TCHAR Character : TagString)
				{
					if (Character == TEXT('.'))
					{
						++Depth;
					}
				}

				if (Depth > BestDepth)
				{
					BestDepth = Depth;
					AbilityTag = Tag;
				}
			}
		}
	}

	return UAeyerjiAbilityTuningSubsystem::FindAbilityRowInTable(
		UAeyerjiAbilityTuningSubsystem::ResolveConfiguredTable(),
		AbilityTag);
}

namespace
{
	UTexture2D* LoadIcon(const TSoftObjectPtr<UTexture2D>& Icon)
	{
		if (Icon.IsNull())
		{
			return nullptr;
		}

		return Icon.LoadSynchronous();
	}
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

	const UGameplayAbility* AbilityCDO = Slot.Class ? Slot.Class->GetDefaultObject<UGameplayAbility>() : nullptr;
	const UGA_AeyerjiBase* AeyerjiAbilityCDO = Cast<UGA_AeyerjiBase>(AbilityCDO);
	if (const FAeyerjiAbilityTableRow* Row = ResolveAbilityRow(Slot))
	{
		if (!Row->DisplayName.IsEmpty())
		{
			Data.DisplayName = Row->DisplayName;
		}

		if (!Row->Description.IsEmpty())
		{
			Data.Description = Row->Description;
		}

		if (!Data.Icon)
		{
			Data.Icon = LoadIcon(Row->Icon);
		}

		Data.ManaCost = Row->Cost.ManaCost;
		Data.CooldownSeconds = Row->Cost.Cooldown;
		Data.RequiredLevel = FMath::Max(1, Row->RequiredLevel);
		Data.bUnlockedByDefault = Row->bUnlockedByDefault;
	}
	else if (AeyerjiAbilityCDO)
	{
		// Deprecated fallback for un-migrated rows; runtime ability logic still uses table data only.
		Data.RequiredLevel = FMath::Max(1, AeyerjiAbilityCDO->RequiredLevel);
		Data.bUnlockedByDefault = AeyerjiAbilityCDO->bUnlockedByDefault;
	}

	(void)ASC;
	return Data;
}
