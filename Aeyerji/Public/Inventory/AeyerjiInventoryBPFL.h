// AeyerjiInventoryBPFL.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Items/ItemTypes.h"
#include "Styling/SlateColor.h"

#include "AeyerjiInventoryBPFL.generated.h"

class AAeyerjiLootPickup;
class UAeyerjiInventoryComponent;
class UAeyerjiItemInstance;
class UItemDefinition;

UENUM(BlueprintType)
enum class EAeyerjiAddItemResult : uint8
{
	Equipped,
	Bagged,
	Failed_NoInventory,
	Failed_NoItem,
	Failed_BagFull
};

/**
 * Blueprint helpers for the custom inventory system.
 */
UCLASS()
class AEYERJI_API UAeyerjiInventoryBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Attempt to equip the item (preferred slot) otherwise place it in the bag grid. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Inventory")
	static EAeyerjiAddItemResult EquipFirstThenBag(
		UAeyerjiInventoryComponent* Inventory,
		UAeyerjiItemInstance* ItemInstance);

	/** Return a consistent UI color for a given item rarity. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Inventory|Display")
	static FLinearColor GetRarityColor(EItemRarity Rarity);

	/** Same as GetRarityColor but wrapped as a Slate color for UMG brush usage. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Inventory|Display")
	static FSlateColor GetRaritySlateColor(EItemRarity Rarity);

	/** Spawn a loot pickup that rolls an item instance from the given definition (SERVER only). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Inventory", meta = (WorldContext = "WorldContextObject"))
	static AAeyerjiLootPickup* SpawnLootByDefinition(
		UObject* WorldContextObject,
		UItemDefinition* Definition,
		int32 ItemLevel,
		EItemRarity Rarity,
		FVector Location,
		FRotator Rotation,
		int32 SeedOverride = 0);

	/** Spawn a loot pickup from an already rolled item instance (SERVER only). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Inventory", meta = (WorldContext = "WorldContextObject"))
	static AAeyerjiLootPickup* SpawnLootByInstance(
		UObject* WorldContextObject,
		UAeyerjiItemInstance* ItemInstance,
		FVector Location,
		FRotator Rotation);

	/** Toggle visibility for all loot labels in the world (client cosmetic helper). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Loot", meta = (WorldContext = "WorldContext"))
	static void SetAllLootLabelsVisible(UObject* WorldContext, bool bVisible);
};
