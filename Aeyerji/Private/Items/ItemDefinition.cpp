// ItemDefinition.cpp

#include "Items/ItemDefinition.h"
#include "Materials/MaterialInterface.h"

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

	// Ensure assets authored before the enum trim stay within Offense/Defense/Magic.
	ItemCategory = ClampEnumValue(ItemCategory, EItemCategory::Offense);
	DefaultSlot = ClampEnumValue(DefaultSlot, EEquipmentSlot::Offense);

	// Keep slot/category aligned (inventory logic assumes matching ordinals).
	if (static_cast<int32>(DefaultSlot) != static_cast<int32>(ItemCategory))
	{
		DefaultSlot = static_cast<EEquipmentSlot>(ItemCategory);
	}
}

UMaterialInterface* UItemDefinition::ResolvePreviewMaterial(EItemRarity Rarity)
{
	// Hard-coded lookup: preview glow materials per rarity. New assets can be added to this list.
	static const TMap<EItemRarity, TSoftObjectPtr<UMaterialInterface>> RarityToMaterial = {
		{ EItemRarity::Common,            TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/MI_LootDropSphereCommon.MI_LootDropSphereCommon"))) },
		{ EItemRarity::Uncommon,          TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/MI_LootDropSphereUncommon.MI_LootDropSphereUncommon"))) },
		{ EItemRarity::Rare,              TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/MI_LootDropSphereRare.MI_LootDropSphereRare"))) },
		{ EItemRarity::Epic,              TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/MI_LootDropSphereEpic.MI_LootDropSphereEpic"))) },
		{ EItemRarity::Legendary,         TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/MI_LootDropSphereLegendary.MI_LootDropSphereLegendary"))) },
		{ EItemRarity::Pure,              TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/MI_LootDropSpherePure.MI_LootDropSpherePure"))) },
		{ EItemRarity::PerfectLegendary,  TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/MI_LootDropSpherePerfectLegendary.MI_LootDropSpherePerfectLegendary"))) },
		{ EItemRarity::Celestial,         TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Inventory/Items/MaterialGlowRarities/MI_LootDropSphereCelestial.MI_LootDropSphereCelestial"))) }
	};

	if (const TSoftObjectPtr<UMaterialInterface>* Found = RarityToMaterial.Find(Rarity))
	{
		return Found->LoadSynchronous();
	}

	return nullptr;
}

void UItemDefinition::GetAffixCountRange(EItemRarity Rarity, int32& OutMin, int32& OutMax) const
{
	const FItemRarityAffixRange* Found = RarityAffixRanges.FindByPredicate(
		[Rarity](const FItemRarityAffixRange& Entry)
		{
			return Entry.Rarity == Rarity;
		});

	if (!Found)
	{
		Found = RarityAffixRanges.FindByPredicate(
			[](const FItemRarityAffixRange& Entry)
			{
				return Entry.Rarity == EItemRarity::Common;
			});
	}

	if (Found)
	{
		OutMin = Found->MinAffixes;
		OutMax = Found->MaxAffixes;
	}
	else
	{
		OutMin = 0;
		OutMax = 0;
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

	for (const FItemEquipSynergyColor& Entry : EquipSynergyColors)
	{
		if (Entry.StackCount <= 0)
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
