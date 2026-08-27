// ItemDefinition.cpp

#include "Items/ItemDefinition.h"
#include "Materials/MaterialInterface.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "UObject/SoftObjectPath.h"

namespace
{
	constexpr int32 MaxItemDefinitionAffixRanges = 256;
	constexpr int32 MaxItemDefinitionAffixes = 64;
	constexpr int32 MaxItemSynergyEntries = 64;

	bool IsFiniteItemDefinitionColor(const FLinearColor& Color)
	{
		return FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G)
			&& FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A);
	}
}

UItemDefinition::UItemDefinition()
{
	RarityAffixRanges = {
		FItemRarityAffixRange(EItemRarity::Common, 0, 0),
		FItemRarityAffixRange(EItemRarity::Uncommon, 1, 2),
		FItemRarityAffixRange(EItemRarity::Rare, 2, 3),
		FItemRarityAffixRange(EItemRarity::Epic, 3, 4),
		FItemRarityAffixRange(EItemRarity::Pure, 4, 5),
		FItemRarityAffixRange(EItemRarity::Legendary, 5, 6),
		FItemRarityAffixRange(EItemRarity::PerfectLegendary, 6, 7),
		FItemRarityAffixRange(EItemRarity::Celestial, 7, 8)
	};

	bEnableEquipSynergy = false;
	EquipSynergyColorParameter = NAME_None;
}

void UItemDefinition::PostLoad()
{
	Super::PostLoad();

	auto ClampEnumValue = [](auto Value, auto DefaultValue)
	{
		using EnumType = decltype(Value);
		const UEnum* Enum = StaticEnum<EnumType>();
		return (Enum && Enum->IsValidEnumValue(static_cast<int64>(Value))) ? Value : DefaultValue;
	};

	// Keep assets authored before the lane rename inside the current lane enum range.
	ItemCategory = ClampEnumValue(ItemCategory, EItemCategory::Assault);
	DefaultSlot = ClampEnumValue(DefaultSlot, EEquipmentSlot::Assault);
	RequiredLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(RequiredLevel);

	// Corruption items must default to the Corruption lane; normal items cannot default there.
	if (ItemCategory == EItemCategory::Corruption)
	{
		DefaultSlot = EEquipmentSlot::Corruption;
		RequiredLevel = UAeyerjiDifficultySettings::GetMaxGameplayLevel();
	}
	else if (DefaultSlot == EEquipmentSlot::Corruption)
	{
		DefaultSlot = static_cast<EEquipmentSlot>(ItemCategory);
	}
}

FName UItemDefinition::GetDefinitionKey() const
{
	return MakeDefinitionKey(this);
}

FName UItemDefinition::MakeDefinitionKey(const UItemDefinition* Definition)
{
	if (!Definition)
	{
		return NAME_None;
	}

	return MakeDefinitionKeyFromSoftPath(FSoftObjectPath(Definition));
}

FName UItemDefinition::MakeDefinitionKeyFromSoftPath(const FSoftObjectPath& DefinitionPath)
{
	if (!DefinitionPath.IsValid())
	{
		return NAME_None;
	}

	return FName(*DefinitionPath.ToString());
}

