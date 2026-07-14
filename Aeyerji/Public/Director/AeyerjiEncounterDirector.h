// Copyright (c) 2025 Aeyerji.
#pragma once

#include "AeyerjiObjectiveTypes.h"
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "Engine/DataAsset.h"
#include "AeyerjiEncounterDirector.generated.h"

class AEnemyParentNative;
class AAeyerjiEncounterDirector;
class AAeyerjiGameState;
class AAeyerjiLevelDirector;
class AAeyerjiSpawnerGroup;
class AAeyerjiSpawnRegion;
class UAeyerjiWorldSpawnProfile;

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

	/** Optional elite-only archetypes. When empty, elite requests fall back to non-elite classes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter")
	TArray<TSubclassOf<AEnemyParentNative>> EliteEnemyTypes;

	/** Minimum enemies to emit whenever this group is chosen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter", meta=(ClampMin="1"))
	int32 MinCount = 3;

	/** Maximum enemies to emit whenever this group is chosen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter", meta=(ClampMin="1"))
	int32 MaxCount = 5;

	/** Radius around the player where this group's pawns will be distributed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter", meta=(ClampMin="100.0", Units="cm"))
	float SpawnRadius = 1000.f;

	/** When false, this group will not be selected twice in a row. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter")
	bool bAllowBackToBackSelection = true;

	/** Chance that this group emits one of its EliteEnemyTypes when planned for a Greater Rift. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Rift", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RiftEliteChance = 0.f;

	/** Weighted Greater Rift progress awarded by an ordinary enemy from this group. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Rift", meta=(ClampMin="1"))
	int32 RiftProgressPoints = 1;

	/** Weighted Greater Rift progress awarded by an elite-pool enemy from this group. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Encounter|Rift", meta=(ClampMin="1"))
	int32 RiftEliteProgressPoints = 5;

	/** Returns a resolved spawn count in the configured min/max range. */
	int32 ResolveSpawnCount() const { return (MinCount == MaxCount) ? MinCount : FMath::RandRange(MinCount, MaxCount); }

	/** Returns a random enemy class from the configured pool. */
	TSubclassOf<AEnemyParentNative> ResolveEnemyClass() const;

	/** Returns a random elite class from EliteEnemyTypes (or nullptr when no elite pool is configured). */
	TSubclassOf<AEnemyParentNative> ResolveEliteEnemyClass() const;
};

