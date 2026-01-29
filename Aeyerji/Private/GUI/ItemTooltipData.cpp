// ItemTooltipData.cpp

#include "GUI/ItemTooltipData.h"

#include "Items/ItemDefinition.h"
#include "Items/ItemInstance.h"

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
		Data.DefinitionId = Definition->ItemId;
		Data.Icon = Definition->Icon;
		Data.Description = Definition->Description;
		Data.ItemCategory = Definition->ItemCategory;
		Data.DefaultSlot = Definition->DefaultSlot;
		Data.BaseModifiers = Definition->BaseModifiers;
	}
	else
	{
		Data.DefaultSlot = Item->EquippedSlot;
		Data.ItemCategory = EItemCategory::Offense;
	}

	return Data;
}