UMaterialInterface* UItemDefinition::ResolvePreviewMaterial(EItemRarity Rarity)
{
	// Hard-coded lookup: preview glow materials per rarity. New assets can be added to this list.
	static const TMap<EItemRarity, TSoftObjectPtr<UMaterialInterface>> RarityToMaterial = {
		{ EItemRarity::Common,            TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/Mi_LootDropSphereCommon.Mi_LootDropSphereCommon"))) },
		{ EItemRarity::Uncommon,          TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/Mi_LootDropSphereUncommon.Mi_LootDropSphereUncommon"))) },
		{ EItemRarity::Rare,              TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/Mi_LootDropSphereRare.Mi_LootDropSphereRare"))) },
		{ EItemRarity::Epic,              TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/Mi_LootDropSphereEpic.Mi_LootDropSphereEpic"))) },
		{ EItemRarity::Legendary,         TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/Mi_LootDropSphereLegendary.Mi_LootDropSphereLegendary"))) },
		{ EItemRarity::Pure,              TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/Mi_LootDropSpherePure.Mi_LootDropSpherePure"))) },
		{ EItemRarity::PerfectLegendary,  TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/Mi_LootDropSpherePerfectLegendary.Mi_LootDropSpherePerfectLegendary"))) },
		{ EItemRarity::Celestial,         TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/Mi_LootDropSphereCelestial.Mi_LootDropSphereCelestial"))) }
	};

	if (const TSoftObjectPtr<UMaterialInterface>* Found = RarityToMaterial.Find(Rarity))
	{
		return Found->LoadSynchronous();
	}

	return nullptr;
}

void UItemDefinition::GetAffixCountRange(EItemRarity Rarity, int32& OutMin, int32& OutMax) const
{
	OutMin = 0;
	OutMax = 0;
	const UEnum* RarityEnum = StaticEnum<EItemRarity>();
	if (!RarityEnum || !RarityEnum->IsValidEnumValue(static_cast<int64>(Rarity)))
	{
		Rarity = EItemRarity::Common;
	}

	const FItemRarityAffixRange* Found = nullptr;
	const int32 RangeCount = FMath::Min(RarityAffixRanges.Num(), MaxItemDefinitionAffixRanges);
	for (int32 RangeIndex = 0; RangeIndex < RangeCount; ++RangeIndex)
	{
		if (RarityAffixRanges[RangeIndex].Rarity == Rarity)
		{
			Found = &RarityAffixRanges[RangeIndex];
			break;
		}
	}

	if (!Found)
	{
		for (int32 RangeIndex = 0; RangeIndex < RangeCount; ++RangeIndex)
		{
			if (RarityAffixRanges[RangeIndex].Rarity == EItemRarity::Common)
			{
				Found = &RarityAffixRanges[RangeIndex];
				break;
			}
		}
	}

	if (Found)
	{
		OutMin = FMath::Clamp(Found->MinAffixes, 0, MaxItemDefinitionAffixes);
		OutMax = FMath::Clamp(Found->MaxAffixes, 0, MaxItemDefinitionAffixes);
		if (OutMin > OutMax)
		{
			Swap(OutMin, OutMax);
		}
	}
}

bool UItemDefinition::TryGetEquipSynergyColor(
	int32 StackCount,
	FLinearColor& OutColor,
	FName& OutColorParam) const
{
	OutColor = FLinearColor::White;
	OutColorParam = PickupVisuals.ColorParameter;

	if (StackCount <= 1 || !bEnableEquipSynergy)
	{
		return false;
	}

	const FItemEquipSynergyColor* BestEntry = nullptr;
	int32 BestStack = 0;

	const int32 EntryCount = FMath::Min(EquipSynergyColors.Num(), MaxItemSynergyEntries);
	for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
	{
		const FItemEquipSynergyColor& Entry = EquipSynergyColors[EntryIndex];
		if (Entry.StackCount <= 0 || !IsFiniteItemDefinitionColor(Entry.Color))
		{
			continue;
		}

		if (Entry.StackCount == StackCount)
		{
			BestEntry = &Entry;
			break;
		}

		if (Entry.StackCount < StackCount && Entry.StackCount > BestStack)
		{
			BestStack = Entry.StackCount;
			BestEntry = &Entry;
		}
	}

	if (BestEntry)
	{
		OutColor = BestEntry->Color;
		if (!EquipSynergyColorParameter.IsNone())
		{
			OutColorParam = EquipSynergyColorParameter;
		}
		return true;
	}

	switch (StackCount)
	{
	case 2:
		OutColor = FLinearColor::Red;
		break;
	case 3:
		OutColor = FLinearColor(1.f, 0.84f, 0.f, 1.f);
		break;
	case 4:
		OutColor = FLinearColor(0.f, 1.f, 1.f, 1.f);
		break;
	case 5:
		OutColor = FLinearColor(0.8f, 0.2f, 1.f, 1.f);
		break;
	default:
		return false;
	}

	if (!EquipSynergyColorParameter.IsNone())
	{
		OutColorParam = EquipSynergyColorParameter;
	}

	return true;
}

bool UItemDefinition::IsCorruptionItem() const
{
	return ItemCategory == EItemCategory::Corruption || DefaultSlot == EEquipmentSlot::Corruption;
}

int32 UItemDefinition::GetEffectiveRequiredLevel() const
{
	const int32 BaseLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(RequiredLevel);
	return IsCorruptionItem() ? UAeyerjiDifficultySettings::GetMaxGameplayLevel() : BaseLevel;
}
