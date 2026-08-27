// ItemAffixDefinition.cpp

#include "Items/ItemAffixDefinition.h"

#include "Attributes/AeyerjiAttributeSet.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr int32 MaxAffixTiersToInspect = 256;
	constexpr int32 MaxAffixContributions = 256;
	constexpr int32 MaxAffixWeight = 1000000000;
	constexpr float MaxAffixMagnitude = 1000000000.f;

	bool IsUsableAffixAttribute(const FGameplayAttribute& Attribute)
	{
		const FProperty* Property = Attribute.GetUProperty();
		if (!Property || !FGameplayAttribute::IsGameplayAttributeDataProperty(Property))
		{
			return false;
		}
		const UClass* AttributeSetClass = Cast<UClass>(Property->GetOwner<UObject>());
		return AttributeSetClass && AttributeSetClass->IsChildOf(UAeyerjiAttributeSet::StaticClass());
	}

	bool IsValidAffixOperation(const EItemModOp Operation)
	{
		const UEnum* Enum = StaticEnum<EItemModOp>();
		return Enum && Enum->IsValidEnumValue(static_cast<int64>(Operation));
	}
}

int32 UItemAffixDefinition::GetTotalWeight(int32 ItemLevel) const
{
	const int32 ClampedItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(ItemLevel);
	int64 Sum = 0;
	const int32 TierCount = FMath::Min(Tiers.Num(), MaxAffixTiersToInspect);
	for (int32 TierIndex = 0; TierIndex < TierCount; ++TierIndex)
	{
		const FAffixTier& Tier = Tiers[TierIndex];
		const int32 ClampedTierMinLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Tier.MinItemLevel);
		if (ClampedItemLevel >= ClampedTierMinLevel)
		{
			Sum = FMath::Min<int64>(Sum + FMath::Clamp<int64>(Tier.Weight, 0, MaxAffixWeight), MaxAffixWeight);
		}
	}
	return static_cast<int32>(FMath::Max<int64>(0, Sum));
}

const FAffixTier* UItemAffixDefinition::RollTier(FRandomStream& RNG, int32 ItemLevel) const
{
	const int32 ClampedItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(ItemLevel);
	const int32 TotalWeight = GetTotalWeight(ItemLevel);
	if (TotalWeight <= 0 || Tiers.Num() == 0)
	{
		return nullptr;
	}

	const int32 Pick = RNG.RandRange(1, TotalWeight);
	int64 Accumulator = 0;
	const int32 TierCount = FMath::Min(Tiers.Num(), MaxAffixTiersToInspect);
	for (int32 TierIndex = 0; TierIndex < TierCount; ++TierIndex)
	{
		const FAffixTier& Tier = Tiers[TierIndex];
		const int32 ClampedTierMinLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Tier.MinItemLevel);
		if (ClampedItemLevel < ClampedTierMinLevel)
		{
			continue;
		}

		Accumulator = FMath::Min<int64>(
			Accumulator + FMath::Clamp<int64>(Tier.Weight, 0, MaxAffixWeight),
			MaxAffixWeight);
		if (Pick <= Accumulator)
		{
			return &Tier;
		}
	}

	return nullptr;
}

void UItemAffixDefinition::BuildFinalModifiers(const FAffixTier& Tier, FRandomStream& RNG, TArray<FItemStatModifier>& OutMods) const
{
	if (!FMath::IsFinite(Tier.MinRoll) || !FMath::IsFinite(Tier.MaxRoll))
	{
		return;
	}
	const float MinRoll = FMath::Clamp(FMath::Min(Tier.MinRoll, Tier.MaxRoll), -MaxAffixMagnitude, MaxAffixMagnitude);
	const float MaxRoll = FMath::Clamp(FMath::Max(Tier.MinRoll, Tier.MaxRoll), -MaxAffixMagnitude, MaxAffixMagnitude);
	const int32 Available = FMath::Max(0, MaxAffixContributions - OutMods.Num());
	const int32 ContributionCount = FMath::Min(AttributeContributions.Num(), Available);
	for (int32 ContributionIndex = 0; ContributionIndex < ContributionCount; ++ContributionIndex)
	{
		const FAttributeRoll& AttributeRoll = AttributeContributions[ContributionIndex];
		if (!IsUsableAffixAttribute(AttributeRoll.Attribute)
			|| !IsValidAffixOperation(AttributeRoll.Op)
			|| !FMath::IsFinite(AttributeRoll.Scale))
		{
			continue;
		}

		const float UnitRoll = RNG.FRandRange(MinRoll, MaxRoll);

		FItemStatModifier NewModifier;
		NewModifier.Attribute = AttributeRoll.Attribute;
		NewModifier.Op = AttributeRoll.Op;
		const double Magnitude = static_cast<double>(AttributeRoll.Scale) * static_cast<double>(UnitRoll);
		NewModifier.Magnitude = FMath::IsFinite(Magnitude)
			? static_cast<float>(FMath::Clamp(Magnitude, -static_cast<double>(MaxAffixMagnitude), static_cast<double>(MaxAffixMagnitude)))
			: 0.f;

		OutMods.Add(MoveTemp(NewModifier));
	}
}

bool UItemAffixDefinition::IsAllowedFor(EItemCategory ItemCategory, EEquipmentSlot Slot) const
{
	const UEnum* CategoryEnum = StaticEnum<EItemCategory>();
	const UEnum* SlotEnum = StaticEnum<EEquipmentSlot>();
	if (!CategoryEnum || !CategoryEnum->IsValidEnumValue(static_cast<int64>(ItemCategory))
		|| !SlotEnum || !SlotEnum->IsValidEnumValue(static_cast<int64>(Slot)))
	{
		return false;
	}

	const int32 CategoryCount = FMath::Min(AllowedCategories.Num(), MaxAffixContributions);
	bool bCategoryAllowed = AllowedCategories.IsEmpty();
	for (int32 Index = 0; Index < CategoryCount && !bCategoryAllowed; ++Index)
	{
		bCategoryAllowed = AllowedCategories[Index] == ItemCategory;
	}
	if (!bCategoryAllowed)
	{
		return false;
	}

	const int32 SlotCount = FMath::Min(SlotFilter.AllowedSlots.Num(), MaxAffixContributions);
	bool bSlotAllowed = SlotFilter.AllowedSlots.IsEmpty();
	for (int32 Index = 0; Index < SlotCount && !bSlotAllowed; ++Index)
	{
		bSlotAllowed = SlotFilter.AllowedSlots[Index] == Slot;
	}
	if (!bSlotAllowed)
	{
		return false;
	}

	return true;
}
