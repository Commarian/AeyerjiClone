// EnemyScalingTable.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"

#include "EnemyScalingTable.generated.h"

/**
 * Per-attribute scaling entry for enemies.
 * Designers author these in a single shared JSON/CSV DataTable.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FEnemyAttributeScalingEntry
{
	GENERATED_BODY()

	/** Attribute name (e.g., AeyerjiAttributeSet.AttackDamage). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling")
	FName AttributeName;

	/** Multiplier applied per level delta: Value * (1 + PerLevelMultiplier * (Level-1)). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling")
	float PerLevelMultiplier = 0.f;

	/** Flat addition per level delta: + PerLevelAdd * (Level-1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling")
	float PerLevelAdd = 0.f;

	/** Legacy difficulty multiplier range kept for backward-compatible data only; runtime now uses the shared world-tier curve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling")
	float DifficultyMinMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling")
	float DifficultyMaxMultiplier = 1.f;

	/** Optional clamps; 0 means unbounded on that side. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling")
	float MinValue = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling")
	float MaxValue = 0.f;
};

/**
 * Row describing how an enemy archetype scales with level and difficulty.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FEnemyScalingRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Archetype tag used to look up this row (e.g., Enemy.Role.Boss). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling")
	FGameplayTag ArchetypeTag;

	/** Optional loot/source tag to stamp on the enemy for downstream systems. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling")
	FGameplayTag SourceTag;

	/** Legacy base-level field kept for backward-compatible data only; runtime now uses the shared level curve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling", meta = (ClampMin = "1.0"))
	float BaseLevel = 1.f;

	/** Legacy level-advantage field kept for backward-compatible data only; runtime now uses the shared level curve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling")
	float MaxLevelAdvantage = 0.f;

	/** Legacy exponent field kept for backward-compatible data only; runtime now uses the shared world-tier curve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling", meta = (ClampMin = "0.1"))
	float DifficultyExponent = 1.25f;

	/** Per-attribute scaling definitions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|EnemyScaling")
	TArray<FEnemyAttributeScalingEntry> Attributes;
};
