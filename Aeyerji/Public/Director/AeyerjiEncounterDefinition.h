// AeyerjiEncounterDefinition.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AeyerjiEncounterDefinition.generated.h"

// Forward-declare to avoid heavy includes in header.
// We'll convert these data rows into the runtime structs declared in AeyerjiSpawnerGroup.
class APawn;
class UGameplayAbility;

/**
 * Author-time enemy set (uses SoftClass so the asset doesn't hard-load enemies in the editor).
 * This maps 1:1 to your runtime FEnemySet.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FEnemySetDef
{
	GENERATED_BODY()

	/** Enemy pawn/character class (soft ref). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(AllowedClasses="Pawn"))
	TSoftClassPtr<APawn> EnemyClass;

	/** Exact count to emit for this set in the wave. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(ClampMin="0"))
	int32 Count = 0;

	/** Time between spawns within this set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(ClampMin="0.0"))
	float SpawnInterval = 0.2f;

	/** Optional flag used for presentation/VFX (outline tint, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn")
	bool bIsElite = false;

	/** Escalates the elite tuning/FX to mini-boss levels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(EditCondition="bIsElite", EditConditionHides))
	bool bIsMiniBoss = false;

	/** Optional signature abilities granted only to this mini boss set. Falls back to spawner defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(EditCondition="bIsMiniBoss", EditConditionHides, AdvancedDisplay))
	TArray<TSubclassOf<UGameplayAbility>> MiniBossGrantedAbilities;

	/** Force these affixes for this set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	TArray<FGameplayTag> ForcedEliteAffixes;

	/** Limits random rolls to these affixes; empty uses the spawner pool. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	TArray<FGameplayTag> EliteAffixPoolOverride;

	/** Minimum random affixes to roll in addition to ForcedEliteAffixes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(ClampMin="0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	int32 MinEliteAffixes = 0;

	/** Maximum random affixes to roll in addition to ForcedEliteAffixes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(ClampMin="0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	int32 MaxEliteAffixes = 0;

	/** Optional per-set stat overrides; leave 0 to use spawner defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	float EliteHealthMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	float EliteDamageMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	float EliteRangeMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	float EliteScaleMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	float EliteXpMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	float MiniBossXpMultiplierOverride = 0.f;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FWaveDefData
{
	GENERATED_BODY()

	/** Optional label to keep the wave list readable in the details panel. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wave")
	FText WaveLabel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wave", meta=(TitleProperty="EnemyClass"))
	TArray<FEnemySetDef> EnemySets;

	/** Delay after the wave finishes emitting before the next begins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Wave", meta=(ClampMin="0.0"))
	float PostSpawnDelay = 0.5f;
};

/**
 * Primary Data Asset used to author a reusable encounter ("Cemetery_Elites_01", "BossRoom_A", etc).
 * Designers create one asset per encounter. At runtime, this resolves soft classes and produces your
 * existing FWaveDefinition / FEnemySet arrays for AAeyerjiSpawnerGroup to consume.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiEncounterDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Optional display name for tools/UI. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter")
	FText DisplayName;

	/** Optional high-level tags for filtering/queries (Biome, Difficulty, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter")
	FGameplayTagContainer Tags;

	/** The authored waves for this encounter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter", meta=(TitleProperty="WaveLabel"))
	TArray<FWaveDefData> Waves;

public:
	/**
	 * Resolves soft classes and writes to OutWaves using the runtime structs defined in the spawner.
	 * Returns true if at least one valid enemy set resolves.
	 *
	 * NOTE: This function performs synchronous loads; call it during encounter activation on the server.
	 */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Encounters")
	bool BuildRuntimeWaves(class TArray<struct FWaveDefinition>& OutWaves) const;

	/** Asset Manager integration (optional but handy for scanning/IDs). */
	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("EncounterDef"), GetFName());
	}
};
