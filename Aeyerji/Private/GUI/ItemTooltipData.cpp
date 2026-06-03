// ItemTooltipData.cpp

#include "GUI/ItemTooltipData.h"

#include "Items/ItemDefinition.h"
#include "Items/ItemInstance.h"

namespace
{
	FText GetLaneDisplayText(EEquipmentSlot Slot)
	{
		switch (Slot)
		{
		case EEquipmentSlot::Assault:
			return FText::FromString(TEXT("Assault"));
		case EEquipmentSlot::Guard:
			return FText::FromString(TEXT("Guard"));
		case EEquipmentSlot::Flow:
			return FText::FromString(TEXT("Flow"));
		case EEquipmentSlot::Corruption:
			return FText::FromString(TEXT("Corruption"));
		default:
			return FText::FromString(TEXT("Assault"));
		}
	}
}

FAeyerjiItemTooltipData FAeyerjiItemTooltipData::FromItem(UAeyerjiItemInstance* Item, EItemTooltipSource Source)
{
	FAeyerjiItemTooltipData Data;
	Data.Item = Item;
	Data.Source = Source;

	if (!Item)
	{
		return Data;
	}

	Data.DisplayName = Item->GetDisplayName();
	Data.Rarity = Item->Rarity;
	Data.ItemLevel = Item->ItemLevel;
	Data.UniqueId = Item->UniqueId;
	Data.InventorySize = Item->InventorySize;
	Data.EquippedSlot = Item->EquippedSlot;
	Data.EquippedSlotIndex = Item->EquippedSlotIndex;
	Data.RolledAffixes = Item->RolledAffixes;
	Data.FinalModifiers = Item->FinalAggregatedModifiers;
	Data.GrantedEffects = Item->GrantedEffects;
	Data.GrantedAbilities = Item->GrantedAbilities;

	if (const UItemDefinition* Definition = Item->Definition)
	{
		Data.DefinitionId = Definition->GetDefinitionKey();
		Data.Icon = Definition->Icon;
		Data.Description = Definition->Description;
		Data.ItemCategory = Definition->ItemCategory;
		Data.DefaultSlot = Definition->DefaultSlot;
		Data.RequiredLevel = Definition->GetEffectiveRequiredLevel();
		Data.LaneDisplayText = GetLaneDisplayText(Definition->DefaultSlot);
		Data.bIsCorruptionItem = Definition->IsCorruptionItem();
		Data.CorruptionPowerText = Definition->CorruptionPowerText;
		Data.CorruptionDrawbackText = Definition->CorruptionDrawbackText;
	}
	else
	{
		Data.DefaultSlot = Item->EquippedSlot;
		Data.ItemCategory = EItemCategory::Assault;
		Data.LaneDisplayText = GetLaneDisplayText(Item->EquippedSlot);
	}

	return Data;
}
