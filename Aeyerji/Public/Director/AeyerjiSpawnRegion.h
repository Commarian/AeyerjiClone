// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AeyerjiSpawnRegion.generated.h"

class UBoxComponent;
class UEnemySpawnGroupDefinition;

/**
 * Defines a weighted world region used when seeding fixed population clusters.
 */
UCLASS(BlueprintType)
class AEYERJI_API AAeyerjiSpawnRegion : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiSpawnRegion();

	/** Shared Actor Tag that excludes a region from Greater Rift discovery without requiring unique tags. */
	static const FName RiftExcludedActorTag;

	/** Returns the region bounds in world space. */
	UFUNCTION(BlueprintPure, Category="SpawnRegion")
	FBox GetRegionBounds() const;

	/** Untagged regions are eligible by default; add Rift.Excluded to Actor Tags for boss or unsafe areas. */
	UFUNCTION(BlueprintPure, Category="SpawnRegion|Rift")
	bool IsRiftEncounterEligible() const;

public:
	/** Collision volume used to describe the region footprint. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SpawnRegion")
	TObjectPtr<UBoxComponent> RegionBounds = nullptr;

	/** Relative weight when selecting this region as a cluster seed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SpawnRegion", meta=(ClampMin="0.0"))
	float RegionWeight = 1.0f;

	/** Multiplier applied to cluster density rolls within this region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SpawnRegion", meta=(ClampMin="0.0"))
	float DensityScale = 1.0f;

	/** Bonus applied to elite chance for clusters seeded in this region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SpawnRegion", meta=(ClampMin="0.0"))
	float EliteChanceBonus = 0.0f;

	/** Multiplier applied to cluster radius when seeding this region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SpawnRegion", meta=(ClampMin="0.0"))
	float ClusterRadiusScale = 1.0f;

	/** When false, elites are suppressed for clusters seeded in this region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SpawnRegion")
	bool bAllowElites = true;

	/** Authored encounter-group composition emitted once when this anchor is activated by a Rift. Empty uses the director fallback pool. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SpawnRegion|Rift")
	TObjectPtr<UEnemySpawnGroupDefinition> RiftEncounterGroup = nullptr;

	/**
	 * Monotonic route position used by Rift pacing to distinguish forward progress from backtracking.
	 * Assign every eligible Rift region a value of zero or greater in the level. Regions on parallel
	 * branches may share an index; actor path is used as the deterministic tiebreaker.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="SpawnRegion|Rift", meta=(ClampMin="-1"))
	int32 RiftProgressionIndex = INDEX_NONE;
};
