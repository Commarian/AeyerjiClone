// ItemGenerator.cpp

#include "Items/ItemGenerator.h"

#include "Items/ItemAffixDefinition.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemInstance.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/LootService.h"
#include "Systems/LootTable.h"
#include "UObject/Package.h"

UAeyerjiItemInstance* UItemGenerator::RollItemInstance(
	UObject* WorldContext,
	UItemDefinition* Definition,
	int32 ItemLevel,
	EItemRarity Rarity,
	int32 SeedOverride,
	EEquipmentSlot SlotOverride)
{
	if (!Definition)
	{
		return nullptr;
	}

	const int32 ClampedItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(ItemLevel);

	int32 MinAffixes = 0;
	int32 MaxAffixes = 0;
	Definition->GetAffixCountRange(Rarity, MinAffixes, MaxAffixes);

	const UAeyerjiLootTable* CachedTable = nullptr;
	if (WorldContext)
	{
		if (UWorld* World = WorldContext->GetWorld())
		{
			if (UGameInstance* GI = World->GetGameInstance())
			{
				if (ULootService* LootService = GI->GetSubsystem<ULootService>())
				{
					CachedTable = LootService->GetLootTable();
				}
			}
		}
	}

	if (CachedTable)
	{
		if (const FRarityScalingRow* RarityRow = CachedTable->FindRarityScaling(Rarity))
		{
			const int32 Bonus = FMath::Max(0, RarityRow->BonusAffixes);
			MinAffixes = FMath::Max(0, MinAffixes + Bonus);
			MaxAffixes = FMath::Max(MinAffixes, MaxAffixes + Bonus);
		}
	}

	const int32 EffectiveSeed = (SeedOverride != 0) ? SeedOverride : FMath::Rand();
	FRandomStream RNG(EffectiveSeed);

	int32 AffixCount = MinAffixes;
	if (MaxAffixes > MinAffixes)
	{
		AffixCount = RNG.RandRange(MinAffixes, MaxAffixes);
	}

	const EEquipmentSlot FinalSlot =
		(SlotOverride == EEquipmentSlot::Assault && Definition->DefaultSlot != EEquipmentSlot::Assault)
			? Definition->DefaultSlot
			: SlotOverride;

	TArray<UItemAffixDefinition*> ChosenAffixes;
	TArray<const FAffixTier*> ChosenTiers;
	FGameplayTagContainer ChosenAffixTags;
	FGameplayTagContainer ChosenExclusionTags;
	TSet<const UItemAffixDefinition*> ChosenAffixDefinitions;

	for (UItemAffixDefinition* GuaranteedAffix : Definition->GuaranteedAffixes)
	{
		if (!GuaranteedAffix)
		{
			continue;
		}

		if (!GuaranteedAffix->IsAllowedFor(Definition->ItemCategory, FinalSlot))
		{
			continue;
		}

		const FAffixTier* Tier = GuaranteedAffix->RollTier(RNG, ClampedItemLevel);
		if (!Tier)
		{
			continue;
		}

		ChosenAffixes.Add(GuaranteedAffix);
		ChosenTiers.Add(Tier);
		ChosenAffixTags.AppendTags(GuaranteedAffix->AffixTags);
		ChosenExclusionTags.AppendTags(GuaranteedAffix->ExclusionTags);
		ChosenAffixDefinitions.Add(GuaranteedAffix);
	}

	if (AffixCount > 0)
	{
		TArray<UItemAffixDefinition*> OptionalAffixes;
		TArray<const FAffixTier*> OptionalTiers;
		ChooseAffixes(
			Definition,
			Definition->OptionalAffixPool,
			ClampedItemLevel,
			FinalSlot,
			AffixCount,
			RNG,
			ChosenAffixTags,
			ChosenExclusionTags,
			ChosenAffixDefinitions,
			OptionalAffixes,
			OptionalTiers);

		ChosenAffixes.Append(OptionalAffixes);
		ChosenTiers.Append(OptionalTiers);
	}

	UObject* Outer = WorldContext ? WorldContext : GetTransientPackage();
	const FName InstanceName = MakeUniqueObjectName(Outer, UAeyerjiItemInstance::StaticClass(), TEXT("AeyerjiItemInstance"));
	UAeyerjiItemInstance* NewInstance = NewObject<UAeyerjiItemInstance>(Outer, UAeyerjiItemInstance::StaticClass(), InstanceName);
	if (!NewInstance)
	{
		return nullptr;
	}

	NewInstance->InitializeFromDefinition(
		Definition,
		Rarity,
		ClampedItemLevel,
		EffectiveSeed,
		FinalSlot,
		ChosenAffixes,
		ChosenTiers);

	// Apply stat scaling from the loot table if available.
	if (CachedTable)
	{
		NewInstance->ApplyLootStatScaling(CachedTable);
		NewInstance->ForceItemChangedForUI();
	}

	return NewInstance;
}

