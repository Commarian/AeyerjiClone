// PlayerLootStats.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Items/ItemTypes.h"

#include "PlayerLootStats.generated.h"

/**
 * Persistent memory for a named pity group such as boss uniques, set pieces, or progression keys.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiLootPityMemory
{
	GENERATED_BODY()

	/** Designer/system key for this pity bucket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Pity")
	FGameplayTag PityGroup;

	/** Failed attempts since the last success in this group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Pity")
	int32 AttemptsSinceLastSuccess = 0;

	/** Total attempts recorded for this group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Pity")
	int32 TotalAttempts = 0;

	/** Total successful drops recorded for this group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Pity")
	int32 TotalSuccesses = 0;

	/** Most recent successful item definition key for this group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Pity")
	FName LastDroppedItemDefinitionKey = NAME_None;

	/** Most recent source tag that recorded this group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Pity")
	FGameplayTag LastSourceTag;
};

/**
 * Lifetime loot tracking data used for pity logic and analytics.
 * Pure data holder that can be serialized directly into save objects.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FPlayerLootStats
{
	GENERATED_BODY()

	FPlayerLootStats()
	{
		TotalItemsDroppedByRarity.Init(0, RarityCount);
		TotalItemsPickedUpByRarity.Init(0, RarityCount);
		Last100Drops.Init(0, RollingWindowSize);
	}

	/** Number of entries in the rarity enum. */
	static constexpr int32 RarityCount = static_cast<int32>(EItemRarity::Celestial) + 1;

	/** Rolling window size used for pity tracking (in drop events). */
	static constexpr int32 RollingWindowSize = 100;

	/** Total number of items ever dropped, indexed by rarity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Stats")
	TArray<int32> TotalItemsDroppedByRarity;

	/** Total number of items ever picked up, indexed by rarity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Stats")
	TArray<int32> TotalItemsPickedUpByRarity;

	/** Total number of legendary (or better) drops witnessed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Stats")
	int32 TotalLegendariesDropped = 0;

	/** Total number of legendary (or better) pickups made. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Stats")
	int32 TotalLegendariesPickedUp = 0;

	/** How many drops have occurred since the last legendary drop event. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Stats")
	int32 DropsSinceLastLegendary = 0;

	/** Circular buffer of the last 100 drop outcomes (0 = non-legendary, 1 = legendary). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Rolling")
	TArray<uint8> Last100Drops;

	/** Write index into Last100Drops (wraps at 100). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Rolling")
	int32 WindowIndex = 0;

	/** Number of valid entries currently stored in the rolling buffer (<= RollingWindowSize). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Rolling")
	int32 WindowCount = 0;

	/** Current count of legendary drops within the rolling buffer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Rolling")
	int32 LegendariesInWindow = 0;

	/** Optional per-item pickup counts keyed by asset-derived item definition key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Stats")
	TMap<FName, int32> ItemsPickedUpById;

	/** Persistent named pity memories keyed by designer-facing gameplay tags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|Pity")
	TArray<FAeyerjiLootPityMemory> PityMemories;

	/** Clears all counters back to zero. */
	void Reset()
	{
		TotalItemsDroppedByRarity.Init(0, RarityCount);
		TotalItemsPickedUpByRarity.Init(0, RarityCount);

		TotalLegendariesDropped = 0;
		TotalLegendariesPickedUp = 0;
		DropsSinceLastLegendary = 0;

		Last100Drops.Init(0, RollingWindowSize);
		WindowIndex = 0;
		WindowCount = 0;
		LegendariesInWindow = 0;

		ItemsPickedUpById.Reset();
		PityMemories.Reset();
	}

	/** Finds a named pity memory record, or null when the group has not been recorded yet. */
	const FAeyerjiLootPityMemory* FindPityMemory(const FGameplayTag& PityGroup) const
	{
		if (!PityGroup.IsValid())
		{
			return nullptr;
		}

		for (const FAeyerjiLootPityMemory& Memory : PityMemories)
		{
			if (Memory.PityGroup == PityGroup)
			{
				return &Memory;
			}
		}

		return nullptr;
	}

	/** Finds or creates a named pity memory record. */
	FAeyerjiLootPityMemory& FindOrAddPityMemory(const FGameplayTag& PityGroup)
	{
		for (FAeyerjiLootPityMemory& Memory : PityMemories)
		{
			if (Memory.PityGroup == PityGroup)
			{
				return Memory;
			}
		}

		FAeyerjiLootPityMemory& NewMemory = PityMemories.AddDefaulted_GetRef();
		NewMemory.PityGroup = PityGroup;
		return NewMemory;
	}

	/** Records one attempt in a named pity bucket. Success resets the miss counter. */
	void RecordPityAttempt(const FGameplayTag& PityGroup, bool bSuccess, FName ItemDefinitionKey = NAME_None, const FGameplayTag& SourceTag = FGameplayTag())
	{
		if (!PityGroup.IsValid())
		{
			return;
		}

		FAeyerjiLootPityMemory& Memory = FindOrAddPityMemory(PityGroup);
		++Memory.TotalAttempts;
		Memory.LastSourceTag = SourceTag;

		if (bSuccess)
		{
			Memory.AttemptsSinceLastSuccess = 0;
			++Memory.TotalSuccesses;
			Memory.LastDroppedItemDefinitionKey = ItemDefinitionKey;
		}
		else
		{
			++Memory.AttemptsSinceLastSuccess;
		}
	}

	/** Adds a new drop result into the rolling window and maintains counters. */
	void AppendRollingEntry(bool bLegendaryDrop)
	{
		// Subtract the value that will be overwritten when the buffer is full.
		if (WindowCount == RollingWindowSize)
		{
			LegendariesInWindow -= Last100Drops.IsValidIndex(WindowIndex) ? Last100Drops[WindowIndex] : 0;
		}
		else
		{
			++WindowCount;
		}

		if (!Last100Drops.IsValidIndex(WindowIndex))
		{
			Last100Drops.SetNumZeroed(RollingWindowSize);
		}

		Last100Drops[WindowIndex] = bLegendaryDrop ? 1 : 0;
		LegendariesInWindow += bLegendaryDrop ? 1 : 0;

		WindowIndex = (WindowIndex + 1) % RollingWindowSize;
	}

	/** Helper to convert a rarity enum value into a valid array index. */
	static int32 RarityToIndex(EItemRarity Rarity)
	{
		const int32 Index = static_cast<int32>(Rarity);
		return (Index >= 0 && Index < RarityCount) ? Index : INDEX_NONE;
	}

	/** True when the rarity qualifies as legendary or above. */
	static bool IsLegendaryRarity(EItemRarity Rarity)
	{
		return static_cast<int32>(Rarity) >= static_cast<int32>(EItemRarity::Legendary);
	}
};
