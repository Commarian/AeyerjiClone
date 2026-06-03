// LootTypes.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
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

	/** Asset-derived key for the dropped item definition, if available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	FName ItemDefinitionKey = NAME_None;

	/** Optional reference to the dropped item definition (not persisted). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	TObjectPtr<UItemDefinition> ItemDefinition = nullptr;

	/** Desired item level for the rolled item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	int32 ItemLevel = 1;

	/** Optional seed to make item rolls deterministic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	int32 Seed = 0;

	/** Source tag that produced the roll. Copied from FLootContext for memory/debugging. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	FGameplayTag SourceTag;

	/** Optional named pity bucket this result should record against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Pity")
	FGameplayTag PityGroup;

	/** Minimum rarity that counts as a success for PityGroup memory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Pity")
	EItemRarity PitySuccessRarity = EItemRarity::Legendary;

	/** True when this roll satisfied the named pity group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Pity")
	bool bCountsAsPitySuccess = false;
};
