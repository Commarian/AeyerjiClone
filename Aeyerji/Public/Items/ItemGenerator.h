// ItemGenerator.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Items/ItemTypes.h"

#include "ItemGenerator.generated.h"

class UItemDefinition;
class UAeyerjiItemInstance;
class UItemAffixDefinition;
struct FAffixTier;

/**
 * Stateless item generator that rolls affixes and builds runtime item instances.
 */
UCLASS()
class AEYERJI_API UItemGenerator : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Items", meta = (WorldContext = "WorldContext"))
	static UAeyerjiItemInstance* RollItemInstance(
		UObject* WorldContext,
		UItemDefinition* Definition,
		int32 ItemLevel,
		EItemRarity Rarity,
		int32 SeedOverride,
		EEquipmentSlot SlotOverride);

	static void ChooseAffixes(
		UItemDefinition* Definition,
		int32 ItemLevel,
		EEquipmentSlot Slot,
		int32 AffixCount,
		FRandomStream& RNG,
		TArray<UItemAffixDefinition*>& OutAffixes,
		TArray<const FAffixTier*>& OutTiers);
};

