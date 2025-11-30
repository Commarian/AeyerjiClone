// AeyerjiInventoryBPFL.cpp

#include "Inventory/AeyerjiInventoryBPFL.h"

#include "Engine/World.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Items/InventoryComponent.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemGenerator.h"
#include "Items/ItemInstance.h"
#include "EngineUtils.h"
#include "Logging/AeyerjiLog.h"

namespace
{
	TSubclassOf<AAeyerjiLootPickup> ResolveLootPickupClass(UObject* WorldContextObject)
	{
		if (const UAeyerjiInventoryComponent* Inventory = Cast<UAeyerjiInventoryComponent>(WorldContextObject))
		{
			return Inventory->GetLootPickupClass();
		}

		return nullptr;
	}
}

EAeyerjiAddItemResult UAeyerjiInventoryBPFL::EquipFirstThenBag(
	UAeyerjiInventoryComponent* Inventory,
	UAeyerjiItemInstance* ItemInstance)
{
	if (!Inventory)
	{
		return EAeyerjiAddItemResult::Failed_NoInventory;
	}

	if (!ItemInstance)
	{
		return EAeyerjiAddItemResult::Failed_NoItem;
	}

	const bool bAlreadyOwned = Inventory->FindItemById(ItemInstance->UniqueId) != nullptr;

	// Ensure the item lives inside the inventory component.
	if (!Inventory->AddItemInstance(ItemInstance, true))
	{
		return EAeyerjiAddItemResult::Failed_BagFull;
	}

	const EEquipmentSlot PreferredSlot = ItemInstance->Definition
		? ItemInstance->Definition->DefaultSlot
		: ItemInstance->EquippedSlot;

	Inventory->Server_EquipItem(ItemInstance->UniqueId, PreferredSlot, INDEX_NONE);
	if (ItemInstance->EquippedSlotIndex != INDEX_NONE)
	{
		return EAeyerjiAddItemResult::Equipped;
	}

	if (Inventory->AutoPlaceItem(ItemInstance))
	{
		return EAeyerjiAddItemResult::Bagged;
	}

	if (!bAlreadyOwned)
	{
		Inventory->Server_RemoveItemById(ItemInstance->UniqueId);
	}
	return EAeyerjiAddItemResult::Failed_BagFull;
}

AAeyerjiLootPickup* UAeyerjiInventoryBPFL::SpawnLootByDefinition(
	UObject* WorldContextObject,
	UItemDefinition* Definition,
	int32 ItemLevel,
	EItemRarity Rarity,
	FVector Location,
	FRotator Rotation,
	int32 SeedOverride)
{
	if (!WorldContextObject || !Definition)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootByDefinition aborted - WorldContext=%s Definition=%s"),
			*GetNameSafe(WorldContextObject),
			*GetNameSafe(Definition));
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootByDefinition aborted - World=%s NetMode=%d"),
			*GetNameSafe(World),
			World ? static_cast<int32>(World->GetNetMode()) : -1);
		return nullptr;
	}

	const TSubclassOf<AAeyerjiLootPickup> LootPickupClass = ResolveLootPickupClass(WorldContextObject);

	AAeyerjiLootPickup* Spawned = AAeyerjiLootPickup::SpawnFromDefinition(
		*World,
		Definition,
		ItemLevel,
		Rarity,
		FTransform(Rotation, Location),
		SeedOverride,
		LootPickupClass);

	AJ_LOG(WorldContextObject, TEXT("SpawnLootByDefinition %s Class=%s Location=%s Result=%s"),
		*GetNameSafe(WorldContextObject),
		*GetNameSafe(LootPickupClass.Get()),
		*Location.ToString(),
		*GetNameSafe(Spawned));
	return Spawned;
}

AAeyerjiLootPickup* UAeyerjiInventoryBPFL::SpawnLootByInstance(
	UObject* WorldContextObject,
	UAeyerjiItemInstance* ItemInstance,
	FVector Location,
	FRotator Rotation)
{
	if (!WorldContextObject || !ItemInstance)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootByInstance aborted - WorldContext=%s Item=%s"),
			*GetNameSafe(WorldContextObject),
			*GetNameSafe(ItemInstance));
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootByInstance aborted - World=%s NetMode=%d"),
			*GetNameSafe(World),
			World ? static_cast<int32>(World->GetNetMode()) : -1);
		return nullptr;
	}

	const TSubclassOf<AAeyerjiLootPickup> LootPickupClass = ResolveLootPickupClass(WorldContextObject);

	AAeyerjiLootPickup* Spawned = AAeyerjiLootPickup::SpawnFromInstance(
		*World,
		ItemInstance,
		FTransform(Rotation, Location),
		LootPickupClass);

	AJ_LOG(WorldContextObject, TEXT("SpawnLootByInstance %s Class=%s Location=%s Result=%s"),
		*GetNameSafe(WorldContextObject),
		*GetNameSafe(LootPickupClass.Get()),
		*Location.ToString(),
		*GetNameSafe(Spawned));
	return Spawned;
}

void UAeyerjiInventoryBPFL::SetAllLootLabelsVisible(UObject* WorldContext, bool bVisible)
{
	if (!WorldContext)
	{
		return;
	}

	if (UWorld* World = WorldContext->GetWorld())
	{
		for (TActorIterator<AAeyerjiLootPickup> It(World); It; ++It)
		{
			It->SetLabelVisible(bVisible);
		}
	}
}

FLinearColor UAeyerjiInventoryBPFL::GetRarityColor(EItemRarity Rarity)
{
	switch (Rarity)
	{
	case EItemRarity::Uncommon:         return FLinearColor(0.25f, 1.f, 0.25f, 1.f);
	case EItemRarity::Rare:             return FLinearColor(0.25f, 0.6f, 1.f, 1.f);
	case EItemRarity::Epic:             return FLinearColor(0.5f, 0.25f, 1.f, 1.f);
	case EItemRarity::Pure:             return FLinearColor(0.95f, 0.9f, 0.3f, 1.f);
	case EItemRarity::Legendary:        return FLinearColor(1.f, 0.6f, 0.2f, 1.f);
	case EItemRarity::PerfectLegendary: return FLinearColor(1.f, 0.23f, 0.11f, 1.f);
	case EItemRarity::Celestial:        return FLinearColor(0.13f, 0.95f, 1.f, 1.f);
	default:                            return FLinearColor(0.35f, 0.35f, 0.35f, 1.f); // Common/unknown fallback
	}
}

FSlateColor UAeyerjiInventoryBPFL::GetRaritySlateColor(EItemRarity Rarity)
{
	return FSlateColor(GetRarityColor(Rarity));
}
