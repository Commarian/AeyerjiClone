// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AeyerjiSpawnRegion.generated.h"

class UBoxComponent;

/**
 * Defines a weighted world region used when seeding fixed population clusters.
 */
UCLASS(BlueprintType)
class AEYERJI_API AAeyerjiSpawnRegion : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiSpawnRegion();

	/** Returns the region bounds in world space. */
	UFUNCTION(BlueprintPure, Category="SpawnRegion")
	FBox GetRegionBounds() const;

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
};