void UItemGenerator::ChooseAffixes(
	const UItemDefinition* Definition,
	const TArray<TObjectPtr<UItemAffixDefinition>>& SourcePool,
	int32 ItemLevel,
	EEquipmentSlot Slot,
	int32 AffixCount,
	FRandomStream& RNG,
	FGameplayTagContainer& InOutChosenAffixTags,
	FGameplayTagContainer& InOutChosenExclusionTags,
	TSet<const UItemAffixDefinition*>& InOutChosenAffixDefinitions,
	TArray<UItemAffixDefinition*>& OutAffixes,
	TArray<const FAffixTier*>& OutTiers)
{
	OutAffixes.Reset();
	OutTiers.Reset();

	if (!Definition || AffixCount <= 0)
	{
		return;
	}

	const int32 ClampedItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(ItemLevel);

	TArray<UItemAffixDefinition*> Pool;
	for (UItemAffixDefinition* Candidate : SourcePool)
	{
		if (!Candidate)
		{
			continue;
		}

		if (InOutChosenAffixDefinitions.Contains(Candidate))
		{
			continue;
		}

		if (!Candidate->IsAllowedFor(Definition->ItemCategory, Slot))
		{
			continue;
		}

		if (Candidate->GetTotalWeight(ClampedItemLevel) <= 0)
		{
			continue;
		}

		Pool.Add(Candidate);
	}

	const auto IsCompatibleWithChosen = [&InOutChosenAffixTags, &InOutChosenExclusionTags](const UItemAffixDefinition* Candidate)
	{
		if (!Candidate)
		{
			return false;
		}

		// Candidate forbids tags that already exist on the item.
		if (!Candidate->ExclusionTags.IsEmpty() && Candidate->ExclusionTags.HasAny(InOutChosenAffixTags))
		{
			return false;
		}

		// Already-chosen affixes forbid tags on the candidate.
		if (!InOutChosenExclusionTags.IsEmpty() && !Candidate->AffixTags.IsEmpty() && InOutChosenExclusionTags.HasAny(Candidate->AffixTags))
		{
			return false;
		}

		return true;
	};

	for (int32 Index = 0; Index < AffixCount && Pool.Num() > 0; ++Index)
	{
		// Enforce mutual exclusivity using AffixTags/ExclusionTags as the list grows.
		Pool.RemoveAllSwap([&IsCompatibleWithChosen](const UItemAffixDefinition* Candidate)
		{
			return !IsCompatibleWithChosen(Candidate);
		});
		if (Pool.Num() == 0)
		{
			break;
		}

		const int32 PickIdx = RNG.RandRange(0, Pool.Num() - 1);
		UItemAffixDefinition* Pick = Pool[PickIdx];

		const FAffixTier* Tier = Pick ? Pick->RollTier(RNG, ClampedItemLevel) : nullptr;
		if (Pick && Tier)
		{
			OutAffixes.Add(Pick);
			OutTiers.Add(Tier);

			InOutChosenAffixTags.AppendTags(Pick->AffixTags);
			InOutChosenExclusionTags.AppendTags(Pick->ExclusionTags);
			InOutChosenAffixDefinitions.Add(Pick);
		}

		Pool.RemoveAtSwap(PickIdx);
	}
}
