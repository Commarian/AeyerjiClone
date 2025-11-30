// ItemAffixDefinition.cpp

#include "Items/ItemAffixDefinition.h"

int32 UItemAffixDefinition::GetTotalWeight(int32 ItemLevel) const
{
	int32 Sum = 0;
	for (const FAffixTier& Tier : Tiers)
	{
		if (ItemLevel >= Tier.MinItemLevel)
		{
			Sum += FMath::Max(0, Tier.Weight);
		}
	}
	return FMath::Max(0, Sum);
}

const FAffixTier* UItemAffixDefinition::RollTier(FRandomStream& RNG, int32 ItemLevel) const
{
	const int32 TotalWeight = GetTotalWeight(ItemLevel);
	if (TotalWeight <= 0 || Tiers.Num() == 0)
	{
		return nullptr;
	}

	const int32 Pick = RNG.RandRange(1, TotalWeight);
	int32 Accumulator = 0;
	for (const FAffixTier& Tier : Tiers)
	{
		if (ItemLevel < Tier.MinItemLevel)
		{
			continue;
		}

		Accumulator += FMath::Max(0, Tier.Weight);
		if (Pick <= Accumulator)
		{
			return &Tier;
		}
	}

	return nullptr;
}

void UItemAffixDefinition::BuildFinalModifiers(const FAffixTier& Tier, FRandomStream& RNG, TArray<FItemStatModifier>& OutMods) const
{
	for (const FAttributeRoll& AttributeRoll : AttributeContributions)
	{
		if (!AttributeRoll.Attribute.IsValid())
		{
			continue;
		}

		const float MinRoll = FMath::Min(Tier.MinRoll, Tier.MaxRoll);
		const float MaxRoll = FMath::Max(Tier.MinRoll, Tier.MaxRoll);
		const float UnitRoll = RNG.FRandRange(MinRoll, MaxRoll);

		FItemStatModifier NewModifier;
		NewModifier.Attribute = AttributeRoll.Attribute;
		NewModifier.Op = AttributeRoll.Op;
		NewModifier.Magnitude = AttributeRoll.Scale * UnitRoll;

		OutMods.Add(MoveTemp(NewModifier));
	}
}

bool UItemAffixDefinition::IsAllowedFor(EItemCategory ItemCategory, EEquipmentSlot Slot) const
{
	if (!AllowedCategories.IsEmpty() && !AllowedCategories.Contains(ItemCategory))
	{
		return false;
	}

	if (!SlotFilter.AllowedSlots.IsEmpty() && !SlotFilter.AllowedSlots.Contains(Slot))
	{
		return false;
	}

	return true;
}
