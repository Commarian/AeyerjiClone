// LootService.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Items/LootTypes.h"
#include "Items/ItemTypes.h"
#include "Player/PlayerLootStats.h"
#include "GameplayTagContainer.h"
#include "Engine/DataTable.h"

#include "LootService.generated.h"

class UAeyerjiLootTable;
class UPlayerStatsTrackingComponent;
class UItemDefinition;
struct FLootTablePool;

/**
 * Context passed into the loot roll pipeline.
 * Extend with enemy info, world tier, etc. as needed.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FLootContext
{
	GENERATED_BODY()

	/** Actor that should receive loot credit (used to look up stats). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	TWeakObjectPtr<AActor> PlayerActor = nullptr;

	/** Enemy level or comparable scaling input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	int32 EnemyLevel = 1;

	/** Player level used to bias item level rolls; falls back to EnemyLevel when unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	int32 PlayerLevel = 0;

	/** Difficulty / world tier index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	int32 WorldTier = 0;

	/** Tag describing the source (mob, elite, boss, chest, event, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	FGameplayTag SourceTag;

	/** Optional: force a specific item definition for this roll (bypasses pool lookup). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	TObjectPtr<UItemDefinition> ForcedItemDefinition = nullptr;

	/** Optional: force a specific item id for this roll (used when ForcedItemDefinition not set). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	FName ForcedItemId = NAME_None;

	/** Optional: base legendary chance for this source (0-1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	float BaseLegendaryChance = 0.0f;

	/** Optional: minimum rarity gate for this roll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	EItemRarity MinimumRarity = EItemRarity::Common;

	/** Optional: non-legendary rarity weights used when no legendary is rolled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	TMap<EItemRarity, float> RarityWeights;

	/** Optional difficulty scalar used by rarity weight tables (typically 1..100). When left at 1.0, the loot system may derive a scalar from the current run difficulty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	float DifficultyScale = 1.f;

	/** Inclusive item-level jitter applied around PlayerLevel; triangular bias toward PlayerLevel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	int32 ItemLevelJitterMin = -2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot")
	int32 ItemLevelJitterMax = 2;
};

/** Bucket definition used by multi-drop rolls (e.g. force 1 legendary, 3 commons, etc.). */
USTRUCT(BlueprintType)
struct AEYERJI_API FLootMultiDropBucket
{
	GENERATED_BODY()

	/** Optional label for clarity in logs/UI. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop")
	FName Tag = NAME_None;

	/** Baseline number of rolls for this bucket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop", meta = (ClampMin = "0"))
	int32 BaseDrops = 0;

	/** +/- variance applied to BaseDrops (clamped >= 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop", meta = (ClampMin = "0"))
	int32 Variance = 0;

	/** Minimum rarity gate for rolls in this bucket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop")
	EItemRarity MinimumRarity = EItemRarity::Common;

	/** Prevent duplicates within this bucket (uses ItemId/Definition->ItemId as the key). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop")
	bool bUniqueWithinBucket = false;

	/** Prevent duplicates across all buckets when true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop")
	bool bUniqueAcrossBuckets = true;
};

/** Configuration for rolling multiple drops in one go. */
USTRUCT(BlueprintType)
struct AEYERJI_API FLootMultiDropConfig
{
	GENERATED_BODY()

	/** Baseline total rolls across all buckets; set to 0 to allow buckets to define totals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop", meta = (ClampMin = "0"))
	int32 TotalBaseDrops = 0;

	/** +/- variance applied to TotalBaseDrops (clamped >= 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop", meta = (ClampMin = "0"))
	int32 TotalVariance = 0;

	/** If true, enforce uniqueness across all drops (uses ItemId/Definition->ItemId). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop")
	bool bRequireTotalUnique = false;

	/** Optional shuffle of bucket order before rolling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop")
	bool bShuffleBuckets = true;

	/** How many times to retry a roll when uniqueness rejects a duplicate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop", meta = (ClampMin = "0"))
	int32 UniquenessRetryCount = 8;

	/** If true, emit on-screen debug when config is invalid or uniqueness is exhausted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop")
	bool bLogDebugToScreen = true;

	/** Duration for on-screen debug (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop", meta = (ClampMin = "0.0"))
	float DebugScreenDuration = 10.f;

	/** Buckets to roll; processed in order unless shuffled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop")
	TArray<FLootMultiDropBucket> Buckets;
};

/** DataTable row for per-actor loot contexts + multi-drop configs (row name keyed however you prefer, e.g. BP class name). */
USTRUCT(BlueprintType)
struct AEYERJI_API FLootActorDropRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop")
	FLootContext LootContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Loot|MultiDrop")
	FLootMultiDropConfig MultiDropConfig;
};

/**
 * Centralized loot roller and pity logic.
 */
UCLASS(Config=Game, DefaultConfig)
class AEYERJI_API ULootService : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Rolls loot for the provided context, applies pity, and records stats. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Loot")
	FLootDropResult RollLoot(const FLootContext& Context);

	/** Rolls multiple drops according to a multi-drop config (no spawning). Returns false on invalid config. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Loot")
	bool RollMultiDrop(const FLootContext& BaseContext, const FLootMultiDropConfig& Config, TArray<FLootDropResult>& OutResults);

	/** Computes the final legendary probability given base chance and player stats. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Loot")
	float ComputeLegendaryChance(const FLootContext& Context, const FPlayerLootStats& Stats) const;

	/** Exposes the loaded loot table for systems that need shared formatting/scaling. */
	UAeyerjiLootTable* GetLootTable() const;

protected:
	// UGameInstanceSubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

private:
	/** Designer-authored loot table asset to drive rarity/item selection. */
	UPROPERTY(EditDefaultsOnly, Config, Category = "Aeyerji|Loot")
	TSoftObjectPtr<UAeyerjiLootTable> LootTableAsset;

	/** Soft pity kicks in after this many non-legendary drops. */
	UPROPERTY(EditDefaultsOnly, Category = "Aeyerji|Loot|Pity")
	int32 SoftPityStart = 20;

	/** Chance added per drop past SoftPityStart. */
	UPROPERTY(EditDefaultsOnly, Category = "Aeyerji|Loot|Pity")
	float SoftPitySlope = 0.005f;

	/** Hard pity guarantees a legendary on or after this many misses. */
	UPROPERTY(EditDefaultsOnly, Category = "Aeyerji|Loot|Pity")
	int32 HardPityDrops = 70;

	/** Absolute cap on legendary probability. */
	UPROPERTY(EditDefaultsOnly, Category = "Aeyerji|Loot|Pity")
	float MaxLegendaryChance = 0.25f;

	/** Bonus applied when the rolling window contains no legendaries. */
	UPROPERTY(EditDefaultsOnly, Category = "Aeyerji|Loot|Pity")
	float StarvedWindowBonus = 0.02f;

	/** Minimum window size before the starved bonus can apply. */
	UPROPERTY(EditDefaultsOnly, Category = "Aeyerji|Loot|Pity")
	int32 StarvedWindowMinCount = 20;

	mutable TWeakObjectPtr<UAeyerjiLootTable> CachedLootTable;

	const FLootTablePool* FindMatchingPool(const FLootContext& Context, const UAeyerjiLootTable& Table) const;
	UPlayerStatsTrackingComponent* ResolvePlayerStats(const FLootContext& Context) const;
	EItemRarity ChooseRarity(const FLootContext& Context, float LegendaryChance, EItemRarity MinimumRarity) const;
};
