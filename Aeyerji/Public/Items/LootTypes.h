// LootTypes.h
#pragma once

#include "CoreMinimal.h"
#include "Items/ItemTypes.h"

#include "LootTypes.generated.h"

class UItemDefinition;

/** Outcome data for a loot roll, used by loot services and stats components. */
USTRUCT(BlueprintType)
struct AEYERJI_API FLootDropResult
{
	GENERATED_BODY()

	/** Rarity chosen by the loot roll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	EItemRarity Rarity = EItemRarity::Common;

	/** Stable identifier for the dropped item definition, if available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	FName ItemId = NAME_None;

	/** Optional reference to the dropped item definition (not persisted). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	TObjectPtr<UItemDefinition> ItemDefinition = nullptr;

	/** Desired item level for the rolled item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	int32 ItemLevel = 1;

	/** Optional seed to make item rolls deterministic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	int32 Seed = 0;
};
