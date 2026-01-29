// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AeyerjiWorldSpawnProfile.generated.h"

class UCurveFloat;
class UEnemySpawnGroupDefinition;

USTRUCT(BlueprintType)
struct AEYERJI_API FWeightedSpawnGroup
{
	GENERATED_BODY()

	/** Spawn group asset used when generating fixed world populations. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation")
	TObjectPtr<UEnemySpawnGroupDefinition> Group = nullptr;

	/** Relative weight when randomly choosing a spawn group. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation", meta=(ClampMin="0.0"))
	float Weight = 1.0f;
};

/**
 * Data asset describing how to pre-populate a map with a fixed enemy population at run start.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiWorldSpawnProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Minimum total enemies to spawn at difficulty 0 (slider = 0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Budget", meta=(ClampMin="0"))
	int32 MinimumEnemyCount = 150;

	/** Target total enemies to spawn at difficulty 0 (slider = 0). Used to derive the target ratio within the min/max range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Budget", meta=(ClampMin="0"))
	int32 TargetEnemyCount = 1000;

	/** Maximum total enemies to spawn at difficulty 0 (slider = 0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Budget", meta=(ClampMin="0"))
	int32 MaximumEnemyCount = 1500;

	/** Minimum total enemies to spawn at difficulty 1000 (slider max). 0 uses MinimumEnemyCount. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Budget", meta=(ClampMin="0", EditCondition="bScaleBudgetByDifficulty"))
	int32 MinimumEnemyCountAtMaxDifficulty = 0;

	/** Maximum total enemies to spawn at difficulty 1000 (slider max). 0 uses MaximumEnemyCount. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Budget", meta=(ClampMin="0", EditCondition="bScaleBudgetByDifficulty"))
	int32 MaximumEnemyCountAtMaxDifficulty = 0;

	/** When true, scale the total enemy budget using the LevelDirector difficulty curve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Budget")
	bool bScaleBudgetByDifficulty = true;

	/** Minimum interpolation alpha when scaling by difficulty (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Budget", meta=(ClampMin="0.0", ClampMax="1.0", EditCondition="bScaleBudgetByDifficulty"))
	float DifficultyBudgetFloor = 0.0f;

	/** Minimum number of clusters to distribute across the map. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters", meta=(ClampMin="1"))
	int32 MinClusterCount = 12;

	/** Maximum number of clusters to distribute across the map. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters", meta=(ClampMin="1"))
	int32 MaxClusterCount = 80;

	/** Minimum enemy count per cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters", meta=(ClampMin="1"))
	int32 MinClusterSize = 6;

	/** Maximum enemy count per cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters", meta=(ClampMin="1"))
	int32 MaxClusterSize = 24;

	/** Minimum radius used when scattering spawns within a cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters", meta=(ClampMin="0.0", Units="cm"))
	float ClusterRadiusMin = 500.f;

	/** Maximum radius used when scattering spawns within a cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters", meta=(ClampMin="0.0", Units="cm"))
	float ClusterRadiusMax = 1400.f;

	/** Min spacing (cm) enforced between cluster centers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters", meta=(ClampMin="0.0", Units="cm"))
	float MinClusterSpacing = 1500.f;

	/** Random exponent used when no density curve is provided. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters", meta=(ClampMin="0.01"))
	float DensityExponent = 1.5f;

	/** Optional curve to shape density rolls (X=0..1 random, Y=0..1 density). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters")
	TObjectPtr<UCurveFloat> DensityCurve = nullptr;

	/** Percentile (0..1) of densest clusters that receive elite bias. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DenseClusterPercentile = 0.2f;

	/** When true, use AAeyerjiSpawnRegion actors to seed cluster centers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters")
	bool bUseSpawnRegions = true;

	/** Attempt count when searching for valid cluster centers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters", meta=(ClampMin="1"))
	int32 ClusterCenterSearchAttempts = 10;

	/** Navigation projection extent used when snapping cluster centers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters", meta=(ClampMin="0.0", Units="cm"))
	float NavProjectionExtent = 1200.f;

	/** Fallback radius used when no spawn regions are present. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Clusters", meta=(ClampMin="0.0", Units="cm"))
	float FallbackSpawnRadius = 6000.f;

	/** Weighted spawn group pool used for fixed world spawns. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Spawning", meta=(TitleProperty="Group"))
	TArray<FWeightedSpawnGroup> SpawnGroups;

	/** When true, apply aggro settings immediately on spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Spawning")
	bool bApplyAggroOnSpawn = false;

	/** Override for MaxSpawnsPerTick while fixed population is spawning (0 = use EncounterDirector default). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Spawning", meta=(ClampMin="0"))
	int32 MaxFixedSpawnsPerTickOverride = 0;

	/** Seed used for deterministic fixed population layouts (0 = random per run). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Spawning")
	int32 Seed = 0;

	/** Base elite chance applied to all fixed spawns. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Elites", meta=(ClampMin="0.0", ClampMax="1.0"))
	float BaseEliteChance = 0.12f;

	/** Additional elite chance applied to dense clusters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Elites", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DenseEliteChanceBonus = 0.25f;

	/** Additional elite chance scaled by density (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Elites", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DensityEliteChanceScale = 0.2f;

	/** Maximum elite chance allowed after bonuses. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Elites", meta=(ClampMin="0.0", ClampMax="1.0"))
	float EliteChanceCap = 0.6f;

	/** Affix rolls for standard elites. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Elites", meta=(ClampMin="0"))
	int32 BaseEliteMinAffixes = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Elites", meta=(ClampMin="0"))
	int32 BaseEliteMaxAffixes = 2;

	/** Affix rolls for elites spawned in dense clusters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Elites", meta=(ClampMin="0"))
	int32 DenseEliteMinAffixes = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Elites", meta=(ClampMin="0"))
	int32 DenseEliteMaxAffixes = 4;

	/** Cluster clears required per shard award in fixed population mode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="FixedPopulation|Progression", meta=(ClampMin="1"))
	int32 ClustersPerShard = 1;

	/** Asset Manager integration identifier. */
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("WorldSpawnProfile"), GetFName());
	}
};
