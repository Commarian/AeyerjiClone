// AbilityTooltipData.cpp

#include "GUI/AbilityTooltipData.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/AeyerjiAbilityTuning.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "Abilities/GA_AeyerjiTargetedEffectBase.h"

bool FAeyerjiAbilityTooltipData::ResolveAbilityConfig(const FAeyerjiAbilitySlot& Slot, FAeyerjiAbilityResolvedConfig& OutConfig)
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

	if (!AbilityTag.IsValid())
	{
		OutConfig = FAeyerjiAbilityResolvedConfig();
		return false;
	}

	const UDataTable* BaseTable = UAeyerjiAbilityTuningSubsystem::ResolveConfiguredTable();
	const FAeyerjiAbilityTableRow* BaseRow = UAeyerjiAbilityTuningSubsystem::FindAbilityRowInTable(BaseTable, AbilityTag);
	if (!BaseRow || !UAeyerjiAbilityTuningSubsystem::MakeResolvedConfigFromBaseRow(*BaseRow, OutConfig))
	{
		OutConfig = FAeyerjiAbilityResolvedConfig();
		return false;
	}

	OutConfig.Rank = FMath::Max(1, Slot.Level);
	if (OutConfig.Rank > 1)
	{
		if (const UDataTable* RankTable = UAeyerjiAbilityTuningSubsystem::ResolveConfiguredRankTable())
		{
			if (const FAeyerjiAbilityRankTableRow* RankRow = UAeyerjiAbilityTuningSubsystem::FindAbilityRankRowInTable(RankTable, AbilityTag, OutConfig.Rank))
			{
				UAeyerjiAbilityTuningSubsystem::ApplyRankOverrides(*RankRow, OutConfig);
			}
		}
	}

	return true;
}

namespace
{
	UTexture2D* LoadIcon(const TSoftObjectPtr<UTexture2D>& Icon)
	{
		if (Icon.IsNull())
		{
			return nullptr;
		}

		return Icon.Get();
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
	FAeyerjiAbilityResolvedConfig Config;
	if (ResolveAbilityConfig(Slot, Config))
	{
		if (!Config.DisplayName.IsEmpty())
		{
			Data.DisplayName = Config.DisplayName;
		}

		if (!Config.Description.IsEmpty())
		{
			Data.Description = Config.Description;
		}

		if (!Data.Icon)
		{
			Data.Icon = LoadIcon(Config.Icon);
		}

		Data.ManaCost = Config.Cost.ManaCost;
		Data.CooldownSeconds = Config.Cost.Cooldown;
		Data.RequiredLevel = FMath::Max(1, Config.RequiredLevel);
		Data.bUnlockedByDefault = Config.bUnlockedByDefault;
		Data.bUsesDamageVariance = Config.Damage.bUseDamageVariance;
		Data.bCanCrit = Config.Damage.bCanCrit;
		Data.bCanBeDodged = Config.Damage.bCanBeDodged;
		Data.bCanLifeSteal = Config.Damage.bCanLifeSteal;
		Data.bCanTriggerOnHit = Config.Damage.bCanTriggerOnHit;
		Data.bCanStagger = Config.Damage.bCanStagger;
	}
	// Non-table-driven abilities apply their class defaults at runtime, even when a row supplies UI metadata.
	if (AeyerjiAbilityCDO && !Cast<UGA_AeyerjiTargetedEffectBase>(AeyerjiAbilityCDO))
	{
		const FAeyerjiDamageRuleConfig& Rules = AeyerjiAbilityCDO->GetDefaultDamageRules();
		Data.bUsesDamageVariance = Rules.bUseVariance;
		Data.bCanCrit = Rules.bCanCrit;
		Data.bCanBeDodged = Rules.bCanBeDodged;
		Data.bCanLifeSteal = Rules.bCanLifeSteal;
		Data.bCanTriggerOnHit = Rules.bCanTriggerOnHit;
		Data.bCanStagger = Rules.bCanStagger;
	}

	(void)ASC;
	return Data;
}