/**
 * Designer-owned encounter director setup. The placed EncounterDirector consumes this and owns pacing decisions.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiEncounterDirectorDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Setup")
	TArray<TObjectPtr<UEnemySpawnGroupDefinition>> SpawnGroups;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="s"))
	float TickIntervalSeconds = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", Units="cm"))
	float MinDistanceBetweenEncounters = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0"))
	float KillVelocitySpawnFloor = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.01"))
	float KillVelocitySpawnCeil = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", Units="cm"))
	float MinDistanceAtSlow = 1800.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", Units="cm"))
	float MinDistanceAtFast = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", Units="s"))
	float MinDowntimeAtSlow = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", Units="s"))
	float MinDowntimeAtFast = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.1", Units="s"))
	float KillVelocityWindowSeconds = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="1"))
	int32 MaxGroupsPerTrigger = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", Units="s"))
	float PostCombatDelaySeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="1"))
	int32 MaxSpawnsPerTick = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.0", Units="cm"))
	float MinSpawnDistanceFromPlayer = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning")
	bool bAvoidRecentPlayerPath = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.0", Units="cm"))
	float RecentPathAvoidRadius = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.1", Units="s"))
	float RecentPathSeconds = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.1", Units="s"))
	float RecentPathSampleInterval = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="1"))
	int32 RecentPathMaxSamples = 32;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning")
	bool bAvoidPlayerForwardSpawnCone = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.0", ClampMax="180.0", Units="deg"))
	float ForwardSpawnConeDegrees = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning")
	bool bUseLineOfSightForForwardCone = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="1"))
	int32 SpawnLocationSearchAttempts = 12;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.0", Units="cm"))
	float GroundTraceUpOffset = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="10.0", Units="cm"))
	float GroundTraceDownDistance = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.0", Units="cm"))
	float SpawnGroundOffset = 5.f;

	/** Minimum distance every ordinary Rift spawn must keep from each living player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Rift", meta=(ClampMin="0.0", Units="cm"))
	float RiftMinimumSpawnDistanceFromPlayers = 1200.f;

	/** Periodic authority-only interval used to activate a safe unopened anchor when encounter pressure is too low. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Rift", meta=(ClampMin="0.1", Units="s"))
	float RiftPressureEvaluationInterval = 2.f;

	/** Minimum live ordinary enemies desired before the pressure evaluator seeks another unopened anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Rift", meta=(ClampMin="0"))
	int32 RiftMinimumActiveEnemyPressure = 8;

	/** Prefers hidden valid locations first, but can use a visible location when no hidden option exists. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Rift")
	bool bRiftPreferHiddenSpawnLocations = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance")
	bool bEnableEnemyLODThrottling = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.05", Units="s"))
	float EnemyLODUpdateInterval = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="cm"))
	float EnemyLODNearDistance = 4000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="cm"))
	float EnemyLODMidDistance = 8000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="cm"))
	float EnemyLODFarDistance = 12000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="s"))
	float EnemyLODMidTickInterval = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="s"))
	float EnemyLODFarTickInterval = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance")
	bool bEnableFixedClusterSleeping = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="cm"))
	float FixedClusterSleepDistance = 14000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="cm"))
	float FixedClusterWakeDistance = 11000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Debug")
	bool bDrawDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Debug", meta=(ClampMin="0.1", Units="s"))
	float DebugLogIntervalSeconds = 1.0f;
};

UENUM(BlueprintType)
enum class EEncounterDirectorState : uint8
{
	Idle,
	InCombat,
	PostCombat
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FFixedClusterClearedSignature, int32, ClusterId, float, DensityAlpha, bool, bDenseCluster);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFixedPopulationClearedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFixedPopulationInitialSpawnCompleteSignature, AAeyerjiEncounterDirector*, Director);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEncounterProgressChangedSignature, float, Progress01, int32, Killed, int32, Total);

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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Registers an externally spawned enemy (boss, mini-boss, scripted spawn) so pacing and cleanup logic stay in sync. */
	UFUNCTION(BlueprintCallable, Category="EncounterDirector")
	void RegisterExternalEnemy(AEnemyParentNative* Enemy, bool bEnterCombatState = true);

	/** Starts a fixed-world population spawn using the provided profile (server-only). */
	UFUNCTION(BlueprintCallable, Category="EncounterDirector|FixedPopulation")
	bool StartFixedWorldPopulation(UAeyerjiWorldSpawnProfile* Profile, AAeyerjiSpawnerGroup* SpawnManager = nullptr, AAeyerjiLevelDirector* LevelDirector = nullptr);

	/** Stops the fixed-world population flow and clears queued spawns (server-only). */
	UFUNCTION(BlueprintCallable, Category="EncounterDirector|FixedPopulation")
	void StopFixedWorldPopulation();

	/** Returns true when fixed-world population mode is active. */
	UFUNCTION(BlueprintPure, Category="EncounterDirector|FixedPopulation")
	bool IsFixedWorldPopulationActive() const { return bFixedPopulationActive; }

	/** Returns the target enemy count for the active fixed population. */
	UFUNCTION(BlueprintPure, Category="EncounterDirector|FixedPopulation")
	int32 GetFixedPopulationTarget() const { return FixedPopulationTarget; }

	/** Returns true once the initial fixed-population spawn queue has been fully processed. */
	UFUNCTION(BlueprintPure, Category="EncounterDirector|FixedPopulation")
	bool IsFixedWorldPopulationInitialSpawnComplete() const { return bFixedPopulationInitialSpawnComplete; }

	/** Returns how many initial fixed-population enemies still need to be spawned. */
	UFUNCTION(BlueprintPure, Category="EncounterDirector|FixedPopulation")
	int32 GetFixedPopulationRemainingToSpawn() const { return FixedPopulationRemaining; }

	/** Returns how many enemies have been counted toward the current objective. */
	UFUNCTION(BlueprintCallable, Category="EncounterDirector|Progress")
	int32 GetKilledCount() const { return KilledCount; }

	/** Returns the raw objective kill target for gameplay code that needs to distinguish "unset" from "1". */
	int32 GetTotalToKillRaw() const { return TotalToKill; }

	/** Returns the objective kill target for the current encounter flow. Blueprint-facing version never returns 0 to avoid divide-by-zero UI paths. */
	UFUNCTION(BlueprintCallable, Category="EncounterDirector|Progress")
	int32 GetTotalToKill() const;

	/** Returns normalized progress (0..1) based on KilledCount / TotalToKill. */
	UFUNCTION(BlueprintCallable, Category="EncounterDirector|Progress")
	float GetProgress01() const;

	/** Builds a coherent objective snapshot for replication into GameState. */
	FAeyerjiObjectiveState BuildObjectiveStateSnapshot(const AAeyerjiLevelDirector* LevelDirector = nullptr, const AAeyerjiGameState* GameState = nullptr) const;

	/** Pushes the latest coherent objective snapshot into the authoritative GameState. */
	void PushObjectiveStateToGameState();

	/** Updates the boss-spawned flag for UI objective switching (server authoritative). */
	UFUNCTION(BlueprintCallable, Category="EncounterDirector|Progress")
	void SetBossSpawned(bool bInBossSpawned);

	/** Returns true once the active boss phase has spawned a boss actor. */
	UFUNCTION(BlueprintPure, Category="EncounterDirector|Progress")
	bool IsBossSpawned() const { return bBossSpawned; }

	/** Registers an enemy for progress tracking without affecting encounter pacing. */
	UFUNCTION(BlueprintCallable, Category="EncounterDirector|Progress")
	void RegisterProgressEnemy(AEnemyParentNative* Enemy, int32 ProgressPoints = 1, int32 RunSerial = 0);

	/** Resets the server-only registration ledger and begins weighted progress for a run serial. */
	void BeginWeightedProgressRun(int32 RunSerial, int32 ProgressTargetPoints);

	/**
	 * Freezes a deterministic one-shot plan over automatically discovered AAeyerjiSpawnRegion actors.
	 * Untagged regions participate by default; regions carrying the Actor Tag Rift.Excluded are ignored.
	 */
	bool BeginRiftRegionRun(int32 RunSerial, int32 RunSeed, int32 ProgressTargetPoints,
		int32 EnemyBudget, float ActivationDistance, float DensityMultiplier, float EliteRateMultiplier,
		float EncounterSizeMultiplier, float ProgressMultiplier, AAeyerjiSpawnerGroup* SpawnManager,
		AAeyerjiLevelDirector* LevelDirector, FString& OutReason);

	/** Stops consuming unused regions while allowing already accepted region queues to finish spawning. */
	void StopRiftRegionActivation();

	/** True while the authority can still consume unused regions for the active Rift serial. */
	bool IsRiftRegionActivationEnabled() const { return bRiftRegionActivationEnabled; }

	/** Freezes weighted progress and rejects all later registration/progress events. */
	void FreezeWeightedProgress();

	/** True while a Rift weighted-progress ledger accepts enemy registrations. */
	bool IsWeightedProgressActive() const { return WeightedProgressRunSerial > 0 && !bWeightedProgressFrozen; }

	int32 GetEnemiesDefeated() const { return EnemiesDefeated; }
	int32 GetWeightedProgressPoints() const { return WeightedProgressPoints; }
	int32 GetWeightedProgressTarget() const { return WeightedProgressTarget; }

