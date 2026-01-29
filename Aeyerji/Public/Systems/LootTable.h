// LootTable.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Items/ItemTypes.h"

class UItemDefinition;
class UAeyerjiLootEntrySet;

#include "LootTable.generated.h"

/** Optional name format per rarity. */
USTRUCT(BlueprintType)
struct AEYERJI_API FItemRarityNameFormat
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	EItemRarity Rarity = EItemRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	FText Prefix;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	FText Suffix;
};

/** Per-level scaling entry for a gameplay attribute. */
USTRUCT(BlueprintType)
struct AEYERJI_API FItemStatScaling
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	FGameplayAttribute Attribute;

	/** Multiplied by (1 + PerLevelMultiplier * (Level-1)). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	float PerLevelMultiplier = 0.f;

	/** Added as (Magnitude += PerLevelAdd * (Level-1)). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	float PerLevelAdd = 0.f;
};

/** DataTable row for per-attribute scaling (editable in CSV/JSON). */
USTRUCT(BlueprintType, meta = (DisplayName = "ItemStatScalingRow"))
struct AEYERJI_API FItemStatScalingRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Attribute name (e.g., AeyerjiAttributeSet.AttackDamage). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	FName AttributeName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	float PerLevelMultiplier = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	float PerLevelAdd = 0.f;
};

/** Rarity weighting row for level/difficulty-aware drop chances. */
USTRUCT(BlueprintType, meta = (DisplayName = "RarityWeightRow"))
struct AEYERJI_API FRarityWeightRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	EItemRarity Rarity = EItemRarity::Common;

	/** Inclusive character/enemy level gate; 0 means unbounded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	int32 MinLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	int32 MaxLevel = 0;

	/** Base weight contribution at MinLevel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	float BaseWeight = 0.f;

	/** Weight added per level above MinLevel (clamped at MaxLevel when set). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	float WeightPerLevel = 0.f;

	/** Multiplies weight by DifficultyScale (from context), e.g., 1.0-100. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	float DifficultyMultiplier = 1.f;
};

/** Rarity-level scaling row for data tables (editable in CSV/Excel). */
USTRUCT(BlueprintType, meta = (DisplayName = "RarityScalingRow"))
struct AEYERJI_API FRarityScalingRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	EItemRarity Rarity = EItemRarity::Common;

	/** Extra affixes added on top of the item definition range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	int32 BonusAffixes = 0;

	/** Multiplier applied to base modifiers from the item definition. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	float BaseModifierMultiplier = 1.f;

	/** Multiplier applied to rolled affix modifiers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	float AffixModifierMultiplier = 1.f;

	/** Multiplier applied to granted gameplay effect levels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	float GrantedEffectLevelMultiplier = 1.f;

	/** Optional heading/value text for UI display. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	FText DisplayHeading;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	FText DisplayValue;
};

/** Weight entry for a specific item definition or id, scoped to a rarity bucket. */
USTRUCT(BlueprintType)
struct AEYERJI_API FLootTableEntry
{
	GENERATED_BODY()

	/** Optional direct reference; if unset, ItemId will be used to resolve (safe for cooked bundles). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	TSoftObjectPtr<UItemDefinition> ItemDefinition;

	/** Stable id for spawning and saves; also used when ItemDefinition is null. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	FName ItemId = NAME_None;

	/**
	 * Optional per-entry drop chance (0-1) evaluated before weight selection.
	 * If no entries in the pool pass their DropChance, the entire roll is suppressed (no loot),
	 * even if weights exist. Set to 1.0 to ensure the entry is eligible every roll.
	 * ULootService::RollLoot picks one item per call; call it multiple times if you want multiple drops.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropChance = 1.0f;

	/** Rarity bucket this entry contributes to; must match the rolled rarity. If you roll a rarity with no entries > 0 weight, loot will be empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	EItemRarity Rarity = EItemRarity::Common;

	/** Weighted chance within the rarity bucket; set to 0 to disable. Must be > 0 for the entry to be considered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/** Optional level gates (inclusive). 0 means unbounded on that side. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot", meta = (ClampMin = "0"))
	int32 MinLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot", meta = (ClampMin = "0"))
	int32 MaxLevel = 0;
};

/** Pool scoped by source tag/tier/level with rarity weights and item entries. */
USTRUCT(BlueprintType)
struct AEYERJI_API FLootTablePool
{
	GENERATED_BODY()

	/** Optional tag describing the source (enemy, chest, event). Empty = wildcard. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	FGameplayTag SourceTag;

	/** Optional world tier bounds (inclusive). 0 means unbounded on that side. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot", meta = (ClampMin = "0"))
	int32 MinWorldTier = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot", meta = (ClampMin = "0"))
	int32 MaxWorldTier = 0;

	/** Optional level bounds (inclusive). 0 means unbounded on that side. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot", meta = (ClampMin = "0"))
	int32 MinLevel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot", meta = (ClampMin = "0"))
	int32 MaxLevel = 0;

	/** Weighted item choices; per-rarity item weights live here, not in RarityWeights. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	TArray<FLootTableEntry> Entries;

	/** Optional external entry sets that expand this pool (lets designers reuse bundles like "all commons"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	TArray<TSoftObjectPtr<UAeyerjiLootEntrySet>> EntrySets;
};

/** Designer-authored loot table; reference in LootService to drive rarity and item rolls. */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiLootTable : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UAeyerjiLootTable();

	/** Ordered pools; the first matching pool is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	TArray<FLootTablePool> Pools;

	/** Info-only note: this asset is auto-loaded by LootService (DefaultEngine.ini LootTableAsset). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	FString AutoloadNote;

	/** Optional per-rarity name formats for prefix/suffix decoration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	TArray<FItemRarityNameFormat> NameFormats;

	/** Optional per-attribute scaling applied using the item's level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	TSoftObjectPtr<UDataTable> StatScalingTable;

	/** Optional rarity scaling table (CSV/DataTable) referenced by rarity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	TSoftObjectPtr<UDataTable> RarityScalingTable;

	/** Optional rarity weight table (CSV/DataTable) for global drop weights. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	TSoftObjectPtr<UDataTable> RarityWeightsTable;

	const FItemRarityNameFormat* FindNameFormat(EItemRarity Rarity) const;
	const FItemStatScalingRow* FindScalingForAttribute(const FGameplayAttribute& Attribute) const;
	const FRarityScalingRow* FindRarityScaling(EItemRarity Rarity) const;
	const FRarityWeightRow* FindRarityWeightRow(const FName& RowName) const;
	void BuildRarityWeights(int32 CharacterLevel, float DifficultyScale, TMap<EItemRarity, float>& OutWeights) const;
};

/**
 * Reusable bundle of loot entries; reference from pools to avoid duplicating common sets.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiLootEntrySet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Entries contained in this reusable set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot")
	TArray<FLootTableEntry> Entries;
};
