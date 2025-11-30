// ItemTooltipData.h
#pragma once

#include "CoreMinimal.h"
#include "Items/ItemTypes.h"

#include "ItemTooltipData.generated.h"

class UAeyerjiItemInstance;
class UTexture2D;
class UItemDefinition;
class UWidget;

/** Source widget requesting the tooltip (inventory tile vs equipment slot). */
UENUM(BlueprintType)
enum class EItemTooltipSource : uint8
{
	InventoryTile,
	EquipmentSlot
};

/**
 * Unified payload describing an item for tooltip/pop-up displays.
 * This is intentionally data-only so designers can style the UI freely in UMG.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiItemTooltipData
{
	GENERATED_BODY()

public:
	/** Item instance being displayed (safe to use for further queries). */
	UPROPERTY(BlueprintReadOnly, Category = "Item")
	TObjectPtr<UAeyerjiItemInstance> Item = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	TObjectPtr<UTexture2D> Icon = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FText DisplayName = FText::GetEmpty();

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FName DefinitionId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	EItemRarity Rarity = EItemRarity::Common;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	EItemCategory ItemCategory = EItemCategory::Offense;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	EEquipmentSlot DefaultSlot = EEquipmentSlot::Offense;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	EEquipmentSlot EquippedSlot = EEquipmentSlot::Offense;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 EquippedSlotIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	int32 ItemLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FGuid UniqueId;

	UPROPERTY(BlueprintReadOnly, Category = "Item")
	FIntPoint InventorySize = FIntPoint(1, 1);

	/** Base stats coming from the definition (pre-affix). */
	UPROPERTY(BlueprintReadOnly, Category = "Item|Stats")
	TArray<FItemStatModifier> BaseModifiers;

	/** Rolled affixes with their modifiers/effects. */
	UPROPERTY(BlueprintReadOnly, Category = "Item|Stats")
	TArray<FRolledAffix> RolledAffixes;

	/** Final aggregated modifiers (definition + affixes). */
	UPROPERTY(BlueprintReadOnly, Category = "Item|Stats")
	TArray<FItemStatModifier> FinalModifiers;

	UPROPERTY(BlueprintReadOnly, Category = "Item|Effects")
	TArray<FItemGrantedEffect> GrantedEffects;

	UPROPERTY(BlueprintReadOnly, Category = "Item|Effects")
	TArray<FItemGrantedAbility> GrantedAbilities;

	/** Where this tooltip request originated (bag tile vs equipment slot). */
	UPROPERTY(BlueprintReadOnly, Category = "Item|UI")
	EItemTooltipSource Source = EItemTooltipSource::InventoryTile;

	static FAeyerjiItemTooltipData FromItem(UAeyerjiItemInstance* Item, EItemTooltipSource Source = EItemTooltipSource::InventoryTile);
};
