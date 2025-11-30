// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "Engine/DataAsset.h"
#include "AeyerjiEncounterDirector.generated.h"

class AEnemyParentNative;

/**
 * Designer-authored definition describing a pool of enemies the encounter director can spawn.
 */
UCLASS(BlueprintType)
class AEYERJI_API UEnemySpawnGroupDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Optional display label for editor/debugging. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter")
	FText DisplayName;

	/** Generic tags so the director can filter groups by biome, pacing, etc. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter")
	FGameplayTagContainer EncounterTags;

	/** Enemy archetypes this group can emit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter")
	TArray<TSubclassOf<AEnemyParentNative>> EnemyTypes;

	/** Minimum enemies to emit whenever this group is chosen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter", meta=(ClampMin="1"))
	int32 MinCount = 3;

	/** Maximum enemies to emit whenever this group is chosen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter", meta=(ClampMin="1"))
	int32 MaxCount = 5;

	/** Radius around the player where this group's pawns will be distributed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter", meta=(ClampMin="100.0"))
	float SpawnRadius = 1000.f;

	/** When false, this group will not be selected twice in a row. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter")
	bool bAllowBackToBackSelection = true;

	/** Returns a resolved spawn count in the configured min/max range. */
	int32 ResolveSpawnCount() const { return (MinCount == MaxCount) ? MinCount : FMath::RandRange(MinCount, MaxCount); }

	/** Returns a random enemy class from the configured pool. */
	TSubclassOf<AEnemyParentNative> ResolveEnemyClass() const;
};

UENUM(BlueprintType)
enum class EEncounterDirectorState : uint8
{
	Idle,
	InCombat,
	PostCombat
};

/**
 * Reactive encounter director that monitors player pace and injects new enemy packs on demand.
 */
UCLASS(Blueprintable)
class AEYERJI_API AAeyerjiEncounterDirector : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiEncounterDirector();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	void RefreshPlayerReference();
	void CleanupInactiveEnemies();
	void UpdateKillWindow();
	bool ShouldTriggerEncounter() const;
	void TriggerEncounter();
	const UEnemySpawnGroupDefinition* ChooseSpawnGroup() const;
	void SpawnFromGroup(const UEnemySpawnGroupDefinition* Group);
	FVector ResolveSpawnLocation(float Radius, float HalfHeight) const;
	void EnterState(EEncounterDirectorState NewState);
	void RegisterSpawnedEnemy(AEnemyParentNative* Enemy);
	void SnapActorToGround(AActor* SpawnedActor, float HalfHeight) const;
	float GetEnemyHalfHeight(TSubclassOf<AEnemyParentNative> EnemyClass) const;

	UFUNCTION()
	void HandleTrackedEnemyDied(AActor* DeadEnemy);

	UFUNCTION()
	void HandleTrackedEnemyDestroyed(AActor* DestroyedActor);

protected:
	/** Author-time spawn groups this director can cycle through. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Config")
	TArray<TObjectPtr<UEnemySpawnGroupDefinition>> SpawnGroups;

	/** Minimum forward distance (cm) the player must travel between encounters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0"))
	float MinDistanceBetweenEncounters = 1200.f;

	/** Distance at which encounters are forced even if kill velocity is low. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0"))
	float ForceSpawnDistance = 2200.f;

	/** Minimum downtime after the last kill before another encounter can trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0"))
	float MinDowntimeSeconds = 1.5f;

	/** Time window used when computing kill velocity (kills per second). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.1"))
	float KillVelocityWindowSeconds = 6.f;

	/** Threshold of kills per second required to proactively spawn another pack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0"))
	float KillVelocityThreshold = 0.5f;

	/** Maximum downtime (seconds) before the director forces another spawn regardless of velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0"))
	float ForceSpawnDowntimeSeconds = 5.f;

	/** Delay after the last enemy in a pack dies before we return to idle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0"))
	float PostCombatDelaySeconds = 1.0f;

	/** Upward offset for ground traces when adjusting spawn Z height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.0"))
	float GroundTraceUpOffset = 120.f;

	/** Downward trace distance to find the floor under a spawn point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="10.0"))
	float GroundTraceDownDistance = 2000.f;

	/** Height applied above the detected ground when placing enemies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.0"))
	float SpawnGroundOffset = 5.f;

	/** Toggle editor/debug rendering of spawn decisions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Debug")
	bool bDrawDebug = false;

	/** Current state exposed for Blueprints/UI. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="EncounterDirector|State")
	EEncounterDirectorState DirectorState = EEncounterDirectorState::Idle;

	/** Rolling kill velocity (kills per second). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="EncounterDirector|State")
	float CurrentKillVelocity = 0.f;

	/** Distance from the player to the location of the last triggered encounter. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="EncounterDirector|State")
	float DistanceFromLastEncounter = 0.f;

	/** How many tracked enemies are currently alive. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="EncounterDirector|State")
	int32 ActiveEnemyCount = 0;

private:
	void RecordKillTimestamp();

private:
	TWeakObjectPtr<APawn> CachedPlayerPawn;
	TWeakObjectPtr<AController> CachedPlayerController;
	TWeakObjectPtr<const UEnemySpawnGroupDefinition> LastSpawnedGroup;
	TArray<TWeakObjectPtr<AActor>> LiveEnemies;
	TArray<double> KillTimestampHistory;
	FVector LastEncounterLocation = FVector::ZeroVector;
	double LastEncounterTimestamp = 0.0;
	double LastKillTimestamp = 0.0;
	double PostCombatTimeRemaining = 0.0;
};