#if WITH_DEV_AUTOMATION_TESTS
	/** Test-only ledger inspection and direct native callback entry points. */
	int32 GetRegisteredProgressPointsForAutomation(const AActor* Enemy) const;
	void NotifyProgressEnemyDiedForAutomation(AActor* Enemy);
	void NotifyProgressEnemyDestroyedForAutomation(AActor* Enemy);
#endif

	/** Applies designer-owned pacing/spawn setup from DirectorDefinition. */
	UFUNCTION(BlueprintCallable, Category="EncounterDirector|Definition")
	void ApplyDirectorDefinition();

	/** Compact debug string for console tools and temporary widgets. */
	UFUNCTION(BlueprintPure, Category="EncounterDirector|Debug")
	FString GetEncounterDirectorDebugString() const;

public:
	/** Designer-facing pacing and spawn-group setup. */
	UPROPERTY(BlueprintReadOnly, Category="EncounterDirector|Resolved")
	TObjectPtr<UAeyerjiEncounterDirectorDefinition> DirectorDefinition = nullptr;

	/** Applies DirectorDefinition during BeginPlay. */
	UPROPERTY(BlueprintReadOnly, Category="EncounterDirector|Resolved")
	bool bApplyDirectorDefinitionOnBeginPlay = true;

	/** Fired when a fixed population cluster is cleared. */
	UPROPERTY(BlueprintAssignable, Category="EncounterDirector|FixedPopulation")
	FFixedClusterClearedSignature OnFixedClusterCleared;

	/** Fired when every fixed population cluster is cleared. */
	UPROPERTY(BlueprintAssignable, Category="EncounterDirector|FixedPopulation")
	FFixedPopulationClearedSignature OnFixedPopulationCleared;

	/** Fired once the initial fixed-population spawn queue has finished spawning. */
	UPROPERTY(BlueprintAssignable, Category="EncounterDirector|FixedPopulation")
	FFixedPopulationInitialSpawnCompleteSignature OnFixedPopulationInitialSpawnComplete;

	/** Fired when progress or boss state changes so UI can refresh. */
	UPROPERTY(BlueprintAssignable, Category="EncounterDirector|Progress")
	FEncounterProgressChangedSignature OnProgressChanged;

