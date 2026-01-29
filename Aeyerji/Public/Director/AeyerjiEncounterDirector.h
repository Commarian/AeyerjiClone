// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameFramework/Actor.h"
#include "Engine/DataAsset.h"
#include "AeyerjiEncounterDirector.generated.h"

class AEnemyParentNative;
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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FFixedClusterClearedSignature, int32, ClusterId, float, DensityAlpha, bool, bDenseCluster);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFixedPopulationClearedSignature);

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

public:
	/** Fired when a fixed population cluster is cleared. */
	UPROPERTY(BlueprintAssignable, Category="EncounterDirector|FixedPopulation")
	FFixedClusterClearedSignature OnFixedClusterCleared;

	/** Fired when every fixed population cluster is cleared. */
	UPROPERTY(BlueprintAssignable, Category="EncounterDirector|FixedPopulation")
	FFixedPopulationClearedSignature OnFixedPopulationCleared;

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

private:
	struct FFixedSpawnGroupEntry;
	struct FFixedSpawnRegionEntry;
	struct FFixedSpawnCluster;
	struct FFixedSpawnRequest;
	struct FEnemyLODState;
	struct FRecentPlayerSample;

	void RecordKillTimestamp();
	void BuildFixedPopulationPlan();
	void ProcessFixedSpawnQueue();
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
	FGameplayTag ResolveArchetypeTagFromClass(TSubclassOf<AEnemyParentNative> EnemyClass) const;

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

	struct FRecentPlayerSample
	{
		FVector Location = FVector::ZeroVector;
		double Timestamp = 0.0;
	};

	TWeakObjectPtr<APawn> CachedPlayerPawn;
	TWeakObjectPtr<AController> CachedPlayerController;
	TWeakObjectPtr<const UEnemySpawnGroupDefinition> LastSpawnedGroup;
	TArray<TWeakObjectPtr<AActor>> LiveEnemies;
	TArray<double> KillTimestampHistory;
	TArray<FRecentPlayerSample> RecentPlayerSamples;
	TArray<TWeakObjectPtr<const UEnemySpawnGroupDefinition>> PendingSpawnRequests;
	TArray<FFixedSpawnRequest> FixedSpawnQueue;
	TArray<FVector> FixedClusterCenters;
	TMap<int32, FFixedSpawnCluster> FixedClusters;
	TMap<TWeakObjectPtr<AActor>, int32> FixedEnemyClusterMap;
	TMap<int32, TArray<TWeakObjectPtr<AEnemyParentNative>>> FixedClusterMembers;
	TMap<TWeakObjectPtr<AEnemyParentNative>, FEnemyLODState> EnemyLODStates;
	TWeakObjectPtr<UAeyerjiWorldSpawnProfile> FixedSpawnProfile;
	TWeakObjectPtr<AAeyerjiSpawnerGroup> FixedPopulationSpawner;
	TWeakObjectPtr<AAeyerjiLevelDirector> FixedPopulationLevelDirector;
	FRandomStream FixedSpawnStream;
	float EnemyLODTimeAccumulator = 0.f;
	int32 FixedPopulationTarget = 0;
	int32 FixedPopulationSpawned = 0;
	int32 FixedPopulationRemaining = 0;
	int32 FixedClustersRemaining = 0;
	int32 FixedSpawnSeed = 0;
	bool bFixedPopulationActive = false;
	bool bFixedPopulationComplete = false;
	bool bSpawnedPopulationSpawner = false;
	FVector LastEncounterLocation = FVector::ZeroVector;
	double LastEncounterTimestamp = 0.0;
	double LastKillTimestamp = 0.0;
	double PostCombatTimeRemaining = 0.0;
	double LastPathSampleTimestamp = 0.0;
	double LastDebugLogTimestamp = -1.0;
};
