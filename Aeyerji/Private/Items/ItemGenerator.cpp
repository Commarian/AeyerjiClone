// ItemGenerator.cpp

#include "Items/ItemGenerator.h"

#include "Items/ItemAffixDefinition.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemInstance.h"
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

	int32 MinAffixes = 0;
	int32 MaxAffixes = 0;
	Definition->GetAffixCountRange(Rarity, MinAffixes, MaxAffixes);

	const int32 EffectiveSeed = (SeedOverride != 0) ? SeedOverride : FMath::Rand();
	FRandomStream RNG(EffectiveSeed);

	int32 AffixCount = MinAffixes;
	if (MaxAffixes > MinAffixes)
	{
		AffixCount = RNG.RandRange(MinAffixes, MaxAffixes);
	}

	const EEquipmentSlot FinalSlot =
		(SlotOverride == EEquipmentSlot::Offense && Definition->DefaultSlot != EEquipmentSlot::Offense)
			? Definition->DefaultSlot
			: SlotOverride;

	TArray<UItemAffixDefinition*> ChosenAffixes;
	TArray<const FAffixTier*> ChosenTiers;
	if (AffixCount > 0)
	{
		ChooseAffixes(Definition, ItemLevel, FinalSlot, AffixCount, RNG, ChosenAffixes, ChosenTiers);
	}

	UObject* Outer = WorldContext ? WorldContext : GetTransientPackage();
	UAeyerjiItemInstance* NewInstance = NewObject<UAeyerjiItemInstance>(Outer);
	if (!NewInstance)
	{
		return nullptr;
	}

	NewInstance->InitializeFromDefinition(
		Definition,
		Rarity,
		ItemLevel,
		EffectiveSeed,
		FinalSlot,
		ChosenAffixes,
		ChosenTiers);

	return NewInstance;
}

void UItemGenerator::ChooseAffixes(
	UItemDefinition* Definition,
	int32 ItemLevel,
	EEquipmentSlot Slot,
	int32 AffixCount,
	FRandomStream& RNG,
	TArray<UItemAffixDefinition*>& OutAffixes,
	TArray<const FAffixTier*>& OutTiers)
{
	OutAffixes.Reset();
	OutTiers.Reset();

	if (!Definition || AffixCount <= 0)
	{
		return;
	}

	TArray<UItemAffixDefinition*> Pool;
	for (UItemAffixDefinition* Candidate : Definition->AffixPool)
	{
		if (!Candidate)
		{
			continue;
		}

		if (!Candidate->IsAllowedFor(Definition->ItemCategory, Slot))
		{
			continue;
		}

		if (Candidate->GetTotalWeight(ItemLevel) <= 0)
		{
			continue;
		}

		Pool.Add(Candidate);
	}

	for (int32 Index = 0; Index < AffixCount && Pool.Num() > 0; ++Index)
	{
		const int32 PickIdx = RNG.RandRange(0, Pool.Num() - 1);
		UItemAffixDefinition* Pick = Pool[PickIdx];

		const FAffixTier* Tier = Pick ? Pick->RollTier(RNG, ItemLevel) : nullptr;
		if (Pick && Tier)
		{
			OutAffixes.Add(Pick);
			OutTiers.Add(Tier);
		}

		Pool.RemoveAtSwap(PickIdx);
	}
}