protected:
	/** How often the director ticks (0 = every frame). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="s"))
	float TickIntervalSeconds = 0.2f;

	void RefreshPlayerReference();
	void UpdateRecentPlayerPath();
	void CleanupInactiveEnemies();
	void UpdateKillWindow();
	bool ShouldTriggerEncounter();
	void TriggerEncounter();
	const UEnemySpawnGroupDefinition* ChooseSpawnGroup() const;
	void SpawnFromGroup(const UEnemySpawnGroupDefinition* Group);
	int32 QueueSpawnsFromGroup(const UEnemySpawnGroupDefinition* Group);
	void ProcessSpawnQueue();
	bool SpawnSingleFromGroup(const UEnemySpawnGroupDefinition* Group);
	FVector ResolveSpawnLocation(float Radius, float HalfHeight) const;
	bool IsSpawnCandidateAllowed(const FVector& Candidate, float MinDistance) const;
	bool IsNearRecentPlayerPath(const FVector& Candidate) const;
	bool IsSpawnLocationVisible(const FVector& Candidate) const;
	void EnterState(EEncounterDirectorState NewState);
	void RegisterSpawnedEnemy(AEnemyParentNative* Enemy);
	void SnapActorToGround(AActor* SpawnedActor, float HalfHeight) const;
	float GetEnemyHalfHeight(TSubclassOf<AEnemyParentNative> EnemyClass) const;
	float GetKillSpeedAlpha() const;
	bool RemoveProgressEnemy(AActor* Enemy);

	UFUNCTION()
	void HandleTrackedEnemyDied(AActor* DeadEnemy);

	UFUNCTION()
	void HandleTrackedEnemyDestroyed(AActor* DestroyedActor);

protected:
	/** Author-time spawn groups this director can cycle through. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Setup")
	TArray<TObjectPtr<UEnemySpawnGroupDefinition>> SpawnGroups;

	/** Minimum forward distance (cm) the player must travel between encounters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", Units="cm"))
	float MinDistanceBetweenEncounters = 1200.f;

	/** Minimum kill velocity required before the director will inject new packs; below this, no spawns are added. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", AdvancedDisplay))
	float KillVelocitySpawnFloor = 0.25f;

	/** Kill velocity at which spawn pacing is at its fastest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.01", AdvancedDisplay))
	float KillVelocitySpawnCeil = 1.5f;

	/** Distance gate when player kill speed is slow (alpha=0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", Units="cm", AdvancedDisplay))
	float MinDistanceAtSlow = 1800.f;

	/** Distance gate when player kill speed is fast (alpha=1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", Units="cm", AdvancedDisplay))
	float MinDistanceAtFast = 900.f;

	/** Downtime gate after the last kill when kill speed is slow (alpha=0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", Units="s", AdvancedDisplay))
	float MinDowntimeAtSlow = 2.0f;

	/** Downtime gate after the last kill when kill speed is fast (alpha=1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", Units="s", AdvancedDisplay))
	float MinDowntimeAtFast = 0.5f;

	/** Time window used when computing kill velocity (kills per second). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.1", Units="s", AdvancedDisplay))
	float KillVelocityWindowSeconds = 6.f;

	/** Max packs to emit per trigger; scales up toward this value as kill velocity rises. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="1", AdvancedDisplay))
	int32 MaxGroupsPerTrigger = 2;

	/** Delay after the last enemy in a pack dies before we return to idle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Pacing", meta=(ClampMin="0.0", Units="s"))
	float PostCombatDelaySeconds = 1.0f;

	/** Maximum number of enemies to spawn per tick when a burst is queued. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="1"))
	int32 MaxSpawnsPerTick = 5;

	/** Minimum distance from the player for dynamic spawns (0 = no minimum). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.0", Units="cm"))
	float MinSpawnDistanceFromPlayer = 0.f;

	/** Avoid spawning on the player's recent traversal path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning")
	bool bAvoidRecentPlayerPath = true;

	/** Radius around recent path samples where spawns are disallowed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.0", Units="cm", EditCondition="bAvoidRecentPlayerPath"))
	float RecentPathAvoidRadius = 600.f;

	/** How many seconds of player movement history to keep. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.1", Units="s", EditCondition="bAvoidRecentPlayerPath", AdvancedDisplay))
	float RecentPathSeconds = 8.0f;

	/** Sample rate for tracking the player's recent path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.1", Units="s", EditCondition="bAvoidRecentPlayerPath", AdvancedDisplay))
	float RecentPathSampleInterval = 0.5f;

	/** Hard cap on stored path samples (oldest are dropped first). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="1", EditCondition="bAvoidRecentPlayerPath", AdvancedDisplay))
	int32 RecentPathMaxSamples = 32;

	/** Avoid spawning directly in the player's forward cone to reduce visible pop-in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning")
	bool bAvoidPlayerForwardSpawnCone = true;

	/** Forward cone angle to avoid (full angle, centered on player forward). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.0", ClampMax="180.0", Units="deg", EditCondition="bAvoidPlayerForwardSpawnCone"))
	float ForwardSpawnConeDegrees = 120.f;

	/** When true, only reject forward-cone spawns if the player has clear line of sight. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(EditCondition="bAvoidPlayerForwardSpawnCone", AdvancedDisplay))
	bool bUseLineOfSightForForwardCone = true;

	/** Attempts to find a valid spawn location before falling back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="1", AdvancedDisplay))
	int32 SpawnLocationSearchAttempts = 12;

	/** Upward offset for ground traces when adjusting spawn Z height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.0", Units="cm", AdvancedDisplay))
	float GroundTraceUpOffset = 120.f;

	/** Downward trace distance to find the floor under a spawn point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="10.0", Units="cm", AdvancedDisplay))
	float GroundTraceDownDistance = 2000.f;

	/** Height applied above the detected ground when placing enemies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Spawning", meta=(ClampMin="0.0", Units="cm", AdvancedDisplay))
	float SpawnGroundOffset = 5.f;

	/** Minimum distance every ordinary Rift spawn must keep from each living player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Rift", meta=(ClampMin="0.0", Units="cm"))
	float RiftMinimumSpawnDistanceFromPlayers = 1200.f;

	/** Server-only cadence for low-pressure anchor activation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Rift", meta=(ClampMin="0.1", Units="s"))
	float RiftPressureEvaluationInterval = 2.f;

	/** Minimum desired number of live Rift enemies before pressure may activate another unopened anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Rift", meta=(ClampMin="0"))
	int32 RiftMinimumActiveEnemyPressure = 8;

	/** Prefer an occluded location first to reduce visible ordinary-enemy pop-in. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Rift")
	bool bRiftPreferHiddenSpawnLocations = true;

	/** When true, enemy tick rates are throttled based on distance to the player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance")
	bool bEnableEnemyLODThrottling = true;

	/** How often to recompute enemy LOD throttling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.05", Units="s", EditCondition="bEnableEnemyLODThrottling"))
	float EnemyLODUpdateInterval = 0.5f;

	/** Distance (cm) within which enemies tick at full rate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="cm", EditCondition="bEnableEnemyLODThrottling"))
	float EnemyLODNearDistance = 4000.f;

	/** Distance (cm) beyond which enemies tick at the mid LOD rate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="cm", EditCondition="bEnableEnemyLODThrottling"))
	float EnemyLODMidDistance = 8000.f;

	/** Distance (cm) beyond which enemies tick at the far LOD rate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="cm", EditCondition="bEnableEnemyLODThrottling"))
	float EnemyLODFarDistance = 12000.f;

	/** Tick interval applied to movement/mesh/perception in the mid LOD band. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="s", EditCondition="bEnableEnemyLODThrottling"))
	float EnemyLODMidTickInterval = 0.1f;

	/** Tick interval applied to movement/mesh/perception in the far LOD band. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="s", EditCondition="bEnableEnemyLODThrottling"))
	float EnemyLODFarTickInterval = 0.25f;

	/** When true, fixed population clusters are slept when far from the player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance")
	bool bEnableFixedClusterSleeping = true;

	/** Distance (cm) beyond which fixed clusters go to sleep. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="cm", EditCondition="bEnableFixedClusterSleeping"))
	float FixedClusterSleepDistance = 14000.f;

	/** Distance (cm) within which sleeping clusters wake up. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Performance", meta=(ClampMin="0.0", Units="cm", EditCondition="bEnableFixedClusterSleeping"))
	float FixedClusterWakeDistance = 11000.f;

	/** Toggle editor/debug rendering of spawn decisions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Debug")
	bool bDrawDebug = false;

	/** How often to emit debug logs when bDrawDebug is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="EncounterDirector|Debug", meta=(ClampMin="0.1", Units="s", EditCondition="bDrawDebug", AdvancedDisplay))
	float DebugLogIntervalSeconds = 1.0f;

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

	/** Total enemies required to complete the current objective. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_ProgressData, Category="EncounterDirector|Progress")
	int32 TotalToKill = 0;

	/** Enemies killed toward the current objective. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_ProgressData, Category="EncounterDirector|Progress")
	int32 KilledCount = 0;

	/** Actual registered enemy deaths; separate from weighted objective points. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_ProgressData, Category="EncounterDirector|Progress")
	int32 EnemiesDefeated = 0;

	/** Weighted points earned from accepted registered deaths. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_ProgressData, Category="EncounterDirector|Progress")
	int32 WeightedProgressPoints = 0;

	/** Frozen weighted target for the active Rift run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_ProgressData, Category="EncounterDirector|Progress")
	int32 WeightedProgressTarget = 0;

	/** True once the boss pawn has actually been spawned. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_BossSpawned, Category="EncounterDirector|Progress")
	bool bBossSpawned = false;

private:
	struct FFixedSpawnGroupEntry;
	struct FFixedSpawnRegionEntry;
	struct FFixedSpawnCluster;
	struct FFixedSpawnRequest;
	struct FRiftRegionPlan;
	struct FRiftSpawnRequest;
	struct FEnemyLODState;
	struct FRecentPlayerSample;

	void RecordKillTimestamp();
	void BuildFixedPopulationPlan();
	void ProcessFixedSpawnQueue();
	AAeyerjiLevelDirector* ResolveObjectiveLevelDirector() const;
	// Recomputes distance-based tick throttling for active enemies.
	void UpdateEnemyLOD(float DeltaSeconds);
	// Sleeps or wakes fixed clusters based on player distance.
	void UpdateFixedClusterLOD(const FVector& PlayerLocation);
	// Applies the requested sleep state to all members of a fixed cluster.
	void ApplyFixedClusterSleepState(int32 ClusterId, bool bSleep);
	// Enables or disables ticking and AI for a single enemy when sleeping.
	void ApplyEnemySleepState(AEnemyParentNative* Enemy, bool bSleep);
	// Applies the selected LOD bucket to an enemy's ticking components.
	void ApplyEnemyLODBucket(AEnemyParentNative* Enemy, FEnemyLODState& State, uint8 NewBucket);
	// Caches baseline tick settings the first time an enemy is seen.
	FEnemyLODState& GetOrCreateEnemyLODState(AEnemyParentNative* Enemy);
	// Removes cached LOD state for a destroyed enemy.
	void RemoveEnemyLODState(AActor* Enemy);
	// Removes an enemy from its fixed cluster membership list.
	void RemoveFixedClusterMember(int32 ClusterId, AActor* Enemy);
	const UEnemySpawnGroupDefinition* ChooseFixedSpawnGroup(const TArray<FFixedSpawnGroupEntry>& Groups);
	bool ResolveFixedClusterCenter(const FFixedSpawnRegionEntry* RegionEntry, const TArray<FVector>& ExistingCenters, float MinSpacing, FVector& OutCenter);
	FVector ResolveFixedSpawnLocation(const FVector& ClusterCenter, float Radius, float HalfHeight, const FBox& RegionBounds, bool bHasRegion);
	void RegisterFixedClusterEnemy(AEnemyParentNative* Enemy, int32 ClusterId);
	void HandleFixedPopulationEnemyRemoved(AActor* Enemy);
	void HandleFixedPopulationClusterDecrement(int32 ClusterId);
	void NotifyFixedPopulationInitialSpawnComplete();
	void ResetRiftRegionRun();
	void ProcessRiftRegionActivation();
	void ProcessRiftSpawnQueue();
	bool TryActivateRiftEncounterGroup(int32 PlanIndex, APawn* Participant, const TCHAR* ActivationReason);
	int32 FindRiftPressureActivationCandidate(APawn*& OutParticipant) const;
	int32 GetActiveRiftEnemyPressure() const;
	void GetLivingRiftParticipants(TArray<APawn*>& OutParticipants) const;
	bool ResolveRiftRegionAnchor(const FBox& Bounds, FVector& OutAnchor);
	bool IsRiftRegionReachableFromParticipant(const FVector& RegionAnchor, const APawn* Participant) const;
	APawn* ResolveNearestLiveParticipant(const FVector& FromLocation) const;
	bool ResolveRiftSpawnLocation(const FRiftRegionPlan& Plan, float HalfHeight, const APawn* Participant,
		FVector& OutLocation, FString& OutRejectReason);
	bool IsRiftSpawnLocationSafe(const FVector& Candidate, const TArray<APawn*>& LivingParticipants,
		bool& bOutVisibleToParticipant, FString& OutRejectReason) const;
	bool SpawnRiftRequest(FRiftSpawnRequest& Request);
	FGameplayTag ResolveArchetypeTagFromClass(TSubclassOf<AEnemyParentNative> EnemyClass) const;
	void ResetProgress(int32 NewTotal);
	void UpdateTotalToKill(int32 NewTotal);
	void IncrementKillCount();
	void HandleProgressChanged();

	UFUNCTION()
	void HandleProgressEnemyDied(AActor* DeadEnemy);

	UFUNCTION()
	void HandleProgressEnemyDestroyed(AActor* DestroyedActor);

private:
	struct FFixedSpawnGroupEntry
	{
		TWeakObjectPtr<const UEnemySpawnGroupDefinition> Group;
		float Weight = 1.0f;
	};

	struct FFixedSpawnRegionEntry
	{
		TWeakObjectPtr<AAeyerjiSpawnRegion> Region;
		FBox Bounds = FBox(EForceInit::ForceInit);
		float Weight = 1.0f;
		float DensityScale = 1.0f;
		float EliteChanceBonus = 0.0f;
		float RadiusScale = 1.0f;
		bool bAllowElites = true;
	};

	struct FFixedSpawnCluster
	{
		int32 ClusterId = INDEX_NONE;
		FVector Center = FVector::ZeroVector;
		float Radius = 0.f;
		FBox RegionBounds = FBox(EForceInit::ForceInit);
		bool bHasRegion = false;
		float DensityAlpha = 0.f;
		float EliteChanceBonus = 0.f;
		bool bDenseCluster = false;
		bool bAllowElites = true;
		bool bSleeping = false;
		int32 TotalEnemies = 0;
		int32 RemainingEnemies = 0;
	};

	struct FEnemyLODState
	{
		// Tracks baseline tick settings and LOD state for an enemy.
		bool bInitialized = false;
		bool bCachedMovement = false;
		bool bCachedMesh = false;
		bool bCachedPerception = false;
		bool bSleeping = false;
		bool bPausedByLOD = false;
		uint8 LODBucket = 255;
		float BaseMovementTickInterval = 0.f;
		bool bMovementTickEnabled = true;
		float BaseMeshTickInterval = 0.f;
		bool bMeshTickEnabled = true;
		float BasePerceptionTickInterval = 0.f;
		bool bPerceptionTickEnabled = true;
	};

	struct FFixedSpawnRequest
	{
		TWeakObjectPtr<const UEnemySpawnGroupDefinition> Group;
		FVector ClusterCenter = FVector::ZeroVector;
		float ClusterRadius = 0.f;
		float DensityAlpha = 0.f;
		float EliteChanceBonus = 0.f;
		bool bDenseCluster = false;
		bool bAllowElites = true;
		int32 ClusterId = INDEX_NONE;
	};

	struct FRiftRegionPlan
	{
		TWeakObjectPtr<AAeyerjiSpawnRegion> Region;
		TWeakObjectPtr<const UEnemySpawnGroupDefinition> EncounterGroup;
		FBox Bounds = FBox(EForceInit::ForceInit);
		FVector Anchor = FVector::ZeroVector;
		FString StableKey;
		float Weight = 1.f;
		int32 Budget = 0;
		int32 ReservedProgress = 0;
		bool bConsumed = false;
	};

	struct FRiftSpawnRequest
	{
		TSubclassOf<AEnemyParentNative> EnemyClass;
		int32 RegionPlanIndex = INDEX_NONE;
		int32 ProgressPoints = 1;
		bool bIsElite = false;
		int32 FailedAttempts = 0;
	};

	struct FRecentPlayerSample
	{
		FVector Location = FVector::ZeroVector;
		double Timestamp = 0.0;
	};

	TWeakObjectPtr<APawn> CachedPlayerPawn;
	TWeakObjectPtr<AController> CachedPlayerController;
	TWeakObjectPtr<const UEnemySpawnGroupDefinition> LastSpawnedGroup;
	TArray<TWeakObjectPtr<AActor>> LiveEnemies;
	TArray<TWeakObjectPtr<AActor>> ProgressOnlyEnemies;
	TMap<TWeakObjectPtr<AActor>, int32> RegisteredProgressEnemyPoints;
	int32 WeightedProgressRunSerial = 0;
	bool bWeightedProgressFrozen = false;
	TArray<double> KillTimestampHistory;
	TArray<FRecentPlayerSample> RecentPlayerSamples;
	TArray<TWeakObjectPtr<const UEnemySpawnGroupDefinition>> PendingSpawnRequests;
	TArray<FFixedSpawnRequest> FixedSpawnQueue;
	TArray<FRiftRegionPlan> RiftRegionPlans;
	TArray<TArray<FRiftSpawnRequest>> RiftReservedRegionRequests;
	TArray<FRiftSpawnRequest> RiftSpawnQueue;
	/** Prevents one stationary participant from consuming every nearby/overlapping region on consecutive ticks. */
	TMap<TWeakObjectPtr<APawn>, TWeakObjectPtr<AAeyerjiSpawnRegion>> RiftParticipantRegionLatch;
	TArray<FVector> FixedClusterCenters;
	TMap<int32, FFixedSpawnCluster> FixedClusters;
	TMap<TWeakObjectPtr<AActor>, int32> FixedEnemyClusterMap;
	TMap<int32, TArray<TWeakObjectPtr<AEnemyParentNative>>> FixedClusterMembers;
	TMap<TWeakObjectPtr<AEnemyParentNative>, FEnemyLODState> EnemyLODStates;
	TWeakObjectPtr<UAeyerjiWorldSpawnProfile> FixedSpawnProfile;
	TWeakObjectPtr<AAeyerjiSpawnerGroup> FixedPopulationSpawner;
	TWeakObjectPtr<AAeyerjiLevelDirector> FixedPopulationLevelDirector;
	TWeakObjectPtr<AAeyerjiSpawnerGroup> RiftPopulationSpawner;
	TWeakObjectPtr<AAeyerjiLevelDirector> RiftLevelDirector;
	FRandomStream FixedSpawnStream;
	FRandomStream RiftSpawnStream;
	float EnemyLODTimeAccumulator = 0.f;
	int32 FixedPopulationTarget = 0;
	int32 FixedPopulationSpawned = 0;
	int32 FixedPopulationRemaining = 0;
	int32 FixedClustersRemaining = 0;
	int32 FixedSpawnSeed = 0;
	bool bFixedPopulationActive = false;
	bool bFixedPopulationInitialSpawnComplete = false;
	bool bFixedPopulationComplete = false;
	bool bSpawnedPopulationSpawner = false;
	bool bSpawnedRiftPopulationSpawner = false;
	bool bRiftRegionActivationEnabled = false;
	float RiftRegionActivationDistance = 2500.f;
	float RiftEliteRateMultiplier = 1.f;
	float RiftProgressMultiplier = 1.f;
	double NextRiftPressureEvaluationTime = 0.0;
	int32 RiftRegionRunSerial = 0;
	FVector LastEncounterLocation = FVector::ZeroVector;
	double LastEncounterTimestamp = 0.0;
	double LastKillTimestamp = 0.0;
	double PostCombatTimeRemaining = 0.0;
	double LastPathSampleTimestamp = 0.0;
	double LastDebugLogTimestamp = -1.0;
	int32 LastBroadcastKilled = INDEX_NONE;
	int32 LastBroadcastTotal = INDEX_NONE;
	bool bLastBroadcastBossSpawned = false;

	UFUNCTION()
	void OnRep_ProgressData();

	UFUNCTION()
	void OnRep_BossSpawned();
};
